# 0011 — Inject every target through a helper process of matching bitness

- **Status:** Accepted
- **Deciders:** Emmo Emminghaus
- **Supersedes:** [0005](0005-separate-32bit-helper.md)

## Context

ADR [0005](0005-separate-32bit-helper.md) gave the two bitnesses two different
mechanisms: the host installed `Hook64` itself, and only 32-bit targets went through
`Helper32.exe`. Two paths for one job turned out to be the more expensive half of the
deal, and the direct x64 path was the weaker of the two:

- **No watchdog.** The helper waits on the host process, so an x86 hook is removed
  even when the host dies without running its teardown. The in-process x64 hook had no
  such backstop: after a `TerminateProcess` (crash, Task Manager) the cloak stayed on
  the windows and the hook DLL stayed mapped in the targets.
- **Two code paths to reason about.** Every change to injection, cleanup or lifetime
  had to be thought through twice, and the x64 half was the one without the safety net.
- **The host held `Hook64.dll` open.** A `LoadLibrary` in the host meant the host
  itself locked a file the installer wants to replace during an update.

## Decision

The host never loads a hook DLL. Every injection goes through
`DeGhoster.Helper<bits>.exe <threadId> <hostHwnd> <hostPid>`, chosen by the bitness of
the target process (`IsWow64Process`). One source, `src/DeGhoster.Helper/`, is built
for both bitnesses, exactly as the hook DLL is.

Each helper loads the matching hook DLL, installs the hook, signals
`Local\DeGhoster.HelperReady.<hostPid>.<threadId>` so the host knows when it may post
`DGH_CLOAK`, and then waits on both the host process and the hooked thread. When
either ends it removes the hook, nudges the target so the loader unmaps the DLL, and
quits. Entries stay deduplicated per **thread id**, pinned to the thread's creation
time so a recycled id is not mistaken for a live hook.

Because a helper can no longer report success synchronously, `HookInjector::ensure()`
returns `Ready`/`Pending`/`Failed` and the host's periodic tick re-drives anything
still pending, instead of blocking the UI thread on the readiness event.

## Consequences

- **+** One injection path, one lifetime model, one set of failure modes.
- **+** Every hook has a watchdog: a hard-killed host no longer leaves cloaked windows
  or mapped hook DLLs behind, for either bitness.
- **+** The host holds no hook DLL open, so it does not lock one itself during an
  update.
- **+** The UI thread never blocks on injection, which also makes a periodic re-scan
  for candidates affordable.
- **−** One helper process per hooked thread instead of an in-process call for x64.
- **−** Cloaking an x64 ghost now costs a process start plus the readiness handshake,
  so it takes up to a tick longer than the old direct call.
- **−** A second executable to ship (`DeGhoster.Helper64.exe`).

## Alternatives considered

- **Keep both paths, add a watchdog to the x64 one:** rejected — a watchdog process
  cannot unhook what another process hooked, because `HHOOK` is process-local. It
  would have had to install the hook itself, which is this decision.
- **Self-watch inside the hook DLL** (the DLL notices the host is gone and uncloaks
  itself): rejected as the primary mechanism — it can undo the cloak but cannot remove
  the hook, so it treats the symptom and leaves two paths in place.
