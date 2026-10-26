"""The prefix's registry as Wine saved it (user.reg), read without starting Wine.

A `wine reg query` at the session's start took 0.2 s, and 4 s for the first
one while Wine started. What the wineserver last saved is only the registry
while no wineserver runs for the prefix: it keeps its changes in memory."""
import os
from pathlib import Path
import re
import socket

STRING = re.compile(r'^(?:str\(2\):)?"((?:[^"\\]|\\.)*)"$')


def server_running(prefix):
    """Whether a wineserver serves PREFIX (Wine's socket in /tmp/.wine-UID)."""
    st = os.stat(prefix)
    path = Path(f'/tmp/.wine-{os.getuid()}/server-{st.st_dev:x}-{st.st_ino:x}/socket')
    if not path.exists():
        return False
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        try:
            s.connect(str(path))
        except OSError:
            return False
    return True


def user_value(prefix, key, name=None):
    """The string value NAME (None: the key's default value) of the HKCU key KEY
    in PREFIX's user.reg; None when it is missing or not a string."""
    header = '[' + key.replace('\\', '\\\\').lower() + ']'
    wanted = '@' if name is None else '"' + name.replace('\\', '\\\\').replace('"', '\\"') + '"'
    inside = False
    try:
        lines = (Path(prefix) / 'user.reg').read_text(encoding='utf-8', errors='replace').splitlines()
    except OSError:
        return None
    for line in lines:
        if line.startswith('['):
            if inside:
                return None
            inside = line.rsplit(' ', 1)[0].lower() == header
        elif inside and line.lower().startswith(wanted.lower() + '='):
            match = STRING.match(line[len(wanted) + 1:])
            return re.sub(r'\\(.)', r'\1', match.group(1)) if match else None
    return None
