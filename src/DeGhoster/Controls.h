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

// Owner-drawn controls in the look of the status window: a slider with a value
// label. Keyboard operable, shows a focus rectangle, greys out when disabled,
// follows the parent's RTL mirroring and sizes itself from GetDpiForWindow.
namespace ui {

inline constexpr wchar_t kSliderClass[] = L"DeGhosterSlider";

// lParam = const Theme* (copied).
constexpr UINT CTL_SETTHEME = WM_USER + 0x60;

// Slider: notifies the parent with WM_HSCROLL(MAKEWPARAM(SB_THUMBPOSITION, pos), hwnd)
// whenever the user changes the value. Keys: arrows +/- step, PgUp/PgDn +/- page,
// Home/End = limits. The value label is drawn as "<pos> %".
constexpr UINT SLM_SETRANGE = WM_USER + 0x61;   // wParam = min, lParam = max
constexpr UINT SLM_SETSTEP  = WM_USER + 0x62;   // wParam = step, lParam = page
constexpr UINT SLM_SETPOS   = WM_USER + 0x63;   // wParam = pos (no notification)
constexpr UINT SLM_GETPOS   = WM_USER + 0x64;

void RegisterControls(HINSTANCE);

} // namespace ui
