#!/usr/bin/env python3
"""XP's Command Prompt for the Linux system.

A terminal (xterm) with the look of XP's console window: XP's raster
"Terminal" font (vgaoem.fon, made a Unicode BDF font) at 8x12, the console's 16 colours, light gray
on black, 80x25 with 300 lines of history, an underline cursor, cmd.exe's
icon and the title of the Start menu's shortcut. It runs bash with
session/xp-cmd.bashrc: XP's banner and a prompt that names the folder as XP
would (H:\\ is the home, Z:\\ the root of the Linux system), and the commands
of the Linux system.

  xp_cmd.py [PREFIX]      the prefix (default: $WINEPREFIX) gives XP's files
  --title TITLE           the window's title (default: the shortcut's name)
  --icon PROGRAM          whose icon the window shows (default: cmd.exe)
  --run COMMAND           run COMMAND with bash instead of the prompt
  --dir FOLDER            start in FOLDER, named the XP way (C:\\windows, H:\\Downloads)
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.append(str(ROOT / 'greeter'))
from xpres import Resources  # noqa: E402

RT_ICON, RT_GROUP_ICON = 3, 14
CACHE = Path(os.environ.get('XDG_CACHE_HOME') or Path.home() / '.cache') / 'win4wine/cmd'
# The console's palette in the ANSI order: black, red, green, yellow, blue,
# magenta, cyan, white, then the bright ones (XP's 16 console colours).
PALETTE = ('#000000', '#800000', '#008000', '#808000', '#000080', '#800080', '#008080', '#C0C0C0',
           '#808080', '#FF0000', '#00FF00', '#FFFF00', '#0000FF', '#FF00FF', '#00FFFF', '#FFFFFF')
TITLE = {'en': 'Command Prompt', 'ja': 'コマンド プロンプト'}
# Code page 437's pictures in its control characters and 0x7F, as the
# console draws them (Python's cp437 codec has the controls there).
CP437_LOW = '\0☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼'
FONT = 'XP Terminal'


def fon_to_bdf(fon, target):
    """XP's raster console font (a .fon of one Windows 2.x FNT, code page 437)
    as a BDF font with the Unicode characters of its glyphs: xterm takes it
    through fontconfig, which reads the FNT as code page 437 only."""
    data = fon.read_bytes()
    ne = struct.unpack_from('<I', data, 0x3c)[0]
    table = ne + struct.unpack_from('<H', data, ne + 0x24)[0]
    shift, at, font = struct.unpack_from('<H', data, table)[0], table + 2, None
    while not font and struct.unpack_from('<H', data, at)[0]:
        kind, count = struct.unpack_from('<HH', data, at)
        at += 8
        for _ in range(count):
            start = struct.unpack_from('<H', data, at)[0] << shift
            at += 12
            if kind == 0x8008 and not font:
                font = data[start:]
    if not font or struct.unpack_from('<H', font, 0)[0] != 0x200:
        raise ValueError('not a Windows 2.x raster font')
    ascent = struct.unpack_from('<H', font, 74)[0]
    width, height = struct.unpack_from('<HH', font, 86)
    first, last = font[95], font[96]
    glyphs = []
    for code in range(first, last + 1):
        char = CP437_LOW[code] if code < 0x20 else '⌂' if code == 0x7f else bytes([code]).decode('cp437')
        glyph_width, start = struct.unpack_from('<HH', font, 118 + (code - first) * 4)
        columns = (glyph_width + 7) // 8
        rows = [''.join(f'{font[start + column * height + row]:02X}' for column in range(columns))
                for row in range(height)]
        glyphs.append(f'STARTCHAR U+{ord(char):04X}\nENCODING {ord(char)}\nSWIDTH 500 0\n'
                      f'DWIDTH {glyph_width} 0\nBBX {glyph_width} {height} 0 {ascent - height}\nBITMAP\n'
                      + '\n'.join(rows) + '\nENDCHAR\n')
    target.write_text(f'STARTFONT 2.1\nFONT -win4wine-{FONT.replace(" ", "")}-medium-r-normal--{height}-90-96-96-c-'
                      f'{width * 10}-iso10646-1\nSIZE 9 96 96\nFONTBOUNDINGBOX {width} {height} 0 {ascent - height}\n'
                      f'STARTPROPERTIES 6\nFAMILY_NAME "{FONT}"\nFONT_ASCENT {ascent}\nFONT_DESCENT {height - ascent}\n'
                      f'SPACING "C"\nCHARSET_REGISTRY "ISO10646"\nCHARSET_ENCODING "1"\nENDPROPERTIES\n'
                      f'CHARS {len(glyphs)}\n' + ''.join(glyphs) + 'ENDFONT\n', encoding='utf-8')


def icon_file(exe, target, side=32):
    """The image of EXE's first icon group nearest SIDE pixels (the most
    colours of those) as an .ico file."""
    resources = Resources(exe)
    # The lowest numbered group, else the first named one (the directory sorts both)
    groups = sorted(resources.names(RT_GROUP_ICON), key=lambda name: isinstance(name, str))
    group = resources.find(RT_GROUP_ICON, groups[0]) if groups else None
    if group is None:
        raise ValueError(f'{exe}: no icon')
    count = struct.unpack_from('<H', group, 4)[0]
    entries = []
    for i in range(count):
        width, height, colours, _, planes, bits, _, number = struct.unpack_from('<BBBBHHIH', group, 6 + i * 14)
        image = resources.find(RT_ICON, number)
        if image:
            entries.append((width, height, colours, planes, bits, image))
    entries = [min(entries, key=lambda e: (abs((e[0] or 256) - side), -e[4]))]
    out = struct.pack('<HHH', 0, 1, len(entries))
    position = 6 + 16 * len(entries)
    for width, height, colours, planes, bits, image in entries:
        out += struct.pack('<BBBBHHII', width, height, colours, 0, planes, bits, len(image), position)
        position += len(image)
    target.write_bytes(out + b''.join(image for *_, image in entries))


def prepare(prefix, program='cmd'):
    """The font configuration and PROGRAM's icon, made once in CACHE."""
    CACHE.mkdir(parents=True, exist_ok=True)
    try:
        assets = Path(json.loads((prefix / 'xp-shell.json').read_text())['assets'])
    except (OSError, ValueError, KeyError, TypeError):
        assets = ROOT / '.local/assets'
    fonts, bdf = CACHE / 'fonts.conf', CACHE / 'fonts/xp-terminal.bdf'
    if (assets / 'fonts/vgaoem.fon').is_file() and not bdf.is_file():
        bdf.parent.mkdir(exist_ok=True)
        try:
            fon_to_bdf(assets / 'fonts/vgaoem.fon', bdf)
        except (ValueError, struct.error, IndexError, UnicodeError):
            bdf.unlink(missing_ok=True)
    if bdf.is_file() and not fonts.is_file():
        # Only this terminal sees XP's font; fontconfig rejects bitmap fonts
        # by default (70-no-bitmaps.conf), so it is accepted by name here.
        fonts.write_text(f'''<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
  <include ignore_missing="yes">/etc/fonts/fonts.conf</include>
  <dir>{bdf.parent}</dir>
  <selectfont><acceptfont><pattern><patelt name="family"><string>{FONT}</string></patelt></pattern></acceptfont></selectfont>
</fontconfig>
''')
    icon = CACHE / f'{program}.xpm'
    if not icon.is_file():
        for exe in (prefix / f'drive_c/windows/syswow64/{program}.exe', assets / f'apps/system32/{program}.exe'):
            if exe.is_file() and shutil.which('magick'):
                try:
                    icon_file(exe, CACHE / f'{program}.ico')
                    subprocess.run(['magick', str(CACHE / f'{program}.ico'), str(icon)], check=True,
                                   capture_output=True, timeout=30)
                    break
                except (ValueError, StopIteration, struct.error, subprocess.SubprocessError, OSError):
                    icon.unlink(missing_ok=True)
    return fonts if fonts.is_file() else None, icon if icon.is_file() else None


