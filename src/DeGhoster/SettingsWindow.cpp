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

#include "SettingsWindow.h"
#include "Controls.h"
#include "HoverButton.h"
#include "Loc.h"
#include "resource.h"

#include <dwmapi.h>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

namespace {
constexpr wchar_t kClass[] = L"DeGhosterSettingsWindow";
constexpr DWORD kStyle = WS_CAPTION | WS_SYSMENU;
constexpr DWORD kExStyleBase = WS_EX_DLGMODALFRAME;

UINT TextFlags() { return DT_NOPREFIX | (loc::isRtl() ? DT_RTLREADING : 0); }
}

HWND SettingsWindow::s_active = nullptr;

HWND SettingsWindow::ActiveHandle() { return s_active; }

void SettingsWindow::Sync()
{
    if (!s_active) return;
    if (auto* self = (SettingsWindow*)GetWindowLongPtrW(s_active, GWLP_USERDATA)) self->sync();
}

void SettingsWindow::Show(HINSTANCE inst, HWND owner, const Theme& theme, UINT dpi, Settings& settings)
{
    if (s_active && IsWindow(s_active)) {
        ShowWindow(s_active, SW_SHOWNORMAL);
        SetForegroundWindow(s_active);
        return;
    }

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
    ui::RegisterControls(inst);

    auto* self = new SettingsWindow(settings);
    self->theme_ = theme;
    self->dpi_ = dpi;
    self->owner_ = owner;
    self->applyDpiAssets();
    const SIZE client = self->measure();

    RECT wr{ 0, 0, client.cx, client.cy };
    AdjustWindowRectExForDpi(&wr, kStyle, FALSE, kExStyleBase, dpi);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;

    // Same placement as the About window: centred on a visible owner, otherwise
    // on the owner's monitor, and always fully inside that monitor's work area.
    HMONITOR mon = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    RECT area = mi.rcWork, o;
    if (!IsIconic(owner) && IsWindowVisible(owner) && GetWindowRect(owner, &o) &&
        o.right > o.left && o.left > -30000)
        area = o;
    int x = area.left + ((area.right - area.left) - ww) / 2;
    int y = area.top + ((area.bottom - area.top) - wh) / 2;
    if (x + ww > mi.rcWork.right)  x = mi.rcWork.right - ww;
    if (y + wh > mi.rcWork.bottom) y = mi.rcWork.bottom - wh;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top)  y = mi.rcWork.top;

    DWORD ex = kExStyleBase | (loc::isRtl() ? WS_EX_LAYOUTRTL : 0);
    HWND h = CreateWindowExW(ex, kClass, loc::t(IDS_SETTINGS_TITLE), kStyle,
                             x, y, ww, wh, owner, nullptr, inst, self);
    if (!h) { delete self; return; }

    ApplyDarkTitleBar(h, theme.dark);
    int corner = 2;   // DWMWCP_ROUND (Win11; ignored on Win10)
    DwmSetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
    SetFocus(IsWindowEnabled(self->toggle_) ? self->toggle_ : self->close_);
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    SettingsWindow* self;
    if (msg == WM_NCCREATE) {
        self = static_cast<SettingsWindow*>(((CREATESTRUCTW*)lp)->lpCreateParams);
        self->hwnd_ = h;
        s_active = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (SettingsWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    }
    if (!self) return DefWindowProcW(h, msg, wp, lp);

    if (msg == WM_NCDESTROY) {
        LRESULT r = self->handle(msg, wp, lp);
        s_active = nullptr;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        delete self;
        return r;
    }
    return self->handle(msg, wp, lp);
}

