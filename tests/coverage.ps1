# Copyright (c) Emmo Emminghaus. SPDX-License-Identifier: AGPL-3.0-or-later

[CmdletBinding()]
param([string]$Config = 'Debug')

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Resolve-CMake {
    $c = Get-Command cmake -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    foreach ($pat in @(
        "$env:ProgramFiles\CMake\bin\cmake.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\*\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")) {
        $hit = Get-ChildItem -Path $pat -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    throw "cmake not found (install CMake or run from a VS developer environment)."
}

function Resolve-OpenCppCoverage {
    $c = Get-Command OpenCppCoverage.exe -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    $p = "$env:ProgramFiles\OpenCppCoverage\OpenCppCoverage.exe"
    if (Test-Path $p) { return $p }
    throw "OpenCppCoverage not found. Install it with:  choco install opencppcoverage"
}

$cmake = Resolve-CMake
$occ   = Resolve-OpenCppCoverage

Write-Host "==> Building $Config (x86 + x64) with PDBs..." -ForegroundColor Cyan
& $cmake -S $repo -B "$repo\build\cmake\x86" -A Win32 | Out-Null
& $cmake --build "$repo\build\cmake\x86" --config $Config
if ($LASTEXITCODE -ne 0) { throw "x86 build failed." }
& $cmake -S $repo -B "$repo\build\cmake\x64" -A x64 | Out-Null
& $cmake --build "$repo\build\cmake\x64" --config $Config
if ($LASTEXITCODE -ne 0) { throw "x64 build failed." }

$out = Join-Path $repo 'coverage'
New-Item -ItemType Directory -Path $out -Force | Out-Null
# Clear only THIS run's outputs; keep coverage\manual.cov (from the guided run)
# and the merged report so a re-run doesn't destroy the manual coverage.
Remove-Item "$out\coverage.cobertura.xml", "$out\auto.cov", "$out\unit.cov", "$out\integration.cov", "$out\interactive.cov" -Force -ErrorAction SilentlyContinue
if (Test-Path "$out\html") { Remove-Item "$out\html" -Recurse -Force }

# Two runs, because OpenCppCoverage drives exactly one command each: the native
# unit tests (deterministic, no desktop interaction) and the integration tests
# (which drive real windows). Their results are merged into one report - running
# only the integration tests used to leave everything UnitTests.exe covers out of
# the numbers entirely.
Write-Host "==> Running native unit tests under OpenCppCoverage..." -ForegroundColor Cyan
& $occ `
    --sources "$repo\src" `
    --modules "$repo\build" `
    --cover_children --quiet `
    --export_type "binary:$out\unit.cov" `
    -- "$repo\build\UnitTests.exe"
if ($LASTEXITCODE -ne 0) { throw "UnitTests.exe failed." }

# The integration tests in two runs: the ones the build server runs too, and the
# ones it cannot (category InteractiveInput: real clicks, a visible mouse cursor).
Write-Host "==> Running integration tests under OpenCppCoverage..." -ForegroundColor Cyan
& $occ `
    --sources "$repo\src" `
    --modules "$repo\build" `
    --cover_children --quiet `
    --export_type "binary:$out\integration.cov" `
    -- dotnet test "$repo\tests\DeGhoster.Tests\DeGhoster.Tests.csproj" -c Release --nologo --disable-build-servers --filter "Category!=InteractiveInput"

Write-Host "==> Running the interactive tests (the build server skips them) under OpenCppCoverage..." -ForegroundColor Cyan
& $occ `
    --sources "$repo\src" `
    --modules "$repo\build" `
    --cover_children --quiet `
    --export_type "binary:$out\interactive.cov" `
    --export_type "cobertura:$out\interactive.cobertura.xml" `
    -- dotnet test "$repo\tests\DeGhoster.Tests\DeGhoster.Tests.csproj" -c Release --nologo --disable-build-servers --filter "Category=InteractiveInput"

Write-Host "==> Merging..." -ForegroundColor Cyan
# What the build server measures ...
& $occ --quiet `
    --input_coverage "$out\unit.cov" `
    --input_coverage "$out\integration.cov" `
    --export_type "cobertura:$out\coverage.cobertura.xml" `
    --export_type "html:$out\html"
# ... and everything automated, as the base for the guided manual run.
& $occ --quiet `
    --input_coverage "$out\unit.cov" `
    --input_coverage "$out\integration.cov" `
    --input_coverage "$out\interactive.cov" `
    --export_type "binary:$out\auto.cov"

# Commit these two files: the build server merges them into its combined report.
Write-Host "==> Storing the interactive tests' coverage in tests\coverage-data..." -ForegroundColor Cyan
& (Join-Path $PSScriptRoot 'coverage-normalize.ps1') -In "$out\interactive.cobertura.xml" `
    -Out "$PSScriptRoot\coverage-data\local.cobertura.xml" `
    -Fingerprint "$PSScriptRoot\coverage-data\local.fingerprint.json" -CoveredOnly

& (Join-Path $PSScriptRoot 'coverage-report.ps1') -Ci "$out\coverage.cobertura.xml" -OutDir $out
Write-Host "Reports: $out\report-ci\index.html (build server) and $out\report-combined\index.html (all)" -ForegroundColor Green
