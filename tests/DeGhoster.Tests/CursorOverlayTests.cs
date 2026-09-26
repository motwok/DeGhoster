// Copyright (c) Emmo Emminghaus mo2000 at mo2000 dot de
// SPDX-License-Identifier: AGPL-3.0-or-later

using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32;
using Xunit;

namespace DeGhoster.Tests;

// The AnyDesk cursor overlay (Specification.md section 10) against CursorSim, a
// window of AnyDesk's session class that sets its own cursors. These tests move
// the real mouse cursor. On a desktop without a visible cursor (no mouse
// attached) there is nothing to enlarge, so the cursor-driven ones are skipped.
[Trait("Category", "Integration")]
public class CursorOverlayTests
{
    private const string OverlayClass = "DeGhosterCursorOverlay";
    private const string HostClass = "DeGhosterMainWindow";
    private const string SettingsClass = "DeGhosterSettingsWindow";
    private const uint WM_COMMAND = 0x0111, WM_KEYDOWN = 0x0100, WM_NULL = 0x0000;
    private const int IDC_POWER = 1002, IDC_SETTINGS = 1005;
    private const int IDC_CURSOR_TOGGLE = 1101, IDC_CURSOR_ZOOM = 1102, IDCANCEL = 2;
    private const int VK_SPACE = 0x20, VK_END = 0x23, VK_HOME = 0x24;
    private const int SimX = 200, SimY = 200, SimW = 480, SimH = 360, Band = 40;
    private const int CURSOR_SHOWING = 1;

    [SkippableTheory]
    [InlineData("64")]
    [InlineData("32")]   // the real AnyDesk client is a 32-bit process
    public void Overlay_shows_the_enlarged_cursor_at_the_hotspot(string bits)
    {
        using var run = Run.Start(bits, "alpha", zoom: 300);
        var (x, y) = run.MoveInside();
        IntPtr ov = run.WaitOverlay();
        AssertRect(ov, x - 6, y - 9, 48, 72);   // 16x24, hotspot (2,3), all x3

        // It follows the mouse.
        run.MoveTo(x + 37, y + 21);
        Assert.True(WaitUntil(() => Math.Abs(Rect(ov).left - (x + 37 - 6)) <= 1 && Rect(ov).top == y + 21 - 9,
                              TimeSpan.FromSeconds(3)), $"overlay did not follow: {Describe(ov)}");
    }

    [SkippableTheory]
    [InlineData("mask")]
    [InlineData("mono")]
    public void Overlay_handles_every_cursor_kind(string kind)
    {
        using var run = Run.Start("64", kind, zoom: 300);
        var (x, y) = run.MoveInside();
        IntPtr ov = run.WaitOverlay();
        if (kind == "mask")
        {
            AssertRect(ov, x, y, 48, 72);   // 16x24 without alpha, hotspot (0,0)
        }
        else
        {
            // 32x32 monochrome with inverting pixels: outlined, so the canvas grows
            // by the outline radius (DPI of the monitor) on every side.
            int r = Math.Max(1, (int)Math.Round(MonitorDpi(x, y) / 96.0 * 0.8, MidpointRounding.AwayFromZero));
            int side = (32 + 2 * r) * 3, hot = (16 + r) * 3;
            AssertRect(ov, x - hot, y - hot, side, side);
        }
    }

    [SkippableFact]
    public void Overlay_stays_off_over_system_cursors_other_windows_and_outside()
    {
        using var run = Run.Start("64", "alpha", zoom: 300);
        run.MoveInside();
        IntPtr ov = run.WaitOverlay();

        // AnyDesk's own UI (title bar, tabs) uses system cursors.
        run.MoveTo(SimX + SimW / 2, SimY + Band / 2);
        Assert.True(WaitUntil(() => !IsWindowVisible(ov), TimeSpan.FromSeconds(3)), "overlay over a system cursor");

        run.MoveInside();
        Assert.True(WaitUntil(() => IsWindowVisible(ov), TimeSpan.FromSeconds(3)), "overlay did not come back");

        // Outside the AnyDesk window.
        run.MoveTo(SimX + SimW + 120, SimY + SimH + 60);
        Assert.True(WaitUntil(() => !IsWindowVisible(ov), TimeSpan.FromSeconds(3)), "overlay outside AnyDesk");

        // A window with the same custom cursors but another class is not AnyDesk.
        using var other = Sim.Start("64", "alpha", "NotAnyDesk#1", SimX + SimW + 20, SimY, 200, 200);
        run.MoveTo(SimX + SimW + 120, SimY + 120);
        Thread.Sleep(600);
        Assert.False(IsWindowVisible(ov), "overlay over a window that is not AnyDesk");
    }

