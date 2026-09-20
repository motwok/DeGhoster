// Copyright (c) Emmo Emminghaus mo2000 at mo2000 dot de
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <windows.h>
#include <dwmapi.h>
#include <string>
#include <cstdio>

#include "Settings.h"
#include "Theme.h"
#include "Hook.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")

static int g_failures = 0;

static void Check(bool cond, const char* what)
{
    std::printf(cond ? "  [ok]   %s\n" : "  [FAIL] %s\n", what);
    if (!cond) ++g_failures;
}

static void SettingsTests()
{
    std::wstring root = L"Software\\DeGhoster_Test_" +
                        std::to_wstring(GetCurrentProcessId()) + L"_" +
                        std::to_wstring(GetTickCount());
    SetEnvironmentVariableW(L"DEGHOSTER_SETTINGS_ROOT", root.c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str());

    std::printf("Settings tests (root=HKCU\\%ls)\n", root.c_str());

    { Settings s; s.load(); Check(s.globalEnabled(), "GlobalEnabled defaults to true"); }
    {
        Settings s; s.load(); s.setGlobalEnabled(false);
        Check(!s.globalEnabled(), "setGlobalEnabled(false) takes effect");
        Settings s2; s2.load();
        Check(!s2.globalEnabled(), "GlobalEnabled=false persists across reload");
        s2.setGlobalEnabled(true);
        Settings s3; s3.load();
        Check(s3.globalEnabled(), "GlobalEnabled=true persists across reload");
    }
    {
        const std::wstring key = L"C:\\Apps\\Ghosty.exe|Some Window Title";
        Settings s; s.load();
        Check(s.isManaged(key), "unknown window is managed by default");
        s.setManaged(key, false);
        Check(!s.isManaged(key), "setManaged(false) marks the window unmanaged");
        Settings s2; s2.load();
        Check(!s2.isManaged(key), "unmanaged state persists across reload");
        s2.setManaged(key, true);
        Check(s2.isManaged(key), "setManaged(true) re-manages the window");
        Settings s3; s3.load();
        Check(s3.isManaged(key), "re-managed state persists across reload");
    }

    RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str());
}

static int Cloaked(HWND h)
{
    int v = 0;
    return DwmGetWindowAttribute(h, DWMWA_CLOAKED, &v, sizeof(v)) == S_OK ? v : -1;
}

// Pump this thread's queue (so the WH_GETMESSAGE hook fires) until the window's
// cloaked state reaches `want`, or the timeout elapses.
static bool PumpUntilCloaked(HWND h, int want, DWORD ms)
{
    DWORD start = GetTickCount();
    MSG m;
    for (;;) {
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        if (Cloaked(h) == want) return true;
        if (GetTickCount() - start > ms) return Cloaked(h) == want;
        Sleep(10);
    }
}

static std::wstring ExeDir()
{
    wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p; size_t i = s.find_last_of(L'\\');
    return i == std::wstring::npos ? L"" : s.substr(0, i + 1);
}

static void HookDllTests()
{
    std::printf("Hook DLL tests\n");

    const wchar_t* cls = L"Chrome_WidgetWin_1";
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);

    HWND h = CreateWindowExW(WS_EX_LAYERED, cls, L"DeGhoster-HookTest", WS_POPUP,
                             120, 120, 300, 200, nullptr, nullptr, wc.hInstance, nullptr);
    if (!h) { Check(false, "create ghost window"); return; }
    SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
    ShowWindow(h, SW_SHOWNA);

    typedef HHOOK (__stdcall* InstallFn)(DWORD, HWND);
    typedef BOOL  (__stdcall* RemoveFn)(HHOOK);

    HMODULE dll = LoadLibraryW((ExeDir() + L"DeGhoster.Hook64.dll").c_str());
    Check(dll != nullptr, "load DeGhoster.Hook64.dll");
    if (dll) {
        auto install = (InstallFn)GetProcAddress(dll, "DgInstallHook");
        auto remove  = (RemoveFn) GetProcAddress(dll, "DgRemoveHook");
        Check(install && remove, "resolve DgInstallHook / DgRemoveHook");
        if (install && remove) {
            HHOOK hk = install(GetCurrentThreadId(), nullptr);
            Check(hk != nullptr, "DgInstallHook returns a hook");

            PostMessageW(h, DGH_CLOAK, 0, 0);
            Check(PumpUntilCloaked(h, 1, 2000), "DGH_CLOAK cloaks the window");

            PostMessageW(h, DGH_UNCLOAK, 0, 0);
            Check(PumpUntilCloaked(h, 0, 2000), "DGH_UNCLOAK un-cloaks the window");

            // Re-cloak, then remove the hook: the window must STAY cloaked
            // (removing the hook does not un-cloak) so DLL detach can restore it.
            PostMessageW(h, DGH_CLOAK, 0, 0);
            Check(PumpUntilCloaked(h, 1, 2000), "re-cloak before teardown");
            Check(remove(hk) != FALSE, "DgRemoveHook succeeds");
            Check(Cloaked(h) == 1, "removing the hook does not un-cloak");
        }
        // Note: the loader defers unloading the hook DLL after UnhookWindowsHookEx,
        // so DllMain's DETACH auto-un-cloak can't be deterministically triggered
        // from within the same process here (the window is torn down below anyway).
        FreeLibrary(dll);
    }

    if (h) DestroyWindow(h);
}

static void ThemeTests()
{
    std::printf("Theme tests\n");
    SetEnvironmentVariableW(L"DEGHOSTER_FORCE_THEME", L"light");
    Check(!Theme::current().dark, "forced light theme -> dark == false");
    SetEnvironmentVariableW(L"DEGHOSTER_FORCE_THEME", L"dark");
    Check(Theme::current().dark, "forced dark theme -> dark == true");
    SetEnvironmentVariableW(L"DEGHOSTER_FORCE_THEME", nullptr);
}

int main()
{
    SettingsTests();
    ThemeTests();
    HookDllTests();
    std::printf("%s (%d failure(s))\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
