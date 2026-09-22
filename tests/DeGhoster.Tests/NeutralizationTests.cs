// Copyright (c) Emmo Emminghaus mo2000 at mo2000 dot de
// SPDX-License-Identifier: AGPL-3.0-or-later

using System.Diagnostics;
using System.Runtime.InteropServices;
using Xunit;

namespace DeGhoster.Tests;

[Trait("Category", "Integration")]
public class NeutralizationTests
{
    private const string GhostClass = "Chrome_WidgetWin_1";
    private const int DWMWA_CLOAKED = 14;

    [Theory]
    [InlineData("64")]  // x64 ghost -> Helper64 injects Hook64
    [InlineData("32")]  // x86 ghost -> Helper32 injects Hook32
    public void DeGhoster_neutralizes_ghost_window(string bits)
    {
        string build = FindBuildDir();
        string deghoster = Path.Combine(build, "DeGhoster.exe");
        string ghostSim = Path.Combine(build, $"GhostSim{bits}.exe");
        Assert.True(File.Exists(deghoster), $"missing {deghoster} - build DeGhoster first");
        Assert.True(File.Exists(ghostSim), $"missing {ghostSim} - build the {bits}-bit config first");

        EnsureGlobalEnabled();

        string title = "DGHTEST-" + Guid.NewGuid().ToString("N");
        Process? sim = null, dg = null;
        try
        {
            sim = Start(ghostSim, $"--title \"{title}\" --timeout 90", build);

            IntPtr h = WaitForWindow(title, TimeSpan.FromSeconds(8));
            Assert.True(h != IntPtr.Zero, "the ghost simulator window was not found");
            Assert.Equal(0, Cloaked(h));

            dg = Start(deghoster, "", build);

            bool cloaked = WaitUntil(() => Cloaked(h) != 0, TimeSpan.FromSeconds(20));
            Assert.True(cloaked, $"DeGhoster did not neutralize the {bits}-bit ghost window (DWMWA_CLOAKED stayed 0)");
        }
        finally
        {
            // Kill ONLY the host (not the process tree): the helper then notices
            // the host handle signal and runs its own cleanup (unhook +
            // FreeLibrary) before exiting. Killing the tree would take the helper
            // down with it and skip that path.
            try { if (dg is { HasExited: false }) dg.Kill(entireProcessTree: false); } catch { }
            var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
            while (DateTime.UtcNow < deadline && HelperProcesses().Length > 0)
                Thread.Sleep(100);
            foreach (var p in HelperProcesses()) Kill(p);
            Kill(sim);
        }
    }

    // Either bitness can be in play: the host injects through a helper matching
    // the target process, so both names have to be reaped (see ADR-0011).
    private static Process[] HelperProcesses() =>
        Process.GetProcessesByName("DeGhoster.Helper32")
               .Concat(Process.GetProcessesByName("DeGhoster.Helper64"))
               .ToArray();

    private static string FindBuildDir()
    {
        for (var d = new DirectoryInfo(AppContext.BaseDirectory); d != null; d = d.Parent)
        {
            string candidate = Path.Combine(d.FullName, "build");
            if (File.Exists(Path.Combine(candidate, "DeGhoster.exe")))
                return candidate;
        }
        throw new DirectoryNotFoundException(
            "Could not locate the build\\ output (with DeGhoster.exe) above " + AppContext.BaseDirectory);
    }

    // Writes into the per-run throwaway root, never the user's real settings.
    private static void EnsureGlobalEnabled() => TestSettings.EnsureGlobalEnabled();

    private static Process? Start(string exe, string args, string? workDir, bool hidden = false)
    {
        var psi = new ProcessStartInfo(exe, args)
        {
            UseShellExecute = false,
            CreateNoWindow = hidden,
            WorkingDirectory = workDir ?? Path.GetDirectoryName(exe) ?? Environment.CurrentDirectory,
        };
        TestSettings.Apply(psi);   // point it at the throwaway registry root
        return Process.Start(psi);
    }

    private static IntPtr WaitForWindow(string title, TimeSpan timeout)
    {
        var end = DateTime.UtcNow + timeout;
        do
        {
            IntPtr h = FindWindowEx(IntPtr.Zero, IntPtr.Zero, GhostClass, title);
            if (h != IntPtr.Zero) return h;
            Thread.Sleep(100);
        } while (DateTime.UtcNow < end);
        return IntPtr.Zero;
    }

    private static bool WaitUntil(Func<bool> cond, TimeSpan timeout)
    {
        var end = DateTime.UtcNow + timeout;
        do
        {
            if (cond()) return true;
            Thread.Sleep(200);
        } while (DateTime.UtcNow < end);
        return cond();
    }

    private static int Cloaked(IntPtr h)
        => DwmGetWindowAttribute(h, DWMWA_CLOAKED, out int v, sizeof(int)) == 0 ? v : -1;

    private static void Kill(Process? p)
    {
        try { if (p is { HasExited: false }) p.Kill(entireProcessTree: true); } catch { }
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr child, string cls, string title);

    [DllImport("dwmapi.dll")]
    private static extern int DwmGetWindowAttribute(IntPtr hwnd, int attr, out int value, int size);
}
