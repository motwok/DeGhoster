// Copyright (c) Emmo Emminghaus mo2000 at mo2000 dot de
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <windows.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <cstdio>

#include "Settings.h"
#include "Theme.h"
#include "Hook.h"
#include "ProcessUtil.h"
#include "HookInjector.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")

static int g_failures = 0;

// Returns the condition so a failed precondition can skip what follows.
static bool Check(bool cond, const char* what)
{
    std::printf(cond ? "  [ok]   %s\n" : "  [FAIL] %s\n", what);
    if (!cond) ++g_failures;
    return cond;
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
    {
        // A value name longer than the old fixed 1024-wchar buffer used to make
        // RegEnumValueW return ERROR_MORE_DATA and cut the enumeration short,
        // silently dropping this opt-out (and any after it) on reload.
        const std::wstring longKey = L"C:\\Apps\\Ghosty.exe|" + std::wstring(2000, L'x');
        const std::wstring shortKey = L"C:\\Apps\\Other.exe|Small";
        Settings s; s.load();
        s.setManaged(longKey, false);
        s.setManaged(shortKey, false);
        Settings s2; s2.load();
        Check(!s2.isManaged(longKey), "long (>1024 char) opt-out survives reload");
        Check(!s2.isManaged(shortKey), "opt-out after a long one is not dropped");
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

static void ProcessUtilTests()
{
    std::printf("ProcessUtil tests\n");

    const std::wstring dir = proc::ExeDir();
    Check(!dir.empty() && dir.back() == L'\\', "ExeDir ends in a backslash");
    Check(GetFileAttributesW((dir + L"DeGhoster.Hook64.dll").c_str()) != INVALID_FILE_ATTRIBUTES,
          "ExeDir points at the build output");

    // WindowTitle must stay RAW: the localized placeholder would end up in the
    // registry opt-out key and be invalidated by a UI-language change.
    const wchar_t* cls = L"DeGhosterTitleProbe";
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);
    HWND named = CreateWindowExW(0, cls, L"ProbeTitle", WS_POPUP, 0, 0, 10, 10,
                                 nullptr, nullptr, wc.hInstance, nullptr);
    HWND blank = CreateWindowExW(0, cls, L"", WS_POPUP, 0, 0, 10, 10,
                                 nullptr, nullptr, wc.hInstance, nullptr);
    Check(proc::WindowTitle(named) == L"ProbeTitle", "WindowTitle returns the title");
    Check(proc::WindowTitle(blank).empty(), "WindowTitle leaves an untitled window empty");

    std::wstring exeName, exePath;
    proc::ResolveHostExe(GetCurrentProcessId(), exeName, exePath);
    Check(_wcsicmp(exeName.c_str(), L"UnitTests.exe") == 0, "ResolveHostExe finds our own image");
    Check(!exePath.empty(), "ResolveHostExe returns a full path");
    proc::ResolveHostExe(0xFFFFFFFCu, exeName, exePath);
    Check(exeName == L"?", "ResolveHostExe marks an unknown pid");

    Check(!proc::IsWow64(GetCurrentProcessId()), "the x64 test process is not WOW64");
    Check(!proc::IsWow64(0xFFFFFFFCu), "IsWow64 is false for an unknown pid");

    if (named) DestroyWindow(named);
    if (blank) DestroyWindow(blank);
}

// A thread with a message queue that exits when its event is signalled, so a
// hook can be installed on it and the thread can then be made to go away.
static DWORD WINAPI ProbeThread(LPVOID param)
{
    HANDLE stop = (HANDLE)param;
    MSG m;
    PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE);   // force the queue into existence
    for (;;) {
        DWORD w = MsgWaitForMultipleObjects(1, &stop, FALSE, INFINITE, QS_ALLINPUT);
        if (w == WAIT_OBJECT_0) break;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { }
    }
    return 0;
}