    [SkippableFact]
    public void Rapid_shape_changes_keep_the_host_responsive()
    {
        using var run = Run.Start("64", "switch", zoom: 300);
        run.MoveInside();
        IntPtr ov = run.WaitOverlay();

        // CursorSim swaps two cursors every 25 ms: every change is a re-render.
        var sw = Stopwatch.StartNew();
        while (sw.ElapsedMilliseconds < 2000)
        {
            Assert.True(SendMessageTimeout(run.Host, WM_NULL, IntPtr.Zero, IntPtr.Zero, 2, 500, out _) != IntPtr.Zero,
                        "DeGhoster stopped answering during rapid shape changes");
            Thread.Sleep(100);
        }
        Assert.True(IsWindowVisible(ov), "overlay vanished during rapid shape changes");
        Assert.Equal(48, Rect(ov).right - Rect(ov).left);
    }

    [SkippableFact]
    public void Global_switch_and_settings_window_control_the_overlay()
    {
        using var run = Run.Start("64", "alpha", zoom: 300);
        run.MoveInside();
        IntPtr ov = run.WaitOverlay();

        // Global switch off hides it; on again brings it back on the next move.
        PostMessage(run.Host, WM_COMMAND, (IntPtr)IDC_POWER, IntPtr.Zero);
        Assert.True(WaitUntil(() => !IsWindowVisible(ov), TimeSpan.FromSeconds(3)), "global off kept the overlay");
        PostMessage(run.Host, WM_COMMAND, (IntPtr)IDC_POWER, IntPtr.Zero);
        run.MoveInside();
        Assert.True(WaitUntil(() => IsWindowVisible(ov), TimeSpan.FromSeconds(3)), "global on did not restore it");

        // Settings window: single instance, the switch and the slider apply live.
        PostMessage(run.Host, WM_COMMAND, (IntPtr)IDC_SETTINGS, IntPtr.Zero);
        IntPtr settings = WaitFor(() => FindWindow(SettingsClass, null), TimeSpan.FromSeconds(5));
        Assert.True(settings != IntPtr.Zero, "the settings window did not open");
        PostMessage(run.Host, WM_COMMAND, (IntPtr)IDC_SETTINGS, IntPtr.Zero);
        Thread.Sleep(300);
        Assert.Equal(settings, FindWindow(SettingsClass, null));
        Assert.Equal(IntPtr.Zero, FindWindowEx(IntPtr.Zero, settings, SettingsClass, null));

        IntPtr toggle = GetDlgItem(settings, IDC_CURSOR_TOGGLE), zoom = GetDlgItem(settings, IDC_CURSOR_ZOOM);
        SendMessage(toggle, WM_KEYDOWN, (IntPtr)VK_SPACE, IntPtr.Zero);
        Assert.True(WaitUntil(() => !IsWindowVisible(ov), TimeSpan.FromSeconds(3)), "switching off kept the overlay");
        Assert.Equal(0, ReadDword("CursorOverlayEnabled"));
        SendMessage(toggle, WM_KEYDOWN, (IntPtr)VK_SPACE, IntPtr.Zero);
        Assert.Equal(1, ReadDword("CursorOverlayEnabled"));

        SendMessage(zoom, WM_KEYDOWN, (IntPtr)VK_END, IntPtr.Zero);
        Assert.Equal(600, ReadDword("CursorOverlayZoom"));
        var (x, y) = run.MoveInside();
        IntPtr ov2 = run.WaitOverlay();
        AssertRect(ov2, x - 12, y - 18, 96, 144);
        SendMessage(zoom, WM_KEYDOWN, (IntPtr)VK_HOME, IntPtr.Zero);
        Assert.Equal(100, ReadDword("CursorOverlayZoom"));
        Assert.True(WaitUntil(() => Rect(ov2).right - Rect(ov2).left == 16, TimeSpan.FromSeconds(3)),
                    $"the zoom did not apply live: {Describe(ov2)}");

        PostMessage(settings, WM_COMMAND, (IntPtr)IDCANCEL, IntPtr.Zero);
        Assert.True(WaitUntil(() => FindWindow(SettingsClass, null) == IntPtr.Zero, TimeSpan.FromSeconds(3)),
                    "Esc did not close the settings window");
    }

