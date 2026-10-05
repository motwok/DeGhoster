// Copyright (c) Emmo Emminghaus mo2000 at mo2000 dot de
// SPDX-License-Identifier: AGPL-3.0-or-later

using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32;
using Xunit;

namespace DeGhoster.Tests;

// The AnyDesk cursor overlay (Specification.md section 10) against CursorSim, a
// window of AnyDesk's session class that sets its own cursors. The cursor-driven
// tests move the real mouse cursor, so they are in the InteractiveInput category:
// the build server has no visible cursor and filters them out, and
// tests\coverage.ps1 runs them locally and commits their coverage for the combined
// report. Anywhere else without a visible cursor they are skipped.
[Trait("Category", "Integration")]
public class CursorOverlayTests
{
    private const string OverlayClass = "DeGhosterCursorOverlay";
    private const string HostClass = "DeGhosterMainWindow";
    private const string SettingsClass = "DeGhosterSettingsWindow";
    private const uint WM_COMMAND = 0x0111, WM_KEYDOWN = 0x0100, WM_NULL = 0x0000;
    private const uint WM_WTSSESSION_CHANGE = 0x02B1;
    private const int WTS_SESSION_LOCK = 7, WTS_SESSION_UNLOCK = 8;
    private const int IDC_POWER = 1002, IDC_SETTINGS = 1005;
    private const int IDC_CURSOR_AUTO = 1101, IDC_CURSOR_ZOOM = 1102, IDCANCEL = 2, IDC_LIST = 1001;
    private const uint WM_EYE_TOGGLE = 0x8041 /* WM_APP+0x41 */, LVM_GETITEMCOUNT = 0x1004;
    private const int VK_SPACE = 0x20, VK_END = 0x23, VK_HOME = 0x24;
    private const int SimX = 200, SimY = 200, SimW = 480, SimH = 360, Band = 40;
    private const int CURSOR_SHOWING = 1;

    [SkippableTheory]

    [Trait("Category", "InteractiveInput")]
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

