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

#include "PopupMenu.h"
#include "Dpi.h"

#include <commctrl.h>
#include <shellscalingapi.h>   // GetDpiForMonitor
#include <vssym32.h>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "uxtheme.lib")

PopupMenu* PopupMenu::current_ = nullptr;

namespace {
constexpr UINT_PTR kFrameSubclassId = 1;
constexpr wchar_t kMenuWindowClass[] = L"#32768";

bool HighContrast()
{
    HIGHCONTRASTW hc{ sizeof(hc) };
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0)
        && (hc.dwFlags & HCF_HIGHCONTRASTON);
}

// A menu is laid out for the monitor it opens on, not for the owner window
// (which is usually hidden in the tray).
UINT MonitorDpi(POINT pt)
{
    UINT x = 96, y = 96;
    if (FAILED(GetDpiForMonitor(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &x, &y)))
        return 96;
    return x;
}

// The colour a theme part paints, sampled from its centre. The dark menu style
// has no fill colour property, only images.
COLORREF PartColor(HTHEME t, int part, int state)
{
    constexpr int kSide = 8;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = kSide;
    bi.bmiHeader.biHeight = -kSide;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    COLORREF c = RGB(43, 43, 43);
    if (bmp && bits) {
        HGDIOBJ old = SelectObject(dc, bmp);
        RECT rc{ 0, 0, kSide, kSide };
        if (SUCCEEDED(DrawThemeBackground(t, dc, part, state, &rc, nullptr))) {
            const BYTE* p = (const BYTE*)bits + ((kSide / 2) * kSide + kSide / 2) * 4;
            c = RGB(p[2], p[1], p[0]);
        }
        SelectObject(dc, old);
        DeleteObject(bmp);
    }
    DeleteDC(dc);
    return c;
}

MARGINS Scale(UINT dpi, MARGINS m)
{
    return { ::Scale(dpi, m.cxLeftWidth), ::Scale(dpi, m.cxRightWidth),
             ::Scale(dpi, m.cyTopHeight), ::Scale(dpi, m.cyBottomHeight) };
}
}

PopupMenu::~PopupMenu()
{
    if (menu_) DestroyMenu(menu_);
    if (theme_) CloseThemeData(theme_);
    if (font_) DeleteObject(font_);
    if (bold_) DeleteObject(bold_);
    if (back_) DeleteObject(back_);
}

void PopupMenu::add(Kind kind, UINT id, const std::wstring& text, bool checked, bool literal,
                    bool isDefault, const std::wstring& alt)
{
    auto it = std::make_unique<Item>();
    it->id = id;
    it->kind = kind;
    it->text = text;
    it->alt = alt;
    it->checked = checked;
    it->literal = literal;
    it->isDefault = isDefault;
    it->msaa.dwMSAASignature = MSAA_MENU_SIG;
    it->msaa.cchWText = (DWORD)it->text.size();
    it->msaa.pszWText = it->text.data();
    items_.push_back(std::move(it));
}

void PopupMenu::addCommand(UINT id, const wchar_t* text, bool isDefault)
{
    add(Kind::Command, id, text, false, false, isDefault, {});
}

void PopupMenu::addToggle(UINT id, const std::wstring& text, bool checked, bool literal,
                          const std::wstring& altText)
{
    add(Kind::Toggle, id, text, checked, literal, false, altText);
}

void PopupMenu::addNote(const wchar_t* text) { add(Kind::Note, 0, text, false, false, false, {}); }

void PopupMenu::addSeparator() { add(Kind::Separator, 0, {}, false, false, false, {}); }

void PopupMenu::setToggle(UINT id, bool checked, const wchar_t* text)
{
    for (size_t i = 0; i < items_.size(); ++i) {
        Item& it = *items_[i];
        if (it.kind != Kind::Toggle || it.id != id) continue;
        const bool stateChanged = it.checked != checked;
        const bool nameChanged = text && it.text != text;
        it.checked = checked;
        if (nameChanged) {
            it.text = text;
            it.msaa.cchWText = (DWORD)it.text.size();
            it.msaa.pszWText = it.text.data();
        }
        if (!menu_) continue;
        // The menu item keeps the state too: accessibility clients read it there.
        CheckMenuItem(menu_, (UINT)i, MF_BYPOSITION | (checked ? MF_CHECKED : MF_UNCHECKED));
        if (!menuWnd_) continue;
        if (stateChanged) NotifyWinEvent(EVENT_OBJECT_STATECHANGE, menuWnd_, OBJID_CLIENT, (LONG)i + 1);
        if (nameChanged)  NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, menuWnd_, OBJID_CLIENT, (LONG)i + 1);
    }
}