    [SkippableFact]
    public void Clicks_pass_through_the_overlay()
    {
        using var run = Run.Start("64", "alpha", zoom: 400);
        var (x, y) = run.MoveInside();
        run.WaitOverlay();
        int before = run.Sim.Clicks();
        ClickAt(x, y);
        Assert.True(WaitUntil(() => run.Sim.Clicks() == before + 1, TimeSpan.FromSeconds(3)),
                    "the click did not reach the AnyDesk window under the overlay");
    }

    [SkippableFact]
    public void Quit_removes_the_overlay_and_restores_the_cursor()
    {
        using var run = Run.Start("64", "alpha", zoom: 300);
        run.MoveInside();
        run.WaitOverlay();

        var quit = Process.Start(new ProcessStartInfo(Path.Combine(run.Build, "DeGhoster.exe"), "--quit")
                                 { UseShellExecute = false });
        Assert.True(quit!.WaitForExit(40000), "the quit switch did not return");
        Assert.True(run.Dg.WaitForExit(10000), "DeGhoster did not exit");
        Assert.Equal(IntPtr.Zero, FindWindow(OverlayClass, null));
        Assert.True((CursorInfo().flags & CURSOR_SHOWING) != 0, "the cursor was left hidden");
    }

    [Fact]
    public void First_start_stores_the_monitor_scaling_as_zoom()
    {
        TestSettings.EnsureGlobalEnabled();
        using (var key = Registry.CurrentUser.CreateSubKey(TestSettings.Root, writable: true))
            key.DeleteValue("CursorOverlayZoom", throwOnMissingValue: false);
        using var run = Run.Start("64", "alpha", zoom: null, needCursor: false);
        Assert.True(WaitUntil(() => ReadDword("CursorOverlayZoom") >= 100, TimeSpan.FromSeconds(5)),
                    "no zoom was stored on first start");
        int z = ReadDword("CursorOverlayZoom");
        Assert.InRange(z, 100, 600);
        Assert.Equal(0, z % 10);
    }

    [Theory]
    [InlineData("ar-SA", "dark")]
    [InlineData("de-DE", "light")]
    public void Settings_window_opens_in_every_layout(string lang, string theme)
    {
        using var run = Run.Start("64", "alpha", zoom: 250, needCursor: false,
            env: new Dictionary<string, string> { ["DEGHOSTER_UILANG"] = lang, ["DEGHOSTER_FORCE_THEME"] = theme });
        PostMessage(run.Host, WM_COMMAND, (IntPtr)IDC_SETTINGS, IntPtr.Zero);
        IntPtr settings = WaitFor(() => FindWindow(SettingsClass, null), TimeSpan.FromSeconds(5));
        Assert.True(settings != IntPtr.Zero, "the settings window did not open");
        Assert.True(Rect(settings).right - Rect(settings).left > 200, "the settings window has no size");
        IntPtr zoom = GetDlgItem(settings, IDC_CURSOR_ZOOM);
        SendMessage(zoom, WM_KEYDOWN, (IntPtr)VK_END, IntPtr.Zero);
        Assert.Equal(600, ReadDword("CursorOverlayZoom"));
        PostMessage(settings, WM_COMMAND, (IntPtr)IDCANCEL, IntPtr.Zero);
        Assert.True(WaitUntil(() => FindWindow(SettingsClass, null) == IntPtr.Zero, TimeSpan.FromSeconds(3)),
                    "the settings window did not close");
    }

    // ---- harness ----------------------------------------------------------------

    // One CursorSim + one DeGhoster (tray only, --taskbar) with the given zoom.
    private sealed class Run : IDisposable
    {
        public string Build = "";
        public Sim Sim = null!;
        public Process Dg = null!;
        public IntPtr Host;