def linux_folder(prefix, folder):
    """The Linux path of FOLDER, an XP name of a folder, through the prefix's
    drives (dosdevices); None when it is not a folder there. A drive comes
    as "C:\\" in quotes, whose last backslash keeps the quote."""
    folder = folder.rstrip('"').replace('\\', '/')
    if len(folder) < 2 or folder[1] != ':':
        return None
    path = (prefix / 'dosdevices' / folder[:2].lower()) / folder[2:].lstrip('/')
    return path.resolve() if path.is_dir() else None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('prefix', nargs='?', type=Path, default=Path(os.environ.get('WINEPREFIX', ROOT / '.local/prefix')))
    parser.add_argument('--title')
    parser.add_argument('--icon', default='cmd')
    parser.add_argument('--run')
    parser.add_argument('--dir')
    args = parser.parse_args()
    prefix, program = args.prefix, args.icon.lower().removesuffix('.exe')
    try:
        language = json.loads((prefix / 'xp-shell.json').read_text()).get('language', 'en')
    except (OSError, ValueError):
        language = 'en'
    fonts, icon = prepare(prefix, program)
    env = dict(os.environ, XP_CMD_PREFIX=str(prefix))
    for name in ('WINEDEBUG', 'WINEDLLOVERRIDES', 'WIN4WINE_EXTERNAL_SHELL', 'WIN4WINE_FOLDER_SHELL'):
        env.pop(name, None)     # the session's Wine settings, not the user's
    title = args.title or TITLE.get(language, TITLE['en'])
    resources = [f'XTerm*color{i}: {colour}' for i, colour in enumerate(PALETTE)] + [
        'XTerm*cursorUnderLine: true', 'XTerm*cursorBlink: true', 'XTerm*scrollBar: false',
        'XTerm*saveLines: 300', 'XTerm*internalBorder: 0', 'XTerm*borderWidth: 0',
        'XTerm*bellIsUrgent: false', 'XTerm*metaSendsEscape: true', 'XTerm*utf8: 1']
    if icon:
        resources.append(f'XTerm*iconHint: {icon}')
    command = ['xterm', '-class', 'XpCommandPrompt', '-T', title, '-n', title, '-geometry', '80x25',
               '-bg', '#000000', '-fg', '#C0C0C0', '-cr', '#C0C0C0']
    if fonts:
        env['FONTCONFIG_FILE'] = str(fonts)
        command += ['-fa', f'{FONT}:pixelsize=12']
    else:
        command += ['-fa', 'Lucida Console,DejaVu Sans Mono:pixelsize=12']
    for resource in resources:
        command += ['-xrm', resource]
    if args.run:
        command += ['-e', 'bash', '-c', args.run]
    else:
        command += ['-e', 'bash', '--rcfile', str(ROOT / 'session/xp-cmd.bashrc'), '-i']
    env['XP_CMD_TITLE'] = title
    folder = linux_folder(prefix, args.dir) if args.dir else None
    terminal = subprocess.Popen(command, env=env, cwd=folder)
    if icon:
        set_icon(terminal, CACHE / f'{program}.ico')
    sys.exit(terminal.wait())


