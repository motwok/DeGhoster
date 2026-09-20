# Copyright (c) Emmo Emminghaus. SPDX-License-Identifier: AGPL-3.0-or-later

param([string]$OutDir)

. "$PSScriptRoot\Common.ps1"

# --- prerequisites --------------------------------------------------------
$wix = Get-Command wix -ErrorAction SilentlyContinue
if (-not $wix) {
    throw "The WiX CLI ('wix') was not found. Install it with:  dotnet tool install --global wix"
}
$exts = ($(& wix extension list -g 2>$null) -join "`n")
if ($exts -notmatch 'WixToolset\.UI\.wixext') {
    throw "The WiX UI extension is missing. Add it with:  wix extension add -g WixToolset.UI.wixext"
}
if ($exts -notmatch 'WixToolset\.Util\.wixext') {
    throw "The WiX Util extension is missing. Add it with:  wix extension add -g WixToolset.Util.wixext"
}

$cultures = Assert-BuildOutput
$ver = Get-DeGhosterVersion
if (-not $OutDir) { $OutDir = $DistDir }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$assetsDir = Join-Path $RepoRoot 'assets'
$wxsMain   = Join-Path $PSScriptRoot 'wix\DeGhoster.wxs'
$wxsLang   = Join-Path $BuildDir 'Languages.generated.wxs'

Write-Host "Staging payload ($($cultures.Count) UI languages incl. en-US)..."
$stage = New-StageDir

Write-Host "Generating EULA (License.rtf) and language fragment..."
ConvertTo-Rtf -InputPath (Join-Path $stage 'LICENSE.txt') -OutputPath (Join-Path $stage 'License.rtf')
New-LanguageFragment -StageDir $stage -OutputPath $wxsLang

$verTag = ($ver.fullSemVer -replace '[+]', '.') -replace '[^A-Za-z0-9._-]', '-'
$msiName = "DeGhoster-$verTag-win-x64.msi"
$msiPath = Join-Path $OutDir $msiName
$versionCsv = "$($ver.major),$($ver.minor),$($ver.patch),$($ver.revision)"

# Project URL for ARP metadata: the git remote if present, else the canonical repo.
$projectUrl = 'https://github.com/motwok/DeGhoster'
try {
    $remote = & git -C $RepoRoot config --get remote.origin.url 2>$null
    if ($LASTEXITCODE -eq 0 -and $remote) {
        $projectUrl = ($remote -replace '\.git$', '') -replace '^git@github\.com:', 'https://github.com/'
    }
} catch { }

Write-Host "Building $msiName (ProductVersion $($ver.majorMinorPatch)) ..."
$wixArgs = @(
    'build'
    '-arch', 'x64'
    '-ext', 'WixToolset.UI.wixext'
    '-ext', 'WixToolset.Util.wixext'
    '-d', "Version=$($ver.majorMinorPatch)"
    '-d', "VersionCsv=$versionCsv"
    '-d', "ProductVersion4=$($ver.informationalVersion)"
    '-d', "StageDir=$stage"
    '-d', "AssetsDir=$assetsDir"
    '-d', "ProjectUrl=$projectUrl"
    '-o', $msiPath
    $wxsMain
    $wxsLang
)
& wix @wixArgs
if ($LASTEXITCODE -ne 0) { throw "wix build failed (exit $LASTEXITCODE)." }

$size = [math]::Round((Get-Item $msiPath).Length / 1MB, 2)
Write-Host "MSI ready: $msiPath ($size MB)" -ForegroundColor Green
