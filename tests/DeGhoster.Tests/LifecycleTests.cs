// Copyright (c) Emmo Emminghaus mo2000 at mo2000 dot de
// SPDX-License-Identifier: AGPL-3.0-or-later

using System.Diagnostics;
using System.Runtime.InteropServices;
using Xunit;

namespace DeGhoster.Tests;

// Start-up and shut-down paths: the quit switch the installer uses, the
// single-instance handoff, the end-session teardown, and the broadcast messages
// the app has to survive. These are message-driven rather than input-driven, so
// unlike the UI tests they do not need the desktop to stay untouched.
[Trait("Category", "Integration")]
public class LifecycleTests
{
    private const string GhostClass = "Chrome_WidgetWin_1";
    private const string HostClass = "DeGhosterMainWindow";
    private const int DWMWA_CLOAKED = 14;
    private const uint WM_ENDSESSION = 0x0016, WM_QUERYENDSESSION = 0x0011;
    private const uint WM_DPICHANGED = 0x02E0, WM_SETTINGCHANGE = 0x001A, WM_COMMAND = 0x0111;
    private const string InfoClass = "DeGhosterInfoWindow";
    private const int IDC_INFO = 1003, IDOK = 1;

    [StructLayout(LayoutKind.Sequential)] private struct RECT { public int left, top, right, bottom; }

    [Fact]
    public void Quit_switch_closes_the_host_and_its_helpers()
    {
        string build = FindBuildDir();
        EnsureGlobalEnabled();
        string title = "DGHQUIT-" + Guid.NewGuid().ToString("N");
        Process? sim = null, dg = null;
        try
        {
            sim = Start(Path.Combine(build, "GhostSim64.exe"), $"--title \"{title}\" --timeout 120", build);
            IntPtr ghost = WaitFor(() => FindWindow(GhostClass, title), TimeSpan.FromSeconds(8));
            Assert.True(ghost != IntPtr.Zero, "the ghost simulator window was not found");

            dg = Start(Path.Combine(build, "DeGhoster.exe"), "", build);
            Assert.True(WaitUntil(() => Cloaked(ghost) != 0 && Helpers().Length > 0, TimeSpan.FromSeconds(25)),
                        "DeGhoster did not inject through a helper");

            // What the MSI runs before it works out which files are in use.
            var quit = Start(Path.Combine(build, "DeGhoster.exe"), "--quit", build);
            Assert.True(quit.WaitForExit(40000), "the quit switch did not return");
            Assert.Equal(0, quit.ExitCode);

            Assert.True(dg.HasExited, "the running instance is still alive after the quit switch");
            Assert.Empty(Helpers());
            // stop() reveals what it cloaked before it drops the hook.
            Assert.True(WaitUntil(() => Cloaked(ghost) == 0, TimeSpan.FromSeconds(10)),
                        "the ghost window was left cloaked");
        }
        finally { Cleanup(dg, sim); }
    }

    [Fact]
    public void Quit_switch_succeeds_when_nothing_is_running()
    {
        string build = FindBuildDir();
        var quit = Start(Path.Combine(build, "DeGhoster.exe"), "--quit", build);
        Assert.True(quit.WaitForExit(20000), "the quit switch hung with no instance running");
        Assert.Equal(0, quit.ExitCode);
    }

    [Fact]
    public void Second_launch_hands_off_to_the_running_instance()
    {
        string build = FindBuildDir();
        EnsureGlobalEnabled();
        Process? dg = null, second = null;
        try
        {
            // Start hidden, so "the window is visible" afterwards actually means the
            // handoff did something. Started normally the window is up regardless and
            // the assertion would hold without a single message being processed.
            dg = Start(Path.Combine(build, "DeGhoster.exe"), "--taskbar", build);
            IntPtr host = WaitFor(() => FindWindow(HostClass, null), TimeSpan.FromSeconds(15));
            Assert.True(host != IntPtr.Zero, "the host window never appeared");
            Assert.False(IsWindowVisible(host), "--taskbar should leave the window hidden");

            second = Start(Path.Combine(build, "DeGhoster.exe"), "", build);
            Assert.True(second.WaitForExit(20000), "the second launch did not exit");
            Assert.Equal(0, second.ExitCode);
            Assert.False(dg.HasExited, "the second launch took the first instance down");
            // The handoff posts a registered message; the first instance surfaces.
            Assert.True(WaitUntil(() => IsWindowVisible(host), TimeSpan.FromSeconds(10)),
                        "the running instance did not surface its window");
        }
        finally { Cleanup(dg, null); try { if (second is { HasExited: false }) second.Kill(); } catch { } }
    }