void PopupMenu::openTheme(HWND owner, bool dark)
{
    if (IsAppThemed()) {
        if (dark && !HighContrast()) {
            // An unknown class name falls back to the light "Menu" style. If the
            // text comes back dark, this Windows has no dark menus, and its
            // native light menu is then the look to match.
            theme_ = OpenThemeDataForDpi(owner, L"DarkMode::Menu", dpi_);
            COLORREF text = 0;
            const bool isDark = theme_
                && SUCCEEDED(GetThemeColor(theme_, MENU_POPUPITEM, MPI_NORMAL, TMT_TEXTCOLOR, &text))
                && GetRValue(text) + GetGValue(text) + GetBValue(text) > 3 * 128;
            if (theme_ && !isDark) {
                CloseThemeData(theme_);
                theme_ = nullptr;
            }
        }
        if (!theme_) theme_ = OpenThemeDataForDpi(owner, L"Menu", dpi_);
    }

    if (!theme_) {
        // Visual styles off: classic metrics in system colours.
        check_ = { GetSystemMetricsForDpi(SM_CXMENUCHECK, dpi_), GetSystemMetricsForDpi(SM_CYMENUCHECK, dpi_) };
        mCheck_ = Scale(dpi_, MARGINS{ 1, 1, 1, 1 });
        mCheckBg_ = Scale(dpi_, MARGINS{ 0, 4, 0, 0 });
        mItem_ = Scale(dpi_, MARGINS{ 0, 0, 1, 1 });
        mText_ = Scale(dpi_, MARGINS{ 2, 20, 2, 2 });
        sepHeight_ = ::Scale(dpi_, 7);
        arrowWidth_ = check_.cx;
        return;
    }

    // A theme opened for the DPI scales the part sizes but not the margins and
    // border widths. The native menu uses them exactly like that, so this
    // does too, rather than scaling the margins and ending up larger.
    SIZE sep{}, arrow{};
    GetThemePartSize(theme_, nullptr, MENU_POPUPCHECK, 0, nullptr, TS_TRUE, &check_);
    GetThemePartSize(theme_, nullptr, MENU_POPUPSEPARATOR, 0, nullptr, TS_TRUE, &sep);
    GetThemePartSize(theme_, nullptr, MENU_POPUPSUBMENU, 0, nullptr, TS_TRUE, &arrow);
    sepHeight_ = sep.cy;
    arrowWidth_ = arrow.cx;
    int itemBorder = 0, backBorder = 0;
    GetThemeInt(theme_, MENU_POPUPITEM, 0, TMT_BORDERSIZE, &itemBorder);
    GetThemeInt(theme_, MENU_POPUPBACKGROUND, 0, TMT_BORDERSIZE, &backBorder);
    GetThemeMargins(theme_, nullptr, MENU_POPUPCHECK, 0, TMT_CONTENTMARGINS, nullptr, &mCheck_);
    GetThemeMargins(theme_, nullptr, MENU_POPUPCHECKBACKGROUND, 0, TMT_CONTENTMARGINS, nullptr, &mCheckBg_);
    GetThemeMargins(theme_, nullptr, MENU_POPUPITEM, 0, TMT_CONTENTMARGINS, nullptr, &mItem_);

    // The text sits the background border in from the gutter and keeps the
    // item border free on its right, as in the native menu.
    mText_ = mItem_;
    mText_.cxLeftWidth = backBorder;
    mText_.cxRightWidth = itemBorder;

    back_ = CreateSolidBrush(PartColor(theme_, MENU_POPUPBACKGROUND, 0));
}

