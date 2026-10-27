#!/usr/bin/env python3
"""Small single-screen X11 WM and file IPC endpoint for the Win32 shell bridge."""
import argparse
import ctypes
import fcntl
import os
from pathlib import Path
import select
import signal
import struct
from Xlib import X, XK, Xatom, Xutil, display, error, protocol
from Xlib.ext import composite, xtest
from hotkeys import Hotkeys
from icons import payload as icon_payload

IN_CLOSE_WRITE, IN_MOVED_TO = 0x8, 0x80
# Keys besides letters, digits and F1-F24 that XP's hotkeys use with the Windows key.
VIRTUAL_KEYS = {XK.XK_Tab: 0x09, XK.XK_Pause: 0x13, XK.XK_Break: 0x13, XK.XK_space: 0x20,
                XK.XK_Escape: 0x1b, XK.XK_Return: 0x0d,
                XK.XK_Left: 0x25, XK.XK_Up: 0x26, XK.XK_Right: 0x27, XK.XK_Down: 0x28}


class Inotify:
    """Wake for the proxy's command and frame files in the IPC directory."""

    def __init__(self, directory):
        libc = ctypes.CDLL(None, use_errno=True)
        self.fd = libc.inotify_init1(os.O_NONBLOCK | os.O_CLOEXEC)
        if self.fd < 0 or libc.inotify_add_watch(self.fd, os.fsencode(directory),
                                                 IN_CLOSE_WRITE | IN_MOVED_TO) < 0:
            raise OSError(ctypes.get_errno(), 'inotify')

    def names(self):
        names = []
        while True:
            try:
                data = os.read(self.fd, 65536)
            except BlockingIOError:
                return names
            offset = 0
            while offset + 16 <= len(data):
                length = struct.unpack_from('iIII', data, offset)[3]
                names.append(data[offset+16:offset+16+length].rstrip(b'\0').decode('utf-8', 'replace'))
                offset += 16 + length


