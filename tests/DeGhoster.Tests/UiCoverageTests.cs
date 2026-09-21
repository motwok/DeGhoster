// Copyright (c) Emmo Emminghaus mo2000 at mo2000 dot de
// SPDX-License-Identifier: AGPL-3.0-or-later

using System.Diagnostics;
using System.Runtime.InteropServices;
using Xunit;

namespace DeGhoster.Tests;

[Trait("Category", "Integration")]
public class UiCoverageTests
{
    private const string GhostClass = "Chrome_WidgetWin_1";
    private const string HostClass = "DeGhosterMainWindow";
    private const string InfoClass = "DeGhosterInfoWindow";
    private const int DWMWA_CLOAKED = 14;
    private const uint WM_COMMAND = 0x0111, WM_CLOSE = 0x0010;
    private const uint WM_MOUSEMOVE = 0x0200, WM_MOUSELEAVE = 0x02A3;
    private const uint WM_TRAY = 0x8040 /* WM_APP+0x40 */, WM_RBUTTONUP = 0x0205;
    private const byte VK_RETURN = 0x0D, VK_ESCAPE = 0x1B, VK_DOWN = 0x28;
    private const uint KEYEVENTF_KEYUP = 0x0002;
    private const uint MOUSEEVENTF_LEFTDOWN = 0x0002, MOUSEEVENTF_LEFTUP = 0x0004;
    private const int IDC_LIST = 1001, IDC_POWER = 1002, IDC_INFO = 1003, IDC_EXIT = 1004;
    private const uint LVM_GETHEADER = 0x101F;

    [StructLayout(LayoutKind.Sequential)] private struct RECT { public int left, top, right, bottom; }

    private static void Key(byte vk)
    {
        keybd_event(vk, 0, 0, IntPtr.Zero);
        keybd_event(vk, 0, KEYEVENTF_KEYUP, IntPtr.Zero);
    }

    [Fact]
    public void Info_window_power_toggle_and_quit()
    {
        var (build, dg, sim, ghost, host) = StartWithGhost("64", null);
        try
        {
            PostMessage(host, WM_COMMAND, (IntPtr)IDC_INFO, IntPtr.Zero);
            IntPtr info = WaitFor(() => FindWindowEx(IntPtr.Zero, IntPtr.Zero, InfoClass, null), TimeSpan.FromSeconds(5));
            Assert.True(info != IntPtr.Zero, "About window did not open");
            Thread.Sleep(400);   // let it paint
            PostMessage(info, WM_CLOSE, IntPtr.Zero, IntPtr.Zero);

            PostMessage(host, WM_COMMAND, (IntPtr)IDC_POWER, IntPtr.Zero);
            Thread.Sleep(300);
            PostMessage(host, WM_COMMAND, (IntPtr)IDC_POWER, IntPtr.Zero);
            Thread.Sleep(300);

            foreach (int id in new[] { IDC_POWER, IDC_INFO, IDC_EXIT })
            {
                IntPtr btn = GetDlgItem(host, id);
                if (btn != IntPtr.Zero)
                {
                    PostMessage(btn, WM_MOUSEMOVE, IntPtr.Zero, (IntPtr)0x00050005);
                    Thread.Sleep(80);
                    PostMessage(btn, WM_MOUSELEAVE, IntPtr.Zero, IntPtr.Zero);
                }
            }
            Thread.Sleep(200);

            // Close via the window's X -> hides back to the tray (does not quit).
            PostMessage(host, WM_CLOSE, IntPtr.Zero, IntPtr.Zero);
            Thread.Sleep(300);

            PostMessage(host, WM_COMMAND, (IntPtr)IDC_EXIT, IntPtr.Zero);
            Assert.True(dg.WaitForExit(10000), "DeGhoster did not quit");
        }
        finally { Cleanup(dg, sim); }
    }

    [Fact]
    public void Rtl_ui_language_paints()
    {
        // ar-SA forces the RTL path (Loc::isRtl, WS_EX_LAYOUTRTL, upright glyphs).
        var (build, dg, sim, ghost, host) = StartWithGhost("64",
            new Dictionary<string, string> { ["DEGHOSTER_UILANG"] = "ar-SA" });
        try
        {
            PostMessage(host, WM_COMMAND, (IntPtr)IDC_INFO, IntPtr.Zero);
            Thread.Sleep(500);
            PostMessage(host, WM_COMMAND, (IntPtr)IDC_EXIT, IntPtr.Zero);
            Assert.True(dg.WaitForExit(10000), "DeGhoster (RTL) did not quit");
        }
        finally { Cleanup(dg, sim); }
    }

