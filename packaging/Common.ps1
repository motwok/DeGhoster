# Copyright (c) Emmo Emminghaus. SPDX-License-Identifier: AGPL-3.0-or-later
#
# Shared helpers for the DeGhoster packaging scripts (ZIP + MSI). Dot-source
# this file:  . "$PSScriptRoot\Common.ps1"

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Repo root is the parent of the packaging/ directory.
$script:RepoRoot = Split-Path -Parent $PSScriptRoot
$script:BuildDir = Join-Path $RepoRoot 'build'
$script:DistDir  = Join-Path $RepoRoot 'dist'

# Runtime payload that ships in both the ZIP and the MSI (files that live
# directly in build/, i.e. not the per-culture .mui folders which are added
# separately).
$script:CorePayload = @(
    'DeGhoster.exe'
    'DeGhoster.Hook64.dll'
    'DeGhoster.Hook32.dll'
    'DeGhoster.Helper32.exe'
    'LICENSE.txt'
    'NOTICE.txt'
)

function Get-DeGhosterVersion {
    <#
      Reads build/version.json (written by cmake/Version.cmake). Falls back to
      running GitVersion directly if the file is missing.
    #>
    $vjson = Join-Path $BuildDir 'version.json'
    if (Test-Path $vjson) {
        return Get-Content $vjson -Raw | ConvertFrom-Json
    }
    Write-Warning "build/version.json not found; running GitVersion directly."
    Push-Location $RepoRoot
    try {
        $json = & dotnet-gitversion 2>$null
        if ($LASTEXITCODE -ne 0 -or -not $json) { $json = & dotnet gitversion 2>$null }
        $gv = $json | ConvertFrom-Json
        return [pscustomobject]@{
            informationalVersion = $gv.InformationalVersion
            fullSemVer           = $gv.FullSemVer
            majorMinorPatch      = $gv.MajorMinorPatch
            fileVersion          = "$($gv.Major).$($gv.Minor).$($gv.Patch).$($gv.CommitsSinceVersionSource)"
            major                = $gv.Major
            minor                = $gv.Minor
            patch                = $gv.Patch
            revision             = [int]$gv.CommitsSinceVersionSource
            shortSha             = $gv.ShortSha
        }
    } finally { Pop-Location }
}

function Get-InstalledCultures {
    <#
      Returns the culture folders in build/ that carry a DeGhoster.exe.mui
      (e.g. en-US, de-DE, ...), sorted, culture code as the string.
    #>
    Get-ChildItem -Path $BuildDir -Directory |
        Where-Object { Test-Path (Join-Path $_.FullName 'DeGhoster.exe.mui') } |
        Where-Object { $_.Name -match '^[A-Za-z]{2,3}(-[A-Za-z0-9]+)*$' } |
        Select-Object -ExpandProperty Name |
        Sort-Object
}

function Assert-BuildOutput {
    <# Verifies the required binaries exist before packaging. #>
    foreach ($f in $CorePayload) {
        $p = Join-Path $BuildDir $f
        if (-not (Test-Path $p)) {
            throw "Missing build artifact '$f'. Build first (see build.ps1 / README)."
        }
    }
    $cultures = @(Get-InstalledCultures)
    if ($cultures -notcontains 'en-US') {
        throw "en-US\DeGhoster.exe.mui missing - the x64 build did not produce the MUI fallback."
    }
    return $cultures
}

function New-StageDir {
    <#
      Copies the runtime payload into a clean staging directory and returns its
      path. Both the ZIP and the MSI are built from this, so they stay in sync.
    #>
    $stage = Join-Path $BuildDir 'stage'
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
    New-Item -ItemType Directory -Path $stage | Out-Null

    foreach ($f in $CorePayload) {
        Copy-Item (Join-Path $BuildDir $f) (Join-Path $stage $f)
    }
    foreach ($c in Get-InstalledCultures) {
        $dst = Join-Path $stage $c
        New-Item -ItemType Directory -Path $dst | Out-Null
        Copy-Item (Join-Path (Join-Path $BuildDir $c) 'DeGhoster.exe.mui') (Join-Path $dst 'DeGhoster.exe.mui')
    }
    return $stage
}

