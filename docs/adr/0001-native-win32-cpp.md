# 0001 — Native Win32 C++ host, no runtime dependencies

- **Status:** Accepted
- **Deciders:** Emmo Emminghaus
- **Supersedes:** the original managed (.NET) prototype

## Context

DeGhoster is a small background utility that must:

- run on any Windows 10/11 machine without the user first installing a runtime,
- inject code into arbitrary foreign processes (both x64 and x86), and
- start fast and idle with a tiny footprint (it runs from logon onward).

A managed (.NET/WinForms/WPF) host would drag in a runtime dependency, complicate
native DLL injection across bitness boundaries, and inflate the install.

## Decision

Implement the host as a **native Win32 application in C++ (Windows SDK)** that links
only against Windows system libraries (`comctl32`, `dwmapi`, `uxtheme`, `gdiplus`,
`shell32`, `user32`, `gdi32`). No third-party libraries, no bundled assets — UI
icons are glyphs from the system "Segoe MDL2 Assets" font.

## Consequences

- **+** Zero runtime prerequisites; the ZIP is a few hundred KB and just runs.
- **+** Injection, DWM, MUI and theming are all first-class, no interop layer.
- **+** Small, fast, low idle cost.
- **−** More manual UI code (owner-draw, DPI, dark mode) than a managed toolkit.
- **−** C++ memory/handle discipline is on us (RAII helpers, `GWLP_USERDATA`).

## Alternatives considered

- **.NET (WinForms/WPF):** rejected — runtime dependency and awkward cross-bitness
  native injection.
- **Electron/web stack:** rejected — huge footprint for a tray utility, and ghost
  windows are themselves a Chromium artifact.
