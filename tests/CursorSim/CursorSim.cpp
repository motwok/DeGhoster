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

// Remote session simulator for the cursor overlay tests: a window of AnyDesk's
// session class ("ad_win#<n>") that sets its own cursors the way AnyDesk does,
// created with CreateIconIndirect. A band at the top uses the system arrow, like
// AnyDesk's own title bar and tabs. Clicks are counted in the title.
//
// With --rdp it plays a program hosting the RDP control instead: the top-level
// window (class --class) holds a chain of child windows below the band, by
// default UIMainClass > UIContainerClass > IHWindowClass (plus an
// OPContainerClass next to the last one), and the innermost child sets the
// custom cursors, like the control's "Input Capture Window".
//
//   --kind alpha|mask|mono|switch|anim  cursor kind (switch: alpha <-> mask every
//                                       25 ms; anim: 20 frames, each its own
//                                       cursor handle and height, every 50 ms)
//   --class <name>                      window class (default ad_win#1)
//   --rdp <outer,...,inner>             RDP mode with this chain of child classes
//                                       ("default" for the real one)
//   --title --x --y --w --h --band --timeout

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr int kFrames = 20;

HCURSOR g_alpha = nullptr, g_mask = nullptr, g_mono = nullptr, g_current = nullptr;
HCURSOR g_anim[kFrames] = {};
int g_frame = 0;
bool g_switch = false, g_animate = false;
int g_band = 40, g_clicks = 0;
const wchar_t* g_title = L"";
HWND g_top = nullptr;     // the top-level window, whose title counts the clicks
HWND g_input = nullptr;   // the window that sets the custom cursors

// 16 x h (24 by default), colour with alpha: opaque red with a black frame,
// hotspot (2, 3).
HCURSOR MakeAlpha(int h = 24)
{
    const int w = 16;
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = w;
    bi.bV5Height = -h;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00; bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    auto* px = (DWORD*)bits;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            px[y * w + x] = (x == 0 || y == 0 || x == w - 1 || y == h - 1) ? 0xFF000000 : 0xFFE02020;
    std::vector<BYTE> zeros((size_t)2 * h, 0);
    HBITMAP mask = CreateBitmap(w, h, 1, 1, zeros.data());
    ICONINFO ii{ FALSE, 2, 3, mask, color };
    HCURSOR c = (HCURSOR)CreateIconIndirect(&ii);
    DeleteObject(color); DeleteObject(mask);
    return c;
}

// 16x24, colour without alpha: left half opaque blue, right half transparent,
// hotspot (0, 0).
HCURSOR MakeMask()
{
    const int w = 16, h = 24;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    auto* px = (DWORD*)bits;
    for (int i = 0; i < w * h; ++i) px[i] = (i % w) < 8 ? 0x002040F0 : 0;
    std::vector<BYTE> andBits((size_t)2 * h);
    for (int y = 0; y < h; ++y) { andBits[y * 2] = 0x00; andBits[y * 2 + 1] = 0xFF; }
    HBITMAP mask = CreateBitmap(w, h, 1, 1, andBits.data());
    ICONINFO ii{ FALSE, 0, 0, mask, color };
    HCURSOR c = (HCURSOR)CreateIconIndirect(&ii);
    DeleteObject(color); DeleteObject(mask);
    return c;
}

// 32x32 monochrome: a black square plus an inverting vertical bar (an I-beam
// that is invisible on dark backgrounds without the outline), hotspot (16, 16).
HCURSOR MakeMono()
{
    const int w = 32, h = 32, stride = 4;
    std::vector<BYTE> bits((size_t)stride * h * 2);
    BYTE* andRows = bits.data();
    BYTE* xorRows = bits.data() + stride * h;
    std::fill(andRows, andRows + stride * h, (BYTE)0xFF);
    auto set = [&](BYTE* rows, int x, int y, bool on) {
        BYTE& b = rows[y * stride + x / 8];
        const BYTE m = (BYTE)(0x80 >> (x % 8));
        b = on ? (BYTE)(b | m) : (BYTE)(b & ~m);
    };
    for (int y = 4; y < 8; ++y)
        for (int x = 12; x < 20; ++x) set(andRows, x, y, false);   // black: AND 0, XOR 0
    for (int y = 10; y < 26; ++y) set(xorRows, 16, y, true);       // inverting: AND 1, XOR 1
    HBITMAP mask = CreateBitmap(w, h * 2, 1, 1, bits.data());
    ICONINFO ii{ FALSE, 16, 16, mask, nullptr };
    HCURSOR c = (HCURSOR)CreateIconIndirect(&ii);
    DeleteObject(mask);
    return c;
}

// Over the part that shows the custom cursors: below the band of the session
// window, or anywhere in the RDP input window.
bool OverInput(HWND h)
{
    POINT pt; GetCursorPos(&pt);
    ScreenToClient(h, &pt);
    RECT rc; GetClientRect(h, &rc);
    return PtInRect(&rc, pt) && (h != g_top || pt.y >= g_band);
}

void CountClick()
{
    wchar_t t[200];
    wsprintfW(t, L"%s CLICKS=%d", g_title, ++g_clicks);
    SetWindowTextW(g_top, t);
}

