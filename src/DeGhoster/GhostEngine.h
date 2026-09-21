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
#include <unordered_map>
#include <unordered_set>
#include "FixInfo.h"
#include "HookInjector.h"

class Settings;

// Event-driven detection + cloaking core (SetWinEventHook, on-demand injection,
// reconcile against settings).
class GhostEngine {
public:
    struct Listener {
        virtual void onTrackedChanged() = 0;
    };

    explicit GhostEngine(Settings& settings);

    void start(HWND host);
    void stop();
    void setListener(Listener* l) { listener_ = l; }

    const std::unordered_map<HWND, FixInfo>& tracked() const { return tracked_; }
    int ghostCount() const { return (int)tracked_.size(); }

    void refreshAll();
    void expirePending();    // drop cloak/uncloak requests the hook never answered

    void onCloaked(HWND);    // hook replies routed from the host window
    void onUncloaked(HWND);

private:
    static void CALLBACK winEventThunk(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    void onWinEvent(DWORD event, HWND);
    void handleCandidate(HWND);
    void reconcile(HWND);
    bool desiredCloaked(const FixInfo&, HWND) const;
    bool isBadPid(DWORD pid);   // true while a failed pid is still in its cooldown

    Settings& settings_;
    Listener* listener_ = nullptr;
    HWND host_ = nullptr;

    HookInjector hooks_;
    std::unordered_map<HWND, FixInfo> tracked_;
    std::unordered_set<HWND> cloaked_;
    std::unordered_map<HWND, ULONGLONG> pending_;   // hwnd -> tick when the request was posted
    std::unordered_map<DWORD, ULONGLONG> badPids_;  // failed hosts -> tick; retried after a cooldown
    HWINEVENTHOOK we1_ = nullptr, we2_ = nullptr;

    static GhostEngine* s_instance;   // for the out-of-context win-event thunk
};
