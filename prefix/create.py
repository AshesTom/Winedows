#!/usr/bin/env python3
"""Create a dedicated 64-bit (WoW64) prefix from locally extracted XP files."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
import registry
import stage
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'extract'))
import media


def create(assets, dest, home, wine):
    assets, dest, home = assets.resolve(strict=True), dest.resolve(), home.resolve(strict=True)
    if dest.exists():
        raise SystemExit(f'Refusing existing prefix: {dest}')
    wine_path = Path(shutil.which(wine) or wine).resolve(strict=True)
    lib = wine_path.parent.parent / 'lib/wine'
    if not all((lib / arch / 'win4wine-controls.dll').is_file() for arch in ('i386-windows', 'x86_64-windows')):
        raise SystemExit('Build the patched WoW64 Wine first: missing self-built themed controls modules')
    theme_helper = Path(__file__).resolve().parents[1] / '.local/bridge/theme-settings.exe'
    if not theme_helper.is_file():
        raise SystemExit('Run bridge-wm/build.sh first: missing self-built theme settings helper')
    records = json.loads((assets / 'manifest.json').read_text())
    if not records or not (assets / 'windows/explorer.exe').is_file():
        raise SystemExit('Incomplete XP assets')
    for name in ('browselc.dll', 'shdoclc.dll'):
        if not (assets / 'system32' / name).is_file():
            raise SystemExit(f'Missing {name}: re-extract your local XP media')
    # Japanese or English XP: the prefix takes the media's language.
    language = media.language(assets)
    for name in ('fonts/msgothic.ttc',) * (language == 'ja') + ('themes/Luna/luna.msstyles',):
        if not (assets / name).is_file():
            raise SystemExit(f'Missing {name}: re-extract local media for the classic baseline')
    dest.parent.mkdir(parents=True, exist_ok=True)
    # The XP shell runs as a 32-bit process; 64-bit applications use Wine's own DLLs.
    # Wine names the prefix's folders and sets its code page from the Unix
    # locale, which is the session's (session.json "locale").
    locale = media.LOCALES[language]
    env = dict(os.environ, WINEPREFIX=str(dest), WINEARCH='win64', LANG=locale, LC_ALL=locale,
               WINEDLLOVERRIDES='mscoree,mshtml=;uxtheme=b', WINEDEBUG=os.environ.get('WINEDEBUG', '-all'))
    def run(*args):
        subprocess.run([wine, *registry.shell_view(args)], env=env, check=True)
    def exists(key, name):
        return subprocess.run([wine, *registry.shell_view(('reg', 'query', key, '/v', name))], env=env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    # Disable optional runtime installers: never obtain Microsoft binaries.
    run('wineboot', '-u')
    # A new prefix also runs Wine's own initial wineboot, which may still be
    # writing the 32-bit system directory; wait until every process ends.
    subprocess.run([str(wine_path.with_name('wineserver')), '-w'], env=env, check=True)
    system = stage.shell_system(dest)
    if system.name != 'syswow64':
        raise SystemExit('Expected a 64-bit prefix: use the WoW64 build of the patched Wine')
    xp = dest / 'drive_c/xp'
    xp.mkdir()
    for source, directory in stage.controls_targets(dest, wine_path):
        shutil.copyfile(source / 'win4wine-controls.dll', directory / 'win4wine-controls.dll')
    shutil.copyfile(assets / 'windows/explorer.exe', xp / 'xp-explorer.exe')
    native = xp / 'native'
    native.mkdir()
    for dll in (assets / 'system32').glob('*.dll'):
        shutil.copyfile(dll, native / dll.name)
    for font in (assets / 'fonts').glob('*'):
        shutil.copyfile(font, dest / 'drive_c/windows/Fonts' / font.name)
    if (assets / 'themes').exists():
        # Into Wine's folder: on Linux a "Resources" beside it would be a second one.
        shutil.copytree(assets / 'themes', dest / 'drive_c/windows/resources/themes', dirs_exist_ok=True)
    for manifest in (assets / 'sxs').rglob('controls.man'):
        identity = ET.parse(manifest).getroot().find('{urn:schemas-microsoft-com:asm.v1}assemblyIdentity')
        dll = manifest.with_name('comctl32.dll')
        if identity is None or not dll.is_file():
            continue
        attrs = identity.attrib
        if attrs.get('processorArchitecture') != 'x86' or attrs.get('name') != 'Microsoft.Windows.Common-Controls':
            continue
        version = attrs['version']
        if version == '6.0.0.0':
            continue  # Prefer the SP3 assembly; avoid an exact-version legacy match.
        if any(ch not in '0123456789.' for ch in version):
            raise SystemExit('Invalid SxS version')
        name = f'x86_microsoft.windows.common-controls_6595b64144ccf1df_{version}_none_deadbeef'
        assembly = dest / 'drive_c/windows/winsxs' / name
        assembly.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(dll, assembly / 'comctl32.dll')
        # Native XP toolbars depend on private messages absent from builtin controls.
        if version == '6.0.2600.5512':
            shutil.copyfile(dll, xp / 'comctl32.dll')
        shutil.copyfile(manifest, assembly.parent / 'manifests' / (name + '.manifest'))
    (dest / 'dosdevices/h:').symlink_to(home)
    # Preserve Wine's generated namespace registrations. Add only explicit XP settings.
    def reg(key, name, value, kind='REG_SZ'):
        run('reg', 'add', key, '/v', name, '/t', kind, '/d', value, '/f')
    reg(r'HKCU\Software\Wine', 'Version', 'winxp')
    for dll in ('mscoree', 'mshtml'):
        reg(r'HKCU\Software\Wine\DllOverrides', dll, '')
    for dll in ('shell32', 'shdocvw', 'browseui', 'shlwapi', 'comctl32', 'linkinfo', 'browselc', 'shdoclc'):
        reg(r'HKCU\Software\Wine\DllOverrides', dll, 'builtin')
        reg(r'HKCU\Software\Wine\AppDefaults\xp-explorer.exe\DllOverrides', dll, 'native')
        shutil.copyfile(native / (dll + '.dll'), system / (dll + '.dll'))
    reg(r'HKCU\Software\Wine\DllOverrides', 'win4wine-controls', 'builtin')
    registry.install(run, exists, language)
    registry.install_classic_fonts(run, exists, language)
    # Let the font's GASP policy apply to synthetic bold/italic too. This is a
    # prefix-wide Wine setting; preserve any existing explicit preference.
    if not exists(r'HKCU\Software\Wine\Fonts', 'AntialiasFakeBoldOrItalic'):
        reg(r'HKCU\Software\Wine\Fonts', 'AntialiasFakeBoldOrItalic', 'N')
    reg(r'HKCU\Software\Wine\X11 Driver', 'Managed', 'Y')
    reg(r'HKCU\Software\Wine\X11 Driver', 'Decorated', 'N')
    for suffix in ('Shell Folders', 'User Shell Folders'):
        reg('HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\' + suffix,
            'Personal', 'H:\\')
    # Restore Wine's saved classic metrics too; ThemeActive alone leaves Luna's
    # taller caption metrics installed after wineboot's default theme.
    subprocess.run([wine, str(theme_helper), 'classic-initialize'],
                   env=dict(env, WIN4WINE_EXTERNAL_SHELL='1'), check=True)
    reg(r'HKCU\Software\Microsoft\Windows\CurrentVersion\Policies\Explorer', 'NoSimpleStartMenu', '1', 'REG_DWORD')
    reg(r'HKCU\Control Panel\Desktop', 'LogPixels', '96', 'REG_DWORD')
    reg(r'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced', 'WebView', '0', 'REG_DWORD')
    # The profile follows this Wine update of the prefix (see stage.py).
    update = (dest / '.update-timestamp').read_text().strip()
    (dest / '.xp-registry-version').write_text(f'{registry.VERSION} {update}\n')
    (dest / 'xp-shell.json').write_text(json.dumps({'assets': str(assets), 'home': str(home),
                                                   'wine': wine, 'profile': 'shell', 'language': language},
                                                  indent=2) + '\n')
    # The running wineserver keeps the system DLLs it mapped at its start as
    # KnownDlls: Wine's own shell32 and shlwapi, not the XP ones copied above.
    # Let it end, so the next program (the Start menu setting) gets XP's.
    subprocess.run([str(wine_path.with_name('wineserver')), '-w'], env=env, check=True)
    print(f'Prefix ready: {dest}')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assets', type=Path)
    parser.add_argument('prefix', type=Path)
    parser.add_argument('--home', type=Path, default=Path.home())
    parser.add_argument('--wine', default=os.environ.get('WINE', 'wine'))
    args = parser.parse_args()
    create(args.assets, args.prefix, args.home, args.wine)
