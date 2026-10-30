#!/usr/bin/env python3
"""The few XP files the native shell draws from, taken from the user's own media.

The native shell needs no Wine and no prefix, so this needs neither 7z nor
cabextract: the ISO's file system is walked here, and each compressed file
(a one-file cabinet, EXPLORER.EX_) goes through bsdtar. No code is run from
the media; the files only hold resources that the shell reads.

  extract.py /path/to/xp.iso [.local/native-assets]
"""
import argparse
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

SECTOR = 2048
# name in the Windows directory -> where it goes under the output, by XP's layout
FILES = {
    'luna.msstyles': 'themes/Luna',
    'explorer.exe': 'system',
    'shell32.dll': 'system32',
    'bliss.jpg': 'web/wallpaper',
    'tahoma.ttf': 'fonts',
    'tahomabd.ttf': 'fonts',
    'framd.ttf': 'fonts',
    'framdit.ttf': 'fonts',
    'trebuc.ttf': 'fonts',
    'trebucbd.ttf': 'fonts',
    'marlett.ttf': 'fonts',
    'themeui.dll': 'system',
    **dict.fromkeys(('airplane.bmp', 'astro.bmp', 'beach.bmp', 'butterfl.bmp', 'car.bmp', 'cat.bmp', 'chess.bmp',
                     'dirtbike.bmp', 'dog.bmp', 'drip.bmp', 'duck.bmp', 'fish.bmp', 'frog.bmp', 'guitar.bmp',
                     'horses.bmp', 'kick.bmp', 'liftoff.bmp', 'palmtree.bmp', 'pnkflowr.bmp', 'redflowr.bmp',
                     'skater.bmp', 'snwflake.bmp', 'soccer.bmp'), 'pictures'),
}
# What session/icon_theme.py makes the Windows XP icon theme from, where it looks for them;
# a theme without some of them still works.
ICON_FILES = {
    'mydocs.dll': 'system32', 'desk.cpl': 'cpl', 'msiexec.exe': 'filetypes', 'zipfldr.dll': 'filetypes',
    'iexplore.exe': 'ie', 'main.cpl': 'keyboard', 'user32.dll': 'resources', 'shimgvw.dll': 'shell',
    'netshell.dll': 'tray', 'sndvol32.exe': 'tray', 'wmploc.dll': 'wmp',
    # The folder windows' flag, the Common Tasks pane in each colour scheme, and the Control Panel's applets
    'browseui.dll': 'system32', 'blue_ss.dll': 'themes/Luna/Shell/NormalColor', 'home_ss.dll': 'themes/Luna/Shell/Homestead',
    'metal_ss.dll': 'themes/Luna/Shell/Metallic', 'timedate.cpl': 'cpl', 'mmsys.cpl': 'cpl', 'sysdm.cpl': 'cpl',
    'nusrmgr.cpl': 'cpl', 'appwiz.cpl': 'cpl', 'ncpa.cpl': 'cpl',
    # XP's Welcome, log off and Turn Off screens (greeter/welcome.py)
    'logonui.exe': 'system32', 'msgina.dll': 'system32', 'winlogon.exe': 'system32', 'msgothic.ttc': 'fonts',
    **dict.fromkeys(('arial.ttf', 'arialbd.ttf', 'ariali.ttf', 'arialbi.ttf', 'micross.ttf'), 'fonts'),
    # System Properties' other pages
    'netid.dll': 'cpl', 'wuaucpl.cpl': 'cpl', 'remotepg.dll': 'cpl',
    # The Search Companion: its icons and words, its steps, and its character
    'srchui.dll': 'srchasst', 'rover.acs': 'srchasst/chars',
    **dict.fromkeys(('finish.xml', 'inetopts.xml', 'inetpref.xml', 'inetsrch.xml', 'lcladv.xml', 'lclcomp.xml',
                     'lcldocs.xml', 'lclmm.xml', 'lclmode.xml', 'lclrfine.xml', 'lclsrch.xml'), 'srchasst/mui'),
}
# The wallpapers' setup list, which has their names as XP shows them.
WALLPAPER_LIST = 'shl_img.inf'


