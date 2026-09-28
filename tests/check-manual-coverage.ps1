# Copyright (c) Emmo Emminghaus. SPDX-License-Identifier: AGPL-3.0-or-later

<#
.SYNOPSIS
  Fails when the committed manual coverage run is older than the code.

.DESCRIPTION
  tests\coverage-manual.ps1 records, next to its results, the git blob id of every
  file under src\ as it was during the run (tests\coverage-data\manual.fingerprint.json).
  This compares that record with src\ at -Rev: any file changed, added or removed
  since the run means the manual test has to be done again.

  With -Base, a change that does not touch src\ at all passes: there is no new
  code for a manual run to cover. The "Manual test" workflow passes the PR's base
  commit, and branch protection makes the branch merge master first, so the code
  checked here is the code that lands.

  Runs on Windows PowerShell 5.1 and PowerShell 7.
#>
[CmdletBinding()]
param(
    # The commit to check; in a pull_request workflow HEAD is the merge result.
    [string]$Rev = 'HEAD',
    # The commit the change is measured against (the PR's base).
    [string]$Base,
    # Read the fingerprint from this file instead of from -Rev.
    [string]$Fingerprint
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$fpPath = 'tests/coverage-data/manual.fingerprint.json'

function Write-Result([string]$verdict, [string[]]$lines) {
    $lines | ForEach-Object { Write-Host $_ }
    if ($env:GITHUB_STEP_SUMMARY) {
        $md = @("### Manual test: $verdict", '') + $lines
        Add-Content -Path $env:GITHUB_STEP_SUMMARY -Value $md -Encoding utf8
    }
}

if ($Base) {
    git -C $repo diff --quiet $Base $Rev -- src
    if ($LASTEXITCODE -eq 0) {
        Write-Result 'not needed' @('This change does not touch `src/`, so no manual run is required.')
        exit 0
    }
    if ($LASTEXITCODE -ne 1) { throw "git diff $Base $Rev failed." }
}

# The code as it is at -Rev: path -> blob id.
$tree = @{}
foreach ($line in @(git -C $repo ls-tree -r $Rev -- src)) {
    $meta, $path = $line -split "`t", 2
    $tree[$path] = ($meta -split ' ')[2]
}
if ($LASTEXITCODE -ne 0) { throw "git ls-tree $Rev failed." }

# The code as it was during the manual run.
$json = if ($Fingerprint) {
    Get-Content -LiteralPath $Fingerprint -Raw
} else {
    # A missing file is an expected outcome here; Windows PowerShell would turn
    # git's message on stderr into a terminating error under 'Stop'.
    $ErrorActionPreference = 'Continue'
    $raw = @(git -C $repo show "$($Rev):$fpPath" 2>$null)
    $ok = $LASTEXITCODE -eq 0
    $ErrorActionPreference = 'Stop'
    if ($ok) { $raw -join "`n" } else { $null }
}
if (-not $json) {
    Write-Result 'missing' @("No manual coverage run is recorded (``$fpPath``).",
        '', 'Run `tests\coverage-manual.ps1` and commit `tests\coverage-data`.')
    exit 1
}
$run = @{}
foreach ($p in (ConvertFrom-Json $json).PSObject.Properties) { $run[$p.Name] = $p.Value }

$changed = @($tree.Keys | Where-Object { $run.ContainsKey($_) -and $run[$_] -ne $tree[$_] } | Sort-Object)
$added   = @($tree.Keys | Where-Object { -not $run.ContainsKey($_) } | Sort-Object)
$removed = @($run.Keys | Where-Object { $_ -like 'src/*' -and -not $tree.ContainsKey($_) } | Sort-Object)

if (-not ($changed.Count + $added.Count + $removed.Count)) {
    Write-Result 'current' @("The committed manual run matches all $($tree.Count) files under ``src/``.")
    exit 0
}

$lines = @('The committed manual run is older than the code. Changed since the run:', '')
$lines += $changed | ForEach-Object { "* ``$_`` (changed)" }
$lines += $added   | ForEach-Object { "* ``$_`` (new)" }
$lines += $removed | ForEach-Object { "* ``$_`` (removed)" }
$lines += '', 'Run `tests\coverage-manual.ps1` on this code and commit `tests\coverage-data`.'
Write-Result 'outdated' $lines
exit 1
