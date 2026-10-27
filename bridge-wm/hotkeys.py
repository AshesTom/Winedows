"""Keyboard shortcuts of the XP session, for bridge-wm.

The session has no desktop's shortcut daemon (KDE's kglobalaccel): bridge-wm,
which holds the keyboard for the Windows key, also takes these keys on the
root window, whatever window has the focus.

- The media keys (Fn with the volume, mute, playback and brightness keys):
  the sound server's volume (wpctl, else pactl), playerctl, brightnessctl.
- The user's shortcuts, ~/.config/win4wine/shortcuts.tsv: a key combination
  and a command per line ("Win+Shift+S<TAB>spectacle -r"), which Control
  Panel's Keyboard Shortcuts item edits. Made the first time from the
  shortcuts that KDE gives the installed applications (their desktop files'
  X-KDE-Shortcuts, as the user's kglobalshortcutsrc changed them), so
  Spectacle's keys work as on the user's KDE desktop. XP's own keys (Win+E,
  Win+R, Alt+Tab, ...) stay the shell's. The file is read again when it
  changes.
"""
import configparser
import os
from pathlib import Path
import shlex
import shutil
import subprocess

from Xlib import X, XK

CONFIG = Path(os.environ.get('XDG_CONFIG_HOME') or Path.home() / '.config')
SHORTCUTS = CONFIG / 'win4wine/shortcuts.tsv'
HEADER = ('# Keyboard shortcuts of the XP session: a key combination, a tab, a command.\n'
          '# Control Panel > Printers and Other Hardware > Keyboard Shortcuts edits this file.\n')
MODIFIERS = {'ctrl': X.ControlMask, 'control': X.ControlMask, 'shift': X.ShiftMask, 'alt': X.Mod1Mask,
             'win': X.Mod4Mask, 'meta': X.Mod4Mask, 'super': X.Mod4Mask}
RELEVANT = X.ControlMask | X.ShiftMask | X.Mod1Mask | X.Mod4Mask
# Key names as Control Panel and KDE write them, as X keysym names.
NAMES = {'pageup': 'Prior', 'pgup': 'Prior', 'pagedown': 'Next', 'pgdown': 'Next', 'del': 'Delete',
         'ins': 'Insert', 'esc': 'Escape', 'enter': 'Return', 'return': 'Return', 'space': 'space',
         'backspace': 'BackSpace', 'print': 'Print', 'printscreen': 'Print', 'pause': 'Pause', 'tab': 'Tab',
         'home': 'Home', 'end': 'End', 'left': 'Left', 'right': 'Right', 'up': 'Up', 'down': 'Down',
         'insert': 'Insert', 'delete': 'Delete', 'escape': 'Escape', 'menu': 'Menu'}
# XP's own: the shell's (Windows key hotkeys) and bridge-wm's.
RESERVED = {(X.Mod4Mask, name) for name in ('e', 'r', 'd', 'm', 'f', 'l', 'u', 'Tab', 'Pause', 'Break', 'F1')} | {
    (X.Mod1Mask, 'Tab'), (X.Mod1Mask | X.ShiftMask, 'Tab'), (X.Mod1Mask, 'F4'), (X.ControlMask, 'Escape'),
    (X.ControlMask | X.Mod1Mask, 'Delete'), (X.Mod4Mask | X.ShiftMask, 'm')}


def volume_command(change):
    """A shell command that changes the default output's volume (XP's 0-100 %)."""
    if shutil.which('wpctl'):
        if change == 'mute':
            return 'wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle'
        if change == 'mic':
            return 'wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle'
        return f'wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ {abs(change)}%{"+" if change > 0 else "-"}'
    if change == 'mute':
        return 'pactl set-sink-mute @DEFAULT_SINK@ toggle'
    if change == 'mic':
        return 'pactl set-source-mute @DEFAULT_SOURCE@ toggle'
    # pactl has no upper limit: stop at 100 %, as XP's volume does.
    return (f'pactl set-sink-volume @DEFAULT_SINK@ {change:+d}% && '
            "v=$(pactl get-sink-volume @DEFAULT_SINK@ | grep -o '[0-9]*%' | head -1 | tr -d %); "
            '[ "${v:-0}" -le 100 ] || pactl set-sink-volume @DEFAULT_SINK@ 100%')


def media_keys():
    """(keysym name, command) of the media keys this system can serve."""
    keys = [('XF86AudioRaiseVolume', volume_command(5)), ('XF86AudioLowerVolume', volume_command(-5)),
            ('XF86AudioMute', volume_command('mute')), ('XF86AudioMicMute', volume_command('mic'))]
    if shutil.which('playerctl'):
        keys += [('XF86AudioPlay', 'playerctl play-pause'), ('XF86AudioPause', 'playerctl pause'),
                 ('XF86AudioStop', 'playerctl stop'), ('XF86AudioNext', 'playerctl next'),
                 ('XF86AudioPrev', 'playerctl previous')]
    if shutil.which('brightnessctl'):
        keys += [('XF86MonBrightnessUp', 'brightnessctl set +5%'), ('XF86MonBrightnessDown', 'brightnessctl set 5%-')]
    return keys


