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

#include "ProcessUtil.h"
#include <tlhelp32.h>
#include <unordered_map>
#include <utility>

namespace proc {

std::wstring ExeDir()
{
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    size_t i = s.find_last_of(L'\\');
    return (i == std::wstring::npos) ? L"" : s.substr(0, i + 1);
}

bool IsWow64(DWORD pid)
{
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return false;
    BOOL wow = FALSE;
    IsWow64Process(p, &wow);
    CloseHandle(p);
    return wow != FALSE;
}

void ResolveHostExe(DWORD pid, std::wstring& exeName, std::wstring& exePath)
{
    DWORD host = pid;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        std::unordered_map<DWORD, std::pair<DWORD, std::wstring>> tree;
        PROCESSENTRY32W e{ sizeof(e) };
        for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e)) {
            std::wstring n = e.szExeFile;
            if (n.size() > 4 && _wcsicmp(n.c_str() + n.size() - 4, L".exe") == 0)
                n = n.substr(0, n.size() - 4);
            tree[e.th32ProcessID] = { e.th32ParentProcessID, n };
        }
        CloseHandle(snap);
        DWORD cur = pid;
        for (int guard = 0; guard < 12; ++guard) {
            auto it = tree.find(cur);
            if (it == tree.end() || _wcsicmp(it->second.second.c_str(), L"msedgewebview2") != 0) break;
            if (tree.find(it->second.first) == tree.end()) break;
            cur = it->second.first;
        }
        host = cur;
    }

    exePath.clear();
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, host);
    if (p) {
        wchar_t buf[1024];
        DWORD sz = 1024;
        if (QueryFullProcessImageNameW(p, 0, buf, &sz)) exePath = buf;
        CloseHandle(p);
    }
    if (!exePath.empty()) {
        size_t i = exePath.find_last_of(L'\\');
        exeName = (i == std::wstring::npos) ? exePath : exePath.substr(i + 1);
    } else {
        exeName = L"?";
    }
}

// The raw title, empty when the window has none. Deliberately NOT localized:
// FixInfo::disableKey() embeds this string in the registry opt-out key, so a
// localized placeholder would orphan the opt-outs for untitled ghosts the moment
// the UI language changed. The UI substitutes IDS_UNTITLED when drawing.
std::wstring WindowTitle(HWND h)
{
    wchar_t t[256] = L"";
    GetWindowTextW(h, t, 256);
    return std::wstring(t);
}

} // namespace proc
