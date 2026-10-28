#!/usr/bin/env python3
"""Crash the Windows 7 shell in a disposable X session and check visible recovery."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
from Xlib import X, display, error

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('config', type=Path, help='supervisor config for the disposable session')
args = parser.parse_args()
config = json.loads(args.config.read_text())
prefix = Path(config['prefix']).resolve(strict=True)
if not (prefix / 'xp-shell.json').is_file():
    raise SystemExit('Expected a dedicated test prefix')
state = Path(config.get('state', str(Path.home() / '.local/state/win4wine'))) / 'status.json'
env = dict(os.environ, WINEPREFIX=str(prefix),
           WINEDLLOVERRIDES='mscoree,mshtml=', WINEDEBUG='-all')
env.pop('WINEARCH', None)  # The existing prefix decides.
d = display.Display()

def wait(predicate, label, timeout=60):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        if predicate():
            return
        time.sleep(.2)
    raise AssertionError(label)

def status():
    return json.loads(state.read_text())

def visible_recovery():
    browser = terminal = False
    for w in d.screen().root.query_tree().children:
        try:
            if w.get_attributes().map_state != X.IsViewable:
                continue
            p = w.get_full_property(d.intern_atom('_WIN4WINE_CLASS'), X.AnyPropertyType)
            browser |= bool(p and bytes(p.value).rstrip(b'\0').lower() == b'explorerwclass')
            terminal |= 'XTerm' in (w.get_wm_class() or ())
        except error.XError:
            pass
    return browser and terminal

try:
    wait(lambda: status()['state'] == 'xp-shell', 'initial shell readiness')
    for count in range(config.get('failures', 3)):
        previous = status().get('attempt', 0)
        subprocess.run([config['wine'], 'taskkill', '/IM', 'w7-explorer.exe', '/F'],
                       env=env, timeout=10, check=True)
        wait(lambda: status()['state'] == 'fallback' or
             status()['state'] == 'xp-shell' and status().get('attempt', 0) > previous,
             'bounded restart or recovery')
        print(status(), flush=True)
        if status()['state'] == 'fallback':
            assert count == config.get('failures', 3) - 1
            break
    assert status()['state'] == 'fallback'
    wait(visible_recovery, 'mapped builtin browser and native recovery terminal')
    # First-run Wine registry persistence can follow the initial mapped windows.
    wait(lambda: (prefix / 'recovery/user.reg').is_file(), 'recovery registry persistence')
    assert not (prefix / 'recovery/drive_c/xp').exists()
    print('PASS: bounded shell restarts, independent builtin browser, native terminal')
finally:
    d.close()
