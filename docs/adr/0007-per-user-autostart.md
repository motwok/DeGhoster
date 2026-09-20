# 0007 — Autostart always per-user (HKCU), never machine-wide

- **Status:** Accepted
- **Deciders:** Emmo Emminghaus

## Context

DeGhoster is a per-user tray tool that should start at sign-in. It must **never** be
registered in the machine-wide autostart (`HKLM\...\Run`): it needs each user's own
session/desktop, and a system-wide entry would run it in the wrong contexts. Yet the
installer also offers a per-machine ("Global") install (ADR
[0008](0008-versioning-and-packaging.md)), where the program files live under
`Program Files` but the autostart must still be per-user for every user of the PC.

## Decision

The autostart entry is **always** written to `HKCU\Software\Microsoft\Windows\
CurrentVersion\Run`, never to HKLM:

- **Per-user install:** a direct HKCU `Run` registry value (self-cleaning on
  uninstall).
- **Per-machine install:** an **Active Setup** entry whose `StubPath` runs
  `DeGhoster.exe --register-autostart`; Windows executes it once per user at logon, so
  each user gets the entry in their own hive.

The app owns the logic: `--register-autostart` / `--unregister-autostart` write/remove
the current user's HKCU `Run` value (see `Autostart.cpp`).

## Consequences

- **+** Every user gets a correct per-user autostart; nothing lands in HKLM `Run`.
- **+** The registration logic lives in one tested place in the app.
- **−** Active Setup cannot remove *other* users' HKCU entries on a per-machine
  uninstall; a stale entry points at a removed exe and is ignored by Windows at logon
  (documented limitation).

## Alternatives considered

- **HKLM `Run`:** rejected — the explicit non-goal; wrong context, not per-user.
- **Startup-folder shortcut:** rejected — same per-user propagation problem on a
  per-machine install, and messier to keep in sync than a single Run value.
