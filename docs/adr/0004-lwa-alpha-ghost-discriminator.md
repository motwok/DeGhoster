# 0004 — Use the `LWA_ALPHA` flag to tell ghosts from real layered windows

- **Status:** Accepted
- **Deciders:** Emmo Emminghaus

## Context

Ghost windows are fully transparent layered windows (`alpha 0`). But many **real,
visible** windows are also layered and can report `alpha 0` — notably windows that
paint themselves with per-pixel alpha via `UpdateLayeredWindow` (e.g. Discord's
rounded window). Cloaking one of those would wrongly hide a legitimate window.

## Decision

Require the discriminator: a window is a ghost only when
`GetLayeredWindowAttributes` **succeeds and returns the `LWA_ALPHA` flag with
`alpha == 0`**. Windows that use per-pixel alpha (`UpdateLayeredWindow`) return
`alpha 0` **without** `LWA_ALPHA`, so they are excluded. This sits alongside the
other criteria (class `Chrome_WidgetWin_1`, visible, `WS_EX_LAYERED` set,
`WS_EX_TRANSPARENT` not set, on-screen, not already cloaked).

## Consequences

- **+** Avoids false positives against real per-pixel-alpha windows.
- **+** Cheap boolean check available from any process.
- **−** Tightly coupled to current Chromium ghost behavior; a different leftover
  window shape would need the criteria revisited.

## Alternatives considered

- **Treat any `alpha == 0` layered window as a ghost:** rejected — hides legitimate
  `UpdateLayeredWindow` windows.
