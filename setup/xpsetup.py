"""What XP's own setup shows, read from the user's media: the text-mode screens' messages
(usetup.exe's message table), the licence (eula.txt) and the billboard of the graphical
phase (winntbbu.dll's strings and bitmaps, syssetup.dll's words). No code of them runs.

  media(ISO, FOLDER)   copies those files from the ISO into FOLDER, once
"""
from pathlib import Path
import re
import struct
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'native-shell'))
sys.path.insert(0, str(HERE.parent / 'greeter'))
import extract  # noqa: E402
from xpres import Resources  # noqa: E402

RT_STRING, RT_MESSAGETABLE = 6, 11
# Characters below 0x20 are pictures on the VGA text screen (code page 437): setupdd.sys's
# bullets are 0x07, its arrows 0x18 to 0x1b
CP437_GLYPHS = str.maketrans({0x04: '\u2666', 0x07: '\u2022', 0x10: '\u25ba', 0x11: '\u25c4',
                              0x18: '\u2191', 0x19: '\u2193', 0x1a: '\u2192', 0x1b: '\u2190'})
FILES = ('usetup.exe', 'eula.txt', 'winntbbu.dll', 'syssetup.dll')


def media(iso_path, folder):
    """FOLDER with FILES from the ISO; the largest eula.txt is XP's own (I386's)."""
    folder = Path(folder)
    if all((folder / name).is_file() for name in FILES):
        return folder
    folder.mkdir(parents=True, exist_ok=True)
    iso = extract.Iso9660(iso_path)
    found = {}
    for name, sector, size in iso.walk():
        for wanted in FILES:
            if name in (wanted, extract.compressed(wanted)) and size > found.get(wanted, (0, 0, -1))[2]:
                found[wanted] = (name, sector, size)
    missing = [name for name in FILES if name not in found]
    if missing:
        raise SystemExit(f'{iso_path} does not have {", ".join(missing)}: is it a Windows XP CD?')
    for wanted, (name, sector, size) in found.items():
        extract.copy_file(iso, name, sector, size, folder / wanted)
    return folder


def messages(path):
    """{id: text} of a PE file's message table, without the %0 that ends a line."""
    resources = Resources(path)
    out = {}
    for name in resources.names(RT_MESSAGETABLE):
        data = resources.find(RT_MESSAGETABLE, name)
        count, = struct.unpack_from('<I', data, 0)
        for block in range(count):
            low, high, offset = struct.unpack_from('<III', data, 4 + 12 * block)
            for number in range(low, high + 1):
                length, flags = struct.unpack_from('<HH', data, offset)
                text = data[offset + 4:offset + length].decode('utf-16-le' if flags & 1 else 'cp437')
                out[number] = text.rstrip('\0').replace('\r\n', '\n').translate(CP437_GLYPHS)
                offset += length
    return out


def strings(path):
    """{id: text} of a PE file's string table."""
    resources = Resources(path)
    out = {}
    for block in resources.names(RT_STRING):
        data, at = resources.find(RT_STRING, block), 0
        for i in range(16):
            length, = struct.unpack_from('<H', data, at)
            at += 2
            if length:
                out[(block - 1) * 16 + i] = data[at:at + 2 * length].decode('utf-16-le')
            at += 2 * length
    return out


def key_label(text):
    """A status bar key's label: the message without its %0 and line end."""
    return re.sub(r'%0.*', '', text, flags=re.S).strip()
