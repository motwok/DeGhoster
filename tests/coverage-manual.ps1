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

# DeGhoster is single-instance: started while another copy runs, the measured one
# hands off to it and exits at once, and the run records nothing but startup code
# - which the Manual test check would still accept as current. So refuse to start.
$running = @(Get-CimInstance Win32_Process -Filter "Name = 'DeGhoster.exe'" -ErrorAction SilentlyContinue)
if ($running.Count) {
    $lines = $running | ForEach-Object {
        $path = if ($_.ExecutablePath) { $_.ExecutablePath } else { '(path not readable)' }
        "  PID $($_.ProcessId): $path"
    }
    $quit = $running | Where-Object { $_.ExecutablePath } | Select-Object -ExpandProperty ExecutablePath -Unique |
            ForEach-Object { "  & `"$_`" --quit" }
    Write-Host "DeGhoster is already running:" -ForegroundColor Red
    $lines | ForEach-Object { Write-Host $_ }
    Write-Host "The measured copy would hand off to it and exit at once. Quit it first, e.g.:" -ForegroundColor Red
    $quit | ForEach-Object { Write-Host $_ }
    exit 1
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

# A throwaway settings root, like the integration tests use. The run toggles eyes
# on real windows too (a WhatsApp ghost is listed next to the simulators), and
# against the user's own HKCU\Software\DeGhoster that switched WhatsApp off for
# good. DeGhoster and the driver read the root from DEGHOSTER_SETTINGS_ROOT, which
# every process started below inherits.
$settingsPrefix = 'DeGhoster_Manual_'
Get-ChildItem HKCU:\Software -ErrorAction SilentlyContinue |
    Where-Object { $_.PSChildName -like "$settingsPrefix*" } |
    ForEach-Object { Remove-Item -LiteralPath $_.PSPath -Recurse -Force -ErrorAction SilentlyContinue }   # left by an aborted run
$settingsRoot = "Software\$settingsPrefix" + [guid]::NewGuid().ToString('N')
$env:DEGHOSTER_SETTINGS_ROOT = $settingsRoot

Write-Host "==> Starting guided coverage session (follow the on-screen steps)..." -ForegroundColor Cyan
Write-Host "    Settings for this run: HKCU\$settingsRoot (your own settings stay untouched)"
$driverArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$repo\tests\manual-driver.ps1")
if ($DpiOnly) { $driverArgs += '-DpiOnly' }
try {
    & $occ `
        --sources "$repo\src" `
        --modules "$repo\build" `
        --cover_children --quiet `
        --export_type "binary:$out\manual.cov" `
        --export_type "cobertura:$out\manual.cobertura.xml" `
        --export_type "html:$out\manual-html" `
        -- powershell @driverArgs
} finally {
    Remove-Item -LiteralPath "HKCU:\$settingsRoot" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item Env:\DEGHOSTER_SETTINGS_ROOT -ErrorAction SilentlyContinue
}

# Commit these two files: the build server merges them into its combined report.
Write-Host "==> Storing the manual run's coverage in tests\coverage-data..." -ForegroundColor Cyan
& (Join-Path $PSScriptRoot 'coverage-normalize.ps1') -In "$out\manual.cobertura.xml" `
    -Out "$PSScriptRoot\coverage-data\manual.cobertura.xml" `
    -Fingerprint "$PSScriptRoot\coverage-data\manual.fingerprint.json" -CoveredOnly
if (Test-Path "$out\coverage.cobertura.xml") {
    & (Join-Path $PSScriptRoot 'coverage-report.ps1') -Ci "$out\coverage.cobertura.xml" -OutDir $out
}

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