UINT PopupMenu::track(HWND owner, bool dark, POINT pt, const std::function<void(UINT id)>& onToggle)
{
    onToggle_ = onToggle;
    dpi_ = MonitorDpi(pt);
    // The menu of a mirrored owner is mirrored too, which only flips the
    // alignment; the reading order of the text has to be asked for.
    rtl_ = (GetWindowLongW(owner, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0;
    openTheme(owner, dark);

    NONCLIENTMETRICSW ncm{ sizeof(ncm) };
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi_);
    font_ = CreateFontIndirectW(&ncm.lfMenuFont);
    ncm.lfMenuFont.lfWeight = FW_BOLD;   // the default item is bold, as natively
    bold_ = CreateFontIndirectW(&ncm.lfMenuFont);

    menu_ = CreatePopupMenu();
    for (size_t i = 0; i < items_.size(); ++i) {
        Item& it = *items_[i];
        MENUITEMINFOW mi{ sizeof(mi) };
        mi.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_DATA;
        mi.fType = MFT_OWNERDRAW | (it.kind == Kind::Separator ? MFT_SEPARATOR : 0);
        mi.wID = it.id;
        mi.fState = (it.checked ? MFS_CHECKED : 0) | (it.kind == Kind::Note ? MFS_DISABLED : 0)
                  | (it.isDefault ? MFS_DEFAULT : 0);
        mi.dwItemData = (ULONG_PTR)&it;
        InsertMenuItemW(menu_, (UINT)i, TRUE, &mi);
    }
    if (back_) {
        // The strips above the first and below the last item are the menu's own.
        MENUINFO mi{ sizeof(mi) };
        mi.fMask = MIM_BACKGROUND;
        mi.hbrBack = back_;
        SetMenuInfo(menu_, &mi);
    }

    current_ = this;
    const DWORD tid = GetCurrentThreadId();
    filterHook_ = SetWindowsHookExW(WH_MSGFILTER, FilterProc, nullptr, tid);
    cbtHook_ = SetWindowsHookExW(WH_CBT, CbtProc, nullptr, tid);

    // KB135788: the owner has to hold the foreground, or the menu does not close
    // when it loses the focus; the WM_NULL afterwards lets that dismissal through.
    SetForegroundWindow(owner);
    const UINT cmd = (UINT)TrackPopupMenuEx(menu_, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);

    if (filterHook_) UnhookWindowsHookEx(filterHook_);
    if (cbtHook_) UnhookWindowsHookEx(cbtHook_);
    filterHook_ = cbtHook_ = nullptr;
    if (menuWnd_ && IsWindow(menuWnd_)) RemoveWindowSubclass(menuWnd_, MenuWndProc, kFrameSubclassId);
    menuWnd_ = nullptr;
    current_ = nullptr;
    DestroyMenu(menu_);
    menu_ = nullptr;
    return cmd;
}

bool PopupMenu::OwnerMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result)
{
    PopupMenu* self = current_;
    if (!self) return false;
    switch (msg) {
    case WM_MEASUREITEM: {
        auto* m = (MEASUREITEMSTRUCT*)lp;
        if (m->CtlType != ODT_MENU) return false;
        auto own = std::find_if(self->items_.begin(), self->items_.end(),
                                [&](const auto& it) { return (ULONG_PTR)it.get() == m->itemData; });
        if (own == self->items_.end()) return false;
        self->measure(m);
        result = TRUE;
        return true;
    }
    case WM_DRAWITEM: {
        auto* d = (const DRAWITEMSTRUCT*)lp;
        if (d->CtlType != ODT_MENU || (HMENU)d->hwndItem != self->menu_) return false;
        self->draw(d);
        result = TRUE;
        return true;
    }
    case WM_MENUCHAR:
        if ((HMENU)lp != self->menu_) return false;
        result = self->menuChar((wchar_t)LOWORD(wp));
        return true;
    case WM_ENTERIDLE:
        // Fallback for a menu window the CBT hook did not see being created.
        if (wp == MSGF_MENU && lp) self->adoptMenuWindow((HWND)lp);
        return false;
    }
    return false;
}

