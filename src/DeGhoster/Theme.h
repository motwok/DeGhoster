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

struct Theme {
    bool dark = true;
    COLORREF back, panel, fore, listBg, rowAlt, foreDim;   // set by current()

    COLORREF accentOn  = RGB(34, 197, 94);
    COLORREF accentOff = RGB(96, 96, 96);
    COLORREF info      = RGB(250, 204, 21);
    COLORREF infoHot   = RGB(253, 224, 71);
    COLORREF exit       = RGB(165, 165, 165);
    COLORREF exitHot    = RGB(240, 240, 240);

    static Theme current();
};

bool IsOsDark();
void ApplyDarkTitleBar(HWND, bool dark);   // sets attribute + forces frame redraw
