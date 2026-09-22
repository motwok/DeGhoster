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

// A helper that never signals readiness within this long is written off, so a
// wedged helper cannot keep a window in limbo forever.
namespace { constexpr ULONGLONG kHelperReadyTimeoutMs = 5000; }

void HookInjector::nudge(DWORD threadId)
{
    // Removing a hook does not unmap the DLL: Windows only lets go once the hooked
    // thread next pulls a message, so an idle target keeps DeGhoster.Hook64.dll
    // mapped - and the file locked - indefinitely. That matters beyond tidiness: an
    // installer replacing the DLL sees those targets as holding it and offers to
    // shut them down. WM_NULL goes to the thread queue, not to a window, so it is a
    // no-op for the target beyond waking its pump.
    PostThreadMessageW(threadId, WM_NULL, 0, 0);
}

void HookInjector::dropHook(DWORD threadId, HHOOK hook)
{
    if (remove_) remove_(hook);
    nudge(threadId);
}

void HookInjector::closeHelper(HelperEntry& e)
{
    if (e.proc)  { TerminateProcess(e.proc, 0); CloseHandle(e.proc); e.proc = nullptr; }
    if (e.ready) { CloseHandle(e.ready); e.ready = nullptr; }
}

HookInjector::Inject HookInjector::ensure(DWORD threadId, DWORD pid, HWND host)
{
    const ULONGLONG born = threadBornTime(threadId);

    auto h = hooks_.find(threadId);
    if (h != hooks_.end()) {
        if (born && born == h->second.born) return Inject::Ready;   // still ours
        dropHook(threadId, h->second.hook);                         // id was recycled
        hooks_.erase(h);
    }

    auto p = helpers_.find(threadId);
    if (p != helpers_.end()) {
        HelperEntry& e = p->second;
        const bool alive = e.proc && WaitForSingleObject(e.proc, 0) == WAIT_TIMEOUT;
        const bool sameThread = born && born == e.born;
        if (alive && sameThread) {
            if (e.ready && WaitForSingleObject(e.ready, 0) == WAIT_OBJECT_0)
                return Inject::Ready;
            if (GetTickCount64() - e.startedAt < kHelperReadyTimeoutMs)
                return Inject::Pending;      // still coming up; ask again next tick
            closeHelper(e);
            helpers_.erase(p);
            return Inject::Failed;           // never signalled: let the pid cool down
        }
        closeHelper(e);
        helpers_.erase(p);
        // A helper that died on us is reported as a failure so the caller's pid
        // cooldown throttles the retry. A live helper bound to a recycled thread
        // id is simply stale, so fall through and start a fresh one.
        if (!alive || !born) return Inject::Failed;
    }

    if (proc::IsWow64(pid)) {
        std::wstring exe = exeDir_ + L"DeGhoster.Helper32.exe";
        if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return Inject::Failed;

        // The helper installs the hook asynchronously in its own process, so the
        // hook is NOT live when this returns and posting DGH_CLOAK now would lose
        // it. The helper sets this manual-reset event once its hook is installed.
        const DWORD myPid = GetCurrentProcessId();
        std::wstring evName = L"Local\\DeGhoster.HelperReady." +
                              std::to_wstring(myPid) + L"." + std::to_wstring(threadId);
        HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, evName.c_str());
        if (!ready) return Inject::Failed;
        ResetEvent(ready);   // a same-named leftover may still be signalled

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
            return Inject::Failed;
        }
        CloseHandle(pi.hThread);

        // Deliberately no wait here: ensure() runs on the UI thread from WinEvent
        // callbacks, WM_TIMER and click handlers, and blocking for seconds froze
        // the window. The caller's periodic tick re-drives this and the entry
        // flips to Ready as soon as the helper signals.
        helpers_[threadId] = { pi.hProcess, ready, born, GetTickCount64() };
        return Inject::Pending;
    }

    if (!install_) return Inject::Failed;
    HHOOK hk = install_(threadId, host);
    if (!hk) return Inject::Failed;
    hooks_[threadId] = { hk, born };
    return Inject::Ready;
}

void HookInjector::pruneDead()
{
    // Nothing tells us when a hooked thread exits, so entries for dead threads
    // would otherwise sit here (holding a live helper process) until shutdown.
    for (auto it = hooks_.begin(); it != hooks_.end(); ) {
        const ULONGLONG born = threadBornTime(it->first);
        if (!born || born != it->second.born) {
            dropHook(it->first, it->second.hook);
            it = hooks_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = helpers_.begin(); it != helpers_.end(); ) {
        const ULONGLONG born = threadBornTime(it->first);
        const bool alive = it->second.proc &&
                           WaitForSingleObject(it->second.proc, 0) == WAIT_TIMEOUT;
        if (!alive || !born || born != it->second.born) {
            closeHelper(it->second);
            nudge(it->first);
            it = helpers_.erase(it);
        } else {
            ++it;
        }
    }
}

void HookInjector::removeAll()
{
    for (auto& kv : hooks_) dropHook(kv.first, kv.second.hook);
    hooks_.clear();
    for (auto& kv : helpers_) { closeHelper(kv.second); nudge(kv.first); }
    helpers_.clear();
}
