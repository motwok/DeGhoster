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

#pragma once
#include <windows.h>
#include <functional>
#include <unordered_set>

// The "ghost cursor" for AnyDesk sessions (Specification.md section 10): a
// click-through, topmost, per-pixel-alpha window that shows an enlarged copy of
// the remote cursor AnyDesk sets on its session window, while the small original
// is hidden with the Magnification API. Driven by out-of-context WinEvents for
// OBJID_CURSOR on the UI thread; no polling, no injection. One instance per process.
class CursorOverlay {
public:
    CursorOverlay() = default;
    ~CursorOverlay();
    CursorOverlay(const CursorOverlay&) = delete;
    CursorOverlay& operator=(const CursorOverlay&) = delete;

    bool create(HINSTANCE);
    // Hides the overlay, restores the system cursor, unhooks and releases everything.
    void destroy();

    // The global switch. Off: hidden, cursor restored, events unhooked.
    void setEnabled(bool on);
    // Decides per AnyDesk window (the root window under the cursor) whether its
    // cursor is enlarged: the window's own on/off in the status list. Without a
    // filter every AnyDesk window qualifies.
    void setWindowFilter(std::function<bool(HWND root)> filter) { filter_ = std::move(filter); }
    // Re-evaluates now, e.g. after a window was switched on or off.
    void refresh();
    // Zoom in percent; takes effect immediately (live preview while over AnyDesk).
    void setZoom(int percent);
    // Session lock/unlock: hide and restore while locked; after unlock the next
    // cursor event decides again.
    void setSessionLocked(bool locked);

    bool visible() const { return visible_; }

    // True for AnyDesk's session window class: "ad_win", with AnyDesk's running
    // "#<n>" suffix (and anything after it) ignored.
    static bool IsAnyDeskClass(const wchar_t* cls);

private:
    static void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD event, HWND, LONG idObject,
                                    LONG idChild, DWORD thread, DWORD time);
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

    bool active() const { return enabled_ && !locked_ && wnd_; }
    void hook(bool on);
    void evaluate(bool shapeChanged);
    bool render(HCURSOR cursor, UINT dpi);
    void place(POINT cursorPos);
    void hide();
    void hideSystemCursor(bool hide);

    HWND wnd_ = nullptr;
    HWINEVENTHOOK hook_ = nullptr;
    bool enabled_ = false, locked_ = false;
    int zoom_ = 100;

    // What the overlay currently shows; a change of any of them means re-render.
    HCURSOR shownCursor_ = nullptr;
    int shownZoom_ = 0;
    UINT shownDpi_ = 0;
    POINT hot_{};              // scaled hotspot of the shown image
    bool visible_ = false;

    std::function<bool(HWND)> filter_;
    bool magInit_ = false, cursorHidden_ = false;
    std::unordered_set<HCURSOR> systemCursors_;

    static CursorOverlay* s_instance;
};
