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

// Segoe MDL2 Assets code points. EyeOff uses F78D, not ED1A ("Hide"), which is
// absent on older MDL2 builds.
namespace glyph {
    inline constexpr wchar_t Power[]  = L"\uE7E8";
    inline constexpr wchar_t Info[]   = L"\uE946";
    inline constexpr wchar_t Exit[]   = L"\uF3B1";
    inline constexpr wchar_t Eye[]    = L"\uE7B3";
    inline constexpr wchar_t EyeOff[] = L"\uF78D";
}
