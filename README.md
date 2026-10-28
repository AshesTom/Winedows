<h1 align="center">Winedows</h1>
<p align="center">
  <img src="docs/images/screenshot-en.png" alt="Winedows: the English Windows XP shell with Linux and Windows applications on Debian 13" width="800" />
</p>
<p align="center">
  <strong>Your Linux desktop, with the real Windows XP shell. About half a gigabyte of RAM.</strong>
</p>
<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-LGPL--2.1--or--later-blue?style=flat" alt="License: LGPL-2.1-or-later"></a>
  <img src="https://img.shields.io/badge/Debian-13-A81D33?style=flat&logo=debian&logoColor=white" alt="Debian 13">
  <img src="https://img.shields.io/badge/Wine-11.0-8B0000?style=flat" alt="Wine 11.0">
  <img src="https://img.shields.io/badge/RAM-~0.5%20GB-2E7D32?style=flat" alt="RAM: about 0.5 GB">
  <img src="https://img.shields.io/badge/status-experimental-orange?style=flat" alt="Status: experimental">
</p>
<p align="center">
  <strong>English</strong> ·
  <a href=".github/readme/README.ja.md">日本語</a>
</p>

> [!IMPORTANT]
> **This is a personal, experimental project**, not a finished product for daily use.
> It has only been tested on a **freshly installed Debian 13** set up with the installation script.
> Installing it on a system already in use (with another desktop environment or display manager, for example) has not been tried.
> A virtual machine or a spare machine is recommended.

## What it is

Winedows runs Windows XP's own `explorer.exe` on a patched Wine as the **desktop shell of Linux**.
The taskbar, Start menu, desktop and folder windows are XP's real ones, and Linux applications get XP's frames and
taskbar buttons. No Microsoft files are included: the few that are needed are extracted locally from your own ISO.