static void HookInjectorTests()
{
    std::printf("HookInjector tests\n");

    {
        HookInjector bad;
        Check(!bad.load(proc::ExeDir() + L"does-not-exist\\"), "load fails without the helpers");
        Check(!bad.available(), "available is false after a failed load");
    }

    HookInjector inj;
    Check(inj.load(proc::ExeDir()), "load finds both helpers");
    Check(inj.available(), "available is true once both helpers are there");

    // A helper started for a thread that does not exist installs nothing and
    // exits. The first call only starts it, so it reports Pending; once it has
    // died the next call must report Failed so the caller's cooldown kicks in.
    const DWORD deadThread = 0xFFFFFFFCu;
    Check(inj.ensure(deadThread, GetCurrentProcessId(), nullptr) == HookInjector::Inject::Pending,
          "ensure reports Pending while the helper starts");
    HookInjector::Inject late = HookInjector::Inject::Pending;
    for (int i = 0; i < 100 && late == HookInjector::Inject::Pending; ++i) {
        Sleep(50);
        late = inj.ensure(deadThread, GetCurrentProcessId(), nullptr);
    }
    Check(late == HookInjector::Inject::Failed, "ensure reports Failed once the helper is gone");

    // The real path, on this very thread: the helper comes up, signals readiness
    // and the entry flips to Ready.
    const DWORD self = GetCurrentThreadId();
    Check(inj.ensure(self, GetCurrentProcessId(), nullptr) == HookInjector::Inject::Pending,
          "ensure starts a helper for a live thread");
    HookInjector::Inject state = HookInjector::Inject::Pending;
    for (int i = 0; i < 200 && state != HookInjector::Inject::Ready; ++i) {
        Sleep(25);
        state = inj.ensure(self, GetCurrentProcessId(), nullptr);
    }
    Check(state == HookInjector::Inject::Ready, "ensure flips to Ready once the helper signals");
    Check(proc::HelperRunning(), "HelperRunning sees the helper we started");

    // The hooked thread is still alive, so the entry must survive a prune.
    inj.pruneDead();
    Check(inj.ensure(self, GetCurrentProcessId(), nullptr) == HookInjector::Inject::Ready,
          "pruneDead keeps the entry for a live thread");

    inj.removeAll();
    bool gone = false;
    for (int i = 0; i < 100 && !gone; ++i) { Sleep(50); gone = !proc::HelperRunning(); }
    Check(gone, "removeAll winds the helper down");

    // pruneDead has to reclaim entries whose thread died. Nothing tells the host
    // about that, so without the sweep the entry would sit there until shutdown.
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD probeTid = 0;
    HANDLE probe = CreateThread(nullptr, 0, ProbeThread, stop, 0, &probeTid);
    Check(probe != nullptr, "probe thread starts");
    if (probe) {
        Sleep(150);   // let the thread reach its message loop
        HookInjector::Inject st = HookInjector::Inject::Pending;
        for (int i = 0; i < 200 && st != HookInjector::Inject::Ready; ++i) {
            Sleep(25);
            st = inj.ensure(probeTid, GetCurrentProcessId(), nullptr);
        }
        Check(st == HookInjector::Inject::Ready, "helper hooks the probe thread");

        SetEvent(stop);
        Check(WaitForSingleObject(probe, 5000) == WAIT_OBJECT_0, "probe thread exits");
        for (int i = 0; i < 100 && proc::HelperRunning(); ++i) Sleep(50);

        // The entry is stale now. After the sweep, asking again must start a fresh
        // helper (Pending) rather than report the dead one as a failure.
        inj.pruneDead();
        Check(inj.ensure(probeTid, GetCurrentProcessId(), nullptr) == HookInjector::Inject::Pending,
              "pruneDead drops the entry for a dead thread");
        inj.removeAll();
        CloseHandle(probe);
    }
    CloseHandle(stop);

    // The documented bail-out: a helper that cannot open the host has no exit
    // condition (it owns no window, so no WM_QUIT ever reaches it) and must not
    // linger with the hook installed. Valid thread id so the hook installs, bogus
    // host pid so opening the host fails.
    {
        std::wstring cmd = L"\"" + proc::ExeDir() + L"DeGhoster.Helper64.exe\" " +
                           std::to_wstring(GetCurrentThreadId()) + L" 0 4294967292";
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(L'\0');
        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        if (Check(CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                                 CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != FALSE,
                  "helper starts for the host-handle test")) {
            CloseHandle(pi.hThread);
            DWORD code = 1;
            Check(WaitForSingleObject(pi.hProcess, 10000) == WAIT_OBJECT_0,
                  "helper exits instead of lingering without a host");
            GetExitCodeProcess(pi.hProcess, &code);
            Check(code == 5, "helper reports the host-handle failure (exit 5)");
            CloseHandle(pi.hProcess);
        }
    }
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
    ProcessUtilTests();
    HookInjectorTests();
    HookDllTests();
    std::printf("%s (%d failure(s))\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
