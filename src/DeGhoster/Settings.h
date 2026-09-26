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
#include <string>
#include <unordered_set>

// HKCU\Software\DeGhoster: global switch, per-window opt-outs (FixInfo::disableKey)
// and the AnyDesk cursor overlay (on/off + zoom).
class Settings {
public:
    static constexpr int kZoomMin = 100, kZoomMax = 600, kZoomStep = 10;

    void load();

    bool globalEnabled() const { return globalEnabled_; }
    void setGlobalEnabled(bool);

    bool isManaged(const std::wstring& key) const;
    void setManaged(const std::wstring& key, bool managed);

    bool cursorOverlayEnabled() const { return cursorOverlayEnabled_; }
    void setCursorOverlayEnabled(bool);

    // Zoom in percent, always within [kZoomMin, kZoomMax] on a kZoomStep grid.
    int cursorOverlayZoom() const { return cursorOverlayZoom_; }
    void setCursorOverlayZoom(int percent);

    // Clamps and snaps any percentage onto the zoom grid.
    static int ClampZoom(int percent);
    // The first-start zoom: the given monitor scaling (dpi 240 -> 250 %), clamped.
    static int ZoomForDpi(unsigned dpi);

private:
    bool globalEnabled_ = true;
    bool cursorOverlayEnabled_ = true;
    int cursorOverlayZoom_ = kZoomMin;
    std::unordered_set<std::wstring> disabled_;
};
