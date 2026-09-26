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

#include "CursorOverlay.h"
#include "CursorImage.h"

#include <magnification.h>
#include <shellscalingapi.h>
#include <cstring>

#pragma comment(lib, "magnification.lib")
#pragma comment(lib, "shcore.lib")

namespace {
constexpr wchar_t kClass[] = L"DeGhosterCursorOverlay";

// Every standard cursor id. AnyDesk's own UI (title bar, tabs, menus, settings)
// uses these; the remote cursor is always one AnyDesk created itself.
constexpr int kSystemCursorIds[] = {
    32512 /*ARROW*/, 32513 /*IBEAM*/, 32514 /*WAIT*/, 32515 /*CROSS*/, 32516 /*UPARROW*/,
    32640 /*SIZE*/, 32641 /*ICON*/, 32642 /*SIZENWSE*/, 32643 /*SIZENESW*/, 32644 /*SIZEWE*/,
    32645 /*SIZENS*/, 32646 /*SIZEALL*/, 32648 /*NO*/, 32649 /*HAND*/, 32650 /*APPSTARTING*/,
    32651 /*HELP*/, 32671 /*PIN*/, 32672 /*PERSON*/,
};

UINT DpiAt(POINT pt)
{
    UINT x = 96, y = 96;
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (FAILED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &x, &y))) return 96;
    return x;
}
}

CursorOverlay* CursorOverlay::s_instance = nullptr;

CursorOverlay::~CursorOverlay() { destroy(); }

bool CursorOverlay::IsAnyDeskClass(const wchar_t* cls)
{
    if (!cls) return false;
    constexpr wchar_t kBase[] = L"ad_win";
    constexpr size_t n = ARRAYSIZE(kBase) - 1;
    return wcsncmp(cls, kBase, n) == 0 && (cls[n] == L'\0' || cls[n] == L'#');
}

bool CursorOverlay::create(HINSTANCE inst)
{
    if (wnd_) return true;

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    // Layered + transparent: invisible to hit-testing, so every click, and
    // WindowFromPoint, goes to the window underneath. No-activate + tool window:
    // never takes focus, never shows up in the taskbar or Alt+Tab.
    wnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                           WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                           kClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, nullptr);
    if (!wnd_) return false;

    for (int id : kSystemCursorIds)
        if (HCURSOR c = LoadCursorW(nullptr, MAKEINTRESOURCEW(id))) systemCursors_.insert(c);

    s_instance = this;
    return true;
}

void CursorOverlay::destroy()
{
    hook(false);
    hide();
    if (magInit_) { MagUninitialize(); magInit_ = false; }
    if (wnd_) { DestroyWindow(wnd_); wnd_ = nullptr; }
    if (s_instance == this) s_instance = nullptr;
}

void CursorOverlay::setEnabled(bool on)
{
    enabled_ = on;
    hook(active());
    if (active()) evaluate(true);
    else hide();
}

void CursorOverlay::setZoom(int percent)
{
    zoom_ = percent;
    if (active()) evaluate(false);   // a zoom change alone re-renders (shownZoom_ differs)
}

void CursorOverlay::setSessionLocked(bool locked)
{
    locked_ = locked;
    if (locked) hide();
    // Unlocked: nothing to do until the next cursor event, which arrives as soon
    // as the mouse moves on the user's desktop again.
}

void CursorOverlay::hook(bool on)
{
    if (on && !hook_) {
        // Out of context: the callback runs here, on the UI thread, from the
        // message loop. SHOW..NAMECHANGE covers show/hide (typing hides the
        // cursor), location (movement) and name (shape) changes; everything
        // that is not OBJID_CURSOR is dropped straight away in the callback.
        // Our own process is deliberately NOT skipped: a cursor event is raised
        // for whichever process the cursor is over, and missing those while the
        // mouse moves from AnyDesk onto a DeGhoster window would strand the
        // overlay there with the real cursor hidden.
        hook_ = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_NAMECHANGE, nullptr,
                                OnWinEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
    } else if (!on && hook_) {
        UnhookWinEvent(hook_);
        hook_ = nullptr;
    }
}

void CALLBACK CursorOverlay::OnWinEvent(HWINEVENTHOOK, DWORD event, HWND, LONG idObject,
                                        LONG, DWORD, DWORD)
{
    if (idObject != OBJID_CURSOR || !s_instance) return;
    switch (event) {
    case EVENT_OBJECT_SHOW:
    case EVENT_OBJECT_HIDE:
    case EVENT_OBJECT_LOCATIONCHANGE:
        s_instance->evaluate(false);
        break;
    case EVENT_OBJECT_NAMECHANGE:
        // A shape change. AnyDesk may destroy a cursor and get the same handle
        // value back for the next shape, so the handle alone cannot tell.
        s_instance->evaluate(true);
        break;
    }
}

