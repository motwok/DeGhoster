# 0008 — GitVersion versioning + dual-scope WiX MSI and portable ZIP

- **Status:** Accepted
- **Deciders:** Emmo Emminghaus

## Context

DeGhoster needs reproducible version stamping and two delivery formats: a portable
ZIP and a standard Windows installer. The installer must let users pick which UI
languages to install, offer the standard "just me / everyone" choice, and set up the
per-user autostart (ADR [0007](0007-per-user-autostart.md)).

## Decision

**Versioning:** derive the version from git via **GitVersion** (`GitVersion.yml`,
pinned in `.config/dotnet-tools.json`). `cmake/Version.cmake` runs at configure time,
generates `src/Version.h` for a shared `VERSIONINFO` resource stamped into **all**
binaries, and writes `build/version.json` as the single source of truth for artifact
names and the MSI `ProductVersion`.

**Packaging:** two artifacts, both built from one staged payload so they stay in sync:

- **Portable ZIP** — all binaries + every `.mui`; unzip and run, no install, no
  autostart.
- **MSI (WiX v4+)** — `Scope="perUserOrMachine"` with `WixUI_Advanced`, whose
  `InstallScopeDlg` gives the "just me / everyone" prompt and whose `FeaturesDlg`
  gives per-language selection (en-US is always installed). Adds a Start-menu
  shortcut and a "Launch DeGhoster" finish-page checkbox, and wires the per-user
  autostart from ADR [0007](0007-per-user-autostart.md). The per-language WiX
  fragment is generated from the staged cultures.

`build.ps1` orchestrates: version-stamp → build x86 + x64 → ZIP + MSI into `dist/`.

## Consequences

- **+** One command produces version-named ZIP + MSI; versions are consistent
  everywhere (binaries, filenames, MSI).
- **+** Language selection and install scope use standard WiX UI, no custom dialogs.
- **−** MSI build needs the WiX CLI plus the UI and Util extensions.
- **−** GitVersion (a .NET tool) is required for exact versions; the build falls back
  to `0.0.0` with a warning if it is absent.

## Alternatives considered

- **Hand-maintained version numbers:** rejected — drift and manual effort.
- **CPack / a single format:** rejected — CPack's WiX path does not cleanly give
  dual-scope + per-language features + the per-user Active Setup autostart.
