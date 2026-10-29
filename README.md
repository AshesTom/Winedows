<h1 align="center">Winedows · Windows 7</h1>
<p align="center">
  <img src="docs/images/screenshot-win7-en.png" alt="Winedows: the Windows 7 shell with Linux and Windows applications on Debian 13" width="800" />
</p>
<p align="center">
  <strong>Your Linux desktop, with the real Windows 7 shell.</strong>
</p>
<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-LGPL--2.1--or--later-blue?style=flat" alt="License: LGPL-2.1-or-later"></a>
  <img src="https://img.shields.io/badge/Debian-13-A81D33?style=flat&logo=debian&logoColor=white" alt="Debian 13">
  <img src="https://img.shields.io/badge/Wine-11.0-8B0000?style=flat" alt="Wine 11.0">
  <img src="https://img.shields.io/badge/status-experimental-orange?style=flat" alt="Status: experimental">
</p>
<p align="center">
  <strong>English</strong> ·
  <a href=".github/readme/README.ja.md">日本語</a>
</p>

> [!IMPORTANT]
> **This is a personal, experimental project**, newer and less tested than the XP shell.
> It has only been tested on a **freshly installed Debian 13** set up with the installation script.
> A virtual machine or a spare machine is recommended.

## What it is

Winedows runs Windows 7 SP1's own x64 `explorer.exe` on a patched Wine as the **desktop shell of Linux**.
The taskbar, Start menu, notification area, desktop, folder windows and file dialogs are Windows 7's real ones, and
Linux applications get Windows 7's frames and are grouped per application on the taskbar. No Microsoft files are
included: the few that are needed are extracted locally from your own ISO.

Winedows started as a fork of [mikuta0407/win4wine](https://github.com/mikuta0407/win4wine) and keeps its
internal names (`win4wine-*`, `~/.config/win4wine`, …).

### One branch per Windows

| Branch | Shell |
|---|---|
| [`WinXP`](../../tree/WinXP) | Windows XP (Luna or Classic), the ReactOS shell, XP Explorer |
| **`Win7`** (this one) | Windows 7 |

`master` only points to these two.

## Install

1. Get a Windows 7 SP1 x64 ISO **yourself**.
2. On a freshly installed Debian 13, run as the user who will log in:

```sh
git clone -b Win7 https://github.com/AshesTom/Winedows.git ~/winedows
cd ~/winedows
scripts/setup.sh --media /path/to/Windows7SP1x64.iso --greeter
```

3. Reboot. Windows 7's logon screen logs you straight into the Windows 7 shell.

| Option | Meaning |
|---|---|
| `--media ISO` | the Windows 7 SP1 x64 ISO (required) |
| `--greeter` | make LightDM show Windows 7's logon screen (LightDM becomes the display manager) |
| `--no-greeter` | keep the current login screen (the default) and choose "Windows 7 Shell (Wine, X11)" there |
| `--dpi N` | the screen's DPI: 96 (100%), 120 (125%) or any value from 19 to 480 |
| `--wayland` | also the "Windows 7 Shell (Wine, Wayland)" session, with a patched labwc (more experimental) |
| `--wine-install DIR` | use a Wine built elsewhere with this repository's patches |
| `--jobs N` | parallel jobs for the Wine build (default: the number of CPUs) |

Building Wine takes about 90 minutes on 4 cores and about 40 GB of free space. Steps already done are skipped, so the
script can be run again after a failure. Details (in Japanese): [docs/windows7.md](docs/windows7.md).

### Settings (`~/.config/win4wine/win7-session.json`)

| Key | Default | Meaning |
|---|---|---|
| `wine`, `prefix` | (written by setup.sh) | the Wine and the prefix the session uses |
| `locale` | `en_US.UTF-8` (`ja_JP.UTF-8` for Japanese media) | the session's locale |
| `input_method` | `fcitx5` for Japanese media only | the input method; left out, none is started |
| `linux_menu` | `true` | list the Linux applications in the Start menu |
| `welcome_screen` | `true` | Windows 7's Welcome and log-off screens around the session |
| `cursor_theme` | `win4wine-win7` | the cursor theme; `null` for none |
| `shutdown_command`, `reboot_command` | `systemctl poweroff` / `reboot` | what Shut down and Restart run |
| `outputs`, `primary` | (none) | Wayland session: per-output settings, e.g. `{"HDMI-A-1": {"scale": 1.5}}`, and the output that holds the taskbar |
| `composite` | `true` | let the X server keep the contents of Wine's windows (no black areas while moving windows) |
| `window_snap` | `false` | `true`: Aero Snap-like halves and maximize with Win+arrows or by dragging to the screen's edges |

Logs: `~/.local/state/win4wine/` (the previous session's are `*.log.1`). If the shell keeps crashing, the session falls
back to Wine's own explorer and an xterm.

### Uninstall

```sh
greeter/install.sh --remove   # back to LightDM's previous login screen
sudo rm /usr/share/xsessions/windows7-shell.desktop /usr/local/bin/windows7-shell-session
sudo rm -f /usr/share/wayland-sessions/windows7-shell-wayland.desktop /usr/local/bin/windows7-shell-wayland-session
```

Everything built, extracted and the prefix live in `.local/` of the checkout. Installed packages are left as they are.

## What it does

- **Windows 7's shell**: Start menu, taskbar, notification area, desktop, Computer and folder windows, libraries.
- **Windows 7's file dialogs** in Windows applications too (the shell's DLLs are shared with them; a program that
  misbehaves can be sent back to Wine's own with `prefix/win7.py builtin`).
- **Linux applications**: Linux windows get Windows 7's frames; the taskbar, Alt+Tab, minimize and maximize work as on
  Windows 7, and Linux applications are listed in the Start menu.
- **Windows 7's logon screen** (optional), Welcome and log-off screens, Aero cursors, the Aero theme, Segoe UI and
  ClearType as on Windows 7.
- **Japanese or English Windows 7**: the language is detected from the ISO; Japanese input with fcitx5 + Mozc.
  Tested with the Japanese media, and English through the English language pack's files.
- **Windows applications**: a WoW64 Wine 11.0 runs 32-bit and 64-bit programs.

<table>
  <tr>
    <td align="center" width="50%">
      <img src="docs/images/screenshot-win7-logon.png" alt="Windows 7's logon screen, choosing a user" width="400" /><br />
      The logon screen (<code>--greeter</code>)
    </td>
    <td align="center" width="50%">
      <img src="docs/images/screenshot-win7-small-en.png" alt="The Windows 7 shell with small taskbar icons" width="400" /><br />
      Small taskbar icons with labels
    </td>
  </tr>
</table>

## Limits

- **Aero Glass (experimental, the default)**: the taskbar, Start menu and window frames are glass with Aero's caption
  buttons, tinted with the window color (Sky) and the glass's reflection, with live taskbar thumbnails and Aero Peek
  (Wine patches 0152 and 0153). The session's own compositor, w7-dwm ([windows7/dwm.c](windows7/dwm.c), OpenGL ES
  through EGL), blends the glass, blurs behind it, casts Windows 7's window shadows and animates the windows as Windows 7 does (opening and
  closing with a slight 3D tilt and fade, minimizing into the taskbar, morphing when maximized or restored); without it (no EGL development files when it was built), or with
  `"aero": false` in `win7-session.json` (`W7_AERO=0 windows7/7.sh run`), it is Windows 7 Basic. On a Wayland desktop,
  `windows7/7.sh run` opens the session in a rootful Xwayland, drawn by the GPU, rather than Xephyr. Peek shows no outlines of the hidden windows yet.
