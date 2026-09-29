// Copyright (c) Emmo Emminghaus mo2000 at mo2000 dot de
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <windows.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <cstdio>
#include <climits>
#include <tlhelp32.h>

#include "Settings.h"
#include "Theme.h"
#include "Hook.h"
#include "ProcessUtil.h"
#include "HookInjector.h"
#include "CursorImage.h"
#include "AutoZoom.h"
#include "CursorOverlay.h"
#include "Controls.h"
#include "SettingsWindow.h"
#include "InfoWindow.h"
#include "GhostEngine.h"
#include "Gfx.h"
#include "Loc.h"

#include <commctrl.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")

static int g_failures = 0;

// Returns the condition so a failed precondition can skip what follows.
static bool Check(bool cond, const char* what)
{
    std::printf(cond ? "  [ok]   %s\n" : "  [FAIL] %s\n", what);
    if (!cond) ++g_failures;
    return cond;
}

static void SettingsTests()
{
    std::wstring root = L"Software\\DeGhoster_Test_" +
                        std::to_wstring(GetCurrentProcessId()) + L"_" +
                        std::to_wstring(GetTickCount());
    SetEnvironmentVariableW(L"DEGHOSTER_SETTINGS_ROOT", root.c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str());

    std::printf("Settings tests (root=HKCU\\%ls)\n", root.c_str());

    { Settings s; s.load(); Check(s.globalEnabled(), "GlobalEnabled defaults to true"); }
    {
        Settings s; s.load(); s.setGlobalEnabled(false);
        Check(!s.globalEnabled(), "setGlobalEnabled(false) takes effect");
        Settings s2; s2.load();
        Check(!s2.globalEnabled(), "GlobalEnabled=false persists across reload");
        s2.setGlobalEnabled(true);
        Settings s3; s3.load();
        Check(s3.globalEnabled(), "GlobalEnabled=true persists across reload");
    }
    {
        const std::wstring key = L"C:\\Apps\\Ghosty.exe";
        Settings s; s.load();
        Check(s.isManaged(key), "unknown program is managed by default");
        s.setManaged(key, false);
        Check(!s.isManaged(key), "setManaged(false) marks the program unmanaged");
        Settings s2; s2.load();
        Check(!s2.isManaged(key), "unmanaged state persists across reload");
        s2.setManaged(key, true);
        Check(s2.isManaged(key), "setManaged(true) re-manages the program");
        Settings s3; s3.load();
        Check(s3.isManaged(key), "re-managed state persists across reload");
    }
    {
        // A value name longer than the old fixed 1024-wchar buffer used to make
        // RegEnumValueW return ERROR_MORE_DATA and cut the enumeration short,
        // silently dropping this opt-out (and any after it) on reload.
        const std::wstring longKey = L"C:\\Apps\\" + std::wstring(2000, L'x') + L"\\Ghosty.exe";
        const std::wstring shortKey = L"C:\\Apps\\Other.exe";
        Settings s; s.load();
        s.setManaged(longKey, false);
        s.setManaged(shortKey, false);
        Settings s2; s2.load();
        Check(!s2.isManaged(longKey), "long (>1024 char) opt-out survives reload");
        Check(!s2.isManaged(shortKey), "opt-out after a long one is not dropped");
    }
    {
        // Opt-outs from before the per-program switch ("<exe path>|<title>") turn
        // into the program's key, in memory and in the registry.
        const std::wstring dis = root + L"\\Disabled";
        HKEY k;
        if (Check(RegCreateKeyExW(HKEY_CURRENT_USER, dis.c_str(), 0, nullptr, 0, KEY_WRITE,
                                  nullptr, &k, nullptr) == ERROR_SUCCESS, "open Disabled for writing")) {
            const wchar_t* old[] = {
                L"C:\\Legacy\\Old.exe|Some title",
                L"C:\\Program Files\\WindowsApps\\Pkg.Name_1.2.3.4_x64__pub123\\bin\\App.exe|T",
                L"|untitled from an unreadable process",
            };
            for (const wchar_t* n : old)
                RegSetValueExW(k, n, 0, REG_SZ, (const BYTE*)L"t", 2 * sizeof(wchar_t));
            RegCloseKey(k);
        }
        Settings s; s.load();
        Check(!s.isManaged(L"C:\\Legacy\\Old.exe"), "an old opt-out switches its program off");
        Check(!s.isManaged(L"Pkg.Name_pub123\\bin\\App.exe"), "an old Store-app opt-out keys on the package family");
        Check(!s.isManaged(L"?"), "an old opt-out without a path becomes the unknown program");
        bool anyOld = false, hasNew = false;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, dis.c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS) {
            wchar_t n[4096];
            for (DWORD i = 0;; ++i) {
                DWORD len = ARRAYSIZE(n);
                if (RegEnumValueW(k, i, n, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
                if (wcschr(n, L'|')) anyOld = true;
                if (lstrcmpW(n, L"C:\\Legacy\\Old.exe") == 0) hasNew = true;
            }
            RegCloseKey(k);
        }
        Check(!anyOld && hasNew, "the registry holds the migrated keys and no old ones");
        Settings s2; s2.load();
        Check(!s2.isManaged(L"C:\\Legacy\\Old.exe"), "a migrated opt-out survives the next reload");
    }

    {
        // First start: the zoom comes from the monitor scaling and is written
        // back at once, so a later change of the scaling cannot move it.
        HKEY k; DWORD v = 0, sz = sizeof(v);
        bool had = RegOpenKeyExW(HKEY_CURRENT_USER, root.c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS &&
                   RegQueryValueExW(k, L"CursorOverlayZoom", nullptr, nullptr, (LPBYTE)&v, &sz) == ERROR_SUCCESS;
        if (had) RegCloseKey(k);
        Check(had && (int)v == Settings::ZoomForDpi(GetDpiForSystem()),
              "first load stores the monitor scaling as CursorOverlayZoom");

        Settings s; s.load();
        s.setCursorOverlayZoom(333);
        Check(s.cursorOverlayZoom() == 330, "setCursorOverlayZoom snaps to the 10 % grid");
        Settings s2; s2.load();
        Check(s2.cursorOverlayZoom() == 330, "CursorOverlayZoom persists across reload");
        s2.setCursorOverlayZoom(5000);
        Settings s3; s3.load();
        Check(s3.cursorOverlayZoom() == Settings::kZoomMax, "an out-of-range zoom is clamped");
    }
    {
        // Only a REG_DWORD counts. An empty string is two zero bytes and used to be
        // read as 0, which switched DeGhoster off.
        HKEY k;
        if (Check(RegCreateKeyExW(HKEY_CURRENT_USER, root.c_str(), 0, nullptr, 0, KEY_WRITE,
                                  nullptr, &k, nullptr) == ERROR_SUCCESS, "open the test root for writing")) {
            const wchar_t empty[] = L"";
            RegSetValueExW(k, L"GlobalEnabled", 0, REG_SZ, (const BYTE*)empty, sizeof(empty));
            const BYTE zero = 0;
            RegSetValueExW(k, L"CursorOverlayAuto", 0, REG_BINARY, &zero, 1);
            RegCloseKey(k);
            Settings s; s.load();
            Check(s.globalEnabled(), "a REG_SZ GlobalEnabled is ignored, not read as 0");
            Check(s.cursorOverlayAuto(), "a REG_BINARY CursorOverlayAuto is ignored, not read as 0");
        }
    }

    RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str());
}

static void ZoomTests()
{
    std::printf("Zoom tests\n");
    Check(Settings::ClampZoom(95) == 100, "ClampZoom(95) = 100");
    Check(Settings::ClampZoom(104) == 100, "ClampZoom(104) = 100");
    Check(Settings::ClampZoom(105) == 110, "ClampZoom(105) = 110");
    Check(Settings::ClampZoom(601) == 600, "ClampZoom(601) = 600");
    Check(Settings::ClampZoom(-50) == 100, "ClampZoom(-50) = 100");
    Check(Settings::ClampZoom(INT_MAX) == 600, "ClampZoom(INT_MAX) = 600, no overflow");
    Check(Settings::ClampZoom(INT_MAX - 1) == 600, "ClampZoom(INT_MAX - 1) = 600, no overflow");
    Check(Settings::ClampZoom(INT_MIN) == 100, "ClampZoom(INT_MIN) = 100");
    Check(Settings::ZoomForDpi(96) == 100, "ZoomForDpi(96) = 100");
    Check(Settings::ZoomForDpi(144) == 150, "ZoomForDpi(144) = 150");
    Check(Settings::ZoomForDpi(240) == 250, "ZoomForDpi(240) = 250 (the POC machine)");
    Check(Settings::ZoomForDpi(120) == 130, "ZoomForDpi(120) = 130 (125 % onto the grid)");
    Check(Settings::ZoomForDpi(960) == 600, "ZoomForDpi(960) is clamped to 600");
}

static void AutoZoomTests()
{
    std::printf("AutoZoom tests\n");
    AutoZoom az;
    Check(az.zoom(40) == 0, "nothing seen: no automatic zoom");

    // Arrow 16 px tall, I-beam 20 px; the local arrow is 40 px.
    const uint64_t arrow = 1, ibeam = 2;
    ULONGLONG t = 1000;
    az.observe(arrow, 16, t);
    Check(az.reference() == arrow && az.zoom(40) == 250, "the first picture is the reference: 40/16 = 250 %");
    az.observe(ibeam, 20, t += 500);            // arrow 500 ms
    az.observe(arrow, 16, t += 300);            // I-beam 300 ms
    Check(az.reference() == arrow, "a picture shown less long does not take over");
    az.observe(ibeam, 20, t += 100000);         // arrow resting: credited 2000 ms only
    az.observe(ibeam, 20, t += 1000);           // I-beam 1000 ms
    Check(az.reference() == arrow, "resting time is capped, so the arrow keeps the lead");
    for (int i = 0; i < 5; ++i) {               // then text editing
        az.observe(arrow, 16, t += 2000);       // I-beam 2000 ms
        az.observe(ibeam, 20, t += 100);        // arrow 100 ms
    }
    Check(az.reference() == ibeam && az.zoom(40) == 200, "a picture shown clearly longer takes over: 40/20 = 200 %");

    // Only the last 10 s count: back to the arrow, it takes over within them.
    const ULONGLONG back = t;
    for (int i = 0; i < 5 && az.reference() != arrow; ++i) {
        az.observe(arrow, 16, t += 100);        // I-beam 100 ms
        az.observe(ibeam, 20, t += 2000);       // arrow 2000 ms
    }
    Check(az.reference() == arrow && t - back <= AutoZoom::kWindowMs,
          "older time drops out, so the arrow is the reference again within 10 s");
    Check(az.zoom(0) == 0, "no target height: no automatic zoom");

    AutoZoom tiny;
    tiny.observe(7, 2, 0);
    Check(tiny.zoom(40) == Settings::kZoomMax, "the automatic zoom is clamped to the maximum");
    AutoZoom empty;
    empty.observe(8, 0, 0);
    Check(empty.zoom(40) == 0, "a picture with nothing visible cannot be the reference");
    empty.observe(9, 40, 10);
    Check(empty.reference() == 9 && empty.zoom(40) == 100, "the next visible picture becomes the reference");
}

static void AnyDeskClassTests()
{
    std::printf("AnyDesk class tests\n");
    Check(CursorOverlay::IsAnyDeskClass(L"ad_win"), "ad_win matches");
    Check(CursorOverlay::IsAnyDeskClass(L"ad_win#2"), "ad_win#2 matches");
    Check(CursorOverlay::IsAnyDeskClass(L"ad_win#205\n"), "ad_win#205 with a trailing newline matches");
    Check(!CursorOverlay::IsAnyDeskClass(L"ad_window"), "ad_window does not match");
    Check(!CursorOverlay::IsAnyDeskClass(L"AD_WIN"), "the match is case-sensitive");
    Check(!CursorOverlay::IsAnyDeskClass(L"xad_win"), "a prefix does not match");
    Check(!CursorOverlay::IsAnyDeskClass(L"gui.tabbar_panel#205"), "AnyDesk's panel class does not match");
    Check(!CursorOverlay::IsAnyDeskClass(L""), "empty does not match");
    Check(!CursorOverlay::IsAnyDeskClass(nullptr), "null does not match");
}

static HCURSOR MakeCursor(int w, int h, const uint32_t* argb, const BYTE* andBits, POINT hot, bool mono)
{
    HBITMAP color = nullptr;
    if (!mono) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HDC dc = GetDC(nullptr);
        color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, dc);
        memcpy(bits, argb, (size_t)w * h * 4);
    }
    HBITMAP mask = CreateBitmap(w, mono ? h * 2 : h, 1, 1, andBits);
    ICONINFO ii{ FALSE, (DWORD)hot.x, (DWORD)hot.y, mask, color };
    HCURSOR c = (HCURSOR)CreateIconIndirect(&ii);
    if (color) DeleteObject(color);
    DeleteObject(mask);
    return c;
}

