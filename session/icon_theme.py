#!/usr/bin/env python3
"""Make a freedesktop icon theme, "Windows XP", from the icons in the user's XP files.

python3 session/icon_theme.py ASSETS THEME_DIRECTORY

The theme's folders, drives, documents, places and the commands of Explorer's
toolbar are XP's own icons, read from the media's DLLs (shell32.dll,
shimgvw.dll, wmploc.dll, zipfldr.dll, ...) that the extraction put in ASSETS,
at XP's 16, 32 and 48 pixel sizes. Like the cursor theme (greeter/cursors.py),
nothing of Microsoft's is shipped: the session makes the theme from the user's
own files (session/supervisor.py), in ~/.local/share/icons/win4wine-xp-icons,
the first time and when this script changes (VERSION). Icons that XP has not
(application icons, most status icons) come from the themes it inherits.
Control Panel's Icons item (session/linuxlook.c) chooses it for Linux
applications. Only the resources are read (greeter/xpres.py).
"""
import os
from pathlib import Path
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'greeter'))
from xpres import Resources  # noqa: E402

RT_ICON, RT_GROUP_ICON, RT_BITMAP = 3, 14, 2
NAME = 'win4wine-xp-icons'
# Bumped when the theme's icons or names change: the session makes it again.
VERSION = '1'
SIZES = (16, 24, 32, 48)
# The themes whose icons stand in for those XP has not, the first installed first.
INHERITS = ('breeze', 'Adwaita', 'hicolor')

