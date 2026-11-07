#!/usr/bin/env python3
"""The installer's second phase as XP's graphical setup shows it: winntbbu.dll's billboard.

Full screen: its background (bitmap 103) and the logo for the screen's width (102, 101
or 100) at (W x 0.0195, H x 0.0053); on the left the five steps (strings 23, 24, 25, 63,
64), each with its bullet (104 done, 109 current, 105 to come, on their key colour
78,111,214), done and current in Tahoma 600, the current one orange, the others in
Tahoma 400 and white; under them the time left (syssetup.dll's words), the status line
and the progress bar; on the right the billboard's panels (Professional's table at
0x59ffe490: title and text, every 36 seconds), white with a shadow of 0,37,109.
The places are sub_59ff2ec1's, from the screen's size and the steps' font
(strings 1 to 3: 9, 8 and 8 points by width); the panels' fonts are strings 4 to 20.

It runs COMMAND and follows what it prints:
  step N            the current step (0 to 4)
  progress P TEXT   the progress bar at P percent, TEXT on the status line (@N: syssetup.dll's string N)
  minutes M         the time left
Its other lines go to the log. The billboard ends with the command, and exits with its status.

  billboard.py --setup FOLDER --assets ASSETS --log FILE [--panel-seconds N] -- COMMAND...
"""
import argparse
import io
import os
from pathlib import Path
import re
import select
import subprocess
import sys
import time

import cairo
import gi
gi.require_version('GdkPixbuf', '2.0')
from gi.repository import GdkPixbuf  # noqa: E402

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'greeter'))
import welcome  # noqa: E402
import xdraw  # noqa: E402
import xpsetup  # noqa: E402
from xpres import Resources  # noqa: E402

STEPS = (23, 24, 25, 63, 64)
# Professional's panels: (title, text), in the table's order
PANELS = ((27, 28), (29, 30), (31, 32), (33, 34), (35, 36), (37, 38), (39, 40), (41, 42), (45, 46),
          (47, 48), (51, 52), (53, 54), (55, 56), (57, 58), (59, 60))
PANEL_SECONDS = 36                          # SetTimer(..., 0x8ca0)
DONE, PENDING, CURRENT, BACKGROUND = 104, 105, 109, 103
KEY = (78, 111, 214)                        # the bullets' transparent colour (0x5a001a50)
WHITE, ORANGE, SHADOW = (255, 255, 255), (255, 128, 0), (0, 37, 109)
# The progress bar (sub_59ff43df): an msctls_progress32 with PBS_SMOOTH, PBM_SETBKCOLOR white
# (0x5a001a48) and PBM_SETBARCOLOR 7,158,5 (0x5a001a4c); its subclass (sub_59ff3598+0xa6) frames
# it on WM_NCPAINT with Rectangle(), NULL_BRUSH and BLACK_PEN, and the control fills inside that
BAR_FILL, BAR_BACK, BAR_FRAME = (7, 158, 5), (255, 255, 255), (0, 0, 0)
SHADOW_OFFSET = 2                           # sub_59ff5aea: the shadow by sub_59ff5825(dc, 2, 2, ...)
# syssetup.dll's words for the time left: "...:\r\n     %d minutes", and for one minute
TIME_LEFT, ONE_MINUTE = 1179, 1180


