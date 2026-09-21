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

#include "Settings.h"
#include <windows.h>
#include <string>
#include <vector>

namespace {
// Registry root under HKCU. Overridable via DEGHOSTER_SETTINGS_ROOT so tests can
// use a throwaway key instead of the real "Software\DeGhoster" (same env-override
// approach as DEGHOSTER_UILANG). Default is the production location.
std::wstring rootKey()
{
    wchar_t buf[256];
    DWORD n = GetEnvironmentVariableW(L"DEGHOSTER_SETTINGS_ROOT", buf, ARRAYSIZE(buf));
    if (n > 0 && n < ARRAYSIZE(buf)) return buf;
    return L"Software\\DeGhoster";
}
std::wstring disabledKey() { return rootKey() + L"\\Disabled"; }
}

void Settings::load()
{
    globalEnabled_ = true;   // reset to defaults so a reload replaces, not merges
    disabled_.clear();

    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, rootKey().c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS) {
        DWORD v = 1, sz = sizeof(v);
        if (RegQueryValueExW(k, L"GlobalEnabled", nullptr, nullptr, (LPBYTE)&v, &sz) == ERROR_SUCCESS)
            globalEnabled_ = v != 0;
        RegCloseKey(k);
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, disabledKey().c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS) {
        // Value names (exePath|title) can be long. Size the buffer to the key's
        // actual maximum so a single long name can't return ERROR_MORE_DATA and
        // cut the enumeration short, silently dropping every later opt-out.
        DWORD maxLen = 0;
        RegQueryInfoKeyW(k, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr, &maxLen, nullptr, nullptr, nullptr);
        std::vector<wchar_t> name(maxLen + 1u);
        for (DWORD i = 0;; ++i) {
            DWORD len = (DWORD)name.size();
            LONG r = RegEnumValueW(k, i, name.data(), &len, nullptr, nullptr, nullptr, nullptr);
            if (r == ERROR_NO_MORE_ITEMS) break;
            if (r == ERROR_SUCCESS && name[0]) disabled_.insert(name.data());
            // any other error for this index: skip it, keep enumerating
        }
        RegCloseKey(k);
    }
}

void Settings::setGlobalEnabled(bool on)
{
    globalEnabled_ = on;
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, rootKey().c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        DWORD v = on ? 1 : 0;
        RegSetValueExW(k, L"GlobalEnabled", 0, REG_DWORD, (LPBYTE)&v, sizeof(v));
        RegCloseKey(k);
    }
}

bool Settings::isManaged(const std::wstring& key) const
{
    return disabled_.find(key) == disabled_.end();
}

void Settings::setManaged(const std::wstring& key, bool managed)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, disabledKey().c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        if (managed) {
            LONG r = RegDeleteValueW(k, key.c_str());
            // Mirror the registry: only forget the opt-out if the delete stuck (or
            // the value was already gone), so the in-memory set can't drift from it.
            if (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND) disabled_.erase(key);
        } else {
            std::wstring title = key.substr(key.find(L'|') + 1);  // readable value
            LONG r = RegSetValueExW(k, key.c_str(), 0, REG_SZ,
                                    (LPBYTE)title.c_str(), (DWORD)((title.size() + 1) * sizeof(wchar_t)));
            if (r == ERROR_SUCCESS) disabled_.insert(key);
        }
        RegCloseKey(k);
    }
}