# (file in ASSETS, icon group) -> (context, freedesktop names). The groups are
# XP SP3's resource numbers; the first group to give a name keeps it.
ICONS = {
    ('system32/shell32.dll', 1): ('mimetypes', ('unknown', 'application-x-generic', 'application-octet-stream')),
    ('system32/shell32.dll', 2): ('mimetypes', ('x-office-document', 'application-rtf', 'application-msword',
                                                'application-vnd.oasis.opendocument.text')),
    ('system32/shell32.dll', 3): ('mimetypes', ('application-x-executable', 'application-x-ms-dos-executable',
                                                'application-x-msdownload', 'application-x-desktop')),
    ('system32/shell32.dll', 4): ('places', ('folder', 'inode-directory', 'folder-saved-search', 'folder-templates',
                                             'folder-publicshare', 'folder-download', 'folder-downloads')),
    ('system32/shell32.dll', 5): ('places', ('folder-open', 'folder-drag-accept', 'folder-visiting')),
    ('system32/shell32.dll', 7): ('devices', ('media-floppy', 'drive-removable-media-floppy')),
    ('system32/shell32.dll', 8): ('devices', ('drive-removable-media', 'drive-removable-media-usb',
                                              'drive-harddisk-usb', 'media-removable')),
    ('system32/shell32.dll', 9): ('devices', ('drive-harddisk', 'drive-harddisk-system', 'drive-harddisk-solidstate',
                                              'drive-multidisk', 'drive-harddisk-root')),
    ('system32/shell32.dll', 10): ('places', ('folder-remote', 'drive-harddisk-network')),
    ('system32/shell32.dll', 12): ('devices', ('drive-optical', 'media-optical', 'media-optical-cd')),
    ('system32/shell32.dll', 222): ('devices', ('media-optical-dvd',)),
    ('system32/shell32.dll', 15): ('places', ('network-server',)),
    ('system32/shell32.dll', 16): ('devices', ('computer', 'user-desktop-computer', 'system')),
    ('system32/shell32.dll', 17): ('devices', ('printer', 'printer-network')),
    ('system32/shell32.dll', 18): ('places', ('network-workgroup', 'network', 'folder-network')),
    ('system32/shell32.dll', 22): ('categories', ('preferences-system', 'systemsettings', 'gnome-control-center',
                                                  'applications-system', 'preferences-other')),
    ('system32/shell32.dll', 23): ('actions', ('system-search', 'search')),
    ('system32/shell32.dll', 24): ('apps', ('help-browser', 'help-contents', 'help-about', 'help-faq')),
    ('system32/shell32.dll', 25): ('actions', ('system-run',)),
    ('system32/shell32.dll', 28): ('actions', ('system-shutdown', 'application-exit')),
    ('system32/shell32.dll', 32): ('places', ('user-trash', 'trashcan_empty')),
    ('system32/shell32.dll', 33): ('places', ('user-trash-full', 'trashcan_full')),
    ('system32/shell32.dll', 35): ('places', ('user-desktop', 'desktop')),
    ('system32/shell32.dll', 44): ('places', ('user-bookmarks', 'bookmarks', 'starred')),
    ('system32/shell32.dll', 45): ('actions', ('system-log-out',)),
    ('system32/shell32.dll', 47): ('places', ('start-here',)),
    ('system32/shell32.dll', 48): ('actions', ('system-lock-screen',)),
    ('system32/shell32.dll', 151): ('mimetypes', ('text-x-script', 'application-x-shellscript')),
    ('system32/shell32.dll', 152): ('mimetypes', ('text-x-generic', 'text-plain', 'text-x-log')),
    ('system32/shell32.dll', 153): ('mimetypes', ('application-x-sharedlib',)),
    ('system32/shell32.dll', 156): ('mimetypes', ('font-x-generic', 'application-x-font-ttf', 'font-ttf')),
    ('system32/shell32.dll', 237): ('places', ('folder-music', 'folder-sound')),
    ('system32/shell32.dll', 259): ('places', ('user-home', 'folder-home')),
    ('system32/shell32.dll', 268): ('apps', ('preferences-desktop-accessibility',)),
    ('system32/shell32.dll', 269): ('apps', ('system-users', 'preferences-desktop-user')),
    ('system32/shell32.dll', 270): ('apps', ('preferences-desktop-theme', 'preferences-desktop-appearance')),
    ('system32/shell32.dll', 271): ('apps', ('system-software-install', 'system-software-update')),
    ('system32/shell32.dll', 273): ('apps', ('preferences-system-network',)),
    ('system32/shell32.dll', 275): ('apps', ('utilities-system-monitor',)),
    ('system32/shell32.dll', 276): ('apps', ('preferences-system-time',)),
    ('system32/shell32.dll', 309): ('devices', ('camera-photo',)),
    ('system32/shell32.dll', 310): ('devices', ('phone',)),
    ('system32/shell32.dll', 306): ('devices', ('media-flash',)),
    ('system32/shell32.dll', 314): ('devices', ('pda',)),
    ('system32/shell32.dll', 315): ('devices', ('scanner',)),
    ('system32/shell32.dll', 317): ('devices', ('camera-video',)),
    ('system32/mydocs.dll', 100): ('places', ('folder-documents',)),
    ('system32/mydocs.dll', 101): ('places', ('folder-pictures', 'folder-images')),
    ('resources/user32.dll', 101): ('status', ('dialog-warning',)),
    ('resources/user32.dll', 102): ('status', ('dialog-question',)),
    ('resources/user32.dll', 103): ('status', ('dialog-error',)),
    ('resources/user32.dll', 104): ('status', ('dialog-information',)),
    ('shell/shimgvw.dll', 1): ('mimetypes', ('image-x-generic', 'image-png', 'image-jpeg', 'image-gif', 'image-bmp',
                                             'image-tiff', 'image-x-bmp')),
    ('wmp/wmploc.dll', 472): ('mimetypes', ('audio-x-generic', 'audio-mpeg', 'audio-x-wav', 'audio-x-vorbis+ogg',
                                            'audio-flac', 'audio-x-flac')),
    ('wmp/wmploc.dll', 473): ('mimetypes', ('video-x-generic', 'video-mp4', 'video-x-matroska', 'video-webm',
                                            'video-x-msvideo')),
    ('wmp/wmploc.dll', 102): ('apps', ('multimedia-player', 'applications-multimedia')),
    ('filetypes/zipfldr.dll', 101): ('mimetypes', ('application-zip', 'package-x-generic', 'application-x-archive',
                                                   'application-x-compressed-tar', 'application-x-7z-compressed',
                                                   'application-x-rar', 'application-x-tar')),
    ('filetypes/msiexec.exe', 1): ('mimetypes', ('application-x-msi', 'application-x-deb', 'application-x-rpm',
                                                 'application-vnd.debian.binary-package')),
    ('ie/iexplore.exe', 32528): ('apps', ('internet-web-browser', 'applications-internet', 'web-browser')),
    ('ie/iexplore.exe', 32529): ('mimetypes', ('text-html', 'application-xhtml+xml')),
    ('tray/sndvol32.exe', 300): ('apps', ('multimedia-volume-control', 'preferences-desktop-sound')),
    ('keyboard/main.cpl', 100): ('devices', ('input-mouse',)),
    ('keyboard/main.cpl', 200): ('devices', ('input-keyboard',)),
    ('tray/netshell.dll', 1): ('places', ('network-wired', 'network-connect')),
    ('cpl/desk.cpl', 100): ('devices', ('video-display', 'preferences-desktop-display')),
}
# XP's toolbar buttons, strips of 32-bit bitmaps in shell32.dll: Explorer's
# (Back, Forward, Cut, Copy, ...) and Internet Explorer's (Stop, Refresh,
# Home), {size: bitmap} -> {index in the strip: names}.
STRIPS = (
    ({16: 216, 24: 214}, {0: ('go-previous',), 1: ('go-next',), 3: ('bookmark-new',), 5: ('edit-cut',),
                          6: ('edit-copy',), 7: ('edit-paste',), 8: ('edit-undo',), 9: ('edit-redo',),
                          10: ('edit-delete', 'delete'), 11: ('document-new',), 12: ('document-open',),
                          13: ('document-save',), 14: ('document-print-preview',),
                          15: ('document-properties', 'configure'), 16: ('help-contextual', 'help-whatsthis'),
                          17: ('edit-find',), 18: ('edit-find-replace',), 19: ('document-print',),
                          28: ('go-up',), 31: ('folder-new',)}),
    ({16: 206, 24: 204}, {2: ('process-stop',), 3: ('view-refresh',), 4: ('go-home',),
                          14: ('view-fullscreen',)}),
)


