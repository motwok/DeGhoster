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

namespace proc {

std::wstring ExeDir();   // trailing backslash
bool IsWow64(DWORD pid);

// Resolve the owning host program (skips the msedgewebview2 parent chain).
void ResolveHostExe(DWORD pid, std::wstring& exeName, std::wstring& exePath);

std::wstring WindowTitle(HWND);

} // namespace proc
