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

// Hook.cpp - x64 Hook-DLL fuer DeGhoster.
//
// Zweck: unsichtbare Klick-Fresser (alpha-0 Layered-Fenster von Chromium/WebView2,
//        Klasse Chrome_WidgetWin_1) IN-PROCESS per DWMWA_CLOAK neutralisieren.
//        Cross-process laesst sich DWMWA_CLOAK nicht setzen -> deshalb der Inject.
//        Ein gecloaktes Fenster bekommt kein Hit-Testing -> Klicks fallen auf den Desktop.
//
// Ablauf: Host erkennt einen Blocker, injiziert (WH_GETMESSAGE) in dessen Thread und
//         postet DGH_CLOAK an das Fenster. Der Hook faengt die Message ab und cloakt.
// Sicherheit: Auto-Uncloak aller getrackten Fenster bei DLL_PROCESS_DETACH.

#include <windows.h>
#include <dwmapi.h>
#include <vector>
#include <algorithm>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")

#include "Hook.h"

// --- Zustand (prozessweit geteilt ueber alle Threads der DLL-Instanz) -------

#pragma data_seg(".dghshared")
HWND g_hostHwnd = NULL;
BOOL g_active   = TRUE;
#pragma data_seg()
#pragma comment(linker, "/section:.dghshared,rws")

HINSTANCE g_appInstance = NULL;

static CRITICAL_SECTION g_lock;
static bool g_lockInit = false;
static std::vector<HWND>* g_cloaked = nullptr;   // von uns gecloakte Fenster

static void EnsureInit()
{
    if (!g_lockInit)
    {
        InitializeCriticalSection(&g_lock);
        g_cloaked = new std::vector<HWND>();
        g_lockInit = true;
    }
}

static bool IsTracked(HWND h)
{
    return std::find(g_cloaked->begin(), g_cloaked->end(), h) != g_cloaked->end();
}

static void SetCloak(HWND hwnd, BOOL on)
{
    int v = on ? 1 : 0;
    DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, &v, sizeof(v));
}

// Prueft, ob das Fenster wirklich ein unsichtbarer (alpha-0) Klick-Fresser ist.
// Schutz davor, versehentlich ein echtes/sichtbares Fenster zu cloaken.
static bool QualifiesAsGhost(HWND hwnd)
{
    if (!IsWindow(hwnd)) return false;
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (!(ex & WS_EX_LAYERED)) return false;
    if (ex & WS_EX_TRANSPARENT) return false;   // schon klick-transparent -> harmlos
    BYTE alpha = 255; COLORREF cr = 0; DWORD fl = 0;
    if (!GetLayeredWindowAttributes(hwnd, &cr, &alpha, &fl)) return false;
    return alpha == 0;                           // vollstaendig unsichtbar
}

static void DoCloak(HWND hwnd)
{
    if (!g_active) return;
    EnsureInit();
    EnterCriticalSection(&g_lock);
    bool notify = false;
    if (!IsTracked(hwnd) && QualifiesAsGhost(hwnd))
    {
        SetCloak(hwnd, TRUE);
        g_cloaked->push_back(hwnd);
        notify = true;
    }
    LeaveCriticalSection(&g_lock);
    if (notify && g_hostHwnd)
        SendNotifyMessageW(g_hostHwnd, WM_DGH_CLOAKED, (WPARAM)hwnd, 0);
}

static void DoUncloak(HWND hwnd)
{
    EnsureInit();
    EnterCriticalSection(&g_lock);
    bool notify = false;
    if (IsTracked(hwnd))
    {
        if (IsWindow(hwnd)) SetCloak(hwnd, FALSE);
        g_cloaked->erase(std::remove(g_cloaked->begin(), g_cloaked->end(), hwnd), g_cloaked->end());
        notify = true;
    }
    LeaveCriticalSection(&g_lock);
    if (notify && g_hostHwnd)
        SendNotifyMessageW(g_hostHwnd, WM_DGH_UNCLOAKED, (WPARAM)hwnd, 0);
}

// --- Hook-Callback ----------------------------------------------------------

static LRESULT CALLBACK GetMsgProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && wParam == PM_REMOVE)
    {
        MSG* m = (MSG*)lParam;
        if (m->message == DGH_CLOAK)        { DoCloak(m->hwnd);   m->message = WM_NULL; }
        else if (m->message == DGH_UNCLOAK) { DoUncloak(m->hwnd); m->message = WM_NULL; }
    }
    return CallNextHookEx(NULL, code, wParam, lParam);
}

// --- Exporte ----------------------------------------------------------------

// Exported via exports.def (plain names on x86 + x64); no __declspec(dllexport)
// so the x86 __stdcall name stays undecorated for GetProcAddress.
extern "C" HHOOK __stdcall DgInstallHook(DWORD threadId, HWND hostHwnd)
{
    if (g_appInstance == NULL) return NULL;
    g_hostHwnd = hostHwnd;
    g_active = TRUE;
    return SetWindowsHookExW(WH_GETMESSAGE, GetMsgProc, g_appInstance, threadId);
}

extern "C" BOOL __stdcall DgRemoveHook(HHOOK hook)
{
    return hook ? UnhookWindowsHookEx(hook) : FALSE;
}

// --- DllMain ----------------------------------------------------------------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
            if (g_appInstance == NULL) g_appInstance = hModule;
            DisableThreadLibraryCalls(hModule);
            break;

        case DLL_PROCESS_DETACH:
            if (g_lockInit && g_cloaked)
            {
                EnterCriticalSection(&g_lock);
                for (HWND h : *g_cloaked) if (IsWindow(h)) SetCloak(h, FALSE);
                g_cloaked->clear();
                LeaveCriticalSection(&g_lock);
            }
            break;
    }
    return TRUE;
}
