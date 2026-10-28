#!/usr/bin/env python3
"""Show Windows 7's Welcome screen while its shell starts, or its log-off or
shut-down screen while the session ends: the Windows 7 counterpart of
xp-splash.py, with its arguments and the words it reads from stdin
('logoff', 'saving', 'shutdown'; and 'preparing' after Welcome, while the
prefix is updated).

LogonUI shows these as its status (authui's UIFILE 12400, atom(Status)): the
wait animation (imageres.dll's BITMAP 5000-5004, 18 frames of 12 to 30
pixels; 20 at 96 DPI) and, 5 pixels on, the line of winlogon.exe.mui or
wininit.exe.mui ("Welcome", "Logging off...", "Shutting down...") in authui's
font 11804, centred, over
the logon screen's background with the edition's name below. The pieces come
from the logon screen's files (greeter/install.sh --win7); it exits with
status 2 when they are not set up, so the session goes on without it.
Measured on Windows 7 SP1 at 1280x800.
"""
import argparse
import os
from pathlib import Path
import select
import signal
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))

# The lines: winlogon.exe.mui's Welcome and log-off lines, then wininit.exe.mui's
# shut-down line, for a restart too (the reference machine's sequence;
# winlogon's own "Windows is shutting down..." is not the one shown).
STATUS = {'logon': ('winlogon', 1002), 'logoff': ('winlogon', 1003), 'saving': ('winlogon', 1003),
          'shutdown': ('wininit', 1002),
          # after Welcome while the shell's prefix is being updated: Windows 7's
          # line for a profile's first logon (winlogon.exe.mui 1009)
          'preparing': ('winlogon', 1009)}
# imageres.dll's wait animation: frame size -> its strip
WAIT = {12: 5000, 16: 5001, 20: 5002, 25: 5003, 30: 5004}
FRAMES = 18
FRAME_TIME = 0.05
# authui's UIFILE 12400: the status element is at least 36 high, the animation
# 20x20 with a 5 margin on its right; centred (flowlayout(0,0,2,2)). Its top
# measured from the screen's centre.
STATUS_HEIGHT, WAIT_SIZE, WAIT_MARGIN, STATUS_Y = 36, 20, 5, -66
# The animation is drawn 4 pixels left of its place in that layout (measured
# with two lines of different widths; the line is where the layout puts it).
WAIT_SHIFT = -4
FONT = 11804


