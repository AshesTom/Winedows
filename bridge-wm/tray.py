#!/usr/bin/env python3
"""Tray host that shows Linux tray icons in the XP notification area.

Owns org.kde.StatusNotifierWatcher on the session bus and registers itself
as the host. When no other X tray runs, it also owns _NET_SYSTEM_TRAY_S0 and
takes in XEmbed icon windows, which it keeps off the screen. Each item's
icon, tooltip and menu go to the bridge IPC directory; the Win32 proxy adds
them with Shell_NotifyIcon and writes the user's clicks back as *.tray
files. Wakes only for D-Bus, X and directory events.

Files (all atomically renamed in the IPC directory):
- tray.tsv: "ID TAB status TAB tooltip" per shown item, status A (active)
  or N (needs attention); passive items are left out.
  XEmbed icons share the IDs; their menus are the applications' own.
- tray-ID.argb: W4I1 icon at the size the proxy asks for in tray-size.
- menu-ID.tsv: "SEQ" then "depth TAB item TAB flags TAB label" per entry;
  flags are any of s (separator), d (disabled), c (checked), r (radio).
- from the proxy, *.tray: "activate ID X Y", "secondary ID X Y",
  "context ID X Y" or "event ID SEQ ITEM".
"""
import argparse
import os
from pathlib import Path
import signal
import struct
import sys
import time

import gi
gi.require_version('Gio', '2.0')
gi.require_version('GdkPixbuf', '2.0')
from gi.repository import Gio, GLib, GdkPixbuf
from Xlib import X, Xatom, display, error
from Xlib.ext import composite, damage, shape, xtest
from Xlib.protocol import event

WATCHER = 'org.kde.StatusNotifierWatcher'
ITEM = 'org.kde.StatusNotifierItem'
MENU = 'com.canonical.dbusmenu'
WATCHER_XML = '''<node><interface name="org.kde.StatusNotifierWatcher">
<method name="RegisterStatusNotifierItem"><arg name="service" type="s" direction="in"/></method>
<method name="RegisterStatusNotifierHost"><arg name="service" type="s" direction="in"/></method>
<property name="RegisteredStatusNotifierItems" type="as" access="read"/>
<property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
<property name="ProtocolVersion" type="i" access="read"/>
<signal name="StatusNotifierItemRegistered"><arg type="s"/></signal>
<signal name="StatusNotifierItemUnregistered"><arg type="s"/></signal>
<signal name="StatusNotifierHostRegistered"/>
<signal name="StatusNotifierHostUnregistered"/>
</interface></node>'''
MAX_ITEMS = 64
MAX_MENU = 256
SYSTEM_TRAY_REQUEST_DOCK = 0
XEMBED_EMBEDDED_NOTIFY = 0
XEMBED_MAPPED = 1
# A window of the default depth has no alpha. It is shown on this colour,
# that of XP's Luna notification area, and the pixels left in it become
# transparent.
XEMBED_BACKGROUND = 0x1397ed


def write_atomic(path, data):
    tmp = path.with_suffix('.tmp-tray')
    tmp.write_bytes(data)
    tmp.replace(path)


def mnemonic(label):
    """dbusmenu marks access keys with _ (and __ for a literal _); Win32 uses &."""
    out, i = [], 0
    while i < len(label):
        ch = label[i]
        if ch == '_' and i + 1 < len(label):
            out.append('_' if label[i + 1] == '_' else '&' + label[i + 1])
            i += 2
            continue
        out.append('&&' if ch == '&' else ch)
        i += 1
    return ''.join(out).replace('\t', ' ').replace('\n', ' ')


