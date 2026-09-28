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
#include <appmodel.h>
#include <tlhelp32.h>
#include <cwctype>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

// Position of `needle` in `hay`, ignoring ASCII case; npos if absent.
size_t FindNoCase(const std::wstring& hay, const std::wstring& needle, size_t from = 0)
{
    if (needle.empty() || hay.size() < needle.size()) return std::wstring::npos;
    for (size_t i = from; i + needle.size() <= hay.size(); ++i)
        if (_wcsnicmp(hay.c_str() + i, needle.c_str(), needle.size()) == 0) return i;
    return std::wstring::npos;
}

// "app-1.0.9187" or "app-2.3.0-beta1": the version folder Squirrel installs into.
bool IsSquirrelVersionDir(const std::wstring& dir)
{
    if (dir.size() < 5 || _wcsnicmp(dir.c_str(), L"app-", 4) != 0) return false;
    size_t i = 4;
    bool digit = false;
    for (; i < dir.size() && dir[i] != L'-'; ++i) {
        if (iswdigit(dir[i])) { digit = true; continue; }
        if (dir[i] != L'.' || !digit || i + 1 >= dir.size() || !iswdigit(dir[i + 1])) return false;
    }
    if (!digit) return false;
    if (i == dir.size()) return true;
    if (++i == dir.size()) return false;   // a bare trailing '-'
    for (; i < dir.size(); ++i)
        if (!iswalnum(dir[i]) && dir[i] != L'.') return false;
    return true;
}

// Package family name and full name of a packaged process; false for any other.
bool PackageNames(HANDLE process, std::wstring& family, std::wstring& full)
{
    wchar_t fam[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1];
    wchar_t ful[PACKAGE_FULL_NAME_MAX_LENGTH + 1];
    UINT32 nf = ARRAYSIZE(fam), nu = ARRAYSIZE(ful);
    if (GetPackageFamilyName(process, &nf, fam) != ERROR_SUCCESS) return false;
    if (GetPackageFullName(process, &nu, ful) != ERROR_SUCCESS) return false;
    family = fam;
    full = ful;
    return true;
}

} // namespace

namespace proc {

std::wstring ProgramKeyFromPath(const std::wstring& path)
{
    if (path.empty()) return L"?";

    // Packaged app: the folder is the package full name, whose fields are
    // separated by '_' (a package name cannot contain one). Keeping only the
    // name and the publisher id gives the family name, which an update keeps.
    const std::wstring apps = L"\\WindowsApps\\";
    const size_t a = FindNoCase(path, apps);
    if (a != std::wstring::npos) {
        const size_t s = a + apps.size(), e = path.find(L'\\', s);
        if (e != std::wstring::npos) {
            std::vector<std::wstring> f;
            for (size_t p = s;;) {
                const size_t u = path.find(L'_', p);
                if (u == std::wstring::npos || u > e) { f.push_back(path.substr(p, e - p)); break; }
                f.push_back(path.substr(p, u - p));
                p = u + 1;
            }
            if (f.size() == 5 && !f[0].empty() && !f[1].empty() && !f[4].empty())
                return f[0] + L"_" + f[4] + path.substr(e);
        }
    }

    // Squirrel: <App>\app-<version>\<exe>; every update installs a new folder.
    const size_t file = path.find_last_of(L'\\');
    if (file != std::wstring::npos && file > 0) {
        const size_t dir = path.find_last_of(L'\\', file - 1);
        const size_t begin = dir == std::wstring::npos ? 0 : dir + 1;
        if (IsSquirrelVersionDir(path.substr(begin, file - begin)))
            return path.substr(0, begin) + L"app" + path.substr(file);
    }
    return path;
}

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

void ResolveHostExe(DWORD pid, std::wstring& exeName, std::wstring& exePath, std::wstring* program)
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
    std::wstring family, full;
    bool packaged = false;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, host);
    if (p) {
        wchar_t buf[1024];
        DWORD sz = 1024;
        if (QueryFullProcessImageNameW(p, 0, buf, &sz)) exePath = buf;
        packaged = program && PackageNames(p, family, full);
        CloseHandle(p);
    }
    if (!exePath.empty()) {
        size_t i = exePath.find_last_of(L'\\');
        exeName = (i == std::wstring::npos) ? exePath : exePath.substr(i + 1);
    } else {
        exeName = L"?";
    }

    if (program) {
        // Ask Windows for the package identity rather than trusting the folder
        // layout; the path is only the fallback (and what old opt-outs migrate from).
        *program = ProgramKeyFromPath(exePath);
        if (packaged && !exePath.empty()) {
            const size_t at = FindNoCase(exePath, L"\\" + full + L"\\");
            *program = family + (at != std::wstring::npos ? exePath.substr(at + full.size() + 1)
                                                           : L"\\" + exeName);
        }
    }
}

bool HelperRunning()
{
    const std::wstring dir = ExeDir();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    bool found = false;
    PROCESSENTRY32W e{ sizeof(e) };
    for (BOOL ok = Process32FirstW(snap, &e); ok && !found; ok = Process32NextW(snap, &e)) {
        if (_wcsicmp(e.szExeFile, L"DeGhoster.Helper32.exe") != 0 &&
            _wcsicmp(e.szExeFile, L"DeGhoster.Helper64.exe") != 0)
            continue;
        // Match on the image path, not just the name: another user's helper is none
        // of our business, and a process we cannot open cannot be one of ours.
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.th32ProcessID);
        if (!p) continue;
        wchar_t buf[1024];
        DWORD sz = 1024;
        if (QueryFullProcessImageNameW(p, 0, buf, &sz)) {
            const std::wstring path = buf;
            if (path.size() > dir.size() &&
                _wcsnicmp(path.c_str(), dir.c_str(), dir.size()) == 0)
                found = true;
        }
        CloseHandle(p);
    }
    CloseHandle(snap);
    return found;
}

// The raw title, empty when the window has none. Deliberately NOT localized: it
// is data, not UI text; the UI substitutes IDS_UNTITLED when drawing.
std::wstring WindowTitle(HWND h)
{
    wchar_t t[256] = L"";
    GetWindowTextW(h, t, 256);
    return std::wstring(t);
}

} // namespace proc