    [Fact]
    public void Broadcast_messages_are_survived()
    {
        string build = FindBuildDir();
        EnsureGlobalEnabled();
        Process? dg = null;
        try
        {
            dg = Start(Path.Combine(build, "DeGhoster.exe"), "", build);
            IntPtr host = WaitFor(() => FindWindow(HostClass, null), TimeSpan.FromSeconds(15));
            Assert.True(host != IntPtr.Zero, "the host window never appeared");

            // A DPI change: the app rescales its font and re-lays out.
            GetWindowRect(host, out RECT rc);
            IntPtr box = Marshal.AllocHGlobal(Marshal.SizeOf<RECT>());
            try
            {
                Marshal.StructureToPtr(rc, box, false);
                SendMessage(host, WM_DPICHANGED, (IntPtr)((144 << 16) | 144), box);
            }
            finally { Marshal.FreeHGlobal(box); }

            // A colour-scheme change: only this one triggers a re-theme.
            SendMessageTimeout(host, WM_SETTINGCHANGE, IntPtr.Zero, "ImmersiveColorSet", 0, 2000, out _);
            SendMessageTimeout(host, WM_SETTINGCHANGE, IntPtr.Zero, "Environment", 0, 2000, out _);

            // Explorer restarting: the tray icon has to be re-added.
            uint taskbarCreated = RegisterWindowMessage("TaskbarCreated");
            Assert.True(taskbarCreated != 0, "TaskbarCreated could not be registered");
            SendMessageTimeout(host, taskbarCreated, IntPtr.Zero, IntPtr.Zero, 0, 2000, out _);

            Assert.False(dg.HasExited, "the app died on a broadcast message");
            Assert.True(IsWindow(host), "the host window was destroyed by a broadcast message");
        }
        finally { Cleanup(dg, null); }
    }

    [Fact]
    public void End_session_tears_down_and_exits()
    {
        string build = FindBuildDir();
        EnsureGlobalEnabled();
        string title = "DGHEND-" + Guid.NewGuid().ToString("N");
        Process? sim = null, dg = null;
        try
        {
            sim = Start(Path.Combine(build, "GhostSim64.exe"), $"--title \"{title}\" --timeout 120", build);
            IntPtr ghost = WaitFor(() => FindWindow(GhostClass, title), TimeSpan.FromSeconds(8));
            Assert.True(ghost != IntPtr.Zero, "the ghost simulator window was not found");

            dg = Start(Path.Combine(build, "DeGhoster.exe"), "", build);
            IntPtr host = WaitFor(() => FindWindow(HostClass, null), TimeSpan.FromSeconds(15));
            Assert.True(host != IntPtr.Zero, "the host window never appeared");
            Assert.True(WaitUntil(() => Cloaked(ghost) != 0, TimeSpan.FromSeconds(25)), "no ghost was neutralized");

            // Windows asks first, then tells. The app must agree and then actually
            // exit - stopping the engine without exiting used to leave it running.
            SendMessageTimeout(host, WM_QUERYENDSESSION, IntPtr.Zero, IntPtr.Zero, 0, 5000, out IntPtr agree);
            Assert.NotEqual(IntPtr.Zero, agree);
            PostMessage(host, WM_ENDSESSION, (IntPtr)1, IntPtr.Zero);

            Assert.True(dg.WaitForExit(30000), "the app did not exit after WM_ENDSESSION");
            Assert.True(WaitUntil(() => Helpers().Length == 0, TimeSpan.FromSeconds(10)), "a helper was left behind");
        }
        finally { Cleanup(dg, sim); }
    }

