#!/usr/bin/env python3
"""fontconfig rules that draw text as XP's font smoothing does (Display Properties > Effects).

  fontsmoothing.py none|standard|cleartype [FONTS_DIRECTORY]
  fontsmoothing.py --xp none|standard|cleartype

The first, for Linux programs:
ClearType: RGB subpixels at every size. None: whole pixels at every size.
Standard: grey only where the face's gasp table asks for it (GASP_DOGRAY), as
XP does (measured in XP SP3: Tahoma whole-pixel up to 16 px, Arial up to 17,
Trebuchet MS Bold smoothed from 12 px as in the title bars, Lucida Console,
which has no gasp table, whole-pixel at 13). XP's own faces in FONTS_DIRECTORY
get their tables' ranges; every other face, whole pixels up to 16, grey above.
Whole pixels are hinted by the font (the session runs FreeType's classic
TrueType interpreter, as Windows hints).

--xp, for XP's own programs (the shell, Notepad...): smoothing allowed at every
size, in RGB subpixels for ClearType, since they choose face by face as GDI does
(fontpolicy.h), including the fonts XP programs create with ANTIALIASED_QUALITY.
"""
from pathlib import Path
import struct
import sys
from xml.sax.saxutils import escape

WHOLE = ('<edit name="antialias" mode="assign"><bool>false</bool></edit>'
         '<edit name="hinting" mode="assign"><bool>true</bool></edit>'
         '<edit name="hintstyle" mode="assign"><const>hintfull</const></edit>'
         '<edit name="autohint" mode="assign"><bool>false</bool></edit>')
GREY = '<edit name="antialias" mode="assign"><bool>true</bool></edit>'
# Above this many pixels per em a face without a gasp table is smoothed
DEFAULT_WHOLE_UP_TO = 16


def tables(data, offset=0):
    count = struct.unpack_from('>H', data, offset + 4)[0]
    return {data[offset + 12 + 16 * i:offset + 16 + 16 * i]: struct.unpack_from('>II', data, offset + 20 + 16 * i)
            for i in range(count)}


def faces(path):
    """(family, bold, italic, gasp ranges) of each face of a .ttf or .ttc"""
    data = path.read_bytes()
    offsets = [0]
    if data[:4] == b'ttcf':
        count = struct.unpack_from('>I', data, 8)[0]
        offsets = list(struct.unpack_from(f'>{count}I', data, 12))
    for offset in offsets:
        found = tables(data, offset)
        if b'name' not in found or b'OS/2' not in found:
            continue
        family = None
        start, _ = found[b'name']
        _, count, strings = struct.unpack_from('>HHH', data, start)
        for i in range(count):
            platform, encoding, language, name, length, at = struct.unpack_from('>HHHHHH', data, start + 6 + 12 * i)
            if name == 1 and platform == 3 and language == 0x409:
                family = data[start + strings + at:start + strings + at + length].decode('utf-16-be')
                break
        os2, _ = found[b'OS/2']
        weight = struct.unpack_from('>H', data, os2 + 4)[0]
        selection = struct.unpack_from('>H', data, os2 + 62)[0]
        ranges = []
        if b'gasp' in found:
            gasp, _ = found[b'gasp']
            for i in range(struct.unpack_from('>H', data, gasp + 2)[0]):
                ranges.append(struct.unpack_from('>HH', data, gasp + 4 + 4 * i))
        if family:
            yield family, weight >= 600, bool(selection & 1), ranges


def rules(smoothing, fonts=None):
    if smoothing == 'cleartype':
        return ('  <match target="font">' + GREY + '<edit name="rgba" mode="assign"><const>rgb</const></edit>'
                '<edit name="lcdfilter" mode="assign"><const>lcddefault</const></edit></match>\n')
    if smoothing != 'standard':
        return f'  <match target="font">{WHOLE}<edit name="rgba" mode="assign"><const>none</const></edit></match>\n'
    out = [f'  <match target="font">{GREY}<edit name="rgba" mode="assign"><const>none</const></edit></match>\n',
           '  <match target="font"><test name="pixelsize" compare="less_eq">'
           f'<double>{DEFAULT_WHOLE_UP_TO}</double></test>{WHOLE}</match>\n']
    for path in sorted(Path(fonts).glob('*.tt[fc]')) if fonts else ():
        for family, bold, italic, ranges in faces(path):
            if not ranges:
                continue
            face = (f'<test name="family"><string>{escape(family)}</string></test>'
                    f'<test name="weight" compare="{"more_eq" if bold else "less"}"><const>bold</const></test>'
                    f'<test name="slant" compare="{"not_eq" if italic else "eq"}"><const>roman</const></test>')
            low = 0
            for high, flags in ranges:
                size = f'<test name="pixelsize" compare="more"><double>{low}</double></test>'
                if high < 0xffff:
                    size += f'<test name="pixelsize" compare="less_eq"><double>{high}</double></test>'
                out.append(f'  <match target="font">{face}{size}{GREY if flags & 2 else WHOLE}</match>\n')
                low = high
    return ''.join(out)


def xp_rules(smoothing):
    rgba = 'rgb' if smoothing == 'cleartype' else 'none'
    return (f'  <match target="font">{GREY}<edit name="rgba" mode="assign"><const>{rgba}</const></edit>'
            '<edit name="lcdfilter" mode="assign"><const>lcddefault</const></edit></match>\n')


if __name__ == '__main__':
    if sys.argv[1:2] == ['--xp']:
        sys.stdout.write(xp_rules(sys.argv[2] if len(sys.argv) > 2 else 'none'))
    else:
        sys.stdout.write(rules(sys.argv[1] if len(sys.argv) > 1 else 'none', sys.argv[2] if len(sys.argv) > 2 else None))