def pixbuf_bytes(pixbuf, size):
    """A W4I1 image of size x size, the picture scaled to fit and centred."""
    if pixbuf.get_width() != size or pixbuf.get_height() != size:
        w, h = pixbuf.get_width(), pixbuf.get_height()
        scale = size / max(w, h)
        sw, sh = max(1, round(w * scale)), max(1, round(h * scale))
        scaled = pixbuf.scale_simple(sw, sh, GdkPixbuf.InterpType.HYPER)
        pixbuf = GdkPixbuf.Pixbuf.new(GdkPixbuf.Colorspace.RGB, True, 8, size, size)
        pixbuf.fill(0)
        scaled.copy_area(0, 0, sw, sh, pixbuf, (size - sw) // 2, (size - sh) // 2)
    if not pixbuf.get_has_alpha():
        pixbuf = pixbuf.add_alpha(False, 0, 0, 0)
    stride, data = pixbuf.get_rowstride(), pixbuf.get_pixels()
    out = bytearray(b'W4I1' + struct.pack('<II', size, size))
    for y in range(size):
        row = data[y * stride:y * stride + size * 4]
        line = bytearray(size * 4)
        # W4I1 pixels are little-endian ARGB words: B, G, R, A in memory.
        line[0::4], line[1::4], line[2::4], line[3::4] = row[2::4], row[1::4], row[0::4], row[3::4]
        out += line
    return bytes(out)


class Item:
    def __init__(self, host, key, name, path, bus=None):
        self.host, self.key, self.name, self.path = host, key, name, path
        self.bus = bus or host.bus
        self.id = host.next_id()
        self.props = {}
        self.icon = None  # last written icon bytes
        self.menu_seq = 0
        self.watch = Gio.bus_watch_name_on_connection(self.bus, name, Gio.BusNameWatcherFlags.NONE,
                                                      None, lambda *_: host.remove(key))
        self.subscription = self.bus.signal_subscribe(name, ITEM, None, path, None, Gio.DBusSignalFlags.NONE,
                                                      lambda *_: self.refresh())
        self.refresh()

    def close(self):
        Gio.bus_unwatch_name(self.watch)
        self.bus.signal_unsubscribe(self.subscription)
        for name in (f'tray-{self.id}.argb', f'menu-{self.id}.tsv'):
            try:
                (self.host.ipc / name).unlink()
            except FileNotFoundError:
                pass

    def refresh(self):
        self.bus.call(self.name, self.path, 'org.freedesktop.DBus.Properties', 'GetAll',
                           GLib.Variant('(s)', (ITEM,)), GLib.VariantType('(a{sv})'),
                           Gio.DBusCallFlags.NONE, 2000, None, self.refreshed)

    def refreshed(self, bus, result):
        try:
            self.props = bus.call_finish(result).unpack()[0]
        except GLib.Error as e:
            print('properties of', self.name, self.path, e.message, file=sys.stderr)
            return
        self.host.publish()

    def status(self):
        status = self.props.get('Status', 'Active')
        return {'Passive': '', 'NeedsAttention': 'N'}.get(status, 'A')

    def tooltip(self):
        tip = self.props.get('ToolTip')
        text = tip[2] if tip and len(tip) > 2 and tip[2] else self.props.get('Title', '') or self.props.get('Id', '')
        return ' '.join(text.split())[:127]

    def pixbuf(self, size):
        attention = self.status() == 'N'
        names = ('AttentionIcon', 'Icon') if attention else ('Icon',)
        for prefix in names:
            pixmaps = self.props.get(prefix + 'Pixmap') or []
            pixmaps = [p for p in pixmaps if p[0] > 0 and p[1] > 0 and len(p[2]) == p[0] * p[1] * 4]
            if pixmaps:
                # The smallest image at least as large as wanted scales best.
                w, h, data = min(pixmaps, key=lambda p: (p[0] < size, abs(p[0] - size)))
                rgba = bytearray(len(data))
                rgba[0::4], rgba[1::4], rgba[2::4], rgba[3::4] = data[1::4], data[2::4], data[3::4], data[0::4]
                return GdkPixbuf.Pixbuf.new_from_bytes(GLib.Bytes.new(bytes(rgba)), GdkPixbuf.Colorspace.RGB,
                                                       True, 8, w, h, w * 4)
            name = self.props.get(prefix + 'Name')
            if name:
                pixbuf = self.host.theme_icon(name, size, self.props.get('IconThemePath', ''))
                if pixbuf:
                    return pixbuf
        return None

    def icon_bytes(self, size):
        pixbuf = self.pixbuf(size)
        return pixbuf_bytes(pixbuf, size) if pixbuf else None

    def call(self, method, args, done=None):
        self.bus.call(self.name, self.path, ITEM, method, args, None, Gio.DBusCallFlags.NONE, 2000, None,
                           done or self.host.ignore)

    def activate(self, x, y):
        if self.props.get('ItemIsMenu'):
            return self.context(x, y)
        self.call('Activate', GLib.Variant('(ii)', (x, y)))

    def secondary(self, x, y):
        self.call('SecondaryActivate', GLib.Variant('(ii)', (x, y)))

    def context(self, x, y):
        menu = self.props.get('Menu')
        if not menu or menu == '/':
            self.call('ContextMenu', GLib.Variant('(ii)', (x, y)))
            return
        self.menu_seq += 1
        seq = self.menu_seq
        # AboutToShow lets the application update the entries first.
        self.bus.call(self.name, menu, MENU, 'AboutToShow', GLib.Variant('(i)', (0,)), None,
                           Gio.DBusCallFlags.NONE, 1000, None,
                           lambda bus, result: self.layout(menu, seq, bus, result))

    def layout(self, menu, seq, bus, result):
        try:
            bus.call_finish(result)
        except GLib.Error:
            pass  # Optional for the application.
        bus.call(self.name, menu, MENU, 'GetLayout', GLib.Variant('(iias)', (0, -1, [])),
                 GLib.VariantType('(u(ia{sv}av))'), Gio.DBusCallFlags.NONE, 2000, None,
                 lambda bus, result: self.write_menu(menu, seq, bus, result))

    def write_menu(self, menu, seq, bus, result):
        try:
            _, root = bus.call_finish(result).unpack()
        except GLib.Error as e:
            print('menu of', self.name, e.message, file=sys.stderr)
            return
        lines = [str(seq)]

        def walk(node, depth):
            for child in node[2]:
                if len(lines) > MAX_MENU:
                    return
                item_id, props, _ = child
                if not props.get('visible', True):
                    continue
                flags = ''
                if props.get('type') == 'separator':
                    flags += 's'
                if not props.get('enabled', True):
                    flags += 'd'
                if props.get('toggle-state', 0) == 1:
                    flags += 'r' if props.get('toggle-type') == 'radio' else 'c'
                lines.append(f'{depth}\t{item_id}\t{flags or "-"}\t{mnemonic(props.get("label", ""))}')
                if depth < 8:
                    walk(child, depth + 1)
        walk(root, 0)
        self.menu = menu
        write_atomic(self.host.ipc / f'menu-{self.id}.tsv', ('\n'.join(lines) + '\n').encode())

    def event(self, seq, item):
        if seq != self.menu_seq or not getattr(self, 'menu', None):
            return
        self.bus.call(self.name, self.menu, MENU, 'Event',
                           GLib.Variant('(isvu)', (item, 'clicked', GLib.Variant('i', 0), int(time.time()))),
                           None, Gio.DBusCallFlags.NONE, 2000, None, self.host.ignore)


class Embedded:
    """An XEmbed icon window inside an invisible frame.

    The frame is redirected with Composite, so neither it nor the icon is
    drawn on the screen, and its input region is empty, so it takes no
    pointer input. Damage reports say when to read the icon's image again."""

    def __init__(self, tray, window, ident):
        self.tray, self.window, self.id = tray, window, ident
        d, size = tray.d, tray.size
        self.icon = None
        self.image = None
        self.pixels = None
        self.frame = tray.root.create_window(-size, -size, size, size, 0, tray.depth, X.InputOutput, X.CopyFromParent,
                                            override_redirect=True, background_pixel=XEMBED_BACKGROUND)
        self.frame.shape_rectangles(shape.SO.Set, shape.SK.Input, X.Unsorted, 0, 0, [])
        self.frame.composite_redirect_window(composite.RedirectManual)
        window.change_attributes(event_mask=X.StructureNotifyMask | X.PropertyChangeMask)
        window.change_save_set(X.SetModeInsert)
        window.reparent(self.frame, 0, 0)
        window.configure(x=0, y=0, width=size, height=size, border_width=0)
        self.depth = window.get_geometry().depth
        self.damage = window.damage_create(damage.DamageReportNonEmpty)
        tray.send_xembed(window, XEMBED_EMBEDDED_NOTIFY, 0, self.frame.id, 0)
        self.frame.map()
        self.mapped = None
        self.title = ''
        self.info()
        self.name()

    def close(self, gone):
        try:
            self.tray.d.damage_destroy(self.damage)
        except error.XError:
            pass
        if not gone:
            # A window that stays alive goes back to the root for the next tray.
            try:
                self.window.unmap()
                self.window.reparent(self.tray.root, 0, 0)
                self.window.change_save_set(X.SetModeDelete)
            except error.XError:
                pass
        self.frame.destroy()
        try:
            (self.tray.host.ipc / f'tray-{self.id}.argb').unlink()
        except FileNotFoundError:
            pass

    def info(self):
        prop = self.window.get_full_property(self.tray.atom('_XEMBED_INFO'), X.AnyPropertyType)
        # Without the property the icon counts as mapped.
        mapped = not prop or len(prop.value) < 2 or bool(prop.value[1] & XEMBED_MAPPED)
        if mapped != self.mapped:
            self.mapped = mapped
            if mapped:
                self.window.map()
            else:
                self.window.unmap()
                self.tray.host.publish()

    def name(self):
        """XEmbed has no tooltip; the window's name or class stands in."""
        title = ''
        for atom, kind in (('_NET_WM_NAME', 'UTF8_STRING'), ('WM_NAME', None)):
            prop = self.window.get_full_property(self.tray.atom(atom), self.tray.atom(kind) if kind else X.AnyPropertyType)
            if prop and prop.value:
                value = prop.value
                title = value.decode('utf-8', 'replace') if isinstance(value, bytes) else str(value)
                break
        if not title:
            wm_class = self.window.get_wm_class()
            title = wm_class[1] if wm_class else ''
        title = ' '.join(title.split())[:127]
        if title != self.title:
            self.title = title
            self.tray.host.publish()

    def capture(self):
        try:
            geometry = self.window.get_geometry()
            w, h = geometry.width, geometry.height
            image = self.window.get_image(0, 0, w, h, X.ZPixmap, 0xffffffff)
        except error.XError:
            return
        data = image.data
        if len(data) != w * h * 4:
            return  # Only 32 bits per pixel, as on any TrueColor screen.
        rgba = bytearray(len(data))
        b, g, r, a = data[0::4], data[1::4], data[2::4], data[3::4]
        if self.depth == 32:
            # Pixels of ARGB visuals are premultiplied; icons are not.
            out = bytearray(len(data))
            for i in range(w * h):
                alpha = a[i]
                if alpha:
                    out[i * 4] = min(255, r[i] * 255 // alpha)
                    out[i * 4 + 1] = min(255, g[i] * 255 // alpha)
                    out[i * 4 + 2] = min(255, b[i] * 255 // alpha)
                    out[i * 4 + 3] = alpha
            rgba = out
        else:
            key = (XEMBED_BACKGROUND >> 16, XEMBED_BACKGROUND >> 8 & 255, XEMBED_BACKGROUND & 255)
            rgba[0::4], rgba[1::4], rgba[2::4] = r, g, b
            for i in range(w * h):
                rgba[i * 4 + 3] = 0 if (r[i], g[i], b[i]) == key else 255
        if (bytes(rgba), w, h) != self.pixels:
            self.pixels = (bytes(rgba), w, h)
            self.image = GdkPixbuf.Pixbuf.new_from_bytes(GLib.Bytes.new(self.pixels[0]), GdkPixbuf.Colorspace.RGB,
                                                         True, 8, w, h, w * 4)
            self.tray.host.publish()

    def status(self):
        return 'A' if self.mapped and self.image else ''

    def tooltip(self):
        return self.title

    def icon_bytes(self, size):
        return pixbuf_bytes(self.image, size) if self.image else None

    def click(self, button):
        """Click the icon with the pointer where the user just clicked its XP icon.

        For the click only, the frame is put under the pointer and takes
        input, and the X server gets a real button press and release (XTest):
        toolkits such as GTK 3 take pointer input through XInput 2 and ignore
        sent events. Applications place their menus beside the icon's window."""
        tray, size = self.tray, self.tray.size
        try:
            pointer = tray.root.query_pointer()
            self.frame.configure(x=pointer.root_x - size // 2, y=pointer.root_y - size // 2, stack_mode=X.Above)
            self.frame.shape_mask(shape.SO.Set, shape.SK.Input, 0, 0, X.NONE)
            xtest.fake_input(tray.d, X.ButtonPress, button)
            xtest.fake_input(tray.d, X.ButtonRelease, button)
            self.frame.shape_rectangles(shape.SO.Set, shape.SK.Input, X.Unsorted, 0, 0, [])
            tray.d.sync()
        except error.XError as e:
            print('click on XEmbed icon failed:', e, file=sys.stderr)

    def activate(self, x, y):
        self.click(1)

    def secondary(self, x, y):
        self.click(2)

    def context(self, x, y):
        self.click(3)

    def event(self, seq, item):
        pass


class XEmbedTray:
    """The X system tray (_NET_SYSTEM_TRAY_Sn), taken only when no other tray runs."""

    def __init__(self, host):
        self.host = host
        self.d = display.Display()
        self.d.set_error_handler(lambda err, request: None)
        screen = self.d.screen()
        self.root, self.depth = screen.root, screen.root_depth
        self.selection = self.atom(f'_NET_SYSTEM_TRAY_S{self.d.get_default_screen()}')
        self.icons = {}  # window id: Embedded
        self.size = host.icon_size()
        self.window = None
        self.last_time = X.CurrentTime
        if self.d.get_selection_owner(self.selection) != X.NONE:
            print('another X system tray runs; XEmbed icons stay with it', file=sys.stderr)
            self.d.close()
            self.d = None
            return
        for ext in ('Composite', 'DAMAGE', 'SHAPE', 'XTEST'):
            if not self.d.has_extension(ext):
                print(f'no {ext} extension; XEmbed icons are not shown', file=sys.stderr)
                self.d.close()
                self.d = None
                return
        self.d.composite_query_version()
        self.d.damage_query_version()
        self.damage_event = self.d.query_extension('DAMAGE').first_event + damage.DamageNotifyCode
        self.window = self.root.create_window(-1, -1, 1, 1, 0, X.CopyFromParent, X.InputOnly, X.CopyFromParent,
                                              override_redirect=True, event_mask=X.PropertyChangeMask)
        self.window.change_property(self.atom('_NET_SYSTEM_TRAY_ORIENTATION'), Xatom.CARDINAL, 32, [0])
        visual = self.argb_visual(screen)
        if visual:
            self.window.change_property(self.atom('_NET_SYSTEM_TRAY_VISUAL'), Xatom.VISUALID, 32, [visual])
        self.window.change_property(self.atom('_NET_SYSTEM_TRAY_ICON_SIZE'), Xatom.CARDINAL, 32, [self.size])
        stamp = self.time(fresh=True)
        self.window.set_selection_owner(self.selection, stamp)
        if self.d.get_selection_owner(self.selection) != self.window:
            print('could not take the X system tray', file=sys.stderr)
            return
        self.root.send_event(event.ClientMessage(window=self.root, client_type=self.atom('MANAGER'),
                                                 data=(32, [stamp, self.selection, self.window.id, 0, 0])),
                             event_mask=X.StructureNotifyMask)
        self.d.flush()
        # The watch ends itself once the connection is closed.
        GLib.io_add_watch(self.d.fileno(), GLib.PRIORITY_DEFAULT, GLib.IO_IN | GLib.IO_HUP | GLib.IO_NVAL,
                          lambda *_: self.events())
        self.drain()

    def atom(self, name):
        return self.d.intern_atom(name)

    @staticmethod
    def argb_visual(screen):
        for depth in screen.allowed_depths:
            if depth.depth == 32:
                for visual in depth.visuals:
                    if visual.visual_class == X.TrueColor:
                        return visual.visual_id
        return None

    def time(self, fresh=False):
        """A server time: the last one seen, or a new one from a property change."""
        if fresh or self.last_time == X.CurrentTime:
            self.window.change_property(self.atom('_WIN4WINE_TRAY_TIME'), Xatom.CARDINAL, 32, [0])
            self.d.flush()
            while True:
                e = self.d.next_event()
                if e.type == X.PropertyNotify and e.window == self.window:
                    self.last_time = e.time
                    break
                self.handle(e)
        return self.last_time

    def send_xembed(self, window, message, detail, data1, data2):
        window.send_event(event.ClientMessage(window=window, client_type=self.atom('_XEMBED'),
                                              data=(32, [self.time(), message, detail, data1, data2])),
                          event_mask=X.NoEventMask)

    def drain(self):
        """Handle events that replies to our own requests left in the queue."""
        def once():
            self.events()
            return GLib.SOURCE_REMOVE
        GLib.idle_add(once)

    def events(self):
        if not self.d:
            return GLib.SOURCE_REMOVE
        try:
            while self.d and self.d.pending_events():
                self.handle(self.d.next_event())
            if self.d:
                self.d.flush()
        except error.ConnectionClosedError:
            print('X connection closed; XEmbed icons stop', file=sys.stderr)
            self.d = None
        return GLib.SOURCE_CONTINUE if self.d else GLib.SOURCE_REMOVE

    def handle(self, e):
        if hasattr(e, 'time') and isinstance(e.time, int) and e.time:
            self.last_time = e.time
        if e.type == X.ClientMessage and e.window == self.window:
            if e.client_type == self.atom('_NET_SYSTEM_TRAY_OPCODE') and e.data[1][1] == SYSTEM_TRAY_REQUEST_DOCK:
                self.dock(e.data[1][2])
        elif e.type == self.damage_event:
            icon = self.icons.get(e.drawable.id if hasattr(e.drawable, 'id') else e.drawable)
            if icon:
                self.d.damage_subtract(icon.damage)
                icon.capture()
        elif e.type == X.PropertyNotify:
            icon = self.icons.get(e.window.id)
            if icon and e.atom == self.atom('_XEMBED_INFO'):
                icon.info()
            elif icon and e.atom in (self.atom('_NET_WM_NAME'), self.atom('WM_NAME')):
                icon.name()
        elif e.type == X.DestroyNotify:
            self.undock(e.window.id, gone=True)
        elif e.type == X.ReparentNotify:
            icon = self.icons.get(e.window.id)
            if icon and e.parent != icon.frame:
                self.undock(e.window.id, gone=True)
        elif e.type == X.SelectionClear and e.window == self.window:
            print('another X system tray took over', file=sys.stderr)
            self.close()

    def dock(self, xid):
        if xid in self.icons or len(self.host.items) + len(self.icons) >= MAX_ITEMS:
            return
        window = self.d.create_resource_object('window', xid)
        try:
            self.icons[xid] = Embedded(self, window, self.host.next_id())
            self.d.sync()
        except error.XError as e:
            print('XEmbed icon', hex(xid), 'could not be embedded:', e, file=sys.stderr)
            icon = self.icons.pop(xid, None)
            if icon:
                icon.close(gone=True)
            return
        self.host.publish()

    def undock(self, xid, gone):
        icon = self.icons.pop(xid, None)
        if icon:
            icon.close(gone)
            self.d.flush()
            self.host.publish()

    def resize(self, size):
        if not self.d or size == self.size:
            return
        self.size = size
        self.window.change_property(self.atom('_NET_SYSTEM_TRAY_ICON_SIZE'), Xatom.CARDINAL, 32, [size])
        for icon in self.icons.values():
            icon.frame.configure(width=size, height=size)
            icon.window.configure(width=size, height=size)
        self.d.flush()

    def close(self):
        if self.d:
            for ident in list(self.icons):
                self.undock(ident, gone=False)
            self.d.close()
            self.d = None


class Host:
    def __init__(self, ipc):
        self.ipc = ipc
        self.items = {}
        self.desktop = None
        self.ids = 0
        self.tsv = None
        self.bus = Gio.bus_get_sync(Gio.BusType.SESSION)
        self.loop = GLib.MainLoop()
        self.monitor = Gio.File.new_for_path(str(ipc)).monitor_directory(Gio.FileMonitorFlags.WATCH_MOVES, None)
        self.monitor.connect('changed', self.changed)
        node = Gio.DBusNodeInfo.new_for_xml(WATCHER_XML)
        self.bus.register_object('/StatusNotifierWatcher', node.interfaces[0], self.method, self.property, None)
        Gio.bus_own_name_on_connection(self.bus, WATCHER, Gio.BusNameOwnerFlags.DO_NOT_QUEUE,
                                       self.acquired, self.lost)
        Gio.bus_own_name_on_connection(self.bus, f'org.kde.StatusNotifierHost-{os.getpid()}',
                                       Gio.BusNameOwnerFlags.NONE, None, None)
        # Every theme below the icon directories is searched; the names of
        # tray icons rarely collide between themes.
        self.theme_dirs = [p for d in [GLib.get_user_data_dir()] + GLib.get_system_data_dirs()
                           for p in (Path(d) / 'icons', Path(d) / 'pixmaps') if p.is_dir()]
        self.index = None
        self.xembed = None
        # In a window of a desktop, the programs on that desktop's bus keep their icons in its
        # tray; they are shown here too, and their menus and clicks go to them.
        address = os.environ.get('WIN4WINE_DESKTOP_BUS')
        if address:
            try:
                self.follow(Gio.DBusConnection.new_for_address_sync(
                    address, Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
                    None, None))
            except GLib.Error as e:
                print('no desktop bus:', e.message, file=sys.stderr)

    def follow(self, bus):
        self.desktop = bus
        bus.signal_subscribe(WATCHER, WATCHER, 'StatusNotifierItemRegistered', '/StatusNotifierWatcher', None,
                             Gio.DBusSignalFlags.NONE, lambda _b, _s, _p, _i, _n, params: self.add_desktop(params.unpack()[0]))
        bus.call(WATCHER, '/StatusNotifierWatcher', 'org.freedesktop.DBus.Properties', 'Get',
                 GLib.Variant('(ss)', (WATCHER, 'RegisteredStatusNotifierItems')), GLib.VariantType('(v)'),
                 Gio.DBusCallFlags.NONE, 2000, None, self.followed)

    def followed(self, bus, result):
        try:
            for service in bus.call_finish(result).unpack()[0]:
                self.add_desktop(service)
        except GLib.Error as e:
            print('no tray on the desktop:', e.message, file=sys.stderr)

    def add_desktop(self, service):
        # "name/path", or a bare name
        name, slash, path = service.partition('/')
        key = f'desktop:{service}'
        if key not in self.items and len(self.items) < MAX_ITEMS:
            self.items[key] = Item(self, key, name, f'/{path}' if slash else '/StatusNotifierItem', self.desktop)
            self.publish()

    def acquired(self, *_):
        self.emit('StatusNotifierHostRegistered', None)
        if os.environ.get('DISPLAY') and not self.xembed:
            try:
                self.xembed = XEmbedTray(self)
            except (error.DisplayError, error.XError) as e:
                print('no X system tray:', e, file=sys.stderr)
        self.publish()

    def lost(self, *_):
        # On the bus of the desktop it follows, that desktop's watcher keeps the name.
        if os.environ.get('WIN4WINE_DESKTOP_BUS'):
            return
        # Another watcher (a desktop's own tray) already serves this login.
        print('another StatusNotifierWatcher owns the name; exiting', file=sys.stderr)
        self.loop.quit()

    def emit(self, signal, params):
        self.bus.emit_signal(None, '/StatusNotifierWatcher', WATCHER, signal, params)

    def shown(self):
        """Every item, StatusNotifierItem or XEmbed icon, in the order of their IDs."""
        icons = list(self.xembed.icons.values()) if self.xembed else []
        return sorted([*self.items.values(), *icons], key=lambda i: i.id)

    def next_id(self):
        used = {item.id for item in self.shown()}
        while True:
            self.ids = self.ids % 0xffff + 1
            if self.ids not in used:
                return self.ids

    def method(self, bus, sender, path, interface, method, params, invocation):
        if method == 'RegisterStatusNotifierItem':
            service = params.unpack()[0]
            # Some items pass their object path and are named by the sender.
            name, path = (sender, service) if service.startswith('/') else (service, '/StatusNotifierItem')
            key = f'{name}{path}'
            if key not in self.items and len(self.items) < MAX_ITEMS:
                self.items[key] = Item(self, key, name, path)
                self.emit('StatusNotifierItemRegistered', GLib.Variant('(s)', (key,)))
        invocation.return_value(None)

    def property(self, bus, sender, path, interface, name):
        if name == 'RegisteredStatusNotifierItems':
            return GLib.Variant('as', list(self.items))
        if name == 'IsStatusNotifierHostRegistered':
            return GLib.Variant('b', True)
        return GLib.Variant('i', 0)

    def remove(self, key):
        item = self.items.pop(key, None)
        if item:
            item.close()
            self.emit('StatusNotifierItemUnregistered', GLib.Variant('(s)', (key,)))
            self.publish()

    def ignore(self, bus, result):
        try:
            bus.call_finish(result)
        except GLib.Error as e:
            print('call failed:', e.message, file=sys.stderr)

    @staticmethod
    def scan(root, index):
        """Index PNG and SVG icons below root by name: (size or 0 for scalable, path)."""
        for dirpath, _, files in os.walk(root):
            size = 0
            for part in Path(dirpath).relative_to(root).parts:
                if 'x' in part and part.split('@')[0].split('x')[0].isdigit():
                    size = int(part.split('x')[0])
            for file in files:
                stem, ext = os.path.splitext(file)
                if ext in ('.png', '.svg'):
                    index.setdefault(stem, []).append((0 if ext == '.svg' else size, os.path.join(dirpath, file)))

    def theme_icon(self, name, size, extra):
        if name.startswith('/'):
            choices = [(0, name)]
        else:
            if self.index is None:
                self.index = {}
                for root in self.theme_dirs:
                    self.scan(root, self.index)
            extra_index = {}
            if extra and os.path.isdir(extra):
                self.scan(extra, extra_index)
            choices = extra_index.get(name) or self.index.get(name)
            if not choices and name.endswith('-symbolic'):
                choices = self.index.get(name[:-len('-symbolic')])
            if not choices:
                return None
            # An image of the wanted size, then a scalable one, then the
            # nearest larger and finally the largest smaller image.
            choices = sorted(choices, key=lambda c: (c[0] != size, c[0] != 0, c[0] < size,
                                                     abs(c[0] - size)))
        for _, path in choices:
            try:
                return GdkPixbuf.Pixbuf.new_from_file_at_size(path, size, size)
            except GLib.Error:
                continue
        return None

    def icon_size(self):
        try:
            size = int((self.ipc / 'tray-size').read_text().split()[0])
        except (OSError, ValueError, IndexError):
            return 16
        return size if 8 <= size <= 64 else 16

    def publish(self):
        size = self.icon_size()
        lines = []
        for item in self.shown():
            status = item.status()
            if not status:
                continue
            icon = item.icon_bytes(size)
            if icon and icon != item.icon:
                write_atomic(self.ipc / f'tray-{item.id}.argb', icon)
            item.icon = icon
            lines.append(f'{item.id}\t{status}\t{item.tooltip()}')
        text = ('\n'.join(lines) + '\n' if lines else '').encode()
        if text != self.tsv:
            write_atomic(self.ipc / 'tray.tsv', text)
            self.tsv = text

    def changed(self, monitor, file, other, event):
        # The WM and the proxy write and remove their own files here all the
        # time (several for each step of a window drag); look only at ours.
        names = [f.get_basename() for f in (file, other) if f]
        if any(name.endswith('.tray') or name == 'tray-size' for name in names):
            self.commands()

    def commands(self):
        if (self.ipc / 'tray-size').exists() and getattr(self, 'size', None) != self.icon_size():
            self.size = self.icon_size()
            if self.xembed:
                self.xembed.resize(self.size)
            for item in self.shown():
                item.icon = None
            self.publish()
        for path in sorted(self.ipc.glob('*.tray')):
            try:
                words = path.read_text().split()
                path.unlink()
            except OSError:
                continue
            try:
                op, ident, args = words[0], int(words[1]), [int(w) for w in words[2:]]
            except (IndexError, ValueError):
                continue
            item = next((i for i in self.shown() if i.id == ident), None)
            if not item:
                continue
            if op in ('activate', 'secondary', 'context') and len(args) == 2:
                getattr(item, op)(*args)
            elif op == 'event' and len(args) == 2:
                item.event(*args)
        if self.xembed and self.xembed.d:
            self.xembed.drain()

    def run(self):
        for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
            GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, sig, lambda: self.loop.quit() or GLib.SOURCE_REMOVE)
        self.commands()
        self.loop.run()
        if self.xembed:
            self.xembed.close()
        try:
            (self.ipc / 'tray.tsv').unlink()
        except FileNotFoundError:
            pass


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--ipc', type=Path, required=True)
    Host(parser.parse_args().ipc).run()