void CursorOverlay::evaluate(bool shapeChanged)
{
    if (!active()) { hide(); return; }

    // Events are asynchronous and coalesced: always read the current state.
    // CURSOR_SHOWING stays set while WE hide the cursor with MagShowSystemCursor,
    // but it is cleared when someone else hides it (the Windows Magnifier in
    // full-screen mode draws its own cursor), and then the overlay must go.
    CURSORINFO ci{ sizeof(ci) };
    if (!GetCursorInfo(&ci) || !ci.hCursor || !(ci.flags & CURSOR_SHOWING)) { hide(); return; }
    if (systemCursors_.count(ci.hCursor)) { hide(); return; }

    POINT pt;
    if (!GetCursorPos(&pt)) pt = ci.ptScreenPos;
    HWND under = WindowFromPoint(pt);
    HWND root = under ? GetAncestor(under, GA_ROOT) : nullptr;
    wchar_t cls[64] = L"";
    if (!root || !GetClassNameW(root, cls, ARRAYSIZE(cls)) || !IsAnyDeskClass(cls)) { hide(); return; }

    const UINT dpi = DpiAt(pt);
    if (shapeChanged || ci.hCursor != shownCursor_ || zoom_ != shownZoom_ || dpi != shownDpi_) {
        if (!render(ci.hCursor, dpi)) { hide(); return; }
    }
    place(pt);
}

bool CursorOverlay::render(HCURSOR cursor, UINT dpi)
{
    cursorimg::Image src;
    if (!cursorimg::Read(cursor, dpi, src)) return false;
    const cursorimg::Image img = cursorimg::Scale(src, zoom_);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = img.w;
    bi.bmiHeader.biHeight = -img.h;   // top-down, like the image
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (dib && bits) {
        std::memcpy(bits, img.px.data(), img.px.size() * sizeof(uint32_t));
        HGDIOBJ old = SelectObject(mem, dib);
        POINT cur{}; GetCursorPos(&cur);
        POINT dst{ cur.x - img.hot.x, cur.y - img.hot.y }, src0{ 0, 0 };
        SIZE size{ img.w, img.h };
        // A size change through UpdateLayeredWindow alone left the part of the
        // old, larger image that the new one no longer covers on screen over
        // AnyDesk's surface (e.g. arrow -> resize cursor) until AnyDesk presented
        // its next frame. Resizing with SetWindowPos first, the same path every
        // move takes, gets that area recomposed.
        if (visible_ && (size.cx != size_.cx || size.cy != size_.cy))
            SetWindowPos(wnd_, HWND_TOPMOST, dst.x, dst.y, size.cx, size.cy,
                         SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        ok = UpdateLayeredWindow(wnd_, screen, &dst, &size, mem, &src0, 0, &blend, ULW_ALPHA) != FALSE;
        SelectObject(mem, old);
    }
    if (dib) DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    if (!ok) return false;

    shownCursor_ = cursor;
    shownZoom_ = zoom_;
    shownDpi_ = dpi;
    hot_ = img.hot;
    size_ = SIZE{ img.w, img.h };
    return true;
}

void CursorOverlay::place(POINT pt)
{
    // HWND_TOPMOST on every move re-asserts the band: the taskbar is topmost too
    // and would otherwise clip the cursor at the bottom edge of the screen.
    SetWindowPos(wnd_, HWND_TOPMOST, pt.x - hot_.x, pt.y - hot_.y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    if (!visible_) {
        ShowWindow(wnd_, SW_SHOWNOACTIVATE);
        visible_ = true;
    }
    hideSystemCursor(true);
}

void CursorOverlay::hide()
{
    if (visible_ && wnd_) ShowWindow(wnd_, SW_HIDE);
    visible_ = false;
    hideSystemCursor(false);
}

void CursorOverlay::hideSystemCursor(bool hideIt)
{
    if (hideIt == cursorHidden_) return;   // only on a state change
    if (hideIt && !magInit_) {
        magInit_ = MagInitialize() != FALSE;
        if (!magInit_) return;   // then the small cursor simply stays visible
    }
    // Only the result counts: GetLastError can be stale after a successful call.
    if (MagShowSystemCursor(hideIt ? FALSE : TRUE)) cursorHidden_ = hideIt;
}

LRESULT CALLBACK CursorOverlay::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(h, msg, wp, lp);
}
