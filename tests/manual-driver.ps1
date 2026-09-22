# Copyright (c) Emmo Emminghaus. SPDX-License-Identifier: AGPL-3.0-or-later
# Not run in CI. Do not run directly - use tests\coverage-manual.ps1.
#
# The guided run covers what an automated test cannot reach honestly. Since the
# LifecycleTests were added, the tray menu, the About window, the per-window eye
# and the message-driven DPI path all run unattended, so the steps below are the
# remainder: owner-draw states that need a real pointer (hover and press are read
# from the mouse, not from a message), a real display-scaling change, and the hard
# kill, which no test should do to a process it also has to measure.

param([switch]$DpiOnly)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Continue'
$repo  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $repo 'build'

Add-Type @"
using System;using System.Runtime.InteropServices;
public static class MD {
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h,int a,out int v,int s);
  [DllImport("user32.dll",EntryPoint="FindWindowW",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c,string n);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
}
"@

function Step([int]$n, [string]$text) {
    Write-Host ""
    Write-Host "== Step $n ==" -ForegroundColor Cyan
    Write-Host $text
    [void](Read-Host "When done, press [Enter]")
}

function Cloaked([IntPtr]$h) {
    if (-not [MD]::IsWindow($h)) { return -1 }
    $v = 0
    if ([MD]::DwmGetWindowAttribute($h, 14, [ref]$v, 4) -ne 0) { return -2 }
    return ($v -band 1)
}

function Helpers() { @(Get-Process DeGhoster.Helper32, DeGhoster.Helper64 -ErrorAction SilentlyContinue).Count }

function Report([string]$what, [bool]$ok) {
    $mark = if ($ok) { 'OK  ' } else { 'HUH '}
    Write-Host ("  [$mark] $what") -ForegroundColor $(if ($ok) { 'Green' } else { 'Yellow' })
}

reg add "HKCU\Software\DeGhoster" /v GlobalEnabled /t REG_DWORD /d 1 /f | Out-Null

$t64 = "DGHMAN64-$PID"
$t32 = "DGHMAN32-$PID"
$g64 = Start-Process (Join-Path $build 'GhostSim64.exe') "--title $t64 --timeout 1200" -PassThru
$g32 = Start-Process (Join-Path $build 'GhostSim32.exe') "--title $t32 --timeout 1200" -PassThru
Start-Sleep -Milliseconds 800
$h64 = [MD]::FindWindow("Chrome_WidgetWin_1", $t64)
$h32 = [MD]::FindWindow("Chrome_WidgetWin_1", $t32)

$dg = Start-Process (Join-Path $build 'DeGhoster.exe') -PassThru
Start-Sleep -Seconds 3

Write-Host ""
Write-Host "DeGhoster is running; one 64-bit and one 32-bit ghost window are present." -ForegroundColor Green
Report "64-bit ghost neutralized" ((Cloaked $h64) -eq 1)
Report "32-bit ghost neutralized" ((Cloaked $h32) -eq 1)
Report "one injector helper per target" ((Helpers) -ge 2)
Write-Host "Perform each action, then press Enter. Take your time." -ForegroundColor Green

if (-not $DpiOnly) {
    Step 1 @"
HOVER AND PRESS (the main reason this run exists). Double-click the tray icon to
open the status window, then, with the MOUSE:
  - hover over the power, info and exit buttons one at a time, and move away again,
  - press and HOLD the power button for a moment before releasing it,
  - hover over the eye on a list row, then click it off and on again.
The hot and pressed states are read from the pointer, so no posted message can
reproduce them.
"@
    Step 2 @"
TRAY MENU with the mouse: right-click the tray icon, then pick entries by
pointing and clicking (the unattended test drives this menu from the keyboard):
'Status Window', 'Active' off and on again, and one per-window entry.
"@
}

Step 3 @"
DPI CHANGE, for real: change the Windows display scaling (Settings -> System ->
Display -> Scale) and apply it, OR drag the status window onto a monitor with a
different scaling. Open the status window first so it receives the change, and
open 'About' as well so that window gets one too.
The unattended test only posts WM_DPICHANGED; this is the genuine article.
"@

if (-not $DpiOnly) {
    Step 4 @"
HARD KILL: end DeGhoster now via Task Manager ('End task' on DeGhoster.exe) -
NOT via the menu and NOT with the exit button. Nothing of the app's own teardown
runs, so this checks the helpers' watchdog: each helper is waiting on the host
process and has to unhook, release its hook DLL and quit on its own, which then
reveals both ghost windows again.
"@

    Write-Host ""
    Write-Host "Checking what the hard kill left behind..." -ForegroundColor Cyan
    $killed = $false
    try { $killed = $dg.HasExited } catch { $killed = $true }
    Report "DeGhoster was ended (if not, end it now and re-run)" $killed

    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline -and (Helpers) -gt 0) { Start-Sleep -Milliseconds 250 }
    Report "every injector helper exited on its own" ((Helpers) -eq 0)

    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline -and ((Cloaked $h64) -eq 1 -or (Cloaked $h32) -eq 1)) { Start-Sleep -Milliseconds 250 }
    Report "64-bit ghost revealed again" ((Cloaked $h64) -eq 0)
    Report "32-bit ghost revealed again" ((Cloaked $h32) -eq 0)

    foreach ($d in @('DeGhoster.Hook64.dll', 'DeGhoster.Hook32.dll')) {
        $p = Join-Path $build $d
        $free = $false
        try { $fs = [IO.File]::Open($p, 'Open', 'ReadWrite', 'None'); $fs.Close(); $free = $true } catch { }
        Report "$d is no longer locked" $free
    }
}

try { if (-not $dg.HasExited) { $dg.Kill() } } catch { }
Get-Process DeGhoster.Helper32, DeGhoster.Helper64 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
foreach ($p in @($g64, $g32)) { try { if (-not $p.HasExited) { $p.Kill() } } catch { } }

Write-Host ""
Write-Host "Done - writing coverage." -ForegroundColor Green