static void CursorImageTests()
{
    using namespace cursorimg;
    std::printf("CursorImage tests\n");
    std::vector<uint8_t> inv;

    {   // colour with alpha: premultiplied, nothing inverting
        const uint32_t color[2] = { 0x80FF0000u, 0x00000000u };
        const uint32_t mask[2] = { 0, 0 };
        Image img;
        Decode(2, 1, color, mask, POINT{ 1, 0 }, img, inv);
        Check(img.w == 2 && img.h == 1 && img.hot.x == 1, "Decode keeps size and hotspot");
        Check(img.px[0] == 0x80800000u, "alpha colour is premultiplied");
        Check(img.px[1] == 0 && inv[0] == 0 && inv[1] == 0, "alpha 0 stays transparent, nothing inverts");
    }
    {   // colour without alpha: the AND mask decides
        const uint32_t color[3] = { 0x00112233u, 0x00FFFFFFu, 0x00000000u };
        const uint32_t mask[3] = { 0x000000u, 0xFFFFFFu, 0xFFFFFFu };
        Image img;
        Decode(3, 1, color, mask, POINT{ 0, 0 }, img, inv);
        Check(img.px[0] == 0xFF112233u, "AND 0 is opaque colour");
        Check(inv[1] == 1 && img.px[1] == 0, "AND 1 with a colour inverts");
        Check(inv[2] == 0 && img.px[2] == 0, "AND 1 with black is transparent");
    }
    {   // monochrome: AND rows above XOR rows
        const uint32_t mask[8] = { 0, 0, 0xFFFFFFu, 0xFFFFFFu,          // AND
                                   0, 0xFFFFFFu, 0xFFFFFFu, 0 };        // XOR
        Image img;
        Decode(2, 2, nullptr, mask, POINT{ 0, 0 }, img, inv);
        Check(img.w == 2 && img.h == 2, "mono decode halves the mask height");
        Check(img.px[0] == 0xFF000000u, "AND 0 / XOR 0 is black");
        Check(img.px[1] == 0xFFFFFFFFu, "AND 0 / XOR 1 is white");
        Check(inv[2] == 1 && img.px[2] == 0, "AND 1 / XOR 1 inverts");
        Check(inv[3] == 0 && img.px[3] == 0, "AND 1 / XOR 0 is transparent");
    }
    {   // outline around a single inverting pixel
        Image img; img.w = 1; img.h = 1; img.hot = POINT{ 0, 0 }; img.px = { 0 };
        std::vector<uint8_t> one{ 1 };
        AddOutline(img, one, 1);
        Check(img.w == 3 && img.h == 3, "the outline grows the canvas by the radius");
        Check(img.hot.x == 1 && img.hot.y == 1, "the hotspot moves with the canvas");
        Check(img.at(1, 1) == 0xFF000000u, "the inverting pixel becomes opaque black");
        Check(img.at(0, 1) == 0xFFFFFFFFu && img.at(2, 1) == 0xFFFFFFFFu &&
              img.at(1, 0) == 0xFFFFFFFFu && img.at(1, 2) == 0xFFFFFFFFu, "direct neighbours become white");
        Check(img.at(0, 0) == 0 && img.at(2, 2) == 0, "corners outside the radius stay transparent");
    }
    {   // no inverting pixel: nothing changes
        Image img; img.w = 2; img.h = 1; img.px = { 0xFF000000u, 0 };
        std::vector<uint8_t> none{ 0, 0 };
        AddOutline(img, none, 3);
        Check(img.w == 2 && img.h == 1 && img.px[1] == 0, "no inverting pixels, no outline");
    }
    Check(OutlineRadius(96) == 1 && OutlineRadius(120) == 1 && OutlineRadius(144) == 1,
          "outline radius is 1 up to 150 %");
    Check(OutlineRadius(192) == 2 && OutlineRadius(240) == 2 && OutlineRadius(480) == 4,
          "outline radius grows with the DPI");
    Check(OutlineRadius(0) == 1, "outline radius is never below 1");

    {   // visible height and fingerprint
        Image img; img.w = 2; img.h = 5; img.px.assign(10, 0);
        Check(VisibleHeight(img) == 0, "an empty picture has no visible height");
        img.px[1 * 2] = 0xFF000000u; img.px[3 * 2 + 1] = 0x80000000u;
        Check(VisibleHeight(img) == 3, "visible height spans the first to the last visible row");
        img.px[4 * 2] = 0x40000000u;   // a soft shadow row below
        Check(VisibleHeight(img) == 3, "pixels under 50 % opacity (shadows) do not count");
        Image same = img, other = img;
        other.px[1 * 2] = 0xFF010101u;
        Check(Key(img) == Key(same) && Key(img) != Key(other), "equal pictures share a key, different ones do not");
        other = img; other.hot.x = 1;
        Check(Key(img) != Key(other), "the hotspot is part of the key");
    }

    {   // integer zoom is exact nearest neighbour: one pixel -> a 5x5 block
        Image img; img.w = 1; img.h = 1; img.px = { 0xFF102030u };
        Image big = Scale(img, 500);
        bool same = big.w == 5 && big.h == 5;
        for (uint32_t c : big.px) same = same && c == 0xFF102030u;
        Check(same, "a single pixel at 500 % becomes an exact 5x5 block");
    }
    {
        Image img; img.w = 2; img.h = 1; img.px = { 0xFFFFFFFFu, 0xFF000000u };
        Image big = Scale(img, 200);
        Check(big.w == 4 && big.h == 2 && big.at(0, 0) == 0xFFFFFFFFu && big.at(1, 0) == 0xFFFFFFFFu &&
              big.at(2, 0) == 0xFF000000u && big.at(3, 0) == 0xFF000000u, "200 % doubles pixels without blur");
    }
    {   // fractional zoom: exact size, rounded hotspot, no dark seams on a flat colour
        Image img; img.w = 3; img.h = 3; img.hot = POINT{ 1, 2 };
        img.px.assign(9, 0xFFE02020u);
        Image big = Scale(img, 250);
        Check(big.w == 8 && big.h == 8, "250 % of 3 px is round(7.5) = 8 px");
        Check(big.hot.x == 3 && big.hot.y == 5, "hotspot is scaled and rounded");
        bool flat = true;
        for (uint32_t c : big.px) flat = flat && c == 0xFFE02020u;
        Check(flat, "a flat colour stays flat: clamped edges, no seams");
    }
    {   // bilinear part: a half-step between transparent and white is half white
        Image img; img.w = 2; img.h = 1; img.px = { 0, 0xFFFFFFFFu };
        Image big = Scale(img, 150);
        Check(big.w == 3, "150 % of 2 px is 3 px");
        const uint32_t mid = big.at(1, 0);
        Check((mid >> 24) > 0x40 && (mid >> 24) < 0xC0 && (mid & 0xFF) == (mid >> 24),
              "the middle pixel is a premultiplied blend");
    }
    {
        Image img; img.w = 4; img.h = 2; img.px.assign(8, 0xFF00FF00u);
        Image same = Scale(img, 100);
        Check(same.w == 4 && same.h == 2 && same.px == img.px, "100 % is the identity");
        Check(Scale(Image{}, 300).px.empty(), "an empty image scales to nothing");
        Check(Scale(img, 0).w == 1, "a zero zoom is treated as 1 %");
    }

    {   // live cursors: the three kinds AnyDesk can hand out
        Image img;
        Check(!Read(nullptr, 96, img), "Read(null) fails");
        Check(Read(LoadCursorW(nullptr, IDC_ARROW), 96, img) && img.w > 0 && img.h > 0,
              "the system arrow can be read");

        uint32_t px[4 * 6];
        for (auto& c : px) c = 0xFFE02020u;
        BYTE zeros[2 * 6] = {};
        HCURSOR alpha = MakeCursor(4, 6, px, zeros, POINT{ 1, 2 }, false);
        Check(Read(alpha, 96, img) && img.w == 4 && img.h == 6 && img.hot.x == 1 && img.hot.y == 2 &&
              img.at(0, 0) == 0xFFE02020u, "an alpha cursor reads back exactly");
        DestroyCursor(alpha);

        for (auto& c : px) c = 0;
        HCURSOR empty = MakeCursor(4, 6, px, zeros, POINT{ 0, 0 }, false);
        // alpha all zero + AND 0 + black = opaque black ("colour without alpha")
        Check(Read(empty, 96, img) && img.at(0, 0) == 0xFF000000u, "a colour cursor without alpha is opaque");
        DestroyCursor(empty);

        BYTE andOnly[2 * 6];
        memset(andOnly, 0xFF, sizeof(andOnly));
        HCURSOR invisible = MakeCursor(4, 6, px, andOnly, POINT{ 0, 0 }, false);
        Check(!Read(invisible, 96, img), "a fully transparent cursor has nothing to show");
        DestroyCursor(invisible);

        // 16x16 monochrome with one inverting column: read, outlined, padded.
        BYTE mono[2 * 16 * 2];
        memset(mono, 0xFF, 2 * 16);         // AND: all transparent ...
        memset(mono + 2 * 16, 0, 2 * 16);   // XOR: ...
        for (int y = 4; y < 12; ++y) mono[2 * 16 + y * 2] = 0x01;   // ... except x = 7 inverting
        HCURSOR ibeam = MakeCursor(16, 16, nullptr, mono, POINT{ 7, 8 }, true);
        Check(Read(ibeam, 192, img) && img.w == 20 && img.h == 20 && img.hot.x == 9 && img.hot.y == 10,
              "a mono cursor with inverting pixels gets a radius-2 outline at 200 %");
        Check(img.at(9, 6) == 0xFF000000u && img.at(8, 6) == 0xFFFFFFFFu,
              "inverting pixels are black with a white rim");
        DestroyCursor(ibeam);
    }
}