- The notification area is Windows 7's own: SysTray (stobject), the network icon (pnidui) and the volume icon
  (SndVolSSO), from the user's Windows 7 files. Their state is not right yet: the volume says no output device is
  installed and the network stays at "Identifying" (Wine has no network list and connection services for them).
- The Network folder, Help and Support and most of Control Panel are missing: Wine has no counterparts for their components.
- Lock, Sleep and Hibernate in the Start menu do nothing.

## How it works

```text
LightDM ─ Windows 7 logon screen (greeter/win7-greeter.py)
   └─ Windows 7 Shell session (session/supervisor.py)
        ├─ Wine 11.0 + patches (wine/patches/)
        │    └─ Windows 7's explorer.exe, shell32.dll, explorerframe.dll, … (from your ISO, extract/win7.py)
        ├─ bridge-wm: an X11 window manager + Win32 proxy windows (bridge-wm/)
        │    (Wayland session: a patched labwc instead, wayland/)
        └─ fcitx5 + Mozc, tray bridge (bridge-wm/tray.py)
```

To try the session in a window of your current desktop without installing it: `windows7/7.sh setup ISO`, then
`windows7/7.sh run` (Arch / CachyOS; `windows7/7.sh` with no argument lists its commands and switches).

The design documents are in Japanese; [docs/README.md](docs/README.md) lists them. Some of them still describe the XP
shell, with which this branch shares Wine, the bridge and the session.

## Caution

> [!WARNING]
> - **Obtain the Windows 7 ISO yourself.** Neither this repository nor its scripts download any Microsoft files.
>   Follow Microsoft's license terms for the ISO and the files extracted from it.
> - **Use at your own risk.** The authors are not responsible for data loss or any other problems.
> - This is an unofficial project, not affiliated with Microsoft or the Wine project.

## Ground rules

- Binaries from Windows are never modified; differences are handled in Wine and around it.
- Microsoft binaries are never disassembled. Behavior is studied through public APIs and our own probes on a real Windows.
- Microsoft files, extracted files and prefixes live only in `localres/` and `.local/`, which Git ignores.

## Credits

- [mikuta0407](https://github.com/mikuta0407) wrote [win4wine](https://github.com/mikuta0407/win4wine), the base of
  this project, including the Windows 7 shell.
- [AshesTom](https://github.com/AshesTom): Winedows.

## License

[LGPL-2.1-or-later](LICENSE), the same as Wine. It covers only the code and patches in this repository,
except `wayland/patches/labwc/` (GPL-2.0-only, as labwc) and `wayland/patches/wlroots/` (MIT, as wlroots).
The Windows 7 files, and the Windows 7 screens in the screenshots, are copyrighted by Microsoft.
