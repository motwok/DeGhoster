# 0005 — Inject 32-bit targets via a separate `Helper32.exe`

- **Status:** Accepted
- **Deciders:** Emmo Emminghaus

## Context

The neutralization (ADR [0002](0002-in-process-cloak-via-hook-dll.md)) injects a hook
DLL into the ghost window's process. The host is x64, but ghost windows can belong to
**32-bit** processes. An x64 process cannot load a 32-bit DLL, and `SetWindowsHookEx`
needs a DLL matching the target's bitness.

## Decision

Ship both `DeGhoster.Hook64.dll` (x64) and `DeGhoster.Hook32.dll` (x86). For an x64
target the host installs Hook64 directly. For an x86 target the host launches
`DeGhoster.Helper32.exe <threadId> <hostHwnd> <hostPid>`; the helper (x86) loads
Hook32, installs the hook on that thread, and holds it until the host process exits
(then removes the hook and quits). Hooks are deduplicated per **thread id** — one
helper per hooked 32-bit thread. Detection and cloak commands remain with the host.

## Consequences

- **+** Full coverage of both 32- and 64-bit ghost hosts.
- **+** Helper lifetime is bound to the host, so hooks are cleaned up on exit.
- **−** An extra executable to ship and a small process per hooked 32-bit thread.
- **−** Cross-process/-bitness handle passing must be done carefully.

## Alternatives considered

- **x86-only or x64-only host:** rejected — would miss half of the possible targets.
