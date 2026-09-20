# Architecture Decision Records

This folder records the **main design decisions** behind DeGhoster and *why* they
were made. Each record is immutable once accepted; a decision that changes gets a
new record that supersedes the old one (noted in both).

Format: a short [MADR](https://adr.github.io/madr/)-style record with
*Status · Context · Decision · Consequences · Alternatives*.

| ADR | Title | Status |
|---|---|---|
| [0001](0001-native-win32-cpp.md) | Native Win32 C++ host, no runtime dependencies | Accepted |
| [0002](0002-in-process-cloak-via-hook-dll.md) | Neutralize ghosts with `DWMWA_CLOAK` from inside the target via an injected hook DLL | Accepted |
| [0003](0003-event-driven-detection.md) | Event-driven detection with `SetWinEventHook` (no polling) | Accepted |
| [0004](0004-lwa-alpha-ghost-discriminator.md) | Use the `LWA_ALPHA` flag to tell ghosts from real layered windows | Accepted |
| [0005](0005-separate-32bit-helper.md) | Inject 32-bit targets via a separate `Helper32.exe` | Accepted |
| [0006](0006-mui-localization.md) | Localize via standard Windows MUI (`muirct` split) | Accepted |
| [0007](0007-per-user-autostart.md) | Autostart always per-user (HKCU), never machine-wide | Accepted |
| [0008](0008-versioning-and-packaging.md) | GitVersion versioning + dual-scope WiX MSI and portable ZIP | Accepted |
| [0009](0009-automated-release-notes.md) | Automated release notes via Release Drafter | Accepted |
| [0010](0010-integration-tests.md) | Integration tests via a native ghost simulator | Accepted |
