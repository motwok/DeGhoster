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

DeGhoster also fixes a second annoyance: in **AnyDesk** remote sessions on a high-resolution
screen, the mouse cursor of the remote computer is often **tiny**. DeGhoster shows it in a
comfortable size instead — see [Enlarging the AnyDesk cursor](#enlarging-the-anydesk-cursor).
Here, too, the fix really belongs elsewhere: AnyDesk's Windows client should scale the remote
cursor to your display itself.

Which vendors' defects DeGhoster works around, and what they would need to fix, is listed in
the README under [Whose bugs are these?](../README.md#whose-bugs-are-these).

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
paused, which windows you switched off and your AnyDesk cursor settings) under your
own Windows account in the
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

The menu also has **Settings…**, which opens the settings window.

## The status window

The status window shows every window DeGhoster currently takes care of, one per line, as
**Window title (program)** — for example `WhatsApp (WhatsApp.Root.exe)` for a ghost window or
`123 456 789 - AnyDesk (AnyDesk.exe)` for an AnyDesk session whose cursor it enlarges. The
window's title bar tells you how many it's handling, e.g. *DeGhoster — 2 Ghosts*.

Each line has a small **eye icon** on the right:

- **Open eye (green)** — DeGhoster is taking care of this window (recommended).
- **Crossed-out eye (grey)** — DeGhoster is ignoring this window and leaves it alone.

Click the eye to switch that window's **program** on or off: if a program has several windows
in the list, they all switch together, and clicking any of their eyes switches them all back.
Updates of the program keep your choice. Ignored windows stay in the list so you can turn
them back on any time. You can do the same from the tray menu, where every detected window
appears as its own entry, in the same order as in the list.

A ghost window whose program hides it — for example when you minimize WhatsApp — stays in
the list, **greyed out**, until the program really closes it. It stays harmless meanwhile.

## Turning it on and off

The **power button** in the toolbar is the master switch:

- **Green** — DeGhoster is active and neutralizing ghosts.
- **Grey** — DeGhoster is paused.

When you pause it, DeGhoster keeps *watching* and keeps the list up to date — it just stops
acting until you switch it back on. The tray menu has the same **Active / Inactive** switch.
Pausing also switches off the enlarged AnyDesk cursor; the zoom setting is greyed out until
you switch DeGhoster back on.

## Enlarging the AnyDesk cursor

On a high-resolution screen with a large display scaling (for example 4K at 250 %), AnyDesk
shows the mouse cursor of the remote computer very small — sometimes barely bigger than a
mosquito. No AnyDesk setting changes that.

DeGhoster puts an enlarged copy of the remote cursor on top of it while your mouse is over
the AnyDesk session, and hides the small original. It follows the mouse and changes shape
with it (arrow, text cursor, hand …). Clicks, typing and focus work exactly as before: the
enlarged cursor is only a picture, every click goes straight through to AnyDesk. Over
AnyDesk's own title bar, tabs and menus, and everywhere outside AnyDesk, you see your normal
cursor.

Every AnyDesk window appears in the status window's list with its own **eye**, just like a
ghost window, and it is **on** by default. Click the eye (or the window's entry in the tray
menu) to switch the enlarged cursor off for AnyDesk; with several AnyDesk windows open, each
has its own entry, and they all switch together.

**The size is automatic.** DeGhoster watches which remote cursor is on screen the longest —
normally the arrow — and makes it exactly as tall as your own mouse pointer. It does that
for each AnyDesk window separately, so a Mac and a Windows computer both look right at the
same time, without any setting. It looks at the last 10 seconds, so it settles within a
few seconds of normal use and follows along if that changes (a long stretch of typing
makes the text cursor the reference for a while).

To use a fixed size instead, open **Settings** — the **gear** button in the status window's
toolbar, or **Settings…** in the tray menu:

- **Automatic size (as large as the local pointer)** — on by default. Switch it off to use
  the fixed zoom below.
- **Zoom** sets the fixed size, from 100 % to 600 %. It starts at your display scaling
  (250 % at 250 %) and is greyed out while the automatic size is on. Changes apply at once:
  rest the mouse over an AnyDesk session, then adjust the slider with the keyboard (arrow
  keys ±10 %, Page Up/Page Down ±50 %, Home/End for the limits) and watch the cursor grow or
  shrink.

Everything is saved immediately; **Close** or **Esc** closes the window.

**Tips for the right size**

- With a fixed size, the right zoom depends on the remote computer, because AnyDesk shrinks
  cursors by a different amount per session. A **Mac** remote typically needs about **twice**
  the zoom of a **Windows** remote (roughly 500 % vs. 250 % at a 250 % display scaling). The
  automatic size takes care of that by itself.
- For the **sharpest** result, make the mouse pointer larger **on the remote computer**
  (Windows: *Settings → Accessibility → Mouse pointer*; macOS: *Accessibility → Display →
  Pointer size*) and use a smaller zoom here. The enlarged cursor is a magnified picture, so a
  bigger original looks crisper.
- Some remote computers send the text cursor (I-beam) at its normal size even when the arrow
  is enlarged, so the text cursor can look a little small next to the arrow.

## The Settings button

The **gear** button (left of the **i**) opens the settings window — see
[Enlarging the AnyDesk cursor](#enlarging-the-anydesk-cursor).

## The Info button

The **i** button opens a small "About" box with the version info, license, and a link to
support the project. Nothing changes on your system when you open it.

## Quitting

DeGhoster is meant to stay running in the background, so **closing the status window only hides
it back to the tray**. To actually quit, use the **Exit** button (the door icon) in the
toolbar or **Exit** in the tray menu. When it quits, every window it had neutralized is
restored to normal automatically.

## It remembers your choices

Any program you switch off, whether DeGhoster is paused, and your AnyDesk cursor size settings
are remembered between restarts. When you start DeGhoster again, it picks up right where you left
off.

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
- **The AnyDesk cursor is still tiny.** Make sure DeGhoster is active (green power button) and
  the AnyDesk window's eye in the status window is open (green). The enlarged cursor only
  appears over the remote screen, not over AnyDesk's own tabs and menus.
- **The AnyDesk cursor is too big or too small.** With the automatic size, move the mouse
  around normally for a few seconds; it settles on the shape you see most. If you prefer a
  fixed size, switch **Automatic size** off in the settings window and adjust **Zoom**.
- **The AnyDesk cursor looks blurry.** Enlarge the mouse pointer on the remote computer and
  lower the zoom here.
- **The menus are in the wrong language.** DeGhoster follows your **Windows display language**.
  Make sure that language was ticked during installation (English is always available as a
  fallback); then sign out and back in.

## Support Me

If DeGhoster saves you some frustration, you can support it here:
**[Buy Me a Coffee](https://ko-fi.com/motwok)** ☕