def muldiv(a, b, c):
    return (a * b + c // 2) // c


def ftol(value):
    return int(value)                       # _ftol truncates


def sizes(W):
    """sub_59ff2bc8's table by the screen's width (0x59ffe4ac, 0x59ffe4cc, 0x59ffe4ec): the strings
    of the panels' title and text sizes in points; sub_59ff215e's string of the steps' size."""
    if W >= 1024:
        return 6, 8, 1
    return (10, 12, 2) if W == 800 else (14, 16, 3)


def geometry(W, H, f, title_pt, text_pt, steps_bottom):
    """sub_59ff2ec1: the places, from the screen's size, the steps' font height F, the
    panels' sizes in points (it adds them to pixels, as it does) and the steps' bottom
    (sub_59ff234c measures it), given STEPS_BOTTOM(left, top, right)."""
    g = {}
    band = H * 13 // 200
    g['logo'] = (ftol(W * 0.0195), ftol(H * 0.0053))
    panel = W * 28 // 100
    if W < 800:
        left = 3 * panel // 20
        top = 2 * band
        right = panel - left
        width = panel - 2 * left
    else:
        left = ftol(W * 0.039)
        top = text_pt - ftol(H * -0.059) + title_pt + band
        right = left - ftol(W * -0.2005)
        width = ftol(W * 0.1705)
    bottom = steps_bottom(left, top, right)
    g['steps'] = (left, top, right)
    if W < 800:
        time_top = int(3 * f / 2) + bottom
    else:
        time_top = bottom - ftol(H * -0.063)
    g['time'] = (left, time_top, width, 5 * f)
    g['status'] = (left, time_top + 5 * f, width, 3 * f)
    g['bar'] = (left, time_top + 5 * f + 3 * f + f // 3, width, int(3 * f / 2))
    g['band'] = band
    g['panel'] = (panel, band, W - panel, H - 2 * band)
    margin = ftol(W * 0.06)
    # its right edge is W - panel - 2 x margin within the panel
    g['text'] = (margin, top - band, W - panel - 3 * margin, H - 2 * band)
    g['title_pitch'] = f + 2 * title_pt
    g['line_pitch'] = text_pt + f
    return g


class Fonts:
    def __init__(self, assets):
        self.fonts, self.face = Path(assets) / 'fonts', 'Tahoma'


class Billboard:
    def __init__(self, setup, assets, panel_seconds=PANEL_SECONDS):
        self.panel_seconds = panel_seconds
        self.winntbbu = Resources(setup / 'winntbbu.dll')
        self.strings = xpsetup.strings(setup / 'winntbbu.dll')
        self.syssetup = xpsetup.strings(setup / 'syssetup.dll')
        self.text = welcome.Text(Fonts(assets))
        self.bitmaps = {}
        self.step, self.percent, self.status, self.minutes = 3, 0, '', None
        self.panel, self.panel_time = 0, time.monotonic()

    def string(self, number):
        return self.strings.get(number, '').replace('|', '')

    def bitmap(self, number):
        if number not in self.bitmaps:
            loader = GdkPixbuf.PixbufLoader()
            loader.write(self.winntbbu.bitmap(number))
            loader.close()
            pixbuf = loader.get_pixbuf().add_alpha(number in (DONE, PENDING, CURRENT), *KEY)
            ok, data = pixbuf.save_to_bufferv('png', [], [])
            self.bitmaps[number] = cairo.ImageSurface.create_from_png(io.BytesIO(data))
        return self.bitmaps[number]

    def lines(self, text, px, bold, family, width):
        """Words wrapped at WIDTH, as DrawText's DT_WORDBREAK does."""
        out = []
        for paragraph in text.split('\n'):
            line = ''
            for word in re.findall(r'^\s+|\S+\s*', paragraph):
                longer = line + word
                if line and self.text.size(self.text.layout(longer.rstrip(), px, bold=bold, family=family))[0] > width:
                    out.append(line.rstrip())
                    line = word
                else:
                    line = longer
            out.append(line.rstrip())
        return out

    def draw_text(self, cr, text, x, y, px, bold, family, color, shadow=None):
        block = self.text.layout(text, px, bold=bold, family=family)
        if shadow:
            self.text.draw(cr, block, x + SHADOW_OFFSET, y + SHADOW_OFFSET, shadow)
        self.text.draw(cr, block, x, y, color)
        return block

    def render(self, W, H):
        surface = cairo.ImageSurface(cairo.FORMAT_RGB24, W, H)
        cr = cairo.Context(surface)
        background = self.bitmap(BACKGROUND)
        cr.save()
        cr.scale(W / background.get_width(), H / background.get_height())
        cr.set_source_surface(background)
        cr.paint()
        cr.restore()

        title_id, text_id, step_id = sizes(W)
        title_pt, text_pt = int(self.strings.get(title_id, '24')), int(self.strings.get(text_id, '11'))
        step_pt = int(self.strings.get(step_id, '8'))
        f = muldiv(step_pt, 96, 72)
        bullet = self.bitmap(DONE)
        indent = bullet.get_width() * 3 // 2
        tm = sum(self.text.layout('x', f, family='Tahoma').win[:2])

        def steps_bottom(left, top, right):
            counts = [len(self.lines(self.string(n), f, True, 'Tahoma', right - left - indent)) for n in STEPS]
            most = max(1, *counts)
            return top + (len(STEPS) - 1) * (most + 1) * tm + max(tm * most, bullet.get_height())

        g = geometry(W, H, f, title_pt, text_pt, steps_bottom)
        logo = self.bitmap(102 if W >= 1024 else 101 if W >= 800 else 100)
        cr.set_source_surface(logo, *g['logo'])
        cr.paint()

        left, y, right = g['steps']
        most = max(1, *[len(self.lines(self.string(n), f, True, 'Tahoma', right - left - indent)) for n in STEPS])
        for i, number in enumerate(STEPS):
            done, current = i < self.step, i == self.step
            cr.set_source_surface(self.bitmap(DONE if done else CURRENT if current else PENDING), left, y)
            cr.paint()
            for row, line in enumerate(self.lines(self.string(number), f, done or current, 'Tahoma',
                                                  right - left - indent)):
                self.draw_text(cr, line, left + indent, y + row * tm, f, done or current, 'Tahoma',
                               ORANGE if current else WHITE)
            y += (most + 1) * tm

        # The time left and the status line are STATIC controls (SetTimeEstimate, SetProgressText):
        # DrawText's word wrap in their width, white (0x5a001a34), the time left in Tahoma 600
        # (0x5a00191c, WM_CTLCOLORSTATIC), the status line in Tahoma 400
        x, top, width, _ = g['time']
        if self.minutes is not None:
            left_text = self.syssetup.get(ONE_MINUTE if self.minutes == 1 else TIME_LEFT, '')
            left_text = left_text.replace('%d', str(self.minutes)).replace('\r\n', '\n')
            for row, line in enumerate(self.lines(left_text, f, True, 'Tahoma', width)):
                self.draw_text(cr, line, x, top + row * tm, f, True, 'Tahoma', WHITE)
        x, top, width, _ = g['status']
        if self.status:
            for row, line in enumerate(self.lines(self.status, f, False, 'Tahoma', width)[:2]):
                self.draw_text(cr, line, x, top + row * tm, f, False, 'Tahoma', WHITE)
        x, top, width, height = g['bar']
        cr.set_source_rgb(*(c / 255 for c in BAR_FRAME))
        cr.rectangle(x, top, width, height)
        cr.fill()
        cr.set_source_rgb(*(c / 255 for c in BAR_BACK))
        cr.rectangle(x + 1, top + 1, width - 2, height - 2)
        cr.fill()
        # PBS_SMOOTH: the inner width times the position over the range
        cr.set_source_rgb(*(c / 255 for c in BAR_FILL))
        cr.rectangle(x + 1, top + 1, (width - 2) * self.percent // 100, height - 2)
        cr.fill()

        title_id, text_id = PANELS[self.panel]
        px, py = g['panel'][0] + g['text'][0], g['band'] + g['text'][1]
        text_width = g['text'][2]
        title_px, text_px = muldiv(title_pt, 96, 72), muldiv(text_pt, 96, 72)
        for line in self.lines(self.string(title_id), title_px, True, 'Arial', text_width):
            self.draw_text(cr, line, px, py, title_px, True, 'Arial', WHITE, SHADOW)
            py += g['title_pitch']
        for paragraph in self.string(text_id).replace('\r\n', '\n').split('\n'):
            for line in self.lines(paragraph, text_px, False, 'Tahoma', text_width) if paragraph else ['']:
                if line:
                    self.draw_text(cr, line, px, py, text_px, False, 'Tahoma', WHITE, SHADOW)
                py += g['line_pitch']
        surface.flush()
        return surface

    def tick(self):
        if time.monotonic() - self.panel_time >= self.panel_seconds:
            self.panel = (self.panel + 1) % len(PANELS)
            self.panel_time = time.monotonic()
            return True
        return False

    def follow(self, line):
        parts = line.split(None, 2)
        if not parts:
            return False
        if parts[0] == 'step' and len(parts) > 1:
            self.step = int(parts[1])
        elif parts[0] == 'progress' and len(parts) > 1:
            self.percent = max(0, min(100, int(parts[1])))
            self.status = parts[2].strip() if len(parts) > 2 else ''
            if re.fullmatch(r'@\d+', self.status):
                self.status = self.syssetup.get(int(self.status[1:]), '')
        elif parts[0] == 'minutes' and len(parts) > 1:
            self.minutes = int(parts[1])
        else:
            return False
        return True


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--setup', type=Path, required=True)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--log', required=True)
    parser.add_argument('--panel-seconds', type=float, default=PANEL_SECONDS, help="each panel's time (XP's: 36)")
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    cache = Path(os.environ.get('XDG_CACHE_HOME') or Path.home() / '.cache') / 'win4wine/setup'
    welcome.use_fonts(args.assets, cache)
    board = Billboard(args.setup, args.assets, args.panel_seconds)

    from Xlib import X, Xatom, display
    d = display.Display()
    screen = d.screen()
    W, H = screen.width_in_pixels, screen.height_in_pixels
    window = screen.root.create_window(0, 0, W, H, 0, screen.root_depth, background_pixel=screen.black_pixel,
                                       event_mask=X.ExposureMask | X.StructureNotifyMask)
    window.set_wm_name('Windows Setup')
    window.set_wm_class('xp-setup', 'Win4wine-setup')
    window.change_property(d.intern_atom('_NET_WM_STATE'), Xatom.ATOM, 32, [d.intern_atom('_NET_WM_STATE_FULLSCREEN')])
    window.map()

    def show():
        image = xdraw.pixmap(d, board.render(W, H))
        window.change_attributes(background_pixmap=image)
        window.clear_area()
        image.free()
        d.flush()

    show()
    log = open(args.log, 'a')
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
    # Read as it comes: select() does not see lines a buffered reader already holds
    pending, redraw = b'', False
    while True:
        ready = select.select([process.stdout, d.fileno()], [], [], 1)[0]
        if process.stdout in ready:
            data = os.read(process.stdout.fileno(), 65536)
            if not data:
                break
            pending += data
            *lines, pending = pending.split(b'\n')
            for line in lines:
                text = line.decode(errors='replace')
                log.write(text + '\n')
                redraw |= board.follow(text)
            log.flush()
        while d.pending_events():
            event = d.next_event()
            if event.type == X.ConfigureNotify and (event.width, event.height) != (W, H):
                W, H = event.width, event.height
                redraw = True
        redraw |= board.tick()
        if redraw:
            show()
            redraw = False
    status = process.wait()
    window.destroy()
    d.flush()
    return status


if __name__ == '__main__':
    sys.exit(main())
