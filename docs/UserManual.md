# DeGhoster — User Manual

## What DeGhoster does

Now and then a program leaves behind an **invisible window** on your screen. You can't see it,
but it sits there and quietly eats your mouse clicks — you click your desktop or another
window, and nothing happens in that spot. Often the **mouse cursor also disappears** while
it's over that area. It's like a ghost blocking the way.

DeGhoster finds those invisible "ghost" windows and gently makes them harmless again, so your
clicks land where you expect and the cursor comes back. It runs quietly in the background and
only steps in when it spots one.

To be fair, this is really the other program's housekeeping to do — an app should clean up its
own leftover windows (the WhatsApp desktop app is a common culprit). Until the app makers get
around to it, DeGhoster quietly does the tidying for you.

## Downloading and installing

There are two ways to get DeGhoster. Both give you the same program. For the full
guide (silent install, upgrades, uninstall) see [Install.md](Install.md).

### Option A — the installer (recommended)

1. Download **`DeGhoster-win-x64.msi`** and double-click it.
2. Accept the licence and follow the wizard. Along the way you can choose:
   - **Who it's for** — *just me* (the default, no admin rights needed) or
     *everyone on this PC* ("Global", needs admin rights).
   - **Languages** — pick which display languages to install. English is always
     included as a fallback; DeGhoster then automatically shows the language that
     matches your Windows user language. You can leave every language ticked or
     trim the list to just the ones you need.
3. On the last page you can leave **"Launch DeGhoster"** ticked to start it right
   away.

The installer also:

- adds a **DeGhoster** entry to your **Start menu**, and
- sets DeGhoster to **start automatically when you sign in** — always just for
  *your* Windows account, never system-wide. (On a "Global" install, each person
  who signs in gets their own autostart entry the first time they log on.)

To remove it later, use **Settings → Apps** (or *Programs and Features*) like any
other program.

### Option B — the portable ZIP

Prefer not to install anything? Download **`DeGhoster-win-x64.zip`**,
unpack it anywhere, and run **`DeGhoster.exe`**. No program files are installed,
there's no Start-menu entry, and it does **not** start automatically at sign-in —
you launch it yourself whenever you want it.

Like the installed version, it does remember your **preferences** (whether it's
paused and which windows you switched off) under your own Windows account in the
registry (`HKEY_CURRENT_USER\Software\DeGhoster`). That's the only thing it writes,
it only affects your account, and it's left behind when you simply delete the
folder — you can remove it by hand if you want a completely clean slate.

## Getting started

Once DeGhoster is running it simply begins watching and places a small **ghost icon
in your system tray** (the area next to the clock).

That's it. From now on DeGhoster catches ghost windows on its own.

## The tray icon

The ghost icon in the tray is your control center:

- **Click it** (left or right) to open the menu.
- **Double-click it** to open the status window (the bold *Status Window* entry is the same
  thing).

## The status window

The status window shows every ghost window DeGhoster currently knows about, one per line, as
**Window title (program)** — for example `WhatsApp (WhatsApp.Root.exe)`. The window's title
bar tells you how many it's handling, e.g. *DeGhoster — 2 Ghosts*.

Each line has a small **eye icon** on the right:

- **Open eye (green)** — DeGhoster is watching and neutralizing this window (recommended).
- **Crossed-out eye (grey)** — DeGhoster is ignoring this window and leaves it alone.

Click the eye to switch that single window. Ignored windows stay in the list so you can turn
them back on any time. You can do the same from the tray menu, where every detected window
appears as its own entry.

## Turning it on and off

The **power button** in the toolbar is the master switch:

- **Green** — DeGhoster is active and neutralizing ghosts.
- **Grey** — DeGhoster is paused.

When you pause it, DeGhoster keeps *watching* and keeps the list up to date — it just stops
acting until you switch it back on. The tray menu has the same **Active / Inactive** switch.

## The Info button

The **i** button opens a small "About" box with the version info, license, and a link to
support the project. Nothing changes on your system when you open it.

## Quitting

DeGhoster is meant to stay running in the background, so **closing the status window only hides
it back to the tray**. To actually quit, use the **Exit** button (the door icon) in the
toolbar or **Exit** in the tray menu. When it quits, every window it had neutralized is
restored to normal automatically.

## It remembers your choices

Any window you switch off, and whether DeGhoster is paused, are remembered between restarts.
When you start DeGhoster again, it picks up right where you left off.

## Troubleshooting

- **Clicks are still blocked somewhere.** Open the status window and make sure the power button
  is **green**. If the offending window is listed but its eye is crossed-out (grey), click it
  to switch it back on.
- **DeGhoster touched a window I want left alone.** Click that window's eye so it's crossed-out
  (grey); it stays off (and is remembered).
- **I don't see the tray icon.** It may be hidden in the tray overflow ("^") — drag it out to
  keep it visible. Make sure DeGhoster is actually running.
- **DeGhoster didn't start when I signed in.** Autostart is set up only by the **installer**.
  The portable ZIP never starts on its own — launch `DeGhoster.exe` yourself, or install the
  MSI if you want it to run automatically.
- **The menus are in the wrong language.** DeGhoster follows your **Windows display language**.
  Make sure that language was ticked during installation (English is always available as a
  fallback); then sign out and back in.

## Support Me

If DeGhoster saves you some frustration, you can support it here:
**[Buy Me a Coffee](https://ko-fi.com/motwok)** ☕
