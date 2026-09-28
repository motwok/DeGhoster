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
#include <string>

struct FixInfo {
    // Which case the window is: a click-eating ghost (neutralized by cloaking it
    // from inside its process) or an AnyDesk session window (whose remote cursor
    // the CursorOverlay enlarges; nothing is done to the window itself).
    enum class Kind { Ghost, AnyDesk };

    Kind kind = Kind::Ghost;
    DWORD pid = 0;
    std::wstring exeName, exePath, title;
    // A ghost its app has hidden (e.g. the app was minimized). It stays listed
    // until the window is destroyed, so the list does not flicker with the app.
    bool hidden = false;

    std::wstring disableKey() const { return exePath + L"|" + title; }  // registry opt-out key
};