    [Fact]
    public void Missing_helpers_are_survived_and_nothing_is_cloaked()
    {
        string build = FindBuildDir();
        EnsureGlobalEnabled();
        // Move the helpers aside rather than running a copy of the app from another
        // directory: a second copy under build\ would be a second module, and the
        // coverage run would then count every source line of it twice.
        string h32 = Path.Combine(build, "DeGhoster.Helper32.exe");
        string h64 = Path.Combine(build, "DeGhoster.Helper64.exe");
        string title = "DGHNOHLP-" + Guid.NewGuid().ToString("N");
        Process? sim = null, dg = null;
        try
        {
            foreach (var p in Helpers()) { try { p.Kill(); p.WaitForExit(5000); } catch { } }
            Hide(h32); Hide(h64);

            sim = Start(Path.Combine(build, "GhostSim64.exe"), $"--title \"{title}\" --timeout 60", build);
            IntPtr ghost = WaitFor(() => FindWindow(GhostClass, title), TimeSpan.FromSeconds(8));
            Assert.True(ghost != IntPtr.Zero, "the ghost simulator window was not found");

            dg = Start(Path.Combine(build, "DeGhoster.exe"), "", build);
            IntPtr host = WaitFor(() => FindWindow(HostClass, null), TimeSpan.FromSeconds(15));
            Assert.True(host != IntPtr.Zero, "the host window never appeared");

            // It must keep running and report the problem rather than fall over - but
            // it cannot fix anything, so the ghost stays exactly as it was.
            Thread.Sleep(4000);
            Assert.False(dg.HasExited, "the app died when its helpers were missing");
            Assert.Equal(0, Cloaked(ghost));
            Assert.Empty(Helpers());
        }
        finally
        {
            Cleanup(dg, sim);
            Unhide(h32); Unhide(h64);
        }
    }

    private const string HiddenSuffix = ".coveragehidden";

    private static void Hide(string path)
    {
        if (File.Exists(path)) File.Move(path, path + HiddenSuffix, overwrite: true);
    }

    private static void Unhide(string path)
    {
        string hidden = path + HiddenSuffix;
        if (File.Exists(hidden)) File.Move(hidden, path, overwrite: true);
    }

    [Fact]
    public void Info_window_handles_a_dpi_change()
    {
        string build = FindBuildDir();
        EnsureGlobalEnabled();
        Process? dg = null;
        try
        {
            dg = Start(Path.Combine(build, "DeGhoster.exe"), "", build);
            IntPtr host = WaitFor(() => FindWindow(HostClass, null), TimeSpan.FromSeconds(15));
            Assert.True(host != IntPtr.Zero, "the host window never appeared");

            PostMessage(host, WM_COMMAND, (IntPtr)IDC_INFO, IntPtr.Zero);
            IntPtr info = WaitFor(() => FindWindow(InfoClass, null), TimeSpan.FromSeconds(10));
            Assert.True(info != IntPtr.Zero, "the About window never appeared");

            GetWindowRect(info, out RECT rc);
            IntPtr box = Marshal.AllocHGlobal(Marshal.SizeOf<RECT>());
            try
            {
                Marshal.StructureToPtr(rc, box, false);
                SendMessage(info, WM_DPICHANGED, (IntPtr)((144 << 16) | 144), box);
            }
            finally { Marshal.FreeHGlobal(box); }
            Assert.True(IsWindow(info), "the About window did not survive the DPI change");

            // Close it the way the OK button does.
            PostMessage(info, WM_COMMAND, (IntPtr)IDOK, IntPtr.Zero);
            Assert.True(WaitUntil(() => FindWindow(InfoClass, null) == IntPtr.Zero, TimeSpan.FromSeconds(10)),
                        "the About window did not close");
            Assert.False(dg.HasExited, "closing the About window took the app down");
        }
        finally { Cleanup(dg, null); }
    }

    private static Process[] Helpers() =>
        Process.GetProcessesByName("DeGhoster.Helper32")
               .Concat(Process.GetProcessesByName("DeGhoster.Helper64"))
               .ToArray();

    private static void Cleanup(Process? dg, Process? sim)
    {
        try { if (dg is { HasExited: false }) dg.Kill(entireProcessTree: false); } catch { }
        foreach (var p in Helpers()) { try { p.Kill(); } catch { } }
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

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr FindWindow(string cls, string? title);
    [DllImport("user32.dll")]
    private static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")]
    private static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint msg, IntPtr w, string l, uint flags, uint ms, out IntPtr res);
    [DllImport("user32.dll")]
    private static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint msg, IntPtr w, IntPtr l, uint flags, uint ms, out IntPtr res);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern uint RegisterWindowMessage(string name);
    [DllImport("user32.dll")]
    private static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")]
    private static extern bool GetWindowRect(IntPtr hwnd, out RECT rc);
    [DllImport("dwmapi.dll")]
    private static extern int DwmGetWindowAttribute(IntPtr hwnd, int attr, out int value, int size);
}
