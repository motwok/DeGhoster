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
// `program`, if given, receives the program's opt-out key (see ProgramKeyFromPath);
// for a packaged app it comes from the package identity Windows reports.
void ResolveHostExe(DWORD pid, std::wstring& exeName, std::wstring& exePath,
                    std::wstring* program = nullptr);

// The key the per-program opt-out stores, stable across the program's updates:
//   * a packaged (Store) app in "...\WindowsApps\<Name>_<Version>_<Arch>_<Res>_<Publisher>"
//     -> "<Name>_<Publisher>\<path inside the package>" (the package family name);
//   * a Squirrel-installed app in "...\<App>\app-<version>\<exe>" -> the same path
//     with "app-<version>" reduced to "app";
//   * any other program -> its full path;
//   * no path (the process could not be read) -> "?".
std::wstring ProgramKeyFromPath(const std::wstring& exePath);

std::wstring WindowTitle(HWND);

// True while an injector helper started from our own directory is still running.
// The helpers hold a hook DLL open, so anything waiting for those files to become
// replaceable (an installer, say) has to wait for them too.
bool HelperRunning();

} // namespace proc
