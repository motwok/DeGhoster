# Copyright (c) Emmo Emminghaus. SPDX-License-Identifier: AGPL-3.0-or-later

<#
.SYNOPSIS
  Makes an OpenCppCoverage Cobertura report mergeable across machines.

.DESCRIPTION
  OpenCppCoverage writes absolute paths (C:\KIProjekte\... locally,
  D:\a\DeGhoster\... on the build server), so reports from different machines
  never line up. This rewrites every file name to its repo-relative form
  (src/DeGhoster/CursorOverlay.cpp), every module name to the bare file name,
  and drops files outside the repo.

  -CoveredOnly keeps only the lines that were hit: a report that is merged into
  another one only has to add hits, and it stays small enough to commit.

  -Fingerprint writes the git blob id of every source file in the report, so a
  merge can leave out the results for a file that has changed since.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$In,
    [Parameter(Mandatory)][string]$Out,
    [switch]$CoveredOnly,
    [string]$Fingerprint,
    # The commit the measured binaries were built from, for a report recorded
    # earlier; by default the files as they are in the working tree.
    [string]$FingerprintAt
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

# "a\DeGhoster\DeGhoster\src\DeGhoster\X.cpp" -> "src/DeGhoster/X.cpp": the
# first "\src\" whose remainder is a file in this repo.
function ConvertTo-RepoPath([string]$file) {
    $f = $file.Replace('/', '\')
    $i = -1
    while (($i = $f.IndexOf('\src\', $i + 1, [StringComparison]::OrdinalIgnoreCase)) -ge 0) {
        $rel = $f.Substring($i + 1)
        if (Test-Path -LiteralPath (Join-Path $repo $rel)) { return $rel.Replace('\', '/') }
    }
    if ($f.StartsWith('src\', [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath (Join-Path $repo $f))) {
        return $f.Replace('\', '/')
    }
    return $null
}

[xml]$x = Get-Content -LiteralPath $In -Raw
$sources = $x.SelectSingleNode('/coverage/sources')
$sources.RemoveAll()
$src = $x.CreateElement('source'); $src.InnerText = '.'
[void]$sources.AppendChild($src)

$files = New-Object 'System.Collections.Generic.HashSet[string]'
foreach ($pkg in @($x.coverage.packages.package)) {
    $pkg.SetAttribute('name', [IO.Path]::GetFileName($pkg.GetAttribute('name')))
    foreach ($cls in @($pkg.classes.class)) {
        if (-not $cls) { continue }
        $rel = ConvertTo-RepoPath $cls.GetAttribute('filename')
        if (-not $rel) { [void]$cls.ParentNode.RemoveChild($cls); continue }
        $cls.SetAttribute('filename', $rel)
        [void]$files.Add($rel)
        if ($CoveredOnly) {
            foreach ($line in @($cls.lines.line)) {
                if ($line -and $line.GetAttribute('hits') -eq '0') { [void]$line.ParentNode.RemoveChild($line) }
            }
        }
    }
}

$outPath = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path (Get-Location) $Out }))
New-Item -ItemType Directory -Path (Split-Path $outPath) -Force | Out-Null
$settings = New-Object Xml.XmlWriterSettings
$settings.Indent = $true
$settings.Encoding = New-Object Text.UTF8Encoding($false)
$w = [Xml.XmlWriter]::Create($outPath, $settings)
try { $x.Save($w) } finally { $w.Dispose() }

if ($Fingerprint) {
    # git hash-object applies the same clean filter (line endings) as git add, so
    # the ids match the committed blobs on every machine.
    $sorted = @($files | Sort-Object)
    $ids = if ($FingerprintAt) {
        @($sorted | ForEach-Object { git -C $repo rev-parse "$($FingerprintAt):$_" })
    } else {
        # Paths as arguments, not on stdin: Windows PowerShell prefixes piped text
        # with a byte order mark, which turns the first path into a missing file.
        @(git -C $repo hash-object -- $sorted)
    }
    $map = [ordered]@{}
    for ($i = 0; $i -lt $sorted.Count; $i++) { $map[$sorted[$i]] = $ids[$i] }
    $fpPath = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($Fingerprint)) { $Fingerprint } else { Join-Path (Get-Location) $Fingerprint }))
    [IO.File]::WriteAllText($fpPath, ($map | ConvertTo-Json), (New-Object Text.UTF8Encoding($false)))
}
