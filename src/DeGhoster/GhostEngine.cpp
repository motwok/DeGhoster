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
#include <algorithm>
#include <vector>

#pragma comment(lib, "dwmapi.lib")

namespace {

constexpr wchar_t kTargetClass[] = L"Chrome_WidgetWin_1";

// LWA_ALPHA flag + alpha 0 distinguishes a ghost from a per-pixel-alpha window.
// `ignoreCloak` skips the not-cloaked test so an already-fixed window can still be
// re-validated: it is cloaked precisely because we cloaked it, and testing that bit
// would report every window we fixed as "no longer a ghost" the moment we fixed it.
bool IsBlocker(HWND h, bool ignoreCloak = false)
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

    if (!ignoreCloak) {
        int cloak = 0;
        if (DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloak, sizeof(cloak)) != S_OK || cloak != 0)
            return false;
    }
    return true;
}

// A cloak/uncloak request whose reply never arrives (dead hook, hung thread, a
// window that stopped qualifying) is dropped after this long, so pending_ can
// never block a window's reconcile permanently.
constexpr ULONGLONG kPendingTimeoutMs = 3000;
// A host we failed to inject is retried after this cooldown instead of being
// blacklisted for the whole session (the failure is often transient).
constexpr ULONGLONG kBadPidCooldownMs = 15000;

// The hook sets and clears only the *app* cloak bit. A window can be
// shell-cloaked as well (it lives on another virtual desktop, say), and that bit
// is not ours to clear, so waiting for the whole attribute to reach 0 would spin
// until the deadline. IsBlocker() deliberately tests all bits; this tests ours.
#ifndef DWM_CLOAKED_APP
#define DWM_CLOAKED_APP 0x00000001
#endif

bool AppCloaked(HWND h)
{
    int c = 0;
    if (DwmGetWindowAttribute(h, DWMWA_CLOAKED, &c, sizeof(c)) != S_OK) return false;
    return (c & DWM_CLOAKED_APP) != 0;
}

} // namespace

GhostEngine* GhostEngine::s_instance = nullptr;

GhostEngine::GhostEngine(Settings& settings) : settings_(settings) {}

