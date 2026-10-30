#!/usr/bin/env python3
"""Check native-shell/extract.py's ISO 9660 walk on a small self-made image:
files in nested directories, entries crossing into the next sector, the
compressed names, and a medium that lacks a file. No XP file is used."""
from pathlib import Path
import struct
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'native-shell'))
import extract  # noqa: E402

SECTOR = extract.SECTOR


def record(name, sector, size, directory=False):
    data = struct.pack('<BBII', 0, 0, sector, 0)[:10] + struct.pack('<II', size, 0) + bytes(7)
    data = bytes([0, 0]) + struct.pack('<I', sector) + bytes(4) + struct.pack('<I', size) + bytes(4) + bytes(7)
    data += bytes([2 if directory else 0, 0, 0]) + bytes(4) + bytes([len(name)]) + name
    if len(data) % 2:
        data += b'\0'
    return bytes([len(data)]) + data[1:]


def image(path, names):
    """A root directory, a subdirectory over two sectors holding NAMES, and their contents."""
    entries = [(name, 40 + i, f'contents of {name}'.encode()) for i, name in enumerate(names)]
    listing = [record(b'\0', 21, SECTOR * 2, True), record(b'\1', 20, SECTOR, True)]
    listing += [record(name.upper().encode() + b';1', sector, len(data)) for name, sector, data in entries]
    # Records do not cross sectors: pad as a real image does.
    directory, used = b'', 0
    for item in listing:
        if used + len(item) > SECTOR:
            directory += bytes(SECTOR - used)
            used = 0
        directory += item
        used += len(item)
    directory = directory.ljust(SECTOR * 2, b'\0')
    root = (record(b'\0', 20, SECTOR, True) + record(b'\1', 20, SECTOR, True)
            + record(b'I386', 21, SECTOR * 2, True)).ljust(SECTOR, b'\0')
    volume = bytearray(SECTOR)
    volume[:6] = b'\x01CD001'
    volume[156:156 + 34] = record(b'\0', 20, SECTOR, True)[:34]
    disk = bytearray(SECTOR * (40 + len(names)))
    disk[16 * SECTOR:17 * SECTOR] = volume
    disk[20 * SECTOR:21 * SECTOR] = root
    disk[21 * SECTOR:23 * SECTOR] = directory
    for _, sector, data in entries:
        disk[sector * SECTOR:sector * SECTOR + len(data)] = data
    Path(path).write_bytes(disk)


def main():
    with tempfile.TemporaryDirectory() as temp:
        iso = Path(temp) / 'test.iso'
        # Enough long names for the directory to take its second sector.
        names = [f'file{i:05}.txt' for i in range(80)] + ['tahoma.tt_', 'bliss.jpg']
        image(iso, names)
        found = {name: (sector, size) for name, sector, size in extract.Iso9660(iso).walk()}
        assert set(found) == set(names), sorted(set(names) ^ set(found))
        sector, size = found['bliss.jpg']
        assert extract.Iso9660(iso).read(sector, size) == b'contents of bliss.jpg'
        assert extract.compressed('tahoma.ttf') == 'tahoma.tt_'
        assert extract.compressed('luna.msstyles') == 'luna.ms_'
        inf = ('[Wallpaper.CopyFiles]\n"%MOON_JPG_NAME%",moon.jpg\ndefault.jpg,bliss.jpg\n"%AZUL_JPG_NAME%",azul.jpg\n\n'
               '[Strings]\nMOON_JPG_NAME = "Moon flower.jpg"\nAZUL_JPG_NAME = "Azul.jpg"\n')
        assert extract.wallpapers(inf) == {'moon.jpg': 'Moon flower.jpg', 'azul.jpg': 'Azul.jpg'}, extract.wallpapers(inf)
        try:
            extract.extract(iso, Path(temp) / 'out')
        except SystemExit as e:
            assert 'luna.msstyles' in str(e), e
        else:
            raise AssertionError('a medium without the files must be refused')
        try:
            (Path(temp) / 'not.iso').write_bytes(bytes(SECTOR * 20))
            extract.Iso9660(Path(temp) / 'not.iso')
        except SystemExit:
            pass
        else:
            raise AssertionError('a file that is no ISO must be refused')
    print('ok')


if __name__ == '__main__':
    main()
