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
    private const uint MN_GETHMENU = 0x01E1;
    private const uint WS_POPUP = 0x80000000, WS_VISIBLE = 0x10000000, WS_EX_TOOLWINDOW = 0x00000080, WS_EX_TOPMOST = 0x00000008;
    private const int WindowEntry = 3;   // tray menu position of the test's ghost (sorts first)
    private static readonly IntPtr DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = new(-4);

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
            // the cursor) is needed; a posted WM_LBUTTON* does not reach the
            // control the same way. ONE click has to be enough, including when the
            // window is not in front - the list swallows the click that activates
            // it, which is why the eye is driven from the raw button messages
            // instead of NM_CLICK. Do not paper over a second click here again.
            void ClickEye()
            {
                SetCursorPos(r.right - 26, rowTop + 15);
                Thread.Sleep(60);
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, IntPtr.Zero);
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, IntPtr.Zero);
            }
            ClickEye();
            Assert.True(WaitUntil(() => Cloaked(ghost) == 0, TimeSpan.FromSeconds(6)),
                        "a single click on the eye did not un-cloak the window");
        }
        finally { Cleanup(dg, sim); }
    }

    [Fact]
    public void Tray_menu_toggles_in_place_and_closes_on_focus_loss()
    {
        var (build, dg, sim, ghost, host) = StartWithGhost("64", null);
        try
        {
            SetCursorPos(500, 500);
            // The menu is driven from the keyboard, and keystrokes go to whatever
            // holds the foreground. A real tray click grants DeGhoster the right to
            // take it; a posted WM_TRAY does not, so SetForegroundWindow inside
            // showTrayMenu is simply refused and the menu opens without keyboard
            // focus - the test then types into whatever window happens to be in
            // front. Hand the foreground over deliberately first.
            // Driving a modal menu means competing for the foreground with whatever
            // else is on the desktop, so give it a few attempts rather than one.
            bool uncloaked = false;
            string why = "the tray menu never opened";
            for (int attempt = 0; attempt < 3 && !uncloaked; attempt++)
            {
                if (!ForceForeground(host)) { why = "could not put DeGhoster in the foreground"; continue; }

                // Open the modal tray menu (right-click). It appears at the cursor
                // and blocks DeGhoster's thread until a selection is made.
                SetCursorPos(500, 500);
                PostMessage(host, WM_TRAY, IntPtr.Zero, (IntPtr)WM_RBUTTONUP);
                Thread.Sleep(600);
                if (FindWindowEx(IntPtr.Zero, IntPtr.Zero, "#32768", null) == IntPtr.Zero) continue;

                // Menu order: Status Window (default), Active, separator, <window
                // entry>, separator, Settings, separator, About, Exit. Walk to the
                // entry and pick it.
                for (int i = 0; i < WindowEntry; i++) { Key(VK_DOWN); Thread.Sleep(90); }
                Key(VK_RETURN);

                why = "toggling the window via the tray menu did not un-cloak it";
                uncloaked = WaitUntil(() => Cloaked(ghost) == 0, TimeSpan.FromSeconds(6));
                if (!uncloaked) { Key(VK_ESCAPE); Thread.Sleep(300); }   // dismiss a stuck menu
            }
            Assert.True(uncloaked, why);

            // A toggle flips in place: the menu is still there after Enter ...
            Thread.Sleep(300);
            IntPtr menu = TrayMenu(dg);
            Assert.True(menu != IntPtr.Zero, "the tray menu closed after toggling an entry with Enter");

            // ... and after a real click on the same entry, which switches it back.
            // The menu is laid out in physical pixels; read its item and place the
            // cursor in the same (per-monitor aware) coordinates.
            IntPtr hmenu = SendMessage(menu, MN_GETHMENU, IntPtr.Zero, IntPtr.Zero);
            IntPtr dpiContext = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            try
            {
                Assert.True(GetMenuItemRect(menu, hmenu, WindowEntry, out RECT item), "window entry not found in the menu");
                SetCursorPos((item.left + item.right) / 2, (item.top + item.bottom) / 2);
            }
            finally { SetThreadDpiAwarenessContext(dpiContext); }
            Thread.Sleep(200);
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, IntPtr.Zero);
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, IntPtr.Zero);
            Assert.True(WaitUntil(() => Cloaked(ghost) != 0, TimeSpan.FromSeconds(6)),
                        "clicking the window entry did not cloak the window again");
            Assert.True(TrayMenu(dg) != IntPtr.Zero, "the tray menu closed after clicking a toggle entry");

            // Losing the focus is what closes it. While a menu is open no other
            // process may take the foreground, so the focus goes the way the user
            // moves it: a click elsewhere, here on a window of the test's own.
            IntPtr other = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, "STATIC", "", WS_POPUP | WS_VISIBLE,
                                          20, 20, 60, 60, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
            try
            {
                SetCursorPos(50, 50);
                Thread.Sleep(200);
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, IntPtr.Zero);
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, IntPtr.Zero);
                Assert.True(WaitUntil(() => TrayMenu(dg) == IntPtr.Zero, TimeSpan.FromSeconds(3)),
                            "the tray menu stayed open after a click elsewhere");
            }
            finally { DestroyWindow(other); }
        }
        finally { Cleanup(dg, sim); }
    }

    // The open tray menu: the visible menu window of DeGhoster's process.
    private static IntPtr TrayMenu(Process dg)
    {
        IntPtr m = IntPtr.Zero;
        while ((m = FindWindowEx(IntPtr.Zero, m, "#32768", null)) != IntPtr.Zero)
            if (IsWindowVisible(m) && GetWindowThreadProcessId(m, out uint pid) != 0 && pid == (uint)dg.Id)
                return m;
        return IntPtr.Zero;
    }

    [Fact]
    public void Taskbar_flag_starts_hidden_but_still_cloaks()
    {
        // Autostart launches with --taskbar: the engine must run (the ghost is
        // cloaked, asserted inside StartWithGhost) while the main window stays
        // hidden in the tray.
        var (build, dg, sim, ghost, host) = StartWithGhost("64", null, "--taskbar");
        try
        {
            Assert.False(IsWindowVisible(host), "main window should stay hidden with --taskbar");
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
        StartWithGhost(string bits, Dictionary<string, string>? env, string dgArgs = "")
    {
        string build = FindBuildDir();
        EnsureGlobalEnabled();
        // "!" sorts before digits and letters, so this ghost is the first list row
        // even when the machine has windows of its own listed (a real AnyDesk
        // session, "1 234 567 890 - AnyDesk", would otherwise come first).
        string title = "!DGHUI-" + Guid.NewGuid().ToString("N");
        Process sim = Start(Path.Combine(build, $"GhostSim{bits}.exe"), $"--title \"{title}\" --timeout 90", build, null);
        IntPtr ghost = WaitFor(() => FindWindowEx(IntPtr.Zero, IntPtr.Zero, GhostClass, title), TimeSpan.FromSeconds(8));
        Assert.True(ghost != IntPtr.Zero, "ghost window not found");
        Process dg = Start(Path.Combine(build, "DeGhoster.exe"), dgArgs, build, env);
        Assert.True(WaitUntil(() => Cloaked(ghost) != 0, TimeSpan.FromSeconds(20)), "window was not cloaked");
        IntPtr host = WaitFor(() => FindWindowEx(IntPtr.Zero, IntPtr.Zero, HostClass, null), TimeSpan.FromSeconds(5));
        Assert.True(host != IntPtr.Zero, "DeGhoster main window not found");
        return (build, dg, sim, ghost, host);
    }

    // Attaching to the foreground thread's input queue is the documented way to
    // hand the foreground to another window; a bare SetForegroundWindow from a
    // background process is refused.
    private static bool ForceForeground(IntPtr hwnd)
    {
        for (int attempt = 0; attempt < 3; attempt++)
        {
            IntPtr fg = GetForegroundWindow();
            if (fg == hwnd) return true;
            uint fgThread = GetWindowThreadProcessId(fg, out _);
            uint ours = GetCurrentThreadId();
            bool attached = fgThread != 0 && fgThread != ours && AttachThreadInput(ours, fgThread, true);
            try
            {
                SetForegroundWindow(hwnd);
                BringWindowToTop(hwnd);
            }
            finally { if (attached) AttachThreadInput(ours, fgThread, false); }
            Thread.Sleep(300);
            if (GetForegroundWindow() == hwnd) return true;
        }
        return GetForegroundWindow() == hwnd;
    }

    private static void Cleanup(Process? dg, Process? sim)
    {
        try { if (dg is { HasExited: false }) dg.Kill(entireProcessTree: true); } catch { }
        foreach (var n in new[] { "DeGhoster.Helper32", "DeGhoster.Helper64" })
            foreach (var p in Process.GetProcessesByName(n)) { try { p.Kill(); } catch { } }
        try { if (sim is { HasExited: false }) sim.Kill(); } catch { }
    }

    private static string FindBuildDir()
    {
        for (var d = new DirectoryInfo(AppContext.BaseDirectory); d != null; d = d.Parent)
            if (File.Exists(Path.Combine(d.FullName, "build", "DeGhoster.exe")))
                return Path.Combine(d.FullName, "build");
        throw new DirectoryNotFoundException("build\\ not found above " + AppContext.BaseDirectory);
    }

    // Writes into the per-run throwaway root, never the user's real settings.
    private static void EnsureGlobalEnabled() => TestSettings.EnsureGlobalEnabled();

    private static Process Start(string exe, string args, string workDir, Dictionary<string, string>? env)
    {
        var psi = new ProcessStartInfo(exe, args) { UseShellExecute = false, WorkingDirectory = workDir };
        TestSettings.Apply(psi, env);   // throwaway registry root, plus the caller's own entries
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
    [DllImport("user32.dll")]
    private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    private static extern bool BringWindowToTop(IntPtr hwnd);
    [DllImport("user32.dll")]
    private static extern bool AttachThreadInput(uint from, uint to, bool attach);
    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("kernel32.dll")]
    private static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")]
    private static extern bool GetMenuItemRect(IntPtr hwnd, IntPtr menu, int item, out RECT rc);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr CreateWindowEx(uint exStyle, string cls, string title, uint style,
        int x, int y, int w, int h, IntPtr parent, IntPtr menu, IntPtr inst, IntPtr param);
    [DllImport("user32.dll")]
    private static extern bool DestroyWindow(IntPtr hwnd);
    [DllImport("user32.dll")]
    private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("dwmapi.dll")]
    private static extern int DwmGetWindowAttribute(IntPtr hwnd, int attr, out int value, int size);
}