class Iso9660:
    """A read-only walk of the ISO 9660 primary volume, which XP's media has."""

    def __init__(self, path):
        self.file = open(path, 'rb')
        self.file.seek(16 * SECTOR)
        volume = self.file.read(SECTOR)
        if volume[:6] != b'\x01CD001':
            raise SystemExit(f'{path}: not an ISO 9660 image')
        self.root = volume[156:156 + 34]

    def read(self, sector, size):
        self.file.seek(sector * SECTOR)
        return self.file.read(size)

    @staticmethod
    def record(data):
        sector, size = struct.unpack_from('<I4xI', data, 2)
        flags, length = data[25], data[32]
        name = data[33:33 + length].decode('ascii', 'replace').split(';')[0].rstrip('.').lower()
        return name, sector, size, bool(flags & 2)

    def walk(self, record=None, seen=None):
        """(name, sector, size) of each file under a directory record."""
        seen = set() if seen is None else seen
        _, sector, size, _ = self.record(record or self.root)
        if sector in seen:
            return
        seen.add(sector)
        data = self.read(sector, size)
        at = 0
        while at < size:
            length = data[at]
            if not length:
                # Records do not cross sectors; the rest of this one is padding.
                at = (at // SECTOR + 1) * SECTOR
                continue
            entry = data[at:at + length]
            at += length
            name, first, count, directory = self.record(entry)
            if entry[33] in (0, 1) and entry[32] == 1:
                continue    # . and ..
            if directory:
                yield from self.walk(entry, seen)
            else:
                yield name, first, count


def copy_file(iso, name, sector, size, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(unpack(name, iso.read(sector, size)))


def compressed(name):
    """XP's name for a compressed file: tahoma.ttf is on the media as TAHOMA.TT_."""
    stem, extension = name.rsplit('.', 1)
    return f'{stem}.{extension[:2]}_'


def unpack(name, data):
    """A file of the media, its one-file cabinet opened if it is compressed: (a temporary copy is removed)."""
    if not name.endswith('_'):
        return data
    with tempfile.TemporaryDirectory(prefix='xp-native-') as temp:
        cabinet = Path(temp) / name
        cabinet.write_bytes(data)
        subprocess.run(['bsdtar', '-xf', str(cabinet), '-C', temp], check=True)
        unpacked = [p for p in Path(temp).iterdir() if p != cabinet]
        if len(unpacked) != 1:
            raise SystemExit(f'{name}: expected one file in the cabinet')
        return unpacked[0].read_bytes()


def wallpapers(text):
    """{name on the media: name XP shows} from SHL_IMG.INF's [Wallpaper.CopyFiles] and [Strings]."""
    strings = dict((key.strip().lower(), value.strip().strip('"'))
                   for key, value in re.findall(r'^([^;\[\n=]+)=([^\n]*)$', text, re.M))
    section = re.search(r'^\[Wallpaper\.CopyFiles\](.*?)(?=^\[|\Z)', text, re.S | re.M | re.I)
    found = {}
    for target, source in re.findall(r'^"%(\w+)%",\s*(\S+)', section.group(1) if section else '', re.M):
        if target.lower() in strings and source.lower() != 'bliss.jpg':
            found[source.lower()] = strings[target.lower()]
    return found


def sounds(text):
    """{name on the media: its name in %windir%\\Media} from LAYOUT.INF: directory 26."""
    strings, files, section = {}, {}, None
    for raw in text.splitlines():
        line = raw.split(';', 1)[0].strip()
        if line.startswith('['):
            section = line.strip('[]').lower()
        elif section == 'strings' and '=' in line:
            key, value = line.split('=', 1)
            strings[key.strip().lower()] = value.strip().strip('"')
        elif section and section.startswith('sourcedisksfiles') and '=' in line:
            name, value = (part.strip() for part in line.split('=', 1))
            fields = [f.strip() for f in value.split(',')]
            if name.lower().endswith('.wav') and len(fields) > 7 and fields[7] == '26':
                files[name.lower()] = fields[10] if len(fields) > 10 else ''
    return {name: strings.get(rename.strip('%').lower(), name) if rename.startswith('%') else (rename or name)
            for name, rename in files.items()}


def extract(source, output):
    iso = Iso9660(source)
    wanted = {}
    media = list(iso.walk())
    layout = next(((n, s, z) for n, s, z in media if n in ('layout.inf', 'layout.in_')), None)
    if layout:
        named = sounds(unpack(layout[0], iso.read(layout[1], layout[2])).decode('latin-1'))
        for name, sector, size in media:
            for stem, target in named.items():
                if name in (stem, compressed(stem)):
                    copy_file(iso, name, sector, size, output / 'media' / target)
    listing = next(((n, s, z) for n, s, z in media if n in (WALLPAPER_LIST, compressed(WALLPAPER_LIST))), None)
    if listing:
        shown = wallpapers(unpack(listing[0], iso.read(listing[1], listing[2])).decode('latin-1'))
        for name, sector, size in media:
            for stem, target in shown.items():
                if name in (stem, compressed(stem)):
                    copy_file(iso, name, sector, size, output / 'web/wallpaper' / target)
    for name, sector, size in media:
        for target in {**FILES, **ICON_FILES}:
            if name in (target, compressed(target)):
                # A plain copy over a compressed one; the setup files' first otherwise.
                if target not in wanted or name == target:
                    wanted[target] = (name, sector, size)
    missing = sorted(set(FILES) - set(wanted))
    if missing:
        raise SystemExit(f'{source} does not have {", ".join(missing)}: is it a Windows XP CD?')
    if not shutil.which('bsdtar'):
        raise SystemExit('bsdtar is needed for the compressed files: sudo pacman -S --needed libarchive')
    for target, (name, sector, size) in wanted.items():
        folder = FILES.get(target) or ICON_FILES[target]
        copy_file(iso, name, sector, size, output / folder / target)
        print(f'{folder}/{target}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('source', type=Path, help='the XP ISO')
    parser.add_argument('output', type=Path, nargs='?',
                        default=Path(__file__).resolve().parents[1] / '.local/native-assets')
    args = parser.parse_args()
    extract(args.source.resolve(strict=True), args.output.resolve())


if __name__ == '__main__':
    sys.exit(main())
