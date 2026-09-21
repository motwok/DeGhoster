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
#include <unordered_map>

// Injects the cloaking hook (DWMWA_CLOAK must run in-process): x64 targets via
// the loaded Hook64 DLL, x86 targets via the Helper32 process. Deduped per thread.
class HookInjector {
public:
    ~HookInjector();

    bool load(const std::wstring& exeDir);
    bool ensure(DWORD threadId, DWORD pid, HWND host);
    void removeAll();

private:
    typedef HHOOK (__stdcall* InstallFn)(DWORD, HWND);
    typedef BOOL  (__stdcall* RemoveFn)(HHOOK);

    // Thread IDs are recycled by Windows, so a bare threadId is not a stable key:
    // a restarted target can reuse an id we still hold an entry for. We pin each
    // entry to the thread's creation time and drop it when that no longer matches.
    struct HookEntry   { HHOOK  hook = nullptr; ULONGLONG born = 0; };
    struct HelperEntry { HANDLE proc = nullptr; ULONGLONG born = 0; };

    static ULONGLONG threadBornTime(DWORD threadId);
    // Returns true if a live, matching entry already exists; prunes a stale one.
    bool haveLiveEntry(DWORD threadId);

    HMODULE   dll_ = nullptr;
    InstallFn install_ = nullptr;
    RemoveFn  remove_  = nullptr;
    std::wstring exeDir_;
    std::unordered_map<DWORD, HookEntry>   hooks_;    // threadId -> hook (x64)
    std::unordered_map<DWORD, HelperEntry> helpers_;  // threadId -> helper process (x86)
};
