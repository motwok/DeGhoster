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

#include "Theme.h"
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

bool IsOsDark()
{
    // Test override (same env-override pattern as DEGHOSTER_UILANG /
    // DEGHOSTER_SETTINGS_ROOT): force light or dark regardless of the OS setting.
    wchar_t forced[16];
    DWORD fn = GetEnvironmentVariableW(L"DEGHOSTER_FORCE_THEME", forced, 16);
    if (fn > 0 && fn < 16) {
        if (lstrcmpiW(forced, L"light") == 0) return false;
        if (lstrcmpiW(forced, L"dark") == 0)  return true;
    }

    HKEY k;
    DWORD v = 1, sz = sizeof(v);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegQueryValueExW(k, L"AppsUseLightTheme", nullptr, nullptr, (LPBYTE)&v, &sz);
        RegCloseKey(k);
    }
    return v == 0;
}

Theme Theme::current()
{
    Theme t;
    t.dark = IsOsDark();
    if (t.dark) {
        t.back = RGB(32, 32, 34);  t.panel = RGB(45, 45, 48);   t.fore = RGB(240, 240, 240);
        t.listBg = RGB(38, 38, 40); t.rowAlt = RGB(46, 46, 50); t.foreDim = RGB(140, 140, 145);
    } else {
        t.back = RGB(243, 243, 243); t.panel = RGB(243, 243, 243); t.fore = RGB(0, 0, 0);
        t.listBg = RGB(255, 255, 255); t.rowAlt = RGB(244, 246, 250); t.foreDim = RGB(120, 120, 120);
    }
    return t;
}

void ApplyDarkTitleBar(HWND h, bool dark)
{
    BOOL v = dark;
    DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &v, sizeof(v));
    SetWindowPos(h, nullptr, 0, 0, 0, 0,   // frame change repaints the caption
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}
