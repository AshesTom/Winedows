"""XP's Explorer navigation click, kept for the shell's own executable name.

HIVEDEF.INF gives Explorer's Navigating event the Start sound, but a live
prefix can lose its .Current value; Explorer then navigates silently (it
asks with SND_NODEFAULT). The shell also runs under another executable name
(C:\\xp\\xp-explorer.exe), under which SND_APPLICATION finds no app key, so
the effective event is mirrored there. Only the Windows Default scheme's
emptied event is restored; the other silent events are silent on a fresh
XP too. The mirror is never wiped when a query fails or the event is silent.

The session (session/supervisor.py) and XP Explorer (xp-explorer/xp.sh,
through xp-explorer/scripts/prefix.py) both repair it before the shell starts.
"""
import subprocess

import saved_registry
import wine_reg

APPS = r'AppEvents\Schemes\Apps'
NAVIGATING = APPS + r'\Explorer\Navigating'
MIRRORS = (APPS + r'\xp-explorer\Navigating\.Current', APPS + r'\xp-explorer\Navigating\.Default')
# Folder windows open in the shell's process: older prefixes opened them in
# another one, which flashed white while navigating.
BROWSE_NEW_PROCESS = r'Software\Microsoft\Windows\CurrentVersion\Explorer\BrowseNewProcess'


def repaired(prefix):
    """Whether PREFIX's saved registry has what repair() writes, as it has
    after the first repair: then no Wine program needs to run for it."""
    if saved_registry.server_running(prefix):
        return False

    def read(key, name=None):
        return saved_registry.user_value(prefix, key, name)
    if read(BROWSE_NEW_PROCESS, 'BrowseNewProcess') != 'no':
        return False
    current, default = read(NAVIGATING + r'\.Current'), read(NAVIGATING + r'\.Default')
    if read(r'AppEvents\Schemes') == '.Default' and not current and default:
        return False
    sound = current or default
    return not sound or all(read(mirror) == sound for mirror in MIRRORS)


def repair(wine, env, log=subprocess.DEVNULL):
    """Restore and mirror the navigation click in the prefix of ENV."""
    def add(key, *args):
        wine_reg.add(wine, env, 'HKCU\\' + key, *args, log=log)
    add(BROWSE_NEW_PROCESS, '/v', 'BrowseNewProcess', '/d', 'no')
    scheme = wine_reg.query(wine, env, r'HKCU\AppEvents\Schemes')
    current = wine_reg.query(wine, env, 'HKCU\\' + NAVIGATING + r'\.Current')
    default = wine_reg.query(wine, env, 'HKCU\\' + NAVIGATING + r'\.Default')
    if current is None and default is None:
        return
    sound = current or default
    if '.Default' in (scheme or '') and not current and default:
        add(NAVIGATING + r'\.Current', '/ve', '/t', 'REG_EXPAND_SZ', '/d', default)
    if sound:
        for mirror in MIRRORS:
            add(mirror, '/ve', '/t', 'REG_EXPAND_SZ', '/d', sound)
