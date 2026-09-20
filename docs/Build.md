# DeGhoster — Build & Packaging

How to build DeGhoster and produce the ZIP and MSI artifacts. See also
[Architecture.md](Architecture.md), [Specification.md](Specification.md) and the
[ADRs](adr/README.md) (esp. [0008](adr/0008-versioning-and-packaging.md)).

## Prerequisites

| Tool | Needed for | Notes |
|---|---|---|
| **Visual Studio 2026** with the C++ workload (MSVC for **x64 and x86**) | compiling | includes CMake ≥ 3.25 and Ninja |
| **Windows SDK** (`muirct.exe`) | MUI language files | ships with the VS C++ workload |
| **.NET SDK** + **GitVersion** | version stamping | `dotnet tool restore` (pinned in `.config/dotnet-tools.json`) |
| **WiX CLI v4+** + UI/Util extensions | the MSI | see [Packaging](#packaging); not needed for the ZIP |

No .NET runtime is required to *run* DeGhoster — .NET is only a build-time tool for
GitVersion.

One-time WiX setup:

```powershell
dotnet tool install --global wix
wix extension add -g WixToolset.UI.wixext
wix extension add -g WixToolset.Util.wixext
```

## Repository layout (build-relevant)

```
CMakeLists.txt            CMake root
CMakePresets.json         presets/workflows x86 + x64
GitVersion.yml            versioning config
.config/                  dotnet-tools.json (pinned GitVersion)
build.ps1                 full build + packaging → dist\
cmake/Languages.cmake     language list (culture=LANGID=rc) — single source of truth
cmake/Version.cmake       GitVersion → src/Version.h + build/version.json
src/Version.h.in          template for the version resource (generates Version.h)
src/version.rc            shared VERSIONINFO for every binary
src/DeGhoster/            Win32 host (C++)
src/DeGhoster.Hook/       hook DLL (x64 + x86)
src/DeGhoster.Helper32/   32-bit injector
src/DeGhoster.Lang/       per-language resource sources (muirct inputs)
packaging/                WiX MSI source + ZIP/MSI scripts
build/                    all build artifacts (gitignored)
dist/                     finished ZIP/MSI artifacts (gitignored)
```

## Versioning

The version is derived from git via **GitVersion**
([ADR-0008](adr/0008-versioning-and-packaging.md)). `cmake/Version.cmake` runs at
every configure, generates `src/Version.h` from `src/Version.h.in`, and stamps a
`VERSIONINFO` resource (via `src/version.rc`) into **all four** binaries. It also
writes `build/version.json`, the single source of truth for artifact names and the
MSI `ProductVersion`.

- Between tags the version is `<major>.<minor>.<patch>-<commits>`; before any tag it
  starts at `0.0.1-<n>`.
- Cut a release by tagging the commit, e.g. `git tag v1.0.0` → GitVersion reports
  `1.0.0`.
- Without GitVersion the build still succeeds with a `0.0.0` fallback and a warning.

## Building

### One command (recommended)

```powershell
.\build.ps1                 # version-stamp, build x86 + x64, then ZIP + MSI → dist\
.\build.ps1 -NoMsi          # skip the MSI (e.g. WiX not installed)
.\build.ps1 -NoZip          # skip the ZIP
.\build.ps1 -PackageOnly    # re-package from an existing build\ (no recompile)
```

`build.ps1` finds CMake (PATH or a VS install) and restores the GitVersion tool.

### CMake directly

```bat
cmake --workflow --preset x86   REM 32-bit hook + helper  -> build\
cmake --workflow --preset x64   REM host + MUI + 64-bit hook -> build\
```

Both workflows write centrally to `build\`. The self-contained runtime set:

```
build\DeGhoster.exe                  host (x64, language-neutral)
build\DeGhoster.Hook64.dll           64-bit hook
build\DeGhoster.Hook32.dll           32-bit hook
build\DeGhoster.Helper32.exe         32-bit injector
build\LICENSE.txt  build\NOTICE.txt  license / third-party notices
build\<culture>\DeGhoster.exe.mui    one language file per UI language (en-US = fallback)
```

> **Note (MUI split idempotency):** the host exe is linked into the CMake tree and
> `muirct` reads it via `$<TARGET_FILE:DeGhosterApp>` to write the language-neutral
> `build\DeGhoster.exe`. This keeps the en-US split idempotent — `muirct` refuses to
> re-split an already-split MUI file, so the linker output must stay pristine.

## Packaging

Both artifacts are built from one staged payload (`build\stage\`) so they stay in
sync, and are named after the version (e.g. `DeGhoster-1.0.0-win-x64.{zip,msi}`) into
`dist\`.

- **ZIP** — `packaging\build-zip.ps1`: portable; unzip and run `DeGhoster.exe`. No
  install, no Start-menu entry, no autostart.
- **MSI** — `packaging\build-msi.ps1` (needs the WiX CLI + UI/Util extensions):
  - **Language selection** via the `FeaturesDlg` feature tree (en-US always
    installed; the OS then shows the language matching the user's Windows UI
    language).
  - **Per-user / per-machine** via `WixUI_Advanced`'s `InstallScopeDlg` (default:
    per-user, `%LocalAppData%\Apps\DeGhoster`, no admin; "Global" → `Program Files`).
  - **Start-menu shortcut** and a **"Launch DeGhoster"** finish-page checkbox.
  - **Autostart always per-user** ([ADR-0007](adr/0007-per-user-autostart.md)):
    a direct HKCU `Run` value for per-user installs; **Active Setup** (running
    `DeGhoster.exe --register-autostart` per user at logon) for per-machine installs.

The per-language WiX fragment (`build\Languages.generated.wxs`) and the EULA
(`build\stage\License.rtf`) are generated by `packaging\Common.ps1` from the staged
payload; the main authoring lives in `packaging\wix\DeGhoster.wxs`.

## Installing / uninstalling (MSI)

For the full end-user / admin guide (scopes, language features, upgrades) see
[Install.md](Install.md). Quick reference:

```powershell
msiexec /i dist\DeGhoster-<version>-win-x64.msi           # interactive
msiexec /i dist\DeGhoster-<version>-win-x64.msi /qn       # silent, per-user default
msiexec /x dist\DeGhoster-<version>-win-x64.msi /qn       # silent uninstall
```

Select specific languages silently with `ADDLOCAL`, e.g.
`ADDLOCAL=Core,lang_de_DE,lang_fr_FR`. Otherwise remove it via **Settings → Apps**.

## Tests

Integration tests live in `tests/` and prove the actual neutralization end to end
([ADR-0010](adr/0010-integration-tests.md)):

- **`tests/GhostSim`** (C++, built for x64 **and** x86 as `build\GhostSim64.exe` /
  `GhostSim32.exe`) creates a real ghost window matching every `IsBlocker` criterion.
- **`tests/DeGhoster.Tests`** (xUnit, .NET) launches GhostSim + DeGhoster and asserts
  the window becomes `DWMWA_CLOAKED`. The `[Theory]` runs both bitnesses, so it covers
  the x64 (Hook64 direct) and x86 (Helper32 → Hook32) injection paths.

Build first, then run the tests:

```powershell
.\build.ps1 -NoZip -NoMsi     # or the two cmake workflow presets
dotnet test tests\DeGhoster.Tests\DeGhoster.Tests.csproj -c Release
```

They need an **interactive desktop with DWM** (they inject across processes and read
DWM state), so they run reliably locally and in the CI Windows job, but not in a
headless/session-0 context. GhostSim is a test-only binary and is **not** shipped in
the ZIP/MSI. Disable building it with `-DDEGHOSTER_BUILD_TESTS=OFF`.

### Code coverage

Native coverage uses **[OpenCppCoverage](https://github.com/OpenCppCoverage/OpenCppCoverage)**,
which measures the native processes the tests launch (`--cover_children`) using the
binaries' **PDBs**. The build emits PDBs for every configuration (see the `MSVC`
block in the root `CMakeLists.txt`); they are not shipped.

```powershell
choco install opencppcoverage   # one-time
.\tests\coverage.ps1            # builds Debug (x86+x64) + runs tests under coverage
```

The report lands in `coverage\` (Cobertura XML + browsable HTML at
`coverage\html\index.html`). Coverage counts the C++ code in `src\` executed
across DeGhoster, the hooks and Helper32. The automated run reaches ~90 % of
`src\`; what it can't reach robustly is the modal tray menu, the per-window eye
click, DPI-change handling, the 32-bit helper's teardown (an orphaned-process
tooling limit) and defensive API-failure branches.

### Guided (manual) coverage — optional

To cover those remaining lines, `tests\coverage-manual.ps1` runs DeGhoster under
OpenCppCoverage and walks **you** through the manual actions on screen (open the
tray menu, toggle a per-window eye, change display scaling, end DeGhoster via Task
Manager for the helper teardown). It then **merges** its result with the automated
`coverage\auto.cov` into `coverage\merged-html`:

```powershell
.\tests\coverage.ps1          # automated run (writes coverage\auto.cov)
.\tests\coverage-manual.ps1   # then follow the on-screen steps
```

This run needs a real interactive desktop and a human, so it is **not** part of CI.

## Continuous integration

[`.github/workflows/build.yml`](../.github/workflows/build.yml) builds and packages on
`windows-latest`:

- **Pull requests to `master`** and **pushes to `master`** → full x86 + x64 build, the
  ZIP + MSI, **and the integration tests**, with the ZIP/MSI uploaded as workflow
  **artifacts**. Make the *Build & package* job a **required status check** in the
  branch protection of `master` so a PR can't merge without a green build, passing tests
  and working deployments.
- **A published GitHub Release** → the same build, and the ZIP + MSI are attached to
  the release as assets (the release tag drives the GitVersion version).

The workflow lets CMake pick the runner's default Visual Studio generator (so it isn't
pinned to a specific VS version) and installs the WiX CLI + UI/Util extensions for the
MSI. Release notes for that release are drafted separately by
[Release Drafter](../.github/workflows/release-drafter.yml)
([ADR-0009](adr/0009-automated-release-notes.md)).

A separate [Coverage](../.github/workflows/coverage.yml) workflow builds Debug and
publishes an OpenCppCoverage report as an artifact on pushes and PRs (informational,
non-blocking).

## Troubleshooting

- **`muirct.exe not found`** — install the Windows SDK (VS C++ workload).
- **`GitVersion not available` warning / version is `0.0.0`** — run
  `dotnet tool restore`, or install GitVersion globally.
- **MSI build fails on missing extension** — add `WixToolset.UI.wixext` and
  `WixToolset.Util.wixext` (see [Prerequisites](#prerequisites)).
- **"Source file is type MUI and cannot be split"** on an incremental build — this
  is the idempotency case the build already guards against; a clean reconfigure
  (`cmake --workflow --preset x64`) resolves any stale state.
