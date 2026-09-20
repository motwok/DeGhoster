# 0006 — Localize via standard Windows MUI (`muirct` split)

- **Status:** Accepted
- **Deciders:** Emmo Emminghaus

## Context

DeGhoster ships in many languages (en-US + 26). We want the UI to follow the user's
Windows display language automatically, with a guaranteed fallback, and without a
bespoke string-loading framework or third-party i18n library (see ADR
[0001](0001-native-win32-cpp.md)).

## Decision

Use the **standard Windows MUI** mechanism. All UI strings live in a `STRINGTABLE`
(IDs in `resource.h`); code only calls `LoadStringW`. The exe is compiled
single-language (en-US); the Windows SDK tool `muirct` splits it into a
language-neutral module plus `en-US\DeGhoster.exe.mui`. Each additional language is
built from a small resource source into `<culture>\DeGhoster.exe.mui`. The OS
resource loader then picks the right `.mui` by UI language, with **en-US** as the
ultimate fallback. `cmake/Languages.cmake` (`<culture>=<LANGID>=<rc>`) is the single
source of truth; adding a language is one list entry plus one `strings_<lang>.rc`.

## Consequences

- **+** Automatic OS-driven language selection with a robust fallback; no runtime i18n
  code.
- **+** RTL handled via `LOCALE_IREADINGLAYOUT` + `WS_EX_LAYOUTRTL`.
- **−** Build depends on `muirct` from the Windows SDK.
- **−** The en-US split must be idempotent (`muirct` refuses to re-split a MUI file),
  which constrains how the build writes the exe (see [Build.md](../Build.md)).

## Alternatives considered

- **Custom string tables / external `.json`/`.po` + loader:** rejected — reinvents
  MUI and adds runtime code and dependencies.
- **Separate per-language executables:** rejected — ship size and maintenance.
