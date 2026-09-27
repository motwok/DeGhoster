# Copyright (c) Emmo Emminghaus. SPDX-License-Identifier: AGPL-3.0-or-later

<#
.SYNOPSIS
  Builds the two coverage reports: the CI's own tests, and the same combined with
  the tests that cannot run on the build server.

.DESCRIPTION
  Some tests need a real desktop with a mouse cursor (category InteractiveInput)
  or a human (tests\coverage-manual.ps1). They run locally, and their results are
  committed under tests\coverage-data (tests\coverage.ps1 and coverage-manual.ps1
  write them). This script

    * normalizes the CI report (-Ci) with coverage-normalize.ps1,
    * leaves out the committed results for every source file that has changed since
      they were recorded (its line numbers would no longer fit) and says which,
    * writes coverage\report-ci (CI tests only) and coverage\report-combined (CI +
      local + manual) with ReportGenerator, and
    * adds both totals to the GitHub job summary when run in Actions.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Ci,
    [string]$OutDir = 'coverage'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$out = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path (Get-Location) $OutDir }))
New-Item -ItemType Directory -Path $out -Force | Out-Null

$ciNorm = Join-Path $out 'ci.cobertura.xml'
& (Join-Path $PSScriptRoot 'coverage-normalize.ps1') -In $Ci -Out $ciNorm

# The committed results, without the files that changed since.
$extras = @()
$notes = @()
$dataDir = Join-Path $PSScriptRoot 'coverage-data'
foreach ($report in @(Get-ChildItem $dataDir -Filter '*.cobertura.xml' -ErrorAction SilentlyContinue)) {
    $name = $report.Name -replace '\.cobertura\.xml$', ''
    $fpFile = Join-Path $dataDir "$name.fingerprint.json"
    if (-not (Test-Path $fpFile)) { $notes += "* ``$name``: no fingerprint, left out"; continue }
    $fp = Get-Content $fpFile -Raw | ConvertFrom-Json
    $stale = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($p in $fp.PSObject.Properties) {
        $path = Join-Path $repo $p.Name
        $now = if (Test-Path $path) { git -C $repo hash-object -- $path } else { '' }
        if ($now -ne $p.Value) { [void]$stale.Add($p.Name) }
    }
    [xml]$x = Get-Content $report.FullName -Raw
    foreach ($cls in @($x.SelectNodes('//class'))) {
        if ($stale.Contains($cls.GetAttribute('filename'))) { [void]$cls.ParentNode.RemoveChild($cls) }
    }
    $filtered = Join-Path $out "$name.cobertura.xml"
    $x.Save($filtered)
    $extras += $filtered
    if ($stale.Count) {
        $notes += "* ``$name``: left out for changed files (re-run the local coverage): " + (($stale | Sort-Object) -join ', ')
    } else {
        $notes += "* ``$name``: included"
    }
}

function New-Report([string[]]$reports, [string]$dir, [string]$title) {
    Push-Location $repo
    try {
        dotnet tool run reportgenerator "-reports:$($reports -join ';')" "-targetdir:$dir" `
            "-reporttypes:Html;TextSummary" "-sourcedirs:$repo" "-title:$title" "-verbosity:Warning"
        if ($LASTEXITCODE -ne 0) { throw "ReportGenerator failed for $title." }
    } finally { Pop-Location }
    $summary = Get-Content (Join-Path $dir 'Summary.txt') -Raw
    if ($summary -match 'Line coverage:\s*([\d.,]+%)') { return $Matches[1] }
    return '?'
}

$ciPct = New-Report @($ciNorm) (Join-Path $out 'report-ci') 'DeGhoster - CI tests'
$allPct = New-Report (@($ciNorm) + $extras) (Join-Path $out 'report-combined') 'DeGhoster - CI + local + manual tests'

$md = @(
    '### Coverage',
    '',
    '| Report | Line coverage |',
    '|---|---|',
    "| CI tests only (``report-ci``) | $ciPct |",
    "| CI + local + manual tests (``report-combined``) | $allPct |",
    ''
) + $(if ($notes) { @('Committed local results (tests/coverage-data):', '') + $notes } else { @('No committed local results in tests/coverage-data.') })
$md | ForEach-Object { Write-Host $_ }
if ($env:GITHUB_STEP_SUMMARY) { $md | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8 }
