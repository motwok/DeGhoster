# Copyright (c) Emmo Emminghaus. SPDX-License-Identifier: AGPL-3.0-or-later
#
# Guided coverage session - the TARGET run under OpenCppCoverage by
# coverage-manual.ps1. It launches GhostSim (x64 + x86) and DeGhoster, then walks
# the user through the manual actions that can't be automated robustly (tray
# menu, per-window eye click, DPI change, 32-bit helper teardown). Coverage of
# the native processes is collected by the OpenCppCoverage that started this.
#
# Not run in CI. Do not run directly - use tests\coverage-manual.ps1.

param([switch]$DpiOnly)   # only walk the DPI step (the rest is automated now)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Continue'
$repo  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repo 'build'

function Step([int]$n, [string]$text) {
    Write-Host ""
    Write-Host "== Step $n ==" -ForegroundColor Cyan
    Write-Host $text
    [void](Read-Host "When done, press [Enter]")
}

reg add "HKCU\Software\DeGhoster" /v GlobalEnabled /t REG_DWORD /d 1 /f | Out-Null

$g64 = Start-Process (Join-Path $build 'GhostSim64.exe') "--title DGHMAN64-$PID --timeout 1200" -PassThru
$g32 = Start-Process (Join-Path $build 'GhostSim32.exe') "--title DGHMAN32-$PID --timeout 1200" -PassThru
Start-Sleep -Milliseconds 800
$dg = Start-Process (Join-Path $build 'DeGhoster.exe') -PassThru
Start-Sleep -Seconds 2

Write-Host "DeGhoster is running (two ghost windows are present)." -ForegroundColor Green
Write-Host "Perform each action, then press Enter. Take your time." -ForegroundColor Green

if (-not $DpiOnly) {
    Step 1 "TRAY MENU: right-click the DeGhoster tray icon. Open 'Statusfenster', toggle 'Aktiv' off and on, and click one per-window entry in the menu to toggle it."
    Step 2 "INFO via menu: right-click the tray icon -> 'Info'. Then close the Info window."
    Step 3 "PER-WINDOW EYE: double-click the tray icon to open the status window. Click the eye on a list row (turns it off), then click it again (on)."
}
Step 4 "DPI CHANGE: change the Windows display scaling (Settings -> System -> Display -> Scale) and apply it, OR drag the DeGhoster status window onto a monitor with a different scaling. (Open the status window first via a double-click on the tray icon so it receives the DPI change.)"
if (-not $DpiOnly) {
    Step 5 "32-BIT TEARDOWN: end DeGhoster now via Task Manager (End task on 'DeGhoster.exe') - NOT via the menu. The 32-bit helper then cleans itself up."
}

Start-Sleep -Seconds 2   # let Helper32 self-clean under coverage

# Cleanup whatever is left.
try { if (-not $dg.HasExited) { $dg.Kill() } } catch { }
Get-Process DeGhoster.Helper32 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
foreach ($p in @($g64, $g32)) { try { if (-not $p.HasExited) { $p.Kill() } } catch { } }

Write-Host "Done - writing coverage." -ForegroundColor Green
