// Copyright (c) Emmo Emminghaus mo2000 at mo2000 dot de
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string>

typedef HHOOK (__stdcall *InstallFn)(DWORD, HWND);
typedef BOOL  (__stdcall *RemoveFn)(HHOOK);

// The helper is built for both bitnesses and always loads the matching hook DLL;
// a hook DLL must match the bitness of the thread it is installed on.
#if defined(_WIN64)
static const wchar_t kHookDll[] = L"DeGhoster.Hook64.dll";
#else
static const wchar_t kHookDll[] = L"DeGhoster.Hook32.dll";
#endif

// Removing the hook does not unmap the DLL from the target: Windows only lets go
// once that thread next pulls a message. Without this an idle target keeps the
// hook DLL mapped, and its file locked, long after we are gone - which is enough
// to make an installer treat the target as holding the file.
static void NudgeTarget(DWORD tid) { PostThreadMessageW(tid, WM_NULL, 0, 0); }

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 4) return 1;
    DWORD tid     = (DWORD)wcstoul(argv[1], nullptr, 10);
    HWND  host    = (HWND)(UINT_PTR)_wcstoui64(argv[2], nullptr, 10);
    DWORD hostPid = (DWORD)wcstoul(argv[3], nullptr, 10);

    wchar_t path[MAX_PATH];
    DWORD pn = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (pn == 0 || pn >= MAX_PATH) return 2;   // path truncated: don't guess a wrong dir
    std::wstring dllPath(path, pn);
    size_t slash = dllPath.find_last_of(L'\\');
    dllPath = (slash == std::wstring::npos) ? std::wstring() : dllPath.substr(0, slash + 1);
    dllPath += kHookDll;
    HMODULE dll = LoadLibraryW(dllPath.c_str());
    if (!dll) return 2;

    InstallFn install = (InstallFn)GetProcAddress(dll, "DgInstallHook");
    RemoveFn  remove  = (RemoveFn) GetProcAddress(dll, "DgRemoveHook");
    if (!install || !remove) return 3;

    HHOOK hook = install(tid, host);
    if (!hook) return 4;

    // Tell the host the hook is now live, so it only now posts DGH_CLOAK
    // (avoids a race where the command arrives before the hook is installed).
    // Event name must match HookInjector: Local\DeGhoster.HelperReady.<hostPid>.<tid>
    {
        std::wstring evName = std::wstring(L"Local\\DeGhoster.HelperReady.") + argv[3] + L"." + argv[1];
        HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, evName.c_str());
        if (ready) { SetEvent(ready); CloseHandle(ready); }
    }

    HANDLE hostProc = OpenProcess(SYNCHRONIZE, FALSE, hostPid);
    if (!hostProc)
    {
        // Without a handle to the host we have no exit condition (the helper has no
        // window, so no WM_QUIT ever arrives). Falling back to GetMessageW would
        // orphan this process forever with the hook still installed. Bail out.
        remove(hook);
        NudgeTarget(tid);
        FreeLibrary(dll);
        return 5;
    }

    // Watch the hooked thread as well. With only the host handle to wait on, the
    // helper outlives its target: closing the app leaves one orphaned helper (with
    // a dead hook) per hooked thread running until DeGhoster itself quits.
    HANDLE tgtThread = OpenThread(SYNCHRONIZE, FALSE, tid);
    HANDLE waits[2] = { hostProc, tgtThread };
    const DWORD nWait = tgtThread ? 2 : 1;

    for (;;)
    {
        DWORD w = MsgWaitForMultipleObjects(nWait, waits, FALSE, INFINITE, QS_ALLINPUT);
        if (w >= WAIT_OBJECT_0 && w < WAIT_OBJECT_0 + nWait) break;   // host or target gone
        MSG msg;
        bool quit = false;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) { quit = true; break; }
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (quit) break;
    }
    if (tgtThread) CloseHandle(tgtThread);
    CloseHandle(hostProc);

    remove(hook);
    NudgeTarget(tid);
    FreeLibrary(dll);
    return 0;
}
