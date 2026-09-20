# 0002 — Neutralize ghosts with `DWMWA_CLOAK` from inside the target

- **Status:** Accepted
- **Deciders:** Emmo Emminghaus

## Context

A ghost window is on-screen, layered, fully transparent (`alpha 0`) but **not**
click-through, so it eats desktop clicks in its rectangle. We must make it stop
receiving hit-tests without destroying it (it belongs to another app) and without
disturbing that app's real windows.

`DwmSetWindowAttribute(hwnd, DWMWA_CLOAK, 1)` removes a window from composition
**and** hit-testing — clicks fall through to whatever is behind it. But DWM cloaking
with `DWM_CLOAKED_APP` only takes effect when set **from inside the owning process**;
a cross-process call does not cloak the window for the app.

## Decision

Neutralize a ghost by **cloaking it in-process**: inject a small hook DLL
(`WH_GETMESSAGE`) into the ghost window's thread, and have the DLL call
`DwmSetWindowAttribute(DWMWA_CLOAK)` when the host posts it a `DGH_CLOAK` command.
The DLL re-verifies the ghost criteria before acting and auto-uncloaks everything on
`DLL_PROCESS_DETACH`. Detection and orchestration stay in the host; only the cloak
runs in the target.

## Consequences

- **+** Reliable, reversible neutralization; the window keeps existing, the owning
  app is unaffected, clicks reach the desktop again.
- **+** Nothing is destroyed — a false positive is fully recoverable (uncloak).
- **−** Requires DLL injection into foreign processes (see [0005](0005-separate-32bit-helper.md)
  for the x86 path) and a host↔DLL message protocol.
- **−** Injecting into third-party processes can look suspicious to security tools.

## Alternatives considered

- **Cross-process `DwmSetWindowAttribute`:** rejected — does not cloak with
  `DWM_CLOAKED_APP`.
- **Destroying / hiding the window from outside** (`ShowWindow`, `DestroyWindow`):
  rejected — not our window; risks breaking the owning app and is irreversible.
- **Making it click-through** (`WS_EX_TRANSPARENT` via `SetWindowLong` cross-process):
  rejected — unreliable across processes and mutates foreign window state.
