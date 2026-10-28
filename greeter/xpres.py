"""Read bitmap, string and dialog resources from a PE file (no code is read or run).

The greeter draws the Welcome screen from the user's own XP files at run
time; this module only walks the documented resource directory. The session
(icons, the command prompt) and the prefix's setup (XP's language, string
tables and self-registration) read XP's files through it too.
"""
from pathlib import Path
import struct

RT_BITMAP, RT_DIALOG, RT_STRING = 2, 5, 6


class Resources:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        pe = struct.unpack_from('<I', self.data, 0x3c)[0]
        if self.data[pe:pe + 4] != b'PE\0\0':
            raise ValueError(f'{path}: not a PE file')
        sections, optional = struct.unpack_from('<H12xH', self.data, pe + 6)
        magic = struct.unpack_from('<H', self.data, pe + 24)[0]
        directories = pe + 24 + (96 if magic == 0x10b else 112)
        self.rva = struct.unpack_from('<I', self.data, directories + 2 * 8)[0]
        self.sections = []
        table = pe + 24 + optional
        for i in range(sections):
            size, address, raw_size, raw = struct.unpack_from('<IIII', self.data, table + 40 * i + 8)
            self.sections.append((address, max(size, raw_size), raw))
        # None for a file without resources: it has no resource of any type.
        self.base = self.offset(self.rva) if self.rva else None

    def offset(self, rva):
        for address, size, raw in self.sections:
            if address <= rva < address + size:
                return rva - address + raw
        raise ValueError(f'RVA {rva:#x} outside the sections')

    def entries(self, offset):
        """(name or ID, target) of the directory at OFFSET, named entries first."""
        if offset is None:
            return
        named, numbered = struct.unpack_from('<12xHH', self.data, offset)
        for i in range(named + numbered):
            name, target = struct.unpack_from('<II', self.data, offset + 16 + 8 * i)
            yield name, target

    def name(self, entry):
        if not entry & 0x80000000:
            return entry
        offset = self.base + (entry & 0x7fffffff)
        length = struct.unpack_from('<H', self.data, offset)[0]
        return self.data[offset + 2:offset + 2 + 2 * length].decode('utf-16-le')

    def find(self, rtype, name, language=None):
        """The data of resource TYPE/NAME (numbers or names) in LANGUAGE (a LANGID)
        if it has it, else in its first language; or None."""
        level = self.base
        if level is None:
            return None
        for key in (rtype, name, None):
            entries = list(self.entries(level))
            if key is None and language is not None:
                chosen = [(entry, target) for entry, target in entries if self.name(entry) == language]
                entries = chosen + entries
            for entry, target in entries:
                if key is None or self.name(entry) == key:
                    break
            else:
                return None
            level = self.base + (target & 0x7fffffff)
            if not target & 0x80000000:
                rva, size = struct.unpack_from('<II', self.data, level)
                start = self.offset(rva)
                return self.data[start:start + size]
        return None

    def languages(self, rtype, name=None):
        """The LANGIDs of the resources of type RTYPE (only of the one named
        NAME, a number or a name, when given), in the order of the directory."""
        found = []
        for entry, target in self.entries(self.base):
            if self.name(entry) != rtype or not target & 0x80000000:
                continue
            for resource, names in self.entries(self.base + (target & 0x7fffffff)):
                if names & 0x80000000 and (name is None or self.name(resource) == name):
                    found += [self.name(lang) for lang, _ in self.entries(self.base + (names & 0x7fffffff))]
        return found

    def names(self, rtype):
        """The names of the resources of type RTYPE (numbers, or strings for
        named ones, which come first), in the order of the directory."""
        for entry, target in self.entries(self.base):
            if self.name(entry) == rtype and target & 0x80000000:
                return [self.name(name) for name, _ in self.entries(self.base + (target & 0x7fffffff))]
        return []

    def bitmap(self, name):
        """A BITMAP resource as the bytes of a .bmp file."""
        dib = self.find(RT_BITMAP, name)
        if dib is None:
            raise KeyError(f'bitmap {name}')
        header, _, _, _, bits, compression, _, _, _, used = struct.unpack_from('<IiiHHIIiiI', dib)
        colors = used or (1 << bits if bits <= 8 else 0)
        masks = 12 if compression == 3 and header == 40 else 0
        offset = 14 + header + masks + 4 * colors
        return b'BM' + struct.pack('<IHHI', 14 + len(dib), 0, 0, offset) + dib

    def string(self, number):
        block = self.find(RT_STRING, number // 16 + 1)
        if block is None:
            return None
        position = 0
        for _ in range(number % 16):
            position += 2 + 2 * struct.unpack_from('<H', block, position)[0]
        length = struct.unpack_from('<H', block, position)[0]
        return block[position + 2:position + 2 + 2 * length].decode('utf-16-le') or None

    def dialog(self, name, language=None):
        """An extended dialog template (DLGTEMPLATEEX): its caption, font and
        controls, each (class, text, id, x, y, width, height, style), in dialog units."""
        data = self.find(RT_DIALOG, name, language)
        if data is None or struct.unpack_from('<HH', data)[1] != 0xffff:
            raise KeyError(f'extended dialog {name}')

        def text(position):
            """A sz_Or_Ord field: (string or number, next position)."""
            first = struct.unpack_from('<H', data, position)[0]
            if first == 0xffff:
                return struct.unpack_from('<H', data, position + 2)[0], position + 4
            end = position
            while struct.unpack_from('<H', data, end)[0]:
                end += 2
            return data[position:end].decode('utf-16-le'), end + 2

        def align(position):
            return (position + 3) & ~3

        count, _, _, dialog_width, dialog_height = struct.unpack_from('<HhhhH', data, 16)
        style = struct.unpack_from('<I', data, 12)[0]
        _, position = text(26)                  # menu
        _, position = text(position)            # class
        caption, position = text(position)
        font = None
        if style & 0x40 or style & 0x48:        # DS_SETFONT / DS_SHELLFONT
            size, weight, italic, charset = struct.unpack_from('<HHBB', data, position)
            face, position = text(position + 6)
            font = (face, size, weight)
        controls = []
        for _ in range(count):
            position = align(position)
            _, _, style, x, y, width, height, ident = struct.unpack_from('<IIIhhhhI', data, position)
            kind, position = text(position + 24)
            label, position = text(position)
            extra = struct.unpack_from('<H', data, position)[0]
            position += 2 + extra
            kind = {0x80: 'Button', 0x81: 'Edit', 0x82: 'Static', 0x83: 'ListBox',
                    0x84: 'ScrollBar', 0x85: 'ComboBox'}.get(kind, kind)
            controls.append((kind, label, ident, x, y, width, height, style))
        return {'caption': caption, 'font': font, 'size': (dialog_width, dialog_height), 'controls': controls}
