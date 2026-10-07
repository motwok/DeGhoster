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

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace {
constexpr wchar_t kClass[] = L"DeGhosterCursorOverlay";
constexpr wchar_t kXorClass[] = L"DeGhosterCursorOverlayXor";
constexpr uint32_t kKey = 0x00FF00FFu;   // the XOR window's transparent colour (magenta)
constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT kRefreshMs = 50;   // 20 frames a second under a resting inverting cursor

// Every standard cursor id. AnyDesk's own UI (title bar, tabs, menus, settings)
// uses these; the remote cursor is always one the client created itself.
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

bool CursorOverlay::IsRdpInputChain(const wchar_t* cls, const wchar_t* parent, const wchar_t* grandparent)
{
    return cls && parent && grandparent && wcscmp(cls, L"IHWindowClass") == 0 &&
           wcscmp(parent, L"UIContainerClass") == 0 && wcscmp(grandparent, L"UIMainClass") == 0;
}

bool CursorOverlay::IsRdpInputWindow(HWND h)
{
    // GA_PARENT, not GetParent: the chain is one of child windows, and GetParent
    // would hand back the owner of a top-level window instead.
    wchar_t cls[3][32] = {};
    for (int i = 0; i < 3; ++i, h = GetAncestor(h, GA_PARENT))
        if (!h || !GetClassNameW(h, cls[i], ARRAYSIZE(cls[i]))) return false;
    return IsRdpInputChain(cls[0], cls[1], cls[2]);
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

    // Pixels that invert the screen go to a second window above the overlay: it
    // shows the screen under them inverted, so reading the screen must not see
    // that window itself. Only a colour-keyed layered window can be left out of
    // screen captures (WDA_EXCLUDEFROMCAPTURE, Windows 10 2004 and later; the
    // per-pixel-alpha overlay cannot), and inverting pixels need no partial alpha.
    // Where that fails, inverting pixels are drawn black with a white outline.
    wc.lpszClassName = kXorClass;
    RegisterClassExW(&wc);
    xorWnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                              WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                              kXorClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, nullptr);
    canInvert_ = xorWnd_ && SetLayeredWindowAttributes(xorWnd_, kKey, 0, LWA_COLORKEY) &&
                 SetWindowDisplayAffinity(xorWnd_, WDA_EXCLUDEFROMCAPTURE);
    if (!canInvert_ && xorWnd_) { DestroyWindow(xorWnd_); xorWnd_ = nullptr; }

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
    if (xorWnd_) { DestroyWindow(xorWnd_); xorWnd_ = nullptr; }
    if (xorDc_) { DeleteDC(xorDc_); xorDc_ = nullptr; }
    if (xorBmp_) { DeleteObject(xorBmp_); xorBmp_ = nullptr; }
    xorPx_ = nullptr;
    xorSize_ = {};
    canInvert_ = false;
    if (s_instance == this) s_instance = nullptr;
}

void CursorOverlay::setEnabled(bool on)
{
    enabled_ = on;
    hook(active());
    if (active()) evaluate(true);
    else hide();
}

void CursorOverlay::setAuto(bool on)
{
    auto_ = on;
    if (active()) evaluate(false);
}

