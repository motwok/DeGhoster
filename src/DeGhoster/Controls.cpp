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

#include "Controls.h"
#include "Gfx.h"

#include <windowsx.h>   // GET_X_LPARAM
#include <algorithm>
#include <string>

// Geometry and mouse positions are in the window's logical coordinates, where
// x = 0 is the leading edge (the right one under a mirrored, RTL parent). GDI+
// ignores the DC's mirroring, though, so painting switches the DC to physical
// coordinates (SetLayout 0) and mirrors every rectangle itself via Painter.

namespace ui {

namespace {

struct State {
    Theme theme;
    HFONT font = nullptr;
    bool checked = false;              // toggle
    int min = 0, max = 100, pos = 0;   // slider
    int step = 1, page = 10;
    bool dragging = false;
};

State* Get(HWND h) { return (State*)GetWindowLongPtrW(h, GWLP_USERDATA); }

int S(HWND h, int px) { return MulDiv(px, (int)GetDpiForWindow(h), 96); }

bool IsRtl(HWND h)
{
    return (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) ||
           (GetParent(h) && (GetWindowLongW(GetParent(h), GWL_EXSTYLE) & WS_EX_LAYOUTRTL));
}

void Fill(HDC dc, const RECT& rc, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &rc, b);
    DeleteObject(b);
}

// Maps logical rectangles to physical ones for a DC that painting switched to
// SetLayout(0), and flips the text alignment to match.
struct Painter {
    HDC dc;
    RECT client;
    bool rtl;

    Painter(HWND h, HDC d) : dc(d)
    {
        GetClientRect(h, &client);
        rtl = (GetLayout(d) & LAYOUT_RTL) != 0;
        SetLayout(d, 0);
    }
    RECT operator()(const RECT& r) const
    {
        if (!rtl) return r;
        return RECT{ client.right - r.right, r.top, client.right - r.left, r.bottom };
    }
    // DT_LEFT / DT_RIGHT given for the LTR case.
    UINT align(UINT ltr) const
    {
        if (!rtl) return ltr;
        return ((ltr & DT_RIGHT) ? DT_LEFT : DT_RIGHT) | DT_RTLREADING;
    }
};

// Focus rectangles follow the keyboard cues, like the standard controls: hidden
// after a mouse click, shown once the keyboard is used.
bool ShowFocus(HWND h)
{
    return GetFocus() == h && !(SendMessageW(h, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS);
}

// Shared lifetime/theme/font/focus handling; returns true if handled.
bool Common(HWND h, UINT msg, WPARAM wp, LPARAM lp, LRESULT& r)
{
    State* s = Get(h);
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)new State());
        r = DefWindowProcW(h, msg, wp, lp);
        return true;
    case WM_NCDESTROY:
        delete s;
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        r = DefWindowProcW(h, msg, wp, lp);
        return true;
    case CTL_SETTHEME:
        if (s && lp) s->theme = *(const Theme*)lp;
        InvalidateRect(h, nullptr, TRUE);
        r = 0;
        return true;
    case WM_SETFONT:
        if (s) s->font = (HFONT)wp;
        if (LOWORD(lp)) InvalidateRect(h, nullptr, TRUE);
        r = 0;
        return true;
    case WM_GETFONT:
        r = s ? (LRESULT)s->font : 0;
        return true;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
    case WM_SETTEXT:
    case WM_UPDATEUISTATE:
        r = DefWindowProcW(h, msg, wp, lp);
        InvalidateRect(h, nullptr, TRUE);
        return true;
    case WM_ERASEBKGND:
        r = 1;   // WM_PAINT covers every pixel
        return true;
    }
    return false;
}

// ---- toggle -----------------------------------------------------------------