// ---- in-process UI: the owner-drawn controls, the settings and About windows --

static int g_commands = 0, g_scrolls = 0, g_settingsChanged = 0;
static LRESULT CALLBACK ParentProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COMMAND && HIWORD(wp) == BN_CLICKED) { ++g_commands; return 0; }
    if (msg == WM_HSCROLL) { ++g_scrolls; return 0; }
    if (msg == SettingsWindow::WM_SETTINGS_CHANGED) { ++g_settingsChanged; return 0; }
    return DefWindowProcW(h, msg, wp, lp);
}

static void Pump()
{
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
}

// A parent that is on screen (so the children really paint) but invisible.
static HWND MakeParent(bool rtl)
{
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = ParentProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"DeGhosterTestParent";
        RegisterClassExW(&wc);
        reg = true;
    }
    HWND p = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | (rtl ? WS_EX_LAYOUTRTL : 0),
                             L"DeGhosterTestParent", L"", WS_POPUP, 0, 0, 500, 120,
                             nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    SetLayeredWindowAttributes(p, 0, 1, LWA_ALPHA);
    ShowWindow(p, SW_SHOWNOACTIVATE);
    return p;
}

static void Key(HWND h, WPARAM vk) { SendMessageW(h, WM_KEYDOWN, vk, 0); }
static int Pos(HWND s) { return (int)SendMessageW(s, ui::SLM_GETPOS, 0, 0); }

