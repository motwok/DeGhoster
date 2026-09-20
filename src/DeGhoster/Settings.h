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

// HKCU\Software\DeGhoster: global switch + per-window opt-outs (FixInfo::disableKey).
class Settings {
public:
    void load();

    bool globalEnabled() const { return globalEnabled_; }
    void setGlobalEnabled(bool);

    bool isManaged(const std::wstring& key) const;
    void setManaged(const std::wstring& key, bool managed);

private:
    bool globalEnabled_ = true;
    std::unordered_set<std::wstring> disabled_;
};
