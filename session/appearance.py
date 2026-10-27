"""XP has no dark mode: the session's applications get no dark preference.

Firefox (and GTK 4 and libadwaita applications) take their light or dark
look from the desktop portal's colour scheme, which xdg-desktop-portal-gtk
reads from GSettings (org.gnome.desktop.interface color-scheme), where KDE
copies its colour scheme's preference. With "prefer-dark" Firefox leaves the
session's GTK theme (qt-style/tools/gtktheme.cpp) for Adwaita's dark one.

While the session runs the preference is "default" (the portal then says
"no preference" and applications follow the theme); the user's value is kept
in the session's state directory and given back when the session ends. A
session that ended without giving it back gives it back at its next end."""
import shutil
import subprocess

KEY = ('org.gnome.desktop.interface', 'color-scheme')
DEFAULT = "'default'"


def _gsettings(env, *args):
    try:
        result = subprocess.run(['gsettings', *args], env=env, capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return result.stdout.strip() if result.returncode == 0 else None


def light(state, env):
    """Make the preference "default", keeping the user's value in STATE."""
    if not shutil.which('gsettings'):
        return
    current = _gsettings(env, 'get', *KEY)
    if current is None:
        return
    saved = state / 'color-scheme'
    # The value this session set is ours: keep the user's that was kept.
    if current != DEFAULT or not saved.exists():
        saved.write_text(current + '\n')
    if current != DEFAULT:
        _gsettings(env, 'set', *KEY, DEFAULT)


def restore(state, env):
    """Give the user's preference back, if the session changed it."""
    saved = state / 'color-scheme'
    try:
        value = saved.read_text().strip()
    except OSError:
        return
    if _gsettings(env, 'get', *KEY) == DEFAULT and value and value != DEFAULT:
        _gsettings(env, 'set', *KEY, value)
    saved.unlink(missing_ok=True)
