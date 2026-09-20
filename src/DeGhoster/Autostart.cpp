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

#include "Autostart.h"

#include <windows.h>
#include <shellapi.h>
#include <string>

namespace {
constexpr wchar_t kRunKey[]   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"DeGhoster";

std::wstring exePath()
{
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::wstring();
    return std::wstring(buf, n);
}
}  // namespace

namespace autostart {

bool registerCurrentUser()
{
    std::wstring path = exePath();
    if (path.empty()) return false;
    std::wstring quoted = L"\"" + path + L"\"";

    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
        return false;
    LONG rc = RegSetValueExW(k, kValueName, 0, REG_SZ, (const BYTE*)quoted.c_str(),
                             (DWORD)((quoted.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
    return rc == ERROR_SUCCESS;
}

bool unregisterCurrentUser()
{
    HKEY k;
    LONG open = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k);
    if (open != ERROR_SUCCESS)
        return open == ERROR_FILE_NOT_FOUND;
    LONG rc = RegDeleteValueW(k, kValueName);
    RegCloseKey(k);
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

bool handleCommandLine(int* exitCode)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;

    bool handled = false;
    int code = 0;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--register-autostart") == 0 ||
            _wcsicmp(argv[i], L"/register-autostart") == 0) {
            code = registerCurrentUser() ? 0 : 1;
            handled = true;
            break;
        }
        if (_wcsicmp(argv[i], L"--unregister-autostart") == 0 ||
            _wcsicmp(argv[i], L"/unregister-autostart") == 0) {
            code = unregisterCurrentUser() ? 0 : 1;
            handled = true;
            break;
        }
    }
    LocalFree(argv);
    if (handled && exitCode) *exitCode = code;
    return handled;
}

}  // namespace autostart
