// Copyright (c) Emmo Emminghaus mo2000 at mo2000 dot de
// SPDX-License-Identifier: AGPL-3.0-or-later

using System.Diagnostics;
using Microsoft.Win32;

namespace DeGhoster.Tests;

/// <summary>
/// A throwaway registry root for the integration tests.
///
/// The tests used to drive DeGhoster against the real HKCU\Software\DeGhoster.
/// Toggling an eye writes an opt-out for the simulator's program, which would
/// switch that program off in the user's own settings; before opt-outs were per
/// program they were keyed by window title, and every run left one more entry
/// behind.
///
/// DeGhoster reads its root from DEGHOSTER_SETTINGS_ROOT (the same hook the
/// native unit tests use), so pointing every process the tests start at a
/// per-run key keeps their settings untouched. The key is removed when the test
/// host exits, and any leftovers from a run that was killed are swept up at
/// start, so they cannot pile up either.
/// </summary>
internal static class TestSettings
{
    public const string EnvVar = "DEGHOSTER_SETTINGS_ROOT";
    private const string Prefix = @"Software\DeGhoster_ITest_";

    public static readonly string Root = Prefix + Guid.NewGuid().ToString("N");

    static TestSettings()
    {
        SweepLeftovers();
        AppDomain.CurrentDomain.ProcessExit += (_, _) => DeleteRoot();
    }

    /// <summary>Environment for a process under test, plus any extra entries.</summary>
    public static Dictionary<string, string> Env(Dictionary<string, string>? extra = null)
    {
        var env = new Dictionary<string, string> { [EnvVar] = Root };
        if (extra != null) foreach (var kv in extra) env[kv.Key] = kv.Value;
        return env;
    }

    public static void Apply(ProcessStartInfo psi, Dictionary<string, string>? extra = null)
    {
        foreach (var kv in Env(extra)) psi.Environment[kv.Key] = kv.Value;
        // Opt-outs are per program, and every test's simulator is the same program:
        // one a test left switched off would switch it off for every later test in
        // this run. So each DeGhoster starts without any.
        if (string.Equals(Path.GetFileName(psi.FileName), "DeGhoster.exe", StringComparison.OrdinalIgnoreCase))
            ResetOptOuts();
    }

    /// <summary>Removes every per-program opt-out from the run's root.</summary>
    public static void ResetOptOuts()
    {
        try { Registry.CurrentUser.DeleteSubKeyTree(Root + @"\Disabled", throwOnMissingSubKey: false); } catch { }
    }

    /// <summary>
    /// DeGhoster's master switch defaults to on when absent, but force it on so a
    /// leftover value cannot disable neutralization for the test.
    /// </summary>
    public static void EnsureGlobalEnabled()
    {
        using var key = Registry.CurrentUser.CreateSubKey(Root, writable: true);
        key?.SetValue("GlobalEnabled", 1, RegistryValueKind.DWord);
    }

    public static void DeleteRoot() => Delete(Root);

    private static void SweepLeftovers()
    {
        try
        {
            using var software = Registry.CurrentUser.OpenSubKey("Software", writable: true);
            if (software == null) return;
            foreach (var name in software.GetSubKeyNames())
                if (name.StartsWith("DeGhoster_ITest_", StringComparison.OrdinalIgnoreCase))
                    try { software.DeleteSubKeyTree(name, throwOnMissingSubKey: false); } catch { }
        }
        catch { }
    }

    private static void Delete(string root)
    {
        try { Registry.CurrentUser.DeleteSubKeyTree(root, throwOnMissingSubKey: false); } catch { }
    }
}