static void ControlTests()
{
    std::printf("Control tests\n");
    HINSTANCE inst = GetModuleHandleW(nullptr);
    ui::RegisterControls(inst);
    ui::RegisterControls(inst);   // idempotent
    Theme theme = Theme::current();
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    for (int rtl = 0; rtl < 2; ++rtl) {
        HWND parent = MakeParent(rtl != 0);
        HWND tg = CreateWindowExW(0, ui::kToggleClass, L"Switch", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  10, 10, 300, 32, parent, (HMENU)(UINT_PTR)7, inst, nullptr);
        HWND sl = CreateWindowExW(0, ui::kSliderClass, L"Zoom", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  10, 50, 400, 32, parent, (HMENU)(UINT_PTR)8, inst, nullptr);
        SendMessageW(tg, ui::CTL_SETTHEME, 0, (LPARAM)&theme);
        SendMessageW(sl, ui::CTL_SETTHEME, 0, (LPARAM)&theme);
        SendMessageW(tg, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(sl, WM_SETFONT, (WPARAM)font, TRUE);
        Check((HFONT)SendMessageW(tg, WM_GETFONT, 0, 0) == font, "controls keep their font");

        // Toggle
        g_commands = 0;
        Check(SendMessageW(tg, BM_GETCHECK, 0, 0) == BST_UNCHECKED, "toggle starts unchecked");
        SendMessageW(tg, BM_SETCHECK, BST_CHECKED, 0);
        Check(SendMessageW(tg, BM_GETCHECK, 0, 0) == BST_CHECKED && g_commands == 0,
              "BM_SETCHECK sets the state without notifying");
        Key(tg, VK_SPACE);
        Check(SendMessageW(tg, BM_GETCHECK, 0, 0) == BST_UNCHECKED && g_commands == 1,
              "Space flips the toggle and notifies the parent");
        Check(SendMessageW(tg, WM_GETDLGCODE, 0, 0) == DLGC_WANTCHARS, "toggle wants chars");
        SendMessageW(tg, WM_CHAR, L' ', 0);
        Key(tg, VK_RETURN);   // ignored
        SendMessageW(tg, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 16));
        SendMessageW(tg, WM_LBUTTONUP, 0, MAKELPARAM(20, 16));
        Check(SendMessageW(tg, BM_GETCHECK, 0, 0) == BST_CHECKED && g_commands == 2,
              "a click flips the toggle");
        SendMessageW(tg, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 16));
        SendMessageW(tg, WM_LBUTTONUP, 0, MAKELPARAM(900, 16));
        Check(g_commands == 2, "releasing outside the toggle does not flip it");

        g_scrolls = 0;
        SendMessageW(sl, ui::SLM_SETRANGE, 100, 600);
        SendMessageW(sl, ui::SLM_SETSTEP, 10, 50);
        SendMessageW(sl, ui::SLM_SETPOS, 250, 0);
        Check(Pos(sl) == 250 && g_scrolls == 0, "SLM_SETPOS sets without notifying");
        SendMessageW(sl, ui::SLM_SETPOS, 9999, 0);
        Check(Pos(sl) == 600, "SLM_SETPOS clamps");
        SendMessageW(sl, ui::SLM_SETPOS, 250, 0);
        Check(SendMessageW(sl, WM_GETDLGCODE, 0, 0) == DLGC_WANTARROWS, "slider wants the arrow keys");
        Key(sl, VK_RIGHT);
        Check(Pos(sl) == (rtl ? 240 : 260), rtl ? "Right lowers the value under RTL" : "Right raises the value");
        Key(sl, VK_LEFT);
        Check(Pos(sl) == 250, "Left undoes Right");
        Key(sl, VK_UP);   Check(Pos(sl) == 260, "Up raises by a step");
        Key(sl, VK_DOWN); Check(Pos(sl) == 250, "Down lowers by a step");
        Key(sl, VK_PRIOR); Check(Pos(sl) == 300, "PgUp raises by a page");
        Key(sl, VK_NEXT);  Check(Pos(sl) == 250, "PgDn lowers by a page");
        Key(sl, VK_END);   Check(Pos(sl) == 600, "End jumps to the maximum");
        Key(sl, VK_END);   // no change, no notification
        Key(sl, VK_HOME);  Check(Pos(sl) == 100, "Home jumps to the minimum");
        Key(sl, VK_TAB);   // not handled
        Check(g_scrolls == 8, "every change (and only a change) notifies the parent");
        SendMessageW(sl, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
        Check(Pos(sl) == 110, "the wheel moves by a step");
        SendMessageW(sl, WM_MOUSEWHEEL, MAKEWPARAM(0, (WORD)-WHEEL_DELTA), 0);
        Check(Pos(sl) == 100, "the wheel moves back");

        // Mouse: press at the far end, drag to the start, release.
        SendMessageW(sl, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(330, 16));
        const int dragged = Pos(sl);
        Check(dragged > 500, "pressing near the end of the track jumps there");
        SendMessageW(sl, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(0, 16));
        Check(Pos(sl) == 100, "dragging to the start reaches the minimum");
        SendMessageW(sl, WM_LBUTTONUP, 0, MAKELPARAM(0, 16));
        SendMessageW(sl, WM_MOUSEMOVE, 0, MAKELPARAM(330, 16));
        Check(Pos(sl) == 100, "moving without the button does nothing");
        SendMessageW(sl, WM_CAPTURECHANGED, 0, 0);

        // Paint in every state: focused, disabled, checked and unchecked.
        SetFocus(tg);
        SendMessageW(tg, WM_UPDATEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);
        UpdateWindow(tg);
        SetFocus(sl);
        SendMessageW(sl, WM_UPDATEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);
        UpdateWindow(sl);
        EnableWindow(tg, FALSE);
        EnableWindow(sl, FALSE);
        SendMessageW(tg, BM_SETCHECK, BST_UNCHECKED, 0);
        SetWindowTextW(tg, L"Switch (off)");
        RedrawWindow(parent, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        Check(SendMessageW(tg, WM_ERASEBKGND, 0, 0) == 1, "controls skip background erasing");
        Pump();

        DestroyWindow(parent);
    }
}

static void SettingsWindowTests()
{
    std::printf("Settings window tests\n");
    std::wstring root = L"Software\\DeGhoster_Test_SW_" + std::to_wstring(GetCurrentProcessId());
    SetEnvironmentVariableW(L"DEGHOSTER_SETTINGS_ROOT", root.c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str());

    HINSTANCE inst = GetModuleHandleW(nullptr);
    HWND owner = MakeParent(false);
    Settings settings; settings.load();
    settings.setCursorOverlayZoom(250);
    Theme theme = Theme::current();

    Check(SettingsWindow::ActiveHandle() == nullptr, "no settings window before Show");
    SettingsWindow::Sync();   // no window: nothing to do
    SettingsWindow::Show(inst, owner, theme, 96, settings);
    HWND w = SettingsWindow::ActiveHandle();
    Check(w != nullptr && IsWindowVisible(w), "Show opens the settings window");
    SettingsWindow::Show(inst, owner, theme, 96, settings);
    Check(SettingsWindow::ActiveHandle() == w, "a second Show reuses the open window");
    Pump();

    HWND autoSw = GetDlgItem(w, SettingsWindow::IDC_CURSOR_AUTO);
    HWND zoom = GetDlgItem(w, SettingsWindow::IDC_CURSOR_ZOOM);
    HWND close = GetDlgItem(w, SettingsWindow::IDC_CLOSE);
    Check(autoSw && zoom && close, "the section has the Auto switch, a slider and a Close button");
    Check(SendMessageW(autoSw, BM_GETCHECK, 0, 0) == BST_CHECKED && Pos(zoom) == 250,
          "the controls show the stored settings (Auto on by default)");
    Check(!IsWindowEnabled(zoom), "the fixed zoom is greyed out while Auto is on");

    g_settingsChanged = 0;
    Key(autoSw, VK_SPACE);
    Check(!settings.cursorOverlayAuto() && IsWindowEnabled(zoom) && g_settingsChanged == 1,
          "switching Auto off saves it, enables the slider and tells the owner");
    Key(zoom, VK_END);
    Check(settings.cursorOverlayZoom() == 600 && g_settingsChanged == 2,
          "moving the slider saves the zoom and tells the owner");
    {
        Settings reread; reread.load();
        Check(reread.cursorOverlayZoom() == 600 && !reread.cursorOverlayAuto(), "changes are saved at once");
    }
    Key(autoSw, VK_SPACE);
    Check(settings.cursorOverlayAuto() && !IsWindowEnabled(zoom), "switching Auto on greys the slider out again");

    settings.setGlobalEnabled(false);
    SettingsWindow::Sync();
    Check(!IsWindowEnabled(autoSw) && !IsWindowEnabled(zoom), "global off greys out the section");
    settings.setGlobalEnabled(true);
    settings.setCursorOverlayAuto(false);
    settings.setCursorOverlayZoom(150);
    SettingsWindow::Sync();
    Check(IsWindowEnabled(zoom) && Pos(zoom) == 150, "Sync picks up changes made elsewhere");

    // DPI change, theme change, repaint, then close with Esc (IDCANCEL).
    RECT r; GetWindowRect(w, &r);
    RECT suggested{ r.left, r.top, r.left + (r.right - r.left) * 2, r.top + (r.bottom - r.top) * 2 };
    SendMessageW(w, WM_DPICHANGED, MAKEWPARAM(192, 192), (LPARAM)&suggested);
    SendMessageW(w, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet");
    SendMessageW(w, WM_SETTINGCHANGE, 0, (LPARAM)L"Other");
    RedrawWindow(w, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    SendMessageW(close, WM_MOUSEMOVE, 0, 0);   // hover state for the owner-drawn button
    RedrawWindow(close, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    GetWindowRect(w, &r);
    Check(r.right - r.left > suggested.right - suggested.left - 200, "the window grows with the DPI");
    SendMessageW(w, WM_COMMAND, 9999, 0);
    Check(SettingsWindow::ActiveHandle() == w, "an unknown command leaves the window open");
    SendMessageW(w, WM_COMMAND, IDCANCEL, 0);
    Pump();
    Check(SettingsWindow::ActiveHandle() == nullptr, "Esc closes the settings window");

    SettingsWindow::Show(inst, owner, theme, 96, settings);
    PostMessageW(SettingsWindow::ActiveHandle(), WM_CLOSE, 0, 0);
    Pump();
    Check(SettingsWindow::ActiveHandle() == nullptr, "the caption button closes it too");

    // An owner that goes away takes the settings window with it (--quit path).
    SettingsWindow::Show(inst, owner, theme, 96, settings);
    DestroyWindow(owner);
    Pump();
    Check(SettingsWindow::ActiveHandle() == nullptr, "destroying the owner closes the settings window");

    // About window: open, DPI change, close.
    HWND owner2 = MakeParent(false);
    InfoWindow::Show(inst, owner2, theme, 96);
    HWND info = InfoWindow::ActiveHandle();
    Check(info != nullptr, "the About window opens");
    if (info) {
        GetWindowRect(info, &r);
        SendMessageW(info, WM_DPICHANGED, MAKEWPARAM(144, 144), (LPARAM)&r);
        RedrawWindow(info, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        SendMessageW(info, WM_COMMAND, IDOK, 0);
        Pump();
        Check(InfoWindow::ActiveHandle() == nullptr, "OK closes the About window");
    }
    DestroyWindow(owner2);

    RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str());
}

struct CountingListener : GhostEngine::Listener {
    int changes = 0;
    void onTrackedChanged() override { ++changes; }
};

static HWND MakeTopLevel(const wchar_t* cls, const wchar_t* title, DWORD ex)
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);   // fails harmlessly when already registered
    HWND h = CreateWindowExW(ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, title, WS_POPUP,
                             60, 60, 200, 150, nullptr, nullptr, wc.hInstance, nullptr);
    return h;
}

static void GhostEngineTests()
{
    std::printf("GhostEngine tests\n");
    std::wstring root = L"Software\\DeGhoster_Test_GE_" + std::to_wstring(GetCurrentProcessId());
    SetEnvironmentVariableW(L"DEGHOSTER_SETTINGS_ROOT", root.c_str());
    RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str());
    Settings s; s.load();
    s.setGlobalEnabled(false);   // track only: nothing gets injected or cloaked here

    // Both cases, in this process: an AnyDesk session window and a ghost.
    HWND ad = MakeTopLevel(L"ad_win#9", L"123 456 789 - AnyDesk", 0);
    HWND ghost = MakeTopLevel(L"Chrome_WidgetWin_1", L"DeGhoster-EngineGhost", WS_EX_LAYERED);
    SetLayeredWindowAttributes(ghost, 0, 0, LWA_ALPHA);
    ShowWindow(ad, SW_SHOWNA);
    ShowWindow(ghost, SW_SHOWNA);
    HWND host = MakeParent(false);

    GhostEngine eng(s);
    CountingListener l;
    eng.setListener(&l);
    eng.start(host);
    const auto& t = eng.tracked();
    Check(t.count(ad) && t.at(ad).kind == FixInfo::Kind::AnyDesk,
          "an AnyDesk session window is listed as a case of its own");
    Check(t.count(ghost) && t.at(ghost).kind == FixInfo::Kind::Ghost, "a ghost is listed as a ghost");
    Check(eng.ghostCount() >= 2 && l.changes >= 2, "both count and both notify the listener");
    // Both belong to this process, so they are one program with one switch.
    Check(t.count(ad) && t.count(ghost) && t.at(ad).disableKey() == t.at(ghost).disableKey() &&
          t.at(ad).disableKey().find(L'|') == std::wstring::npos && t.at(ad).disableKey() != L"?",
          "windows are switched off per program, AnyDesk windows and ghosts alike");
    Check(t.count(ad) && eng.trackAnyDesk(ad) == &t.at(ad), "trackAnyDesk finds the tracked window");
    Check(eng.trackAnyDesk(ghost) == nullptr, "a ghost is not an AnyDesk window");
    Check(eng.trackAnyDesk(host) == nullptr, "an ordinary window is not tracked");

    // Hidden (the app minimized): stays listed, marked, and comes back unmarked.
    // WhatsApp does not only hide its ghost when minimized, it also parks it at
    // -32000,-32000, where Windows keeps minimized windows - off every screen.
    RECT shown{}; GetWindowRect(ghost, &shown);
    ShowWindow(ghost, SW_HIDE);
    SetWindowPos(ghost, nullptr, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(ad, SW_HIDE);
    eng.tick();
    Check(t.count(ghost) && t.at(ghost).hidden, "a hidden ghost parked off screen stays listed, marked hidden");
    Check(t.count(ad) && t.at(ad).hidden, "a hidden AnyDesk window stays listed, marked hidden");
    SetWindowPos(ghost, nullptr, shown.left, shown.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(ghost, SW_SHOWNA);
    eng.tick();
    Check(t.count(ghost) && !t.at(ghost).hidden, "shown again, the ghost is no longer marked");

    // The cursor can reach a new AnyDesk window before any of its events do.
    HWND ad2 = MakeTopLevel(L"ad_win#9", L"987 654 321 - AnyDesk", 0);
    ShowWindow(ad2, SW_SHOWNA);
    Check(eng.trackAnyDesk(ad2) != nullptr && t.count(ad2), "trackAnyDesk picks up a new AnyDesk window");

    // Gone, or no longer a ghost: dropped.
    DestroyWindow(ad);
    eng.tick();
    Check(!t.count(ad) && t.count(ad2), "a destroyed AnyDesk window leaves the list, the other stays");
    SetLayeredWindowAttributes(ghost, 0, 255, LWA_ALPHA);
    eng.tick();
    Check(!t.count(ghost), "a window that stops being a ghost leaves the list");

    eng.stop(500);
    DestroyWindow(ad2);
    DestroyWindow(ghost);
    DestroyWindow(host);
    RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str());
}

// This process's top-level window of a class (a running DeGhoster has its own).
static HWND FindOwnWindow(const wchar_t* cls)
{
    for (HWND h = FindWindowExW(nullptr, nullptr, cls, nullptr); h; h = FindWindowExW(nullptr, h, cls, nullptr)) {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid == GetCurrentProcessId()) return h;
    }
    return nullptr;
}

static void CursorOverlayTests()
{
    std::printf("CursorOverlay tests\n");
    CursorOverlay ov;
    Check(ov.create(GetModuleHandleW(nullptr)), "the overlay window is created");
    Check(ov.create(GetModuleHandleW(nullptr)), "create is idempotent");
    HWND w = FindOwnWindow(L"DeGhosterCursorOverlay");
    Check(w != nullptr, "the overlay has its own window class");
    if (w) {
        const LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
        const LONG_PTR need = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        Check((ex & need) == need, "the overlay is layered, click-through, topmost, no-activate, tool window");
        Check(SendMessageW(w, WM_NCHITTEST, 0, 0) == HTTRANSPARENT, "the overlay never takes a hit test");
        Check(SendMessageW(w, WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATE, "the overlay never activates");
    }
    // The cursor is not over an AnyDesk window here, so it must stay hidden.
    ov.setZoom(300);
    ov.setEnabled(true);
    Pump();
    Check(!ov.visible(), "enabled but not over AnyDesk: hidden");
    ov.setSessionLocked(true);
    Check(!ov.visible(), "locked: hidden");
    ov.setSessionLocked(false);
    ov.setEnabled(false);
    Check(!ov.visible(), "disabled: hidden");
    ov.destroy();
    ov.destroy();   // idempotent
    Check(FindOwnWindow(L"DeGhosterCursorOverlay") == nullptr, "destroy removes the window");
}

static int Cloaked(HWND h)
{
    int v = 0;
    return DwmGetWindowAttribute(h, DWMWA_CLOAKED, &v, sizeof(v)) == S_OK ? v : -1;
}

// Pump this thread's queue (so the WH_GETMESSAGE hook fires) until the window's
// cloaked state reaches `want`, or the timeout elapses.
static bool PumpUntilCloaked(HWND h, int want, DWORD ms)
{
    DWORD start = GetTickCount();
    MSG m;
    for (;;) {
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        if (Cloaked(h) == want) return true;
        if (GetTickCount() - start > ms) return Cloaked(h) == want;
        Sleep(10);
    }
}

static std::wstring ExeDir()
{
    wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p; size_t i = s.find_last_of(L'\\');
    return i == std::wstring::npos ? L"" : s.substr(0, i + 1);
}

static void HookDllTests()
{
    std::printf("Hook DLL tests\n");

    const wchar_t* cls = L"Chrome_WidgetWin_1";
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);

    HWND h = CreateWindowExW(WS_EX_LAYERED, cls, L"DeGhoster-HookTest", WS_POPUP,
                             120, 120, 300, 200, nullptr, nullptr, wc.hInstance, nullptr);
    if (!h) { Check(false, "create ghost window"); return; }
    SetLayeredWindowAttributes(h, 0, 0, LWA_ALPHA);
    ShowWindow(h, SW_SHOWNA);

    typedef HHOOK (__stdcall* InstallFn)(DWORD, HWND);
    typedef BOOL  (__stdcall* RemoveFn)(HHOOK);

    HMODULE dll = LoadLibraryW((ExeDir() + L"DeGhoster.Hook64.dll").c_str());
    Check(dll != nullptr, "load DeGhoster.Hook64.dll");
    if (dll) {
        auto install = (InstallFn)GetProcAddress(dll, "DgInstallHook");
        auto remove  = (RemoveFn) GetProcAddress(dll, "DgRemoveHook");
        Check(install && remove, "resolve DgInstallHook / DgRemoveHook");
        if (install && remove) {
            HHOOK hk = install(GetCurrentThreadId(), nullptr);
            Check(hk != nullptr, "DgInstallHook returns a hook");

            PostMessageW(h, DGH_CLOAK, 0, 0);
            Check(PumpUntilCloaked(h, 1, 2000), "DGH_CLOAK cloaks the window");

            PostMessageW(h, DGH_UNCLOAK, 0, 0);
            Check(PumpUntilCloaked(h, 0, 2000), "DGH_UNCLOAK un-cloaks the window");

            // Re-cloak, then remove the hook: the window must STAY cloaked
            // (removing the hook does not un-cloak) so DLL detach can restore it.
            PostMessageW(h, DGH_CLOAK, 0, 0);
            Check(PumpUntilCloaked(h, 1, 2000), "re-cloak before teardown");
            Check(remove(hk) != FALSE, "DgRemoveHook succeeds");
            Check(Cloaked(h) == 1, "removing the hook does not un-cloak");
        }
        // Note: the loader defers unloading the hook DLL after UnhookWindowsHookEx,
        // so DllMain's DETACH auto-un-cloak can't be deterministically triggered
        // from within the same process here (the window is torn down below anyway).
        FreeLibrary(dll);
    }

    if (h) DestroyWindow(h);
}

static void ProcessUtilTests()
{
    std::printf("ProcessUtil tests\n");

    const std::wstring dir = proc::ExeDir();
    Check(!dir.empty() && dir.back() == L'\\', "ExeDir ends in a backslash");
    Check(GetFileAttributesW((dir + L"DeGhoster.Hook64.dll").c_str()) != INVALID_FILE_ATTRIBUTES,
          "ExeDir points at the build output");

    // WindowTitle must stay RAW: the localized placeholder would end up in the
    // registry opt-out key and be invalidated by a UI-language change.
    const wchar_t* cls = L"DeGhosterTitleProbe";
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);
    HWND named = CreateWindowExW(0, cls, L"ProbeTitle", WS_POPUP, 0, 0, 10, 10,
                                 nullptr, nullptr, wc.hInstance, nullptr);
    HWND blank = CreateWindowExW(0, cls, L"", WS_POPUP, 0, 0, 10, 10,
                                 nullptr, nullptr, wc.hInstance, nullptr);
    Check(proc::WindowTitle(named) == L"ProbeTitle", "WindowTitle returns the title");
    Check(proc::WindowTitle(blank).empty(), "WindowTitle leaves an untitled window empty");

    std::wstring exeName, exePath;
    proc::ResolveHostExe(GetCurrentProcessId(), exeName, exePath);
    Check(_wcsicmp(exeName.c_str(), L"UnitTests.exe") == 0, "ResolveHostExe finds our own image");
    Check(!exePath.empty(), "ResolveHostExe returns a full path");
    proc::ResolveHostExe(0xFFFFFFFCu, exeName, exePath);
    Check(exeName == L"?", "ResolveHostExe marks an unknown pid");

    // The per-program opt-out key.
    std::wstring program;
    proc::ResolveHostExe(GetCurrentProcessId(), exeName, exePath, &program);
    Check(program == exePath, "an unpackaged program is keyed by its full path");
    proc::ResolveHostExe(0xFFFFFFFCu, exeName, exePath, &program);
    Check(program == L"?", "an unreadable process is the unknown program");

    struct { const wchar_t* path; const wchar_t* key; const char* what; } cases[] = {
        { L"C:\\Program Files (x86)\\AnyDesk\\AnyDesk.exe", L"C:\\Program Files (x86)\\AnyDesk\\AnyDesk.exe",
          "a normal program keeps its full path" },
        { L"C:\\Program Files\\WindowsApps\\5319275A.WhatsAppDesktop_2.2637.100.0_x64__cv1g1gvanyjgm\\WhatsApp.Root.exe",
          L"5319275A.WhatsAppDesktop_cv1g1gvanyjgm\\WhatsApp.Root.exe",
          "a Store app is keyed by its package family, without version and architecture" },
        { L"D:\\WindowsApps\\Pkg.Name_1.0.0.0_neutral_split.scale-200_pub123\\sub\\App.exe",
          L"Pkg.Name_pub123\\sub\\App.exe",
          "a Store app on another drive, with a resource id and a subfolder" },
        { L"C:\\Program Files\\WindowsApps\\NotAPackageFolder\\App.exe",
          L"C:\\Program Files\\WindowsApps\\NotAPackageFolder\\App.exe",
          "a WindowsApps folder that is no package name keeps the full path" },
        { L"C:\\Users\\u\\AppData\\Local\\Discord\\app-1.0.9187\\Discord.exe",
          L"C:\\Users\\u\\AppData\\Local\\Discord\\app\\Discord.exe",
          "a Squirrel version folder is reduced to app" },
        { L"C:\\Users\\u\\AppData\\Local\\slack\\App-4.41.105-beta2\\slack.exe",
          L"C:\\Users\\u\\AppData\\Local\\slack\\app\\slack.exe",
          "a Squirrel prerelease folder, in any case, is reduced as well" },
        { L"C:\\Tools\\app-foo\\x.exe", L"C:\\Tools\\app-foo\\x.exe", "app-<not a version> is left alone" },
        { L"C:\\Tools\\app-\\x.exe", L"C:\\Tools\\app-\\x.exe", "a bare app- is left alone" },
        { L"C:\\Tools\\app-1.\\x.exe", L"C:\\Tools\\app-1.\\x.exe", "a version with a trailing dot is left alone" },
        { L"C:\\Tools\\myapp-1.0\\x.exe", L"C:\\Tools\\myapp-1.0\\x.exe", "only a folder named app-<version> counts" },
        { L"C:\\Tools\\app-1.0\\bin\\x.exe", L"C:\\Tools\\app-1.0\\bin\\x.exe",
          "the version folder must hold the exe itself" },
        { L"", L"?", "no path is the unknown program" },
    };
    for (const auto& c : cases) Check(proc::ProgramKeyFromPath(c.path) == c.key, c.what);

    // For every Store app running right now, the key Windows' package identity
    // gives must equal the one read from its path, which is what old opt-outs
    // migrate from.
    int packaged = 0, agree = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W e{ sizeof(e) };
        for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e)) {
            proc::ResolveHostExe(e.th32ProcessID, exeName, exePath, &program);
            if (exePath.find(L"\\WindowsApps\\") == std::wstring::npos) continue;
            ++packaged;
            if (program == proc::ProgramKeyFromPath(exePath)) ++agree;
        }
        CloseHandle(snap);
    }
    if (packaged) Check(agree == packaged, "package identity and path give the same key for running Store apps");
    else std::printf("  [skip] no Store app running to compare package identity with the path\n");

    Check(!proc::IsWow64(GetCurrentProcessId()), "the x64 test process is not WOW64");
    Check(!proc::IsWow64(0xFFFFFFFCu), "IsWow64 is false for an unknown pid");

    if (named) DestroyWindow(named);
    if (blank) DestroyWindow(blank);
}

