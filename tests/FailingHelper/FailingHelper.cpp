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

// A stand-in for an injector helper whose hook cannot be installed (an elevated
// target, say): the integration tests copy it over DeGhoster.Helper64.exe. It
// logs every launch - "<tick> <thread id> <host hwnd> <host pid>", the helper's
// arguments - to FailingHelper.log next to itself and exits the way the real
// helper does when SetWindowsHookEx refuses.
#include <windows.h>
#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR args, int)
{
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return 2;
    std::wstring log(path, n);
    log = log.substr(0, log.find_last_of(L'\\') + 1) + L"FailingHelper.log";

    HANDLE f = CreateFileW(log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        std::string line = std::to_string(GetTickCount64()) + " ";
        for (const wchar_t* p = args; *p; ++p) line += (char)*p;   // digits and spaces only
        line += "\r\n";
        DWORD written = 0;
        WriteFile(f, line.data(), (DWORD)line.size(), &written, nullptr);
        CloseHandle(f);
    }
    return 4;   // DeGhoster.Helper's exit code for a refused hook
}