class Manager:
    def __init__(self, ipc, redirect=True):
        self.ipc = ipc.resolve()
        self.ipc.mkdir(mode=0o700, parents=True, exist_ok=True)
        stat = self.ipc.stat()
        if stat.st_uid != os.getuid() or stat.st_mode & 0o077:
            raise SystemExit('IPC directory must be owned by this user, mode 0700')
        self.d = display.Display()
        self.root = self.d.screen().root
        self.atoms = {}
        self.clients = {}
        self.active = 0
        self.drag = None
        self.running = True
        self.previous_snapshot = None
        self.previous_geometry = None
        self.previous_icons = {}
        self.frames = {}
        self.cascade_slot = 0
        self.bad_access = False
        def onerror(err, request):
            if isinstance(err, error.BadAccess):
                self.bad_access = True
        self.d.set_error_handler(onerror)
        self.root.change_attributes(event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask |
                                    X.PropertyChangeMask | X.StructureNotifyMask)
        self.d.sync()
        if self.bad_access:
            raise SystemExit('Another WM already owns this X display')
        # Wine's windows keep their contents off screen (Composite), and the
        # X server puts them on screen itself: a window moved or closed then
        # uncovers them without asking their programs to paint again. Wine
        # fills an exposed area black until its process gets to it, which
        # showed while windows were dragged. Native clients paint their own
        # exposures, and they and their frames change size often; a
        # redirected window's every resize costs a new off-screen image,
        # which made resizing a native client several times slower in a VM.
        # Their frames are redirected only while they move (frame_moves).
        self.redirect = redirect and self.d.has_extension('Composite')
        self.previous_classes = None
        self.previous_owners = None
        self.overlays = {}  # override-redirect shell windows kept above the taskbar and Start menu:
                            # the Start button, the user picture and notification balloons/tooltips
                            # (unmanaged, so every restack would otherwise bury them under the taskbar)
        # Windows without a cursor of their own, such as the menus of Linux
        # applications while they hold the pointer, show the root's: an
        # arrow, as window managers set, instead of the X server's cross.
        font = self.d.open_font('cursor')
        self.root.change_attributes(cursor=font.create_glyph_cursor(font, 68, 69, (0, 0, 0),
                                                                    (0xffff, 0xffff, 0xffff)))
        font.close()
        self.support = self.root.create_window(-1, -1, 1, 1, 0, X.CopyFromParent)
        self.setprop(self.root, '_NET_SUPPORTING_WM_CHECK', Xatom.WINDOW, [self.support.id])
        self.setprop(self.support, '_NET_SUPPORTING_WM_CHECK', Xatom.WINDOW, [self.support.id])
        self.support.change_property(self.atom('_NET_WM_NAME'), self.atom('UTF8_STRING'), 8, b'win4wine')
        supported = ['_NET_SUPPORTED', '_NET_SUPPORTING_WM_CHECK', '_NET_CLIENT_LIST',
                     '_NET_CLIENT_LIST_STACKING', '_NET_ACTIVE_WINDOW', '_NET_WORKAREA',
                     '_NET_NUMBER_OF_DESKTOPS', '_NET_CURRENT_DESKTOP', '_NET_WM_STATE',
                     '_NET_WM_STATE_HIDDEN', '_NET_WM_STATE_MAXIMIZED_VERT', '_NET_WM_STATE_MAXIMIZED_HORZ',
                     '_NET_WM_STATE_ABOVE', '_NET_WM_STATE_FULLSCREEN',
                     '_NET_WM_WINDOW_TYPE', '_NET_WM_WINDOW_TYPE_DESKTOP', '_NET_WM_WINDOW_TYPE_DOCK',
                     '_NET_WM_ICON', '_NET_FRAME_EXTENTS', '_NET_CLOSE_WINDOW', '_NET_WM_STRUT', '_NET_WM_STRUT_PARTIAL']
        self.setprop(self.root, '_NET_SUPPORTED', Xatom.ATOM, [self.atom(a) for a in supported])
        self.setprop(self.root, '_NET_NUMBER_OF_DESKTOPS', Xatom.CARDINAL, [1])
        self.setprop(self.root, '_NET_CURRENT_DESKTOP', Xatom.CARDINAL, [0])
        self.own_xsettings()
        self.tab = self.d.keysym_to_keycode(XK.string_to_keysym('Tab'))
        self.escape = self.d.keysym_to_keycode(XK.string_to_keysym('Escape'))
        self.alt_codes = {code for code in self.d.get_modifier_mapping()[X.Mod1MapIndex] if code}
        self.switch = None
        self.switch_session = 0
        for modifiers in (0, X.LockMask, X.Mod2Mask, X.LockMask | X.Mod2Mask):
            self.root.grab_key(self.tab, X.Mod1Mask | modifiers, True, X.GrabModeAsync, X.GrabModeAsync)
            self.root.grab_key(self.tab, X.Mod1Mask | X.ShiftMask | modifiers, True, X.GrabModeAsync, X.GrabModeAsync)
            for button in (1, 3):
                self.root.grab_button(button, X.Mod1Mask | modifiers, False,
                                      X.ButtonPressMask | X.ButtonReleaseMask | X.PointerMotionMask,
                                      X.GrabModeAsync, X.GrabModeAsync, X.NONE, X.NONE)
            self.root.grab_key(self.d.keysym_to_keycode(XK.string_to_keysym('F4')), X.Mod1Mask | modifiers,
                               True, X.GrabModeAsync, X.GrabModeAsync)
        # The Windows key with XP's hotkeys (Win+R, Win+E, ...) and Ctrl+Esc
        # belong to the shell even while a native client has the keyboard; see shell_key.
        self.super_codes = {self.d.keysym_to_keycode(XK.string_to_keysym(name)) for name in ('Super_L', 'Super_R')}
        self.super_codes.discard(0)
        self.shell_keys = 0
        self.shell_combo = None  # keys pressed with the Windows key under a native client
        for code in self.super_codes:
            self.root.grab_key(code, X.AnyModifier, True, X.GrabModeAsync, X.GrabModeSync)
        for modifiers in (0, X.LockMask, X.Mod2Mask, X.LockMask | X.Mod2Mask):
            self.root.grab_key(self.escape, X.ControlMask | modifiers, True, X.GrabModeAsync, X.GrabModeSync)
        # The media keys and the user's shortcuts (hotkeys.py)
        self.hotkeys = Hotkeys(self.d, self.root)
        self.shell_combo_used = False
        for w in self.root.query_tree().children:
            if w.get_attributes().map_state == X.IsViewable:
                self.manage(w)
        self.workarea()

    def own_xsettings(self):
        """Hide GTK header-bar window buttons; the Win32 frame already has them.

        Only claims an unowned XSETTINGS selection, so an existing settings
        daemon keeps control. GTK falls back to its defaults for other keys.
        """
        selection = self.atom('_XSETTINGS_S%d' % self.d.get_default_screen())
        if self.d.get_selection_owner(selection) != X.NONE:
            return
        def pad(data):
            return data + b'\0' * (-len(data) % 4)
        name, value = b'Gtk/DecorationLayout', b':'
        entry = struct.pack('<BBH', 1, 0, len(name)) + pad(name) + struct.pack('<II', 0, len(value)) + pad(value)
        settings = struct.pack('<B3xII', 0, 0, 1) + entry
        kind = self.atom('_XSETTINGS_SETTINGS')
        self.support.change_property(kind, kind, 8, settings)
        self.support.set_selection_owner(selection, X.CurrentTime)
        if self.d.get_selection_owner(selection) != self.support:
            return
        self.root.send_event(protocol.event.ClientMessage(
            window=self.root, client_type=self.atom('MANAGER'),
            data=(32, [X.CurrentTime, selection, self.support.id, 0, 0])), event_mask=X.StructureNotifyMask)
        self.d.flush()

    def atom(self, name):
        if name not in self.atoms:
            self.atoms[name] = self.d.intern_atom(name)
        return self.atoms[name]

    def prop(self, w, name):
        p = w.get_full_property(self.atom(name), X.AnyPropertyType)
        return p.value if p else []

    def setprop(self, w, name, kind, data):
        w.change_property(self.atom(name), kind, 32, data)

    def kind(self, w):
        cls = bytes(self.prop(w, '_WIN4WINE_CLASS')).decode('ascii', 'replace').strip('\0').lower()
        types = self.prop(w, '_NET_WM_WINDOW_TYPE')
        if cls == 'dv2controlhost':
            return 'popup'
        if cls == 'win4wineproxy':
            return 'proxy'
        # A top-level WorkerW is not the desktop: XP's Show Desktop moves the
        # desktop's icons into one and raises it over the minimized windows,
        # just below the taskbar (measured on the reference XP machine).
        if cls == 'progman' or self.atom('_NET_WM_WINDOW_TYPE_DESKTOP') in types:
            return 'desktop'
        if cls == 'shell_traywnd' or self.atom('_NET_WM_WINDOW_TYPE_DOCK') in types:
            return 'dock'
        return 'normal'

    def overlay_window(self, w):
        """Remember an override-redirect shell window shown above the taskbar.

        Notification balloons and tooltips are unmanaged (Wine maps them
        override-redirect), so each restack raising the taskbar Above would
        bury them: their stem then disappears under the taskbar. Like the
        Start button and the user picture, they are kept above instead, as
        topmost windows are on XP. Returns whether it was remembered."""
        if w.id in self.overlays:
            return True
        try:
            cls = bytes(self.prop(w, '_WIN4WINE_CLASS')).strip(b'\0').lower()
        except error.XError:
            return False
        if cls in (b'win4winestartbutton', b'desktop user picture', b'tooltips_class32'):
            self.overlays[w.id] = w
            return True
        return False

    def manage(self, w):
        attrs = w.get_attributes()
        if attrs.override_redirect or w.id == self.support.id:
            w.map()
            if attrs.override_redirect and attrs.map_state == X.IsViewable and self.overlay_window(w):
                # Shown before this WM started: raise it above the taskbar now;
                # a window mapping later is caught by MapNotify instead.
                self.restack()
            return
        if w.id not in self.clients:
            r = w.get_geometry()
            self.clients[w.id] = {'w': w, 'hidden': False, 'kind': self.kind(w), 'restore': None, 'size': None,
                                  'geometry': (r.x, r.y, r.width, r.height, r.border_width),
                                  'framed': False, 'icon_dirty': True, 'extents': None}
            w.change_attributes(event_mask=X.PropertyChangeMask | X.FocusChangeMask)
            # Left and right clicks activate a window, as on XP (a right click
            # on the desktop opens its menu, which then needs the keyboard).
            for button in (1, 3):
                w.grab_button(button, 0, True, X.ButtonPressMask, X.GrabModeSync, X.GrabModeAsync, X.NONE, X.NONE)
            self.refresh(self.clients[w.id])
        client = self.clients[w.id]
        client['kind'] = self.kind(w)
        self.update_redirect(client)
        if client['kind'] == 'proxy':
            if w.id not in self.frames:
                w.unmap()
            return
        client['hidden'] = False
        if client['kind'] == 'normal' and not client['hwnd'] and self.fullscreen(client):
            self.fill_screen(client)
        elif (client['kind'] == 'normal' and client['owner'] and not client['hwnd'] and not client.get('placed')
              and not self.framed_owned(client)):
            # An owned Linux window has no frame; keep it inside the work area
            # (GTK's file chooser asks for a fixed size, larger than small screens),
            # centred on its owner unless it placed itself (GTK 4 does not).
            client['placed'] = True
            x, y, width, height = client['geometry'][:4]
            width, height = self.fit(client, 0, 0)
            owner = self.clients.get(client['owner'])
            if owner and not self.positioned(client):
                ox, oy, owidth, oheight = owner['geometry'][:4]
                x, y = ox + (owidth-width) // 2, oy + (oheight-height) // 2
            ax, ay, awidth, aheight = self.area
            x, y = max(ax, min(x, ax+awidth-width)), max(ay, min(y, ay+aheight-height))
            if (x, y, width, height) != client['geometry'][:4]:
                self.configure(client, x=x, y=y, width=width, height=height)
        self.setprop(w, 'WM_STATE', self.atom('WM_STATE'), [1, 0])
        w.map()
        if client['kind'] in ('normal', 'popup'):
            self.focus(w)
        self.restack()
        self.workarea()

    def update_redirect(self, c):
        # Frames only while they move (frame_moves)
        wanted = self.redirect and (c['kind'] == 'proxy' and c.get('moving', False) or
                                    bool(c['hwnd']) and c['kind'] != 'proxy')
        if wanted != c.get('redirected', False):
            c['redirected'] = wanted
            (composite.redirect_window if wanted else composite.unredirect_window)(c['w'], composite.RedirectAutomatic)

    def refresh(self, c, atom=None):
        """Cache the client properties that every bridge pass reads.

        Right after a native window moves, the X server is busy repainting
        what it uncovered, and each round trip waits behind that work. The
        caches follow PropertyNotify; geometry follows ConfigureNotify.
        """
        w = c['w']
        if atom in (None, self.atom('_WIN4WINE_HWND')):
            hwnd = self.prop(w, '_WIN4WINE_HWND')
            c['hwnd'] = int(hwnd[0]) if len(hwnd) else 0
        if atom in (None, self.atom('_NET_WM_NAME'), self.atom('WM_NAME')):
            title = self.prop(w, '_NET_WM_NAME')
            name = bytes(title).decode('utf-8', 'replace') if len(title) else (w.get_wm_name() or '')
            c['title'] = name.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ')[:500]
        if atom in (None, Xatom.WM_CLASS):
            # The application's class: Windows 7's taskbar groups a Linux
            # application's windows by it (classes.tsv, proxy).
            try:
                names = w.get_wm_class()
            except error.XError:
                names = None
            c['class'] = (names[1] if names and len(names) > 1 else '').replace('\t', ' ').replace('\n', ' ')[:100]
        if atom in (None, self.atom('_NET_WM_STATE')):
            c['states'] = set(self.prop(w, '_NET_WM_STATE'))
        if atom in (None, self.atom('WM_TRANSIENT_FOR')):
            try:
                owner = w.get_wm_transient_for()
            except error.XError:
                owner = None
            c['owner'] = owner.id if owner else 0
        if atom in (None, self.atom('_MOTIF_WM_HINTS'), self.atom('_NET_WM_WINDOW_TYPE')):
            c['undecorated'] = self.undecorated(w)

    def undecorated(self, w):
        """Whether a window asks for no frame: _MOTIF_WM_HINTS without
        decorations (client-side decorations, splash screens), or a type that
        XP gives no caption (splash, notification, menus, tooltips)."""
        hints = self.prop(w, '_MOTIF_WM_HINTS')
        if len(hints) >= 3 and hints[0] & 2 and not hints[2]:
            return True
        bare = {self.atom(name) for name in (
            '_NET_WM_WINDOW_TYPE_SPLASH', '_NET_WM_WINDOW_TYPE_NOTIFICATION', '_NET_WM_WINDOW_TYPE_TOOLTIP',
            '_NET_WM_WINDOW_TYPE_DND', '_NET_WM_WINDOW_TYPE_COMBO', '_NET_WM_WINDOW_TYPE_POPUP_MENU',
            '_NET_WM_WINDOW_TYPE_DROPDOWN_MENU', '_NET_WM_WINDOW_TYPE_MENU')}
        return any(t in bare for t in self.prop(w, '_NET_WM_WINDOW_TYPE'))

    def framed_owned(self, c):
        """An owned window that XP would draw with a caption: a dialog, or an
        application's second window. Its frame is owned by its owner's (no
        taskbar button, kept above it), as Win32 owned windows are."""
        return bool(c['owner']) and not c.get('undecorated') and c['owner'] in self.clients

    def configure(self, c, **values):
        """Configure a client and tell it its new root geometry (ICCCM)."""
        c['w'].configure(**values)
        x, y, width, height, border = c['geometry']
        c['geometry'] = (values.get('x', x), values.get('y', y), values.get('width', width),
                         values.get('height', height), values.get('border_width', border))
        self.configure_notify(c['w'])

    def redraw_native(self, w):
        # Wine keeps its own window surfaces. Native clients (notably xterm on
        # the VM's VGA path) can retain stale pixels after resize/activation.
        # Ask exposed descendants to repaint without clearing their contents or
        # injecting input. A terminal's drawing window is a child of its client.
        c = self.clients.get(w.id)
        if not c or c['hwnd']:
            return
        pending = [w]
        for _ in range(2048):
            if not pending:
                break
            child = pending.pop()
            if child.get_attributes().map_state != X.IsViewable:
                continue
            r = child.get_geometry()
            child.send_event(protocol.event.Expose(window=child, x=0, y=0,
                             width=r.width, height=r.height, count=0), event_mask=X.ExposureMask)
            pending.extend(child.query_tree().children)

    def configure_notify(self, w):
        # ICCCM: report root coordinates even when a move/stack request did not
        # generate a size change for the client. Native clients stay root children.
        x, y, width, height, border = self.clients[w.id]['geometry']
        w.send_event(protocol.event.ConfigureNotify(event=w, window=w,
                     above_sibling=X.NONE, x=x, y=y, width=width,
                     height=height, border_width=border,
                     override=False), event_mask=X.StructureNotifyMask)

    def focus_shell(self, w, time=X.CurrentTime):
        """Give the XP desktop or taskbar the keyboard, as a click does on XP.

        It is no application task, so no client stays active. TIME is that of
        the user's event: Wine ignores WM_TAKE_FOCUS older than the foreground
        window's own focus, and the keyboard only reaches a window that Wine
        made active."""
        if self.atom('WM_TAKE_FOCUS') in self.prop(w, 'WM_PROTOCOLS'):
            self.message(w, 'WM_PROTOCOLS', [self.atom('WM_TAKE_FOCUS'), time, 0, 0, 0])
        w.set_input_focus(X.RevertToPointerRoot, time)
        if self.active:
            self.active = 0
            self.setprop(self.root, '_NET_ACTIVE_WINDOW', Xatom.WINDOW, [0])

    def focused_client(self):
        """The client that holds the X keyboard focus, or one of its windows does."""
        focused = self.d.get_input_focus().focus
        while hasattr(focused, 'id') and focused.id not in (X.NONE, X.PointerRoot, self.root.id):
            if focused.id in self.clients:
                return focused.id
            focused = focused.query_tree().parent
        return None

    def native_focus(self):
        """The native client that has the keyboard rather than Wine, if any."""
        focused = self.d.get_input_focus().focus
        while hasattr(focused, 'id') and focused.id not in (X.NONE, X.PointerRoot, self.root.id):
            c = self.clients.get(focused.id)
            if c:
                return focused.id if c['kind'] == 'normal' and not c['hwnd'] else None
            focused = focused.query_tree().parent
        return None

    def shell_key(self, e):
        """A Windows key or Ctrl+Esc press, frozen by the WM's synchronous grab.

        Under a native client, Wine has none of these keys, and a window that
        Wine has yet to activate would lose them. The WM keeps the keyboard
        until the Windows key is up (the grab of a key lasts until its release),
        then the proxy enters the same keys into Wine with SendInput: XP's
        hotkeys (Win+R, Win+E, ...) and Wine's Start menu keys work as for a
        Win32 window, with the foreground's right to open the Start menu.
        Otherwise the key goes on as is."""
        if self.switch is not None:
            # The switcher's own grab delivers it; Esc there closes the switcher.
            self.d.allow_events(X.AsyncKeyboard, e.time)
            self.switch_event(e)
            return
        mode = X.ReplayKeyboard
        try:
            if self.native_focus():
                mode = X.AsyncKeyboard
                if e.detail == self.escape:
                    self.write_shell_keys('ctrl-escape', [])
                else:
                    self.shell_combo = []
        finally:
            self.d.allow_events(mode, e.time)

    def shell_combo_event(self, e):
        """A key while the WM holds the keyboard for the Windows key."""
        if e.type == X.KeyPress and e.detail not in self.super_codes and self.hotkeys.press(e):
            # A shortcut with the Windows key: the shell gets none of it.
            self.shell_combo_used = True
        elif e.type == X.KeyPress and e.detail not in self.super_codes:
            keysym = self.d.keycode_to_keysym(e.detail, 0)
            vk = VIRTUAL_KEYS.get(keysym)
            if vk is None and (XK.XK_a <= keysym <= XK.XK_z or XK.XK_0 <= keysym <= XK.XK_9):
                vk = ord(chr(keysym).upper())
            if vk is None and XK.XK_F1 <= keysym <= XK.XK_F24:
                vk = 0x70 + keysym - XK.XK_F1
            if vk is not None:
                self.shell_combo.append(vk)
        elif e.type == X.KeyRelease and e.detail in self.super_codes:
            if not self.shell_combo_used:
                self.write_shell_keys('windows', self.shell_combo)
            self.shell_combo = None
            self.shell_combo_used = False

    def write_shell_keys(self, kind, keys):
        self.shell_keys += 1
        temporary = self.ipc / 'shellkeys.tmp'
        temporary.write_text(f'{self.shell_keys} {kind}' + ''.join(f' {k:x}' for k in keys) + '\n')
        temporary.replace(self.ipc / 'shellkeys.tsv')

    def raise_managed(self, w):
        """Raise a managed window to just under the unmanaged (override-redirect)
        windows shown: above them, the X server would draw the menus covered for
        a frame before raise_overlays raised them again (All Programs blinked
        each time a submenu opened and the Start menu took the focus)."""
        # The lowest of those above the other managed windows, which w goes over.
        sibling = None
        try:
            for child in self.root.query_tree().children:
                if child.id in (self.support.id, w.id):
                    continue
                try:
                    attrs = child.get_attributes()
                except error.XError:
                    continue
                if attrs.map_state != X.IsViewable:
                    continue
                if not attrs.override_redirect:
                    sibling = None
                elif sibling is None:
                    sibling = child
            if sibling is not None:
                w.configure(sibling=sibling, stack_mode=X.Below)
            else:
                w.configure(stack_mode=X.Above)
        except error.XError:
            pass

    def raise_overlays(self):
        # The unmanaged (override-redirect) windows that are shown stay above the
        # managed ones, in their order: XP's menus and the Start menu's All
        # Programs cascade (popups of its own), their drop shadows, tooltips and
        # balloons are topmost on XP. Raising the Start menu when it takes the
        # focus back from a closed submenu covered All Programs.
        # Only those under a managed window move, in their order: restacking
        # the others each time (a menu that opens) made the menus flicker.
        try:
            children = self.root.query_tree().children
        except error.XError:
            children = []
        unmanaged, covered = [], []
        for w in children:
            if w.id == self.support.id:
                continue
            try:
                attrs = w.get_attributes()
            except error.XError:
                continue
            if attrs.map_state != X.IsViewable:
                continue
            if attrs.override_redirect:
                unmanaged.append(w)
            else:
                # A managed window above these covers them.
                covered += unmanaged
                unmanaged = []
        # The ones already on top follow, to keep the order.
        for w in covered + unmanaged if covered else ():
            try:
                w.configure(stack_mode=X.Above)
            except error.XError:
                pass
        # Windows 7's Start button over the taskbar and the user picture over the
        # Start menu are unmanaged windows of their own; raising the taskbar or
        # the menu must not cover them.
        for w in list(self.overlays.values()):
            try:
                w.configure(stack_mode=X.Above)
            except error.XError:
                self.overlays.pop(w.id, None)

    def focus(self, w):
        # Wine activation targets the proxy's XID; the native client owns X
        # keyboard focus and the single EWMH task identity.
        if w.id in self.frames:
            native = self.clients.get(self.frames[w.id])
            if not native:
                return
            w = native['w']
        c = self.clients.get(w.id)
        if c and c['kind'] == 'popup':
            # An XP Start popup needs real X keyboard focus for Wine activation,
            # without becoming the active application task or an Alt+Tab entry.
            w.map()
            self.raise_managed(w)
            self.raise_overlays()
            if self.atom('WM_TAKE_FOCUS') in self.prop(w, 'WM_PROTOCOLS'):
                self.message(w, 'WM_PROTOCOLS', [self.atom('WM_TAKE_FOCUS'), X.CurrentTime, 0, 0, 0])
            w.set_input_focus(X.RevertToPointerRoot, X.CurrentTime)
            return
        if c and c['kind'] in ('desktop', 'dock'):
            self.focus_shell(w)
            return
        if not c or c['kind'] != 'normal':
            return
        c['hidden'] = False
        w.map()
        self.setprop(w, 'WM_STATE', self.atom('WM_STATE'), [1, 0])
        states = list(self.prop(w, '_NET_WM_STATE'))
        self.setprop(w, '_NET_WM_STATE', Xatom.ATOM, [s for s in states if s != self.atom('_NET_WM_STATE_HIDDEN')])
        self.raise_managed(w)
        self.raise_owned(w.id)
        protocols = self.prop(w, 'WM_PROTOCOLS')
        if self.atom('WM_TAKE_FOCUS') in protocols:
            self.message(w, 'WM_PROTOCOLS', [self.atom('WM_TAKE_FOCUS'), X.CurrentTime, 0, 0, 0])
        w.set_input_focus(X.RevertToPointerRoot, X.CurrentTime)
        self.active = w.id
        self.setprop(self.root, '_NET_ACTIVE_WINDOW', Xatom.WINDOW, [w.id])
        self.restack()
        self.redraw_native(w)

    def raise_owned(self, owner, seen=()):
        """Keep the windows that OWNER owns (WM_TRANSIENT_FOR) above it, as Windows
        keeps owned windows above their owner: a click on the caption of a window
        with a modal dialog raised the window over its dialog."""
        seen = (*seen, owner)
        owned = [c for c in self.clients.values()
                 if c['kind'] == 'normal' and not c['hidden'] and c['owner'] == owner and c['w'].id not in seen]
        if not owned:
            return
        try:
            order = [w.id for w in self.root.query_tree().children]
        except error.XError:
            return
        for c in sorted(owned, key=lambda c: order.index(c['w'].id) if c['w'].id in order else len(order)):
            try:
                self.raise_managed(c['w'])
            except error.XError:
                continue
            self.raise_owned(c['w'].id, seen)

    def message(self, w, name, values):
        w.send_event(protocol.event.ClientMessage(window=w, client_type=self.atom(name), data=(32, values)))

    def minimize(self, w):
        c = self.clients[w.id]
        c['hidden'] = True
        w.unmap()
        for frame_id, native_id in self.frames.items():
            if native_id == w.id:
                self.clients[frame_id]['w'].unmap()
        self.setprop(w, 'WM_STATE', self.atom('WM_STATE'), [3, 0])
        states = list(self.prop(w, '_NET_WM_STATE'))
        hidden = self.atom('_NET_WM_STATE_HIDDEN')
        self.setprop(w, '_NET_WM_STATE', Xatom.ATOM, list(set(states + [hidden])))
        if self.active == w.id:
            self.active = 0
            self.focus_visible()

    def focus_visible(self):
        """Focus the most recently managed client that is shown, or nothing."""
        visible = [c for c in self.clients.values() if c['kind'] == 'normal' and not c['hidden']]
        if visible:
            self.focus(visible[-1]['w'])
        else:
            self.root.set_input_focus(X.RevertToPointerRoot, X.CurrentTime)
            self.setprop(self.root, '_NET_ACTIVE_WINDOW', Xatom.WINDOW, [0])

    def close(self, w):
        if self.atom('WM_DELETE_WINDOW') in self.prop(w, 'WM_PROTOCOLS'):
            self.message(w, 'WM_PROTOCOLS', [self.atom('WM_DELETE_WINDOW'), X.CurrentTime, 0, 0, 0])
        else:
            w.kill_client()

    def unmanage(self, window_id):
        self.clients.pop(window_id, None)
        self.previous_icons.pop(window_id, None)
        (self.ipc / f'icon-{window_id:x}.argb').unlink(missing_ok=True)
        (self.ipc / f'icon-{window_id:x}-small.argb').unlink(missing_ok=True)
        # Hide the orphan frame before the proxy destroys its HWND, and reject
        # any activation the proxy has already queued for it.
        for frame_id, native_id in list(self.frames.items()):
            if frame_id == window_id or native_id == window_id:
                frame = self.clients.get(frame_id)
                if frame:
                    frame['w'].unmap()
                del self.frames[frame_id]
        if self.active == window_id:
            # Focus the next window still shown, not a minimized one (Wine
            # withdraws a minimized window that it restores, and maps it again).
            self.active = 0
            self.focus_visible()
        self.restack()
        self.workarea()

    def restack(self):
        # Native clients remain root children. The genuine Wine nonclient frame
        # sits immediately below its native client, so Wine draws the XP caption
        # and resize borders while the native pixels cover its unused client area.
        for frame_id, native_id in self.frames.items():
            if frame_id in self.clients and native_id in self.clients:
                self.clients[frame_id]['w'].configure(sibling=self.clients[native_id]['w'], stack_mode=X.Below)
        for c in self.clients.values():
            if c['kind'] == 'desktop':
                c['w'].configure(stack_mode=X.Below)
        for c in self.clients.values():
            if c['kind'] == 'dock' and not c['hidden']:
                self.raise_managed(c['w'])
        # As on XP, the active full-screen window (a video, a game, a screen
        # capture's selection) covers the taskbar; another one uncovers it.
        active = self.clients.get(self.active)
        if active and active['kind'] == 'normal' and not active['hidden'] and self.fullscreen(active):
            self.raise_managed(active['w'])
            self.raise_owned(self.active)
        # Win32 topmost windows (EWMH above), such as XP's dimmed shut down
        # screen, cover the topmost taskbar; keep their transients above them.
        above = self.atom('_NET_WM_STATE_ABOVE')
        raised = set()
        for c in list(self.clients.values()):
            if c['kind'] == 'normal' and not c['hidden'] and above in self.prop(c['w'], '_NET_WM_STATE'):
                self.raise_managed(c['w'])
                raised.add(c['w'].id)
        for i, c in list(self.clients.items()):
            if c['kind'] == 'normal' and not c['hidden'] and i not in raised:
                if c['owner'] in raised:
                    self.raise_managed(c['w'])
        for c in self.clients.values():
            if c['kind'] == 'popup' and not c['hidden']:
                self.raise_managed(c['w'])
        self.raise_overlays()
        ids = [i for i, c in self.clients.items() if c['kind'] != 'proxy']
        self.setprop(self.root, '_NET_CLIENT_LIST', Xatom.WINDOW, ids)
        stack = [self.frames.get(w.id, w.id) for w in self.root.query_tree().children if w.id in ids or w.id in self.frames]
        stack = list(dict.fromkeys(stack))
        self.setprop(self.root, '_NET_CLIENT_LIST_STACKING', Xatom.WINDOW, stack)

    def workarea(self):
        g = self.root.get_geometry()
        left = top = right = bottom = 0
        for c in self.clients.values():
            if c['kind'] != 'dock' or c['hidden']:
                continue
            w = c['w']
            strut = list(self.prop(w, '_NET_WM_STRUT_PARTIAL')) or list(self.prop(w, '_NET_WM_STRUT'))
            if len(strut) >= 4:
                left, right, top, bottom = [max(a, b) for a, b in zip((left, right, top, bottom), strut[:4])]
            else:
                x, y, width, height, _ = c['geometry']
                if width >= g.width // 2:
                    if y >= g.height // 2:
                        bottom = max(bottom, g.height - y)
                    else:
                        top = max(top, y + height)
                elif x >= g.width // 2:
                    right = max(right, g.width - x)
                else:
                    left = max(left, x + width)
        self.area = (left, top, max(1, g.width-left-right), max(1, g.height-top-bottom))
        if list(self.prop(self.root, '_NET_WORKAREA')) != list(self.area):
            self.setprop(self.root, '_NET_WORKAREA', Xatom.CARDINAL, list(self.area))

    def bridge_alive(self):
        # The proxy holds a byte-range lock on proxy.lock while it runs;
        # wineserver backs it with a POSIX lock and drops it at process exit.
        try:
            fd = os.open(self.ipc / 'proxy.lock', os.O_RDONLY | os.O_CLOEXEC)
        except OSError:
            return False
        try:
            query = struct.pack('hhqqi4x', fcntl.F_WRLCK, os.SEEK_SET, 0, 1, 0)
            return struct.unpack('hhqqi4x', fcntl.fcntl(fd, fcntl.F_GETLK, query))[0] != fcntl.F_UNLCK
        except OSError:
            return False
        finally:
            os.close(fd)

    def write_switch(self, state):
        temporary = self.ipc / 'switch.tmp'
        temporary.write_text(f'{self.switch_session} {self.switch} {state}\n')
        temporary.replace(self.ipc / 'switch.tsv')

    def switch_key(self, reverse):
        """Alt+Tab: XP's switch window, drawn by the Win32 bridge.

        The keyboard stays grabbed while Alt is held; Tab and Shift+Tab move
        the selection, Escape cancels and releasing Alt switches. Without the
        bridge, cycle through the clients directly.
        """
        if not self.bridge_alive():
            self.cycle(reverse)
            return
        if self.root.grab_keyboard(True, X.GrabModeAsync, X.GrabModeAsync, X.CurrentTime) != X.GrabSuccess:
            self.cycle(reverse)
            return
        self.switch_session += 1
        self.switch = -1 if reverse else 1
        self.write_switch('o')
        # Alt may already be up when the grab starts; its release is then lost.
        keys = self.d.query_keymap()
        if not any(keys[code // 8] & (1 << (code % 8)) for code in self.alt_codes):
            self.end_switch('c')

    def switch_event(self, e):
        if e.type == X.KeyPress and e.detail == self.tab:
            self.switch += -1 if e.state & X.ShiftMask else 1
            self.write_switch('o')
        elif e.type == X.KeyPress and e.detail == self.escape:
            self.end_switch('x')
        elif e.type == X.KeyRelease and e.detail in self.alt_codes:
            self.end_switch('c')

    def end_switch(self, state):
        self.write_switch(state)
        self.d.ungrab_keyboard(X.CurrentTime)
        self.switch = None

    def cycle(self, reverse=False):
        ids = [i for i, c in self.clients.items() if c['kind'] == 'normal']
        if not ids:
            self.active = 0
            self.setprop(self.root, '_NET_ACTIVE_WINDOW', Xatom.WINDOW, [0])
            return
        index = ids.index(self.active) if self.active in ids else -1
        self.focus(self.clients[ids[(index + (-1 if reverse else 1)) % len(ids)]]['w'])

    def maximize(self, w, restore=False):
        c = self.clients[w.id]
        if c['hwnd']:
            # Wine's owner thread supplies its computed outer rectangle. Only
            # acknowledge EWMH state here; resizing it races restore placement.
            atoms = {self.atom('_NET_WM_STATE_MAXIMIZED_VERT'), self.atom('_NET_WM_STATE_MAXIMIZED_HORZ')}
            states = set(self.prop(w, '_NET_WM_STATE')) - atoms
            if not restore:
                states.update(atoms)
            self.setprop(w, '_NET_WM_STATE', Xatom.ATOM, list(states))
            self.focus(w)
            return
        if restore and c['restore']:
            x, y, width, height = c['restore']
            c['restore'] = None
        elif not restore:
            if c['restore'] is None:
                c['restore'] = c['geometry'][:4]
            x, y, width, height = self.area
            # Fit the complete Wine border inside the work area, using its
            # measured extents rather than assuming classic or Luna metrics.
            frame = next((self.clients[f]['w'] for f, n in self.frames.items() if n == w.id), None)
            if frame:
                (nx, ny, nwidth, nheight, _), outer = c['geometry'], frame.get_geometry()
                left, top = nx-outer.x, ny-outer.y
                width -= outer.width-nwidth
                height -= outer.height-nheight
                x += left; y += top
        else:
            self.focus(w)
            return
        self.configure(c, x=x, y=y, width=max(1, width), height=max(1, height))
        atoms = [self.atom('_NET_WM_STATE_MAXIMIZED_VERT'), self.atom('_NET_WM_STATE_MAXIMIZED_HORZ')]
        states = set(self.prop(w, '_NET_WM_STATE')) - set(atoms)
        if not restore:
            states.update(atoms)
        self.setprop(w, '_NET_WM_STATE', Xatom.ATOM, list(states))
        self.focus(w)

    def fullscreen(self, c):
        return self.atom('_NET_WM_STATE_FULLSCREEN') in c.get('states', ())

    def fill_screen(self, c):
        """Give a full-screen native client the whole screen, over the taskbar."""
        if c.get('windowed') is None:
            c['windowed'] = c['geometry'][:4]
        g = self.root.get_geometry()
        if c['geometry'][:4] != (0, 0, g.width, g.height) or c['geometry'][4]:
            self.configure(c, x=0, y=0, width=g.width, height=g.height, border_width=0)

    def set_fullscreen(self, c, on):
        """_NET_WM_STATE_FULLSCREEN, added or removed by the client.

        Wine sizes its own windows; a native client gets the screen, then
        its earlier rectangle back, inside its frame again."""
        if c['kind'] != 'normal':
            return
        if not c['hwnd']:
            if on:
                self.fill_screen(c)
            elif c.get('windowed'):
                x, y, width, height = c.pop('windowed')
                self.configure(c, x=x, y=y, width=width, height=height)
                c['extents'] = None  # the next bridge pass frames it again
        self.restack()

    def positioned(self, c):
        try:
            hints = c['w'].get_wm_normal_hints()
        except error.XError:
            return False
        if not hints or not hints.flags & (Xutil.USPosition | Xutil.PPosition):
            return False
        return bool(hints.flags & Xutil.USPosition) or c['geometry'][:2] != (0, 0)

    def fit(self, c, frame_width, frame_height):
        """The client's size, shrunk to fit the work area with its frame (not below its minimum size)."""
        width, height = c['geometry'][2:4]
        try:
            hints = c['w'].get_wm_normal_hints()
        except error.XError:
            hints = None
        min_width, min_height = (hints.min_width, hints.min_height) if hints and hints.flags & Xutil.PMinSize else (1, 1)
        return (min(width, max(self.area[2]-frame_width, min_width, 1)),
                min(height, max(self.area[3]-frame_height, min_height, 1)))

    def cascade(self, left, top):
        """Next XP default-window slot, as native client coordinates.

        XP steps 22px right (96 DPI, scaled here by the frame width) and
        SM_CYSIZE + SM_CYFRAME down, which is the caption extent minus one,
        and restarts after a quarter of the work area.
        """
        x, y, width, height = self.area
        step_x, step_y = max(1, round(22 * max(left, 1) / 4)), max(1, top - 1)
        slot = self.cascade_slot + 1
        if slot * step_x > width // 4 or slot * step_y > height // 4:
            slot = 1
        self.cascade_slot = slot
        return x + slot * step_x + left, y + slot * step_y + top

    def sync_frames(self):
        try:
            pairs = [line.split() for line in (self.ipc / 'frames.tsv').read_text().splitlines()]
            handles = {int(fields[0], 16): int(fields[1], 16) for fields in pairs}
            extents = {int(fields[1], 16): tuple(map(int, fields[2:7])) for fields in pairs if len(fields) >= 7}
            ready = {int(fields[1], 16): not int(fields[7]) for fields in pairs if len(fields) >= 8}
            clips = {int(fields[1], 16): tuple(map(int, fields[8:10])) for fields in pairs if len(fields) == 10}
        except (OSError, ValueError, IndexError):
            handles = {}
            extents = {}
            ready = {}
            clips = {}
        frames = {}
        for i, c in self.clients.items():
            if c['kind'] != 'proxy':
                continue
            native_id = handles.get(c['hwnd']) if c['hwnd'] else None
            native = self.clients.get(native_id)
            if native and native['kind'] == 'normal' and not native['hwnd']:
                frames[i] = native_id
                metrics = extents.get(native_id)
                if self.fullscreen(native):
                    # A full-screen window has no frame (XP draws none around
                    # one); its taskbar button stays.
                    metrics = None
                    if native['extents'] != [0, 0, 0, 0]:
                        native['extents'] = [0, 0, 0, 0]
                        self.setprop(native['w'], '_NET_FRAME_EXTENTS', Xatom.CARDINAL, native['extents'])
                if metrics:
                    left, right, top, bottom, maximized = metrics
                    if not native['framed']:
                        nx, ny, nwidth, nheight = native['geometry'][:4]
                        width, height = self.fit(native, left+right, top+bottom)
                        if (width, height) != (nwidth, nheight):
                            # Too large for the work area with its frame:
                            # shrink it, centred, as other window managers do.
                            ax, ay, awidth, aheight = self.area
                            x = ax + left + max(0, (awidth-left-right-width) // 2)
                            y = ay + top + max(0, (aheight-top-bottom-height) // 2)
                        elif native['owner'] in self.clients and not self.positioned(native):
                            # A dialog: centred on its owner, inside the work area.
                            ox, oy, owidth, oheight = self.clients[native['owner']]['geometry'][:4]
                            ax, ay, awidth, aheight = self.area
                            x = max(ax + left, min(ox + (owidth - width) // 2, ax + awidth - right - width))
                            y = max(ay + top, min(oy + (oheight - height) // 2, ay + aheight - bottom - height))
                        elif self.positioned(native):
                            # Keep an explicit position, but keep the first
                            # caption visible above content placed at 0,0.
                            x = max(nx, self.area[0]+left)
                            y = max(ny, self.area[1]+top)
                        else:
                            x, y = self.cascade(left, top)
                        if (x, y, width, height) != (nx, ny, nwidth, nheight):
                            self.configure(native, x=x, y=y, width=width, height=height)
                        native['framed'] = True
                    if native['extents'] != [left, right, top, bottom]:
                        native['extents'] = [left, right, top, bottom]
                        self.setprop(native['w'], '_NET_FRAME_EXTENTS', Xatom.CARDINAL, native['extents'])
                    if maximized and native['restore'] is not None:
                        x, y, width, height = self.area
                        bx, by = clips.get(native_id, (0, 0))
                        wanted = (x+left-bx, y+top-by, max(1, width-left-right+2*bx), max(1, height-top-bottom+2*by))
                        if native['geometry'][:4] != wanted:
                            self.configure(native, x=wanted[0], y=wanted[1], width=wanted[2], height=wanted[3])
                if native['geometry'][4]:
                    self.configure(native, border_width=0)
                if native['hidden'] or not ready.get(native_id, True) or self.fullscreen(native):
                    c['w'].unmap()
                else:
                    c['w'].map()
            else:
                c['w'].unmap()
        if frames != self.frames:
            self.frames = frames
            self.restack()
        self.write_geometry()

    def frame_moves(self, frame, values):
        """Keep a frame's pixels off screen while it moves, not while it changes size.

        Its client moves first (follow_frame) and covers the frame's leading
        edge for a moment; the frame's move then cannot copy that edge, and
        it stayed black until Wine painted it again, about 5 ms in each step
        of a drag (wide frames such as Windows 7's showed it). A redirected
        frame moves from its off-screen image. Redirected, each size change
        would cost a new image, so a size change ends it; the switch costs
        one repaint, without black."""
        x, y, width, height, _ = frame['geometry']
        resized = values.get('width', width) != width or values.get('height', height) != height
        if resized or values.get('x', x) != x or values.get('y', y) != y:
            frame['moving'] = not resized
            self.update_redirect(frame)

    def follow_frame(self, frame_id, values):
        """Move a native client with its frame, in the same pass.

        Dragging or sizing a frame moves the Wine window; the proxy then asks
        for the client's new geometry, one round trip through Wine and the
        IPC directory later. Meanwhile the frame's empty client area showed
        beside the client. The client's rectangle follows from the frame's by
        the measured border extents, so both move in the same flush; the
        proxy's later commands for these rectangles are then stale. The
        client moves first: where it already covers the frame's client
        area, the frame's move exposes nothing that Wine must paint."""
        native = self.clients.get(self.frames[frame_id])
        if not native or native['hidden'] or native['restore'] is not None or not native['extents'] or \
                self.fullscreen(native):
            return
        left, right, top, bottom = native['extents']
        x, y, width, height, _ = self.clients[frame_id]['geometry']
        x, y = values.get('x', x), values.get('y', y)
        width, height = values.get('width', width), values.get('height', height)
        if width <= left + right or height <= top + bottom:
            return
        wanted = (x + left, y + top, width - left - right, height - top - bottom)
        if native['geometry'][:4] != wanted:
            self.configure(native, x=wanted[0], y=wanted[1], width=wanted[2], height=wanted[3])
            native['followed'] = (native.get('followed') or [])[-31:] + [wanted]

    def follow_native(self, native_id):
        """Move a frame with its native client, the other way round.

        A client that moves itself (GTK drags its header bar so without
        _NET_WM_MOVERESIZE) waited for the proxy to move the frame. Wine
        takes the frame's new position from the X server as from any WM."""
        native = self.clients[native_id]
        frame = next((self.clients.get(f) for f, n in self.frames.items() if n == native_id), None)
        if not frame or native['hidden'] or native['restore'] is not None or not native['extents'] or \
                self.fullscreen(native):
            return
        left, right, top, bottom = native['extents']
        x, y, width, height, _ = native['geometry']
        wanted = (x - left, y - top, width + left + right, height + top + bottom)
        if frame['geometry'][:4] != wanted:
            self.configure(frame, x=wanted[0], y=wanted[1], width=wanted[2], height=wanted[3])
            native['followed'] = (native.get('followed') or [])[-31:] + [native['geometry'][:4]]

    def write_geometry(self):
        """Publish native geometry from the cache, without X round trips."""
        geometries = []
        for i, c in self.clients.items():
            if c['kind'] == 'normal' and not c['hwnd']:
                x, y, width, height, _ = c['geometry']
                geometries.append(f'{i:x} {x} {y} {width} {height} {int(c["restore"] is not None)} {self.area[0]} {self.area[1]} {self.area[2]} {self.area[3]}\n')
        text = ''.join(geometries)
        if text != self.previous_geometry:
            temporary = self.ipc / 'geometry.tmp'
            temporary.write_text(text)
            temporary.replace(self.ipc / 'geometry.tsv')
            self.previous_geometry = text

    def snapshot(self):
        lines = []
        classes = []
        focused = self.d.get_input_focus().focus
        # An XP menu can take focus without changing the EWMH active client.
        # Do not keep stealing Win32 foreground from that menu back to a proxy.
        try:
            while (hasattr(focused, 'id') and focused.id not in (X.NONE, X.PointerRoot, self.root.id)
                   and focused.id not in self.clients):
                focused = focused.query_tree().parent
            focused_id = focused.id if hasattr(focused, 'id') else 0
        except error.XError:
            focused_id = 0
        owners = []
        for i, c in self.clients.items():
            w = c['w']
            if c['kind'] != 'normal' or c['hwnd'] or (c['owner'] and not self.framed_owned(c)):
                continue
            if c['owner']:
                owners.append(f'{i:x}\t{c["owner"]:x}\n')
            if c['icon_dirty']:
                # The caption's 16x16 icon beside the big one; the proxy
                # reads both when the big file changes, so it goes last.
                data = (icon_payload(self.d, w, self.atom('_NET_WM_ICON'), 16),
                        icon_payload(self.d, w, self.atom('_NET_WM_ICON')))
                if i not in self.previous_icons or data != self.previous_icons[i]:
                    for name, payload in zip((f'icon-{i:x}-small.argb', f'icon-{i:x}.argb'), data):
                        target = self.ipc / name
                        if payload and data[1]:
                            temporary = target.with_suffix('.tmp')
                            temporary.write_bytes(payload)
                            temporary.replace(target)
                        else:
                            target.unlink(missing_ok=True)
                    self.previous_icons[i] = data
                c['icon_dirty'] = False
            lines.append(f'{i:x}\t{int(c["hidden"])}\t{int(i == self.active and i == focused_id)}\t{c["title"]}\n')
            classes.append(f'{i:x}\t{c.get("class", "")}\n')
        # The owned windows' owners, read by the proxy before it makes their
        # frames (windows.tsv keeps its format).
        text = ''.join(owners)
        if text != self.previous_owners:
            temp = self.ipc / 'owners.tmp'
            temp.write_text(text, encoding='utf-8')
            temp.replace(self.ipc / 'owners.tsv')
            self.previous_owners = text
        text = ''.join(classes)
        if text != self.previous_classes:
            temp = self.ipc / 'classes.tmp'
            temp.write_text(text, encoding='utf-8')
            temp.replace(self.ipc / 'classes.tsv')
            self.previous_classes = text
        text = ''.join(lines)
        if text != self.previous_snapshot:
            temp = self.ipc / 'windows.tmp'
            temp.write_text(text, encoding='utf-8')
            temp.replace(self.ipc / 'windows.tsv')
            self.previous_snapshot = text

    def commands(self):
        def order(path):
            # PID-SEQUENCE.cmd: in the order each proxy wrote them (9 before 10).
            pid, _, sequence = path.stem.partition('-')
            return (int(pid), int(sequence)) if pid.isdigit() and sequence.isdigit() else (0, 0)
        for command in sorted(self.ipc.glob('*.cmd'), key=order):
            try:
                op, value, *args = command.read_text().split()
                i = int(value, 16)
                c = self.clients.get(i)
                if op == 'switch' and c and c['kind'] == 'normal':
                    # Alt+Tab chose this window, which may also be a Wine one.
                    self.focus(c['w'])
                elif c and c['kind'] == 'normal' and not c['hwnd']:
                    if op == 'geometry' and len(args) == 4:
                        x, y, width, height = map(int, args)
                        wanted = (x, y, width, height)
                        # Rectangles that follow_frame has already applied
                        # arrive late; a newer one may be in place by then.
                        if width > 0 and height > 0 and c['geometry'][:4] != wanted and \
                                wanted not in (c.get('followed') or ()):
                            self.configure(c, x=x, y=y, width=width, height=height)
                            c['followed'] = None
                    elif op == 'restore':
                        self.maximize(c['w'], restore=True)
                    else:
                        {'activate': self.focus, 'minimize': self.minimize, 'close': self.close, 'maximize': self.maximize}[op](c['w'])
            except (ValueError, KeyError, OSError):
                pass
            finally:
                command.unlink(missing_ok=True)

    def changes_bridge(self, e):
        """What the proxy's files may need after event E: 0 nothing, 1 the
        native geometry only, 2 a whole pass.

        Each step of a window drag brings a ConfigureRequest and a
        ConfigureNotify. Only those of native clients, the taskbar and the
        root change what the proxy reads, and mostly just geometry.tsv; a
        whole pass for each of them made the WM a large part of a drag."""
        if e.type in (X.MotionNotify, X.ButtonRelease):
            return 0
        # A native client's request is published at once, from the cache,
        # without waiting for its notify.
        if e.type in (X.ConfigureRequest, X.ConfigureNotify) and e.window.id != self.root.id:
            c = self.clients.get(e.window.id)
            if not c or c['kind'] == 'proxy' or c['hwnd'] and c['kind'] != 'dock':
                return 0
            # A maximized client is kept fitted to the work area by sync_frames.
            return 1 if c['kind'] == 'normal' and c['restore'] is None else 2
        return 2

    def event(self, e):
        if e.type == X.MapRequest:
            self.manage(e.window)
        elif e.type == X.MapNotify:
            if e.override and self.overlay_window(e.window):
                self.restack()
            c = self.clients.get(e.window.id)
            if c and (c['kind'] == 'popup' or c['kind'] == 'normal' and self.active == e.window.id):
                # Wine defers WM_TAKE_FOCUS while waiting for the initial map acknowledgement.
                if self.atom('WM_TAKE_FOCUS') in self.prop(e.window, 'WM_PROTOCOLS'):
                    self.message(e.window, 'WM_PROTOCOLS', [self.atom('WM_TAKE_FOCUS'), X.CurrentTime, 0, 0, 0])
        elif e.type == X.ConfigureRequest:
            values = {name: getattr(e, name) for bit, name in
                      ((X.CWX, 'x'), (X.CWY, 'y'), (X.CWWidth, 'width'), (X.CWHeight, 'height'),
                       (X.CWBorderWidth, 'border_width'), (X.CWSibling, 'sibling'), (X.CWStackMode, 'stack_mode'))
                      if e.value_mask & bit}
            c = self.clients.get(e.window.id)
            if c:
                moves = e.value_mask & (X.CWX | X.CWY | X.CWWidth | X.CWHeight)
                if moves and e.window.id in self.frames:
                    self.frame_moves(c, values)
                    self.follow_frame(e.window.id, values)
                self.configure(c, **values)
                if moves and c['kind'] == 'normal' and not c['hwnd']:
                    self.follow_native(e.window.id)
            else:
                e.window.configure(**values)
            if e.value_mask & (X.CWSibling | X.CWStackMode):
                self.restack()
            if c and c['kind'] == 'dock':
                self.workarea()
        elif e.type == X.DestroyNotify:
            self.overlays.pop(e.window.id, None)
            self.unmanage(e.window.id)
        elif e.type == X.UnmapNotify:
            self.overlays.pop(e.window.id, None)
            c = self.clients.get(e.window.id)
            # A minimized client that withdraws itself sends only the synthetic
            # UnmapNotify (XWithdrawWindow); Wine 11 restores a minimized window
            # this way and maps it again once WM_STATE is gone.
            withdrawn = c and c['hidden'] and e.send_event and c['kind'] == 'normal'
            if c and (not c['hidden'] or withdrawn) and c['kind'] != 'proxy':
                # ICCCM withdrawal acknowledgement. Wine waits for PropertyDelete
                # (WithdrawnState) before issuing a subsequent XMapWindow. Keep
                # iconic clients and deliberately hidden proxy frames registered.
                e.window.delete_property(self.atom('WM_STATE'))
                self.unmanage(e.window.id)
        elif e.type == X.PropertyNotify:
            c = self.clients.get(e.window.id)
            if c:
                if e.atom in (self.atom('_NET_WM_ICON'), self.atom('WM_HINTS')):
                    c['icon_dirty'] = True
                self.refresh(c, e.atom)
                if e.atom in (self.atom('_WIN4WINE_CLASS'), self.atom('_NET_WM_WINDOW_TYPE')):
                    c['kind'] = self.kind(e.window)
                if e.atom in (self.atom('_WIN4WINE_CLASS'), self.atom('_WIN4WINE_HWND')):
                    self.update_redirect(c)
                if c['kind'] == 'proxy' and e.window.id not in self.frames:
                    e.window.unmap()
                if c['kind'] == 'dock' or e.atom in (self.atom('_WIN4WINE_CLASS'), self.atom('_NET_WM_WINDOW_TYPE')):
                    self.workarea()
        elif e.type == X.ButtonPress:
            if e.state & X.Mod1Mask and e.window.id == self.root.id and e.child:
                child = self.clients.get(e.child.id)
                if child and child['kind'] == 'normal':
                    self.focus(e.child)
                    r = e.child.get_geometry()
                    self.drag = (e.child, e.detail, e.root_x, e.root_y, r.x, r.y, r.width, r.height)
            else:
                c = self.clients.get(e.window.id)
                if c and c['kind'] in ('desktop', 'dock'):
                    self.focus_shell(e.window, e.time)
                else:
                    self.focus(e.window)
                self.d.allow_events(X.ReplayPointer, X.CurrentTime)
        elif e.type == X.MotionNotify and self.drag:
            w, button, px, py, x, y, width, height = self.drag
            dx, dy = e.root_x-px, e.root_y-py
            if button == 1:
                w.configure(x=x+dx, y=y+dy)
            else:
                w.configure(width=max(64, width+dx), height=max(32, height+dy))
        elif e.type == X.ButtonRelease:
            self.drag = None
        elif self.shell_combo is not None and e.type in (X.KeyPress, X.KeyRelease):
            # The WM holds the keyboard until the Windows key is released;
            # repeated presses of that key are ignored there.
            self.shell_combo_event(e)
        elif e.type == X.KeyPress and (e.detail in self.super_codes or
                                       e.detail == self.escape and e.state & X.ControlMask):
            self.shell_key(e)
        elif self.switch is not None and e.type in (X.KeyPress, X.KeyRelease):
            self.switch_event(e)
        elif e.type == X.KeyPress and self.hotkeys.press(e):
            if e.state & X.Mod4Mask:
                # The shell saw the Windows key go down: another key before it
                # goes up, or it would open the Start menu.
                control = self.d.keysym_to_keycode(XK.XK_Control_L)
                xtest.fake_input(self.d, X.KeyPress, control)
                xtest.fake_input(self.d, X.KeyRelease, control)
        elif e.type == X.KeyPress and e.detail == self.d.keysym_to_keycode(XK.string_to_keysym('F4')):
            if self.active in self.clients:
                self.close(self.clients[self.active]['w'])
        elif e.type == X.KeyPress and e.detail == self.tab:
            self.switch_key(bool(e.state & X.ShiftMask))
        elif e.type == X.FocusIn and e.mode == X.NotifyNormal and e.detail not in (X.NotifyPointer, X.NotifyPointerRoot, X.NotifyDetailNone) and e.window.id in self.clients:
            # A FocusIn can arrive after the focus has moved on (an Alt+Tab
            # right after a window was focused); it must not take back the
            # active window.
            if self.clients[e.window.id]['kind'] == 'normal' and self.focused_client() == e.window.id:
                self.active = e.window.id
                self.setprop(self.root, '_NET_ACTIVE_WINDOW', Xatom.WINDOW, [self.active])
        elif e.type == X.ConfigureNotify:
            if e.window.id == self.root.id:
                self.workarea()
            elif not e.send_event and e.window.id in self.clients:
                c = self.clients[e.window.id]
                c['geometry'] = (e.x, e.y, e.width, e.height, e.border_width)
                size = (e.width, e.height)
                if c['size'] != size:
                    c['size'] = size
                    if c['kind'] == 'normal' and not c['hidden']:
                        self.redraw_native(e.window)
        elif e.type == X.ClientMessage:
            values = e.data[1]
            if e.client_type == self.atom('_NET_ACTIVE_WINDOW'):
                self.focus(e.window)
            elif e.client_type == self.atom('_NET_CLOSE_WINDOW'):
                self.close(e.window)
            elif e.client_type == self.atom('WM_CHANGE_STATE') and values[0] == 3:
                if e.window.id in self.clients:
                    self.minimize(e.window)
            elif e.client_type == self.atom('_NET_WM_STATE') and e.window.id in self.clients:
                states = set(self.prop(e.window, '_NET_WM_STATE'))
                for atom in values[1:3]:
                    if not atom:
                        continue
                    if values[0] == 1 or values[0] == 2 and atom not in states:
                        states.add(atom)
                    else:
                        states.discard(atom)
                self.setprop(e.window, '_NET_WM_STATE', Xatom.ATOM, list(states))
                self.clients[e.window.id]['states'] = states
                if self.atom('_NET_WM_STATE_FULLSCREEN') in values[1:3]:
                    self.set_fullscreen(self.clients[e.window.id], self.atom('_NET_WM_STATE_FULLSCREEN') in states)
                if self.atom('_NET_WM_STATE_ABOVE') in values[1:3]:
                    self.restack()
                maximize_atoms = {self.atom('_NET_WM_STATE_MAXIMIZED_VERT'), self.atom('_NET_WM_STATE_MAXIMIZED_HORZ')}
                if maximize_atoms & set(values[1:3]):
                    maximized = self.atom('_NET_WM_STATE_MAXIMIZED_VERT') in states or self.atom('_NET_WM_STATE_MAXIMIZED_HORZ') in states
                    if self.clients[e.window.id]['kind'] == 'proxy':
                        native = self.clients.get(self.frames.get(e.window.id))
                        if native and (native['restore'] is not None) != maximized:
                            self.maximize(native['w'], restore=not maximized)
                    else:
                        self.maximize(e.window, restore=not maximized)

    def run(self):
        """Sleep until an X event, a proxy file or a signal arrives.

        Every change the proxy must see comes from an X event (map, configure,
        property, focus) or from the proxy's own files, so the bridge state is
        recomputed only then. Writes happen only when their content changes;
        the events they cause settle after one more pass.
        """
        changes = Inotify(self.ipc)
        wake_read, wake_write = os.pipe2(os.O_NONBLOCK | os.O_CLOEXEC)
        signal.set_wakeup_fd(wake_write)
        dirty = 2
        while self.running:
            while self.d.pending_events():
                e = self.d.next_event()
                try:
                    self.event(e)
                    dirty = max(dirty, self.changes_bridge(e))
                except error.XError:
                    dirty = 2  # A client may disappear between any two requests.
            self.hotkeys.check()
            if any(name.endswith('.cmd') or name == 'frames.tsv' for name in changes.names()):
                dirty = 2
            if dirty:
                try:
                    self.write_geometry()  # First: frames follow their clients.
                    if dirty == 2:
                        self.commands()
                        self.snapshot()
                        self.sync_frames()
                except error.XError:
                    pass
                dirty = 0
            self.d.flush()
            if self.d.pending_events():
                continue  # Replies to this pass queued more events.
            ready = select.select([self.d.fileno(), changes.fd, wake_read], [], [])[0]
            if wake_read in ready:
                while True:
                    try:
                        os.read(wake_read, 64)
                    except BlockingIOError:
                        break
        self.d.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ipc', required=True, type=Path)
    parser.add_argument('--no-composite', action='store_true', help="leave Wine's windows unredirected")
    args = parser.parse_args()
    wm = Manager(args.ipc, redirect=not args.no_composite)
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda *_: setattr(wm, 'running', False))
    wm.run()