def png(width, height, rows):
    """A PNG file of rows of (r, g, b, a)."""
    raw = b''.join(b'\0' + bytes(c for pixel in row for c in pixel) for row in rows)

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def decode(dib):
    """An icon image (a DIB with its AND mask): (width, height, rows of (r, g, b, a))."""
    header, width, height, _, bits, _, _, _, _, used = struct.unpack_from('<IiiHHIIiiI', dib)
    height //= 2
    colors = used or (1 << bits if bits <= 8 else 0)
    palette = [struct.unpack_from('<BBB', dib, header + 4 * i)[::-1] for i in range(colors)]
    xor = dib[header + 4 * colors:]
    xor_stride = (width * bits + 31) // 32 * 4
    and_mask = xor[xor_stride * height:]
    and_stride = (width + 31) // 32 * 4
    alpha = bits == 32 and any(xor[4 * x + 3 + y * xor_stride] for y in range(height) for x in range(width))
    rows = []
    for y in range(height):
        line = xor[(height - 1 - y) * xor_stride:]
        mask = and_mask[(height - 1 - y) * and_stride:]
        row = []
        for x in range(width):
            transparent = len(mask) > x // 8 and mask[x // 8] >> (7 - x % 8) & 1
            if bits == 32:
                b, g, r, a = line[4 * x:4 * x + 4]
                row.append((r, g, b, a) if alpha else (r, g, b, 0 if transparent else 255))
                continue
            if bits == 24:
                color = tuple(line[3 * x:3 * x + 3][::-1])
            elif bits == 16:
                value = struct.unpack_from('<H', line, 2 * x)[0]
                color = tuple((value >> shift & 31) * 255 // 31 for shift in (10, 5, 0))
            else:
                index = line[x * bits // 8] >> (8 - bits - x * bits % 8) & ((1 << bits) - 1)
                color = palette[index] if index < len(palette) else (0, 0, 0)
            row.append((0, 0, 0, 0) if transparent else color + (255,))
        rows.append(row)
    return width, height, rows


def group_images(resources, number):
    """The images of icon group NUMBER: {size: (bits, data)}, the most colours at each size."""
    group = resources.find(RT_GROUP_ICON, number)
    if group is None:
        return {}
    count = struct.unpack_from('<H', group, 4)[0]
    best = {}
    for i in range(count):
        width, height, _, _, _, bits, _, resource = struct.unpack_from('<BBBBHHIH', group, 6 + 14 * i)
        width = width or 256
        if width != (height or 256):
            continue
        data = resources.find(RT_ICON, resource)
        if data is None or data[:4] == b'\x89PNG':
            continue
        if width not in best or bits > best[width][0]:
            best[width] = (bits, data)
    return best


def icon_groups(path):
    """The numbers of the icon groups in the PE file at PATH."""
    return Resources(path).names(RT_GROUP_ICON)


def strip_icon(dib, index, size):
    """Icon INDEX of a strip of SIZE x SIZE images (a 32-bit bitmap with alpha), as rows."""
    header, width, height, _, bits = struct.unpack_from('<IiiHH', dib)
    if bits != 32 or (index + 1) * size > width or abs(height) != size:
        return None
    pixels = dib[header:]
    rows = []
    for y in range(size):
        line = (size - 1 - y if height > 0 else y) * width * 4
        rows.append([(pixels[line + 4 * x + 2], pixels[line + 4 * x + 1], pixels[line + 4 * x], pixels[line + 4 * x + 3])
                     for x in range(index * size, (index + 1) * size)])
    return rows


def write_theme(assets, directory):
    assets, directory = Path(assets), Path(directory)
    opened = {}
    contexts = set()

    def resources(file):
        if file not in opened:
            path = assets / file
            try:
                opened[file] = Resources(path) if path.is_file() else None
            except (OSError, ValueError, struct.error):
                opened[file] = None
        return opened[file]

    def put(size, context, names, width, height, rows):
        target = directory / f'{size}x{size}' / context
        target.mkdir(parents=True, exist_ok=True)
        first = target / f'{names[0]}.png'
        if first.exists() or first.is_symlink():
            return
        first.write_bytes(png(width, height, rows))
        for alias in names[1:]:
            link = target / f'{alias}.png'
            if not link.exists() and not link.is_symlink():
                link.symlink_to(first.name)
        contexts.add((size, context))

    for (file, number), (context, names) in ICONS.items():
        if (res := resources(file)) is None:
            continue
        images = group_images(res, number)
        for size in SIZES:
            if size in images:
                try:
                    put(size, context, names, *decode(images[size][1]))
                except (struct.error, IndexError):
                    continue
    if (shell32 := resources('system32/shell32.dll')) is not None:
        for bitmaps, icons in STRIPS:
            for size, number in bitmaps.items():
                dib = shell32.find(RT_BITMAP, number)
                for index, names in icons.items():
                    rows = dib and strip_icon(dib, index, size)
                    if rows:
                        put(size, 'actions', names, size, size, rows)
    folders = sorted(contexts)
    inherits = [t for t in INHERITS if any((Path(d) / 'icons' / t / 'index.theme').is_file() for d in data_dirs())]
    text = ('[Icon Theme]\nName=Windows XP\nComment=Windows XP icons, from your Windows XP files\n'
            f'Inherits={",".join(inherits or ["hicolor"])}\nExample=folder\n'
            f'X-Win4Wine-Version={VERSION}\n'
            f'Directories={",".join(f"{s}x{s}/{c}" for s, c in folders)}\n\n')
    context_names = {'actions': 'Actions', 'apps': 'Applications', 'categories': 'Categories', 'devices': 'Devices',
                     'mimetypes': 'MimeTypes', 'places': 'Places', 'status': 'Status'}
    for size, context in folders:
        text += f'[{size}x{size}/{context}]\nSize={size}\nContext={context_names[context]}\nType=Threshold\n\n'
    (directory / 'index.theme').write_text(text)


def data_dirs():
    return [os.environ.get('XDG_DATA_HOME') or str(Path.home() / '.local/share'),
            *(os.environ.get('XDG_DATA_DIRS') or '/usr/local/share:/usr/share').split(':')]


def current(directory):
    """Whether the theme in DIRECTORY was made by this VERSION."""
    try:
        return f'X-Win4Wine-Version={VERSION}\n' in (Path(directory) / 'index.theme').read_text()
    except OSError:
        return False


def ensure(assets, directory):
    """Make the theme in DIRECTORY unless it is there and current; its files go
    in a new folder first, so applications never see half of it."""
    directory = Path(directory)
    if current(directory) or not (Path(assets) / 'system32/shell32.dll').is_file():
        return False
    import shutil
    import tempfile
    directory.parent.mkdir(parents=True, exist_ok=True)
    new = Path(tempfile.mkdtemp(prefix='.' + directory.name + '-', dir=directory.parent))
    write_theme(assets, new)
    new.chmod(0o755)
    old = directory.with_name('.' + directory.name + '-old')
    shutil.rmtree(old, ignore_errors=True)
    if directory.exists():
        directory.rename(old)
    new.rename(directory)
    shutil.rmtree(old, ignore_errors=True)
    return True


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    write_theme(sys.argv[1], sys.argv[2])
