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

#include "MainWindow.h"
#include "AppInfo.h"
#include "Glyphs.h"
#include "Gfx.h"
#include "HoverButton.h"
#include "InfoWindow.h"
#include "Loc.h"
#include "ProcessUtil.h"
#include "resource.h"
#include "Hook.h"   // WM_DGH_CLOAKED / WM_DGH_UNCLOAKED

#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>   // GET_X_LPARAM
#include <algorithm>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace {
constexpr wchar_t kClass[] = L"DeGhosterMainWindow";

constexpr UINT WM_TRAY = WM_APP + 0x40;
// Posted by the list subclass below when the eye cell was clicked.
constexpr UINT WM_EYE_TOGGLE = WM_APP + 0x41;
constexpr UINT_PTR kListSubclassId = 1;
constexpr UINT_PTR kPendingTimer = 1;
enum { IDC_LIST = 1001, IDC_POWER, IDC_INFO, IDC_EXIT };
enum { IDM_SHOW = 2001, IDM_ACTIVE, IDM_INFO, IDM_QUIT, IDM_WIN_BASE = 3000 };

// Cross-process "show yourself" ping from a second launch. RegisterWindowMessage
// returns the same value in every process for this string.
UINT showExistingMsg() { static UINT m = RegisterWindowMessageW(L"DeGhoster_ShowExistingInstance"); return m; }

// The list view swallows the click that activates an inactive window: it emits no
// NM_CLICK for it, so the eye needed a second click whenever the window was not
// already in front. A subclass sees the raw button messages whatever the control
// decides to do with them, so one click is enough again.
//
// The refdata carries the cell the press started on (item + 1, or 0 for "not the
// eye"), so a press that drifts off the cell before release does not toggle.
LRESULT CALLBACK ListProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref)
{
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) {
        LVHITTESTINFO ht{};
        ht.pt.x = GET_X_LPARAM(lp);
        ht.pt.y = GET_Y_LPARAM(lp);
        ListView_SubItemHitTest(h, &ht);
        const DWORD_PTR cell = (ht.iItem >= 0 && ht.iSubItem == 1) ? (DWORD_PTR)(ht.iItem + 1) : 0;

        if (msg == WM_LBUTTONDOWN) {
            SetWindowSubclass(h, ListProc, kListSubclassId, cell);
        } else {
            if (cell && cell == ref) {
                LVITEMW it{}; it.mask = LVIF_PARAM; it.iItem = ht.iItem;
                if (ListView_GetItem(h, &it))
                    PostMessageW(GetParent(h), WM_EYE_TOGGLE, (WPARAM)it.lParam, 0);
            }
            SetWindowSubclass(h, ListProc, kListSubclassId, 0);
        }
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// FixInfo::title is stored raw because it is part of the registry opt-out key, so
// the placeholder for an untitled window is substituted here, at display time.
std::wstring RowLabel(const FixInfo& fi)
{
    const wchar_t* t = fi.title.empty() ? loc::t(IDS_UNTITLED) : fi.title.c_str();
    return std::wstring(t) + L" (" + fi.exeName + L")";
}
}

bool MainWindow::activateExisting()
{
    HWND h = FindWindowW(kClass, nullptr);
    if (!h) return false;
    PostMessageW(h, showExistingMsg(), 0, 0);
    return true;
}

bool MainWindow::requestShutdown(DWORD timeoutMs)
{
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;

    if (HWND h = FindWindowW(kClass, nullptr)) {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        HANDLE p = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
        // The tray Exit path: uncloak the windows, wind the helpers down, quit.
        PostMessageW(h, WM_COMMAND, IDC_EXIT, 0);
        if (p) {
            const ULONGLONG now = GetTickCount64();
            WaitForSingleObject(p, now < deadline ? (DWORD)(deadline - now) : 0);
            CloseHandle(p);
        }
    }

    // The helpers go away on their own once the host is gone, but until they do
    // they still hold a hook DLL open - which is the whole reason for waiting.
    while (GetTickCount64() < deadline && proc::HelperRunning())
        Sleep(100);

    return !FindWindowW(kClass, nullptr) && !proc::HelperRunning();
}

MainWindow::MainWindow() : engine_(settings_) {}