function ConvertTo-Rtf {
    <# Converts a plain-text file to a minimal RTF document (for the WixUI EULA). #>
    param([Parameter(Mandatory)][string]$InputPath,
          [Parameter(Mandatory)][string]$OutputPath)

    $text = Get-Content -Path $InputPath -Raw
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.Append('{\rtf1\ansi\ansicpg1252\deff0{\fonttbl{\f0\fnil\fcharset0 Segoe UI;}}')
    [void]$sb.Append('\fs18' + "`r`n")
    foreach ($ch in $text.ToCharArray()) {
        switch ($ch) {
            "`r" { }
            "`n" { [void]$sb.Append('\par ') }
            "`t" { [void]$sb.Append('\tab ') }
            '\'  { [void]$sb.Append('\\') }
            '{'  { [void]$sb.Append('\{') }
            '}'  { [void]$sb.Append('\}') }
            default {
                $code = [int][char]$ch
                if ($code -gt 127) { [void]$sb.Append('\u' + $code + '?') }
                else               { [void]$sb.Append($ch) }
            }
        }
    }
    [void]$sb.Append('}')
    Set-Content -Path $OutputPath -Value $sb.ToString() -Encoding ascii -NoNewline
}

function New-LanguageFragment {
    <#
      Generates the WiX fragment describing the per-culture .mui folders,
      components and features from the staged cultures. en-US goes into the
      always-installed core group; every other culture becomes a selectable
      feature under LanguageFeatures.
    #>
    param([Parameter(Mandatory)][string]$StageDir,
          [Parameter(Mandatory)][string]$OutputPath)

    $cultures = @(Get-InstalledCultures)
    $nl = "`r`n"
    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.Append('<?xml version="1.0" encoding="UTF-8"?>' + $nl)
    [void]$sb.Append('<!-- GENERATED by packaging/Common.ps1 - do not edit. -->' + $nl)
    [void]$sb.Append('<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">' + $nl)

    # 1) Directories for every culture under APPLICATIONFOLDER.
    [void]$sb.Append('  <Fragment>' + $nl)
    [void]$sb.Append('    <DirectoryRef Id="APPLICATIONFOLDER">' + $nl)
    foreach ($c in $cultures) {
        $id = $c -replace '-', '_'
        [void]$sb.Append("      <Directory Id=`"dir_$id`" Name=`"$c`" />" + $nl)
    }
    [void]$sb.Append('    </DirectoryRef>' + $nl)
    [void]$sb.Append('  </Fragment>' + $nl)

    # 2) en-US mui: always-installed core component group.
    [void]$sb.Append('  <Fragment>' + $nl)
    [void]$sb.Append('    <ComponentGroup Id="CoreLangComponents">' + $nl)
    if ($cultures -contains 'en-US') {
        $src = Join-Path $StageDir 'en-US\DeGhoster.exe.mui'
        [void]$sb.Append('      <Component Directory="dir_en_US" Id="cmp_mui_en_US">' + $nl)
        [void]$sb.Append("        <File Id=`"fil_mui_en_US`" Source=`"$src`" />" + $nl)
        [void]$sb.Append('      </Component>' + $nl)
    }
    [void]$sb.Append('    </ComponentGroup>' + $nl)
    [void]$sb.Append('  </Fragment>' + $nl)

    # 3) Selectable features for the remaining cultures.
    [void]$sb.Append('  <Fragment>' + $nl)
    [void]$sb.Append('    <FeatureGroup Id="LanguageFeatures">' + $nl)
    foreach ($c in $cultures) {
        if ($c -eq 'en-US') { continue }
        $id = $c -replace '-', '_'
        $src = Join-Path $StageDir "$c\DeGhoster.exe.mui"
        $title = $c
        try { $title = "$([System.Globalization.CultureInfo]::GetCultureInfo($c).EnglishName) ($c)" } catch { }
        $title = [System.Security.SecurityElement]::Escape($title)
        [void]$sb.Append("      <Feature Id=`"lang_$id`" Title=`"$title`" Level=`"1`" AllowAdvertise=`"no`">" + $nl)
        [void]$sb.Append("        <Component Directory=`"dir_$id`" Id=`"cmp_mui_$id`">" + $nl)
        [void]$sb.Append("          <File Id=`"fil_mui_$id`" Source=`"$src`" />" + $nl)
        [void]$sb.Append('        </Component>' + $nl)
        [void]$sb.Append('      </Feature>' + $nl)
    }
    [void]$sb.Append('    </FeatureGroup>' + $nl)
    [void]$sb.Append('  </Fragment>' + $nl)

    [void]$sb.Append('</Wix>' + $nl)
    Set-Content -Path $OutputPath -Value $sb.ToString() -Encoding UTF8 -NoNewline
}
