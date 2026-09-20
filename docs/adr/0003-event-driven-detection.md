# 0003 — Event-driven detection with `SetWinEventHook` (no polling)

- **Status:** Accepted
- **Deciders:** Emmo Emminghaus

## Context

Ghost windows appear and move at unpredictable times (app launch, virtual-desktop
switch, window relocation). DeGhoster must notice them promptly while staying
effectively invisible in idle CPU/energy use, since it runs from logon onward.

## Decision

Detect changes **event-driven** via out-of-context `SetWinEventHook`
(`WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS`), reacting only to
`EVENT_OBJECT_CREATE/SHOW/LOCATIONCHANGE` (check candidate → track/reconcile) and
`EVENT_OBJECT_DESTROY` (drop), filtered to `OBJID_WINDOW`/`CHILDID_SELF`. A one-time
`EnumWindows` scan at startup catches ghosts that already exist.

## Consequences

- **+** No polling loop → negligible idle cost; reacts within one event dispatch.
- **+** Simple mental model: the tracked set is a projection of window events.
- **−** `LOCATIONCHANGE` can be chatty; candidate checks must be cheap and the code
  must debounce/reconcile idempotently.
- **−** Relies on the app pumping messages (it does; the hook is out-of-context).

## Alternatives considered

- **Timer polling with `EnumWindows`:** rejected — constant wakeups, higher latency
  or higher cost, worse energy behavior.
