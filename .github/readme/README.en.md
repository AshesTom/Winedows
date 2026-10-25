<p align="center">
  <img src="../../docs/images/screenshot-en.png" alt="win4wine: the English Windows XP shell with Linux and Windows applications on Debian 13" width="800" />
</p>
<p align="center">
  <strong>Your Linux desktop, with the Windows XP shell.</strong>
</p>
<p align="center">
  <a href="../../LICENSE"><img src="https://img.shields.io/badge/license-LGPL--2.1--or--later-blue?style=flat" alt="License: LGPL-2.1-or-later"></a>
  <img src="https://img.shields.io/badge/Debian-13-A81D33?style=flat&logo=debian&logoColor=white" alt="Debian 13">
  <img src="https://img.shields.io/badge/Wine-11.0-8B0000?style=flat" alt="Wine 11.0">
  <img src="https://img.shields.io/badge/status-experimental-orange?style=flat" alt="Status: experimental">
</p>
<p align="center">
  <a href="../../README.md">日本語</a> ·
  <strong>English</strong>
</p>

> [!IMPORTANT]
> **This is a personal, experimental project**, not a finished product for daily use.
> It has only been tested on a **freshly installed Debian 13** set up with the installation script.
> Installing it on a system already in use (with another desktop environment or display manager, for example) has not been tried.
> A virtual machine or a spare machine is recommended.

## Install

1. Get a Windows XP Professional SP3 (x86) ISO (Japanese or English) **yourself**.
2. On a freshly installed Debian 13, run as the user who will log in:

```sh
git clone https://github.com/mikuta0407/win4wine.git ~/win4wine
cd ~/win4wine
scripts/setup.sh --media /path/to/WindowsXPSP3.iso --greeter
```

3. Reboot. The XP Welcome screen logs you straight into the XP shell.

Without an XP ISO, try the ReactOS shell instead (no ISO needed):

```sh
scripts/setup.sh --shell reactos --lang en   # or --lang ja
```

and choose the session "ReactOS Shell (Wine, X11)" at the login screen.

