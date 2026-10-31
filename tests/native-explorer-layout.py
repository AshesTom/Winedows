#!/usr/bin/env python3
"""Link the Explorer UI probe against the current native-shell build, and run it."""
import os
from pathlib import Path
import shutil
import tempfile
import shlex
import subprocess
import sys
root = Path(__file__).resolve().parents[1]
build = root / '.local/native-shell-build'
if len(sys.argv) < 3:
    raise SystemExit('usage: native-explorer-layout.py OUTPUT_DIR FIXTURE_DIR [--verify]')
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
args = shlex.split((build / 'CMakeFiles/explorer.dir/link.txt').read_text())
args = [a for a in args if a != 'CMakeFiles/explorer.dir/src/main.cpp.o' and not a.startswith('-Wl,--dependency-file=')]
exe = out / 'explorer-layout-test'
args[args.index('-o') + 1] = str(exe)
args[1:1] = ['-std=c++17', '-fPIC', '-I' + str(root/'native-shell/src'), '-I' + str(root/'qt-style/src'), str(root/'tests/native-explorer-layout.cpp')] + shlex.split(subprocess.check_output(['pkg-config', '--cflags', 'Qt6Test', 'Qt6Widgets'], text=True))
args += shlex.split(subprocess.check_output(['pkg-config', '--libs', 'Qt6Test'], text=True))
subprocess.run(args, cwd=build, check=True)
with tempfile.TemporaryDirectory(prefix='settings-', dir=out) as config:
    dest = Path(config) / 'win4wine'
    dest.mkdir()
    source = Path(os.environ.get('XDG_CONFIG_HOME', Path.home()/'.config')) / 'win4wine/native-look.conf'
    if source.is_file():
        shutil.copy2(source, dest/'native-look.conf')
    else:
        (dest/'native-look.conf').write_text(f'style={root}/.local/native-assets/themes/Luna/luna.msstyles\ncolour=NormalColor\nsize=NormalSize\n')
    env = dict(os.environ, XDG_CONFIG_HOME=config)
    subprocess.run([str(exe), str(out), str(Path(sys.argv[2]).resolve()), *sys.argv[3:]], env=env, check=True)
