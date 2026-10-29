#!/usr/bin/env python3
"""Values of a Windows registry hive file (regf), read without changing it.

extract/win7.py reads the license policy of the Windows 7 image from its
SYSTEM hive (ControlSet001\\Control\\ProductOptions, ProductPolicy): what the
edition allows (Personalization, Aero's composition and transparency...).
Wine answers SLGetWindowsInformationDWORD from HKLM\\Software\\Wine\\
LicenseInformation, which the prefix gets these values in.

  hives.py HIVE
"""
import struct
import sys

BASE = 4096  # the hive's cells start after its header block


class Hive:
    def __init__(self, path):
        self.data = memoryview(open(path, 'rb').read())

    def cell(self, offset):
        return self.data[BASE + offset + 4:]

    def subkeys(self, offset):
        cell = self.cell(offset)
        kind, count = bytes(cell[:2]), struct.unpack_from('<H', cell, 2)[0]
        if kind in (b'lf', b'lh'):
            return [struct.unpack_from('<I', cell, 4 + 8 * i)[0] for i in range(count)]
        if kind == b'li':
            return [struct.unpack_from('<I', cell, 4 + 4 * i)[0] for i in range(count)]
        if kind == b'ri':
            return [key for i in range(count) for key in self.subkeys(struct.unpack_from('<I', cell, 4 + 4 * i)[0])]
        return []

    def key(self, path):
        """The key at PATH (backslashes), from the hive's root, or None."""
        offset = struct.unpack_from('<I', self.data, 0x24)[0]
        for part in path.split('\\'):
            cell = self.cell(offset)
            if not struct.unpack_from('<I', cell, 0x14)[0]:
                return None
            for child in self.subkeys(struct.unpack_from('<I', cell, 0x1c)[0]):
                node = self.cell(child)
                flags, length = struct.unpack_from('<H', node, 2)[0], struct.unpack_from('<H', node, 0x48)[0]
                name = bytes(node[0x4c:0x4c + length]).decode('latin-1' if flags & 0x20 else 'utf-16le')
                if name.lower() == part.lower():
                    offset = child
                    break
            else:
                return None
        return offset

    def value(self, key, name):
        """The data of value NAME of KEY (an offset from key()), or None."""
        cell = self.cell(key)
        count, values = struct.unpack_from('<II', cell, 0x24)
        for i in range(count):
            value = self.cell(struct.unpack_from('<I', self.cell(values), 4 * i)[0])
            length, size, offset, _, flags = struct.unpack_from('<HIIIH', value, 2)
            if bytes(value[0x14:0x14 + length]).decode('latin-1' if flags & 1 else 'utf-16le').lower() != name.lower():
                continue
            if size & 0x80000000:  # stored in the offset itself
                return struct.pack('<I', offset)[:size & 0x7fffffff]
            data = self.cell(offset)
            if bytes(data[:2]) == b'db':  # big data: a list of segments
                segments, segment_list = struct.unpack_from('<HI', data, 2)
                parts = [bytes(self.cell(struct.unpack_from('<I', self.cell(segment_list), 4 * k)[0])[:16344])
                         for k in range(segments)]
                return b''.join(parts)[:size]
            return bytes(data[:size])
        return None


def product_policy(path):
    """{name: (registry type, data)} of the license policy of the SYSTEM hive at PATH."""
    hive = Hive(path)
    key = hive.key(r'ControlSet001\Control\ProductOptions')
    blob = hive.value(key, 'ProductPolicy') if key is not None else None
    policy = {}
    if not blob:
        return policy
    size = struct.unpack_from('<I', blob, 4)[0]
    offset = 20
    while offset < 20 + size:
        entry, name_size, kind, data_size = struct.unpack_from('<HHHH', blob, offset)
        name = blob[offset + 16:offset + 16 + name_size].decode('utf-16le')
        policy[name] = (kind, blob[offset + 16 + name_size:offset + 16 + name_size + data_size])
        offset += entry
    return policy


if __name__ == '__main__':
    for name, (kind, data) in sorted(product_policy(sys.argv[1]).items()):
        print(kind, name, data.hex())
