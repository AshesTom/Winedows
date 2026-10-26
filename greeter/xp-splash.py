#!/usr/bin/env python3
"""Show XP's Welcome screen while the XP shell starts, or its log-off screen while the session ends.

The session supervisor runs this. 'logon' shows the user's picture with
"Loading your personal settings...", as XP does after a logon, and follows
the words read from stdin: 'applying' ("Applying your personal settings...")
and 'welcome' (XP's big "welcome" alone, while the shell starts), until it is
terminated. A line 'progress PERCENT STEP' adds the percentage to the line and
the step under it, once the settings have taken more than PROGRESS_AFTER
seconds. 'logoff' shows "Logging off..." and changes its line for each
word read ('logoff', 'saving', 'shutdown'). It exits with status 2 when the Welcome
screen's files are not set up (greeter/install.sh), so the session goes on
without it. The window stays above the windows mapped after it.
"""
import argparse
import os
from pathlib import Path
import pwd
import select
import signal
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))

STATUS = {'loading': 1682, 'applying': 1679, 'logoff': 1691, 'saving': 1687, 'shutdown': 1684}
# A quick logon shows XP's line alone.
PROGRESS_AFTER = 3


def user_picture(name, home):
    for path in (Path('/var/lib/AccountsService/icons') / name, Path(home) / '.face'):
        try:
            if path.is_file() and os.access(path, os.R_OK):
                return path
        except OSError:
            pass
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('logon', 'logoff'))
    parser.add_argument('--dpi', type=int, default=96, help="the XP shell's LogPixels")
    parser.add_argument('--assets', help="extract/extract.py's output, without the greeter's configuration")
    args = parser.parse_args()
    try:
        import welcome
        import xdraw
        from Xlib import X, display
        cache = Path(os.environ.get('XDG_CACHE_HOME') or Path.home() / '.cache') / 'win4wine/welcome'
        config, assets = welcome.setup(cache, args.assets)
    except Exception as e:  # Not set up: the session runs without it.
        print(f'xp-splash: {e}', file=sys.stderr)
        return 2
    entry = pwd.getpwuid(os.getuid())
    full_name = entry.pw_gecos.split(',')[0]
    picture = user_picture(entry.pw_name, entry.pw_dir) or welcome.default_picture(config, assets)
    screen = welcome.Screen(assets, [welcome.User(entry.pw_name, full_name or None, picture)], dpi=args.dpi)
    if args.mode == 'logon':
        screen.mode, screen.selected = 'logon', 0
        screen.status = assets.string('winlogon', STATUS['loading'])
    else:
        screen.mode = 'status'
        screen.status = assets.string('winlogon', STATUS['logoff'])

    d = display.Display()
    root = d.screen().root
    width, height = root.get_geometry().width, root.get_geometry().height
    window = root.create_window(0, 0, width, height, 0, d.screen().root_depth,
                                override_redirect=True, event_mask=X.ExposureMask)
    root.change_attributes(event_mask=X.SubstructureNotifyMask)

    def show():
        image = xdraw.pixmap(d, screen.render(width, height))
        window.change_attributes(background_pixmap=image)
        window.clear_area()
        image.free()
        d.flush()

    show()
    # The supervisor waits for this name before it goes on to end the session.
    window.set_wm_name('win4wine-splash')
    window.map()
    d.flush()

    def finish(*_):
        if args.mode == 'logoff':
            # Keep the last line on the screen until the display manager ends the X server.
            xdraw.set_root_background(d, screen.render(width, height))
        sys.exit(0)
    signal.signal(signal.SIGTERM, finish)
    words = sys.stdin.fileno()
    pending = b''
    started = time.monotonic()
    base, progress = screen.status, None    # progress: (percent, step) not shown yet

    def show_progress():
        nonlocal progress
        percent, step = progress
        progress = None
        screen.status, screen.detail = f'{base} {percent}%', step
        show()
    while True:
        fds = [d.fileno()] + ([words] if words is not None else [])
        wait = max(0, started + PROGRESS_AFTER - time.monotonic()) if progress else None
        ready, _, _ = select.select(fds, [], [], wait if not d.pending_events() else 0)
        if words in ready:
            chunk = os.read(words, 4096)
            if not chunk:
                words = None  # The supervisor is gone; stay until the X server ends.
            *lines, pending = (pending + chunk).split(b'\n')
            for line in lines:
                line = line.decode(errors='replace').strip()
                if line.startswith('progress '):
                    _, percent, *step = line.split(' ', 2)
                    progress = (percent, step[0] if step else '')
                    if time.monotonic() - started >= PROGRESS_AFTER:
                        show_progress()
                    continue
                for word in line.split():
                    if word == 'welcome' and args.mode == 'logon':
                        screen.mode = 'welcome'
                        show()
                    elif word in STATUS:
                        base = screen.status = assets.string('winlogon', STATUS[word])
                        screen.detail, progress = '', None
                        show()
        elif progress and time.monotonic() - started >= PROGRESS_AFTER:
            show_progress()
        while d.pending_events():
            event = d.next_event()
            # Stay above what the session maps or raises meanwhile, as XP's Welcome screen does.
            mapped = event.type == X.MapNotify and event.window.id != window.id
            raised = event.type == X.ConfigureNotify and event.window.id != window.id and \
                getattr(event.above_sibling, 'id', event.above_sibling) == window.id
            if mapped or raised:
                window.configure(stack_mode=X.Above)
                d.flush()


if __name__ == '__main__':
    sys.exit(main())
