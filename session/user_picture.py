#!/usr/bin/env python3
"""XP's account picture becomes the Linux user's picture.

XP's User Accounts ("Change my picture") saves the chosen picture as
User Account Pictures\\<user>.bmp, which XP's Start menu shows. While the
session runs, each new picture also becomes the user's picture in Linux:
AccountsService's icon (the login screen, XP's Welcome screen and splash
in greeter/, KDE and GNOME) and ~/.face (older display managers).

At the first start a picture the user already has in Linux is kept; only a
change made in XP replaces it. The picture last taken is recorded in
~/.local/state/win4wine/user-picture.

The login screen runs as LightDM's user, which can't read ~/.face in a
private home: it shows AccountsService's icon. When AccountsService has none
(it came after the picture was taken, or refused it then), the session gives
it ~/.face at its start.
"""
import argparse
import os
from pathlib import Path
import pwd
import sys
import tempfile
import time

PICTURES = 'drive_c/ProgramData/Microsoft/User Account Pictures'
ICONS = Path('/var/lib/AccountsService/icons')
# XP's pictures are 48x48; Linux shows them larger.
SIZE = 96


def state_file():
    return Path(os.environ.get('XDG_STATE_HOME') or Path.home() / '.local/state') / 'win4wine/user-picture'


def signature(path):
    try:
        stat = path.stat()
    except OSError:
        return None
    return f'{stat.st_mtime_ns} {stat.st_size}'


def linux_picture(user):
    return (ICONS / user).is_file() or (Path.home() / '.face').is_file()


def png(source, target):
    """SOURCE (XP's BMP) as a SIZExSIZE PNG at TARGET; False without GdkPixbuf."""
    try:
        import gi
        gi.require_version('GdkPixbuf', '2.0')
        from gi.repository import GdkPixbuf
    except (ImportError, ValueError):
        return False
    picture = GdkPixbuf.Pixbuf.new_from_file(str(source))
    picture = picture.scale_simple(SIZE, SIZE, GdkPixbuf.InterpType.BILINEAR)
    picture.savev(str(target), 'png', [], [])
    return True


def accounts_service(user, picture):
    """AccountsService copies PICTURE as the user's icon (polkit lets a user
    of the active session change their own)."""
    try:
        import gi
        gi.require_version('Gio', '2.0')
        from gi.repository import Gio, GLib
        bus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
        path = bus.call_sync('org.freedesktop.Accounts', '/org/freedesktop/Accounts', 'org.freedesktop.Accounts',
                             'FindUserByName', GLib.Variant('(s)', (user,)), GLib.VariantType('(o)'),
                             Gio.DBusCallFlags.NONE, 10000, None).unpack()[0]
        bus.call_sync('org.freedesktop.Accounts', path, 'org.freedesktop.Accounts.User', 'SetIconFile',
                      GLib.Variant('(s)', (str(picture),)), None,
                      Gio.DBusCallFlags.ALLOW_INTERACTIVE_AUTHORIZATION, 30000, None)
        return True
    except Exception as e:  # No AccountsService, or polkit refused.
        print(f'AccountsService: {e}', file=sys.stderr)
        return False


def apply(source, user):
    with tempfile.TemporaryDirectory() as temp:
        picture = Path(temp) / 'picture.png'
        if not png(source, picture):
            picture = source
        accounts_service(user, picture)
        face = Path.home() / '.face'
        temporary = face.with_name('.face.win4wine')
        temporary.write_bytes(picture.read_bytes())
        temporary.replace(face)
    print(f'Linux picture from {source}', flush=True)


def watch(prefix, interval):
    user = pwd.getpwuid(os.getuid()).pw_name
    source = prefix / PICTURES / f'{user}.bmp'
    state = state_file()
    try:
        taken = state.read_text().strip()
    except OSError:
        taken = None
    face = Path.home() / '.face'
    if taken is not None and face.is_file() and not (ICONS / user).is_file():
        with tempfile.TemporaryDirectory() as temp:
            # AccountsService reads the file as root, but not through a symbolic link.
            picture = Path(temp) / 'picture'
            picture.write_bytes(face.read_bytes())
            if accounts_service(user, picture):
                print('Linux picture given to AccountsService', flush=True)
    while True:
        current = signature(source)
        if current and current != taken:
            # The first time, a Linux picture of the user's own stays.
            if taken is not None or not linux_picture(user):
                # User Accounts may still be writing it.
                time.sleep(1)
                if signature(source) == current:
                    apply(source, user)
            taken = current
            state.parent.mkdir(parents=True, exist_ok=True)
            state.write_text(current + '\n')
        if interval <= 0:
            return
        time.sleep(interval)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('prefix', type=Path)
    parser.add_argument('--interval', type=float, default=2, help='seconds between checks (0: check once)')
    args = parser.parse_args()
    watch(args.prefix, args.interval)
