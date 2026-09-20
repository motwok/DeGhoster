# DeGhoster — Specification

*What* DeGhoster must do — requirements and externally observable behavior. For the
internal design see [Architecture.md](Architecture.md); for the rationale behind the
main choices see the [ADRs](adr/README.md); for building see [Build.md](Build.md).

## 1. Purpose

Some Chromium-based desktop apps (WebView2, Electron) leave an **invisible window**
on screen. When the owning app moves to another virtual desktop, that window stays
behind and, within its rectangle, produces two symptoms:

- **Click-eating** — it intercepts desktop clicks, so clicks in that area do nothing.
- **Cursor disappearance** — it ignores the cursor-setting message (`WM_SETCURSOR`),
  so the mouse pointer vanishes while it is over the window's area.

DeGhoster detects such "ghost windows" and neutralizes them so clicks reach the
desktop again and the cursor is restored.

DeGhoster exists because this is ultimately a defect in the offending applications —
they should destroy or cloak these leftover windows themselves (WhatsApp's desktop
app is a recurring example); until they do, DeGhoster is a pragmatic workaround.

### 1.1 Scope and outlook

Today DeGhoster targets this one, precisely-defined ghost-window defect. But the
invisible and leftover windows that Chromium/WebView2 apps produce can misbehave in
more than one way — swallowing clicks and hiding the cursor are simply the symptoms
seen most often — and the approach behind DeGhoster generalizes well beyond them:
recognize a *well-defined* bad-window pattern and neutralize it **reversibly**,
in-process, without ever touching a legitimate window.

The intention is therefore for DeGhoster to grow over time. As further such issues are
identified and shown to be safely mitigable with the same event-driven, reversible
technique, they are meant to be added here as additional cases. Each addition is
deliberate and held to the same bar: the offending window must be detectable
**precisely** (no false positives against real windows) and neutralizable
**reversibly** (a wrong guess must always be undoable). Anything that cannot meet both
conditions stays out of scope.

## 2. Ghost-window definition

A top-level window is treated as a ghost when **all** of the following hold:

| Property | Required value |
|---|---|
| Class name | `Chrome_WidgetWin_1` |
| Visible | `IsWindowVisible == true` |
| Ex-style `WS_EX_LAYERED` | set |
| Ex-style `WS_EX_TRANSPARENT` | not set (it would already be click-through) |
| `GetLayeredWindowAttributes` | returns true, flag `LWA_ALPHA`, `alpha == 0` |
| On screen | rectangle intersects the virtual screen, width/height > 0 |
| `DWMWA_CLOAKED` | `0` (not already cloaked) |

The `LWA_ALPHA` check is essential: real, visible windows using per-pixel alpha
(`UpdateLayeredWindow`, e.g. Discord) report `alpha 0` but **without** `LWA_ALPHA` and
must therefore be ignored ([ADR-0004](adr/0004-lwa-alpha-ghost-discriminator.md)).

**Why an invisible (alpha 0) window still eats clicks:** the real ghost is a
DirectComposition window (`WS_EX_NOREDIRECTIONBITMAP`). The usual rule *"a fully
transparent (alpha 0) layered window lets clicks pass through"* is based on the
window's redirection bitmap; a window with no redirection bitmap does not get that
behavior, so it stays invisible **and** hit-tests its whole rectangle — i.e. it eats
clicks. Neutralizing it (cloaking) removes it from hit-testing, so clicks fall through
again. This was confirmed against a real WhatsApp ghost, and a plain
`WS_EX_NOREDIRECTIONBITMAP` window reproduces it (see
[Architecture.md](Architecture.md#why-a-ghost-eats-clicks) and the tests).

## 3. Required behavior

- **Neutralize** each ghost so it no longer intercepts input — clicks reach the
  desktop again **and** the cursor is set normally over that area — **without
  destroying it** and **without disturbing the owning app's real windows**.
  Neutralization must be fully **reversible**.
- **Detect promptly and idle cheaply.** Ghosts must be picked up as they appear, move
  or close; already-open ghosts must be caught at startup. Idle cost must be
  negligible (the app runs from logon onward) — see
  [ADR-0003](adr/0003-event-driven-detection.md).
- **Both bitnesses.** Ghost windows owned by 32-bit *and* 64-bit processes must be
  handled ([ADR-0005](adr/0005-separate-32bit-helper.md)).
- **Identify the real app.** The UI must name the owning **host program**, not the
  generic `msedgewebview2` process, shown as `Window title (executable.exe)`.

## 4. State model

| Set | Meaning |
|---|---|
| tracked | every live ghost window currently listed |
| cloaked | tracked windows currently neutralized by us |
| disabled | per-window opt-out, key `<ExePath>\|<WindowTitle>` |
| global enabled | master switch |

**Reconcile rule** per window: neutralize ⇔ `globalEnabled && managed &&
windowAlive`, where `managed = key ∉ disabled`.

- **Global off** disables only the *action*. Detection keeps running and the list
  stays; entries vanish only when the window closes.
- **Per-window off** releases that window but keeps it listed.

## 5. Persistence

Choices survive restarts, per user (`HKCU`): the master switch and the set of
per-window opt-outs (keyed by full executable path + window title). See
[Architecture.md](Architecture.md#persistence-registry) for the exact keys.

## 6. Autostart

DeGhoster starts at sign-in, registered **always per-user, never machine-wide**
([ADR-0007](adr/0007-per-user-autostart.md)). The portable ZIP does **not** register
autostart; only the installer does. The app exposes `--register-autostart` /
`--unregister-autostart` for the installer.

## 7. User interface

- **Tray app.** Left/right click opens the context menu; double-click opens the
  status window (bold "Statusfenster" = default action).
- **Menu:** status window · Active/Inactive · one entry per detected window (toggles
  it) · Info · Quit.
- **Status window:** title `DeGhoster — N Geister`; a list of detected windows
  (`Title (exe)`), each with a per-window **eye switch** shown the same way as the
  global power switch — a green circle with an open eye when managed, a grey circle
  with a crossed-out eye when ignored; toolbar with power (green/grey), info, quit.
- **Closing the window hides to tray;** the app quits only via Quit/Exit. On quit,
  every neutralized window is restored automatically.
- DPI-aware (per-monitor v2), follows the Windows light/dark theme, and renders
  per-OS (Win 10 look on Win 10, Win 11 look on Win 11).

## 8. Localization

The UI follows the user's Windows display language automatically, with **en-US** as
the guaranteed fallback, via standard Windows MUI
([ADR-0006](adr/0006-mui-localization.md)). Shipped: en-US + 26 languages (de, fr,
es, it, nl, pt-BR, pt-PT, ru, pl, cs, sk, hu, ro, el, da, fi, sv, nb, tr, uk, ja, ko,
zh-CN, zh-TW, ar, he — including RTL). Adding a language is one entry in
`cmake/Languages.cmake` plus one `strings_<lang>.rc`.

## 9. Non-functional requirements

- **No runtime dependencies** ([ADR-0001](adr/0001-native-win32-cpp.md)); links only
  against Windows system libraries, no bundled assets.
- **Small and fast**: tiny footprint, quick start, negligible idle usage.
- **Delivery**: a portable ZIP and a standard MSI with language selection and a
  per-user / per-machine choice ([ADR-0008](adr/0008-versioning-and-packaging.md),
  [Build.md](Build.md)).
- **License**: GNU AGPL v3 (see `LICENSE`, `NOTICE`).
