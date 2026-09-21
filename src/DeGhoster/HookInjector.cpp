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

#include "HookInjector.h"
#include "ProcessUtil.h"
#include <vector>

HookInjector::~HookInjector() { removeAll(); }

bool HookInjector::load(const std::wstring& exeDir)
{
    exeDir_ = exeDir;
    dll_ = LoadLibraryW((exeDir_ + L"DeGhoster.Hook64.dll").c_str());
    if (!dll_) return false;
    install_ = (InstallFn)GetProcAddress(dll_, "DgInstallHook");
    remove_  = (RemoveFn)GetProcAddress(dll_, "DgRemoveHook");
    return install_ && remove_;
}

ULONGLONG HookInjector::threadBornTime(DWORD threadId)
{
    HANDLE t = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, threadId);
    if (!t) return 0;
    FILETIME create{}, exit{}, kernel{}, user{};
    ULONGLONG born = 0;
    if (GetThreadTimes(t, &create, &exit, &kernel, &user))
        born = ((ULONGLONG)create.dwHighDateTime << 32) | create.dwLowDateTime;
    CloseHandle(t);
    return born;
}

bool HookInjector::haveLiveEntry(DWORD threadId)
{
    ULONGLONG born = threadBornTime(threadId);

    auto h = hooks_.find(threadId);
    if (h != hooks_.end()) {
        if (born && born == h->second.born) return true;   // same thread, hook still ours
        if (remove_) remove_(h->second.hook);              // stale: thread id was recycled
        hooks_.erase(h);
    }

    auto p = helpers_.find(threadId);
    if (p != helpers_.end()) {
        bool alive = p->second.proc &&
                     WaitForSingleObject(p->second.proc, 0) == WAIT_TIMEOUT;
        if (alive && born && born == p->second.born) return true;
        if (p->second.proc) { TerminateProcess(p->second.proc, 0); CloseHandle(p->second.proc); }
        helpers_.erase(p);
    }
    return false;
}

bool HookInjector::ensure(DWORD threadId, DWORD pid, HWND host)
{
    if (haveLiveEntry(threadId)) return true;

    ULONGLONG born = threadBornTime(threadId);

    if (proc::IsWow64(pid)) {
        std::wstring exe = exeDir_ + L"DeGhoster.Helper32.exe";
        if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

        // The helper installs the hook asynchronously in its own process. Unlike
        // the in-process x64 path, the hook is NOT live the instant ensure()
        // returns, so we must not post DGH_CLOAK yet or it is lost. The helper
        // signals this manual-reset event once its hook is installed; we wait for
        // it (or the helper dying, or a timeout) before returning.
        DWORD myPid = GetCurrentProcessId();
        std::wstring evName = L"Local\\DeGhoster.HelperReady." +
                              std::to_wstring(myPid) + L"." + std::to_wstring(threadId);
        HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, evName.c_str());
        if (!ready) return false;

        // Build the command line in a std::wstring: the exe path can be up to
        // MAX_PATH, so a fixed 256-wchar buffer (old wsprintfW) could overflow.
        std::wstring cmd = L"\"" + exe + L"\" " +
                           std::to_wstring(threadId) + L" " +
                           std::to_wstring((unsigned long long)(ULONG_PTR)host) + L" " +
                           std::to_wstring(myPid);
        std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back(L'\0');   // CreateProcessW may write to the command line

        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(ready);
            return false;
        }
        CloseHandle(pi.hThread);

        HANDLE waits[2] = { ready, pi.hProcess };
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, 5000);
        CloseHandle(ready);
        if (w != WAIT_OBJECT_0) {
            // Helper failed to install the hook (it exited, or timed out). Caching
            // the dead handle here would strand the window in pending_ forever, so
            // tear it down and report failure instead.
            TerminateProcess(pi.hProcess, 0);
            CloseHandle(pi.hProcess);
            return false;
        }
        helpers_[threadId] = { pi.hProcess, born };
        return true;
    }

    if (!install_) return false;
    HHOOK hk = install_(threadId, host);
    if (!hk) return false;
    hooks_[threadId] = { hk, born };
    return true;
}

void HookInjector::removeAll()
{
    if (remove_)
        for (auto& kv : hooks_) remove_(kv.second.hook);
    hooks_.clear();
    for (auto& kv : helpers_)
        if (kv.second.proc) { TerminateProcess(kv.second.proc, 0); CloseHandle(kv.second.proc); }
    helpers_.clear();
}