void CursorOverlay::refresh()
{
    if (active()) evaluate(false);
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
    // Over an AnyDesk session window (its root has AnyDesk's class) or over the
    // input window of an RDP control, whatever program hosts it. Either way the
    // session window is the root: that is what the status list shows.
    HWND under = WindowFromPoint(pt);
    HWND root = under ? GetAncestor(under, GA_ROOT) : nullptr;
    wchar_t cls[64] = L"";
    if (!root) { hide(); return; }
    const bool anyDesk = GetClassNameW(root, cls, ARRAYSIZE(cls)) && IsAnyDeskClass(cls);
    if (!anyDesk && !IsRdpInputWindow(under)) { hide(); return; }
    if (filter_ && !filter_(root)) { hide(); return; }

    const UINT dpi = DpiAt(pt);
    const bool newShape = shapeChanged || ci.hCursor != shownCursor_;
    if (newShape || dpi != shownDpi_) {
        if (!cursorimg::Read(ci.hCursor, src_)) { hide(); return; }
        // Without a way to read the screen under itself, the overlay shows
        // inverting pixels black with a white outline instead.
        if (!canInvert_) cursorimg::AddOutline(src_, cursorimg::OutlineRadius(dpi));
        srcKey_ = cursorimg::Key(src_);
        srcHeight_ = cursorimg::VisibleHeight(src_);
    }
    const int zoom = effectiveZoom(root, dpi);
    if (newShape || zoom != shownZoom_ || dpi != shownDpi_) {
        if (!render(ci.hCursor, dpi, zoom)) { hide(); return; }
    }
    place(pt);
    // A cursor AnyDesk sets while the real one is hidden can get drawn once and
    // then stay frozen on screen (seen with the remote's resize cursors), until
    // something like a click refreshes it. Showing and hiding it again after
    // every new shape makes Windows drop that stale image.
    if (newShape && cursorHidden_) {
        MagShowSystemCursor(TRUE);
        MagShowSystemCursor(FALSE);
    }
}

int CursorOverlay::effectiveZoom(HWND root, UINT dpi)
{
    if (!auto_) return zoom_;
    // Forget windows that are gone, so the map only holds live sessions.
    if (!autos_.count(root))
        for (auto it = autos_.begin(); it != autos_.end();)
            it = IsWindow(it->first) ? std::next(it) : autos_.erase(it);
    AutoZoom& az = autos_[root];
    az.observe(srcKey_, srcHeight_, GetTickCount64());
    const int z = az.zoom(TargetHeight(dpi));
    return z ? z : zoom_;
}

// How tall the local arrow is on screen at `dpi`: the visible part of the system
// arrow's picture (which Windows hands a per-monitor aware process at its base
// size) scaled to the monitor, as measured in the proof of concept.
int CursorOverlay::TargetHeight(UINT dpi)
{
    auto it = targets_.find(dpi);
    if (it != targets_.end()) return it->second;
    cursorimg::Image arrow;
    int h = 0;
    if (cursorimg::Read(LoadCursorW(nullptr, IDC_ARROW), arrow))
        h = MulDiv(cursorimg::VisibleHeight(arrow), (int)dpi, 96);
    targets_[dpi] = h;
    return h;
}

bool CursorOverlay::render(HCURSOR cursor, UINT dpi, int zoom)
{
    shown_ = cursorimg::Scale(src_, zoom);
    POINT cur{}; GetCursorPos(&cur);
    if (!paint(cur)) return false;

    shownCursor_ = cursor;
    shownZoom_ = zoom;
    shownDpi_ = dpi;
    hot_ = shown_.hot;
    return true;
}

// Puts the shown picture at cursor position `cur`: the overlay gets its ordinary
// pixels (its inverting ones stay transparent), the XOR window the inverting ones.
bool CursorOverlay::paint(POINT cur)
{
    const cursorimg::Image& img = shown_;
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
        HGDIOBJ old = SelectObject(mem, dib);
        POINT dst{ cur.x - img.hot.x, cur.y - img.hot.y }, src0{ 0, 0 };
        std::memcpy(bits, img.px.data(), img.px.size() * sizeof(uint32_t));
        SIZE size{ img.w, img.h };
        BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        ok = UpdateLayeredWindow(wnd_, screen, &dst, &size, mem, &src0, 0, &blend, ULW_ALPHA) != FALSE;
        SelectObject(mem, old);
    }
    if (dib) DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    if (ok) paintXor(cur);
    return ok;
}