def set_icon(terminal, ico):
    """cmd.exe's icon on the terminal's window (_NET_WM_ICON, which the XP
    taskbar shows): xterm puts its own there."""
    try:
        from Xlib import Xatom, display
        icons = []
        for side in (16, 32):
            raw = subprocess.run(['magick', str(ico), '-resize', f'{side}x{side}', '-depth', '8', 'bgra:-'],
                                 capture_output=True, check=True, timeout=30).stdout
            icons += [side, side] + list(struct.unpack(f'<{side * side}I', raw[:side * side * 4]))
        d = display.Display()
        pid, net_pid, wm_icon = terminal.pid, d.intern_atom('_NET_WM_PID'), d.intern_atom('_NET_WM_ICON')
        for _ in range(100):
            for window in d.screen().root.query_tree().children:
                for candidate in [window] + list(window.query_tree().children):
                    value = candidate.get_full_property(net_pid, Xatom.CARDINAL)
                    if value and value.value[0] == pid:
                        candidate.change_property(wm_icon, Xatom.CARDINAL, 32, icons)
                        d.flush()
                        return
            if terminal.poll() is not None:
                return
            time.sleep(0.05)
    except Exception:   # without python-xlib or magick, xterm's own icon stays
        pass


if __name__ == '__main__':
    main()
