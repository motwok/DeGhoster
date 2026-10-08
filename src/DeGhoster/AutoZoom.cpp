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

#include "AutoZoom.h"
#include "Settings.h"
#include <algorithm>

void AutoZoom::observe(uint64_t key, int visibleHeight, ULONGLONG now)
{
    if (haveCur_) {
        const ULONGLONG dt = now > last_ ? now - last_ : 0;
        if (dt) spans_.push_back({ cur_, now, std::min(dt, kMaxCreditMs) });
    }
    if (visibleHeight > 0) heights_[key] = visibleHeight;
    cur_ = key;
    haveCur_ = true;
    last_ = now;

    // Forget what happened before the window. A span that straddles its start
    // counts only with its part inside.
    const ULONGLONG start = now > kWindowMs ? now - kWindowMs : 0;
    while (!spans_.empty() && spans_.front().end <= start) spans_.pop_front();

    auto visible = [this](uint64_t k) {
        auto it = heights_.find(k);
        return it != heights_.end() && it->second > 0;
    };
    // The first visible picture is the reference until another one has clearly
    // been on screen longer.
    if (!haveRef_ || !visible(ref_)) {
        if (visible(key)) { ref_ = key; haveRef_ = true; }
        return;
    }

    std::unordered_map<uint64_t, ULONGLONG> shown;
    for (const Span& s : spans_) {
        const ULONGLONG begin = s.end - s.ms;
        shown[s.key] += begin >= start ? s.ms : s.end - start;
    }
    uint64_t best = ref_;
    ULONGLONG bestMs = shown[ref_];
    for (auto& kv : shown)
        if (visible(kv.first) && kv.second > bestMs) { best = kv.first; bestMs = kv.second; }
    if (best != ref_ && bestMs > shown[ref_] * kHysteresis) ref_ = best;
}

void AutoZoom::pause(ULONGLONG now)
{
    if (!haveCur_) return;
    const ULONGLONG dt = now > last_ ? now - last_ : 0;
    if (dt) spans_.push_back({ cur_, now, std::min(dt, kMaxCreditMs) });
    haveCur_ = false;
}

int AutoZoom::zoom(int targetHeight) const
{
    if (!haveRef_ || targetHeight <= 0) return 0;
    auto it = heights_.find(ref_);
    if (it == heights_.end() || it->second <= 0) return 0;
    return Settings::ClampZoom(MulDiv(targetHeight, 100, it->second));
}
