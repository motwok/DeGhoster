# 0013 — Detect RDP sessions by the RDP control's window classes

- **Status:** Accepted
- **Deciders:** Emmo Emminghaus

## Context

The remote cursor overlay ([ADR-0012](0012-anydesk-cursor-overlay.md)) also helps in
Remote Desktop (RDP) sessions. RDP does not shrink the remote cursor: it shows the
remote computer's own pointer at the size the remote is set to. On a high-DPI client
(3840×2160 at 250 %) a remote at 100 % still delivers a 25×38 px arrow next to a
local arrow of 60×93 px. Enlarging the pointer on the remote fixes it, but has to be
set up on every remote computer and session host separately; enlarging it once on the
client covers all of them. A proof of concept (about 20 hours of real sessions)
established:

- The RDP client sets its own, non-system Win32 cursors on the control's input window,
  the same mechanism as AnyDesk; an animated remote cursor arrives as one cursor
  handle per frame (about 20), and the `OBJID_CURSOR` WinEvents followed every frame.
- The same automatic and fixed zoom fit RDP and AnyDesk alike (a Windows arrow is
  25×38 over RDP and 24×38 over AnyDesk).
- The client is not one program: Remote Desktop Connection (`mstsc.exe`), WSLg
  (`msrdc.exe`, which shows graphical Linux apps through RDP RemoteApp) and
  connection managers (Hyper-V VMConnect, Remote Desktop Connection Manager,
  mRemoteNG, …) all embed the same ActiveX control, `mstscax.dll` or its fork `rdclientax.dll`.

## Decision

1. **RDP is detected by the window-class chain of the RDP control**, not by process
   name or loaded module: the window under the cursor has the class `IHWindowClass`
   ("Input Capture Window"), its parent `UIContainerClass` and that window's parent
   `UIMainClass`. Exact and case-sensitive, regardless of the host program.
2. **The listed window is the host's visible top-level window** (`GA_ROOT` of the
   input window): an RDP window is a case like an AnyDesk window, with its own row,
   eye and tray entry, switched per program.
3. **No setting of its own.** RDP shares Auto and the fixed zoom with AnyDesk; it is
   switched on and off per window (per program) with the eye, as every case is. The
   global feature toggle and the separate "Enlarge RDP cursor" switch suggested in the
   proof of concept's specification are not built, because a hidden global switch
   would bypass the per-window model.

## Consequences

- **+** Covers every program that embeds the control, including ones never tested,
  with no list of executables to maintain.
- **+** Precise to the session area: the overlay stays off over the host's own UI
  (connection bar, toolbars, a connection manager's tree view), which never lies in the
  input window.
- **+** Cheap: three `GetClassName` calls per cursor event, no process handle, no
  module enumeration, independent of the host's bitness.
- **−** The class names are an undocumented implementation detail. They are identical
  in `mstscax.dll` and `rdclientax.dll` and have been stable across Windows versions;
  if Microsoft changes them, the overlay simply stays off — no false positives.
- **−** Clients with their own RDP implementation (FreeRDP-based ones, for example)
  are not covered; they would need a rule of their own.
- **−** Listing needs the control found inside a top-level window. mstsc creates it
  while its window is still hidden, so the window's children are searched when a
  top-level window is shown (and at startup, and when the cursor reaches it), not on
  the one-second sweep.

## Alternatives considered

- **Process names** (`mstsc.exe`, `msrdc.exe`): misses every other program that embeds
  the control, and `msrdc.exe` exists in two unrelated products (the Remote Desktop
  app and WSL).
- **Loaded module** (`mstscax.dll` / `rdclientax.dll` in the process): needs process
  access and module enumeration, and is true for the whole process, also where the
  mouse is not over a session (a connection manager's tree view).
- **Cache the class check per window handle**: not needed at three `GetClassName`
  calls per event, and a handle reused by a new window could return a stale answer.