class Splash:
    def __init__(self, w7logon, assets, strings, dpi):
        """STRINGS: {'winlogon': Resources, 'wininit': Resources or None}"""
        from xpres import Resources
        self.w7logon, self.assets, self.strings = w7logon, assets, strings
        self.scale = dpi / 96
        size = min(WAIT, key=lambda s: abs(s - WAIT_SIZE * self.scale))
        self.wait = w7logon.Bitmap(Resources(assets.root / 'imageres.dll').bitmap(WAIT[size]), alpha=True)
        self.wait_size = size
        self.screen = w7logon.Screen(assets)

    def line(self, mode):
        source, number = STATUS[mode]
        if not self.strings.get(source):
            source, number = 'winlogon', 1100   # its "Windows is shutting down..."
        return self.strings[source].string(number) or ''

    def render(self, width, height, mode):
        """The screen without the animation, and the animation's place."""
        cairo, Gdk, PangoCairo = self.w7logon.cairo, self.w7logon.Gdk, self.w7logon.PangoCairo
        surface = cairo.ImageSurface(cairo.FORMAT_RGB24, width, height)
        cr = cairo.Context(surface)
        Gdk.cairo_set_source_pixbuf(cr, self.assets.background(width, height), 0, 0)
        cr.paint()
        brand = self.assets.bitmap(self.w7logon.BRANDING, self.assets.basebrd)
        options = self.w7logon.OPTIONS_HEIGHT
        cr.set_source_surface(brand.surface, (width - brand.width) // 2,
                              height - options + (options - brand.height) // 2)
        cr.paint()
        text = self.line(mode)
        _, logical = self.screen.layout(cr, text, FONT).get_pixel_extents()
        margin = round(WAIT_MARGIN * self.scale)
        top = height // 2 + round(STATUS_Y * self.scale)
        box = max(round(STATUS_HEIGHT * self.scale), logical.height)
        x = (width - (self.wait_size + margin + logical.width)) // 2
        self.screen.text(cr, text, FONT, x + self.wait_size + margin, top + (box - logical.height) // 2)
        place = (x + round(WAIT_SHIFT * self.scale), top + (box - self.wait_size) // 2)
        return surface, place

    def frame(self, base, place, number):
        """The animation's frame NUMBER over BASE, as a small surface."""
        cairo = self.w7logon.cairo
        size = self.wait_size
        patch = cairo.ImageSurface(cairo.FORMAT_RGB24, size, size)
        cr = cairo.Context(patch)
        cr.set_source_surface(base, -place[0], -place[1])
        cr.paint()
        cr.set_source_surface(self.wait.surface, -number * size, 0)
        cr.rectangle(0, 0, size, size)
        cr.fill()
        return patch


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('logon', 'logoff'))
    parser.add_argument('--dpi', type=int, default=96, help="the shell's LogPixels")
    parser.add_argument('--media', help="extract/win7.py's output, when the logon screen is not installed")
    args = parser.parse_args()
    try:
        import w7logon
        import xdraw
        from xpres import Resources
        from Xlib import X, display
        cache = Path(os.environ.get('XDG_CACHE_HOME') or Path.home() / '.cache') / 'win4wine/w7logon'
        _, assets = w7logon.setup(cache, args.media)
        strings = {}
        for name in ('winlogon', 'wininit'):
            mui = sorted(assets.root.glob(f'*/{name}.exe.mui'))
            strings[name] = Resources(mui[0]) if mui else None
        if not strings['winlogon']:
            raise FileNotFoundError('winlogon.exe.mui: run setup.sh --greeter again')
        splash = Splash(w7logon, assets, strings, args.dpi)
    except Exception as e:  # Not set up: the session runs without it.
        print(f'w7-splash: {e}', file=sys.stderr)
        return 2

    d = display.Display()
    root = d.screen().root
    width, height = root.get_geometry().width, root.get_geometry().height
    window = root.create_window(0, 0, width, height, 0, d.screen().root_depth,
                                override_redirect=True, event_mask=X.ExposureMask)
    root.change_attributes(event_mask=X.SubstructureNotifyMask)
    gc = window.create_gc()
    state = {'mode': args.mode, 'frame': 0}

    def show():
        base, place = splash.render(width, height, state['mode'])
        image = xdraw.pixmap(d, base)
        window.change_attributes(background_pixmap=image)
        window.clear_area()
        image.free()
        state.update(base=base, place=place)
        animate()

    def animate():
        patch = xdraw.pixmap(d, splash.frame(state['base'], state['place'], state['frame']))
        window.copy_area(gc, patch, 0, 0, splash.wait_size, splash.wait_size, *state['place'])
        patch.free()
        d.flush()

    show()
    # The supervisor waits for this name before it goes on to end the session.
    window.set_wm_name('win4wine-splash')
    window.map()
    d.flush()

    def finish(*_):
        if state['mode'] not in ('logon', 'preparing'):
            # Keep the last line on the screen until the display manager ends the X server.
            xdraw.set_root_background(d, state['base'])
        sys.exit(0)
    signal.signal(signal.SIGTERM, finish)
    words = sys.stdin.fileno()
    next_frame = time.monotonic() + FRAME_TIME
    while True:
        fds = [d.fileno()] + ([words] if words is not None else [])
        timeout = 0 if d.pending_events() else max(0, next_frame - time.monotonic())
        ready, _, _ = select.select(fds, [], [], timeout)
        if words in ready:
            line = os.read(words, 256).decode(errors='replace').split()
            if not line:
                words = None  # The supervisor is gone; stay until the X server ends.
            for word in line:
                if word in STATUS and STATUS[word] != STATUS[state['mode']]:
                    state['mode'] = word
                    show()
        while d.pending_events():
            event = d.next_event()
            # Stay above what the session maps or raises meanwhile, as LogonUI does.
            mapped = event.type == X.MapNotify and event.window.id != window.id
            raised = event.type == X.ConfigureNotify and event.window.id != window.id and \
                getattr(event.above_sibling, 'id', event.above_sibling) == window.id
            if mapped or raised:
                window.configure(stack_mode=X.Above)
                d.flush()
            elif event.type == X.Expose and event.count == 0:
                animate()
        if time.monotonic() >= next_frame:
            state['frame'] = (state['frame'] + 1) % FRAMES
            next_frame += FRAME_TIME
            if next_frame < time.monotonic():
                next_frame = time.monotonic() + FRAME_TIME
            animate()


if __name__ == '__main__':
    sys.exit(main())
