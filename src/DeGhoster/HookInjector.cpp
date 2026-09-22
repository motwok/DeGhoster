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

namespace {

// A helper that never signals readiness within this long is written off, so a
// wedged helper cannot keep a window in limbo forever.
constexpr ULONGLONG kHelperReadyTimeoutMs = 5000;
// Shared budget for letting every helper wind down on teardown.
constexpr DWORD kHelperExitBudgetMs = 2000;

constexpr wchar_t kHelper64[] = L"DeGhoster.Helper64.exe";
constexpr wchar_t kHelper32[] = L"DeGhoster.Helper32.exe";

bool Exists(const std::wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

} // namespace

HookInjector::~HookInjector() { removeAll(); }

bool HookInjector::load(const std::wstring& exeDir)
{
    exeDir_ = exeDir;
    // Both are required: which one a window needs depends on the bitness of the
    // process that owns it, and that is only known once a ghost turns up.
    available_ = Exists(exeDir_ + kHelper64) && Exists(exeDir_ + kHelper32);
    return available_;
}

std::wstring HookInjector::helperPath(DWORD pid) const
{
    // A hook DLL must match the bitness of the thread it is installed on, so a
    // WOW64 target needs the 32-bit helper and everything else the 64-bit one.
    return exeDir_ + (proc::IsWow64(pid) ? kHelper32 : kHelper64);
}

void HookInjector::nudge(DWORD threadId)
{
    // Removing a hook does not unmap the DLL: Windows only lets go once the hooked
    // thread next pulls a message, so an idle target keeps the hook DLL mapped -
    // and its file locked - indefinitely. WM_NULL goes to the thread queue, not to
    // a window, so it is a no-op for the target beyond waking its pump.
    PostThreadMessageW(threadId, WM_NULL, 0, 0);
}

void HookInjector::closeHelper(HelperEntry& e)
{
    if (e.proc)  { TerminateProcess(e.proc, 0); CloseHandle(e.proc); e.proc = nullptr; }
    if (e.ready) { CloseHandle(e.ready); e.ready = nullptr; }
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

HookInjector::Inject HookInjector::ensure(DWORD threadId, DWORD pid, HWND host)
{
    const ULONGLONG born = threadBornTime(threadId);

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
        const bool diedOnUs = !alive;
        closeHelper(e);
        helpers_.erase(p);
        // A helper that died on us is reported as a failure so the caller's pid
        // cooldown throttles the retry. A live helper bound to a recycled thread
        // id is merely stale, so fall through and start a fresh one.
        if (diedOnUs || !born) return Inject::Failed;
    }

    const std::wstring exe = helperPath(pid);
    if (!Exists(exe)) return Inject::Failed;

    // The helper installs the hook asynchronously in its own process, so the hook
    // is NOT live when this returns and posting DGH_CLOAK now would lose it. The
    // helper sets this manual-reset event once its hook is installed.
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
    // callbacks, WM_TIMER and click handlers, and blocking for seconds froze the
    // window. The caller's periodic tick re-drives this and the entry flips to
    // Ready as soon as the helper signals.
    helpers_[threadId] = { pi.hProcess, ready, pi.dwThreadId, born, GetTickCount64() };
    return Inject::Pending;
}

void HookInjector::pruneDead()
{
    // Nothing tells us when a hooked thread exits, so entries for dead threads
    // would otherwise sit here, holding a live helper process, until shutdown.
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
    // Ask each helper to wind down on its own first: it then unhooks, unloads the
    // DLL and nudges its target, none of which a TerminateProcess would do.
    for (auto& kv : helpers_)
        if (kv.second.mainThread) PostThreadMessageW(kv.second.mainThread, WM_QUIT, 0, 0);

    // One budget for all of them, so a wedged helper cannot hold up shutdown.
    const ULONGLONG deadline = GetTickCount64() + kHelperExitBudgetMs;
    for (auto& kv : helpers_) {
        if (!kv.second.proc) continue;
        const ULONGLONG now = GetTickCount64();
        WaitForSingleObject(kv.second.proc, now < deadline ? (DWORD)(deadline - now) : 0);
    }

    for (auto& kv : helpers_) { closeHelper(kv.second); nudge(kv.first); }
    helpers_.clear();
}