void SettingsWindow::applyDpiAssets()
{
    if (uiFont_) DeleteObject(uiFont_);
    if (titleFont_) DeleteObject(titleFont_);
    uiFont_ = CreateFontW(-MulDiv(9, dpi_, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    titleFont_ = CreateFontW(-MulDiv(11, dpi_, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    for (HWND c : { toggle_, zoom_, close_ })
        if (c) SendMessageW(c, WM_SETFONT, (WPARAM)uiFont_, TRUE);
}

void SettingsWindow::applyTheme()
{
    if (brush_) DeleteObject(brush_);
    brush_ = CreateSolidBrush(theme_.back);
    for (HWND c : { toggle_, zoom_ })
        if (c) SendMessageW(c, ui::CTL_SETTHEME, 0, (LPARAM)&theme_);
    if (hwnd_) {
        ApplyDarkTitleBar(hwnd_, theme_.dark);
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

SIZE SettingsWindow::measure()
{
    HDC dc = GetDC(nullptr);
    auto extent = [&](HFONT f, const wchar_t* t) {
        HFONT of = (HFONT)SelectObject(dc, f);
        RECT r{ 0, 0, 0, 0 };
        DrawTextW(dc, t, -1, &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, of);
        return SIZE{ r.right - r.left, r.bottom - r.top };
    };
    const SIZE section = extent(titleFont_, loc::t(IDS_SETTINGS_CURSOR_SECTION));
    const SIZE toggle  = extent(uiFont_, loc::t(IDS_CURSOR_ENLARGE));
    const SIZE zoom    = extent(uiFont_, loc::t(IDS_SETTINGS_ZOOM));
    const SIZE close   = extent(uiFont_, loc::t(IDS_SETTINGS_CLOSE));

    // The content grows with long translations, but never past the monitor.
    const int pad = S(24);
    int content = std::max({ S(380), (int)section.cx, S(40 + 3 + 10 + 12) + (int)toggle.cx, (int)zoom.cx });
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(MonitorFromWindow(hwnd_ ? hwnd_ : owner_, MONITOR_DEFAULTTONEAREST), &mi);
    content = std::min(content, (int)(mi.rcWork.right - mi.rcWork.left) - 2 * pad - S(40));

    int y = S(20);
    rcSection_ = { pad, y, pad + content, y + section.cy };
    y += section.cy + S(12);
    const int toggleH = std::max(S(32), (int)toggle.cy + S(8));
    rcToggle_ = { pad, y, pad + content, y + toggleH };
    y += toggleH + S(12);
    rcZoomLabel_ = { pad, y, pad + content, y + zoom.cy };
    y += zoom.cy + S(4);
    rcZoom_ = { pad, y, pad + content, y + S(32) };
    y += S(32) + S(12);

    HFONT of = (HFONT)SelectObject(dc, uiFont_);
    RECT hint{ 0, 0, content, 0 };
    DrawTextW(dc, loc::t(IDS_SETTINGS_HINT), -1, &hint, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, of);
    rcHint_ = { pad, y, pad + content, y + (hint.bottom - hint.top) };
    y += (hint.bottom - hint.top) + S(20);

    const int closeW = std::max(S(88), (int)close.cx + S(24));
    rcClose_ = { pad + content - closeW, y, pad + content, y + S(32) };
    y += S(32) + S(20);

    ReleaseDC(nullptr, dc);
    return SIZE{ content + 2 * pad, y };
}

void SettingsWindow::layout()
{
    auto move = [](HWND c, const RECT& r) {
        if (c) MoveWindow(c, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
    };
    move(toggle_, rcToggle_);
    move(zoom_, rcZoom_);
    move(close_, rcClose_);
}

void SettingsWindow::createControls()
{
    HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE);
    toggle_ = CreateWindowExW(0, ui::kToggleClass, loc::t(IDS_CURSOR_ENLARGE),
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0,
                              hwnd_, (HMENU)(UINT_PTR)IDC_CURSOR_TOGGLE, inst, nullptr);
    zoom_ = CreateWindowExW(0, ui::kSliderClass, loc::t(IDS_SETTINGS_ZOOM),
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0,
                            hwnd_, (HMENU)(UINT_PTR)IDC_CURSOR_ZOOM, inst, nullptr);
    close_ = CreateWindowW(L"BUTTON", loc::t(IDS_SETTINGS_CLOSE),
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0,
                           hwnd_, (HMENU)(UINT_PTR)IDC_CLOSE, inst, nullptr);
    SendMessageW(zoom_, ui::SLM_SETRANGE, Settings::kZoomMin, Settings::kZoomMax);
    SendMessageW(zoom_, ui::SLM_SETSTEP, Settings::kZoomStep, 5 * Settings::kZoomStep);
    ui::EnableHover(close_);
    applyDpiAssets();
    applyTheme();
    sync();
    layout();
}

void SettingsWindow::sync()
{
    const bool global = settings_.globalEnabled();
    const bool on = settings_.cursorOverlayEnabled();
    SendMessageW(toggle_, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(zoom_, ui::SLM_SETPOS, settings_.cursorOverlayZoom(), 0);
    // Global off greys out the whole section; feature off only the zoom.
    HWND focus = GetFocus();
    EnableWindow(toggle_, global);
    EnableWindow(zoom_, global && on);
    if (focus && !IsWindowEnabled(focus)) SetFocus(close_);
    InvalidateRect(hwnd_, &rcZoomLabel_, TRUE);
}

void SettingsWindow::paint(HDC hdc)
{
    RECT rc; GetClientRect(hwnd_, &rc);
    FillRect(hdc, &rc, brush_);
    SetBkMode(hdc, TRANSPARENT);

    HFONT old = (HFONT)SelectObject(hdc, titleFont_);
    SetTextColor(hdc, theme_.fore);
    RECT r = rcSection_;
    DrawTextW(hdc, loc::t(IDS_SETTINGS_CURSOR_SECTION), -1, &r, DT_LEFT | DT_SINGLELINE | TextFlags());

    SelectObject(hdc, uiFont_);
    SetTextColor(hdc, IsWindowEnabled(zoom_) ? theme_.fore : theme_.foreDim);
    r = rcZoomLabel_;
    DrawTextW(hdc, loc::t(IDS_SETTINGS_ZOOM), -1, &r, DT_LEFT | DT_SINGLELINE | TextFlags());

    SetTextColor(hdc, theme_.foreDim);
    r = rcHint_;
    DrawTextW(hdc, loc::t(IDS_SETTINGS_HINT), -1, &r, DT_LEFT | DT_WORDBREAK | TextFlags());
    SelectObject(hdc, old);
}

void SettingsWindow::drawCloseButton(const DRAWITEMSTRUCT* d)
{
    bool active = ui::IsHot(d->hwndItem) || (d->itemState & ODS_SELECTED);
    HBRUSH b = CreateSolidBrush(active ? theme_.rowAlt : theme_.panel);
    FillRect(d->hDC, &d->rcItem, b); DeleteObject(b);
    HBRUSH bd = CreateSolidBrush(theme_.foreDim);
    FrameRect(d->hDC, &d->rcItem, bd); DeleteObject(bd);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, theme_.fore);
    HFONT of = (HFONT)SelectObject(d->hDC, uiFont_);
    RECT r = d->rcItem;
    DrawTextW(d->hDC, loc::t(IDS_SETTINGS_CLOSE), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | TextFlags());
    if ((d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT)) {
        RECT fr = d->rcItem;
        InflateRect(&fr, -S(3), -S(3));
        DrawFocusRect(d->hDC, &fr);
    }
    SelectObject(d->hDC, of);
}

LRESULT SettingsWindow::handle(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        createControls();
        return 0;

    case WM_DPICHANGED: {
        // Per-monitor v2: fonts, control sizes and the window itself follow the
        // new scaling. The suggested rect only supplies the position; the size is
        // measured again, since text does not scale exactly linearly.
        dpi_ = HIWORD(wp);
        applyDpiAssets();
        const SIZE client = measure();
        RECT wr{ 0, 0, client.cx, client.cy };
        AdjustWindowRectExForDpi(&wr, kStyle, FALSE, (DWORD)GetWindowLongW(hwnd_, GWL_EXSTYLE), dpi_);
        const RECT* p = (const RECT*)lp;
        SetWindowPos(hwnd_, nullptr, p->left, p->top, wr.right - wr.left, wr.bottom - wr.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    }

    case WM_SETTINGCHANGE:
        if (lp && lstrcmpiW((LPCWSTR)lp, L"ImmersiveColorSet") == 0) {
            theme_ = Theme::current();
            applyTheme();
        }
        return 0;

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
        if (d->CtlID == IDC_CLOSE) drawCloseButton(d);
        return TRUE;
    }

    case WM_HSCROLL:
        if ((HWND)lp == zoom_) {
            settings_.setCursorOverlayZoom((int)SendMessageW(zoom_, ui::SLM_GETPOS, 0, 0));
            SendMessageW(owner_, WM_SETTINGS_CHANGED, 0, 0);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_CURSOR_TOGGLE:
            settings_.setCursorOverlayEnabled(SendMessageW(toggle_, BM_GETCHECK, 0, 0) == BST_CHECKED);
            sync();
            SendMessageW(owner_, WM_SETTINGS_CHANGED, 0, 0);
            return 0;
        case IDC_CLOSE:
        case IDOK:
        case IDCANCEL:   // Esc, via IsDialogMessage
            DestroyWindow(hwnd_);
            return 0;
        }
        return 0;

    case WM_CLOSE: DestroyWindow(hwnd_); return 0;
    case WM_DESTROY:
        if (brush_) { DeleteObject(brush_); brush_ = nullptr; }
        if (uiFont_) { DeleteObject(uiFont_); uiFont_ = nullptr; }
        if (titleFont_) { DeleteObject(titleFont_); titleFont_ = nullptr; }
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