def parse_keys(text):
    """'Ctrl+Shift+S' as (modifier mask, keysym name), or None."""
    parts = [p.strip() for p in text.replace(' ', '').split('+')]
    if not parts or not parts[-1]:
        return None
    mask = 0
    for part in parts[:-1]:
        if part.lower() not in MODIFIERS:
            return None
        mask |= MODIFIERS[part.lower()]
    key = parts[-1]
    name = NAMES.get(key.lower(), key.lower() if len(key) == 1 else key)
    if XK.string_to_keysym(name) == 0:
        return None
    return mask, name


def desktop_command(exec_line):
    """A desktop entry's Exec without its field codes (%u, %F, ...)."""
    words = [w for w in shlex.split(exec_line) if not (len(w) == 2 and w.startswith('%'))]
    return shlex.join(words)


def kde_shortcuts():
    """[(keys, command)] that KDE gives the installed applications."""
    overrides = configparser.ConfigParser(interpolation=None, strict=False, delimiters=('=',))
    overrides.optionxform = str
    try:
        overrides.read(CONFIG / 'kglobalshortcutsrc', encoding='utf-8')
    except configparser.Error:
        pass
    found = []
    homes = [Path(os.environ.get('XDG_DATA_HOME') or Path.home() / '.local/share')]
    homes += [Path(d) for d in os.environ.get('XDG_DATA_DIRS', '/usr/local/share:/usr/share').split(':') if d]
    seen = set()
    for directory in homes:
        for entry in sorted((directory / 'applications').glob('*.desktop')):
            if entry.name in seen:
                continue
            seen.add(entry.name)
            desktop = configparser.ConfigParser(interpolation=None, strict=False, delimiters=('=',))
            desktop.optionxform = str
            try:
                desktop.read(entry, encoding='utf-8')
            except (configparser.Error, UnicodeDecodeError):
                continue
            changed = f'services][{entry.name}'
            for group in desktop.sections():
                if group == 'Desktop Entry':
                    action = '_launch'
                elif group.startswith('Desktop Action '):
                    action = group[len('Desktop Action '):]
                else:
                    continue
                keys = desktop[group].get('X-KDE-Shortcuts', '')
                command = desktop[group].get('Exec')
                if overrides.has_section(changed) and overrides[changed].get(action) is not None:
                    keys = overrides[changed][action].replace('\t', ',')
                if not command or not keys or keys == 'none':
                    continue
                for combo in keys.split(','):
                    if combo and combo != 'none' and parse_keys(combo):
                        found.append((combo, desktop_command(command)))
    return found


class Hotkeys:
    def __init__(self, d, root):
        self.d, self.root = d, root
        self.bindings = {}   # (keycode, mask) -> command
        self.grabbed = []
        self.stamp = None
        if not SHORTCUTS.exists():
            self.seed()
        self.load()

    def seed(self):
        try:
            entries = [(k, c) for k, c in kde_shortcuts() if parse_keys(k) and parse_keys(k) not in RESERVED]
            SHORTCUTS.parent.mkdir(parents=True, exist_ok=True)
            temporary = SHORTCUTS.with_suffix('.tmp')
            temporary.write_text(HEADER + ''.join(f'{k}\t{c}\n' for k, c in entries), encoding='utf-8')
            temporary.replace(SHORTCUTS)
        except OSError:
            pass

    def entries(self):
        try:
            lines = SHORTCUTS.read_text(encoding='utf-8').splitlines()
        except OSError:
            return []
        out = []
        for line in lines:
            if not line.strip() or line.lstrip().startswith('#') or '\t' not in line:
                continue
            keys, command = line.split('\t', 1)
            parsed = parse_keys(keys)
            if parsed and parsed not in RESERVED and command.strip():
                out.append((parsed, command.strip()))
        return out

    def load(self):
        try:
            self.stamp = SHORTCUTS.stat().st_mtime_ns
        except OSError:
            self.stamp = None
        for code, mask in self.grabbed:
            self.root.ungrab_key(code, mask)
        self.grabbed, self.bindings = [], {}
        wanted = [((0, name), command) for name, command in media_keys()] + self.entries()
        for (mask, name), command in wanted:
            code = self.d.keysym_to_keycode(XK.string_to_keysym(name))
            if not code or (code, mask) in self.bindings:
                continue
            self.bindings[(code, mask)] = command
            # With Caps Lock and Num Lock too.
            for lock in (0, X.LockMask, X.Mod2Mask, X.LockMask | X.Mod2Mask):
                self.root.grab_key(code, mask | lock, True, X.GrabModeAsync, X.GrabModeAsync)
                self.grabbed.append((code, mask | lock))

    def check(self):
        """Read the file again if it changed."""
        try:
            stamp = SHORTCUTS.stat().st_mtime_ns
        except OSError:
            stamp = None
        if stamp != self.stamp:
            self.load()

    def press(self, e):
        """Run the command of a shortcut key press; whether it was one."""
        command = self.bindings.get((e.detail, e.state & RELEVANT))
        if command is None:
            return False
        subprocess.Popen(['sh', '-c', command], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)
        return True
