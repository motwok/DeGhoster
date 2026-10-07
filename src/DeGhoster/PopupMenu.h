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
#include <oleacc.h>   // MSAAMENUINFO
#include <uxtheme.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Owner-drawn popup menu (the tray menu). It paints with the OS menu visual
// style - its dark variant when the apps theme is dark, the plain one under
// high contrast - using the system menu font and metrics at the DPI of the
// monitor it opens on, so it looks like a native menu in either theme.
//
// Toggle items flip in place: clicking one (or pressing Enter on it) runs the
// toggle callback and leaves the menu open. The menu only closes when a command
// is chosen, on Esc, or when it loses the focus (a click elsewhere, another
// window taking the foreground).
class PopupMenu {
public:
    PopupMenu() = default;
    ~PopupMenu();
    PopupMenu(const PopupMenu&) = delete;
    PopupMenu& operator=(const PopupMenu&) = delete;

    void addCommand(UINT id, const wchar_t* text, bool isDefault = false);
    // literal: the text is shown as is ('&' is no mnemonic), for window titles.
    // altText: a second label setToggle() may switch to; the width covers both.
    void addToggle(UINT id, const std::wstring& text, bool checked,
                   bool literal = false, const std::wstring& altText = {});
    void addNote(const wchar_t* text);   // greyed, not selectable
    void addSeparator();

    // Updates a toggle while the menu is open (from the toggle callback).
    void setToggle(UINT id, bool checked, const wchar_t* text = nullptr);

    // Shows the menu modally at pt (screen coordinates) for the owner window.
    // onToggle fires for each toggle item flipped while the menu is open.
    // Returns the chosen command id, or 0 when the menu was dismissed.
    UINT track(HWND owner, bool dark, POINT pt, const std::function<void(UINT id)>& onToggle);

    // The owner window's WM_MEASUREITEM / WM_DRAWITEM / WM_MENUCHAR /
    // WM_ENTERIDLE belong to the open menu; the owner forwards them here first.
    static bool OwnerMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result);

private:
    enum class Kind { Command, Toggle, Note, Separator };
    struct Item {
        MSAAMENUINFO msaa;   // must stay first: screen readers read the name from it
        UINT id = 0;
        Kind kind = Kind::Command;
        bool literal = false;
        bool checked = false;
        bool isDefault = false;
        std::wstring text, alt;
    };

    void add(Kind, UINT id, const std::wstring& text, bool checked, bool literal,
             bool isDefault, const std::wstring& alt);
    void openTheme(HWND owner, bool dark);
    void measure(MEASUREITEMSTRUCT*);
    void draw(const DRAWITEMSTRUCT*);
    void drawFlat(const DRAWITEMSTRUCT*, const Item&);
    LRESULT menuChar(wchar_t ch);
    bool filter(const MSG&);   // true: the message was a toggle and is consumed
    int hotItem() const;
    void adoptMenuWindow(HWND);
    void paintFrame(HDC dc);

    static LRESULT CALLBACK FilterProc(int code, WPARAM, LPARAM);
    static LRESULT CALLBACK CbtProc(int code, WPARAM, LPARAM);
    static LRESULT CALLBACK MenuWndProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

    std::vector<std::unique_ptr<Item>> items_;
    HMENU menu_ = nullptr;
    HWND menuWnd_ = nullptr;
    HTHEME theme_ = nullptr;   // null: visual styles are off, draw in system colours
    UINT dpi_ = 96;
    HFONT font_ = nullptr, bold_ = nullptr;
    HBRUSH back_ = nullptr;    // the style's menu background, for the frame and the margins
    HHOOK filterHook_ = nullptr, cbtHook_ = nullptr;
    std::function<void(UINT)> onToggle_;

    // Item layout, already scaled to dpi_.
    SIZE check_{};
    int sepHeight_ = 0, arrowWidth_ = 0;
    MARGINS mCheck_{}, mCheckBg_{}, mItem_{}, mText_{};

    static PopupMenu* current_;   // the open menu; the UI is single-threaded
};
