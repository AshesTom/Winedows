#!/usr/bin/env python3
"""Windows Welcome at the XP prefix's first session: XP's intro video, then its pages over its music.

The session supervisor runs this before the XP shell starts when the prefix
has no oobe.json yet. It plays intro.wmv full screen (Esc or a click skips
it), then shows the pages greeter/oobe.py draws while title.wma plays, as
msoobe.exe did. Finish fades the music out, writes the choices to the
prefix's oobe.json and puts the first name and the Automatic Updates choice
in its registry (RegisteredOwner, AUOptions); XP's other user accounts are
not created: the session's Linux user is the account. It exits with status
2 when the prefix or extract/extract.py's 'oobe' files are missing (the
session goes on without it); then nothing is written and it runs again at
the next session.

  xp-oobe.py PREFIX [--wine WINE]
"""
import argparse
import json
import os
from pathlib import Path
import pwd
import select
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'greeter'))

INTRO_TIMEOUT = 20      # msobshel.htm's g_VideoEvent: go on if the video never ends
FADE = 2.0              # the music fades out after Finish
FRAME_RATE = 50         # the most redraws a second while the music fades


class Media:
    """intro.wmv and title.wma through GStreamer; without it, or without its
    decoders (asfdemux, the WMV and WMA decoders), there is neither."""

    def __init__(self, wake):
        self.wake, self.frame, self.lock = wake, None, threading.Lock()
        self.player = self.music = None
        try:
            import gi
            gi.require_version('Gst', '1.0')
            from gi.repository import Gst
            Gst.init(None)
            self.Gst = Gst
        except (ImportError, ValueError) as e:
            print(f'xp-oobe: no GStreamer ({e}): no video and no music', file=sys.stderr)
            self.Gst = None

    def signal(self, *_):
        try:
            os.write(self.wake, b'.')
        except OSError:
            pass

    def playbin(self, path, video):
        Gst = self.Gst
        player = Gst.ElementFactory.make('playbin')
        if player is None:
            return None
        player.set_property('uri', Gst.filename_to_uri(str(path)))
        if video:
            sink = Gst.parse_bin_from_description(
                'videoconvert ! video/x-raw,format=BGRx ! appsink name=sink emit-signals=true max-buffers=2 drop=true',
                True)
            sink.get_by_name('sink').connect('new-sample', self.sample)
            player.set_property('video-sink', sink)
        else:
            player.set_property('video-sink', Gst.ElementFactory.make('fakesink'))
        bus = player.get_bus()
        bus.enable_sync_message_emission()
        bus.connect('sync-message', self.signal)
        return player

    def sample(self, sink):
        sample = sink.emit('pull-sample')
        caps = sample.get_caps().get_structure(0)
        buffer = sample.get_buffer()
        ok, info = buffer.map(self.Gst.MapFlags.READ)
        if ok:
            with self.lock:
                self.frame = (caps.get_value('width'), caps.get_value('height'), bytes(info.data))
            buffer.unmap(info)
            self.signal()
        return self.Gst.FlowReturn.OK

    def start_intro(self, path):
        if not self.Gst or not path.is_file():
            return False
        self.player = self.playbin(path, True)
        if not self.player or self.player.set_state(self.Gst.State.PLAYING) == self.Gst.StateChangeReturn.FAILURE:
            self.player = None
            return False
        return True

    def ended(self, player):
        """Whether PLAYER reached its end or failed."""
        if not player:
            return True
        while True:
            message = player.get_bus().pop_filtered(self.Gst.MessageType.EOS | self.Gst.MessageType.ERROR)
            if message is None:
                return False
            if message.type == self.Gst.MessageType.ERROR:
                print(f'xp-oobe: {message.parse_error()[0].message}', file=sys.stderr)
            return True

    def take_frame(self):
        with self.lock:
            frame, self.frame = self.frame, None
        return frame

    def stop(self, player):
        if player:
            player.set_state(self.Gst.State.NULL)

    def start_music(self, path):
        if self.Gst and path.is_file():
            self.music = self.playbin(path, False)
            if self.music:
                self.music.set_state(self.Gst.State.PLAYING)

    def volume(self, value):
        if self.music:
            self.music.set_property('volume', max(0.0, value))


