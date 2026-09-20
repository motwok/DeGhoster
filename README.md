<div align="center">
  <img src="assets/logo.svg" width="128" alt="DeGhoster Logo">

  # DeGhoster

  *Who you gonna call? Bustin' invisible, click-eating window ghosts.*

  **Neutralizes invisible WebView2/Chromium "ghost windows" that intercept your desktop clicks.**

  [![Build](https://img.shields.io/github/actions/workflow/status/motwok/DeGhoster/build.yml?branch=master&style=flat-square)](https://github.com/motwok/DeGhoster/actions/workflows/build.yml)
  [![Latest release](https://img.shields.io/github/v/release/motwok/DeGhoster?sort=semver&style=flat-square)](https://github.com/motwok/DeGhoster/releases/latest)
  [![Downloads](https://img.shields.io/github/downloads/motwok/DeGhoster/total?style=flat-square)](https://github.com/motwok/DeGhoster/releases)
  [![Stars](https://img.shields.io/github/stars/motwok/DeGhoster?style=flat-square)](https://github.com/motwok/DeGhoster/stargazers)
  [![Forks](https://img.shields.io/github/forks/motwok/DeGhoster?style=flat-square)](https://github.com/motwok/DeGhoster/network/members)
  [![Issues](https://img.shields.io/github/issues/motwok/DeGhoster?style=flat-square)](https://github.com/motwok/DeGhoster/issues)

  ![Audience](https://img.shields.io/badge/audience-Windows%20desktop%20users-informational?style=flat-square)
  ![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011%20(x64)-0078D6?style=flat-square&logo=windows&logoColor=white)
  ![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white)
  ![Runtime deps](https://img.shields.io/badge/runtime%20deps-none-brightgreen?style=flat-square)
  ![UI languages](https://img.shields.io/badge/UI%20languages-27-blueviolet?style=flat-square)
  [![License: AGPL v3](https://img.shields.io/badge/license-AGPL--3.0-blue?style=flat-square)](LICENSE)
  [![PRs: not accepted](https://img.shields.io/badge/PRs-not%20accepted-red?style=flat-square)](CONTRIBUTING.md)
  [![Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20Me-FF5E5B?style=flat-square&logo=ko-fi&logoColor=white)](https://ko-fi.com/motwok)
</div>

---

## What is it?

Chromium-based desktop apps (WebView2, Electron — e.g. WhatsApp, Teams) sometimes
leave an **invisible, click-eating window** on your screen. When the app moves to
another virtual desktop, that ghost window stays behind and, within its rectangle:

- **swallows your desktop clicks** — you click the desktop or another window and
  nothing happens there; and
- **makes the mouse cursor disappear** — the ghost ignores the cursor-setting message
  (`WM_SETCURSOR`), so the pointer vanishes while it's over that area.

DeGhoster detects these windows automatically and **neutralizes** them — reversibly,
without disturbing the app that caused them — so your clicks land where you expect and
the cursor is back. It runs quietly in the background as a **native Win32 app with no
runtime dependencies**, follows the Windows light/dark theme, and is localized into
27 languages.

DeGhoster currently targets this one ghost-window defect; other window issues from
these apps that can be detected precisely and mitigated reversibly may be added over
time.

> This is ultimately a bug in the offending apps — **WhatsApp's desktop app is a
> recurring example** — they should destroy or cloak their own leftover windows.
> Until they do, DeGhoster does the tidying for you.

## Install

- **MSI setup** (recommended): language selection, per-user *or* per-machine install,
  Start-menu entry and autostart.
- **Portable ZIP**: unzip and run `DeGhoster.exe`, no installation.

Full installation guide (incl. silent install): [Install.md](docs/Install.md).
Both artifacts are produced by [`build.ps1`](build.ps1) into `dist\` — see
[Build.md](docs/Build.md).

## Usage (quick overview)

- **Tray app:** icon in the notification area. Left/right click opens the menu,
  double-click opens the status window.
- **Global on/off** (power button): disables only the *action* — detection keeps
  running.
- **Per-window on/off:** the eye on each row (green = managed, grey = ignored).

The full guide is in the [User Manual](docs/UserManual.md).

## Documentation

| Document | Contents |
|---|---|
| [User Manual](docs/UserManual.md) | Usage and troubleshooting (for end users) |
| [Install](docs/Install.md) | Installation, language selection, silent install, uninstall |
| [Specification](docs/Specification.md) | Requirements and observable behavior |
| [Architecture](docs/Architecture.md) | Internal design, components, protocols |
| [ADRs](docs/adr/README.md) | The main design decisions and their rationale |
| [Build](docs/Build.md) | Prerequisites, building, versioning, packaging |

## Changelog

Release notes are compiled automatically from merged pull requests and published on
the [Releases](https://github.com/motwok/DeGhoster/releases) page. See
[CHANGELOG.md](CHANGELOG.md) for how it works.

## Contributing

Please use **GitHub Issues** for bug reports and feature requests — **pull requests
are not accepted**. See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

**GNU AGPL v3** — see [LICENSE](LICENSE).

## Third-party

None. DeGhoster links only against Windows system libraries. See [NOTICE](NOTICE).

## Support Me

If you find this project useful, please consider supporting it
by buying me a coffee: [Buy Me a Coffee](https://ko-fi.com/motwok)
