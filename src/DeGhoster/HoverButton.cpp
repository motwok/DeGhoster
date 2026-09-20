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

#include "HoverButton.h"
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

namespace {

LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    if (msg == WM_MOUSEMOVE) {
        if (!GetPropW(h, L"hot")) {
            SetPropW(h, L"hot", (HANDLE)1);
            InvalidateRect(h, nullptr, TRUE);
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
        }
    } else if (msg == WM_MOUSELEAVE) {
        RemovePropW(h, L"hot");
        InvalidateRect(h, nullptr, TRUE);
    }
    return DefSubclassProc(h, msg, wp, lp);
}

} // namespace

namespace ui {

void EnableHover(HWND button) { SetWindowSubclass(button, Proc, 1, 0); }
bool IsHot(HWND button) { return GetPropW(button, L"hot") != nullptr; }

} // namespace ui
