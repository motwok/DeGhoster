# 0010 — Integration tests via a native ghost simulator

- **Status:** Accepted
- **Deciders:** Emmo Emminghaus
- **Relates to:** [0002](0002-in-process-cloak-via-hook-dll.md), [0005](0005-separate-32bit-helper.md)

## Context

DeGhoster's core value is neutralizing ghost windows, which involves cross-process
DLL injection and an in-process DWM cloak — behavior that unit tests of isolated
functions cannot meaningfully cover. We need a test that proves the real thing works,
for **both** the x64 (Hook64 direct) and x86 (Helper32 → Hook32) paths.

`GhostEngine::IsBlocker` classifies ghosts purely by window properties (class
`Chrome_WidgetWin_1`, layered, alpha 0 + `LWA_ALPHA`, on-screen, not cloaked) — it does
**not** require the owning process to be `msedgewebview2`. So a purpose-built window can
stand in for a real WebView2 ghost.

## Decision

Ship two test artifacts under `tests/`:

- **GhostSim** — a tiny **C++** program (built for x64 and x86 through the existing
  CMake, no runtime dependency) that creates a real window meeting every ghost
  criterion, with a unique title, and self-exits after a timeout.
- **DeGhoster.Tests** — an **xUnit (.NET)** project that launches GhostSim and
  DeGhoster and asserts the window becomes `DWMWA_CLOAKED`. A `[Theory]` runs both
  bitnesses.

The assertion is **`DWMWA_CLOAKED` becoming set**, read cross-process — that is the
mechanism that makes clicks fall through and the cursor return, and it is far more
reliable than trying to observe click/cursor behavior directly. Tests run locally and
as a required check in the CI Windows job; GhostSim is not shipped.

## Consequences

- **+** Real end-to-end proof of neutralization on both injection paths; regressions
  in injection, the host↔hook protocol, or the criteria are caught.
- **+** The C++ simulator is faithful (a WebView2 ghost window is native, not .NET) and
  needs no extra runtime; only the test runner adds a dev-time .NET/xUnit dependency.
- **+** Writing these tests already surfaced two real x86 defects (stdcall export
  decoration on Hook32, and a cloak-before-hook-ready race) which were then fixed.
- **−** Tests need an interactive desktop with DWM; they cannot run headless/session-0.
- **−** GUI/injection timing means the assertions must poll with timeouts.

## Alternatives considered

- **Unit-test `IsBlocker` in isolation:** rejected as the sole approach — it would test
  detection but not the injection/cloak that is the whole point (though it could be
  added later if `IsBlocker` is exposed).
- **Drive a real WebView2 app:** rejected — heavyweight, non-deterministic, and not
  needed since detection is process-agnostic.
