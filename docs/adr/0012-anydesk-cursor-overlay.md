# 0012 — Enlarge the AnyDesk remote cursor with an overlay, not by injection

- **Status:** Accepted
- **Deciders:** Emmo Emminghaus

> Amended: opt-outs are now per program, not per window (point 7). Every AnyDesk
> window still has its own row, eye and tray entry, but the eye switches the AnyDesk
> program, so all AnyDesk windows switch together; the per-remote opt-out through the
> window title is gone. See [Specification.md](../Specification.md) sections 4 and 5.
>
> Amended: inverting pixels now really invert the screen (point 5). A second,
> colour-keyed window above the overlay, left out of screen captures, shows the
> screen under them XOR their colour; black with a white outline remains only as
> the fallback where Windows cannot leave a window out of captures. Black on a dark
> background was not readable, outline or not.

## Context

On a client with high display scaling (tested: 3840×2160 at 250 %) the cursor of
the remote machine is tiny in an AnyDesk session. A proof of concept established:

- The AnyDesk client is a **32-bit** process; with 3D enabled it renders with
  D3D11/DXGI, which has no hardware-cursor API.
- The remote cursor is an ordinary **Win32 cursor** that AnyDesk creates from the
  remote cursor image and sets on its session window. Cursor handles are valid
  across processes, so it can be read from outside (`GetCursorInfo`, `GetIconInfo`).
- AnyDesk delivers that image **already shrunk** (a Mac arrow: 28×40 canvas, visible
  arrow ≈ 12×17 px), and Windows does not scale cursors to the display DPI. Without
  3D, AnyDesk paints the cursor into the video image instead — larger, still too
  small. No AnyDesk setting and no DPI compatibility override fixes it.

DeGhoster's scope ([Specification.md](../Specification.md) §1.1) admits further
cases that can be detected precisely and mitigated reversibly. This is one.

## Decision

1. **A "ghost cursor" overlay window, owned by the DeGhoster host.** A borderless
   popup with `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
   WS_EX_TOOLWINDOW` shows an enlarged copy of the current AnyDesk cursor via
   `UpdateLayeredWindow`, aligned at the hotspot. It never takes input, focus or a
   hit test. Nothing is injected into AnyDesk; `DeGhoster.Hook*`/`Helper*` are not
   involved.
2. **Detection by window class.** The overlay shows only while the root window under
   the cursor has the class `ad_win` (AnyDesk appends a running `#<n>`, which is
   ignored) and the cursor is not one of the system cursors (`LoadCursor(NULL,
   IDC_*)`). The class instead of the image name `AnyDesk.exe` also covers renamed and
   custom-branded AnyDesk clients; the system-cursor rule keeps the overlay off
   AnyDesk's own title bar, tabs, menus and settings pages.
3. **Event source: out-of-context WinEvents for `OBJID_CURSOR`**, consistent with
   [ADR-0003](0003-event-driven-detection.md): `SHOW`, `HIDE`, `LOCATIONCHANGE`
   (movement) and `NAMECHANGE` (shape). On each event the current position and cursor
   are read, because events are coalesced. The subscription exists only while the
   global switch is on. Animated remote cursors get no special
   handling: every image change AnyDesk makes arrives as a `NAMECHANGE`.
