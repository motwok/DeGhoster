# Copyright (c) Emmo Emminghaus. SPDX-License-Identifier: AGPL-3.0-or-later

[CmdletBinding()]
param(
    [switch]$NoZip,
    [switch]$NoMsi,
    [switch]$PackageOnly,
    [string]$OutDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RepoRoot = $PSScriptRoot

function Resolve-CMake {
    $c = Get-Command cmake -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    $candidates = @(
        "$env:ProgramFiles\CMake\bin\cmake.exe"
        "$env:ProgramFiles\Microsoft Visual Studio\18\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        "$env:ProgramFiles\Microsoft Visual Studio\2022\*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )
    foreach ($pat in $candidates) {
        $hit = Get-ChildItem -Path $pat -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    throw "cmake not found. Install CMake or run from a Visual Studio developer environment."
}

if (-not $PackageOnly) {
    $cmake = Resolve-CMake
    Write-Host "Using cmake: $cmake" -ForegroundColor Cyan

    try { & dotnet tool restore | Out-Null } catch { Write-Warning "dotnet tool restore failed: $_" }

    Write-Host "==> Building x86 (Hook32 + Helper32)..." -ForegroundColor Cyan
    & $cmake --workflow --preset x86
    if ($LASTEXITCODE -ne 0) { throw "x86 build failed." }

    Write-Host "==> Building x64 (Host + MUI + Hook64)..." -ForegroundColor Cyan
    & $cmake --workflow --preset x64
    if ($LASTEXITCODE -ne 0) { throw "x64 build failed." }
}

if (-not $NoZip) {
    Write-Host "==> Packaging ZIP..." -ForegroundColor Cyan
    & (Join-Path $RepoRoot 'packaging\build-zip.ps1') -OutDir $OutDir
}

if (-not $NoMsi) {
    Write-Host "==> Packaging MSI..." -ForegroundColor Cyan
    & (Join-Path $RepoRoot 'packaging\build-msi.ps1') -OutDir $OutDir
}

Write-Host "Done." -ForegroundColor Green