    [Fact]
    public void List_eye_click_toggles_a_window()
    {
        var (build, dg, sim, ghost, host) = StartWithGhost("64", null);
        try
        {
            IntPtr list = GetDlgItem(host, IDC_LIST);
            Assert.True(list != IntPtr.Zero, "list view not found");
            SetForegroundWindow(host);
            Thread.Sleep(250);
            GetWindowRect(list, out RECT r);

            // Row 0 starts just below the column header. Query the list's header and
            // use its bottom edge so the click lands on the first row whether or not
            // the header is visible (it has ~0 height under LVS_NOCOLUMNHEADER).
            int rowTop = r.top;
            IntPtr header = SendMessage(list, LVM_GETHEADER, IntPtr.Zero, IntPtr.Zero);
            if (header != IntPtr.Zero && GetWindowRect(header, out RECT hr) &&
                hr.bottom > r.top && hr.bottom < r.bottom)
                rowTop = hr.bottom;

            // Eye cell = rightmost column, first row. A REAL click (mouse_event at
            // the cursor) makes the list fire NM_CLICK -> onListClick -> toggle,
            // which a posted WM_LBUTTON* does not. The first click may only
            // activate the window, so click again if it didn't take.
            void ClickEye()
            {
                SetCursorPos(r.right - 26, rowTop + 15);
                Thread.Sleep(60);
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, IntPtr.Zero);
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, IntPtr.Zero);
            }
            ClickEye();
            if (!WaitUntil(() => Cloaked(ghost) == 0, TimeSpan.FromSeconds(3))) ClickEye();

            Assert.True(WaitUntil(() => Cloaked(ghost) == 0, TimeSpan.FromSeconds(5)),
                        "clicking the eye did not un-cloak the window");
        }
        finally { Cleanup(dg, sim); }
    }

    [Fact]
    public void Tray_menu_toggles_a_window()
    {
        var (build, dg, sim, ghost, host) = StartWithGhost("64", null);
        try
        {
            SetCursorPos(500, 500);
            // Open the modal tray menu (right-click). It appears at the cursor and
            // blocks DeGhoster's thread until a selection is made.
            PostMessage(host, WM_TRAY, IntPtr.Zero, (IntPtr)WM_RBUTTONUP);
            Thread.Sleep(600);
            // Menu order: Status Window (default), Active, separator, <window entry>,
            // separator, About, Exit. Navigate to the window entry and select it.
            for (int i = 0; i < 3; i++) { Key(VK_DOWN); Thread.Sleep(90); }
            Key(VK_RETURN);

            bool uncloaked = WaitUntil(() => Cloaked(ghost) == 0, TimeSpan.FromSeconds(6));
            if (!uncloaked) Key(VK_ESCAPE);   // safety: dismiss a stuck menu
            Assert.True(uncloaked, "toggling the window via the tray menu did not un-cloak it");
        }
        finally { Cleanup(dg, sim); }
    }

    [Fact]
    public void Closing_a_ghost_drops_it_from_tracking()
    {
        var (build, dg, sim, ghost, host) = StartWithGhost("64", null);
        try
        {
            PostMessage(ghost, WM_CLOSE, IntPtr.Zero, IntPtr.Zero);
            Assert.True(sim.WaitForExit(8000), "ghost simulator did not close");
            Thread.Sleep(500);   // let DeGhoster process the destroy event
            Assert.False(dg.HasExited, "DeGhoster should still be running");
        }
        finally { Cleanup(dg, sim); }
    }

    private (string build, Process dg, Process sim, IntPtr ghost, IntPtr host)
        StartWithGhost(string bits, Dictionary<string, string>? env)
    {
        string build = FindBuildDir();
        EnsureGlobalEnabled();
        string title = "DGHUI-" + Guid.NewGuid().ToString("N");
        Process sim = Start(Path.Combine(build, $"GhostSim{bits}.exe"), $"--title \"{title}\" --timeout 90", build, null);
        IntPtr ghost = WaitFor(() => FindWindowEx(IntPtr.Zero, IntPtr.Zero, GhostClass, title), TimeSpan.FromSeconds(8));
        Assert.True(ghost != IntPtr.Zero, "ghost window not found");
        Process dg = Start(Path.Combine(build, "DeGhoster.exe"), "", build, env);
        Assert.True(WaitUntil(() => Cloaked(ghost) != 0, TimeSpan.FromSeconds(20)), "window was not cloaked");
        IntPtr host = WaitFor(() => FindWindowEx(IntPtr.Zero, IntPtr.Zero, HostClass, null), TimeSpan.FromSeconds(5));
        Assert.True(host != IntPtr.Zero, "DeGhoster main window not found");
        return (build, dg, sim, ghost, host);
    }

    private static void Cleanup(Process? dg, Process? sim)
    {
        try { if (dg is { HasExited: false }) dg.Kill(entireProcessTree: true); } catch { }
        foreach (var p in Process.GetProcessesByName("DeGhoster.Helper32")) { try { p.Kill(); } catch { } }
        try { if (sim is { HasExited: false }) sim.Kill(); } catch { }
    }

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

    private static Process Start(string exe, string args, string workDir, Dictionary<string, string>? env)
    {
        var psi = new ProcessStartInfo(exe, args) { UseShellExecute = false, WorkingDirectory = workDir };
        if (env != null) foreach (var kv in env) psi.Environment[kv.Key] = kv.Value;
        return Process.Start(psi)!;
    }

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

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr child, string cls, string? title);
    [DllImport("user32.dll")]
    private static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")]
    private static extern IntPtr GetDlgItem(IntPtr parent, int id);
    [DllImport("user32.dll")]
    private static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")]
    private static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")]
    private static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    [DllImport("user32.dll")]
    private static extern void mouse_event(uint flags, int dx, int dy, uint data, IntPtr extra);
    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(IntPtr hwnd, out RECT rc);
    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("dwmapi.dll")]
    private static extern int DwmGetWindowAttribute(IntPtr hwnd, int attr, out int value, int size);
}
