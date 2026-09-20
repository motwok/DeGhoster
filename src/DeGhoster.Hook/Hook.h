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

#ifndef DGH_HOOK_H
#define DGH_HOOK_H

#include <windows.h>

// Host -> DLL: an ein konkretes Fenster gepostete Kommandos (WM_APP-Bereich).
// Der WH_GETMESSAGE-Hook faengt sie ab (m->hwnd = Zielfenster) und fuehrt sie IN-PROCESS aus.
#define DGH_CLOAK          (WM_APP + 0x10)
#define DGH_UNCLOAK        (WM_APP + 0x11)

// DLL -> Host: Statusmeldungen (wParam = HWND).
#define WM_DGH_CLOAKED     (WM_APP + 0x20)
#define WM_DGH_UNCLOAKED   (WM_APP + 0x21)

// DWM-Cloak-Attribut (in-process setzbar => DWM_CLOAKED_APP).
#ifndef DWMWA_CLOAK
#define DWMWA_CLOAK        13
#endif
#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED      14
#endif

#endif
