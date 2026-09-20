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

#include "Loc.h"
#include <string>
#include <unordered_map>

// Standard MUI: the exe is a language-neutral module split by muirct; the
// resource loader picks the matching <culture>\DeGhoster.exe.mui by the user's
// UI language (en-US is the ultimate fallback). LoadStringW does it all.

namespace {
std::unordered_map<UINT, std::wstring> g_cache;
std::wstring g_override;   // forced UI language from DEGHOSTER_UILANG, if any
}

namespace loc {

void init()
{
    // Optional per-process UI-language override (e.g. DEGHOSTER_UILANG=ja-JP),
    // applied before any resource is loaded so the loader picks that .mui.
    wchar_t buf[64];
    DWORD n = GetEnvironmentVariableW(L"DEGHOSTER_UILANG", buf, ARRAYSIZE(buf));
    if (n > 0 && n < ARRAYSIZE(buf)) {
        g_override = buf;
        std::wstring list = g_override;
        list.push_back(L'\0');
        ULONG num = 0;
        SetProcessPreferredUILanguages(MUI_LANGUAGE_NAME, list.c_str(), &num);
    }
}

const wchar_t* t(UINT id)
{
    auto it = g_cache.find(id);
    if (it != g_cache.end()) return it->second.c_str();
    wchar_t buf[512];
    int n = LoadStringW(GetModuleHandleW(nullptr), id, buf, ARRAYSIZE(buf));
    return g_cache.emplace(id, std::wstring(buf, n)).first->second.c_str();
}

bool isRtl()
{
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = L"";
    if (!g_override.empty()) {
        lstrcpynW(name, g_override.c_str(), LOCALE_NAME_MAX_LENGTH);
    } else if (!LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT),
                                 name, LOCALE_NAME_MAX_LENGTH, 0)) {
        return false;
    }
    DWORD layout = 0;   // LOCALE_IREADINGLAYOUT: 1 = right-to-left
    GetLocaleInfoEx(name, LOCALE_IREADINGLAYOUT | LOCALE_RETURN_NUMBER,
                    (LPWSTR)&layout, sizeof(layout) / sizeof(wchar_t));
    return layout == 1;
}

} // namespace loc
