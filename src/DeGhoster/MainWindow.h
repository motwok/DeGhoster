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
#include <commctrl.h>
#include <shellapi.h>
#include <vector>
#include "Theme.h"
#include "Settings.h"
#include "GhostEngine.h"
#include "CursorOverlay.h"

// Tray app + status window: ghost list with per-window eye toggle, power/info/exit
// toolbar, tray menu.
class MainWindow : public GhostEngine::Listener {
public:
    MainWindow();
    ~MainWindow();

    // startHidden keeps the main window in the tray on launch (used by autostart,
    // via the --taskbar flag) instead of popping it open.
    bool create(HINSTANCE, bool startHidden = false);

    // If an instance is already running, bring its window to the front. Returns
    // true if one was found (the new process should then exit).
    static bool activateExisting();

    // Closes a running instance and waits until it AND its injector helpers are
    // gone, so the files they hold open can be replaced. Returns false if anything
    // was still running when the budget ran out. Used by the installer.
    static bool requestShutdown(DWORD timeoutMs);

    // GhostEngine::Listener
    void onTrackedChanged() override;

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);

    void onCreate();
    void layout();
    void sizeListColumns();
    void applyTheme();
    void applyFont();
    void rebuildList();
    std::vector<std::pair<std::wstring, HWND>> sortedRows() const;   // label + window, list order
    void updateStatus();

    void drawButton(const DRAWITEMSTRUCT*);
    LRESULT listCustomDraw(NMLVCUSTOMDRAW*);
    COLORREF rowColor(HWND) const;   // dimmed for a ghost its app has hidden

    void showWindow();
    void showTrayMenu();
    void addTray();
    void retryTray();          // timer: re-add a tray icon the shell refused
    void warnHooksMissing();   // balloon when the x64 hook DLL failed to load

    void setGlobalEnabled(bool);
    void toggleWindow(HWND ghost);
    void applyCursorOverlay();   // pushes the zoom and the global switch to the overlay
    void showSettings();

    int S(int px) const { return MulDiv(px, dpi_, 96); }

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr, list_ = nullptr, power_ = nullptr, info_ = nullptr, exit_ = nullptr, gear_ = nullptr;
    HFONT uiFont_ = nullptr;
    HIMAGELIST rowSizer_ = nullptr;   // 1px-wide image list that forces a taller row height
    NOTIFYICONDATAW nid_{};
    UINT dpi_ = 96;
    UINT taskbarCreatedMsg_ = 0;   // "TaskbarCreated" broadcast: re-add the tray icon
    int trayRetries_ = 0;
    bool reallyExit_ = false;

    Theme theme_;
    Settings settings_;
    GhostEngine engine_;
    CursorOverlay overlay_;

    std::vector<HWND> menuWindows_;   // maps per-window menu ids to ghosts
};