// A thread with a message queue that exits when its event is signalled, so a
// hook can be installed on it and the thread can then be made to go away.
static DWORD WINAPI ProbeThread(LPVOID param)
{
    HANDLE stop = (HANDLE)param;
    MSG m;
    PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE);   // force the queue into existence
    for (;;) {
        DWORD w = MsgWaitForMultipleObjects(1, &stop, FALSE, INFINITE, QS_ALLINPUT);
        if (w == WAIT_OBJECT_0) break;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { }
    }
    return 0;
}

static void HookInjectorTests()
{
    std::printf("HookInjector tests\n");

    {
        HookInjector bad;
        Check(!bad.load(proc::ExeDir() + L"does-not-exist\\"), "load fails without the helpers");
        Check(!bad.available(), "available is false after a failed load");
    }

    HookInjector inj;
    Check(inj.load(proc::ExeDir()), "load finds both helpers");
    Check(inj.available(), "available is true once both helpers are there");

    // A helper started for a thread that does not exist installs nothing and
    // exits. The first call only starts it, so it reports Pending; once it has
    // died the next call must report Failed so the caller's cooldown kicks in.
    const DWORD deadThread = 0xFFFFFFFCu;
    Check(inj.ensure(deadThread, GetCurrentProcessId(), nullptr) == HookInjector::Inject::Pending,
          "ensure reports Pending while the helper starts");
    HookInjector::Inject late = HookInjector::Inject::Pending;
    for (int i = 0; i < 100 && late == HookInjector::Inject::Pending; ++i) {
        Sleep(50);
        late = inj.ensure(deadThread, GetCurrentProcessId(), nullptr);
    }
    Check(late == HookInjector::Inject::Failed, "ensure reports Failed once the helper is gone");

    // The real path, on this very thread: the helper comes up, signals readiness
    // and the entry flips to Ready.
    const DWORD self = GetCurrentThreadId();
    Check(inj.ensure(self, GetCurrentProcessId(), nullptr) == HookInjector::Inject::Pending,
          "ensure starts a helper for a live thread");
    HookInjector::Inject state = HookInjector::Inject::Pending;
    for (int i = 0; i < 200 && state != HookInjector::Inject::Ready; ++i) {
        Sleep(25);
        state = inj.ensure(self, GetCurrentProcessId(), nullptr);
    }
    Check(state == HookInjector::Inject::Ready, "ensure flips to Ready once the helper signals");
    Check(proc::HelperRunning(), "HelperRunning sees the helper we started");

    // The hooked thread is still alive, so the entry must survive a prune.
    inj.pruneDead();
    Check(inj.ensure(self, GetCurrentProcessId(), nullptr) == HookInjector::Inject::Ready,
          "pruneDead keeps the entry for a live thread");

    inj.removeAll();
    bool gone = false;
    for (int i = 0; i < 100 && !gone; ++i) { Sleep(50); gone = !proc::HelperRunning(); }
    Check(gone, "removeAll winds the helper down");

    // pruneDead has to reclaim entries whose thread died. Nothing tells the host
    // about that, so without the sweep the entry would sit there until shutdown.
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD probeTid = 0;
    HANDLE probe = CreateThread(nullptr, 0, ProbeThread, stop, 0, &probeTid);
    Check(probe != nullptr, "probe thread starts");
    if (probe) {
        Sleep(150);   // let the thread reach its message loop
        HookInjector::Inject st = HookInjector::Inject::Pending;
        for (int i = 0; i < 200 && st != HookInjector::Inject::Ready; ++i) {
            Sleep(25);
            st = inj.ensure(probeTid, GetCurrentProcessId(), nullptr);
        }
        Check(st == HookInjector::Inject::Ready, "helper hooks the probe thread");

        SetEvent(stop);
        Check(WaitForSingleObject(probe, 5000) == WAIT_OBJECT_0, "probe thread exits");
        for (int i = 0; i < 100 && proc::HelperRunning(); ++i) Sleep(50);

        // The entry is stale now. After the sweep, asking again must start a fresh
        // helper (Pending) rather than report the dead one as a failure.
        inj.pruneDead();
        Check(inj.ensure(probeTid, GetCurrentProcessId(), nullptr) == HookInjector::Inject::Pending,
              "pruneDead drops the entry for a dead thread");
        inj.removeAll();
        CloseHandle(probe);
    }
    CloseHandle(stop);

    // The documented bail-out: a helper that cannot open the host has no exit
    // condition (it owns no window, so no WM_QUIT ever reaches it) and must not
    // linger with the hook installed. Valid thread id so the hook installs, bogus
    // host pid so opening the host fails.
    {
        std::wstring cmd = L"\"" + proc::ExeDir() + L"DeGhoster.Helper64.exe\" " +
                           std::to_wstring(GetCurrentThreadId()) + L" 0 4294967292";
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(L'\0');
        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        if (Check(CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                                 CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi) != FALSE,
                  "helper starts for the host-handle test")) {
            CloseHandle(pi.hThread);
            DWORD code = 1;
            Check(WaitForSingleObject(pi.hProcess, 10000) == WAIT_OBJECT_0,
                  "helper exits instead of lingering without a host");
            GetExitCodeProcess(pi.hProcess, &code);
            Check(code == 5, "helper reports the host-handle failure (exit 5)");
            CloseHandle(pi.hProcess);
        }
    }
}

static void ThemeTests()
{
    std::printf("Theme tests\n");
    SetEnvironmentVariableW(L"DEGHOSTER_FORCE_THEME", L"light");
    Check(!Theme::current().dark, "forced light theme -> dark == false");
    SetEnvironmentVariableW(L"DEGHOSTER_FORCE_THEME", L"dark");
    Check(Theme::current().dark, "forced dark theme -> dark == true");
    SetEnvironmentVariableW(L"DEGHOSTER_FORCE_THEME", nullptr);
}

int main()
{
    gfx::GdiPlus gdiplus;
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LINK_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    SettingsTests();
    ZoomTests();
    AutoZoomTests();
    AnyDeskClassTests();
    CursorImageTests();
    ControlTests();
    SettingsWindowTests();
    CursorOverlayTests();
    GhostEngineTests();
    ThemeTests();
    ProcessUtilTests();
    HookInjectorTests();
    HookDllTests();
    std::printf("%s (%d failure(s))\n", g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
