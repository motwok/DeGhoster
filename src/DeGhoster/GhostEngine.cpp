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

#include "GhostEngine.h"
#include "Settings.h"
#include "ProcessUtil.h"

#include <dwmapi.h>          // must precede Hook.h: DWMWA_CLOAKED is an SDK enum here
#include "Hook.h"            // DGH_CLOAK / DGH_UNCLOAK
#include <vector>

#pragma comment(lib, "dwmapi.lib")

namespace {

constexpr wchar_t kTargetClass[] = L"Chrome_WidgetWin_1";

// LWA_ALPHA flag + alpha 0 distinguishes a ghost from a per-pixel-alpha window.
bool IsBlocker(HWND h)
{
    if (!IsWindowVisible(h)) return false;
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
    if (!(ex & WS_EX_LAYERED) || (ex & WS_EX_TRANSPARENT)) return false;

    BYTE alpha = 255; COLORREF cr = 0; DWORD flags = 0;
    if (!GetLayeredWindowAttributes(h, &cr, &alpha, &flags)) return false;
    if (!(flags & LWA_ALPHA) || alpha != 0) return false;

    wchar_t cls[64] = L"";
    GetClassNameW(h, cls, 64);
    if (lstrcmpW(cls, kTargetClass) != 0) return false;

    RECT r;
    if (!GetWindowRect(h, &r) || r.right <= r.left || r.bottom <= r.top) return false;
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vr = vx + GetSystemMetrics(SM_CXVIRTUALSCREEN), vb = vy + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (!(r.right > vx && r.left < vr && r.bottom > vy && r.top < vb)) return false;

    int cloak = 0;
    if (DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloak, sizeof(cloak)) != S_OK || cloak != 0) return false;
    return true;
}

} // namespace

GhostEngine* GhostEngine::s_instance = nullptr;

GhostEngine::GhostEngine(Settings& settings) : settings_(settings) {}

void GhostEngine::start(HWND host)
{
    host_ = host;
    s_instance = this;
    hooks_.load(proc::ExeDir());

    EnumWindows([](HWND h, LPARAM) -> BOOL { s_instance->handleCandidate(h); return TRUE; }, 0);

    we1_ = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, nullptr, winEventThunk,
                           0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    we2_ = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, winEventThunk,
                           0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

void GhostEngine::stop()
{
    if (we1_) UnhookWinEvent(we1_);
    if (we2_) UnhookWinEvent(we2_);
    we1_ = we2_ = nullptr;
    hooks_.removeAll();
    s_instance = nullptr;
}

void CALLBACK GhostEngine::winEventThunk(HWINEVENTHOOK, DWORD ev, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD)
{
    if (!s_instance || !hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    s_instance->onWinEvent(ev, hwnd);
}

void GhostEngine::onWinEvent(DWORD event, HWND hwnd)
{
    if (event == EVENT_OBJECT_DESTROY) {
        pending_.erase(hwnd);
        cloaked_.erase(hwnd);
        if (tracked_.erase(hwnd) && listener_) listener_->onTrackedChanged();
        return;
    }
    handleCandidate(hwnd);
}

void GhostEngine::handleCandidate(HWND h)
{
    if (tracked_.count(h) || !IsBlocker(h)) return;
    DWORD pid = 0, tid = GetWindowThreadProcessId(h, &pid);
    if (!tid || badPids_.count(pid)) return;

    FixInfo fi;
    fi.pid = pid;
    fi.title = proc::WindowTitle(h);
    proc::ResolveHostExe(pid, fi.exeName, fi.exePath);
    tracked_[h] = fi;
    if (listener_) listener_->onTrackedChanged();
    reconcile(h);
}

bool GhostEngine::desiredCloaked(const FixInfo& fi, HWND h) const
{
    return settings_.globalEnabled() && settings_.isManaged(fi.disableKey()) && IsWindow(h);
}

void GhostEngine::reconcile(HWND h)
{
    auto it = tracked_.find(h);
    if (it == tracked_.end()) return;

    bool desired = desiredCloaked(it->second, h);
    bool isCloaked = cloaked_.count(h) != 0;
    if (pending_.count(h)) return;

    if (desired && !isCloaked) {
        DWORD pid = 0, tid = GetWindowThreadProcessId(h, &pid);
        if (tid && hooks_.ensure(tid, pid, host_)) {
            pending_.insert(h);
            PostMessageW(h, DGH_CLOAK, 0, 0);
        } else if (tid) {
            badPids_.insert(pid);
        }
    } else if (!desired && isCloaked) {
        pending_.insert(h);
        PostMessageW(h, DGH_UNCLOAK, 0, 0);
    }
}

void GhostEngine::refreshAll()
{
    std::vector<HWND> v;
    v.reserve(tracked_.size());
    for (auto& kv : tracked_) v.push_back(kv.first);
    for (HWND h : v) reconcile(h);
}

void GhostEngine::onCloaked(HWND h)
{
    pending_.erase(h);
    cloaked_.insert(h);
    reconcile(h);   // re-check: state may have flipped while the reply was in flight
}

void GhostEngine::onUncloaked(HWND h)
{
    pending_.erase(h);
    cloaked_.erase(h);
    if (!IsWindow(h)) {
        if (tracked_.erase(h) && listener_) listener_->onTrackedChanged();
    } else {
        reconcile(h);
    }
}