Details: 🔗 [installation guide (INSTALL.en.md)](INSTALL.en.md). Building Wine takes about 90 minutes on 4 cores.
The theme (XP's classic one, for example) and the text size can be changed later ([Changing the look and the text size](INSTALL.en.md#changing-the-look-and-the-text-size)).

## What it is

It runs Windows XP's own `explorer.exe` on a patched Wine as the **desktop shell of Linux**.
The taskbar, Start menu, desktop and folder windows are XP's real ones, and Linux applications get XP's frames and taskbar buttons.

No Microsoft files are included. Only the files that are needed are extracted locally from your own ISO.

Without an XP ISO, the session can run **[ReactOS](https://reactos.org/)'s shell** instead (a Windows 2000-like classic look).
ReactOS is free software, so the setup script downloads it from its official release ([docs/reactos.md](../../docs/reactos.md), Japanese).

**Windows 7's shell** is on the [`master` branch](../../../../tree/master). This branch (`WinXP`) has the XP (and ReactOS) shells,
and **XP Explorer** ([xp-explorer/](../../xp-explorer/README.md)), which opens XP's shell in a window of your Linux desktop.

## What it does

- **XP's shell**: Luna (or Classic) taskbar, Start menu, desktop, My Computer and folder windows.
- **Linux applications**: Linux windows get XP's frames; the taskbar, Alt+Tab, minimize and maximize work as on XP.
  Linux applications are listed under All Programs.
- **Notification area**: Linux tray icons (StatusNotifierItem and XEmbed) show up in XP's notification area,
  beside XP's own network, battery and volume icons and their flyouts (Volume, Power Meter, Wireless Network
  Connection Status) and XP's Volume Control mixer, fed by NetworkManager and PipeWire or PulseAudio (from XP Explorer, [xp-explorer/](../../xp-explorer/README.md)).
  The wireless network list connects to new networks too, through XP's network key dialog. Their menus and windows
  read their words from your ISO's XP files.
- **From XP Explorer**: the folder task pane, Display Properties (the Windows XP and Windows Classic themes, styles and
  color schemes; the schemes are made from your ISO's HIVEDEF.INF and themeui.dll), image thumbnails and previews,
  XP's sounds (startup, logon and logoff too),
  Network Connections, Internet Explorer 6, the Search Companion with Rover, Windows Media Player 9, and the Start menu's left column (Internet,
  E-mail and the most used programs), User Accounts ("Change my picture" offers XP's 23 pictures and changes the
  Start menu's and the Linux user's), and XP's Network Connections folder (the Linux wired and wireless adapters and
  their state, opened from the notification area's network icon) with XP's "Choose a wireless network" (NetworkManager's
  Wi-Fi networks, joined from it), Add or Remove Programs (with the Start menu's Set Program Access and
  Defaults; the Linux applications are listed too, Remove goes to the package manager and the default programs become
  the Linux defaults), the Security Center (Automatic Updates, Internet Options) and the Help and Support Center (XP's help
  topics and their service, opened from the Start menu's Help and Support), all from your own ISO's files (`prefix/extras.py`).
- **XP's own programs and All Programs**: XP's Calculator, Notepad, Paint, WordPad, Command Prompt, Task Manager,
  Registry Editor, Character Map, Sound Recorder, Volume Control, Magnifier, On-Screen Keyboard, HyperTerminal,
  Remote Desktop Connection, the games (FreeCell, Hearts, Minesweeper, Pinball, Solitaire, Spider Solitaire) and
  Windows Messenger, in place of Wine's. All Programs is XP's own, made from your ISO's setup INFs in its language
  (Windows Update, Windows Catalog and Set Program Access and Defaults at the top, then Accessories, Games, Startup
  and XP's programs); the Linux applications are in its Linux Applications folder (`prefix/startmenu.py`).
- **Keyboard layout**: the Linux system's (`localectl`) at first. Add input languages or change the default in
  Control Panel, Regional and Language Options, Languages, Details (XP's own), and the session's layout (labwc on
  Wayland, setxkbmap on X11) follows at once; with two or more, Left Alt+Shift switches them as on XP
  (`session/keyboard.py`).
- **Japanese input**: fcitx5 + Mozc, toggled with Ctrl+Space (or Zenkaku/Hankaku), in GTK, Qt and Wine applications.
- **Japanese or English XP**: either ISO works. The language is detected from the ISO, and the fonts (Tahoma and others on
  English XP), the locale and the Welcome screen follow that XP (no Japanese input is set up for English XP).
- **Windows applications**: a WoW64 Wine 11.0 runs 32-bit and 64-bit programs. Run `wine setup.exe` in a terminal and
  the installer's shortcuts land in XP's Start menu.
- **XP Welcome screen**: an optional LightDM greeter that shows XP's Welcome and log-off screens.
  While the shell starts, XP's big "welcome" screen is shown.
- **XP's keys**: the Windows key, Ctrl+Esc, Win+R and Win+E.
- **Quick Launch**: right-click the taskbar, Toolbars, Quick Launch (XP and ReactOS; on Windows 7, as on the real one, New
  Toolbar with the Quick Launch folder). Shortcuts to Linux applications from the Start menu can go there too, and Show
  Desktop gives the windows back when used again, as on Windows.
- **Recovery**: if the XP shell keeps crashing, the session falls back to Wine's own explorer and an xterm.
- **Wayland session (optional, even more experimental than the X11 session)**: a patched labwc instead of the X11 WM
  (XP and ReactOS shells), with a scale per monitor and scales such as 125% or 150%; the shell stays crisp,
  and Wayland applications get the shell's frames too. It is new, tested less than the X11 session and has known limits ([docs/wayland.md](../../docs/wayland.md),
  Japanese). The X11 session is the one to try first.
- **ReactOS shell (optional)**: the session with ReactOS 0.4.16's explorer instead of XP's, in Japanese or English; it can be set
  up beside the XP shell.
- **XP Explorer (optional)**: without replacing your session, XP's shell in a Wine virtual desktop window next to KDE or
  any other desktop ([xp-explorer/](../../xp-explorer/README.md)).

<table>
  <tr>
    <td align="center" width="50%">
      <img src="../../docs/images/screenshot-classic-en.png" alt="The XP shell with the classic theme and Linux applications" width="400" /><br />
      XP's classic theme (<code>--theme classic</code>)
    </td>
    <td align="center" width="50%">
      <img src="../../docs/images/screenshot-reactos-en.png" alt="The ReactOS shell and Linux applications" width="400" /><br />
      The ReactOS shell (<code>--shell reactos</code>; no XP ISO needed)
    </td>
  </tr>
</table>

## Caution

> [!WARNING]
> - **Obtain the Windows XP (and Windows 7) ISO yourself.** Neither this repository nor its scripts download any Microsoft files.
>   Follow Microsoft's license terms for the ISO and the files extracted from it.
> - **Use at your own risk.** The author is not responsible for data loss or any other problems.
> - This is an unofficial project, not affiliated with Microsoft or the Wine project.
> - Only the Japanese and the English XP SP3 (x86) have been tested. XP in other languages is set up the same way as English XP
>   and might work, but it has not been tested at all (`setup.sh` warns and goes on).

## How it works

```text
LightDM ─ XP Welcome greeter (greeter/)
   └─ XP Shell session (session/supervisor.py)
        ├─ Wine 11.0 + 181 patches (wine/patches/)
        │    └─ XP's explorer.exe, shell32.dll, … (extracted from the ISO; ReactOS's or Windows 7's in their sessions)
        ├─ bridge-wm: an X11 WM + Win32 proxy windows (bridge-wm/)
        │    (Wayland session: a patched labwc instead of the WM, wayland/)
        └─ fcitx5 + Mozc, tray bridge (bridge-wm/tray.py)
```

The design documents are in Japanese; [docs/README.md](../../docs/README.md) lists them.

## Similar projects

- [loss32](https://loss32.org/): a planned Linux distribution whose desktop is made only of Win32 software on Wine,
  with parts of ReactOS. While win4wine runs XP's own shell on Debian, loss32 is built from free software only.

## Ground rules

- Binaries from Windows are never modified; differences are handled in Wine and around it.
- Microsoft binaries are never disassembled. Behavior is studied through public APIs and our own probes on a real XP.
- Microsoft files, extracted files and prefixes live only in `localres/` and `.local/`, which Git ignores.

## Contributors

- [AshesTom](https://github.com/AshesTom) ([fork](https://github.com/AshesTom/win4wine)): XP's Display Properties,
  sounds, keyboard layouts, XP's own notification area icons, the Start menu's left column, Internet Explorer 6 and
  Windows Media Player 9, the welcome screen while the shell starts, caption animation and small icons, Arch Linux
  support, `xp.sh` and more.

## License

[LGPL-2.1-or-later](../../LICENSE), the same as Wine. It covers only the code and patches in this repository,
except `wayland/patches/labwc/` (GPL-2.0-only, as labwc) and `wayland/patches/wlroots/` (MIT, as wlroots).
The Windows XP and Windows 7 files, and the XP and Windows 7 screens in the screenshots, are copyrighted by Microsoft.
The ReactOS files that the ReactOS shell uses are under ReactOS's licenses (GPL-2.0-or-later; most of its DLLs LGPL-2.1-or-later).
