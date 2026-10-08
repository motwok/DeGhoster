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
#include <cstdint>
#include <deque>
#include <unordered_map>

// The automatic zoom of one session window (Specification.md section 10.4). The
// remote cursor that was shown the longest during the last kWindowMs is taken as
// the reference - in practice the normal arrow - and the zoom makes it as tall as
// the local arrow. No shape recognition: only how long each picture is on screen
// counts, and only recently, so the size follows a change within seconds.
class AutoZoom {
public:
    // The shown picture changed or stays; `now` in ms (GetTickCount64). The time
    // since the previous call is credited to the picture shown until now, capped
    // so a mouse resting on one shape does not outweigh real use.
    void observe(uint64_t key, int visibleHeight, ULONGLONG now);
    // The picture went off screen (overlay hidden, cursor over another window):
    // credits it until now and stops the clock, so the time until a picture is
    // next seen does not count.
    void pause(ULONGLONG now);

    // Zoom in percent that makes the reference picture `targetHeight` pixels tall,
    // on the settings' grid; 0 while nothing has been seen.
    int zoom(int targetHeight) const;

    uint64_t reference() const { return ref_; }

    static constexpr ULONGLONG kMaxCreditMs = 2000;
    // Only on-screen time within this span before the latest event counts.
    static constexpr ULONGLONG kWindowMs = 10000;
    // Another picture takes over the reference only once it has been shown this
    // much longer within the window, so the size does not flip between two
    // similar shares.
    static constexpr double kHysteresis = 1.5;

private:
    struct Span { uint64_t key; ULONGLONG end, ms; };
    std::deque<Span> spans_;                       // credited time, oldest first
    std::unordered_map<uint64_t, int> heights_;    // visible height per picture
    uint64_t cur_ = 0, ref_ = 0;
    bool haveCur_ = false, haveRef_ = false;
    ULONGLONG last_ = 0;
};
