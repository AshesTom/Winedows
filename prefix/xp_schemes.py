#!/usr/bin/env python3
"""XP's color schemes for Display Properties, made from the user's own files.

At a user's first logon XP's themeui.dll turns the classic appearance schemes
of HIVEDEF.INF (HKCU\\Control Panel\\Appearance\\Schemes, one binary value per
scheme and size) into the form its Appearance page reads: New Schemes, one key
per scheme with a key per size. Under Wine it does not do so, so this makes
the same values from the same files (the user's HIVEDEF.INF and themeui.dll):

- a scheme's name is the first of themeui's strings 850-899 that the legacy
  name starts with ("High Contrast #1" for "High Contrast #1 (large)"); the
  schemes come in the order of their first size in the INF;
- a size is normal, large or extra large by the INF's string name (_LARGE,
  _EXTRA_LARGE), named by themeui's strings 2019-2021, ordered as in the INF;
- its values are the legacy scheme's: the metrics (Size #0-#8), its six
  fonts (Font #0-#5) and 29 colors (Color #0-#28), and the menu highlight and
  menu bar colors (Color #29, #30) of the highlight and menu colors;
- Contrast is 0, or for the High Contrast schemes (themeui's names 853-864)
  1 on a dark window color and 2 on a light one; Flat Menus is 0;
- the INF names Microsoft Sans Serif for most schemes, which has no bold face
  (Wine makes the captions' bold up and blurs them); the classic theme's fonts
  read on the reference machines are the language's classic font (English XP
  Tahoma 8pt, Japanese XP MS UI Gothic 9pt, prefix/registry.py): the schemes
  name it instead, at its height for their 8pt fonts.

The Themes key's DefaultVisualStyleOn and DefaultVisualStyleOff are the
visual style the Windows XP and Windows Classic themes turn on (Luna) and the
classic scheme they turn to (the INF's current one, normal size).

  xp_schemes.py HIVEDEF.INF THEMEUI.DLL [LANGUAGE]   the .reg text, on stdout
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'xp-explorer/scripts'))
import xpreg  # noqa: E402
from registry import CLASSIC_FONTS  # noqa: E402

SCHEMES = r'Control Panel\Appearance\Schemes'
NAMES = range(850, 900)
SIZES = {'normal': 2019, 'large': 2020, 'extra large': 2021}
HIGH_CONTRAST = range(853, 865)
# The legacy SCHEMEDATA: version, NONCLIENTMETRICSW (without its own size),
# the icon title font and the colors. New Schemes put the icon title font
# (Font #3) before the status and message fonts.
LOGFONT = 92
METRICS = ((8, 0), (12, 1), (16, 2), (20, 3), (24, 4), (120, 5), (124, 6), (220, 7), (224, 8))
FONTS = (28, 128, 228, 504, 320, 412)
COLORS = 596
SANS_SERIF = ('Microsoft Sans Serif', 'MS Sans Serif')


def strings(dll):
    """The string table of a PE file: {ID: text}, in the file's own language
    where it has English beside it (XP's service pack files do)."""
    data = Path(dll).read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count, optional = struct.unpack_from('<H12xH', data, pe + 6)
    magic = struct.unpack_from('<H', data, pe + 24)[0]
    directories = pe + 24 + (96 if magic == 0x10b else 112)
    sections = [struct.unpack_from('<IIII', data, pe + 24 + optional + 40 * i + 8) for i in range(count)]

    def offset(rva):
        for size, address, raw_size, raw in sections:
            if address <= rva < address + max(size, raw_size):
                return rva - address + raw
        raise ValueError('RVA outside the image')

    base = offset(struct.unpack_from('<I', data, directories + 16)[0])

    def entries(at):
        named, ids = struct.unpack_from('<12xHH', data, at)
        for i in range(named + ids):
            name, target = struct.unpack_from('<II', data, at + 16 + 8 * i)
            yield name, target

    result = {}
    for kind, target in entries(base):
        if kind != 6 or not target & 0x80000000:
            continue
        for block, target2 in entries(base + (target & 0x7fffffff)):
            languages = dict(entries(base + (target2 & 0x7fffffff)))
            for language in sorted(languages, key=lambda language: language == 0x409)[:1]:
                rva, size = struct.unpack_from('<II', data, base + languages[language])
                at, end = offset(rva), offset(rva) + size
                for k in range(16):
                    length = struct.unpack_from('<H', data, at)[0]
                    if length:
                        result[(block - 1) * 16 + k] = data[at + 2:at + 2 + 2 * length].decode('utf-16le')
                    at += 2 + 2 * length
    return result


def legacy_schemes(inf_text):
    """[(INF string name, legacy name, binary)] in the INF's order."""
    names = {}
    section = None
    for raw in inf_text.splitlines():
        line = raw.strip()
        if line.startswith('['):
            section = line.strip('[]').lower()
        elif section == 'strings' and '=' in line and not line.startswith(';'):
            key, value = line.split('=', 1)
            names[key.strip().lower()] = value.strip().strip('"')
    result = []
    logical, current = [], ''
    for raw in inf_text.splitlines():
        line = raw.rstrip()
        if line.endswith('\\'):
            current += line[:-1]
            continue
        logical.append(current + line)
        current = ''
    for line in logical:
        fields = xpreg.fields(line)
        if len(fields) < 5 or fields[0].upper() != 'HKCU' or fields[1].replace('\\\\', '\\') != SCHEMES:
            continue
        token = fields[2].strip('%')
        data = bytes(int(value, 16) for value in fields[4:] if value)
        result.append((token, names.get(token.lower(), token), data))
    return result


def size_of(token):
    token = token.upper()
    if token.endswith('_EXTRA_LARGE'):
        return 'extra large'
    if token.endswith('_LARGE'):
        return 'large'
    return 'normal'


def quote(text):
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'


def hexes(data):
    return ','.join(f'{b:02x}' for b in data)


def classic_font(font, language):
    """A scheme's LOGFONTW with the language's classic face for Microsoft
    Sans Serif, and its height for an 8pt (-11 pixel) font."""
    face = font[28:].decode('utf-16le').split('\0', 1)[0]
    if face not in SANS_SERIF:
        return font
    name, height = CLASSIC_FONTS.get(language, CLASSIC_FONTS['en'])
    if struct.unpack_from('<i', font, 0)[0] != -11:
        height = struct.unpack_from('<i', font, 0)[0]
    return struct.pack('<i', height) + font[4:28] + name.encode('utf-16le').ljust(64, b'\0')


def contrast(display, window):
    if display not in HIGH_CONTRAST:
        return 0
    red, green, blue = window & 0xff, window >> 8 & 0xff, window >> 16 & 0xff
    return 2 if red + green + blue > 3 * 0x80 else 1


def registry(inf_text, themeui, language='en'):
    texts = strings(themeui)
    names = [(i, texts[i]) for i in NAMES if texts.get(i)]
    schemes = {}  # display ID -> [(size, legacy name, data)]
    for token, legacy, data in legacy_schemes(inf_text):
        matches = [(i, name) for i, name in names if legacy.startswith(name)]
        if not matches:
            continue
        longest = max(len(name) for _, name in matches)
        display = min(i for i, name in matches if len(name) == longest)
        schemes.setdefault(display, []).append((size_of(token), legacy, data))
    lines = ['Windows Registry Editor Version 5.00', '']
    root = r'HKEY_CURRENT_USER\Control Panel\Appearance\New Schemes'
    for number, (display, sizes) in enumerate(schemes.items()):
        lines += [f'[{root}\\{number}]', f'"DisplayName"="@themeui.dll,-{display}"', '']
        for index, (size, legacy, data) in enumerate(sizes):
            values = {}
            colors = struct.unpack_from('<29I', data, COLORS)
            for i, color in enumerate(colors + (colors[13], colors[4])):
                values[f'Color #{i}'] = f'dword:{color:08x}'
            values['Contrast'] = f'dword:{contrast(display, colors[5]):08x}'
            values['DisplayName'] = f'"@themeui.dll,-{SIZES[size]}"'
            values['Flat Menus'] = 'dword:00000000'
            for i, at in enumerate(FONTS):
                values[f'Font #{i}'] = 'hex:' + hexes(classic_font(data[at:at + LOGFONT], language))
            values['LegacyName'] = quote(legacy)
            for at, i in METRICS:
                values[f'Size #{i}'] = 'hex(b):' + hexes(struct.pack('<q', struct.unpack_from('<i', data, at)[0]))
            lines.append(f'[{root}\\{number}\\Sizes\\{index}]')
            lines += [f'{quote(name)}={value}' for name, value in sorted(values.items())]
            lines.append('')
    # The themes' visual style settings: Luna, or the INF's current classic scheme.
    current = next((value for key, value in xpreg_values(inf_text, r'Control Panel\Appearance', 'Current')), '')
    themes = r'HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes'
    style = r'%ResourceDir%\themes\Luna\Luna.msstyles'
    lines += [f'[{themes}\\DefaultVisualStyleOff]', '"InstallVisualStyle"=hex(2):00,00',
              f'"InstallVisualStyleColor"={quote(current)}', f'"InstallVisualStyleSize"={quote(texts.get(2019, "Normal"))}',
              '', f'[{themes}\\DefaultVisualStyleOn]',
              '"InstallVisualStyle"=hex(2):' + hexes((style + '\0').encode('utf-16le')),
              '"InstallVisualStyleColor"="NormalColor"', '"InstallVisualStyleSize"="NormalSize"', '']
    return '\r\n'.join(lines) + '\r\n'


def xpreg_values(inf_text, key, name):
    """(key, value) of the INF's HKCU lines that set NAME under KEY (the first)."""
    text = xpreg.hive_entries(inf_text, 'HKCU', (key,))
    found = []
    for line in text.splitlines():
        if line.startswith(quote(name) + '='):
            found.append((key, line.split('=', 1)[1].strip('"')))
    return found[:1]


if __name__ == '__main__':
    sys.stdout.write(registry(xpreg.read_inf(sys.argv[1]), sys.argv[2], *sys.argv[3:4]))