MainWindow::~MainWindow()
{
    if (uiFont_) DeleteObject(uiFont_);
    if (rowSizer_) ImageList_Destroy(rowSizer_);
}

bool MainWindow::create(HINSTANCE inst, bool startHidden)
{
    inst_ = inst;

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    // Size for the primary-monitor DPI up front; GetDpiForWindow is unreliable
    // before the window is shown at a CW_USEDEFAULT position.
    dpi_ = GetDpiForSystem();
    RECT wr{ 0, 0, S(396), S(269) };
    AdjustWindowRectExForDpi(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi_);
    int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    RECT wa{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int wx = wa.left + ((wa.right - wa.left) - ww) / 2;
    int wy = wa.top + ((wa.bottom - wa.top) - wh) / 2;

    DWORD ex = loc::isRtl() ? WS_EX_LAYOUTRTL : 0;
    HWND h = CreateWindowExW(ex, kClass, app::Name, WS_OVERLAPPEDWINDOW,
                             wx, wy, ww, wh, nullptr, nullptr, inst, this);
    if (!h) return false;

    ChangeWindowMessageFilterEx(h, WM_DGH_CLOAKED, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(h, WM_DGH_UNCLOAKED, MSGFLT_ALLOW, nullptr);

    // Shell broadcasts this when Explorer (re)starts; we re-add the tray icon so it
    // doesn't vanish for good after an Explorer crash. Allow it through the filter
    // in case we ever run elevated.
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (taskbarCreatedMsg_) ChangeWindowMessageFilterEx(h, taskbarCreatedMsg_, MSGFLT_ALLOW, nullptr);

    applyFont();
    settings_.load();
    addTray();
    applyTheme();               // before ShowWindow, so the first paint has real colours
    // Autostart (--taskbar) launches straight to the tray; the window is created
    // hidden and the user opens it from the tray icon when they want it.
    if (!startHidden) ShowWindow(h, SW_SHOW);
    layout();
    UpdateWindow(h);

    engine_.setListener(this);
    engine_.start(h);
    // Backstop for cloak/uncloak requests the hook never answers: drop them so a
    // window can't stay stuck in "pending" forever.
    SetTimer(h, kPendingTimer, 1000, nullptr);
    updateStatus();
    // A hook DLL that will not load makes the x64 path a silent no-op: ghosts are
    // listed but never fixed, with nothing to tell the user why. Say so once.
    if (!engine_.hooksAvailable()) warnHooksMissing();
    return true;
}

LRESULT CALLBACK MainWindow::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    MainWindow* self;
    if (msg == WM_NCCREATE) {
        self = static_cast<MainWindow*>(((CREATESTRUCTW*)lp)->lpCreateParams);
        self->hwnd_ = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (MainWindow*)GetWindowLongPtrW(h, GWLP_USERDATA);
    }
    return self ? self->handle(msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

void MainWindow::onCreate()
{
    DWORD bs = WS_CHILD | WS_VISIBLE | BS_OWNERDRAW;
    power_ = CreateWindowW(L"BUTTON", L"", bs, 0, 0, 0, 0, hwnd_, (HMENU)IDC_POWER, inst_, nullptr);
    info_  = CreateWindowW(L"BUTTON", L"", bs, 0, 0, 0, 0, hwnd_, (HMENU)IDC_INFO,  inst_, nullptr);
    exit_  = CreateWindowW(L"BUTTON", L"", bs, 0, 0, 0, 0, hwnd_, (HMENU)IDC_EXIT,  inst_, nullptr);
    ui::EnableHover(power_);
    ui::EnableHover(info_);
    ui::EnableHover(exit_);

    list_ = CreateWindowExW(0, WC_LISTVIEWW, L"",
                            // LVS_SHAREIMAGELISTS: rowSizer_ belongs to us and is freed in
                            // applyFont()/~MainWindow. Without it the control destroys the
                            // image list on teardown and those calls hit a freed handle.
                            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
                            LVS_NOSORTHEADER | LVS_SHAREIMAGELISTS,
                            0, 0, 0, 0, hwnd_, (HMENU)IDC_LIST, inst_, nullptr);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_DOUBLEBUFFER);
    SetWindowSubclass(list_, ListProc, kListSubclassId, 0);
    LVCOLUMNW col{}; col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.pszText = (LPWSTR)loc::t(IDS_COL_WINDOW); col.cx = S(300); ListView_InsertColumn(list_, 0, &col);
    col.mask |= LVCF_FMT; col.fmt = LVCFMT_CENTER; col.pszText = (LPWSTR)L""; col.cx = S(54);
    ListView_InsertColumn(list_, 1, &col);
}

void MainWindow::applyFont()
{
    if (uiFont_) DeleteObject(uiFont_);
    uiFont_ = CreateFontW(-MulDiv(9, dpi_, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (list_) SendMessageW(list_, WM_SETFONT, (WPARAM)uiFont_, TRUE);

    // Force a taller row so the per-window eye can be drawn as large as the
    // global power button. A 1px-wide (invisible) small-image list bumps the
    // list's row height without adding a real icon indent.
    if (list_) {
        HIMAGELIST old = rowSizer_;
        rowSizer_ = ImageList_Create(1, S(34), ILC_COLOR32, 1, 1);
        ListView_SetImageList(list_, rowSizer_, LVSIL_SMALL);
        if (old) ImageList_Destroy(old);
    }
}

void MainWindow::applyTheme()
{
    theme_ = Theme::current();
    ApplyDarkTitleBar(hwnd_, theme_.dark);
    if (list_) {
        SetWindowTheme(list_, theme_.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        ListView_SetBkColor(list_, theme_.listBg);
        ListView_SetTextBkColor(list_, theme_.listBg);
        ListView_SetTextColor(list_, theme_.fore);
    }
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::layout()
{
    RECT rc; GetClientRect(hwnd_, &rc);
    int band = S(44), btn = S(34), pad = S(5), gap = S(2);
    MoveWindow(power_, pad, (band - btn) / 2, btn, btn, TRUE);
    MoveWindow(exit_, rc.right - pad - btn, (band - btn) / 2, btn, btn, TRUE);
    MoveWindow(info_, rc.right - pad - 2 * btn - gap, (band - btn) / 2, btn, btn, TRUE);
    MoveWindow(list_, 0, band, rc.right, rc.bottom - band, TRUE);
    sizeListColumns();
}

void MainWindow::sizeListColumns()
{
    if (!list_) return;
    // Base the widths on the list's own client rect, which already excludes the
    // vertical scrollbar. Using the window width instead made the columns overflow
    // (and add a horizontal scrollbar clipping the eye) once rows scrolled.
    RECT lc; GetClientRect(list_, &lc);
    int eye = S(54);
    int wcol = (lc.right - lc.left) - eye - S(2);
    if (wcol < S(120)) wcol = S(120);
    ListView_SetColumnWidth(list_, 0, wcol);
    ListView_SetColumnWidth(list_, 1, eye);
}

void MainWindow::rebuildList()
{
    // Remember the selected ghost so a rebuild (fired on every add/remove) doesn't
    // drop the user's selection.
    HWND selected = nullptr;
    int sel = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
    if (sel >= 0) {
        LVITEMW s{}; s.mask = LVIF_PARAM; s.iItem = sel;
        if (ListView_GetItem(list_, &s)) selected = (HWND)s.lParam;
    }

    // tracked() is an unordered_map, so iteration order is unstable and rows would
    // visibly reshuffle. Sort by label (then hwnd) for a stable, predictable list.
    std::vector<std::pair<std::wstring, HWND>> rows;
    rows.reserve(engine_.tracked().size());
    for (auto& kv : engine_.tracked())
        rows.push_back({ RowLabel(kv.second), kv.first });
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        int c = CompareStringOrdinal(a.first.c_str(), -1, b.first.c_str(), -1, TRUE);
        return c ? c == CSTR_LESS_THAN : a.second < b.second;
    });

    ListView_DeleteAllItems(list_);
    for (auto& r : rows) {
        LVITEMW it{}; it.mask = LVIF_PARAM | LVIF_TEXT;
        it.iItem = ListView_GetItemCount(list_);
        it.lParam = (LPARAM)r.second;
        it.pszText = (LPWSTR)r.first.c_str();
        int idx = ListView_InsertItem(list_, &it);
        if (r.second == selected && idx >= 0) {
            ListView_SetItemState(list_, idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(list_, idx, FALSE);
        }
    }
    sizeListColumns();   // a new vertical scrollbar may have narrowed the client area
}

void MainWindow::updateStatus()
{
    int n = engine_.ghostCount();
    wchar_t t[128];
    wsprintfW(t, L"%s \u2014 %d %s", app::Name, n,
              loc::t(n == 1 ? IDS_GHOST_SINGULAR : IDS_GHOST_PLURAL));
    SetWindowTextW(hwnd_, t);
    // With no hook DLL the count is beside the point, so the tooltip carries the
    // reason nothing is being fixed instead. Every translation fits szTip.
    lstrcpynW(nid_.szTip, engine_.hooksAvailable() ? t : loc::t(IDS_HOOK_MISSING),
              ARRAYSIZE(nid_.szTip));
    nid_.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void MainWindow::onTrackedChanged() { rebuildList(); updateStatus(); }

void MainWindow::drawButton(const DRAWITEMSTRUCT* d)
{
    SetLayout(d->hDC, 0);   // keep glyphs upright even under WS_EX_LAYOUTRTL
    RECT rc = d->rcItem;
    int w = rc.right - rc.left, hh = rc.bottom - rc.top;
    bool hot = ui::IsHot(d->hwndItem);
    bool pressed = (d->itemState & ODS_SELECTED) != 0;
    HBRUSH bg = CreateSolidBrush(theme_.back);
    FillRect(d->hDC, &rc, bg);
    DeleteObject(bg);
    int gl = (int)(std::min(w, hh) * 0.55);

    if (d->CtlID == IDC_POWER) {
        int dia = std::min(w, hh) - S(6);
        RECT c{ rc.left + (w - dia) / 2, rc.top + (hh - dia) / 2,
                rc.left + (w - dia) / 2 + dia, rc.top + (hh - dia) / 2 + dia };
        bool on = settings_.globalEnabled();
        gfx::FillCircle(d->hDC, c, 255, on ? theme_.accentOn : theme_.accentOff);
        if (pressed || hot)
            gfx::FillCircle(d->hDC, c, theme_.dark ? (pressed ? 150 : 100) : (pressed ? 90 : 60), RGB(255, 255, 255));
        gfx::DrawGlyph(d->hDC, rc, glyph::Power, on ? RGB(255, 255, 255) : RGB(0, 0, 0), gl);
    } else if (d->CtlID == IDC_INFO) {
        gfx::DrawGlyph(d->hDC, rc, glyph::Info, hot ? theme_.infoHot : theme_.info, gl);
    } else if (d->CtlID == IDC_EXIT) {
        gfx::DrawGlyph(d->hDC, rc, glyph::Exit, hot ? theme_.exitHot : theme_.exit, gl);
    }
}

LRESULT MainWindow::listCustomDraw(NMLVCUSTOMDRAW* cd)
{
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        bool alt = (cd->nmcd.dwItemSpec % 2) != 0;
        cd->clrTextBk = alt ? theme_.rowAlt : theme_.listBg;
        cd->clrText = theme_.fore;
        return CDRF_NOTIFYSUBITEMDRAW;
    }
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
        if (cd->iSubItem == 1) {
            HWND ghost = (HWND)cd->nmcd.lItemlParam;
            RECT rc; ListView_GetSubItemRect(list_, (int)cd->nmcd.dwItemSpec, 1, LVIR_BOUNDS, &rc);
            HBRUSH bg = CreateSolidBrush((cd->nmcd.dwItemSpec % 2) ? theme_.rowAlt : theme_.listBg);
            FillRect(cd->nmcd.hdc, &rc, bg); DeleteObject(bg);
            auto it = engine_.tracked().find(ghost);
            bool managed = it == engine_.tracked().end() || settings_.isManaged(it->second.disableKey());
            DWORD oldLayout = GetLayout(cd->nmcd.hdc);
            // SetLayout(0) keeps the glyph upright, but it also switches the DC to
            // physical coordinates while rc came back in the mirrored logical ones.
            // Unmirrored, the eye lands on top of the title column under RTL.
            RECT dr = rc;
            if (oldLayout & LAYOUT_RTL) {
                RECT lc; GetClientRect(list_, &lc);
                dr.left  = lc.right - rc.right;
                dr.right = lc.right - rc.left;
            }
            SetLayout(cd->nmcd.hdc, 0);
            int w = dr.right - dr.left, hh = dr.bottom - dr.top;
            int side = std::min(w, hh);
            int dia = side - S(6);
            RECT c{ dr.left + (w - dia) / 2, dr.top + (hh - dia) / 2,
                    dr.left + (w - dia) / 2 + dia, dr.top + (hh - dia) / 2 + dia };
            gfx::FillCircle(cd->nmcd.hdc, c, 255, managed ? theme_.accentOn : theme_.accentOff);
            gfx::DrawGlyph(cd->nmcd.hdc, dr, managed ? glyph::Eye : glyph::EyeOff,
                           managed ? RGB(255, 255, 255) : RGB(0, 0, 0), (int)(side * 0.55));
            SetLayout(cd->nmcd.hdc, oldLayout);
            return CDRF_SKIPDEFAULT;
        }
        bool alt = (cd->nmcd.dwItemSpec % 2) != 0;
        cd->clrTextBk = alt ? theme_.rowAlt : theme_.listBg;
        cd->clrText = theme_.fore;
        return CDRF_NEWFONT;
    }
    }
    return CDRF_DODEFAULT;
}

void MainWindow::setGlobalEnabled(bool on)
{
    if (settings_.globalEnabled() == on) return;
    settings_.setGlobalEnabled(on);
    InvalidateRect(power_, nullptr, TRUE);
    engine_.refreshAll();
}

void MainWindow::toggleWindow(HWND ghost)
{
    auto it = engine_.tracked().find(ghost);
    if (it == engine_.tracked().end()) return;
    std::wstring key = it->second.disableKey();
    settings_.setManaged(key, !settings_.isManaged(key));
    engine_.refreshAll();
    InvalidateRect(list_, nullptr, FALSE);
}

void MainWindow::showWindow()
{
    ShowWindow(hwnd_, SW_SHOW);
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    SetForegroundWindow(hwnd_);
}

void MainWindow::addTray()
{
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAY;
    if (nid_.hIcon) DestroyIcon(nid_.hIcon);   // re-add (TaskbarCreated) would else leak the old one
    nid_.hIcon = (HICON)LoadImageW(inst_, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    lstrcpynW(nid_.szTip, app::Name, ARRAYSIZE(nid_.szTip));
    Shell_NotifyIconW(NIM_ADD, &nid_);
}

void MainWindow::warnHooksMissing()
{
    // The brand name is not localized, so the balloon title needs no string of its own.
    nid_.uFlags = NIF_INFO;
    nid_.dwInfoFlags = NIIF_WARNING;
    lstrcpynW(nid_.szInfoTitle, app::Name, ARRAYSIZE(nid_.szInfoTitle));
    lstrcpynW(nid_.szInfo, loc::t(IDS_HOOK_MISSING), ARRAYSIZE(nid_.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
    nid_.dwInfoFlags = 0;
}

void MainWindow::showTrayMenu()
{
    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_SHOW, loc::t(IDS_MENU_STATUS));
    SetMenuDefaultItem(m, IDM_SHOW, FALSE);
    bool on = settings_.globalEnabled();
    AppendMenuW(m, MF_STRING | (on ? MF_CHECKED : 0), IDM_ACTIVE,
                loc::t(on ? IDS_MENU_ACTIVE : IDS_MENU_INACTIVE));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    menuWindows_.clear();
    if (engine_.tracked().empty()) {
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, loc::t(IDS_MENU_NOWINDOWS));
    } else {
        for (auto& kv : engine_.tracked()) {
            UINT id = IDM_WIN_BASE + (UINT)menuWindows_.size();
            menuWindows_.push_back(kv.first);
            std::wstring label = RowLabel(kv.second);
            bool managed = settings_.isManaged(kv.second.disableKey());
            AppendMenuW(m, MF_STRING | (managed ? MF_CHECKED : 0), id, label.c_str());
        }
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_INFO, loc::t(IDS_MENU_INFO));
    AppendMenuW(m, MF_STRING, IDM_QUIT, loc::t(IDS_MENU_QUIT));

    SetForegroundWindow(hwnd_);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);   // KB135788: let the menu dismiss on click-away
    DestroyMenu(m);

    if (cmd == IDM_SHOW) showWindow();
    else if (cmd == IDM_ACTIVE) setGlobalEnabled(!on);
    else if (cmd == IDM_INFO) InfoWindow::Show(inst_, hwnd_, theme_, dpi_);
    else if (cmd == IDM_QUIT) { reallyExit_ = true; DestroyWindow(hwnd_); }
    else if (cmd >= IDM_WIN_BASE) {
        size_t i = cmd - IDM_WIN_BASE;
        if (i < menuWindows_.size()) toggleWindow(menuWindows_[i]);
    }
}

LRESULT MainWindow::handle(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: onCreate(); return 0;
    case WM_SIZE: layout(); return 0;

    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd_, &rc);
        HBRUSH b = CreateSolidBrush(theme_.back);
        FillRect((HDC)wp, &rc, b);
        DeleteObject(b);
        return 1;
    }

    case WM_DPICHANGED: {
        dpi_ = HIWORD(wp);
        applyFont();
        RECT* p = (RECT*)lp;
        SetWindowPos(hwnd_, nullptr, p->left, p->top, p->right - p->left, p->bottom - p->top, SWP_NOZORDER);
        layout();
        return 0;
    }

    case WM_EYE_TOGGLE: toggleWindow((HWND)wp); return 0;

    case WM_DRAWITEM: drawButton((DRAWITEMSTRUCT*)lp); return TRUE;

    case WM_NOTIFY: {
        auto* n = (NMHDR*)lp;
        if (n->idFrom == IDC_LIST && n->code == NM_CUSTOMDRAW)
            return listCustomDraw((NMLVCUSTOMDRAW*)lp);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_POWER: setGlobalEnabled(!settings_.globalEnabled()); return 0;
        case IDC_INFO:  InfoWindow::Show(inst_, hwnd_, theme_, dpi_); return 0;
        case IDC_EXIT:  reallyExit_ = true; DestroyWindow(hwnd_); return 0;
        }
        return 0;

    case WM_TRAY:
        // Left click / double click restores the window; right click opens the menu.
        // (Opening the menu on left-up made the double-click restore unreachable,
        // because the first up already put up the modal menu.)
        if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_LBUTTONDBLCLK) showWindow();
        else if (LOWORD(lp) == WM_RBUTTONUP) showTrayMenu();
        return 0;

    case WM_DGH_CLOAKED:   engine_.onCloaked((HWND)wp);   updateStatus(); return 0;
    case WM_DGH_UNCLOAKED: engine_.onUncloaked((HWND)wp); updateStatus(); return 0;

    case WM_TIMER:
        if (wp == kPendingTimer) engine_.tick();
        return 0;

    case WM_SETTINGCHANGE:
        // Only a colour-scheme change needs a re-theme; re-applying on every
        // settings broadcast is wasted work (and flickers the caption).
        if (lp && lstrcmpiW((LPCWSTR)lp, L"ImmersiveColorSet") == 0) {
            applyTheme();
            InvalidateRect(power_, nullptr, TRUE);
        }
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        // Logoff/shutdown/Restart-Manager. Uncloak on a short budget (the session
        // is ending on a clock), then actually exit: stopping the engine without
        // exiting left the process alive with a live timer that could re-inject,
        // and the Restart Manager expects the app to close itself. stop() is
        // idempotent, so WM_DESTROY repeating the teardown is harmless.
        if (wp) {
            engine_.stop(700);
            reallyExit_ = true;
            DestroyWindow(hwnd_);
        }
        return 0;

    case WM_CLOSE:
        if (!reallyExit_) { ShowWindow(hwnd_, SW_HIDE); return 0; }
        DestroyWindow(hwnd_);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd_, kPendingTimer);
        engine_.stop();
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        if (nid_.hIcon) { DestroyIcon(nid_.hIcon); nid_.hIcon = nullptr; }
        PostQuitMessage(0);
        return 0;
    }
    if (msg == taskbarCreatedMsg_ && taskbarCreatedMsg_) {
        addTray();       // Explorer restarted: re-create our tray icon
        updateStatus();
        return 0;
    }
    if (msg == showExistingMsg()) {   // a second launch asked us to surface
        showWindow();
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}
