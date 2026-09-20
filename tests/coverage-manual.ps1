# Copyright (c) Emmo Emminghaus. SPDX-License-Identifier: AGPL-3.0-or-later

[CmdletBinding()]
param([string]$Config = 'Debug', [switch]$DpiOnly)

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
    throw "cmake not found."
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
$out   = Join-Path $repo 'coverage'
New-Item -ItemType Directory -Path $out -Force | Out-Null

Write-Host "==> Building $Config (x86 + x64) with PDBs..." -ForegroundColor Cyan
& $cmake -S $repo -B "$repo\build\cmake\x86" -A Win32 | Out-Null
& $cmake --build "$repo\build\cmake\x86" --config $Config
& $cmake -S $repo -B "$repo\build\cmake\x64" -A x64 | Out-Null
& $cmake --build "$repo\build\cmake\x64" --config $Config

Write-Host "==> Starting guided coverage session (follow the on-screen steps)..." -ForegroundColor Cyan
$driverArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$repo\tests\manual-driver.ps1")
if ($DpiOnly) { $driverArgs += '-DpiOnly' }
& $occ `
    --sources "$repo\src" `
    --modules "$repo\build" `
    --cover_children --quiet `
    --export_type "binary:$out\manual.cov" `
    --export_type "html:$out\manual-html" `
    -- powershell @driverArgs

$auto = Join-Path $out 'auto.cov'
if (Test-Path $auto) {
    Write-Host "==> Merging automated + manual coverage..." -ForegroundColor Cyan
    & $occ `
        --input_coverage "$auto" `
        --input_coverage "$out\manual.cov" `
        --export_type "cobertura:$out\merged.cobertura.xml" `
        --export_type "html:$out\merged-html"
    Write-Host "Merged report: $out\merged-html\index.html" -ForegroundColor Green
} else {
    Write-Host "No coverage\auto.cov found - run tests\coverage.ps1 first to get a merged total." -ForegroundColor Yellow
    Write-Host "Manual-only report: $out\manual-html\index.html" -ForegroundColor Green
}