        public static Run Start(string bits, string kind, int? zoom, bool needCursor = true,
                                Dictionary<string, string>? env = null)
        {
            SetThreadDpiAwarenessContext(new IntPtr(-4));   // physical pixels, like DeGhoster
            var r = new Run { Build = FindBuildDir() };
            TestSettings.EnsureGlobalEnabled();
            using (var key = Registry.CurrentUser.CreateSubKey(TestSettings.Root, writable: true))
            {
                key.SetValue("CursorOverlayEnabled", 1, RegistryValueKind.DWord);
                if (zoom.HasValue) key.SetValue("CursorOverlayZoom", zoom.Value, RegistryValueKind.DWord);
            }
            try
            {
                r.Sim = Sim.Start(bits, kind, "ad_win#1", SimX, SimY, SimW, SimH);
                var psi = new ProcessStartInfo(Path.Combine(r.Build, "DeGhoster.exe"), "--taskbar")
                          { UseShellExecute = false, WorkingDirectory = r.Build };
                TestSettings.Apply(psi, env);
                r.Dg = Process.Start(psi)!;
                r.Host = WaitFor(() => FindWindow(HostClass, null), TimeSpan.FromSeconds(10));
                Assert.True(r.Host != IntPtr.Zero, "DeGhoster main window not found");
                if (needCursor)
                {
                    r.MoveInside();
                    Skip.If((CursorInfo().flags & CURSOR_SHOWING) == 0,
                            "no visible mouse cursor on this desktop (no mouse attached)");
                }
                return r;
            }
            catch
            {
                r.Dispose();
                throw;
            }
        }

        // Somewhere below the system-cursor band; moves twice so a cursor event
        // is raised even if the cursor already was there.
        public (int x, int y) MoveInside()
        {
            int x = SimX + SimW / 2, y = SimY + SimH / 2;
            MoveTo(x + 1, y + 1);
            MoveTo(x, y);
            return (x, y);
        }

        // Glides there in small steps like a real mouse instead of one jump: under
        // OpenCppCoverage (a debugger) a single isolated cursor event can go
        // missing, while a real mouse always delivers a stream of them.
        public void MoveTo(int x, int y)
        {
            GetCursorPos(out POINT from);
            const int steps = 6;
            for (int i = 1; i <= steps; i++)
            {
                SetCursorPos(from.x + (x - from.x) * i / steps, from.y + (y - from.y) * i / steps);
                Thread.Sleep(25);
            }
            Thread.Sleep(80);
            SetCursorPos(x + 1, y);   // settle: two more events around the final position
            Thread.Sleep(40);
            SetCursorPos(x, y);
            Thread.Sleep(80);
        }

        public IntPtr WaitOverlay()
        {
            IntPtr ov = IntPtr.Zero;
            bool ok = WaitUntil(() =>
            {
                ov = FindWindow(OverlayClass, null);
                return ov != IntPtr.Zero && IsWindowVisible(ov);
            }, TimeSpan.FromSeconds(6));
            Assert.True(ok, "the overlay did not appear over the AnyDesk window");
            Thread.Sleep(150);   // let a coalesced last move land
            return ov;
        }

        public void Dispose()
        {
            try { if (Dg is { HasExited: false }) Dg.Kill(entireProcessTree: true); } catch { }
            foreach (var n in new[] { "DeGhoster.Helper32", "DeGhoster.Helper64" })
                foreach (var p in Process.GetProcessesByName(n)) { try { p.Kill(); } catch { } }
            Sim?.Dispose();
        }
    }

    private sealed class Sim : IDisposable
    {
        public Process P = null!;
        public IntPtr Hwnd;

        public static Sim Start(string bits, string kind, string cls, int x, int y, int w, int h)
        {
            string build = FindBuildDir();
            string title = "ADSIM-" + Guid.NewGuid().ToString("N");
            var psi = new ProcessStartInfo(Path.Combine(build, $"CursorSim{bits}.exe"),
                $"--kind {kind} --class \"{cls}\" --title {title} --x {x} --y {y} --w {w} --h {h} --band {Band} --timeout 120")
                { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = build };
            var s = new Sim { P = Process.Start(psi)! };
            s.Hwnd = WaitFor(() => FindWindow(cls, title), TimeSpan.FromSeconds(8));
            Assert.True(s.Hwnd != IntPtr.Zero, "CursorSim window not found");
            return s;
        }

