#!/usr/bin/env python3
"""The graphical phase's work: the native session copied to PREFIX, then made a session.

It prints the billboard's lines (setup/billboard.py): the step (3 Installing Windows,
4 Finalizing installation), the progress with syssetup.dll's words for each part
(@205 Installing Components, @1164 Installing Start menu items, @1165 Registering
components, @1168 Saving settings) and the minutes left.

PREFIX holds the checkout's layout: native-shell/session.sh finds the programs in
PREFIX/.local/native-shell, XP's files in PREFIX/.local/native-assets and the rest
beside it, as in the checkout. --system adds the session to the display manager's
list (/usr/share/xsessions, with sudo); --greeter makes XP's Welcome screen LightDM's
greeter (greeter/install.sh).

  install_files.py --prefix PREFIX [--system] [--greeter]
  install_files.py --demo           the same lines, over about DEMO_SECONDS, with nothing copied
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
# What the session needs from the checkout (its sources stay behind)
TREE = ('bridge-wm/wm.py', 'bridge-wm/tray.py', 'bridge-wm/hotkeys.py', 'bridge-wm/icons.py',
        'greeter', 'native-shell/session.sh', 'native-shell/extract.py', 'native-shell/tools',
        'native-shell/xp-cmd.bashrc', '.local/native-shell', '.local/native-assets', '.local/qt-style')
SESSION = '''[Desktop Entry]
Type=Application
Name=Windows XP (native)
Comment=The XP shell as Linux programs, without Wine
Exec={prefix}/native-shell/session.sh
DesktopNames=XP-Shell
'''


DEMO_SECONDS = 40


def demo():
    """What the billboard is told during an install, without the install."""
    say('step 3')
    count = sum(1 for _ in files(TREE))
    for percent in range(0, 71, 2):
        say(f'minutes {max(1, (70 - percent) // 20 + 1)}')
        say(f'progress {percent} {"@54" if percent < 30 else "@205"}')
        time.sleep(DEMO_SECONDS * 0.7 / 36)
    for line in ('progress 72 @1164', 'minutes 1', 'step 4', 'progress 80 @1165', 'progress 88 @1165',
                 'progress 96 @1168', 'progress 100 @1168'):
        say(line)
        time.sleep(DEMO_SECONDS * 0.3 / 7)
    say(f'(demo: {count} files would have been copied)')
    return 0


def say(line):
    print(line, flush=True)


def files(tree):
    for name in tree:
        source = ROOT / name
        if source.is_dir():
            for path in sorted(source.rglob('*')):
                if path.is_file() and '__pycache__' not in path.parts:
                    yield path
        elif source.is_file():
            yield source


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--prefix', type=Path)
    parser.add_argument('--demo', action='store_true')
    parser.add_argument('--system', action='store_true')
    parser.add_argument('--greeter', action='store_true')
    args = parser.parse_args()
    if args.demo:
        return demo()
    if not args.prefix:
        parser.error('--prefix is needed')
    prefix = args.prefix.expanduser().resolve()
    if not (ROOT / '.local/native-shell/bin/explorer').is_file():
        sys.exit('Build it first: native-shell/build.sh')
    say('step 3')
    all_files = list(files(TREE))
    started = time.monotonic()
    for i, source in enumerate(all_files):
        relative = source.relative_to(ROOT)
        target = prefix / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        if i % 25 == 0:
            percent = 70 * i // max(1, len(all_files))
            # The time left from the pace so far, as syssetup's estimate is, rounded up to a minute
            elapsed = time.monotonic() - started
            left = elapsed / max(i, 1) * (len(all_files) - i) + 20
            say(f'minutes {max(1, int(left // 60) + 1)}')
            say(f'progress {percent} @205')
    say('progress 72 @1164')
    # XP's icons as an icon theme, made once (the session would make it at its first start)
    icons = Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share') / 'icons/win4wine-xp-icons'
    subprocess.run([sys.executable, '-c', 'import sys; sys.path.insert(0, sys.argv[1]); import icon_theme; '
                    'icon_theme.ensure(sys.argv[2], sys.argv[3])', str(prefix / 'native-shell/tools'),
                    str(prefix / '.local/native-assets'), str(icons)], check=False)
    say('minutes 1')
    say('step 4')
    say('progress 80 @1165')
    if args.system:
        with tempfile.NamedTemporaryFile('w', suffix='.desktop', delete=False) as entry:
            entry.write(SESSION.format(prefix=prefix))
        subprocess.run(['sudo', '-n', 'install', '-Dm644', entry.name, '/usr/share/xsessions/xp-native.desktop'],
                       check=True)
        os.unlink(entry.name)
        say('/usr/share/xsessions/xp-native.desktop')
    if args.greeter:
        say('progress 88 @1165')
        subprocess.run(['sudo', '-n', 'true'], check=True)
        subprocess.run([str(prefix / 'greeter/install.sh'), str(prefix / '.local/native-assets'), 'xp-native'],
                       check=True)
    say('progress 96 @1168')
    marker = prefix / '.local/installed'
    marker.write_text(time.strftime('%Y-%m-%d %H:%M:%S\n'))
    say('progress 100 @1168')
    time.sleep(1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