class Screen:
    """A full-screen window: the video's frames are put on it, the pages are its background."""

    def __init__(self):
        from Xlib import X, display, Xatom
        self.X = X
        self.d = display.Display()
        screen = self.d.screen()
        root = screen.root
        geometry = root.get_geometry()
        self.width, self.height = geometry.width, geometry.height
        self.window = root.create_window(
            0, 0, self.width, self.height, 0, screen.root_depth, background_pixel=screen.black_pixel,
            event_mask=(X.ExposureMask | X.KeyPressMask | X.ButtonPressMask | X.ButtonReleaseMask
                        | X.PointerMotionMask | X.StructureNotifyMask | X.FocusChangeMask))
        # A full-screen window that a compositor (labwc) manages and gives the keyboard;
        # before the X11 session's WM starts nothing manages it and it gets the focus itself.
        self.window.set_wm_name('Windows Welcome')
        self.window.set_wm_class('xp-oobe', 'Win4wine-oobe')
        self.window.change_property(self.d.intern_atom('_NET_WM_STATE'), Xatom.ATOM, 32,
                                    [self.d.intern_atom('_NET_WM_STATE_FULLSCREEN')])
        self.gc = self.window.create_gc()
        pixmap = self.window.create_pixmap(1, 1, 1)
        self.blank = pixmap.create_cursor(pixmap, (0, 0, 0), (0, 0, 0), 0, 0)
        self.window.map()
        self.d.flush()

    def wanted_size(self):
        # A compositor may give the window another size than the root's.
        geometry = self.window.get_geometry()
        return geometry.width, geometry.height

    def focus(self):
        self.window.set_input_focus(self.X.RevertToParent, self.X.CurrentTime)
        self.d.flush()

    def cursor(self, visible):
        self.window.change_attributes(cursor=self.X.NONE if visible else self.blank)
        self.d.flush()

    def video(self, frame):
        """Put a frame of the video on the screen, scaled to fit as Windows Media Player's full screen did."""
        import cairo
        width, height, data = frame
        scale = min(self.width / width, self.height / height)
        w, h = max(1, round(width * scale)), max(1, round(height * scale))
        x, y = (self.width - w) // 2, (self.height - h) // 2
        source = cairo.ImageSurface.create_for_data(bytearray(data), cairo.FORMAT_RGB24, width, height, width * 4)
        target = cairo.ImageSurface(cairo.FORMAT_RGB24, w, h)
        cr = cairo.Context(target)
        cr.scale(w / width, h / height)
        cr.set_source_surface(source)
        cr.get_source().set_filter(cairo.FILTER_BILINEAR)
        cr.paint()
        target.flush()
        self.put(target, x, y)

    def put(self, surface, x, y):
        data, stride = bytes(surface.get_data()), surface.get_stride()
        width, height = surface.get_width(), surface.get_height()
        depth = self.d.screen().root_depth
        rows = max(1, 200000 // stride)
        for top in range(0, height, rows):
            count = min(rows, height - top)
            self.window.put_image(self.gc, x, y + top, width, count, self.X.ZPixmap, depth, 0,
                                  data[top * stride:(top + count) * stride])
        self.d.flush()

    def page(self, surface):
        import xdraw
        image = xdraw.pixmap(self.d, surface)
        self.window.change_attributes(background_pixmap=image)
        self.window.clear_area()
        image.free()
        self.d.flush()


KEY_NAMES = {}


def key_text(d, event):
    """A key press as greeter/oobe.py's Wizard.key() takes it: the keysym's name, the
    character it types (from the core keyboard mapping), Alt, Shift and Control."""
    from Xlib import X, XK
    if not KEY_NAMES:
        XK.load_keysym_group('xkb')
        KEY_NAMES.update((value, name[3:]) for name, value in vars(XK).items() if name.startswith('XK_'))
    shift = bool(event.state & X.ShiftMask)
    keysym = d.keycode_to_keysym(event.detail, 1 if shift else 0) or d.keycode_to_keysym(event.detail, 0)
    name = KEY_NAMES.get(keysym, '')
    char = ''
    if 0x20 <= keysym <= 0x7e or 0xa0 <= keysym <= 0xff:
        char = chr(keysym)
    elif keysym & 0xff000000 == 0x01000000:
        char = chr(keysym & 0xffffff)
    elif keysym == XK.XK_KP_Space:
        char = ' '
    elif XK.XK_KP_0 <= keysym <= XK.XK_KP_9:
        char = chr(ord('0') + keysym - XK.XK_KP_0)
    if char and event.state & X.LockMask and char.isalpha():
        char = char.lower() if shift else char.upper()
    return name, char, bool(event.state & X.Mod1Mask), shift, bool(event.state & X.ControlMask)


def user_name():
    """The name XP's Identities page suggests: the Linux user's full name, or the login name."""
    entry = pwd.getpwuid(os.getuid())
    return entry.pw_gecos.split(',')[0].strip() or entry.pw_name


def save(prefix, wizard):
    """oobe.json: the session runs Windows Welcome again only without it."""
    record = dict(time=time.time(), automatic_updates=wizard.automatic_updates == 1, users=wizard.names)
    target = Path(prefix) / 'oobe.json'
    temporary = target.with_name('oobe.json.tmp')
    temporary.write_text(json.dumps(record, indent=2, ensure_ascii=False) + '\n')
    temporary.replace(target)


def apply(prefix, wine, wizard):
    """The first name as the registered owner (winver, System Properties) and the
    Automatic Updates choice (4: automatic, 1: off), in the 32-bit view the XP
    shell reads. Returns the regedit process, None without Wine."""
    if not wine:
        return None
    nt = r'HKEY_LOCAL_MACHINE\Software\Microsoft\Windows NT\CurrentVersion'
    au = r'HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update'
    lines = ['Windows Registry Editor Version 5.00', '']
    if wizard.names:
        owner = wizard.names[0].replace('\\', '\\\\').replace('"', '\\"')
        lines += [f'[{nt}]', f'"RegisteredOwner"="{owner}"', '']
    lines += [f'[{au}]', f'"AUOptions"=dword:{4 if wizard.automatic_updates == 1 else 1:08x}', '']
    descriptor, name = tempfile.mkstemp(prefix='oobe-', suffix='.reg')
    os.close(descriptor)
    reg = Path(name)
    reg.write_text('\r\n'.join(lines), encoding='utf-16')
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all')
    env.pop('WINEARCH', None)
    process = subprocess.Popen([wine, r'C:\windows\syswow64\regedit.exe', '/S',
                                'Z:' + str(reg).replace('/', '\\')], env=env,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    process.reg = reg
    return process


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('prefix', type=Path)
    parser.add_argument('--wine')
    args = parser.parse_args()
    try:
        config = json.loads((args.prefix / 'xp-shell.json').read_text())
        assets = Path(config['assets'])
        import oobe
        missing = oobe.ready(assets)
        if missing:
            raise FileNotFoundError('missing ' + ', '.join(f'oobe/{name}' for name in missing))
        import welcome
        cache = Path(os.environ.get('XDG_CACHE_HOME') or Path.home() / '.cache') / 'win4wine/oobe'
        welcome.use_fonts(assets, cache)
        source = oobe.Source(assets, config.get('language', 'ja'))
        wizard = oobe.Wizard(source, user_name())
    except Exception as e:  # Not set up: the session runs without it.
        print(f'xp-oobe: {e}', file=sys.stderr)
        return 2

    wake_read, wake_write = os.pipe2(os.O_NONBLOCK | os.O_CLOEXEC)
    media = Media(wake_write)
    screen = Screen()
    X = screen.X

    # The intro, as GoNavigate(CKPT_ANIMATION) plays it: full screen, no pointer.
    intro = media.start_intro(assets / 'oobe/intro.wmv')
    screen.cursor(not intro)
    started = time.monotonic()
    state = 'intro' if intro else 'pages'
    fade = None
    regedit = None
    size = (screen.width, screen.height)

    def show_page():
        screen.page(wizard.render(*size))

    if state == 'pages':
        media.start_music(assets / 'oobe/title.wma')
        show_page()
    while True:
        timeout = None
        if state == 'intro':
            timeout = max(0.0, INTRO_TIMEOUT - (time.monotonic() - started))
        elif state == 'fade':
            timeout = 1 / FRAME_RATE
        if not screen.d.pending_events():
            select.select([screen.d.fileno(), wake_read], [], [], timeout)
        try:
            os.read(wake_read, 4096)
        except BlockingIOError:
            pass
        redraw = False
        skip = False
        while screen.d.pending_events():
            event = screen.d.next_event()
            if event.type == X.MapNotify and event.window.id == screen.window.id:
                screen.focus()
            elif event.type == X.ConfigureNotify and event.window.id == screen.window.id:
                if (event.width, event.height) != size and state != 'intro':
                    size = (event.width, event.height)
                    redraw = True
                size = (event.width, event.height)
                screen.width, screen.height = size
            elif state == 'intro':
                if event.type == X.ButtonPress or (event.type == X.KeyPress and
                                                   key_text(screen.d, event)[0] in ('Escape', 'Return', 'space')):
                    skip = True
            elif state == 'pages':
                if event.type == X.MotionNotify:
                    redraw |= wizard.motion(event.event_x, event.event_y)
                elif event.type == X.ButtonPress and event.detail == 1:
                    redraw |= wizard.press(event.event_x, event.event_y)
                elif event.type == X.ButtonRelease and event.detail == 1:
                    redraw |= wizard.release(event.event_x, event.event_y)
                elif event.type == X.KeyPress:
                    redraw |= wizard.key(*key_text(screen.d, event))
        if state == 'intro':
            frame = media.take_frame()
            if frame:
                screen.video(frame)
            if skip or media.ended(media.player) or time.monotonic() - started > INTRO_TIMEOUT:
                media.stop(media.player)
                state = 'pages'
                screen.cursor(True)
                # InitApplication's PlayBackgroundMusic, heard from the Welcome page on.
                media.start_music(assets / 'oobe/title.wma')
                redraw = True
        elif state == 'pages' and wizard.done:
            save(args.prefix, wizard)
            regedit = apply(args.prefix, args.wine, wizard)
            state, fade = 'fade', time.monotonic()
        if state == 'fade':
            progress = (time.monotonic() - fade) / FADE
            media.volume(1 - progress)
            if progress >= 1:
                break
        if redraw and state in ('pages', 'fade'):
            show_page()
    media.stop(media.music)
    if regedit:
        try:
            regedit.wait(timeout=60)
        except subprocess.TimeoutExpired:
            regedit.kill()
        regedit.reg.unlink(missing_ok=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
