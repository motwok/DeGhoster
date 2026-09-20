# Copyright (c) Emmo Emminghaus. SPDX-License-Identifier: AGPL-3.0-or-later
#
# Ghost-window probe. Lists every on-screen top-level window that is layered
# (WS_EX_LAYERED) or of class Chrome_WidgetWin_1, and dumps the properties that
# matter for the DeGhoster detection and for the click-eating question:
#   class, process, rect, visible, ex-styles (LAYERED/TRANSPARENT/TOOLWINDOW/
#   NOACTIVATE/TOPMOST), GetLayeredWindowAttributes (LWA flags + alpha + colorkey),
#   DWM cloaked, and whether the window is hit-testable at its own centre
#   (HitsSelf = Y means clicks there land on THIS window, i.e. it eats them).
#
# Run it WHILE the real ghost (e.g. WhatsApp) is reproducing the click-eating,
# then look at the row whose HitsSelf=Y and Visible=Y over the affected area -
# its Alpha / LWA flags tell us what a faithful test ghost must look like and
# what DeGhoster must actually match.
#
#   powershell -ExecutionPolicy Bypass -File tests\ghost-probe.ps1
#   ... -File tests\ghost-probe.ps1 -Watch      # live: window under the cursor

param([switch]$Watch, [int]$Seconds = 30)

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class GhostProbe
{
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr p);
    delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] static extern int GetWindowLong(IntPtr h, int i);
    [DllImport("user32.dll")] static extern IntPtr GetWindowLongPtr(IntPtr h, int i);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool GetLayeredWindowAttributes(IntPtr h, out uint key, out byte alpha, out uint flags);
    [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(POINT p);
    [DllImport("user32.dll")] static extern bool GetCursorPos(out POINT p);
    [DllImport("user32.dll")] static extern IntPtr GetAncestor(IntPtr h, uint f);
    [DllImport("dwmapi.dll")] static extern int DwmGetWindowAttribute(IntPtr h, int a, out int v, int s);

    [StructLayout(LayoutKind.Sequential)] struct RECT { public int l,t,r,b; }
    [StructLayout(LayoutKind.Sequential)] struct POINT { public int x,y; }

    const int GWL_STYLE=-16, GWL_EXSTYLE=-20;
    const long WS_EX_LAYERED=0x80000, WS_EX_TRANSPARENT=0x20, WS_EX_TOOLWINDOW=0x80,
               WS_EX_NOACTIVATE=0x8000000, WS_EX_TOPMOST=0x8;

    static long ExStyle(IntPtr h) { return (long)(IntPtr.Size==8 ? GetWindowLongPtr(h,GWL_EXSTYLE) : (IntPtr)GetWindowLong(h,GWL_EXSTYLE)); }

    public class Row {
        public string HWND, Class, Process, Rect, Ex, LWA, Cloaked;
        public int Alpha; public bool Visible, OnScreen, HitsSelf;
    }

    static Row BuildRow(IntPtr h)
    {
            long ex = ExStyle(h);
            var cn = new StringBuilder(256); GetClassName(h, cn, 256);
            string cls = cn.ToString();

            RECT r; GetWindowRect(h, out r);
            int w = r.r-r.l, ht = r.b-r.t;
            bool onScreen = w>0 && ht>0;

            uint pid; GetWindowThreadProcessId(h, out pid);
            string proc = pid.ToString();
            try { proc = System.Diagnostics.Process.GetProcessById((int)pid).ProcessName; } catch {}

            uint key, flags; byte alpha=255;
            bool haveLwa = GetLayeredWindowAttributes(h, out key, out alpha, out flags);
            string lwa = haveLwa ? string.Format("a={0}{1}{2}", alpha,
                            ((flags&2)!=0?" ALPHA":""), ((flags&1)!=0?" COLORKEY":"")) : "(none)";

            int cloaked=0; DwmGetWindowAttribute(h, 14, out cloaked, 4);

            var exs = new List<string>();
            if((ex&WS_EX_LAYERED)!=0) exs.Add("LAYERED");
            if((ex&WS_EX_TRANSPARENT)!=0) exs.Add("TRANSPARENT");
            if((ex&WS_EX_TOOLWINDOW)!=0) exs.Add("TOOL");
            if((ex&WS_EX_NOACTIVATE)!=0) exs.Add("NOACTIVATE");
            if((ex&WS_EX_TOPMOST)!=0) exs.Add("TOPMOST");
            if((ex&0x00200000)!=0) exs.Add("NOREDIR");   // WS_EX_NOREDIRECTIONBITMAP (DComp)

            bool hits=false;
            if(onScreen){ POINT c; c.x=(r.l+r.r)/2; c.y=(r.t+r.b)/2; hits = WindowFromPoint(c)==h; }

            return new Row {
                HWND="0x"+h.ToString("X"), Class=cls, Process=proc,
                Rect=string.Format("{0},{1} {2}x{3}", r.l, r.t, w, ht),
                Visible=IsWindowVisible(h), OnScreen=onScreen,
                Ex=string.Join("|", exs), LWA=lwa, Alpha=(haveLwa?alpha:-1),
                Cloaked=(cloaked!=0?("yes("+cloaked+")"):"no"), HitsSelf=hits
            };
    }

    public static Row[] Scan()
    {
        var rows = new List<Row>();
        EnumWindows((h,p) => {
            long ex = ExStyle(h);
            var cn = new StringBuilder(256); GetClassName(h, cn, 256);
            if ((ex & WS_EX_LAYERED)==0 && cn.ToString() != "Chrome_WidgetWin_1") return true;
            rows.Add(BuildRow(h));
            return true;
        }, IntPtr.Zero);
        return rows.ToArray();
    }

    // Top-level window currently under the mouse cursor.
    public static Row AtCursor()
    {
        POINT p; GetCursorPos(out p);
        IntPtr h = WindowFromPoint(p);
        if (h != IntPtr.Zero) h = GetAncestor(h, 2 /*GA_ROOT*/);
        return h == IntPtr.Zero ? null : BuildRow(h);
    }
}
"@