4. **Automatic zoom by on-screen time, per AnyDesk window (default)**, with a
   user-set fixed factor (100 … 600 %, first value = the primary monitor's scaling)
   as the alternative. The remote cursor picture shown the longest is the reference —
   in practice the normal arrow — and the zoom makes it as tall as the local arrow.
   Pictures are told apart by their pixels; nothing is recognized by shape.
5. **Sharp bilinear upscaling**: nearest neighbour by the integer part of the factor,
   then bilinear to the exact size, on premultiplied pixels with clamped edges.
   Inverting pixels really invert: the overlay cannot XOR the screen, so a second,
   colour-keyed window left out of screen captures shows the screen under them
   XOR their colour, refreshed on every cursor event and every 50 ms while such a
   cursor is shown. Where Windows cannot leave a window out of captures, they are
   drawn black with a white outline instead.
6. **The small original is hidden with `MagShowSystemCursor`** while the overlay is
   shown, and restored whenever it is hidden, on quit, on end-session and on session
   lock (`WTSRegisterSessionNotification`).
7. **Every AnyDesk window is a case of its own**, exactly like a ghost window: a row
   with the eye switch in the status list, a tray entry, counted in the title, opted
   out per window with the same `<ExePath>|<WindowTitle>` key. There is no separate
   feature switch; the global power button and the per-window eyes cover it, and
   several AnyDesk windows are independent.
8. **A settings window** is introduced for the zoom slider, instead of more tray
   submenus; it is the place for future settings as well.

## Consequences

- **+** No code in a foreign, signed process: no antivirus or AnyDesk self-protection
  trouble, nothing to break on AnyDesk updates, works for the 32-bit client without a
  32-bit helper.
- **+** Works with AnyDesk's 3D rendering on and off, because in both modes AnyDesk
  sets its own Win32 cursor.
- **+** Reversible and self-healing: switching a window or DeGhoster off removes every effect
  at once, and Windows restores a cursor hidden with `MagShowSystemCursor` by itself
  when the process ends, even after a crash. No UIAccess manifest is needed. The
  Windows Magnifier and the lock screen coexist with it (tested in the POC).
- **+** No idle cost besides the event subscription; rendering happens only when the
  cursor handle, the zoom or the monitor DPI changes, otherwise the window just moves.
- **−** The enlarged image is an upscaled, already-downscaled picture, so it is softer
  than a native cursor. A larger pointer on the remote machine gives a larger, sharper
  source and is the better lever.
- **+** The automatic zoom fits every remote at once: AnyDesk shrinks cursors by a
  session-dependent amount (a Mac remote needs about twice the factor of a Windows
  remote), and each window gets its own factor without any setting.
- **−** The automatic zoom looks at the last 10 s only, so it settles quickly but a
  stretch spent mostly in text makes the I-beam the reference for a while (the
  hysteresis and the 2 s cap per interval dampen that). Some remotes send the I-beam at its base
  size while the arrow comes enlarged, so one factor per window cannot make every
  shape match. This is documented for users.
- **−** Two more system libraries (`Magnification.dll`, `Wtsapi32.dll`) and a
  per-monitor DPI query (`Shcore.dll`).

## Alternatives considered

- **Hook `CreateIconIndirect`/`SetCursor` inside AnyDesk** and enlarge the cursor
  there: the cleanest picture, but code in a foreign signed process (antivirus,
  AnyDesk self-protection, updates). Rejected.
- **`WH_MOUSE_LL` plus a timer** (the first POC version): rejected as unnecessary —
  the WinEvents missed none of 1 896 shape changes in real sessions, checked by an
  independent 250 ms watchdog — and a timer contradicts ADR-0003.
- **Match the image name `AnyDesk.exe`**: misses renamed and branded clients.
- **A fixed DPI rule** (zoom = monitor DPI / 96): fits a Windows remote (≈ 2.5 at
  250 %) but leaves Mac cursors far too small.
- **Automatic calibration from the remote arrow** (arrow detected by geometry, factor =
  local arrow height / remote arrow height): plausible factors (Mac 5.17, Windows
  2.45), but it needed extra rules for the I-beam and for look-alikes and still missed
  Mac I-beam variants. Every new shape needed another heuristic. The chosen automatic
  zoom avoids that by using on-screen time instead of shape.
- **Only a user-set factor**: one value does not fit a Mac and a Windows remote at the
  same time; kept as the alternative to Auto.
- **Recognizing cursor types by shape masks** and drawing the local cursor instead:
  postponed. The masks derive from Apple and Microsoft cursor artwork, and whether
  creating and using them is permitted needs a legal review first.
- **Other upscalers**: bicubic is visibly blurry, nearest neighbour blocky; the
  pixel-art scalers (Scale2x/Scale3x, and by extension xBR/hqx) gave no visible gain,
  because AnyDesk's cursor images are already anti-aliased and those algorithms need
  exactly equal colours.