// The XOR window: the screen under the inverting pixels XOR their colour, the
// key colour everywhere else. The screen is read with the overlay in it (its
// inverting pixels are transparent there) but without this window, which is
// left out of captures.
void CursorOverlay::paintXor(POINT cur)
{
    if (!xorWnd_) return;
    const cursorimg::Image& img = shown_;
    if (!img.inverts()) { ShowWindow(xorWnd_, SW_HIDE); return; }

    if (!xorDc_ || xorSize_.cx != img.w || xorSize_.cy != img.h) {
        if (!xorDc_) xorDc_ = CreateCompatibleDC(nullptr);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = img.w;
        bi.bmiHeader.biHeight = -img.h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(xorDc_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp || !bits) {
            if (bmp) DeleteObject(bmp);
            ShowWindow(xorWnd_, SW_HIDE);
            return;
        }
        SelectObject(xorDc_, bmp);   // deselects the previous one, which can go now
        if (xorBmp_) DeleteObject(xorBmp_);
        xorBmp_ = bmp;
        xorPx_ = static_cast<uint32_t*>(bits);
        xorSize_ = { img.w, img.h };
    }

    const POINT dst{ cur.x - img.hot.x, cur.y - img.hot.y };
    HDC screen = GetDC(nullptr);
    BitBlt(xorDc_, 0, 0, img.w, img.h, screen, dst.x, dst.y, SRCCOPY);
    ReleaseDC(nullptr, screen);
    GdiFlush();
    cursorimg::XorLayer(img, xorPx_, xorPx_, kKey);

    // Above the overlay, at its place; the picture goes in right after the move.
    SetWindowPos(xorWnd_, HWND_TOPMOST, dst.x, dst.y, img.w, img.h,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOCOPYBITS | SWP_NOREDRAW | SWP_SHOWWINDOW);
    if (HDC dc = GetDC(xorWnd_)) {
        BitBlt(dc, 0, 0, img.w, img.h, xorDc_, 0, 0, SRCCOPY);
        ReleaseDC(xorWnd_, dc);
    }
    ValidateRect(xorWnd_, nullptr);
}

void CursorOverlay::place(POINT pt)
{
    // An inverting picture shows what is under it, which changes with every move
    // and, below a resting mouse, whenever the screen does (text being typed under
    // the I-beam). So it is recomposed on every move and on a short timer.
    if (shown_.inverts()) {
        paint(pt);
        SetTimer(wnd_, kRefreshTimer, kRefreshMs, nullptr);
    } else {
        KillTimer(wnd_, kRefreshTimer);
    }
    // HWND_TOPMOST on every move re-asserts the band: the taskbar is topmost too
    // and would otherwise clip the cursor at the bottom edge of the screen.
    SetWindowPos(wnd_, HWND_TOPMOST, pt.x - hot_.x, pt.y - hot_.y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    if (shown_.inverts() && xorWnd_)   // the XOR window stays above it
        SetWindowPos(xorWnd_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
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
    if (wnd_) KillTimer(wnd_, kRefreshTimer);
    if (xorWnd_) ShowWindow(xorWnd_, SW_HIDE);
    // Forget what the window holds: AnyDesk can destroy a cursor and get the same
    // handle value back for its next shape, so after a hide (a failed read among
    // them) an unchanged handle must not re-show the old picture unread.
    shownCursor_ = nullptr;
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
    if (msg == WM_PAINT && s_instance && h == s_instance->xorWnd_) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (s_instance->xorDc_)
            BitBlt(dc, 0, 0, s_instance->xorSize_.cx, s_instance->xorSize_.cy, s_instance->xorDc_, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_TIMER && wp == kRefreshTimer) {
        // Below a resting mouse the screen under an inverting cursor can change.
        if (s_instance && s_instance->visible_ && s_instance->shown_.inverts()) {
            POINT cur{};
            if (GetCursorPos(&cur)) s_instance->paint(cur);
        } else {
            KillTimer(h, kRefreshTimer);
        }
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