// The chain's windows in RDP mode. Only the innermost one sets custom cursors;
// the others are covered by it and never see the mouse.
LRESULT CALLBACK ChildProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (h == g_input) {
        switch (msg) {
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) { SetCursor(g_current); return TRUE; }
            break;
        case WM_LBUTTONDOWN:
            CountClick();
            return 0;
        }
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HBRUSH b = CreateSolidBrush(RGB(40, 40, 44));
        FillRect(dc, &rc, b); DeleteObject(b);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(h == g_input && OverInput(h) ? g_current : LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        if (h == g_input) CountClick();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HBRUSH b = CreateSolidBrush(RGB(40, 40, 44));
        FillRect(dc, &rc, b); DeleteObject(b);
        RECT band{ 0, 0, rc.right, g_band };
        HBRUSH bb = CreateSolidBrush(RGB(200, 60, 40));
        FillRect(dc, &band, bb); DeleteObject(bb);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_TIMER:
        if (wp == 1) { DestroyWindow(h); return 0; }
        if (wp == 2) {
            if (g_animate) g_current = g_anim[g_frame = (g_frame + 1) % kFrames];
            else g_current = g_current == g_alpha ? g_mask : g_alpha;
            if (OverInput(g_input)) SetCursor(g_current);
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
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
    // Physical pixels, like DeGhoster: the tests compare sizes in pixels.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    g_title = argStr(argc, argv, L"--title", L"DeGhoster-CursorSim");
    const wchar_t* kind = argStr(argc, argv, L"--kind", L"alpha");
    const wchar_t* cls  = argStr(argc, argv, L"--class", L"ad_win#1");
    long timeoutSec = argLong(argc, argv, L"--timeout", 60);
    long x = argLong(argc, argv, L"--x", 200), y = argLong(argc, argv, L"--y", 200);
    long w = argLong(argc, argv, L"--w", 480), hgt = argLong(argc, argv, L"--h", 360);
    g_band = (int)argLong(argc, argv, L"--band", 40);

    const wchar_t* rdp  = argStr(argc, argv, L"--rdp", nullptr);

    g_alpha = MakeAlpha();
    g_mask = MakeMask();
    g_mono = MakeMono();
    g_switch = _wcsicmp(kind, L"switch") == 0;
    g_animate = _wcsicmp(kind, L"anim") == 0;
    for (int i = 0; g_animate && i < kFrames; ++i) g_anim[i] = MakeAlpha(20 + i);
    g_current = _wcsicmp(kind, L"mask") == 0 ? g_mask
              : _wcsicmp(kind, L"mono") == 0 ? g_mono
              : g_animate ? g_anim[0] : g_alpha;

    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = cls;
    if (!RegisterClassExW(&wc)) {
        wprintf(L"ERROR: RegisterClassEx failed (%lu)\n", GetLastError());
        return 2;
    }
    HWND h = CreateWindowExW(WS_EX_TOPMOST, cls, g_title, WS_POPUP,
                             (int)x, (int)y, (int)w, (int)hgt, nullptr, nullptr, inst, nullptr);
    if (!h) {
        wprintf(L"ERROR: CreateWindowEx failed (%lu)\n", GetLastError());
        return 2;
    }
    g_top = g_input = h;

    if (rdp) {
        // The chain below the band, each child filling its parent.
        std::wstring chain = _wcsicmp(rdp, L"default") == 0
            ? L"UIMainClass,UIContainerClass,IHWindowClass" : rdp;
        std::vector<std::wstring> classes;
        for (size_t start = 0, end; start <= chain.size(); start = end + 1) {
            end = chain.find(L',', start);
            if (end == std::wstring::npos) end = chain.size();
            classes.push_back(chain.substr(start, end - start));
        }
        HWND parent = h;
        int cx = (int)w, cy = (int)hgt - g_band, top = g_band;
        for (size_t i = 0; i < classes.size(); ++i) {
            const bool last = i + 1 == classes.size();
            WNDCLASSEXW cc{ sizeof(cc) };
            cc.lpfnWndProc = ChildProc;
            cc.hInstance = inst;
            cc.lpszClassName = classes[i].c_str();
            RegisterClassExW(&cc);   // may already exist from an earlier link
            if (last) {
                // The painter sits next to the input window, under it.
                WNDCLASSEXW oc = cc;
                oc.lpszClassName = L"OPContainerClass";
                RegisterClassExW(&oc);
                CreateWindowExW(0, oc.lpszClassName, L"Output Painter Window", WS_CHILD | WS_VISIBLE,
                                0, top, cx, cy, parent, nullptr, inst, nullptr);
            }
            HWND c = CreateWindowExW(0, cc.lpszClassName, last ? L"Input Capture Window" : L"",
                                     WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, top, cx, cy,
                                     parent, nullptr, inst, nullptr);
            if (!c) {
                wprintf(L"ERROR: child %s failed (%lu)\n", classes[i].c_str(), GetLastError());
                return 2;
            }
            SetWindowPos(c, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            parent = c;
            top = 0;
        }
        g_input = parent;
    }

    ShowWindow(h, SW_SHOWNA);
    UpdateWindow(h);
    if (timeoutSec > 0) SetTimer(h, 1, (UINT)(timeoutSec * 1000), nullptr);
    if (g_switch || g_animate) SetTimer(h, 2, g_animate ? 50 : 25, nullptr);

    wprintf(L"HWND=0x%p PID=%lu\n", (void*)h, GetCurrentProcessId());
    fflush(stdout);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    DestroyCursor(g_alpha); DestroyCursor(g_mask); DestroyCursor(g_mono);
    for (HCURSOR c : g_anim) if (c) DestroyCursor(c);
    return 0;
}
