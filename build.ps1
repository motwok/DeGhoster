# Copyright (c) Emmo Emminghaus. SPDX-License-Identifier: AGPL-3.0-or-later
#
# One-shot build + packaging for DeGhoster.
#
#   .\build.ps1                # version-stamp, build x86+x64, then ZIP + MSI
#   .\build.ps1 -NoMsi         # skip the MSI (e.g. WiX not installed)
#   .\build.ps1 -PackageOnly   # (re)package from an existing build/ output
#
# Versioning is via GitVersion (see cmake/Version.cmake + GitVersion.yml); the
# binaries are stamped with a VERSIONINFO resource and the artifacts are named
# after the version. Output lands in dist/.

[CmdletBinding()]
param(
    [switch]$NoZip,
    [switch]$NoMsi,
    [switch]$PackageOnly,   # skip compiling; just (re)build the packages
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

    # Restore the pinned GitVersion tool so `dotnet gitversion` works in CI too.
    try { & dotnet tool restore | Out-Null } catch { Write-Warning "dotnet tool restore failed: $_" }

    # x86 first (32-bit hook + helper), then x64 (host + MUI + 64-bit hook).
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
