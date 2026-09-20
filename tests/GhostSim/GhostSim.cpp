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
//
// GhostSim - reproduces the DeGhoster problem for the integration tests.
//
// It creates a top-level window that matches every ghost criterion DeGhoster's
// GhostEngine::IsBlocker checks: class "Chrome_WidgetWin_1", visible,
// WS_EX_LAYERED (not WS_EX_TRANSPARENT), fully transparent via
// SetLayeredWindowAttributes(alpha 0, LWA_ALPHA), on-screen, not cloaked. It
// pumps messages (so DeGhoster's injected WH_GETMESSAGE hook can cloak it) and
// exits itself after a timeout so a crashed test never leaves it behind.
//
// Usage: GhostSim[64|32].exe --title <unique-title> [--timeout <seconds>]
//                            [--x <px>] [--y <px>]
// It prints "HWND=0x.... PID=...." so a harness can also read it from stdout;
// the test itself finds the window by class + unique title.

#include <windows.h>
#include <dwmapi.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#pragma comment(lib, "dwmapi.lib")

namespace {

constexpr wchar_t kClass[]     = L"Chrome_WidgetWin_1";   // the ghost (must match)
constexpr wchar_t kSinkClass[] = L"DeGhosterClickSink";  // the opaque click sink

// --sink mode state: an opaque window that counts the clicks it receives and
// reflects the count in its title ("<base> CLICKS=N") so a harness can prove
// whether clicks reach it through (or are eaten above) it.
bool           g_sink = false;
int            g_clicks = 0;
const wchar_t* g_baseTitle = L"";

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN:
        if (g_sink) {
            wchar_t t[160];
            wsprintfW(t, L"%s CLICKS=%d", g_baseTitle, ++g_clicks);
            SetWindowTextW(h, t);
            return 0;
        }
        break;
    case WM_PAINT:
        if (g_sink) {
            PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
            RECT rc; GetClientRect(h, &rc);
            HBRUSH b = CreateSolidBrush(RGB(30, 120, 200));
            FillRect(dc, &rc, b); DeleteObject(b);
            EndPaint(h, &ps);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wp == 1) { DestroyWindow(h); return 0; }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool argFlag(int argc, wchar_t** argv, const wchar_t* name)
{
    for (int i = 1; i < argc; ++i)
        if (_wcsicmp(argv[i], name) == 0) return true;
    return false;
}

long argLong(int argc, wchar_t** argv, const wchar_t* name, long dflt)
{
    for (int i = 1; i < argc - 1; ++i)
        if (_wcsicmp(argv[i], name) == 0) return wcstol(argv[i + 1], nullptr, 10);
    return dflt;
}

const wchar_t* argStr(int argc, wchar_t** argv, const wchar_t* name, const wchar_t* dflt)
{
    for (int i = 1; i < argc - 1; ++i)
        if (_wcsicmp(argv[i], name) == 0) return argv[i + 1];
    return dflt;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    const wchar_t* title = argStr(argc, argv, L"--title", L"DeGhoster-GhostSim");
    long timeoutSec      = argLong(argc, argv, L"--timeout", 60);
    long x               = argLong(argc, argv, L"--x", 100);
    long y               = argLong(argc, argv, L"--y", 100);
    long alpha           = argLong(argc, argv, L"--alpha", 0);   // ghost alpha (0..255)
    bool redir           = argFlag(argc, argv, L"--redir");      // opt OUT of NOREDIRECTIONBITMAP (then it's click-through)
    g_sink               = argFlag(argc, argv, L"--sink");
    g_baseTitle          = title;

    HINSTANCE inst = GetModuleHandleW(nullptr);
    const wchar_t* cls = g_sink ? kSinkClass : kClass;

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = inst;
    wc.lpszClassName = cls;
    if (!RegisterClassExW(&wc)) {
        wprintf(L"ERROR: RegisterClassEx failed (%lu)\n", GetLastError());
        return 2;
    }

    // Sink: an opaque top-level window. Ghost: layered (alpha set below) plus
    // WS_EX_NOREDIRECTIONBITMAP by default, to faithfully mimic a real Chromium
    // ghost: with no redirection bitmap the "alpha 0 lets clicks through" rule
    // doesn't apply, so the window stays invisible AND hit-testable (it eats
    // clicks). Pass --redir to drop it (then an alpha-0 ghost is click-through).
    DWORD exStyle = g_sink ? 0 : WS_EX_LAYERED;
    if (!redir && !g_sink) exStyle |= 0x00200000;   // WS_EX_NOREDIRECTIONBITMAP
    HWND h = CreateWindowExW(exStyle, cls, title, WS_POPUP,
                             (int)x, (int)y, 320, 200, nullptr, nullptr, inst, nullptr);
    if (!h) {
        wprintf(L"ERROR: CreateWindowEx failed (%lu)\n", GetLastError());
        return 2;
    }

    // Ghost only: near-invisible via LWA_ALPHA (alpha default 0).
    if (!g_sink) SetLayeredWindowAttributes(h, 0, (BYTE)alpha, LWA_ALPHA);
    ShowWindow(h, SW_SHOWNA);
    UpdateWindow(h);

    if (timeoutSec > 0) SetTimer(h, 1, (UINT)(timeoutSec * 1000), nullptr);

    wprintf(L"HWND=0x%p PID=%lu\n", (void*)h, GetCurrentProcessId());
    fflush(stdout);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
