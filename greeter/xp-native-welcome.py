#!/usr/bin/env python3
"""XP's Welcome screen for the native session in a window (native-shell/session.sh).

This is not a login: the user is logged in already, on the desktop around the
window, so no password is asked and nothing is authenticated. It draws XP's
screen (greeter/welcome.py, from the user's own XP files), and a click on the
account enters the session. A whole session's real login is LightDM's, with
greeter/xp-greeter.py.

Exit status: 0 the account was chosen, 3 Turn Off, 4 Restart, 2 the Welcome
screen's files are not there, so the session goes on without it.
"""
import argparse
import os
from pathlib import Path
import pwd
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

STATUS_LOADING = 1682
# How long "Loading your personal settings..." shows, in milliseconds
LOADING = 1200


def play(assets, name):
    """A sound of XP's, if the media gave it and the system can play it."""
    path = next(Path(assets).glob(f'media/{name}'), None)
    player = shutil.which('pw-play') or shutil.which('paplay') or shutil.which('aplay')
    if path and player:
        subprocess.Popen([player, str(path)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', required=True)
    parser.add_argument('--startup', action='store_true', help="play XP's startup sound")
    args = parser.parse_args()
    try:
        import welcome
        cache = Path(os.environ.get('XDG_CACHE_HOME') or Path.home() / '.cache') / 'win4wine/welcome'
        config, assets = welcome.setup(cache, args.assets)
        import gi
        gi.require_version('Gtk', '3.0')
        from gi.repository import GLib, Gtk
    except Exception as e:
        print(f'xp-native-welcome: {e}', file=sys.stderr)
        return 2
    entry = pwd.getpwuid(os.getuid())
    home = Path(entry.pw_dir)
    face = home / '.face'
    picture = face if face.is_file() else welcome.default_picture(config, assets)
    screen = welcome.Screen(assets, [welcome.User(entry.pw_name, entry.pw_gecos.split(',')[0] or None, picture)])
    screen.prompt = False
    result = {'status': 1}

    # Above the window manager's windows: there are none yet, and nothing frames this one.
    window = Gtk.Window(type=Gtk.WindowType.POPUP)
    area = Gtk.DrawingArea()
    window.add(area)
    geometry = window.get_screen().get_root_window().get_geometry()
    width, height = geometry[2], geometry[3]
    window.resize(width, height)
    window.move(0, 0)

    def draw(_, cr):
        cr.set_source_surface(screen.render(width, height), 0, 0)
        cr.paint()
        return True

    def finish(status):
        result['status'] = status
        Gtk.main_quit()

    def enter(index):
        screen.mode, screen.selected, screen.hover = 'logon', index, None
        screen.status = assets.string('winlogon', STATUS_LOADING)
        area.queue_draw()
        GLib.timeout_add(LOADING, finish, 0)

    def target(x, y):
        if screen.dialog:
            rects = screen.dialog_rects(width, height)
            for name in ('off', 'restart', 'cancel'):
                bx, by, bw, bh = rects[name]
                if bx <= x < bx + bw and by <= y < by + bh:
                    return ('dialog', name)
            return None
        if screen.mode != 'list':
            return None
        px, py, pw, ph = screen.power_rect(width, height)
        if px <= x < px + pw and py <= y < py + ph:
            return ('power',)
        index = screen.item_at(width, height, x, y)
        return ('user', index) if index is not None else None

    def motion(_, event):
        hit = target(event.x, event.y)
        hover = hit[1] if hit and hit[0] == 'user' else None
        dialog_hover = hit[1] if hit and hit[0] == 'dialog' else None
        power = hit == ('power',)
        if (hover, dialog_hover, power) != (screen.hover, screen.dialog_hover, screen.power_hover):
            screen.hover, screen.dialog_hover, screen.power_hover = hover, dialog_hover, power
            area.queue_draw()
        return True

    def release(_, event):
        if event.button != 1:
            return True
        hit = target(event.x, event.y)
        if hit == ('power',):
            screen.dialog = True
        elif hit and hit[0] == 'user':
            enter(hit[1])
        elif hit == ('dialog', 'cancel'):
            screen.dialog = False
        elif hit == ('dialog', 'off'):
            finish(3)
        elif hit == ('dialog', 'restart'):
            finish(4)
        area.queue_draw()
        return True

    area.connect('draw', draw)
    window.add_events(0)
    from gi.repository import Gdk
    area.add_events(Gdk.EventMask.POINTER_MOTION_MASK | Gdk.EventMask.BUTTON_PRESS_MASK | Gdk.EventMask.BUTTON_RELEASE_MASK)
    area.connect('motion-notify-event', motion)
    area.connect('button-release-event', release)
    window.show_all()
    if args.startup:
        play(args.assets, 'Windows XP Startup.wav')
    Gtk.main()
    return result['status']


if __name__ == '__main__':
    sys.exit(main())
