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
#include "Settings.h"
#include "Theme.h"

// Modeless, single-instance settings window (Specification.md section 10.6).
// Changes apply and are saved at once; the owner is told with
// WM_SETTINGS_CHANGED so it can apply them. Built in sections; "AnyDesk cursor"
// is the first one.
class SettingsWindow {
public:
    static constexpr UINT WM_SETTINGS_CHANGED = WM_APP + 0x42;   // sent to the owner
    enum { IDC_CURSOR_AUTO = 1101, IDC_CURSOR_ZOOM, IDC_CLOSE };

    // `settings` must outlive the window (it is the owner's own object).
    static void Show(HINSTANCE, HWND owner, const Theme&, UINT dpi, Settings& settings);
    static HWND ActiveHandle();   // for the main loop's IsDialogMessage; null when closed
    // Re-reads the settings into the controls (changed from the tray or the
    // global switch while the window is open).
    static void Sync();

private:
    explicit SettingsWindow(Settings& s) : settings_(s) {}
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);
    void createControls();
    void applyDpiAssets();
    void applyTheme();
    SIZE measure();          // lays the content out; returns the client size it needs
    void layout();
    void paint(HDC);
    void sync();
    void drawCloseButton(const DRAWITEMSTRUCT*);
    int S(int px) const { return MulDiv(px, dpi_, 96); }

    Settings& settings_;
    HWND hwnd_ = nullptr, owner_ = nullptr;
    HWND auto_ = nullptr, zoom_ = nullptr, close_ = nullptr;
    HFONT uiFont_ = nullptr, titleFont_ = nullptr;
    HBRUSH brush_ = nullptr;
    Theme theme_;
    UINT dpi_ = 96;

    // Layout (client coordinates), computed by measure().
    RECT rcSection_{}, rcAuto_{}, rcZoomLabel_{}, rcZoom_{}, rcHint_{}, rcClose_{};

    static HWND s_active;
};