Winedows started as a fork of [mikuta0407/win4wine](https://github.com/mikuta0407/win4wine) and keeps its
internal names (`win4wine-*` packages, `~/.config/win4wine`, …), so existing installs carry on unchanged.

### One branch per Windows

| Branch | Shell |
|---|---|
| **`WinXP`** (this one) | Windows XP (Luna or Classic), the [ReactOS](https://reactos.org/) shell when you have no XP ISO, and XP Explorer |
| [`Win7`](../../tree/Win7) | Windows 7 |

`master` only points to these two.

## Lightweight

XP recommended 128 MB of RAM, and its shell stays small on Wine. Measured on a full XP session right after logon
(X11, 1280×800, Luna, the notification area with network, volume, Security Center and Messenger running), summing the
proportional set size (PSS, shared pages split between the processes that map them) of every process of the session:

| Part | Processes | RAM (PSS) |
|---|---|---:|
| XP's shell on Wine | `explorer.exe`, XP's tray and Start menu helpers, XP's services, `wineserver` | ≈ 300 MB |
| Session | the X11 window manager, the tray bridge, the supervisor, the Linux application menu, the GTK theme watcher | ≈ 130 MB |
| Linux desktop services | D-Bus, XDG portals, gvfs | ≈ 65 MB |
| X server | | ≈ 30 MB |
| **Total** | 45 processes | **≈ 520 MB** |

For comparison, GNOME and KDE Plasma usually need around 1 GB or more right after logon.

On disk: the Wine build is about 1.5 GB (with debug symbols), the files taken from the XP ISO about 160 MB,
and the Wine prefix grows with the Windows programs you install. Building Wine needs about 40 GB once.

## Install

1. Get a Windows XP Professional SP3 (x86) ISO (English or Japanese) **yourself**.
2. On a freshly installed Debian 13, run as the user who will log in:

```sh
git clone -b WinXP https://github.com/AshesTom/Winedows.git ~/winedows
cd ~/winedows
scripts/setup.sh --media /path/to/WindowsXPSP3.iso --greeter
```

3. Reboot. The XP Welcome screen logs you straight into the XP shell.

Without an XP ISO, try the ReactOS shell instead (no ISO needed):

```sh
scripts/setup.sh --shell reactos --lang en   # or --lang ja
```

and choose the session "ReactOS Shell (Wine, X11)" at the login screen.

Details: 🔗 [installation guide](.github/readme/INSTALL.en.md). Building Wine takes about 90 minutes on 4 cores.
The theme (XP's classic one, for example) and the text size can be changed later
([Changing the look and the text size](.github/readme/INSTALL.en.md#changing-the-look-and-the-text-size)).

## What it does

- **XP's shell**: Luna (or Classic) taskbar, Start menu, desktop, My Computer and folder windows.
- **Linux applications**: Linux windows get XP's frames; the taskbar, Alt+Tab, minimize and maximize work as on XP.
  Linux applications are listed under All Programs, in a Linux Applications folder.
- **Notification area**: Linux tray icons (StatusNotifierItem and XEmbed) show up in XP's notification area, beside
  XP's own network, battery and volume icons with their flyouts and XP's Volume Control, fed by NetworkManager and
  PipeWire or PulseAudio. The wireless network list joins new networks through XP's network key dialog.
- **XP's own programs and control panels**, from your ISO, in place of Wine's: Calculator, Notepad, Paint, WordPad,
  Command Prompt, Task Manager, Registry Editor, the games, Windows Messenger, Internet Explorer 6, Windows Media
  Player 9, the Search Companion with Rover, Display Properties (themes, styles, color schemes), User Accounts
  (the picture also becomes the Linux user's), Network Connections and "Choose a wireless network", Add or Remove
  Programs (Linux packages included), the Security Center and the Help and Support Center. XP's sounds play at
  startup, logon and logoff.
- **Keyboard layout**: the Linux system's at first; add input languages in Regional and Language Options and the
  session follows at once, with Left Alt+Shift to switch as on XP.
- **Japanese input**: fcitx5 + Mozc (Ctrl+Space or Zenkaku/Hankaku) in GTK, Qt and Wine applications.
- **English or Japanese XP**: the language is detected from the ISO, and the fonts, locale and Welcome screen follow it.
- **Windows applications**: a WoW64 Wine 11.0 runs 32-bit and 64-bit programs; `wine setup.exe` puts the installer's
  shortcuts in XP's Start menu.
- **XP Welcome screen**: an optional LightDM greeter with XP's Welcome and log-off screens.
- **XP's keys and habits**: the Windows key, Ctrl+Esc, Win+R, Win+E, Quick Launch, Show Desktop, XP's DPI setting.
- **Recovery**: if the XP shell keeps crashing, the session falls back to Wine's own explorer and an xterm.
- **Wayland session (optional, more experimental)**: a patched labwc instead of the X11 window manager, with a scale
  per monitor and fractional scales ([docs/wayland.md](docs/wayland.md), Japanese). Try the X11 session first.
- **ReactOS shell (optional)**: ReactOS 0.4.16's explorer instead of XP's, in English or Japanese, beside the XP shell.
- **XP Explorer (optional)**: XP's shell in a window of your current desktop (KDE or any other), without replacing it
  ([xp-explorer/](xp-explorer/README.md)).

<table>
  <tr>
    <td align="center" width="50%">
      <img src="docs/images/screenshot-classic-en.png" alt="The XP shell with the classic theme and Linux applications" width="400" /><br />
      XP's classic theme (<code>--theme classic</code>)
    </td>
    <td align="center" width="50%">
      <img src="docs/images/screenshot-reactos-en.png" alt="The ReactOS shell and Linux applications" width="400" /><br />
      The ReactOS shell (<code>--shell reactos</code>; no XP ISO needed)
    </td>
  </tr>
</table>

## How it works

```text
LightDM ─ XP Welcome greeter (greeter/)
   └─ XP Shell session (session/supervisor.py)
        ├─ Wine 11.0 + patches (wine/patches/)
        │    └─ XP's explorer.exe, shell32.dll, … (extracted from your ISO; ReactOS's in its session)
        ├─ bridge-wm: an X11 window manager + Win32 proxy windows (bridge-wm/)
        │    (Wayland session: a patched labwc instead, wayland/)
        └─ fcitx5 + Mozc, tray bridge (bridge-wm/tray.py)
```

The design documents are in Japanese; [docs/README.md](docs/README.md) lists them.

## Caution

> [!WARNING]
> - **Obtain the Windows XP ISO yourself.** Neither this repository nor its scripts download any Microsoft files.
>   Follow Microsoft's license terms for the ISO and the files extracted from it.
> - **Use at your own risk.** The authors are not responsible for data loss or any other problems.
> - This is an unofficial project, not affiliated with Microsoft or the Wine project.
> - Only the English and the Japanese XP SP3 (x86) have been tested. Other languages are set up like English XP and
>   might work, but have not been tested (`setup.sh` warns and goes on).

## Ground rules

- Binaries from Windows are never modified; differences are handled in Wine and around it.
- Microsoft binaries are never disassembled. Behavior is studied through public APIs and our own probes on a real XP.
- Microsoft files, extracted files and prefixes live only in `localres/` and `.local/`, which Git ignores.

## Similar projects

- [loss32](https://loss32.org/): a planned Linux distribution whose desktop is made only of Win32 software on Wine,
  with parts of ReactOS. While Winedows runs XP's own shell on Debian, loss32 is built from free software only.

## Credits

- [mikuta0407](https://github.com/mikuta0407) wrote [win4wine](https://github.com/mikuta0407/win4wine), the base of
  this project: the patched Wine, bridge-wm, the session, the greeters and the XP, ReactOS and Windows 7 shells.
- [AshesTom](https://github.com/AshesTom): XP Explorer and its additions, XP's Display Properties, sounds, keyboard
  layouts, XP's own notification area icons, the Start menu's left column, Internet Explorer 6 and Windows Media
  Player 9, Arch Linux support, `xp.sh` and more.

## License

[LGPL-2.1-or-later](LICENSE), the same as Wine. It covers only the code and patches in this repository,
except `wayland/patches/labwc/` (GPL-2.0-only, as labwc) and `wayland/patches/wlroots/` (MIT, as wlroots).
The Windows XP files, and the XP screens in the screenshots, are copyrighted by Microsoft.
The ReactOS files that the ReactOS shell uses are under ReactOS's licenses (GPL-2.0-or-later; most of its DLLs LGPL-2.1-or-later).
