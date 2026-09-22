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

#include "InfoWindow.h"
#include "AppInfo.h"
#include "HoverButton.h"
#include "Loc.h"
#include "ProcessUtil.h"
#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

namespace {
constexpr wchar_t kClass[]   = L"DeGhosterInfoWindow";
constexpr int     kLinkCoffee  = 101;
constexpr int     kLinkLicense = 102;

// Brand strings stay English; everything else comes from the string table.
constexpr wchar_t kTagline[] = L"Who you gonna call? Bustin' invisible, click-eating window ghosts.";
constexpr wchar_t kCopy[]    = L"Copyright (c) Emmo Emminghaus mo2000 at mo2000 dot de";
}

HWND InfoWindow::s_active = nullptr;

HWND InfoWindow::ActiveHandle() { return s_active; }

void InfoWindow::Show(HINSTANCE inst, HWND owner, const Theme& theme, UINT dpi)
{
    if (s_active && IsWindow(s_active)) { SetForegroundWindow(s_active); return; }

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = inst;
        wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        RegisterClassExW(&wc);
        registered = true;
    }

    auto* self = new InfoWindow();
    self->theme_ = theme;
    self->dpi_ = dpi;

    RECT wr{ 0, 0, self->S(400), self->S(396) };
    AdjustWindowRectExForDpi(&wr, WS_CAPTION | WS_SYSMENU, FALSE, 0, dpi);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;

    // Centre on the owner, but a minimized owner reports a rect near (-32000,-32000),
    // which would place us off-screen. Fall back to the owner's monitor work area,
    // then clamp so we're always fully on that monitor.
    RECT area;
    HMONITOR mon = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    RECT o;
    if (!IsIconic(owner) && IsWindowVisible(owner) && GetWindowRect(owner, &o) &&
        o.right > o.left && o.left > -30000) {
        area = o;
    } else {
        area = mi.rcWork;
    }
    int x = area.left + ((area.right - area.left) - ww) / 2;
    int y = area.top + ((area.bottom - area.top) - wh) / 2;
    if (x + ww > mi.rcWork.right)  x = mi.rcWork.right - ww;
    if (y + wh > mi.rcWork.bottom) y = mi.rcWork.bottom - wh;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top)  y = mi.rcWork.top;

    DWORD ex = WS_EX_DLGMODALFRAME | (loc::isRtl() ? WS_EX_LAYOUTRTL : 0);
    HWND h = CreateWindowExW(ex, kClass, loc::t(IDS_INFO_TITLE),
                             WS_CAPTION | WS_SYSMENU, x, y, ww, wh, owner, nullptr, inst, self);
    if (!h) { delete self; return; }

    ApplyDarkTitleBar(h, theme.dark);
    int corner = 2;   // DWMWCP_ROUND (Win11; ignored on Win10)
    DwmSetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
}

LRESULT CALLBACK InfoWindow::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    InfoWindow* self;
    if (msg == WM_NCCREATE) {
        self = static_cast<InfoWindow*>(((CREATESTRUCTW*)lp)->lpCreateParams);
        self->hwnd_ = h;
        s_active = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (InfoWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    }
    if (!self) return DefWindowProcW(h, msg, wp, lp);

    if (msg == WM_NCDESTROY) {
        LRESULT r = self->handle(msg, wp, lp);
        s_active = nullptr;
        delete self;
        return r;
    }
    return self->handle(msg, wp, lp);
}

