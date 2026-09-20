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

bool HookInjector::ensure(DWORD threadId, DWORD pid, HWND host)
{
    if (hooks_.count(threadId) || helpers_.count(threadId)) return true;

    if (proc::IsWow64(pid)) {   // x86 target -> delegate to the 32-bit helper
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

        wchar_t cmd[256];
        wsprintfW(cmd, L"\"%s\" %lu %llu %lu", exe.c_str(),
                  threadId, (unsigned long long)(ULONG_PTR)host, myPid);
        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            if (ready) CloseHandle(ready);
            return false;
        }
        CloseHandle(pi.hThread);

        if (ready) {
            HANDLE waits[2] = { ready, pi.hProcess };  // hook ready, or helper exited
            WaitForMultipleObjects(2, waits, FALSE, 5000);
            CloseHandle(ready);
        }
        helpers_[threadId] = pi.hProcess;
        return true;
    }

    if (!install_) return false;
    HHOOK hk = install_(threadId, host);
    if (!hk) return false;
    hooks_[threadId] = hk;
    return true;
}

void HookInjector::removeAll()
{
    if (remove_)
        for (auto& kv : hooks_) remove_(kv.second);
    hooks_.clear();
    for (auto& kv : helpers_)
        if (kv.second) { TerminateProcess(kv.second, 0); CloseHandle(kv.second); }
    helpers_.clear();
}
