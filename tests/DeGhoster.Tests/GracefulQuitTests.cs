// Copyright (c) Emmo Emminghaus mo2000 at mo2000 dot de
// SPDX-License-Identifier: AGPL-3.0-or-later

using System.Diagnostics;
using System.Runtime.InteropServices;
using Xunit;

namespace DeGhoster.Tests;

[Trait("Category", "Integration")]
public class GracefulQuitTests
{
    private const string GhostClass = "Chrome_WidgetWin_1";
    private const string HostClass = "DeGhosterMainWindow";
    private const int DWMWA_CLOAKED = 14;
    private const uint WM_COMMAND = 0x0111;
    private const int IDC_EXIT = 1004;   // enum { IDC_LIST=1001, POWER, INFO, EXIT }

    [Theory]
    [InlineData("64")]
    [InlineData("32")]
    public void Graceful_exit_tears_down_cleanly(string bits)
    {
        string build = FindBuildDir();
        string deghoster = Path.Combine(build, "DeGhoster.exe");
        string ghostSim = Path.Combine(build, $"GhostSim{bits}.exe");
        Assert.True(File.Exists(deghoster) && File.Exists(ghostSim), "build the binaries first");

        EnsureGlobalEnabled();
        string title = "DGHGQ-" + Guid.NewGuid().ToString("N");
        Process? sim = null, dg = null;
        try
        {
            sim = Start(ghostSim, $"--title \"{title}\" --timeout 90", build);
            IntPtr ghost = WaitFor(() => FindWindowEx(IntPtr.Zero, IntPtr.Zero, GhostClass, title), TimeSpan.FromSeconds(8));
            Assert.True(ghost != IntPtr.Zero, "ghost window not found");

            dg = Start(deghoster, "", build);
            Assert.True(WaitUntil(() => Cloaked(ghost) != 0, TimeSpan.FromSeconds(20)), "window was not cloaked");

            IntPtr host = WaitFor(() => FindWindowEx(IntPtr.Zero, IntPtr.Zero, HostClass, null), TimeSpan.FromSeconds(5));
            Assert.True(host != IntPtr.Zero, "DeGhoster main window not found");
            PostMessage(host, WM_COMMAND, (IntPtr)IDC_EXIT, IntPtr.Zero);

            Assert.True(dg.WaitForExit(10000), "DeGhoster did not exit after the Exit command");
            Assert.Equal(0, dg.ExitCode);
            // Graceful exit runs GhostEngine/HookInjector teardown (removeAll ->
            // DgRemoveHook, helper cleanup). We don't assert the window un-cloaks
            // here: the DLL only unloads (and auto-uncloaks) once the target
            // pumps messages, which an idle GhostSim doesn't; the in-process
            // detach/un-cloak path is covered deterministically by UnitTests.exe.
        }
        finally
        {
            Kill(dg);
            foreach (var p in Process.GetProcessesByName("DeGhoster.Helper32")) Kill(p);
            Kill(sim);
        }
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
        var psi = new ProcessStartInfo("reg.exe")
        { UseShellExecute = false, CreateNoWindow = true };
        foreach (var a in new[] { "add", @"HKCU\Software\DeGhoster", "/v", "GlobalEnabled", "/t", "REG_DWORD", "/d", "1", "/f" })
            psi.ArgumentList.Add(a);
        Process.Start(psi)?.WaitForExit(5000);
    }

    private static Process Start(string exe, string args, string workDir)
        => Process.Start(new ProcessStartInfo(exe, args)
        { UseShellExecute = false, WorkingDirectory = workDir })!;

    private static IntPtr WaitFor(Func<IntPtr> get, TimeSpan timeout)
    {
        var end = DateTime.UtcNow + timeout;
        do { var h = get(); if (h != IntPtr.Zero) return h; Thread.Sleep(100); }
        while (DateTime.UtcNow < end);
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

    private static void Kill(Process? p)
    { try { if (p is { HasExited: false }) p.Kill(entireProcessTree: true); } catch { } }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr child, string cls, string? title);
    [DllImport("user32.dll")]
    private static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("dwmapi.dll")]
    private static extern int DwmGetWindowAttribute(IntPtr hwnd, int attr, out int value, int size);
}