void InfoWindow::applyDpiAssets()
{
    // Own our font instead of borrowing the caller's: the main window deletes its
    // font on WM_DPICHANGED, which would leave this modeless window on a freed HFONT.
    if (uiFont_) DeleteObject(uiFont_);
    uiFont_ = CreateFontW(-MulDiv(9, dpi_, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (icon_) DestroyIcon(icon_);
    icon_ = (HICON)LoadImageW((HINSTANCE)GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE),
                              MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, S(48), S(48), 0);
    if (licenseLink_) SendMessageW(licenseLink_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
    if (link_)        SendMessageW(link_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
}

void InfoWindow::layout()
{
    RECT rc; GetClientRect(hwnd_, &rc);
    MoveWindow(licenseLink_, S(24), S(248), S(340), S(24), TRUE);
    MoveWindow(link_, S(24), S(318), S(280), S(24), TRUE);
    MoveWindow(ok_, rc.right - S(24) - S(88), rc.bottom - S(22) - S(32), S(88), S(32), TRUE);
}

void InfoWindow::paint(HDC hdc)
{
    RECT rc; GetClientRect(hwnd_, &rc);
    FillRect(hdc, &rc, brush_);
    DrawIconEx(hdc, S(24), S(24), icon_, S(48), S(48), 0, nullptr, DI_NORMAL);
    SetBkMode(hdc, TRANSPARENT);

    HFONT title = CreateFontW(-MulDiv(16, dpi_, 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
                              DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT old = (HFONT)SelectObject(hdc, title);
    SetTextColor(hdc, theme_.fore);
    RECT rt{ S(86), S(30), rc.right - S(20), S(72) };
    DrawTextW(hdc, app::Name, -1, &rt, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(hdc, uiFont_);
    SetTextColor(hdc, theme_.fore);
    RECT r1{ S(24), S(88), rc.right - S(24), S(136) };
    DrawTextW(hdc, kTagline, -1, &r1, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    SetTextColor(hdc, theme_.foreDim);
    RECT r2{ S(24), S(138), rc.right - S(24), S(190) };
    DrawTextW(hdc, loc::t(IDS_INFO_DESC), -1, &r2, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    RECT r3{ S(24), S(198), rc.right - S(24), S(218) };
    DrawTextW(hdc, kCopy, -1, &r3, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    RECT r4{ S(24), S(220), rc.right - S(24), S(240) };
    DrawTextW(hdc, loc::t(IDS_INFO_LICENSE), -1, &r4, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(hdc, theme_.fore);
    RECT r5{ S(24), S(286), rc.right - S(24), S(316) };
    DrawTextW(hdc, loc::t(IDS_INFO_SUPPORT), -1, &r5, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

    SelectObject(hdc, old);
    DeleteObject(title);
}

LRESULT InfoWindow::handle(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        brush_ = CreateSolidBrush(theme_.back);
        applyDpiAssets();
        {
            std::wstring lic = std::wstring(L"<a>") + loc::t(IDS_INFO_THIRDPARTY) + L"</a>";
            licenseLink_ = CreateWindowExW(0, WC_LINK, lic.c_str(),
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0,
                                           hwnd_, (HMENU)(UINT_PTR)kLinkLicense, nullptr, nullptr);
        }
        link_ = CreateWindowExW(0, WC_LINK,
                                L"<a href=\"https://ko-fi.com/motwok\">\u2615 Buy Me a Coffee</a>",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0,
                                hwnd_, (HMENU)(UINT_PTR)kLinkCoffee, nullptr, nullptr);
        ok_ = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                            0, 0, 0, 0, hwnd_, (HMENU)(UINT_PTR)IDOK, nullptr, nullptr);
        SendMessageW(licenseLink_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
        SendMessageW(link_, WM_SETFONT, (WPARAM)uiFont_, TRUE);
        ui::EnableHover(ok_);
        return 0;

    case WM_SIZE: layout(); return 0;

    case WM_DPICHANGED: {
        // The process is PerMonitorV2, so dragging this window to a monitor with a
        // different scaling rescales the frame. Without this the font, icon and
        // control positions stayed at the DPI the window was opened at.
        dpi_ = HIWORD(wp);
        applyDpiAssets();
        const RECT* p = (const RECT*)lp;
        SetWindowPos(hwnd_, nullptr, p->left, p->top, p->right - p->left, p->bottom - p->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    }

    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd_, &rc);
        FillRect((HDC)wp, &rc, brush_);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd_, &ps);
        paint(hdc);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_DRAWITEM: {
        auto* d = (DRAWITEMSTRUCT*)lp;
        if (d->CtlID != IDOK) return TRUE;
        bool active = ui::IsHot(d->hwndItem) || (d->itemState & ODS_SELECTED);
        HBRUSH b = CreateSolidBrush(active ? theme_.rowAlt : theme_.panel);
        FillRect(d->hDC, &d->rcItem, b); DeleteObject(b);
        HBRUSH bd = CreateSolidBrush(theme_.foreDim);
        FrameRect(d->hDC, &d->rcItem, bd); DeleteObject(bd);
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, theme_.fore);
        HFONT of = (HFONT)SelectObject(d->hDC, uiFont_);
        DrawTextW(d->hDC, L"OK", -1, &d->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(d->hDC, of);
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, theme_.foreDim);
        return (LRESULT)brush_;
    }
    case WM_NOTIFY: {
        auto* n = (NMHDR*)lp;
        if (n->code == NM_CLICK || n->code == NM_RETURN) {
            if (n->idFrom == kLinkCoffee)
                ShellExecuteW(nullptr, L"open", app::CoffeeUrl, nullptr, nullptr, SW_SHOWNORMAL);
            else if (n->idFrom == kLinkLicense)
                ShellExecuteW(nullptr, L"open", (proc::ExeDir() + L"NOTICE.txt").c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
        }
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) DestroyWindow(hwnd_);
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd_); return 0;
    case WM_DESTROY:
        if (icon_) DestroyIcon(icon_);
        if (brush_) DeleteObject(brush_);
        if (uiFont_) DeleteObject(uiFont_);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
