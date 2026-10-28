#!/usr/bin/env python3
"""The session's file manager for Linux applications: the shell's explorer.

Firefox's "Show in Folder", Chromium's and other applications' "Open
containing folder" ask the freedesktop file manager interface
(org.freedesktop.FileManager1) to show a file; without this service D-Bus
would start Dolphin or Nautilus. Here the shell's own explorer opens the
folder with the file selected (explorer /select,), in the session's prefix.

  filemanager.py WINE EXPLORER      (session/supervisor.py starts it)
"""
import subprocess
import sys

import gi
gi.require_version('Gio', '2.0')
from gi.repository import Gio, GLib

NAME = 'org.freedesktop.FileManager1'
PATH = '/org/freedesktop/FileManager1'
INTERFACE = f'''<node><interface name="{NAME}">
  <method name="ShowFolders"><arg type="as" name="URIs" direction="in"/><arg type="s" name="StartupId" direction="in"/></method>
  <method name="ShowItems"><arg type="as" name="URIs" direction="in"/><arg type="s" name="StartupId" direction="in"/></method>
  <method name="ShowItemProperties"><arg type="as" name="URIs" direction="in"/><arg type="s" name="StartupId" direction="in"/></method>
</interface></node>'''


def windows_path(uri):
    """A file: URI as its path on Wine's Z: drive (the Linux root), or None."""
    path = Gio.File.new_for_uri(uri).get_path()
    if not path:
        return None
    return 'Z:' + path.replace('/', '\\')


class FileManager:
    def __init__(self, wine, explorer):
        self.wine, self.explorer = wine, explorer

    def open(self, *args):
        # A second explorer hands the folder to the shell's and exits.
        subprocess.Popen([self.wine, self.explorer, *args], stdin=subprocess.DEVNULL,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)

    def call(self, connection, sender, path, interface, method, parameters, invocation):
        uris = parameters.unpack()[0]
        for uri in uris:
            target = windows_path(uri)
            if not target:
                continue
            if method == 'ShowFolders':
                self.open(target)
            else:
                # The item selected in its folder (its properties, too, start there).
                self.open('/select,' + target)
        invocation.return_value(None)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    manager = FileManager(sys.argv[1], sys.argv[2])
    info = Gio.DBusNodeInfo.new_for_xml(INTERFACE)

    def bus(connection, name):
        connection.register_object(PATH, info.interfaces[0], manager.call, None, None)

    def lost(connection, name):
        # Another file manager kept the name (one already running): nothing to serve.
        loop.quit()

    loop = GLib.MainLoop()
    Gio.bus_own_name(Gio.BusType.SESSION, NAME, Gio.BusNameOwnerFlags.REPLACE, bus, None, lost)
    loop.run()


if __name__ == '__main__':
    main()