$rows = [GhostProbe]::Scan()
Write-Host ("Found {0} layered / Chrome_WidgetWin_1 top-level windows." -f $rows.Count) -ForegroundColor Cyan

Write-Host "`n=== Click-EATER candidates (Visible + OnScreen + HitsSelf + not cloaked) ===" -ForegroundColor Yellow
$eaters = $rows | Where-Object { $_.Visible -and $_.OnScreen -and $_.HitsSelf -and $_.Cloaked -eq 'no' }
if ($eaters) {
    ($eaters | Select-Object Class, Process, Rect, LWA, Ex | Format-Table -AutoSize | Out-String -Width 300).TrimEnd() | Write-Host
} else { Write-Host "(none right now)" }

Write-Host "`n=== All (compact) ===" -ForegroundColor Cyan
($rows | Sort-Object -Property @{e={$_.HitsSelf}}, @{e={$_.Visible}} -Descending |
    Select-Object Process, @{n='HitsSelf';e={$_.HitsSelf}}, @{n='Vis';e={$_.Visible}}, LWA, Rect, Ex |
    Format-Table -AutoSize | Out-String -Width 300).TrimEnd() | Write-Host

Write-Host "`nA click-EATING ghost is: Visible=True, OnScreen=True, HitsSelf=True, Cloaked=no." -ForegroundColor Yellow
Write-Host "Its LWA (alpha + flags) + Class is what a faithful test ghost and DeGhoster's detection must match." -ForegroundColor Yellow

if ($Watch) {
    Write-Host "`n=== Cursor watch: move the mouse over the dead-click spot ($Seconds s; Ctrl+C to stop) ===" -ForegroundColor Cyan
    $end = (Get-Date).AddSeconds($Seconds); $last = ''
    while ((Get-Date) -lt $end) {
        $r = [GhostProbe]::AtCursor()
        if ($r) {
            $line = "{0,-20} {1,-16} vis={2,-5} cloaked={3,-7} LWA={4,-14} {5}" -f $r.Class, $r.Process, $r.Visible, $r.Cloaked, $r.LWA, $r.Ex
            if ($line -ne $last) { Write-Host $line; $last = $line }
        }
        Start-Sleep -Milliseconds 500
    }
}
