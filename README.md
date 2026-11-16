# Winedows (Windows XP, Wine)

English · [日本語](.github/readme/README.ja.md)

> [!WARNING]
> This version is discontinued and will not receive further updates.
> Use the native version instead: [`nativeshell`](../../tree/nativeshell) branch, [releases](../../releases).

Runs Windows XP's `explorer.exe` on a patched Wine as the Linux desktop shell. Linux applications get XP window
frames, taskbar buttons and tray icons.

![Desktop](docs/images/screenshot-en.png)

| | |
|---|---|
| ![Windows Classic](docs/images/screenshot-classic-en.png) | ![ReactOS shell](docs/images/screenshot-reactos-en.png) |

## Features

- XP shell: taskbar, Start menu, desktop, Explorer windows (Luna or Windows Classic)
- Linux applications in the taskbar, Alt+Tab and All Programs
- Linux tray icons in the XP notification area, with network, volume and battery icons
- XP programs and control panels from your ISO: Notepad, Paint, Internet Explorer 6, Windows Media Player 9, Display Properties, User Accounts, Network Connections
- Windows applications (32 and 64-bit) through Wine 11.0
- English and Japanese XP, Japanese input with fcitx5 and Mozc
- Optional XP logon screen (LightDM greeter)
- ReactOS shell, for use without an XP ISO
- About 520 MB of RAM for the whole session

## Requirements

- Debian 13, freshly installed
- A Windows XP Professional SP3 x86 ISO, English or Japanese
- About 40 GB of free space to build Wine (about 90 minutes on 4 cores)

## Installation

```sh
git clone -b WinXP https://github.com/AshesTom/Winedows.git ~/winedows
cd ~/winedows
scripts/setup.sh --media /path/to/windows-xp.iso --greeter
```

Reboot after the installation. Without an XP ISO, use the ReactOS shell:

```sh
scripts/setup.sh --shell reactos --lang en
```

Options and configuration: [installation guide](.github/readme/INSTALL.en.md).

## Notice

Experimental, unofficial project, not affiliated with Microsoft or the Wine project. No Microsoft files are
distributed: you must provide your own Windows XP ISO. Files extracted from it stay in `localres/` and `.local/`.

## License

LGPL-2.1-or-later, for the code and patches in this repository only. `wayland/patches/labwc/` is GPL-2.0-only and
`wayland/patches/wlroots/` is MIT. Windows XP and its files are copyrighted by Microsoft. Originally forked from
[mikuta0407/win4wine](https://github.com/mikuta0407/win4wine).