void PaintToggle(HWND h, HDC dc)
{
    State* s = Get(h);
    Painter p(h, dc);
    const RECT rc = p.client;
    Fill(dc, rc, s->theme.back);

    const bool enabled = IsWindowEnabled(h) != FALSE;
    const int pw = S(h, 40), ph = S(h, 20), inset = S(h, 3), gap = S(h, 4);
    RECT pill{ rc.left + inset, (rc.top + rc.bottom - ph) / 2, rc.left + inset + pw, (rc.top + rc.bottom + ph) / 2 };
    const int kd = ph - 2 * gap;   // knob diameter
    const int ky = pill.top + gap;
    if (s->checked) {
        gfx::FillPill(dc, p(pill), enabled ? s->theme.accentOn : s->theme.accentOff);
        RECT k{ pill.right - gap - kd, ky, pill.right - gap, ky + kd };
        gfx::FillCircle(dc, p(k), 255, RGB(255, 255, 255));
    } else {
        gfx::StrokePill(dc, p(pill), s->theme.foreDim, std::max(1.0f, GetDpiForWindow(h) / 96.0f * 1.5f));
        RECT k{ pill.left + gap, ky, pill.left + gap + kd, ky + kd };
        gfx::FillCircle(dc, p(k), 255, s->theme.foreDim);
    }

    wchar_t text[256] = L"";
    GetWindowTextW(h, text, ARRAYSIZE(text));
    HFONT of = (HFONT)SelectObject(dc, s->font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, enabled ? s->theme.fore : s->theme.foreDim);
    RECT tr = p(RECT{ pill.right + S(h, 10), rc.top, rc.right - inset, rc.bottom });
    DrawTextW(dc, text, -1, &tr, p.align(DT_LEFT) | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, of);

    if (ShowFocus(h)) {
        RECT fr = rc;
        InflateRect(&fr, -1, -1);
        SetTextColor(dc, s->theme.fore);
        DrawFocusRect(dc, &fr);
    }
}

void Flip(HWND h)
{
    State* s = Get(h);
    s->checked = !s->checked;
    InvalidateRect(h, nullptr, TRUE);
    SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(h), BN_CLICKED), (LPARAM)h);
}

LRESULT CALLBACK ToggleProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT r;
    if (Common(h, msg, wp, lp, r)) return r;
    State* s = Get(h);
    if (!s) return DefWindowProcW(h, msg, wp, lp);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        PaintToggle(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case BM_GETCHECK: return s->checked ? BST_CHECKED : BST_UNCHECKED;
    case BM_SETCHECK:
        s->checked = wp == BST_CHECKED;
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    case WM_GETDLGCODE: return DLGC_WANTCHARS;
    case WM_LBUTTONDOWN:
        SetFocus(h);
        SetCapture(h);
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == h) {
            ReleaseCapture();
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            RECT rc; GetClientRect(h, &rc);
            if (PtInRect(&rc, pt)) Flip(h);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_SPACE) { Flip(h); return 0; }
        break;
    case WM_CHAR:
        if (wp == L' ') return 0;   // handled on key-down; no beep
        break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---- slider -----------------------------------------------------------------

struct SliderGeometry { RECT track; RECT value; int thumb; };

SliderGeometry Geometry(HWND h, const State* s)
{
    RECT rc; GetClientRect(h, &rc);
    const int labelW = S(h, 60), pad = S(h, 12);
    SliderGeometry g{};
    g.value = RECT{ rc.right - labelW, rc.top, rc.right - S(h, 4), rc.bottom };
    g.track = RECT{ rc.left + pad, rc.top, std::max(rc.left + pad + 1, rc.right - labelW - pad), rc.bottom };
    const int span = std::max(1, s->max - s->min);
    g.thumb = g.track.left + MulDiv(s->pos - s->min, g.track.right - g.track.left, span);
    return g;
}

void Notify(HWND h)
{
    SendMessageW(GetParent(h), WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, Get(h)->pos), (LPARAM)h);
}

void SetPosFromUser(HWND h, int v)
{
    State* s = Get(h);
    // Snap to the step grid, measured from the minimum.
    if (s->step > 1) v = s->min + (v - s->min + (v >= s->min ? s->step / 2 : -s->step / 2)) / s->step * s->step;
    v = std::clamp(v, s->min, s->max);
    if (v == s->pos) return;
    s->pos = v;
    InvalidateRect(h, nullptr, TRUE);
    Notify(h);
}

int PosFromX(HWND h, int x)
{
    State* s = Get(h);
    SliderGeometry g = Geometry(h, s);
    const int w = std::max(1, (int)(g.track.right - g.track.left));
    const int dx = std::clamp(x - (int)g.track.left, 0, w);
    return s->min + MulDiv(dx, s->max - s->min, w);
}