void PopupMenu::measure(MEASUREITEMSTRUCT* m)
{
    const Item& it = *(const Item*)m->itemData;
    if (it.kind == Kind::Separator) {
        m->itemWidth = 0;
        m->itemHeight = sepHeight_ + mItem_.cyTopHeight + mItem_.cyBottomHeight;
        return;
    }

    HDC dc = GetDC(nullptr);
    HGDIOBJ old = SelectObject(dc, it.isDefault ? bold_ : font_);
    const UINT flags = DT_SINGLELINE | DT_LEFT | DT_CALCRECT | (it.literal ? DT_NOPREFIX : 0);
    RECT a{}, b{};
    DrawTextW(dc, it.text.c_str(), -1, &a, flags);
    if (!it.alt.empty()) DrawTextW(dc, it.alt.c_str(), -1, &b, flags);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    const int textW = (std::max)(a.right - a.left, b.right - b.left);
    const int textH = (std::max)(a.bottom - a.top, b.bottom - b.top);

    const int checkBgW = check_.cx + mCheck_.cxLeftWidth + mCheck_.cxRightWidth;
    const int checkBgH = check_.cy + mCheck_.cyTopHeight + mCheck_.cyBottomHeight;
    // Like the native menu, the column of a submenu arrow stays free on the
    // right. The menu then adds the width of a check mark to every owner-drawn
    // item on its own, which the native menu has as well, so it is left in.
    const int cx = mCheckBg_.cxLeftWidth + checkBgW + mCheckBg_.cxRightWidth
                 + mText_.cxLeftWidth + textW + mText_.cxRightWidth
                 + arrowWidth_ + mCheckBg_.cxRightWidth + mCheck_.cxRightWidth;
    const int cy = (std::max)(textH + mText_.cyTopHeight + mText_.cyBottomHeight,
                              checkBgH + mCheckBg_.cyTopHeight + mCheckBg_.cyBottomHeight);
    m->itemWidth = (UINT)cx;
    m->itemHeight = (UINT)cy;
}