        public int Clicks()
        {
            var sb = new StringBuilder(256);
            GetWindowText(Hwnd, sb, sb.Capacity);
            var t = sb.ToString();
            int i = t.IndexOf("CLICKS=", StringComparison.Ordinal);
            return i >= 0 && int.TryParse(t[(i + 7)..], out int n) ? n : 0;
        }

        public void Dispose() { try { if (!P.HasExited) P.Kill(); } catch { } }
    }

    private static void AssertRect(IntPtr ov, int left, int top, int w, int h)
    {
        bool ok = WaitUntil(() =>
        {
            var r = Rect(ov);
            return Math.Abs(r.left - left) <= 1 && Math.Abs(r.top - top) <= 1 &&
                   r.right - r.left == w && r.bottom - r.top == h;
        }, TimeSpan.FromSeconds(3));
        Assert.True(ok, $"expected {w}x{h} at ({left},{top}), got {Describe(ov)}");
    }

    private static string Describe(IntPtr h)
    {
        var r = Rect(h);
        return $"{r.right - r.left}x{r.bottom - r.top} at ({r.left},{r.top}), visible={IsWindowVisible(h)}";
    }

    private static RECT Rect(IntPtr h) { GetWindowRect(h, out RECT r); return r; }

    private static int ReadDword(string name)
    {
        using var key = Registry.CurrentUser.OpenSubKey(TestSettings.Root);
        return key?.GetValue(name) is int v ? v : -1;
    }

    private static uint MonitorDpi(int x, int y)
    {
        IntPtr mon = MonitorFromPoint(new POINT { x = x, y = y }, 2);
        return GetDpiForMonitor(mon, 0, out uint dx, out _) == 0 ? dx : 96;
    }

    private static CURSORINFO CursorInfo()
    {
        var ci = new CURSORINFO { cbSize = Marshal.SizeOf<CURSORINFO>() };
        GetCursorInfo(ref ci);
        return ci;
    }

    private static void ClickAt(int x, int y)
    {
        SetCursorPos(x, y);
        Thread.Sleep(80);
        var inputs = new[]
        {
            new INPUT { type = 0, mi = new MOUSEINPUT { dwFlags = 0x0002 } },   // left down
            new INPUT { type = 0, mi = new MOUSEINPUT { dwFlags = 0x0004 } },   // left up
        };
        SendInput((uint)inputs.Length, inputs, Marshal.SizeOf<INPUT>());
    }

    private static string FindBuildDir()
    {
        for (var d = new DirectoryInfo(AppContext.BaseDirectory); d != null; d = d.Parent)
            if (File.Exists(Path.Combine(d.FullName, "build", "DeGhoster.exe")))
                return Path.Combine(d.FullName, "build");
        throw new DirectoryNotFoundException("build\\ not found above " + AppContext.BaseDirectory);
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
        do { if (cond()) return true; Thread.Sleep(100); } while (DateTime.UtcNow < end);
        return cond();
    }

    [StructLayout(LayoutKind.Sequential)] private struct RECT { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)] private struct POINT { public int x, y; }
    [StructLayout(LayoutKind.Sequential)] private struct CURSORINFO { public int cbSize; public int flags; public IntPtr hCursor; public POINT pt; }
    [StructLayout(LayoutKind.Sequential)] private struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)] private struct INPUT { public uint type; public MOUSEINPUT mi; }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr FindWindow(string cls, string? title);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string? title);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] private static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] private static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] private static extern bool GetCursorPos(out POINT pt);
    [DllImport("user32.dll")] private static extern bool GetCursorInfo(ref CURSORINFO ci);
    [DllImport("user32.dll")] private static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] private static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")]
    private static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, IntPtr w, IntPtr l, uint flags, uint ms, out IntPtr result);
    [DllImport("user32.dll")] private static extern IntPtr GetDlgItem(IntPtr parent, int id);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(IntPtr h, StringBuilder s, int max);
    [DllImport("user32.dll")] private static extern uint SendInput(uint n, INPUT[] inputs, int cb);
    [DllImport("user32.dll")] private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] private static extern IntPtr MonitorFromPoint(POINT pt, uint flags);
    [DllImport("shcore.dll")] private static extern int GetDpiForMonitor(IntPtr mon, int type, out uint x, out uint y);
}
