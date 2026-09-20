// Copyright (c) Emmo Emminghaus mo2000 at mo2000 dot de
// SPDX-License-Identifier: AGPL-3.0-or-later

using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using Xunit;

namespace DeGhoster.Tests;

// Proof that the GhostSim ghost reproduces the real WhatsApp/WebView2 defect
// (an invisible, click-eating window) AND that DeGhoster fixes it:
//
//   1. An opaque "click sink" window sits at a spot and counts the clicks it gets.
//   2. A ghost window (Chrome_WidgetWin_1, WS_EX_LAYERED | NOREDIRECTIONBITMAP,
//      alpha 0) sits ON TOP of it. Like a real Chromium ghost, it is invisible
//      but still hit-testable (no redirection bitmap -> the "alpha 0 lets clicks
//      through" rule does not apply), so it eats clicks.
//   3. A real click (SendInput) on that spot -> the sink gets NOTHING (eaten).
//   4. DeGhoster cloaks the ghost -> the same click now reaches the sink.
//
// The real ghost's exact profile was captured with tests\ghost-probe.ps1:
//   Chrome_WidgetWin_1 / msedgewebview2 / a=0 ALPHA / LAYERED|TOOL|NOACTIVATE|NOREDIR.
[Trait("Category", "Integration")]
public class ClickThroughProofTests
{
    private const string GhostClass = "Chrome_WidgetWin_1";
    private const string SinkClass = "DeGhosterClickSink";
    private const int DWMWA_CLOAKED = 14;

