# Winedows (Windows 7, Wine)

English · [日本語](.github/readme/README.ja.md)

> [!WARNING]
> This version is discontinued and will not receive further updates.
> Use the native version instead: [`nativeshell`](../../tree/nativeshell) branch, [releases](../../releases).

Runs Windows 7 SP1's x64 `explorer.exe` on a patched Wine as the Linux desktop shell. Linux applications get
Windows 7 window frames and are grouped on the taskbar.

![Desktop](docs/images/screenshot-win7-en.png)

| | |
|---|---|
| ![Logon screen](docs/images/screenshot-win7-logon.png) | ![Small taskbar icons](docs/images/screenshot-win7-small-en.png) |

## Features

- Windows 7 shell: Start menu, taskbar, notification area, desktop, Explorer windows, libraries
- Windows 7 file dialogs in Windows applications
- Linux applications in the taskbar, Alt+Tab and the Start menu
- Aero Glass, live taskbar thumbnails and Aero Peek (experimental)
- Optional Windows 7 logon screen (LightDM greeter), Aero cursors, Segoe UI
- Windows applications (32 and 64-bit) through Wine 11.0
- English and Japanese Windows 7, Japanese input with fcitx5 and Mozc

## Known limitations

- The network and volume icons do not show the correct state.
- The Network folder, Help and Support and most of the Control Panel are missing.
- Lock, Sleep and Hibernate do nothing.

## Requirements

- Debian 13, freshly installed
- A Windows 7 SP1 x64 ISO
- About 40 GB of free space to build Wine (about 90 minutes on 4 cores)

## Installation

```sh
git clone -b Win7 https://github.com/AshesTom/Winedows.git ~/winedows
cd ~/winedows
scripts/setup.sh --media /path/to/windows-7.iso --greeter
```

Reboot after the installation.

| Option | Description |
|---|---|
| `--media ISO` | Windows 7 SP1 x64 ISO (required) |
| `--greeter` | Use the Windows 7 logon screen (LightDM) |
| `--dpi N` | Screen DPI, for example 96 or 120 |
| `--wayland` | Also install the Wayland session (experimental) |
| `--jobs N` | Parallel jobs for the Wine build |

Settings are in `~/.config/win4wine/win7-session.json`, logs in `~/.local/state/win4wine/`.
More details (Japanese): [docs/windows7.md](docs/windows7.md).

## Notice

Experimental, unofficial project, not affiliated with Microsoft or the Wine project. No Microsoft files are
distributed: you must provide your own Windows 7 ISO. Files extracted from it stay in `localres/` and `.local/`.

## License

LGPL-2.1-or-later, for the code and patches in this repository only. `wayland/patches/labwc/` is GPL-2.0-only and
`wayland/patches/wlroots/` is MIT. Windows 7 and its files are copyrighted by Microsoft. Originally forked from
[mikuta0407/win4wine](https://github.com/mikuta0407/win4wine).
