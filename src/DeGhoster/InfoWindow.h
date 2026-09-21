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

#pragma once
#include <windows.h>
#include "Theme.h"

// Dark-mode "About" popup (TaskDialog has no dark theming). Modeless, single-instance.
class InfoWindow {
public:
    static void Show(HINSTANCE, HWND owner, const Theme&, UINT dpi);
    static HWND ActiveHandle();   // for the main loop's IsDialogMessage; null when closed

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);
    void layout();
    void paint(HDC);

    HWND hwnd_ = nullptr, link_ = nullptr, licenseLink_ = nullptr, ok_ = nullptr;
    HBRUSH brush_ = nullptr;
    HICON icon_ = nullptr;
    HFONT uiFont_ = nullptr;   // owned: created in WM_CREATE, freed in WM_DESTROY
    Theme theme_;
    UINT dpi_ = 96;
    int S(int px) const { return MulDiv(px, dpi_, 96); }

    static HWND s_active;
};
