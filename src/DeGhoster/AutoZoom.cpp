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
        seen_[cur_].ms += std::min(dt, kMaxCreditMs);
    }
    Entry& e = seen_[key];
    if (visibleHeight > 0) e.height = visibleHeight;
    cur_ = key;
    haveCur_ = true;
    last_ = now;

    // The first picture is the reference until another one has clearly been on
    // screen longer.
    auto ref = seen_.find(ref_);
    if (ref == seen_.end() || ref->second.height <= 0) {
        ref_ = key;
        return;
    }
    for (auto& kv : seen_)
        if (kv.second.height > 0 && kv.second.ms > ref->second.ms * kHysteresis) {
            ref_ = kv.first;
            ref = seen_.find(ref_);
        }
}

int AutoZoom::zoom(int targetHeight) const
{
    auto ref = seen_.find(ref_);
    if (ref == seen_.end() || ref->second.height <= 0 || targetHeight <= 0) return 0;
    return Settings::ClampZoom(MulDiv(targetHeight, 100, ref->second.height));
}
