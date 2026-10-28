"""Wine's reg.exe from Python: a value's data, and values added.

Both are best effort and never raise: reg.exe starts the prefix's
wineserver and services when none runs, which can hang (a minute at most
here) or fail; the callers then leave the registry as it is."""
import subprocess

TIMEOUT = 60
# reg query's types, longest first: REG_SZ is the end of no other name.
TYPES = ('REG_EXPAND_SZ', 'REG_MULTI_SZ', 'REG_BINARY', 'REG_DWORD', 'REG_QWORD', 'REG_SZ')


def query(wine, env, key, name=None, view=None):
    """The data of the value NAME of KEY (its default value without NAME) as
    reg query prints it: '' for a value without data, None when the key or
    the value is missing or reg.exe fails. VIEW 32 or 64 chooses that
    registry view (reg's /reg:32 and /reg:64)."""
    options = (f'/reg:{view}',) if view else ()
    try:
        result = subprocess.run([wine, 'reg', 'query', key, *(('/v', name) if name else ('/ve',)), *options], env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, errors='replace',
                                timeout=TIMEOUT)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if result.returncode:
        return None
    for line in result.stdout.splitlines():
        for kind in TYPES:
            if kind in line:
                return line.split(kind, 1)[1].strip()
    return ''


def add(wine, env, key, *args, log=subprocess.DEVNULL):
    """reg add KEY ARGS /f, its output to LOG; whether it succeeded."""
    try:
        return subprocess.run([wine, 'reg', 'add', key, *args, '/f'], env=env, stdout=log,
                              stderr=subprocess.STDOUT, timeout=TIMEOUT).returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False