    [Trait("Category", "InteractiveInput")]
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
            // 32x32 monochrome with inverting pixels: they really invert the screen,
            // so there is no outline and the canvas keeps its size. The inverting
            // pixels are in a second window of the same size on top of it.
            AssertRect(ov, x - 48, y - 48, 96, 96);
            IntPtr xor = FindWindow(XorClass, null);
            Assert.True(WaitUntil(() => IsWindowVisible(xor) && Rect(xor).left == Rect(ov).left &&
                                        Rect(xor).top == Rect(ov).top && Rect(xor).right == Rect(ov).right,
                                  TimeSpan.FromSeconds(3)), $"no inverting window over the overlay: {Describe(xor)}");
        }
    }

    [SkippableFact]

    [Trait("Category", "InteractiveInput")]
    public void Inverting_pixels_get_a_window_left_out_of_screen_captures()
    {
        using var run = Run.Start("64", "alpha", zoom: 300);
        run.MoveInside();
        IntPtr ov = run.WaitOverlay();
        IntPtr xor = FindWindow(XorClass, null);
        Assert.True(xor != IntPtr.Zero, "this Windows has no window for inverting pixels");
        Assert.Equal(WDA_EXCLUDEFROMCAPTURE, DisplayAffinity(xor));   // it must never read itself
        Assert.Equal(WDA_NONE, DisplayAffinity(ov));                  // the overlay itself is captured normally
        Assert.False(IsWindowVisible(xor), "an alpha cursor has no inverting pixels to show");

        using var mono = Sim.Start("64", "mono", "ad_win#2", SimX + SimW + 20, SimY, 200, 200);
        run.MoveTo(SimX + SimW + 120, SimY + 120);
        Assert.True(WaitUntil(() => IsWindowVisible(ov) && IsWindowVisible(xor), TimeSpan.FromSeconds(3)),
                    "an inverting cursor did not show its inverting window");

        // It follows the mouse, and a resting mouse keeps it (the refresh timer).
        run.MoveTo(SimX + SimW + 90, SimY + 150);
        Assert.True(WaitUntil(() => Rect(xor).left == Rect(ov).left && Rect(xor).top == Rect(ov).top,
                              TimeSpan.FromSeconds(3)), "the inverting window did not follow the overlay");
        Thread.Sleep(300);
        Assert.True(IsWindowVisible(xor), "the inverting window vanished under a resting mouse");

        // Back on a cursor without inverting pixels, and on a system cursor: gone.
        run.MoveInside();
        Assert.True(WaitUntil(() => IsWindowVisible(ov) && !IsWindowVisible(xor), TimeSpan.FromSeconds(3)),
                    "the inverting window stayed over an ordinary cursor");
        run.MoveTo(SimX + SimW + 120, SimY + 120);
        Assert.True(WaitUntil(() => IsWindowVisible(xor), TimeSpan.FromSeconds(3)), "the inverting window did not come back");
        run.MoveTo(SimX + SimW / 2, SimY + Band / 2);
        Assert.True(WaitUntil(() => !IsWindowVisible(ov) && !IsWindowVisible(xor), TimeSpan.FromSeconds(3)),
                    "the inverting window stayed when the overlay was hidden");
    }

    private const string XorClass = "DeGhosterCursorOverlayXor";
    private const uint WDA_NONE = 0x0, WDA_EXCLUDEFROMCAPTURE = 0x11;

    private static uint DisplayAffinity(IntPtr h) => GetWindowDisplayAffinity(h, out uint a) ? a : 0xFFFFFFFF;

    [DllImport("user32.dll")] private static extern bool GetWindowDisplayAffinity(IntPtr h, out uint affinity);

    [SkippableFact]

    [Trait("Category", "InteractiveInput")]
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

        // Outside the AnyDesk window: over another program's own UI. A window of
        // this test's, so a real remote session on the desktop cannot be there.
        using var other = Sim.Start("64", "alpha", "NotAnyDesk#1", SimX + SimW + 20, SimY, 200, 200);
        run.MoveTo(SimX + SimW + 120, SimY + Band / 2);
        Assert.True(WaitUntil(() => !IsWindowVisible(ov), TimeSpan.FromSeconds(3)), "overlay outside AnyDesk");

        // A window with the same custom cursors but another class is not AnyDesk.
        run.MoveInside();
        Assert.True(WaitUntil(() => IsWindowVisible(ov), TimeSpan.FromSeconds(3)), "overlay did not come back");
        run.MoveTo(SimX + SimW + 120, SimY + 120);
        Thread.Sleep(600);
        Assert.False(IsWindowVisible(ov), "overlay over a window that is not AnyDesk");
    }

    [SkippableFact]

    [Trait("Category", "InteractiveInput")]
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

    [Trait("Category", "InteractiveInput")]
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

        // Settings window: single instance, the slider applies live.
        PostMessage(run.Host, WM_COMMAND, (IntPtr)IDC_SETTINGS, IntPtr.Zero);
        IntPtr settings = WaitFor(() => FindWindow(SettingsClass, null), TimeSpan.FromSeconds(5));
        Assert.True(settings != IntPtr.Zero, "the settings window did not open");
        PostMessage(run.Host, WM_COMMAND, (IntPtr)IDC_SETTINGS, IntPtr.Zero);
        Thread.Sleep(300);
        Assert.Equal(settings, FindWindow(SettingsClass, null));
        Assert.Equal(IntPtr.Zero, FindWindowEx(IntPtr.Zero, settings, SettingsClass, null));

        IntPtr zoom = GetDlgItem(settings, IDC_CURSOR_ZOOM);
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

    [Trait("Category", "InteractiveInput")]
    public void Auto_sizes_the_cursor_like_the_local_arrow_and_can_be_switched_off()
    {
        using var run = Run.Start("64", "alpha", zoom: null);   // Auto on
        var (x, y) = run.MoveInside();
        IntPtr ov = run.WaitOverlay();

        // The only picture shown is the reference, so it is made about as tall as
        // the local arrow (the zoom sits on a 10 % grid and within 100-600 %).
        int target = Math.Clamp(LocalArrowHeight(MonitorDpi(x, y)), 24, 24 * 6);
        Assert.True(WaitUntil(() => Math.Abs(Height(ov) - target) <= Math.Max(3, target / 10), TimeSpan.FromSeconds(3)),
                    $"expected about {target} px (the local arrow), got {Describe(ov)}");
        Assert.Equal(Height(ov) * 2 / 3, Rect(ov).right - Rect(ov).left, 2.0);   // 16x24 keeps its aspect

        // Auto off in the settings window: the fixed zoom (100 %) applies live.
        using (var key = Registry.CurrentUser.CreateSubKey(TestSettings.Root, writable: true))
            key.SetValue("CursorOverlayZoom", 100, RegistryValueKind.DWord);
        PostMessage(run.Host, WM_COMMAND, (IntPtr)IDC_SETTINGS, IntPtr.Zero);
        IntPtr settings = WaitFor(() => FindWindow(SettingsClass, null), TimeSpan.FromSeconds(5));
        Assert.True(settings != IntPtr.Zero, "the settings window did not open");
        IntPtr autoSw = GetDlgItem(settings, IDC_CURSOR_AUTO), zoomSl = GetDlgItem(settings, IDC_CURSOR_ZOOM);
        Assert.False(IsWindowEnabled(zoomSl), "the fixed zoom is not greyed out while Auto is on");
        SendMessage(autoSw, WM_KEYDOWN, (IntPtr)VK_SPACE, IntPtr.Zero);
        Assert.Equal(0, ReadDword("CursorOverlayAuto"));
        Assert.True(IsWindowEnabled(zoomSl), "switching Auto off did not enable the fixed zoom");
        SendMessage(zoomSl, WM_KEYDOWN, (IntPtr)VK_HOME, IntPtr.Zero);   // 100 %
        run.MoveInside();
        AssertRect(ov, x - 2, y - 3, 16, 24);
        PostMessage(settings, WM_COMMAND, (IntPtr)IDCANCEL, IntPtr.Zero);
    }

    private static int Height(IntPtr h) => Rect(h).bottom - Rect(h).top;

    // The local arrow's height on screen: the visible rows (at least 50 % opaque) of the system arrow's
    // picture (as a per-monitor aware thread gets it) times DPI/96. The standard
    // arrows have an alpha channel; a monochrome scheme is counted by its AND mask.
    private static int LocalArrowHeight(uint dpi)
    {
        IntPtr arrow = LoadCursor(IntPtr.Zero, (IntPtr)32512);
        if (!GetIconInfo(arrow, out ICONINFO ii)) return 0;
        try
        {
            bool color = ii.hbmColor != IntPtr.Zero;
            IntPtr bmp = color ? ii.hbmColor : ii.hbmMask;
            GetObject(bmp, Marshal.SizeOf<BITMAP>(), out BITMAP bm);
            int w = bm.bmWidth, rows = color ? bm.bmHeight : bm.bmHeight / 2;
            var bi = new BITMAPINFOHEADER { biSize = Marshal.SizeOf<BITMAPINFOHEADER>(), biWidth = w, biHeight = -rows, biPlanes = 1, biBitCount = 32 };
            var px = new int[w * rows];
            IntPtr dc = GetDC(IntPtr.Zero);
            GetDIBits(dc, bmp, 0, (uint)rows, px, ref bi, 0);
            ReleaseDC(IntPtr.Zero, dc);
            int top = -1, bottom = -1;
            for (int y = 0; y < rows; y++)
                for (int x = 0; x < w; x++)
                {
                    int p = px[y * w + x];
                    bool visible = color ? ((uint)p >> 24) >= 128 : (p & 0xFFFFFF) == 0;
                    if (visible) { if (top < 0) top = y; bottom = y; break; }
                }
            return top < 0 ? 0 : (bottom - top + 1) * (int)dpi / 96;
        }
        finally
        {
            if (ii.hbmColor != IntPtr.Zero) DeleteObject(ii.hbmColor);
            if (ii.hbmMask != IntPtr.Zero) DeleteObject(ii.hbmMask);
        }
    }

    [StructLayout(LayoutKind.Sequential)] private struct ICONINFO { public bool fIcon; public int xHotspot, yHotspot; public IntPtr hbmMask, hbmColor; }
    [StructLayout(LayoutKind.Sequential)] private struct BITMAP { public int bmType, bmWidth, bmHeight, bmWidthBytes; public short bmPlanes, bmBitsPixel; public IntPtr bmBits; }
    [StructLayout(LayoutKind.Sequential)] private struct BITMAPINFOHEADER { public int biSize, biWidth, biHeight; public short biPlanes, biBitCount; public int biCompression, biSizeImage, biXPelsPerMeter, biYPelsPerMeter, biClrUsed, biClrImportant; }
    [DllImport("user32.dll")] private static extern IntPtr LoadCursor(IntPtr inst, IntPtr id);
    [DllImport("user32.dll")] private static extern bool GetIconInfo(IntPtr icon, out ICONINFO ii);
    [DllImport("gdi32.dll")] private static extern int GetObject(IntPtr h, int size, out BITMAP bm);
    [DllImport("gdi32.dll")] private static extern int GetDIBits(IntPtr dc, IntPtr bmp, uint start, uint lines, int[] bits, ref BITMAPINFOHEADER bi, uint usage);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr h);
    [DllImport("user32.dll")] private static extern IntPtr GetDC(IntPtr h);
    [DllImport("user32.dll")] private static extern int ReleaseDC(IntPtr h, IntPtr dc);
    [DllImport("user32.dll")] private static extern bool IsWindowEnabled(IntPtr h);

    [SkippableFact]

    [Trait("Category", "InteractiveInput")]
    public void AnyDesk_windows_share_their_programs_switch()
    {
        using var run = Run.Start("64", "alpha", zoom: 300);
        using var second = Sim.Start("64", "alpha", "ad_win#2", SimX + SimW + 20, SimY, 200, 200);
        run.MoveInside();
        IntPtr ov = run.WaitOverlay();
        string program = "\\CursorSim64.exe";

        // What a click on the first window's eye in the status list posts.
        PostMessage(run.Host, WM_EYE_TOGGLE, run.Sim.Hwnd, IntPtr.Zero);
        Assert.True(WaitUntil(() => !IsWindowVisible(ov), TimeSpan.FromSeconds(3)), "switching the window off kept the overlay");
        Assert.True(WaitUntil(() => DisabledKeys().Any(k => k.EndsWith(program, StringComparison.OrdinalIgnoreCase)),
                              TimeSpan.FromSeconds(3)),
                    "the opt-out was not stored as the program's path");
        Assert.DoesNotContain(DisabledKeys(), k => k.Contains('|'));
        run.MoveInside();
        Thread.Sleep(400);
        Assert.False(IsWindowVisible(ov), "the overlay came back over a window that is switched off");

        // The second window of the same program is switched off with it.
        run.MoveTo(SimX + SimW + 120, SimY + 120);
        Thread.Sleep(400);
        Assert.False(IsWindowVisible(ov), "the other window of the program still got an overlay");

        // Switching on through the second window's eye brings both back.
        PostMessage(run.Host, WM_EYE_TOGGLE, second.Hwnd, IntPtr.Zero);
        Assert.True(WaitUntil(() => IsWindowVisible(ov), TimeSpan.FromSeconds(3)),
                    "switching the program on through its other window did not bring the overlay back");
        run.MoveInside();
        Assert.True(WaitUntil(() => IsWindowVisible(ov), TimeSpan.FromSeconds(3)), "the first window stayed switched off");
        Assert.False(DisabledKeys().Any(k => k.EndsWith(program, StringComparison.OrdinalIgnoreCase)),
                     "the opt-out was not removed");
    }

    [Fact]
    public void AnyDesk_windows_are_listed_and_counted()
    {
        using var run = Run.Start("64", "alpha", zoom: 300, needCursor: false);
        Assert.True(WaitUntil(() => Count(run.Host) >= 1, TimeSpan.FromSeconds(5)), "the AnyDesk window was not counted");
        // The title updates window by window while the startup scan runs (and a
        // developer machine has windows of its own), so wait until it settles.
        int with = StableCount(run.Host);

        // A selected row stays selected when the list is rebuilt for a new window.
        IntPtr list = GetDlgItem(run.Host, IDC_LIST);
        SendMessage(list, WM_KEYDOWN, (IntPtr)VK_HOME, IntPtr.Zero);
        Assert.True(Selected(list) == 0, "the first row could not be selected");

        using (Sim.Start("64", "alpha", "ad_win#2", SimX + SimW + 20, SimY, 200, 200))
        {
            Assert.True(WaitUntil(() => Count(run.Host) == with + 1, TimeSpan.FromSeconds(5)),
                        "a second AnyDesk window was not counted as a window of its own");
            Assert.True(Selected(list) >= 0, "the rebuilt list lost the selection");
        }
        Assert.True(WaitUntil(() => Count(run.Host) == with, TimeSpan.FromSeconds(5)),
                    $"a closed AnyDesk window was not dropped from the count ({with} -> {Count(run.Host)})");
        Assert.True((int)SendMessage(GetDlgItem(run.Host, IDC_LIST), LVM_GETITEMCOUNT, IntPtr.Zero, IntPtr.Zero) == with,
                    "the list does not show every counted window");
    }

    // ---- RDP control (mstsc, WSLg, connection managers) ---------------------------

    private const string RdpHostClass = "RdpHostSim";
    private const string RdpChain = "default";   // UIMainClass > UIContainerClass > IHWindowClass

    [SkippableTheory]

    [Trait("Category", "InteractiveInput")]
    [InlineData("64")]
    [InlineData("32")]
    public void Rdp_overlay_shows_over_the_input_window_only(string bits)
    {
        using var run = Run.Start(bits, "alpha", zoom: 300, rdp: RdpChain);
        var (x, y) = run.MoveInside();
        IntPtr ov = run.WaitOverlay();
        AssertRect(ov, x - 6, y - 9, 48, 72);   // 16x24, hotspot (2,3), all x3

        // The host's own UI (here the band above the control) is not the session.
        run.MoveTo(SimX + SimW / 2, SimY + Band / 2);
        Assert.True(WaitUntil(() => !IsWindowVisible(ov), TimeSpan.FromSeconds(3)), "overlay over the host's own UI");

        int before = run.Sim.Clicks();
        (x, y) = run.MoveInside();
        Assert.True(WaitUntil(() => IsWindowVisible(ov), TimeSpan.FromSeconds(3)), "overlay did not come back");
        ClickAt(x, y);
        Assert.True(WaitUntil(() => run.Sim.Clicks() == before + 1, TimeSpan.FromSeconds(3)),
                    "the click did not reach the RDP input window under the overlay");
    }

    [SkippableTheory]

    [Trait("Category", "InteractiveInput")]
    [InlineData("UIContainerClass,UIMainClass,IHWindowClass")]   // the classes in another order
    [InlineData("UIMainClass,IHWindowClass")]                    // no container
    [InlineData("TscShellAxHostClass,UIContainerClass,IHWindowClass")]   // wrong grandparent
    public void Rdp_overlay_stays_off_over_a_wrong_class_chain(string chain)
    {
        using var run = Run.Start("64", "alpha", zoom: 300, rdp: RdpChain);
        run.MoveInside();
        IntPtr ov = run.WaitOverlay();

        using var other = Sim.Start("64", "alpha", "RdpHostSimBad", SimX + SimW + 20, SimY, 200, 200, chain);
        run.MoveTo(SimX + SimW + 120, SimY + 120);
        Thread.Sleep(600);
        Assert.False(IsWindowVisible(ov), $"overlay over the chain {chain}");
    }

    [SkippableFact]

    [Trait("Category", "InteractiveInput")]
    public void Rdp_and_AnyDesk_are_switched_independently()
    {
        // Two programs: AnyDesk is CursorSim64.exe, the RDP host CursorSim32.exe.
        using var run = Run.Start("64", "alpha", zoom: 300);
        using var rdp = Sim.Start("32", "alpha", RdpHostClass, SimX + SimW + 20, SimY, 200, 200, RdpChain);
        (int x, int y) inRdp = (SimX + SimW + 120, SimY + 120);
        run.MoveTo(inRdp.x, inRdp.y);
        IntPtr ov = run.WaitOverlay();

        PostMessage(run.Host, WM_EYE_TOGGLE, rdp.Hwnd, IntPtr.Zero);
        Assert.True(WaitUntil(() => !IsWindowVisible(ov), TimeSpan.FromSeconds(3)), "switching RDP off kept the overlay");
        Assert.True(WaitUntil(() => DisabledKeys().Any(k => k.EndsWith("\\CursorSim32.exe", StringComparison.OrdinalIgnoreCase)),
                              TimeSpan.FromSeconds(3)), "the RDP host's program was not switched off");
        run.MoveInside();
        Assert.True(WaitUntil(() => IsWindowVisible(ov), TimeSpan.FromSeconds(3)), "switching RDP off also switched AnyDesk off");
        run.MoveTo(inRdp.x, inRdp.y);
        Thread.Sleep(400);
        Assert.False(IsWindowVisible(ov), "the overlay came back over the switched-off RDP window");

        PostMessage(run.Host, WM_EYE_TOGGLE, rdp.Hwnd, IntPtr.Zero);
        run.MoveTo(inRdp.x + 5, inRdp.y);
        Assert.True(WaitUntil(() => IsWindowVisible(ov), TimeSpan.FromSeconds(3)), "switching RDP on did not bring it back");
    }

    [SkippableFact]

    [Trait("Category", "InteractiveInput")]
    public void Rdp_animated_cursor_is_followed_frame_by_frame()
    {
        // 20 frames of 16x20 .. 16x39, each its own cursor handle, every 50 ms:
        // how the RDP client delivers an animated cursor.
        using var run = Run.Start("64", "anim", zoom: 100, rdp: RdpChain);
        var (x, y) = run.MoveInside();
        IntPtr ov = run.WaitOverlay();
        var heights = new HashSet<int>();
        var sw = Stopwatch.StartNew();
        while (sw.ElapsedMilliseconds < 2500 && heights.Count < 20)
        {
            // Only while the mouse is still where the test put it: someone using the
            // machine meanwhile would measure another window's cursor.
            GetCursorPos(out POINT now);
            if (now.x == x && now.y == y && IsWindowVisible(ov)) heights.Add(Height(ov));
            Thread.Sleep(5);
        }
        Assert.True(heights.Count >= 15, $"the overlay followed only {heights.Count} of 20 frames");
        Assert.All(heights, h => Assert.InRange(h, 20, 39));
    }

    [Fact]
    public void Rdp_windows_are_listed_and_counted()
    {
        using var run = Run.Start("64", "alpha", zoom: 300, needCursor: false, rdp: RdpChain);
        Assert.True(WaitUntil(() => Count(run.Host) >= 1, TimeSpan.FromSeconds(5)), "the RDP window was not counted");
        int with = StableCount(run.Host);

        using (Sim.Start("32", "alpha", RdpHostClass, SimX + SimW + 20, SimY, 200, 200, RdpChain))
            Assert.True(WaitUntil(() => Count(run.Host) == with + 1, TimeSpan.FromSeconds(5)),
                        "a second RDP window was not counted as a window of its own");
        Assert.True(WaitUntil(() => Count(run.Host) == with, TimeSpan.FromSeconds(5)),
                    $"a closed RDP window was not dropped from the count ({with} -> {Count(run.Host)})");

        // A window with the classes in the wrong order is not counted.
        using (Sim.Start("64", "alpha", "RdpHostSimBad", SimX + SimW + 20, SimY, 200, 200,
                         "UIContainerClass,UIMainClass,IHWindowClass"))
        {
            Thread.Sleep(1500);
            Assert.Equal(with, Count(run.Host));
        }
    }

    private const uint LVM_GETNEXTITEM = 0x100C;
    private const int LVNI_SELECTED = 0x0002;

    private static int Selected(IntPtr list) =>
        (int)SendMessage(list, LVM_GETNEXTITEM, (IntPtr)(-1), (IntPtr)LVNI_SELECTED);

    [SkippableFact]

    [Trait("Category", "InteractiveInput")]
    public void Auto_zoom_forgets_closed_windows()
    {
        // Auto keeps a zoom per session window; a closed one is dropped when the
        // cursor reaches a new window.
        using var run = Run.Start("64", "alpha", zoom: null);
        using var rdp = Sim.Start("32", "alpha", RdpHostClass, SimX + SimW + 20, SimY, 200, 200, RdpChain);
        run.MoveInside();
        IntPtr ov = run.WaitOverlay();
        run.Sim.Dispose();
        Assert.True(run.Sim.P.WaitForExit(5000), "the first session window did not close");
        run.MoveTo(SimX + SimW + 120, SimY + 120);
        Assert.True(WaitUntil(() => IsWindowVisible(ov), TimeSpan.FromSeconds(3)),
                    "the overlay did not follow onto the second session window");
    }

    // The number in the status window's title, "DeGhoster — N Ghosts".
    private static int Count(IntPtr host)
    {
        var sb = new StringBuilder(256);
        GetWindowText(host, sb, sb.Capacity);
        var m = System.Text.RegularExpressions.Regex.Match(sb.ToString(), @"\d+");
        return m.Success ? int.Parse(m.Value) : -1;
    }

    private static int StableCount(IntPtr host)
    {
        int last = Count(host);
        var end = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        for (var since = DateTime.UtcNow; DateTime.UtcNow < end; Thread.Sleep(200))
        {
            int now = Count(host);
            if (now != last) { last = now; since = DateTime.UtcNow; }
            else if (DateTime.UtcNow - since > TimeSpan.FromSeconds(1.5)) break;
        }
        return last;
    }

    private static string[] DisabledKeys()
    {
        using var key = Registry.CurrentUser.OpenSubKey(TestSettings.Root + @"\Disabled");
        return key?.GetValueNames() ?? Array.Empty<string>();
    }

    [SkippableFact]

    [Trait("Category", "InteractiveInput")]
    public void Session_lock_hides_the_overlay_until_unlock()
    {
        using var run = Run.Start("64", "alpha", zoom: 300);
        run.MoveInside();
        IntPtr ov = run.WaitOverlay();

        // What WTSRegisterSessionNotification delivers around the lock screen.
        PostMessage(run.Host, WM_WTSSESSION_CHANGE, (IntPtr)WTS_SESSION_LOCK, IntPtr.Zero);
        Assert.True(WaitUntil(() => !IsWindowVisible(ov), TimeSpan.FromSeconds(3)), "lock kept the overlay");
        run.MoveInside();
        Thread.Sleep(400);
        Assert.False(IsWindowVisible(ov), "the overlay came back while the session is locked");

        PostMessage(run.Host, WM_WTSSESSION_CHANGE, (IntPtr)WTS_SESSION_UNLOCK, IntPtr.Zero);
        run.MoveInside();
        Assert.True(WaitUntil(() => IsWindowVisible(ov), TimeSpan.FromSeconds(3)), "unlock did not bring it back");

        PostMessage(run.Host, WM_COMMAND, (IntPtr)9999, IntPtr.Zero);   // an unknown command is ignored
        Thread.Sleep(200);
        Assert.False(run.Dg.HasExited, "DeGhoster exited on an unknown command");
    }

    [SkippableFact]

    [Trait("Category", "InteractiveInput")]
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

    [Trait("Category", "InteractiveInput")]
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

        // `zoom` is a fixed zoom (Auto off); null leaves Auto on, the default.
        // `rdp` makes the simulator a program hosting the RDP control (see Sim).
        public static Run Start(string bits, string kind, int? zoom, bool needCursor = true,
                                Dictionary<string, string>? env = null, string? rdp = null)
        {
            SetThreadDpiAwarenessContext(new IntPtr(-4));   // physical pixels, like DeGhoster
            var r = new Run { Build = FindBuildDir() };
            TestSettings.EnsureGlobalEnabled();
            using (var key = Registry.CurrentUser.CreateSubKey(TestSettings.Root, writable: true))
            {
                key.SetValue("CursorOverlayAuto", zoom.HasValue ? 0 : 1, RegistryValueKind.DWord);
                if (zoom.HasValue) key.SetValue("CursorOverlayZoom", zoom.Value, RegistryValueKind.DWord);
            }
            try
            {
                r.Sim = rdp == null
                    ? Sim.Start(bits, kind, "ad_win#1", SimX, SimY, SimW, SimH)
                    : Sim.Start(bits, kind, RdpHostClass, SimX, SimY, SimW, SimH, rdp);
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
            Assert.True(ok, "the overlay did not appear over the session window");
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
        public string Title = "";
        public IntPtr Hwnd;

        // `rdp` is a chain of child classes (outermost first, "default" for the
        // real RDP control's) for a program hosting the RDP control; then `cls`
        // is the host's top-level class and Hwnd that top-level window.
        public static Sim Start(string bits, string kind, string cls, int x, int y, int w, int h, string? rdp = null)
        {
            string build = FindBuildDir();
            string title = "ADSIM-" + Guid.NewGuid().ToString("N");
            string rdpArg = rdp == null ? "" : $" --rdp \"{rdp}\"";
            var psi = new ProcessStartInfo(Path.Combine(build, $"CursorSim{bits}.exe"),
                $"--kind {kind} --class \"{cls}\" --title {title} --x {x} --y {y} --w {w} --h {h} --band {Band} --timeout 120{rdpArg}")
                { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = build };
            var s = new Sim { P = Process.Start(psi)!, Title = title };
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
