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

#include "Autostart.h"
#include "Gfx.h"
#include "Loc.h"
#include "MainWindow.h"
#include "InfoWindow.h"

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int)
{
    // Installer hooks: (un)register the per-user autostart, then exit without UI.
    int autostartExit = 0;
    if (autostart::handleCommandLine(&autostartExit))
        return autostartExit;

    loc::init();
    gfx::GdiPlus gdiplus;
    INITCOMMONCONTROLSEX icc{ sizeof(icc),
        ICC_LISTVIEW_CLASSES | ICC_LINK_CLASS | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    MainWindow window;
    if (!window.create(inst)) return 1;

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        HWND info = InfoWindow::ActiveHandle();
        if (info && IsDialogMessageW(info, &m)) continue;   // Tab/Esc/Enter routing
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return (int)m.wParam;
}