void PaintSlider(HWND h, HDC dc)
{
    State* s = Get(h);
    Painter p(h, dc);
    const RECT rc = p.client;
    Fill(dc, rc, s->theme.back);

    const bool enabled = IsWindowEnabled(h) != FALSE;
    SliderGeometry g = Geometry(h, s);
    const int cy = (rc.top + rc.bottom) / 2, th = S(h, 4);
    RECT full{ g.track.left, cy - th / 2, g.track.right, cy - th / 2 + th };
    gfx::FillPill(dc, p(full), s->theme.dark ? RGB(80, 80, 84) : RGB(200, 200, 204));
    RECT done{ g.track.left, full.top, std::max(g.thumb, (int)g.track.left + th), full.bottom };
    gfx::FillPill(dc, p(done), enabled ? s->theme.accentOn : s->theme.accentOff);
    const int d = S(h, 18);
    RECT t{ g.thumb - d / 2, cy - d / 2, g.thumb - d / 2 + d, cy - d / 2 + d };
    gfx::FillCircle(dc, p(t), 255, enabled ? s->theme.accentOn : s->theme.accentOff);
    InflateRect(&t, -S(h, 5), -S(h, 5));
    gfx::FillCircle(dc, p(t), 255, RGB(255, 255, 255));

    wchar_t text[32];
    wsprintfW(text, L"%d %%", s->pos);
    HFONT of = (HFONT)SelectObject(dc, s->font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, enabled ? s->theme.fore : s->theme.foreDim);
    RECT vr = p(g.value);
    DrawTextW(dc, text, -1, &vr, p.align(DT_RIGHT) | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, of);

    if (ShowFocus(h)) {
        RECT fr = rc;
        InflateRect(&fr, -1, -1);
        SetTextColor(dc, s->theme.fore);
        DrawFocusRect(dc, &fr);
    }
}

LRESULT CALLBACK SliderProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT r;
    if (Common(h, msg, wp, lp, r)) return r;
    State* s = Get(h);
    if (!s) return DefWindowProcW(h, msg, wp, lp);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        PaintSlider(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case SLM_SETRANGE:
        s->min = (int)wp; s->max = std::max((int)wp, (int)lp);
        s->pos = std::clamp(s->pos, s->min, s->max);
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    case SLM_SETSTEP:
        s->step = std::max(1, (int)wp); s->page = std::max(1, (int)lp);
        return 0;
    case SLM_SETPOS:
        s->pos = std::clamp((int)wp, s->min, s->max);
        InvalidateRect(h, nullptr, TRUE);
        return 0;
    case SLM_GETPOS: return s->pos;
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case WM_KEYDOWN: {
        // Left/right follow the visual direction, which is mirrored under RTL.
        const int dir = IsRtl(h) ? -1 : 1;
        switch (wp) {
        case VK_LEFT:  SetPosFromUser(h, s->pos - dir * s->step); return 0;
        case VK_RIGHT: SetPosFromUser(h, s->pos + dir * s->step); return 0;
        case VK_DOWN:  SetPosFromUser(h, s->pos - s->step); return 0;
        case VK_UP:    SetPosFromUser(h, s->pos + s->step); return 0;
        case VK_NEXT:  SetPosFromUser(h, s->pos - s->page); return 0;
        case VK_PRIOR: SetPosFromUser(h, s->pos + s->page); return 0;
        case VK_HOME:  SetPosFromUser(h, s->min); return 0;
        case VK_END:   SetPosFromUser(h, s->max); return 0;
        }
        break;
    }
    case WM_LBUTTONDOWN:
        SetFocus(h);
        SetCapture(h);
        s->dragging = true;
        SetPosFromUser(h, PosFromX(h, GET_X_LPARAM(lp)));
        return 0;
    case WM_MOUSEMOVE:
        if (s->dragging) SetPosFromUser(h, PosFromX(h, GET_X_LPARAM(lp)));
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (s->dragging) {
            s->dragging = false;
            if (GetCapture() == h) ReleaseCapture();
        }
        return 0;
    case WM_MOUSEWHEEL:
        SetPosFromUser(h, s->pos + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? s->step : -s->step));
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace

void RegisterControls(HINSTANCE inst)
{
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpfnWndProc = ToggleProc;
    wc.lpszClassName = kToggleClass;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = SliderProc;
    wc.lpszClassName = kSliderClass;
    RegisterClassExW(&wc);
    done = true;
}

} // namespace ui
