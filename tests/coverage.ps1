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
Remove-Item "$out\coverage.cobertura.xml", "$out\auto.cov" -Force -ErrorAction SilentlyContinue
if (Test-Path "$out\html") { Remove-Item "$out\html" -Recurse -Force }

Write-Host "==> Running tests under OpenCppCoverage..." -ForegroundColor Cyan
& $occ `
    --sources "$repo\src" `
    --modules "$repo\build" `
    --cover_children --quiet `
    --export_type "cobertura:$out\coverage.cobertura.xml" `
    --export_type "html:$out\html" `
    --export_type "binary:$out\auto.cov" `
    -- dotnet test "$repo\tests\DeGhoster.Tests\DeGhoster.Tests.csproj" -c Release --nologo --disable-build-servers

Write-Host "Coverage report: $out\html\index.html" -ForegroundColor Green
