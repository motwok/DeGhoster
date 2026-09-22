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
#include <commctrl.h>
#include <shellapi.h>

#include "Autostart.h"
#include "Gfx.h"
#include "Loc.h"
#include "MainWindow.h"
#include "InfoWindow.h"

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int)
{
    int autostartExit = 0;
    if (autostart::handleCommandLine(&autostartExit))
        return autostartExit;

    // Single instance: a second launch (e.g. autostart + a manual start) would give
    // two tray icons and two engines fighting over the same windows. Hand off to the
    // running instance and exit. The mutex is released automatically on exit.
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\DeGhoster.SingleInstance");
    const DWORD mutexErr = GetLastError();
    // ERROR_ACCESS_DENIED means the mutex exists but belongs to an instance we are
    // not allowed to open, typically one running elevated. That is still "already
    // running": treating the null handle as "first instance" started a second tray
    // icon and a second engine fighting over the same windows.
    const bool alreadyRunning = instanceMutex ? (mutexErr == ERROR_ALREADY_EXISTS)
                                              : (mutexErr == ERROR_ACCESS_DENIED);
    if (alreadyRunning) {
        MainWindow::activateExisting();
        return 0;
    }

    // --taskbar: autostart launches with this so the app comes up in the tray
    // instead of popping the main window open.
    bool startHidden = false;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; ++i)
                if (lstrcmpiW(argv[i], L"--taskbar") == 0 || lstrcmpiW(argv[i], L"/taskbar") == 0) {
                    startHidden = true;
                    break;
                }
            LocalFree(argv);
        }
    }

    loc::init();
    gfx::GdiPlus gdiplus;
    INITCOMMONCONTROLSEX icc{ sizeof(icc),
        ICC_LISTVIEW_CLASSES | ICC_LINK_CLASS | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    MainWindow window;
    if (!window.create(inst, startHidden)) return 1;

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        HWND info = InfoWindow::ActiveHandle();
        if (info && IsDialogMessageW(info, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return (int)m.wParam;
}
