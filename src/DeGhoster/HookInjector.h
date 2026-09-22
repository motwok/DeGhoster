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

// Installs the cloaking hook into target threads. DWMWA_CLOAK only takes effect
// in-process, so the work has to happen inside the target.
//
// The host never loads a hook DLL itself. Every injection goes through a helper
// process of the target's bitness, which owns the LoadLibrary and the hook. That
// buys one code path instead of two, and gives every hook a watchdog: the helper
// waits on the host and on the hooked thread, so the hook is removed even if the
// host dies without running its own teardown. Deduped per thread.
class HookInjector {
public:
    ~HookInjector();

    // Outcome of ensure(). A helper installs the hook asynchronously, so success
    // cannot be reported synchronously: callers must tolerate Pending and come
    // back later instead of blocking the UI thread until the helper is up.
    enum class Inject { Ready, Pending, Failed };

    bool load(const std::wstring& exeDir);   // remember the dir, check the helpers
    bool available() const { return available_; }
    Inject ensure(DWORD threadId, DWORD pid, HWND host);
    void pruneDead();    // release entries whose target thread no longer exists
    void removeAll();

private:
    // Thread IDs are recycled by Windows, so a bare threadId is not a stable key:
    // a restarted target can reuse an id we still hold an entry for. We pin each
    // entry to the thread's creation time and drop it when that no longer matches.
    struct HelperEntry {
        HANDLE    proc       = nullptr;
        HANDLE    ready      = nullptr;   // helper signals it once its hook is live
        DWORD     mainThread = 0;         // for a graceful WM_QUIT on teardown
        ULONGLONG born       = 0;         // creation time of the hooked thread
        ULONGLONG startedAt  = 0;
    };

    static ULONGLONG threadBornTime(DWORD threadId);
    static void nudge(DWORD threadId);     // make the target unmap the hook DLL
    static void closeHelper(HelperEntry&);
    std::wstring helperPath(DWORD pid) const;

    std::wstring exeDir_;
    bool available_ = false;
    std::unordered_map<DWORD, HelperEntry> helpers_;   // threadId -> helper process
};
