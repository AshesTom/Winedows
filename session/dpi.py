"""The XP shell's DPI (LogPixels) and font smoothing, and the matching settings for Linux applications.

XP scales text and metrics with LogPixels and keeps 32x32 icons; Linux
toolkits follow Xft.dpi for their text the same way. GTK 3 scales its
widgets only by whole factors (GDK_SCALE), so from 192 DPI on it doubles
them and keeps the text at Xft.dpi. With XP's ClearType, Linux applications
get subpixel rendering in XP's RGB order too (Xft.rgba and fontconfig).
Their default font takes the size of XP's message font (Tahoma 8 pt on
English XP), not the desktop's larger one."""
from pathlib import Path
import re
import struct

DEFAULT = 96


def _registry_value(hive, key, name):
    """A REG_DWORD (int), REG_SZ (str) or REG_BINARY (bytes) from a Wine registry file, or None."""
    try:
        text = Path(hive).read_text(encoding='utf-8', errors='replace')
    except OSError:
        return None
    header = '[' + key.replace('\\', '\\\\') + ']'
    for section in text.split('\n\n'):
        lines = re.sub(r'\\\n\s*', '', section).strip().splitlines()
        if not lines or not lines[0].startswith(header + ' ') and lines[0] != header:
            continue
        for line in lines[1:]:
            match = re.fullmatch(r'"%s"=(?:dword:([0-9a-fA-F]{8})|"([^"\\]*)"|hex:([0-9a-fA-F,]*))' % re.escape(name),
                                 line.strip())
            if match and match.group(3) is not None:
                return bytes(int(byte, 16) for byte in match.group(3).split(',') if byte)
            if match:
                return int(match.group(1), 16) if match.group(1) else match.group(2)
    return None


def _registry_dword(hive, key, name):
    value = _registry_value(hive, key, name)
    return value if isinstance(value, int) else None


def prefix_dpi(prefix):
    """LogPixels as Wine reads it: the user's, then the hardware profile's, else 96."""
    prefix = Path(prefix)
    value = (_registry_dword(prefix / 'user.reg', r'Control Panel\Desktop', 'LogPixels')
             or _registry_dword(prefix / 'system.reg',
                                r'System\ControlSet001\Hardware Profiles\Current\Software\Fonts', 'LogPixels'))
    return value if value and 0 < value <= 960 else DEFAULT


def prefix_ui_font(prefix):
    """XP's message font (Display Properties, Appearance) as (face, points), or None.

    WindowMetrics keeps it as a LOGFONTW in pixels at AppliedDPI; XP's
    sizes are whole points (Tahoma 8 is 11 pixels at 96 DPI)."""
    key = r'Control Panel\Desktop\WindowMetrics'
    hive = Path(prefix) / 'user.reg'
    font = _registry_value(hive, key, 'MessageFont')
    if not isinstance(font, bytes) or len(font) < 92:
        return None
    height = struct.unpack_from('<i', font)[0]
    face = font[28:92].decode('utf-16-le', 'replace').split('\0')[0]
    applied = _registry_dword(hive, key, 'AppliedDPI') or DEFAULT
    points = round(abs(height) * 72 / applied)
    return (face, points) if face and 0 < points < 100 else None


def prefix_cleartype(prefix):
    """Whether XP's font smoothing is ClearType (Display Properties, Effects) in the prefix."""
    hive = Path(prefix) / 'user.reg'
    smoothing = _registry_value(hive, r'Control Panel\Desktop', 'FontSmoothing')
    try:
        smoothing = int(smoothing)
    except (TypeError, ValueError):
        return False
    return bool(smoothing) and _registry_dword(hive, r'Control Panel\Desktop', 'FontSmoothingType') == 2


def cursor_size(dpi):
    """XP's SM_CXCURSOR: 32 up to 144 DPI, 64 above (measured at 144 and 168 DPI)."""
    return 64 if dpi > 144 else 32


def environment(dpi, wayland=False):
    """Variables for Linux applications started in the session.

    On Wayland, applications take each output's scale from the compositor."""
    env = {'XCURSOR_SIZE': str(cursor_size(dpi))}
    scale = window_scale(dpi)
    if scale >= 2 and not wayland:
        env.update(GDK_SCALE=str(scale), GDK_DPI_SCALE=f'{1 / scale:g}')
    return env


def window_scale(dpi):
    """GTK's whole window scale for DPI: 2 from 192 DPI on."""
    return max(1, dpi // DEFAULT)


def xsettings(dpi, cursor_theme=None, font=None):
    """The XSETTINGS (name: int or str) that give X11 applications the same DPI,
    scale and cursor as the environment, also those started without it.

    As gnome-settings-daemon: Xft/DPI is the whole DPI (Qt, Chromium), and
    GTK, scaled by Gdk/WindowScalingFactor, takes Gdk/UnscaledDPI instead
    (with GDK_SCALE set it keeps Xft/DPI and GDK_DPI_SCALE). FONT, (face,
    points), is GTK's default font (Gtk/FontName), which also Qt's GTK
    platform theme, Electron and Firefox take."""
    scale = window_scale(dpi)
    settings = {'Xft/DPI': dpi * 1024, 'Gdk/WindowScalingFactor': scale,
                'Gdk/UnscaledDPI': dpi * 1024 // scale, 'Gtk/CursorThemeSize': cursor_size(dpi)}
    if cursor_theme:
        settings['Gtk/CursorThemeName'] = cursor_theme
    if font:
        settings['Gtk/FontName'] = f'{font[0]} {font[1]}'
    return settings


def set_screen_dpi(d, dpi):
    """Give the X screen of Xlib display D the physical size of DPI (what xrandr --dpi does).

    Toolkits that size points by the screen's millimetres (Tk, Xt, core
    fonts) then follow it too. Xorg takes it; Xephyr keeps the size from its
    -dpi option (xp.sh)."""
    from Xlib import error
    from Xlib.ext import randr
    screen = d.screen()
    width, height = screen.width_in_pixels, screen.height_in_pixels
    mm = (round(width * 25.4 / dpi), round(height * 25.4 / dpi))
    if (screen.width_in_mms, screen.height_in_mms) == mm or not d.has_extension('RANDR'):
        return
    try:
        randr.set_screen_size(screen.root, width, height, *mm)
        d.sync()
    except error.XError:
        pass


# The resources set for ClearType; the marker lets a later session with
# standard smoothing on the same X server take them back.
CLEARTYPE_RESOURCES = {'Xft.rgba': 'rgb', 'Xft.lcdfilter': 'lcddefault', 'win4wine.cleartype': '1'}


def set_xft(d, dpi, cleartype=False):
    """Set Xft.dpi, and Xft.rgba for ClearType, in the root window's resources (what xrdb -merge does) on Xlib display D."""
    from Xlib import Xatom
    root = d.screen().root
    atom = d.intern_atom('RESOURCE_MANAGER')
    prop = root.get_full_property(atom, Xatom.STRING)
    value = prop.value if prop else b''
    if isinstance(value, str):
        value = value.encode()
    lines = value.decode('latin-1').splitlines()
    names = {'Xft.dpi'}
    if cleartype or any(line.split(':')[0] == 'win4wine.cleartype' for line in lines):
        names.update(CLEARTYPE_RESOURCES)
    lines = [line for line in lines if line.split(':')[0] not in names]
    lines.append(f'Xft.dpi:\t{dpi}')
    if cleartype:
        lines.extend(f'{name}:\t{value}' for name, value in CLEARTYPE_RESOURCES.items())
    root.change_property(atom, Xatom.STRING, 8, ('\n'.join(lines) + '\n').encode('latin-1'))
    d.flush()