void GhostEngine::start(HWND host)
{
    host_ = host;
    s_instance = this;
    hooksLoaded_ = hooks_.load(proc::ExeDir());

    EnumWindows([](HWND h, LPARAM) -> BOOL { s_instance->handleCandidate(h); return TRUE; }, 0);

    we1_ = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, nullptr, winEventThunk,
                           0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    we2_ = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, winEventThunk,
                           0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

void GhostEngine::stop(DWORD uncloakBudgetMs)
{
    if (we1_) UnhookWinEvent(we1_);
    if (we2_) UnhookWinEvent(we2_);
    we1_ = we2_ = nullptr;

    pending_.clear();                       // nothing may re-post a cloak now
    uncloakAllAndWait(uncloakBudgetMs);     // must precede removeAll(): the hook
                                            // has to still be installed to act
    hooks_.removeAll();
    s_instance = nullptr;
}

void GhostEngine::uncloakAllAndWait(DWORD budgetMs)
{
    if (cloaked_.empty()) return;

    std::vector<HWND> waiting;
    waiting.reserve(cloaked_.size());
    for (HWND h : cloaked_)
        if (IsWindow(h) && PostMessageW(h, DGH_UNCLOAK, 0, 0))
            waiting.push_back(h);

    // Wait on the observable DWM state rather than the hook's WM_DGH_UNCLOAKED
    // reply: stop() runs inside WM_DESTROY/WM_ENDSESSION, where pumping a nested
    // message loop would re-enter the window procedure already tearing the window
    // down, and a dispatched reply could re-inject through reconcile().
    const ULONGLONG deadline = GetTickCount64() + budgetMs;
    while (!waiting.empty() && GetTickCount64() < deadline) {
        waiting.erase(std::remove_if(waiting.begin(), waiting.end(),
                                     [](HWND h) { return !IsWindow(h) || !AppCloaked(h); }),
                      waiting.end());
        if (waiting.empty()) break;
        Sleep(15);
    }

    // Anything still waiting never pumped our message (hung, or already exiting);
    // the DLL_PROCESS_DETACH path in the hook stays the fallback for those.
    cloaked_.clear();
}

void CALLBACK GhostEngine::winEventThunk(HWINEVENTHOOK, DWORD ev, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD)
{
    if (!s_instance || !hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    s_instance->onWinEvent(ev, hwnd);
}

void GhostEngine::onWinEvent(DWORD event, HWND hwnd)
{
    if (event == EVENT_OBJECT_DESTROY) {
        untrack(hwnd);
        return;
    }
    handleCandidate(hwnd);
}

bool GhostEngine::isBadPid(DWORD pid)
{
    auto it = badPids_.find(pid);
    if (it == badPids_.end()) return false;
    if (GetTickCount64() - it->second >= kBadPidCooldownMs) {
        badPids_.erase(it);   // cooldown elapsed: allow a retry
        return false;
    }
    return true;
}

void GhostEngine::handleCandidate(HWND h)
{
    if (tracked_.count(h) || !IsBlocker(h)) return;
    DWORD pid = 0, tid = GetWindowThreadProcessId(h, &pid);
    if (!tid || isBadPid(pid)) return;

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
        // Without the cooldown check a failed host would be re-attempted on every
        // tick; isBadPid() also expires the entry once the cooldown has lapsed,
        // which is what makes the periodic retry in tick() eventually take effect.
        if (!tid || isBadPid(pid)) return;
        switch (hooks_.ensure(tid, pid, host_)) {
        case HookInjector::Inject::Ready:
            if (PostMessageW(h, DGH_CLOAK, 0, 0))
                pending_[h] = GetTickCount64();
            break;
        case HookInjector::Inject::Pending:
            break;            // helper still starting; tick() comes back for it
        case HookInjector::Inject::Failed:
            badPids_[pid] = GetTickCount64();
            break;
        }
    } else if (!desired && isCloaked) {
        if (PostMessageW(h, DGH_UNCLOAK, 0, 0))
            pending_[h] = GetTickCount64();
    }
}

void GhostEngine::untrack(HWND h)
{
    // If we cloaked it, reveal it again: the window either vanished or stopped
    // qualifying, and leaving our cloak on a window that became legitimate would
    // hide it for good.
    if (cloaked_.count(h) && IsWindow(h)) PostMessageW(h, DGH_UNCLOAK, 0, 0);
    pending_.erase(h);
    cloaked_.erase(h);
    if (tracked_.erase(h) && listener_) listener_->onTrackedChanged();
}

void GhostEngine::tick()
{
    expirePending();
    hooks_.pruneDead();

    // Qualification is otherwise only ever tested when a window is first seen, so
    // a window that stops being a ghost (alpha back to 255, layering dropped) would
    // stay tracked and cloaked forever. No win-event covers those changes, which is
    // why this is a sweep rather than an event handler.
    std::vector<HWND> stale, idle;
    for (auto& kv : tracked_) {
        HWND h = kv.first;
        if (!IsWindow(h) || !IsBlocker(h, /*ignoreCloak*/ true)) { stale.push_back(h); continue; }
        if (!cloaked_.count(h) && !pending_.count(h)) idle.push_back(h);
    }
    for (HWND h : stale) untrack(h);
    for (HWND h : idle)  reconcile(h);   // picks up ready helpers and lapsed cooldowns

    // Detection is otherwise purely event-driven, but the properties that make a
    // window a ghost (alpha, layering) can change without raising any win-event, so
    // a window released above would never be picked up again. Re-scanning is only
    // affordable here because ensure() no longer blocks: handleCandidate() can now
    // start a helper from inside this callback without freezing the UI thread.
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        reinterpret_cast<GhostEngine*>(lp)->handleCandidate(h);
        return TRUE;
    }, (LPARAM)this);
}

void GhostEngine::expirePending()
{
    ULONGLONG now = GetTickCount64();
    std::vector<HWND> stale;
    for (auto& kv : pending_)
        if (now - kv.second >= kPendingTimeoutMs) stale.push_back(kv.first);
    for (HWND h : stale) {
        pending_.erase(h);
        reconcile(h);   // no reply came; re-evaluate (a live window will be retried)
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
    // A reply can arrive after the window was destroyed and untracked; don't
    // resurrect a stale HWND in cloaked_ (it would never be cleaned up).
    if (!tracked_.count(h) || !IsWindow(h)) { cloaked_.erase(h); return; }
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
