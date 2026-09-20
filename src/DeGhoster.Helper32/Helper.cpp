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

// Helper.cpp - 32-Bit-Injektor fuer DeGhoster.
//
// Der x64-Host kann die 32-Bit-Hook-DLL nicht selbst in 32-Bit-Zielprozesse laden.
// Dieser kleine x86-Helfer uebernimmt das: er laedt DeGhoster.Hook32.dll, ruft
// DgInstallHook(threadId, hostHwnd) und haelt den Hook, bis der Host endet (dann
// DgRemoveHook + Ende). Erkennung/Cloak-Kommandos macht weiterhin der Host per
// PostMessage; die injizierte 32-Bit-DLL fuehrt sie im Zielprozess aus.
//
// Aufruf:  DeGhoster.Helper32.exe <threadId> <hostHwnd> <hostPid>   (alle dezimal)

#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string>

typedef HHOOK (__stdcall *InstallFn)(DWORD, HWND);
typedef BOOL  (__stdcall *RemoveFn)(HHOOK);

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc < 4) return 1;                       // argv[0] = exe-Pfad
    DWORD tid     = (DWORD)wcstoul(argv[1], nullptr, 10);
    HWND  host    = (HWND)(UINT_PTR)_wcstoui64(argv[2], nullptr, 10);
    DWORD hostPid = (DWORD)wcstoul(argv[3], nullptr, 10);

    // Hook-DLL neben dieser Exe laden.
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) slash[1] = 0;
    wcscat_s(path, L"DeGhoster.Hook32.dll");
    HMODULE dll = LoadLibraryW(path);
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
    if (hostProc)
    {
        // Nachrichten pumpen und gleichzeitig auf das Host-Ende warten.
        for (;;)
        {
            DWORD w = MsgWaitForMultipleObjects(1, &hostProc, FALSE, INFINITE, QS_ALLINPUT);
            if (w == WAIT_OBJECT_0) break;                 // Host beendet
            MSG msg;
            bool quit = false;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT) { quit = true; break; }
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            if (quit) break;
        }
        CloseHandle(hostProc);
    }
    else
    {
        // Fallback: bis WM_QUIT laufen.
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }

    remove(hook);
    FreeLibrary(dll);
    return 0;
}
