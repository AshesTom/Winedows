#!/usr/bin/env python3
"""Exercise fresh-only font policy seeding and explicit display switches."""
import importlib.util
import json
from pathlib import Path
import runpy
import sys
import tempfile
import unittest
from unittest.mock import patch
import subprocess

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'prefix'))
spec = importlib.util.spec_from_file_location('prefix_create', ROOT / 'prefix/create.py')
create = importlib.util.module_from_spec(spec)
spec.loader.exec_module(create)


class FontRendering(unittest.TestCase):
    def test_fresh_seed_preserves_existing_preference(self):
        for existing in (None, 'Y', 'custom'):
            with self.subTest(existing=existing), tempfile.TemporaryDirectory() as temporary:
                base = Path(temporary)
                assets, dest = base / 'assets', base / 'prefix'
                wine = base / 'runtime/bin/wine'
                wine.parent.mkdir(parents=True)
                wine.touch()
                for arch in ('i386-windows', 'x86_64-windows'):
                    controls = wine.parent.parent / 'lib/wine' / arch / 'win4wine-controls.dll'
                    controls.parent.mkdir(parents=True)
                    controls.touch()
                files = ['windows/explorer.exe', 'fonts/msgothic.ttc', 'themes/Luna/luna.msstyles']
                files += ['system32/' + name + '.dll' for name in
                          ('shell32', 'shdocvw', 'browseui', 'shlwapi', 'comctl32', 'linkinfo', 'browselc', 'shdoclc')]
                for name in files:
                    target = assets / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.touch()
                (assets / 'manifest.json').write_text(json.dumps([{'fixture': True}]))
                writes = []

                def fake_run(command, **kwargs):
                    if command[1:3] == ['wineboot', '-u']:
                        for directory in ('drive_c/windows/system32', 'drive_c/windows/syswow64',
                                          'drive_c/windows/Fonts', 'dosdevices'):
                            (dest / directory).mkdir(parents=True, exist_ok=True)
                        (dest / '.update-timestamp').write_text('0\n')
                    if command[1:3] == ['reg', 'query']:
                        return subprocess.CompletedProcess(command, 0 if existing is not None else 1)
                    if command[1:3] == ['reg', 'add'] and 'AntialiasFakeBoldOrItalic' in command:
                        writes.append(command[command.index('/d') + 1])
                    return subprocess.CompletedProcess(command, 0)

                with patch.object(create.subprocess, 'run', side_effect=fake_run), \
                     patch.object(create.media, 'language', return_value='en'):
                    create.create(assets, dest, base, str(wine))
                self.assertEqual(writes, ['N'] if existing is None else [])

    def test_display_explicit_only(self):
        for option, expected in ((None, []), ('xp', ['N']), ('wine', ['Y'])):
            with self.subTest(option=option), tempfile.TemporaryDirectory() as temporary:
                prefix = Path(temporary)
                (prefix / 'xp-shell.json').write_text('{}')
                commands = []
                argv = ['display.py'] + ([] if option is None else ['--font-rendering', option])
                with patch.dict('os.environ', {'WINEPREFIX': str(prefix), 'WINE': 'fixture-wine'}), \
                     patch.object(sys, 'argv', argv), \
                     patch('subprocess.run', side_effect=lambda cmd, **kw: commands.append(cmd)):
                    runpy.run_path(str(ROOT / 'prefix/display.py'), run_name='__main__')
                writes = [cmd[cmd.index('/d') + 1] for cmd in commands if 'AntialiasFakeBoldOrItalic' in cmd]
                self.assertEqual(writes, expected)
                self.assertFalse(any('FontSmoothing' in cmd for cmd in commands))

    def test_font_smoothing(self):
        for option, kind in ((None, None), ('standard', '1'), ('cleartype', '2')):
            with self.subTest(option=option), tempfile.TemporaryDirectory() as temporary:
                prefix = Path(temporary)
                (prefix / 'xp-shell.json').write_text('{}')
                commands = []
                argv = ['display.py'] + ([] if option is None else ['--font-smoothing', option])
                with patch.dict('os.environ', {'WINEPREFIX': str(prefix), 'WINE': 'fixture-wine'}), \
                     patch.object(sys, 'argv', argv), \
                     patch('subprocess.run', side_effect=lambda cmd, **kw: commands.append(cmd)):
                    runpy.run_path(str(ROOT / 'prefix/display.py'), run_name='__main__')
                values = {cmd[cmd.index('/v') + 1]: cmd[cmd.index('/d') + 1] for cmd in commands}
                if option is None:
                    self.assertEqual(values, {})
                else:
                    self.assertEqual(values, {'FontSmoothing': '2', 'FontSmoothingType': kind,
                                              'FontSmoothingOrientation': '1'})

    def test_font_smoothing_xp_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            prefix = Path(temporary)
            (prefix / 'reactos-shell.json').write_text('{}')
            with patch.dict('os.environ', {'WINEPREFIX': str(prefix)}), \
                 patch.object(sys, 'argv', ['display.py', '--font-smoothing', 'cleartype']), \
                 self.assertRaises(SystemExit):
                runpy.run_path(str(ROOT / 'prefix/display.py'), run_name='__main__')


if __name__ == '__main__':
    unittest.main()
