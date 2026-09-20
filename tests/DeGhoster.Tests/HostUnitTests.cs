// Copyright (c) Emmo Emminghaus mo2000 at mo2000 dot de
// SPDX-License-Identifier: AGPL-3.0-or-later

using System.Diagnostics;
using System.Text;
using Xunit;

namespace DeGhoster.Tests;

// Covers the non-GUI host features the end-to-end test doesn't reach:
//  * Settings persistence - via the native UnitTests.exe (isolated registry root)
//  * Autostart register/unregister - via DeGhoster.exe's CLI flags
public class HostUnitTests
{
    private const string RunKey = @"HKCU\Software\Microsoft\Windows\CurrentVersion\Run";
    private const string RunValue = "DeGhoster";

    [Fact]
    public void Settings_persistence_unit_tests_pass()
    {
        string exe = Path.Combine(FindBuildDir(), "UnitTests.exe");
        Assert.True(File.Exists(exe), $"missing {exe} - build the x64 config first");

        var (code, output) = Run(exe, "");
        Assert.True(code == 0, $"native Settings unit tests failed (exit {code}):\n{output}");
    }

    [Fact]
    public void Autostart_registers_and_unregisters_per_user()
    {
        string deghoster = Path.Combine(FindBuildDir(), "DeGhoster.exe");
        Assert.True(File.Exists(deghoster), $"missing {deghoster}");

        string? saved = ReadRunValue();   // preserve any real user entry
        try
        {
            // register -> HKCU Run value = the quoted exe path
            Assert.Equal(0, Run(deghoster, "--register-autostart").code);
            string? v = ReadRunValue();
            Assert.False(string.IsNullOrEmpty(v), "autostart value was not written");
            Assert.Equal($"\"{deghoster}\"", v, ignoreCase: true);

            // unregister -> value gone
            Assert.Equal(0, Run(deghoster, "--unregister-autostart").code);
            Assert.Null(ReadRunValue());
        }
        finally
        {
            // Restore the machine to its prior state.
            if (saved is null) DeleteRunValue();
            else WriteRunValue(saved);
        }
    }

    // --- helpers ------------------------------------------------------------

    private static string FindBuildDir()
    {
        for (var d = new DirectoryInfo(AppContext.BaseDirectory); d != null; d = d.Parent)
        {
            string candidate = Path.Combine(d.FullName, "build");
            if (File.Exists(Path.Combine(candidate, "DeGhoster.exe")))
                return candidate;
        }
        throw new DirectoryNotFoundException("Could not locate build\\ above " + AppContext.BaseDirectory);
    }

    private static (int code, string output) Run(string exe, string args)
    {
        var psi = new ProcessStartInfo(exe, args)
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            WorkingDirectory = Path.GetDirectoryName(exe)!,
        };
        using var p = Process.Start(psi)!;
        string o = p.StandardOutput.ReadToEnd() + p.StandardError.ReadToEnd();
        p.WaitForExit(15000);
        return (p.HasExited ? p.ExitCode : -1, o);
    }

    // Registry access via reg.exe (ArgumentList avoids quoting pitfalls).
    private static (int code, string output) Reg(params string[] args)
    {
        var psi = new ProcessStartInfo("reg.exe")
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };
        foreach (var a in args) psi.ArgumentList.Add(a);
        using var p = Process.Start(psi)!;
        string o = p.StandardOutput.ReadToEnd();
        p.WaitForExit(10000);
        return (p.ExitCode, o);
    }

    private static string? ReadRunValue()
    {
        var (code, output) = Reg("query", RunKey, "/v", RunValue);
        if (code != 0) return null;
        foreach (var line in output.Split('\n'))
        {
            int i = line.IndexOf("REG_SZ", StringComparison.Ordinal);
            if (i >= 0) return line[(i + "REG_SZ".Length)..].Trim();
        }
        return null;
    }

    private static void WriteRunValue(string data)
        => Reg("add", RunKey, "/v", RunValue, "/t", "REG_SZ", "/d", data, "/f");

    private static void DeleteRunValue()
        => Reg("delete", RunKey, "/v", RunValue, "/f");
}