    [Fact]
    public void Ghost_eats_clicks_and_DeGhoster_lets_them_through()
    {
        string build = FindBuildDir();
        string sim = Path.Combine(build, "GhostSim64.exe");
        Assert.True(File.Exists(sim), $"missing {sim}");
        EnsureGlobalEnabled();

        string sinkTitle = "SINK-" + Guid.NewGuid().ToString("N");
        string ghostTitle = "GHOST-" + Guid.NewGuid().ToString("N");
        Process? pSink = null, pGhost = null, dg = null;
        try
        {
            pSink = Start(sim, $"--sink --title \"{sinkTitle}\" --x 300 --y 300 --timeout 180", build);
            pGhost = Start(sim, $"--title \"{ghostTitle}\" --x 300 --y 300 --timeout 180", build);   // default: NOREDIR ghost

            IntPtr hSink = WaitFor(() => FindWindowEx(IntPtr.Zero, IntPtr.Zero, SinkClass, sinkTitle), TimeSpan.FromSeconds(8));
            IntPtr hGhost = WaitFor(() => FindWindowEx(IntPtr.Zero, IntPtr.Zero, GhostClass, ghostTitle), TimeSpan.FromSeconds(8));
            Assert.True(hSink != IntPtr.Zero, "click sink window not found");
            Assert.True(hGhost != IntPtr.Zero, "ghost window not found");

            // Z-order: ghost topmost, sink directly below it.
            SetWindowPos(hGhost, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetWindowPos(hSink, hGhost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            Thread.Sleep(400);

            GetWindowRect(hGhost, out RECT r);
            int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            int before = SinkClicks(hSink);

            // (a) Ghost present -> the click is eaten; the sink count does NOT rise.
            ClickAt(cx, cy);
            Thread.Sleep(400);
            int afterGhost = SinkClicks(hSink);
            Assert.True(afterGhost == before,
                $"the ghost should eat the click (sink stays {before}), but sink went to {afterGhost}");

            // (b) DeGhoster cloaks the ghost -> the same click now reaches the sink.
            dg = Start(Path.Combine(build, "DeGhoster.exe"), "", build);
            Assert.True(WaitUntil(() => Cloaked(hGhost) != 0, TimeSpan.FromSeconds(20)), "ghost was not cloaked");

            ClickAt(cx, cy);
            Thread.Sleep(500);
            int afterFix = SinkClicks(hSink);
            Assert.True(afterFix > afterGhost,
                $"after DeGhoster cloaked the ghost the click should reach the sink ({afterGhost} -> >{afterGhost}), but it was {afterFix}");
        }
        finally
        {
            try { if (dg is { HasExited: false }) dg.Kill(entireProcessTree: true); } catch { }
            foreach (var p in new[] { pGhost, pSink }) { try { if (p is { HasExited: false }) p.Kill(); } catch { } }
        }
    }

    // --- helpers ------------------------------------------------------------

    private static int SinkClicks(IntPtr hSink)
    {
        var sb = new StringBuilder(256);
        GetWindowText(hSink, sb, sb.Capacity);
        var s = sb.ToString();
        int i = s.IndexOf("CLICKS=", StringComparison.Ordinal);
        return i >= 0 && int.TryParse(s[(i + 7)..], out int n) ? n : 0;
    }

    private static void ClickAt(int x, int y)
    {
        int sw = GetSystemMetrics(0), sh = GetSystemMetrics(1);
        int ax = x * 65535 / Math.Max(1, sw - 1), ay = y * 65535 / Math.Max(1, sh - 1);
        var inputs = new[]
        {
            MouseInput(ax, ay, MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE),
            MouseInput(ax, ay, MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_ABSOLUTE),
            MouseInput(ax, ay, MOUSEEVENTF_LEFTUP | MOUSEEVENTF_ABSOLUTE),
        };
        SendInput((uint)inputs.Length, inputs, Marshal.SizeOf<INPUT>());
    }

    private static INPUT MouseInput(int ax, int ay, uint flags)
        => new() { type = 0, mi = new MOUSEINPUT { dx = ax, dy = ay, dwFlags = flags } };

    private static string FindBuildDir()
    {
        for (var d = new DirectoryInfo(AppContext.BaseDirectory); d != null; d = d.Parent)
            if (File.Exists(Path.Combine(d.FullName, "build", "DeGhoster.exe")))
                return Path.Combine(d.FullName, "build");
        throw new DirectoryNotFoundException("build\\ not found above " + AppContext.BaseDirectory);
    }

    private static void EnsureGlobalEnabled()
    {
        var psi = new ProcessStartInfo("reg.exe") { UseShellExecute = false, CreateNoWindow = true };
        foreach (var a in new[] { "add", @"HKCU\Software\DeGhoster", "/v", "GlobalEnabled", "/t", "REG_DWORD", "/d", "1", "/f" })
            psi.ArgumentList.Add(a);
        Process.Start(psi)?.WaitForExit(5000);
    }

    private static Process Start(string exe, string args, string workDir)
        => Process.Start(new ProcessStartInfo(exe, args) { UseShellExecute = false, WorkingDirectory = workDir })!;

    private static IntPtr WaitFor(Func<IntPtr> get, TimeSpan timeout)
    {
        var end = DateTime.UtcNow + timeout;
        do { var h = get(); if (h != IntPtr.Zero) return h; Thread.Sleep(100); } while (DateTime.UtcNow < end);
        return IntPtr.Zero;
    }

    private static bool WaitUntil(Func<bool> cond, TimeSpan timeout)
    {
        var end = DateTime.UtcNow + timeout;
        do { if (cond()) return true; Thread.Sleep(150); } while (DateTime.UtcNow < end);
        return cond();
    }

    private static int Cloaked(IntPtr h)
        => DwmGetWindowAttribute(h, DWMWA_CLOAKED, out int v, sizeof(int)) == 0 ? v : -1;

    private const uint MOUSEEVENTF_MOVE = 0x0001, MOUSEEVENTF_ABSOLUTE = 0x8000,
                       MOUSEEVENTF_LEFTDOWN = 0x0002, MOUSEEVENTF_LEFTUP = 0x0004;
    private const uint SWP_NOSIZE = 0x0001, SWP_NOMOVE = 0x0002, SWP_NOACTIVATE = 0x0010;
    private static readonly IntPtr HWND_TOPMOST = new(-1);

    [StructLayout(LayoutKind.Sequential)] private struct RECT { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)] private struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)] private struct INPUT { public uint type; public MOUSEINPUT mi; }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr child, string cls, string? title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(IntPtr hwnd, StringBuilder s, int max);
    [DllImport("user32.dll")]
    private static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(IntPtr hwnd, out RECT rc);
    [DllImport("user32.dll")]
    private static extern uint SendInput(uint n, INPUT[] inputs, int cbSize);
    [DllImport("user32.dll")]
    private static extern int GetSystemMetrics(int index);
    [DllImport("dwmapi.dll")]
    private static extern int DwmGetWindowAttribute(IntPtr hwnd, int attr, out int value, int size);
}
