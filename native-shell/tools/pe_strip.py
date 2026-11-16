#!/usr/bin/env python3
"""A PE file reduced to its resources: what the native shell reads of XP's programs and libraries.

The shell, the greeter and the session only walk a PE file's resource directory (strings, menus,
dialogs, icons, bitmaps, the type library of wmp.dll...), through its section table. So of each
.exe, .dll or .cpl taken from the media only the headers and the sections the resources are in
are kept, at the same RVAs: the code, the data and the relocations are dropped. That is about
half of what extract.py takes, and no code of XP is left on the disk.

  pe_strip.py FILE...      strips each file in place (a file that is not a PE file is left as it is)
"""
import struct
import sys
from pathlib import Path


def strip(data):
    """DATA with only its headers and its resource sections, or DATA itself if it is not a PE
    file, has no resources, or would not be smaller."""
    if len(data) < 0x40 or data[:2] != b'MZ':
        return data
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        return data
    count, = struct.unpack_from('<H', data, pe + 6)
    optional, = struct.unpack_from('<H', data, pe + 20)
    magic, = struct.unpack_from('<H', data, pe + 24)
    directories = pe + 24 + (112 if magic == 0x20b else 96)
    rsrc_rva, rsrc_size = struct.unpack_from('<II', data, directories + 2 * 8)
    if not rsrc_rva:
        return data
    table = pe + 24 + optional
    alignment, = struct.unpack_from('<I', data, pe + 24 + 36)   # FileAlignment
    alignment = max(alignment, 0x200)
    sections = [bytearray(data[table + 40 * i:table + 40 * (i + 1)]) for i in range(count)]
    end = rsrc_rva + max(rsrc_size, 1)

    def span(header):
        vsize, address, raw_size, raw = struct.unpack_from('<IIII', header, 8)
        return address, address + max(vsize, raw_size), raw, raw_size

    kept = [s for s in sections if span(s)[0] < end and rsrc_rva < span(s)[1]]
    if not kept:
        return data
    headers_end = table + 40 * len(kept)
    out = bytearray(data[:table])
    first_raw = -(-max(headers_end, 0x400) // alignment) * alignment
    body, at = bytearray(), first_raw
    for header in kept:
        _, _, raw, raw_size = span(header)
        chunk = data[raw:raw + raw_size]
        struct.pack_into('<I', header, 20, at if raw_size else 0)   # PointerToRawData
        struct.pack_into('<I', header, 24, 0)                        # PointerToRelocations
        body += chunk + b'\0' * (-len(chunk) % alignment)
        at += len(chunk) + (-len(chunk) % alignment)
        out += header
    out += b'\0' * (first_raw - len(out))
    struct.pack_into('<H', out, pe + 6, len(kept))
    struct.pack_into('<I', out, pe + 24 + 60, first_raw)                 # SizeOfHeaders
    struct.pack_into('<I', out, pe + 24 + 64, 0)                         # CheckSum
    # The entry point and every directory but the resources' now point at nothing.
    struct.pack_into('<I', out, pe + 24 + 16, 0)                         # AddressOfEntryPoint
    for i in range(struct.unpack_from('<I', out, directories - 4)[0]):
        if i != 2:
            struct.pack_into('<II', out, directories + 8 * i, 0, 0)
    out += body
    return bytes(out) if len(out) < len(data) else data


def strip_file(path):
    path = Path(path)
    data = path.read_bytes()
    stripped = strip(data)
    if stripped is not data:
        path.write_bytes(stripped)
    return len(data) - len(stripped)


if __name__ == '__main__':
    saved = sum(strip_file(p) for p in sys.argv[1:])
    print(f'{saved / 1e6:.1f} MB saved')