void PopupMenu::draw(const DRAWITEMSTRUCT* d)
{
    const Item& it = *(const Item*)d->itemData;
    if (!theme_) { drawFlat(d, it); return; }

    HDC dc = d->hDC;
    const RECT rc = d->rcItem;
    const bool hot = (d->itemState & ODS_SELECTED) != 0;
    const bool off = (d->itemState & (ODS_GRAYED | ODS_DISABLED)) != 0;
    const int checkBgW = check_.cx + mCheck_.cxLeftWidth + mCheck_.cxRightWidth;
    const int checkBgH = check_.cy + mCheck_.cyTopHeight + mCheck_.cyBottomHeight;
    const int gutterRight = rc.left + mCheckBg_.cxLeftWidth + checkBgW + mCheckBg_.cxRightWidth;

    DrawThemeBackground(theme_, dc, MENU_POPUPBACKGROUND, 0, &rc, nullptr);
    RECT gutter{ rc.left, rc.top, gutterRight, rc.bottom };
    DrawThemeBackground(theme_, dc, MENU_POPUPGUTTER, 0, &gutter, nullptr);

    if (it.kind == Kind::Separator) {
        RECT sep{ gutterRight, rc.top + mItem_.cyTopHeight, rc.right, rc.bottom - mItem_.cyBottomHeight };
        DrawThemeBackground(theme_, dc, MENU_POPUPSEPARATOR, 0, &sep, nullptr);
        return;
    }

    if (hot) DrawThemeBackground(theme_, dc, MENU_POPUPITEM, off ? MPI_DISABLEDHOT : MPI_HOT, &rc, nullptr);

    if (it.checked) {
        RECT bg{ rc.left + mCheckBg_.cxLeftWidth, rc.top + (rc.bottom - rc.top - checkBgH) / 2, 0, 0 };
        bg.right = bg.left + checkBgW;
        bg.bottom = bg.top + checkBgH;
        DrawThemeBackground(theme_, dc, MENU_POPUPCHECKBACKGROUND, off ? MCB_DISABLED : MCB_NORMAL, &bg, nullptr);
        RECT ck{ bg.left + mCheck_.cxLeftWidth, bg.top + mCheck_.cyTopHeight, 0, 0 };
        ck.right = ck.left + check_.cx;
        ck.bottom = ck.top + check_.cy;
        DrawThemeBackground(theme_, dc, MENU_POPUPCHECK, off ? MC_CHECKMARKDISABLED : MC_CHECKMARKNORMAL, &ck, nullptr);
    }

    RECT text{ gutterRight + mText_.cxLeftWidth, rc.top + mText_.cyTopHeight,
               rc.right - mText_.cxRightWidth, rc.bottom - mText_.cyBottomHeight };
    const DWORD flags = DT_SINGLELINE | DT_LEFT | DT_VCENTER | (it.literal ? DT_NOPREFIX : 0)
                      | ((d->itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0) | (rtl_ ? DT_RTLREADING : 0);
    const int state = off ? (hot ? MPI_DISABLEDHOT : MPI_DISABLED) : (hot ? MPI_HOT : MPI_NORMAL);
    HGDIOBJ old = SelectObject(dc, it.isDefault ? bold_ : font_);
    DrawThemeText(theme_, dc, MENU_POPUPITEM, state, it.text.c_str(), -1, flags, 0, &text);
    SelectObject(dc, old);
}

void PopupMenu::drawFlat(const DRAWITEMSTRUCT* d, const Item& it)
{
    HDC dc = d->hDC;
    RECT rc = d->rcItem;
    const bool hot = (d->itemState & ODS_SELECTED) != 0 && it.kind != Kind::Separator;
    const bool off = (d->itemState & (ODS_GRAYED | ODS_DISABLED)) != 0;
    BOOL flat = FALSE;
    SystemParametersInfoW(SPI_GETFLATMENU, 0, &flat, 0);
    FillRect(dc, &rc, GetSysColorBrush(hot ? (flat ? COLOR_MENUHILIGHT : COLOR_HIGHLIGHT) : COLOR_MENU));

    if (it.kind == Kind::Separator) {
        RECT sep{ rc.left, rc.top + (rc.bottom - rc.top) / 2 - 1, rc.right, rc.bottom };
        DrawEdge(dc, &sep, EDGE_ETCHED, BF_TOP);
        return;
    }

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(off ? COLOR_GRAYTEXT : (hot ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT)));
    const int checkBgW = check_.cx + mCheck_.cxLeftWidth + mCheck_.cxRightWidth;
    const int gutterRight = rc.left + mCheckBg_.cxLeftWidth + checkBgW + mCheckBg_.cxRightWidth;

    if (it.checked) {
        // Marlett "a" is the check mark the classic menu draws.
        HFONT marlett = CreateFontW(-check_.cy, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, SYMBOL_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                    DEFAULT_PITCH, L"Marlett");
        HGDIOBJ old = SelectObject(dc, marlett);
        RECT ck{ rc.left + mCheckBg_.cxLeftWidth, rc.top, rc.left + mCheckBg_.cxLeftWidth + checkBgW, rc.bottom };
        DrawTextW(dc, L"a", 1, &ck, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
        SelectObject(dc, old);
        DeleteObject(marlett);
    }

    RECT text{ gutterRight + mText_.cxLeftWidth, rc.top, rc.right - mText_.cxRightWidth, rc.bottom };
    const UINT flags = DT_SINGLELINE | DT_LEFT | DT_VCENTER | (it.literal ? DT_NOPREFIX : 0)
                     | ((d->itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0) | (rtl_ ? DT_RTLREADING : 0);
    HGDIOBJ old = SelectObject(dc, it.isDefault ? bold_ : font_);
    DrawTextW(dc, it.text.c_str(), -1, &text, flags);
    SelectObject(dc, old);
}

LRESULT PopupMenu::menuChar(wchar_t ch)
{
    // Owner-drawn items have no mnemonics, so the menu asks. Match the first
    // letter, starting after the highlighted item, as the native menu does for
    // items without one.
    const int n = (int)items_.size();
    const int start = hotItem();
    int first = -1, matches = 0;
    wchar_t want = ch;
    CharUpperBuffW(&want, 1);
    for (int k = 1; k <= n; ++k) {
        const int i = (start + k + n) % n;
        const Item& it = *items_[i];
        if (it.kind == Kind::Separator || it.kind == Kind::Note || it.text.empty()) continue;
        wchar_t c = it.text[0];
        CharUpperBuffW(&c, 1);
        if (c != want) continue;
        if (first < 0) first = i;
        ++matches;
    }
    if (first < 0) return MAKELRESULT(0, MNC_IGNORE);
    // A unique command runs at once; a toggle is only selected, so the menu
    // stays open for it like it does for a click.
    const bool run = matches == 1 && items_[first]->kind == Kind::Command;
    return MAKELRESULT(first, run ? MNC_EXECUTE : MNC_SELECT);
}

int PopupMenu::hotItem() const
{
    for (int i = 0; i < (int)items_.size(); ++i)
        if (GetMenuState(menu_, (UINT)i, MF_BYPOSITION) & MF_HILITE) return i;
    return -1;
}

bool PopupMenu::filter(const MSG& m)
{
    int pos = -1;
    if (m.message == WM_LBUTTONUP || m.message == WM_RBUTTONUP) {
        // The menu loop routes the mouse through the owner window with screen
        // coordinates, so the message's own point is the one to test.
        pos = MenuItemFromPoint(nullptr, menu_, m.pt);
    } else if (m.message == WM_KEYDOWN && m.wParam == VK_RETURN) {
        pos = hotItem();
    } else {
        return false;
    }
    if (pos < 0 || pos >= (int)items_.size() || items_[pos]->kind != Kind::Toggle) return false;

    // Swallowing the release (or the Enter) is what keeps the menu open.
    if (onToggle_) onToggle_(items_[pos]->id);
    if (menuWnd_) InvalidateRect(menuWnd_, nullptr, FALSE);
    return true;
}

LRESULT CALLBACK PopupMenu::FilterProc(int code, WPARAM wp, LPARAM lp)
{
    if (code == MSGF_MENU && current_ && current_->filter(*(const MSG*)lp)) return TRUE;
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK PopupMenu::CbtProc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HCBT_CREATEWND && current_ && !current_->menuWnd_) {
        wchar_t cls[16];
        if (GetClassNameW((HWND)wp, cls, ARRAYSIZE(cls)) && lstrcmpW(cls, kMenuWindowClass) == 0)
            current_->adoptMenuWindow((HWND)wp);
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void PopupMenu::adoptMenuWindow(HWND h)
{
    if (menuWnd_) return;
    menuWnd_ = h;
    // With owner-drawn items the menu window falls back to a classic frame
    // and background; the native menu draws both from the style.
    if (!theme_) return;
    SetWindowSubclass(h, MenuWndProc, kFrameSubclassId, (DWORD_PTR)this);
    if (IsWindowVisible(h)) RedrawWindow(h, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
}

void PopupMenu::paintFrame(HDC dc)
{
    // Repaint the border around the client area from the style, as the
    // native menu has it.
    RECT win, client;
    GetWindowRect(menuWnd_, &win);
    GetClientRect(menuWnd_, &client);
    MapWindowPoints(menuWnd_, nullptr, (POINT*)&client, 2);
    OffsetRect(&client, -win.left, -win.top);
    OffsetRect(&win, -win.left, -win.top);

    const int saved = SaveDC(dc);
    ExcludeClipRect(dc, client.left, client.top, client.right, client.bottom);
    FillRect(dc, &win, back_);
    DrawThemeBackground(theme_, dc, MENU_POPUPBORDERS, 0, &win, nullptr);
    RestoreDC(dc, saved);
}

LRESULT CALLBACK PopupMenu::MenuWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
{
    auto* self = (PopupMenu*)ref;
    switch (msg) {
    case WM_NCPAINT: {
        const LRESULT r = DefSubclassProc(h, msg, wp, lp);
        if (HDC dc = GetWindowDC(h)) { self->paintFrame(dc); ReleaseDC(h, dc); }
        return r;
    }
    case WM_PRINT: {   // the fade-in animation renders the menu through WM_PRINT
        const LRESULT r = DefSubclassProc(h, msg, wp, lp);
        if (lp & PRF_NONCLIENT) self->paintFrame((HDC)wp);
        return r;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, MenuWndProc, kFrameSubclassId);
        break;
    }
    return DefSubclassProc(h, msg, wp, lp);
}
