#!/usr/bin/env python3
"""Check session/linux_apps.py: desktop entries to XP Start menu shortcuts.

Without --wine, checks the parts that need no Wine on self-made desktop
entries and a self-made icon: which entries are shown, names, folders, the
command line and the .ico file. With --wine, also writes the shortcuts into a
new, discarded prefix on an owned Xvfb: a second pass changes nothing, a
shortcut started through Wine's start.exe runs its Linux command (without the
session's WINEDEBUG), and --watch follows an added and a removed entry,
removing only its own shortcuts and the folders it made, as --remove does.
Build first: bridge-wm/build.sh (linux-launch.exe, linux-links.exe). The
entries are named in Japanese: the ja_JP.UTF-8 locale must exist (locale -a).
"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'session'))
import linux_apps  # noqa: E402


def png(size, rgba):
    """A plain PNG of one colour, so that no image is copied from anywhere."""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + bytes(rgba) * size for _ in range(size))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def entry(directory, name, text):
    path = directory / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('[Desktop Entry]\nType=Application\n' + text)
    return path


def fixture(base, marker):
    home, system = base / 'home', base / 'system'
    apps, user_apps = system / 'applications', home / 'applications'
    entry(apps, 'editor.desktop', 'Name=Editor\nName[ja]=エディター\nComment=Edit text\n'
          'Exec=true %F\nIcon=w4w-test\nCategories=Utility;TextEditor;\nStartupWMClass=EditorWindow\n')
    entry(apps, 'browser.desktop', 'Name=Browser\nExec=true --name=%c %U\nCategories=Network;WebBrowser;\n')
    entry(apps, 'top.desktop', 'Name=Top\nExec=true\nTerminal=true\nCategories=System;Monitor;\n')
    entry(apps, 'plain.desktop', 'Name=Plain: one\nExec=true\n')
    entry(apps, 'twin.desktop', 'Name=Plain: one\nExec=true\n')
    entry(apps, 'hidden.desktop', 'Name=Hidden\nExec=true\nNoDisplay=true\n')
    entry(apps, 'other-desktop.desktop', 'Name=Other\nExec=true\nOnlyShowIn=GNOME;\n')
    entry(apps, 'missing.desktop', 'Name=Missing\nExec=true\nTryExec=w4w-no-such-program\n')
    entry(apps, 'masked.desktop', 'Name=Masked\nExec=true\n')
    entry(apps, 'wine/Programs/windows-app.desktop', 'Name=Windows app\nExec=true\n')
    entry(apps, 'kde/sub.desktop', 'Name=Sub\nExec=true\nCategories=Game;\n')
    entry(apps, 'launch.desktop', 'Name=Launch\nCategories=Utility;\n'
          f'Exec=sh -c "echo \\\\${{WINEDEBUG-unset}} > {marker}"\n')
    entry(user_apps, 'masked.desktop', 'Name=Masked\nExec=true\nHidden=true\n')
    icons = system / 'icons/hicolor'
    (icons / '48x48/apps').mkdir(parents=True)
    (icons / '48x48/apps/w4w-test.png').write_bytes(png(48, (200, 40, 30, 255)))
    (icons / 'index.theme').write_text('[Icon Theme]\nName=Hicolor\nDirectories=48x48/apps\n\n'
                                       '[48x48/apps]\nSize=48\nType=Fixed\n')
    return home, system


def check_offline(base):
    marker = base / 'marker'
    home, system = fixture(base, marker)
    # gdk-pixbuf recognizes image types through the MIME database of the data
    # directories, and with glycin finds its image loaders there too; give it
    # the system's, without the system's applications.
    mime = base / 'mime-only'
    mime.mkdir()
    for name in ('mime', 'glycin-loaders'):
        if Path('/usr/share', name).is_dir():
            (mime / name).symlink_to(Path('/usr/share', name))
    os.environ.update(XDG_DATA_HOME=str(home), XDG_DATA_DIRS=f'{system}:{mime}', LANG='ja_JP.UTF-8',
                      HOME=str(base / 'user'), XDG_CURRENT_DESKTOP='')
    os.environ.pop('LC_ALL', None)
    os.environ.pop('LC_MESSAGES', None)
    menu = linux_apps.Menu.__new__(linux_apps.Menu)
    menu.japanese, menu.flat = True, True
    # XP Explorer's (--flat): the category folders in All Programs itself
    flat = {
        'editor.desktop': 'アクセサリ\\エディター.lnk',
        'browser.desktop': 'インターネット\\Browser.lnk',
        'top.desktop': 'アクセサリ\\システム ツール\\Top.lnk',
        'plain.desktop': 'Plain_ one.lnk',
        'twin.desktop': 'Plain_ one (2).lnk',
        'kde-sub.desktop': 'ゲーム\\Sub.lnk',
        'launch.desktop': 'アクセサリ\\Launch.lnk',
    }
    links = {k: v['link'] for k, v in menu.wanted(linux_apps.entries()).items()}
    assert links == flat, links
    # The session's: all of them in the Linux Applications folder
    menu.flat = False
    wanted = menu.wanted(linux_apps.entries())
    links = {k: v['link'] for k, v in wanted.items()}
    assert links == {k: 'Linux アプリケーション\\' + v for k, v in flat.items()}, links
    terminal = 'x-terminal-emulator' if linux_apps.shutil.which('x-terminal-emulator') else 'xterm'
    assert wanted['editor.desktop']['arguments'] == 'true', wanted['editor.desktop']
    assert wanted['editor.desktop']['description'] == 'Edit text'
    assert wanted['browser.desktop']['arguments'] == 'true --name=Browser', wanted['browser.desktop']
    assert wanted['top.desktop']['arguments'] == f'{terminal} -e true', wanted['top.desktop']
    assert wanted['launch.desktop']['arguments'] == \
        f'sh -c "echo ${{WINEDEBUG-unset}} > {marker}"', wanted['launch.desktop']
    assert linux_apps.split_exec('a "b c" "d\\"e" f\\g') == ['a', 'b c', 'd"e', 'f\\g']
    if linux_apps.pixbuf_module():
        icon = base / 'test.ico'
        assert linux_apps.make_icon('w4w-test', icon)
        data = icon.read_bytes()
        reserved, kind, count = struct.unpack_from('<HHH', data)
        assert (reserved, kind, count) == (0, 1, 3), (reserved, kind, count)
        sizes = [struct.unpack_from('<BB', data, 6 + 16 * i) for i in range(count)]
        assert sizes == [(16, 16), (32, 32), (48, 48)], sizes
        offset = struct.unpack_from('<I', data, 6 + 16 * 2 + 12)[0]
        assert data[offset + 40:offset + 44] == bytes((30, 40, 200, 255)), data[offset + 40:offset + 44]
        assert not linux_apps.make_icon('w4w-no-such-icon', base / 'none.ico')
    else:
        print('linux-apps: GdkPixbuf missing; icon checks skipped')
    print('linux-apps: entries, names, folders, commands and icons OK')
    return system, marker


def wait_for(condition, what, timeout=30):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        if condition():
            return
        time.sleep(0.2)
    raise AssertionError(f'Timed out waiting for {what}')


def check_wine(base, wine, system, marker):
    prefix = base / 'prefix'
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEARCH='win64', WINEDEBUG='-all',
               WINEDLLOVERRIDES='mscoree,mshtml=')
    read_fd, write_fd = os.pipe()
    with (base / 'xvfb.log').open('w') as log:
        xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen', '0', '1024x768x24',
                                 '-nolisten', 'tcp'], pass_fds=(write_fd,), stdout=log, stderr=log)
    os.close(write_fd)
    watcher = None
    try:
        with os.fdopen(read_fd) as number:
            env['DISPLAY'] = ':' + number.readline().strip()
        subprocess.run([str(wine), 'wineboot', '-i'], env=env, check=True, timeout=300,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        env.pop('WINEARCH')
        (prefix / 'xp-shell.json').write_text('{}\n')
        programs = prefix / linux_apps.PROGRAMS
        linux = programs / linux_apps.LINUX_FOLDER[0]
        mine = linux / 'アクセサリ' / 'mine.txt'
        os.environ.update(WINEPREFIX=str(prefix), WINEDEBUG='-all', DISPLAY=env['DISPLAY'])
        menu = linux_apps.Menu(prefix, str(wine))
        before = set(programs.rglob('*'))
        assert menu.sync() == (7, 0)
        links = sorted(str(p.relative_to(linux)) for p in programs.rglob('*.lnk'))
        assert len(links) == 7 and 'アクセサリ/エディター.lnk' in links, links
        assert (prefix / linux_apps.STATE / 'icons/editor.desktop.ico').is_file()
        # The applications of Linux windows for the proxy (Windows 7's jump lists and pinning)
        rows = {line.split('\t')[0]: line.split('\t')[1:] for line in
                (prefix / linux_apps.STATE / 'relaunch.tsv').read_text(encoding='utf-8').splitlines()}
        launch = 'C:\\xp\\linux-apps\\linux-launch.exe'
        assert rows['editorwindow'] == ['エディター', f'{launch} true',
                                        'C:\\xp\\linux-apps\\icons\\editor.desktop.ico,0'], rows
        assert rows['editor'] == rows['editorwindow'], rows
        assert rows['browser'] == ['Browser', f'{launch} true --name=Browser', ''], rows
        assert 'hidden' not in rows and 'missing' not in rows, rows
        assert menu.sync() == (0, 0)
        # A shortcut opened through the Windows shell runs its Linux command.
        subprocess.run([str(wine), 'start', 'C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\'
                        f'{linux.name}\\アクセサリ\\Launch.lnk'], env=env, check=True, timeout=60)
        wait_for(marker.is_file, 'the launched Linux command')
        assert marker.read_text().strip() == 'unset', marker.read_text()
        print('linux-apps: shortcuts written once; a shortcut starts its Linux program')
        # A file of the user's own in a folder made here keeps that folder.
        mine.write_text('kept\n')
        watcher = subprocess.Popen([sys.executable, str(ROOT / 'session/linux_apps.py'), str(prefix),
                                    '--wine', str(wine), '--watch'], env=env)
        entry(system / 'applications', 'added.desktop', 'Name=Added\nExec=true\nCategories=Office;\n')
        wait_for((linux / 'オフィス' / 'Added.lnk').is_file, 'the added entry')
        for name in ('added.desktop', 'kde/sub.desktop', 'editor.desktop', 'launch.desktop'):
            (system / 'applications' / name).unlink()
        wait_for(lambda: not (linux / 'オフィス').exists() and not (linux / 'ゲーム').exists(),
                 'removed entries and their folders')
        assert mine.is_file() and not (linux / 'アクセサリ' / 'エディター.lnk').exists()
        assert sorted(json.loads((prefix / linux_apps.STATE / 'state.json').read_text())['links']) == \
            ['browser.desktop', 'plain.desktop', 'top.desktop', 'twin.desktop']
        print('linux-apps: --watch follows added and removed entries')
        watcher.terminate()
        watcher.wait(timeout=10)
        watcher = None
        assert menu.sync(remove=True) == (0, 4)
        left = sorted(str(p.relative_to(programs)) for p in set(programs.rglob('*')) - before)
        assert left == [linux.name, f'{linux.name}/アクセサリ', f'{linux.name}/アクセサリ/mine.txt'], left
        print('linux-apps: --remove leaves only what it did not make')
    finally:
        if watcher:
            watcher.terminate()
            watcher.wait(timeout=10)
        subprocess.run([str(wine.parent / 'wineserver'), '-k'], env=env, timeout=30, check=False)
        xvfb.terminate()
        xvfb.wait(timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--wine', type=Path, help='the patched Wine, to also write and start shortcuts')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='linux-apps-', dir=ROOT / '.local') as directory:
        base = Path(directory).resolve()
        system, marker = check_offline(base)
        if args.wine:
            check_wine(base, args.wine.resolve(), system, marker)
    print('linux-apps: PASS')


if __name__ == '__main__':
    main()
