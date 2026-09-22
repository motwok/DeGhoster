# DeGhoster — Installation

How to install, update and remove DeGhoster. For day-to-day usage see the
[User Manual](UserManual.md); for building the artifacts yourself see
[Build.md](Build.md).

## Which download?

| Download | Best for | Installs? | Start-menu entry | Autostart at sign-in |
|---|---|---|---|---|
| `DeGhoster-win-x64.msi` | most people | yes (per-user or per-machine) | yes | yes (per-user) |
| `DeGhoster-win-x64.zip` | portable / no install | no — just unzip | no | no |

DeGhoster is 64-bit and needs Windows 10 or 11. There is **no runtime prerequisite**
— nothing else to install first.

## Installing with the MSI (recommended)

Double-click the `.msi` and follow the wizard. You will be asked:

1. **Accept the licence** (GNU AGPL v3).
2. **Who it's for** — the standard install-scope prompt:
   - **Just me** *(default)* — installs to `%LocalAppData%\Apps\DeGhoster`, **no
     admin rights** required.
   - **Everyone on this PC ("Global")** — installs to `Program Files`, requires
     admin rights.
3. **Languages** — tick the UI languages to install. English (en-US) is always
   included as the fallback; Windows then shows the language matching your user
   display language. Leave all ticked or trim to what you need.
4. **Finish** — leave **"Launch DeGhoster"** ticked to start it immediately.

The installer also adds a **DeGhoster** Start-menu shortcut and sets DeGhoster to
**start automatically when you sign in**.

## Autostart: always per-user

The autostart entry is **always** written per user (`HKCU\...\CurrentVersion\Run`),
never to the machine-wide `HKLM` Run key
([ADR-0007](adr/0007-per-user-autostart.md)):

- **Just me** install → your Run entry is created directly (and removed on uninstall).
- **Global** install → each user who signs in gets their own Run entry automatically
  the first time they log on (via Windows *Active Setup*, which runs
  `DeGhoster.exe --register-autostart` once per user).

The Run entry launches DeGhoster with the **`--taskbar`** switch, so at sign-in it
starts **hidden in the system tray** rather than opening the status window. Open it
whenever you like from the tray icon. (Starting `DeGhoster.exe --taskbar` yourself
does the same thing; without the switch the window opens normally.)

You can toggle autostart yourself from a command prompt — the entry it writes
already includes `--taskbar`:

```powershell
"<path>\DeGhoster.exe" --register-autostart      # add this user's autostart
"<path>\DeGhoster.exe" --unregister-autostart    # remove it
```

## Portable ZIP

Prefer not to install? Unpack the ZIP anywhere and run `DeGhoster.exe`. No program
files are installed, there is no Start-menu entry, and it does **not** start
automatically at sign-in — you launch it yourself. It still remembers your
preferences (paused state, per-window opt-outs) under
`HKEY_CURRENT_USER\Software\DeGhoster`; delete the folder (and, if you want a clean
slate, that registry key) to remove it.

Want the portable copy to start at sign-in anyway? Run it once with
`--register-autostart` — this writes the same per-user, tray-hidden Run entry the
installer would (`"<path>\DeGhoster.exe" --taskbar`); remove it with
`--unregister-autostart`. Because the entry stores the exe's full path, re-register
after moving the folder.

## Silent / unattended install (IT admins)

```powershell
# Per-user (default scope), silent:
msiexec /i DeGhoster-win-x64.msi /qn /l*v install.log

# Per-machine ("Global"), silent (run elevated):
msiexec /i DeGhoster-win-x64.msi ALLUSERS=1 /qn /l*v install.log

# Only specific languages (plus the always-present core / en-US):
msiexec /i DeGhoster-win-x64.msi /qn ADDLOCAL=Core,lang_de_DE,lang_fr_FR
```

**Feature names for `ADDLOCAL`:**

- `Core` — the program files + English (en-US); always required.
- `lang_<culture>` — one per extra language, culture code with `-` replaced by `_`,
  e.g. `lang_de_DE`, `lang_fr_FR`, `lang_pt_BR`, `lang_zh_CN`.
- Omitting `ADDLOCAL` installs **all** languages (the default).

The finish-page **"Launch DeGhoster"** option only applies to interactive installs;
under `/qn` nothing is launched.

## Upgrading

Installing a newer version over an older one upgrades in place (same install scope);
your settings are preserved. Downgrades are blocked with a clear message.

## Uninstalling

- **Interactive:** *Settings → Apps* (or *Programs and Features*) → **DeGhoster** →
  Uninstall.
- **Silent:** `msiexec /x DeGhoster-win-x64.msi /qn`

Uninstalling removes the program files, the Start-menu shortcut and your per-user
autostart entry. (On a per-machine uninstall, autostart entries created for *other*
users cannot be removed automatically; such a leftover points at a removed file and
is ignored by Windows at logon.)

## Where things go

| Item | Per-user install | Per-machine install |
|---|---|---|
| Program files | `%LocalAppData%\Apps\DeGhoster` | `%ProgramFiles%\DeGhoster` |
| Start-menu shortcut | your Programs menu | all-users Programs menu |
| Autostart | `HKCU\...\CurrentVersion\Run\DeGhoster` | same (per user, via Active Setup) |
| App preferences | `HKCU\Software\DeGhoster` | `HKCU\Software\DeGhoster` (per user) |

## Troubleshooting

- **"Do you want to allow this app to make changes?" (UAC)** — only the **Global**
  install needs admin rights; choose **Just me** to install without elevation.
- **Menus are in the wrong language** — make sure that language was ticked during
  installation (English is always available as a fallback); DeGhoster follows your
  **Windows display language**. Sign out and back in after changing it.
- **It didn't start after sign-in** — autostart is set up only by the **installer**;
  the portable ZIP never starts on its own.
- **I don't see it** — DeGhoster runs in the system tray (the ghost icon next to the
  clock, possibly under the "^" overflow), not as a normal window.
