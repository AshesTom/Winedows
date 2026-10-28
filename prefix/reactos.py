#!/usr/bin/env python3
"""Create or stage a dedicated 64-bit (WoW64) prefix for ReactOS's shell.

  reactos.py create ASSETS PREFIX [--home DIR] [--wine WINE] [--lang ja|en] [--theme THEME]
  reactos.py stage PREFIX [--wine WINE]
  reactos.py theme PREFIX THEME [--wine WINE]

THEME is classic (ReactOS's default classic look, the default) or one of
ReactOS's visual styles: lautus, lunar or mizu.

ASSETS comes from extract/reactos.py. ReactOS's explorer runs as
C:\\ros\\ros-explorer.exe, a 32-bit process; it loads ReactOS's shell DLLs,
which are placed in syswow64 (several are KnownDlls, so the explorer's own
directory is not searched). The prefix keeps Wine's DLLs for every other
program (DllOverrides builtin) and gives ros-explorer.exe the native ones
(AppDefaults), as prefix/create.py does for XP's explorer. The session runs
"stage" before each start: a Wine update of the prefix replaces the DLLs and
the shell's registrations, and Wine rewrites the font links when the prefix
starts under another locale.
"""
import argparse
import filecmp
import json
import os
from pathlib import Path
import pwd
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MARKER = 'reactos-shell.json'
# Bump when the registry settings below change; stage then applies them again.
VERSION = 5
# Loaded natively by ros-explorer.exe only; Wine has builtin modules of these names.
SHELL_DLLS = ('shell32', 'browseui', 'shdocvw', 'shlwapi', 'comctl32', 'zipfldr')
# ReactOS-only modules (no Wine builtin of the name): ros-explorer.exe loads them by name.
EXTRA_DLLS = ('msgina',)
SXS = 'x86_microsoft.windows.common-controls_6595b64144ccf1df_6.0.2600.2982_none_deadbeef'
LOCALES = {'ja': 'ja_JP.UTF-8', 'en': 'en_US.UTF-8'}
# The code pages Wine records for each language (locale_current)
CODEPAGES = {'ja': '932,932', 'en': '1252,437'}
# ReactOS's visual styles: directory and file, color and size names (their COLORNAMES and SIZENAMES).
THEMES = {'lautus': ('Lautus', 'Normal Color', 'Normal Size'), 'lunar': ('Lunar', 'ReactOS', 'Normal Size'),
          'mizu': ('Mizu', 'Normal Color', 'Normal Size')}

# ReactOS's default classic colors (win32ss/gdi/ntgdi/stockobj.c), the
# "Windows Standard" scheme of Windows 2000, by their registry names.
COLORS = (
    ('Scrollbar', (212, 208, 200)), ('Background', (58, 110, 165)), ('ActiveTitle', (10, 36, 106)),
    ('InactiveTitle', (128, 128, 128)), ('Menu', (212, 208, 200)), ('Window', (255, 255, 255)),
    ('WindowFrame', (0, 0, 0)), ('MenuText', (0, 0, 0)), ('WindowText', (0, 0, 0)),
    ('TitleText', (255, 255, 255)), ('ActiveBorder', (212, 208, 200)), ('InactiveBorder', (212, 208, 200)),
    ('AppWorkSpace', (128, 128, 128)), ('Hilight', (10, 36, 106)), ('HilightText', (255, 255, 255)),
    ('ButtonFace', (212, 208, 200)), ('ButtonShadow', (128, 128, 128)), ('GrayText', (128, 128, 128)),
    ('ButtonText', (0, 0, 0)), ('InactiveTitleText', (212, 208, 200)), ('ButtonHilight', (255, 255, 255)),
    ('ButtonDkShadow', (64, 64, 64)), ('ButtonLight', (212, 208, 200)), ('InfoText', (0, 0, 0)),
    ('InfoWindow', (255, 255, 225)), ('ButtonAlternateFace', (181, 181, 181)), ('HotTrackingColor', (0, 0, 128)),
    ('GradientActiveTitle', (166, 202, 240)), ('GradientInactiveTitle', (192, 192, 192)),
    ('MenuHilight', (49, 106, 197)), ('MenuBar', (236, 233, 216)),
)
# ReactOS's default window metrics (boot/bootdata/hivedef.inf): Tahoma 8pt,
# bold captions, 18-pixel captions and menus.
METRICS = (('CaptionWidth', '18'), ('CaptionHeight', '18'), ('SmCaptionWidth', '12'), ('SmCaptionHeight', '15'),
           ('MenuWidth', '18'), ('MenuHeight', '18'), ('ScrollWidth', '16'), ('ScrollHeight', '16'),
           ('BorderWidth', '1'), ('IconSpacing', '75'), ('IconVerticalspacing', '75'))
FONTS = (('CaptionFont', 700), ('SmCaptionFont', 400), ('MenuFont', 400), ('StatusFont', 400),
         ('MessageFont', 400), ('IconFont', 400))
# The user interface fonts that the CJK fallback is linked to.
LINKED_FONTS = ('Tahoma', 'MS Shell Dlg', 'MS Shell Dlg 2', 'Microsoft Sans Serif', 'Segoe UI', 'MS UI Gothic')
# Wine's own desktop items that ReactOS's desktop does not have: Internet
# Explorer (Wine's is not set up in this prefix) and the Unix root folder.
# The Start menu folders that ReactOS names in the user's language (userenv's
# IDS_STARTUP, shell32's IDS_ADMINTOOLS) and Wine in English: Wine's name, which
# is also the user's value in Shell Folders, and the common folder's value.
MENU_FOLDERS = (('StartUp', 'Common StartUp'), ('Administrative Tools', 'Common Administrative Tools'))
MENU_FOLDER_NAMES = {'ja': ('スタートアップ', '管理ツール')}
HIDDEN_DESKTOP_ITEMS = ('{871C5380-42A0-1069-A2EA-08002B30309D}', '{9D20AAE8-0625-44B0-9CA7-71889C2254D9}')


def logfont(height, weight, face='Tahoma'):
    return struct.pack('<5l8B64s', height, 0, 0, 0, weight, 0, 0, 0, 1, 0, 0, 0, 0,
                       face.encode('utf-16le').ljust(64, b'\0')).hex()


class Prefix:
    def __init__(self, path, wine, lang):
        self.path = path
        self.wine = Path(shutil.which(str(wine)) or wine).resolve(strict=True)
        self.lang = lang
        locale = LOCALES[lang]
        self.env = dict(os.environ, WINEPREFIX=str(path), WINEDEBUG=os.environ.get('WINEDEBUG', '-all'),
                        WINEDLLOVERRIDES='mscoree,mshtml=', LANG=locale, LC_ALL=locale)
        self.env.pop('WINEARCH', None)

    @property
    def system(self):
        return self.path / 'drive_c/windows/syswow64'

    def run(self, *args, timeout=300, check=True, env=None):
        return subprocess.run([str(self.wine), *args], env=env or self.env, check=check, timeout=timeout,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def reg(self, key, name, value, kind='REG_SZ', view=None):
        args = ['reg', 'add', key, *(('/ve',) if name is None else ('/v', name)), '/t', kind, '/d', str(value), '/f']
        self.run(*args, *((f'/reg:{view}',) if view else ()))

    def wait(self):
        server = str(self.wine.with_name('wineserver'))
        subprocess.run([server, '-w'], env=self.env, check=False, timeout=120)

    def stop(self):
        server = str(self.wine.with_name('wineserver'))
        subprocess.run([server, '-k'], env=self.env, check=False, timeout=15)
        self.wait()

    def place(self, source, target):
        """Copy SOURCE over TARGET unless equal, without truncating a mapped image."""
        if target.is_file() and filecmp.cmp(source, target, shallow=False):
            return
        target.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=target.parent, prefix='.ros-stage-', delete=False) as temp:
            path = Path(temp.name)
        try:
            shutil.copyfile(source, path)
            os.replace(path, target)
        finally:
            path.unlink(missing_ok=True)

    def place_files(self):
        native = self.path / 'drive_c/ros/native'
        for name in SHELL_DLLS + EXTRA_DLLS:
            self.place(native / f'{name}.dll', self.system / f'{name}.dll')
        # ReactOS's Common Controls 6 beside the shell, which takes it from
        # there (Wine 0143); the assembly stays Wine's, for every other 32-bit
        # program (a replaced one gave them Wine's version 5, without buttons
        # or edit controls).
        self.place(native / 'sxs/comctl32.dll', self.path / 'drive_c/ros/comctl32.dll')
        wine_controls = self.wine.parent.parent / 'lib/wine/i386-windows/comctl32_v6.dll'
        assembly = self.path / f'drive_c/windows/winsxs/{SXS}/comctl32.dll'
        if wine_controls.is_file() and assembly.is_file():
            self.place(wine_controls, assembly)

    def registrations(self):
        """What a Wine update of the prefix rewrites or a locale change resets."""
        # Without SDL, Wine's controller driver reads controllers through udev
        # instead of waking up a thousand times a second (prefix/registry.py).
        self.reg(r'HKLM\System\CurrentControlSet\Services\WineBus', 'Enable SDL', 0, 'REG_DWORD')
        for name in SHELL_DLLS:
            self.reg(r'HKCU\Software\Wine\DllOverrides', name, 'builtin')
            self.reg(r'HKCU\Software\Wine\AppDefaults\ros-explorer.exe\DllOverrides', name, 'native')
        # ReactOS's explorer becomes the shell only when Winlogon names it (explorer.cpp).
        for view in (32, 64):
            self.reg(r'HKLM\Software\Microsoft\Windows NT\CurrentVersion\Winlogon', 'Shell', 'ros-explorer.exe',
                     view=view)
        # ReactOS's own registrations (class objects, icons, folders), written
        # by its DLLs into the 32-bit view. Their manifests lead to the Common
        # Controls 6 assembly, which is Wine's (see place_files).
        key = r'HKCU\Software\Wine\AppDefaults\regsvr32.exe\DllOverrides'
        for name in SHELL_DLLS:
            if name != 'comctl32':
                self.reg(key, name, 'native')
        try:
            for name in ('shell32', 'browseui', 'shdocvw'):
                self.run(r'C:\windows\syswow64\regsvr32.exe', '/s', f'C:\\windows\\system32\\{name}.dll')
        finally:
            self.run('reg', 'delete', r'HKCU\Software\Wine\AppDefaults\regsvr32.exe', '/f', check=False)
        # Folders open in ReactOS's explorer (boot/bootdata/hivecls.inf); a new
        # process hands the window to the running shell.
        self.reg(r'HKCR\Folder\shell\open\command', None, r'C:\ros\ros-explorer.exe "%1"')
        self.reg(r'HKCR\Folder\shell\explore\command', None, r'C:\ros\ros-explorer.exe /e,"%1"')
        # Wine's ShellExecute runs executables itself; ReactOS's needs their
        # association, or a shortcut to a program opens its first menu item.
        for extension in ('.exe', '.com'):
            self.reg('HKCR\\' + extension, None, 'exefile')
        # Wine has no help viewer for Help and Support.
        self.reg(r'HKCU\Software\Microsoft\Windows\CurrentVersion\Policies\Explorer', 'NoSMHelp', 1, 'REG_DWORD')
        self.menu_folders()
        # Wine rewrites the system links for the prefix's locale.
        if self.lang == 'ja':
            for face in LINKED_FONTS:
                for view in (32, 64):
                    self.reg(r'HKLM\Software\Microsoft\Windows NT\CurrentVersion\FontLink\SystemLink', face,
                             'DroidSansFallback.ttf,Droid Sans Fallback', 'REG_MULTI_SZ', view=view)

    def menu_folders(self):
        """Give the Start menu folders ReactOS's names in the prefix's language.

        Wine reads their paths from User Shell Folders, so it keeps using them."""
        names = MENU_FOLDER_NAMES.get(self.lang)
        if not names:
            return
        user = pwd.getpwuid(os.getuid()).pw_name
        roots = ((f'users/{user}/AppData/Roaming', r'%USERPROFILE%\AppData\Roaming', 'HKCU', False),
                 ('ProgramData', '%ProgramData%', 'HKLM', True))
        for (english, common), name in zip(MENU_FOLDERS, names):
            for root, variable, hive, is_common in roots:
                programs = self.path / 'drive_c' / root / 'Microsoft/Windows/Start Menu/Programs'
                if (programs / english).is_dir() and not (programs / name).exists():
                    (programs / english).rename(programs / name)
                relative = '\\Microsoft\\Windows\\Start Menu\\Programs\\' + name
                key = hive + r'\Software\Microsoft\Windows\CurrentVersion\Explorer'
                value_name = common if is_common else english
                self.reg(key + r'\User Shell Folders', value_name, variable + relative, 'REG_EXPAND_SZ')
                self.reg(key + r'\Shell Folders', value_name,
                         'C:\\' + root.replace('/', '\\') + relative)

    def theme(self, name):
        """Turn visual styles off (classic) or apply one of ReactOS's."""
        helper = ROOT / '.local/bridge/theme-settings.exe'
        if not helper.is_file():
            raise SystemExit('Run bridge-wm/build.sh first: missing self-built theme settings helper')
        if name == 'classic':
            args = ['classic']
        else:
            directory, color, size = THEMES[name]
            path = f'C:\\windows\\Resources\\Themes\\{directory}\\{directory.lower()}.msstyles'
            if not (self.path / 'drive_c/windows/Resources/Themes' / directory / f'{directory.lower()}.msstyles').is_file():
                raise SystemExit(f'Missing the {directory} theme: extract the ReactOS files again')
            args = ['msstyles', path, color, size]
        subprocess.run([str(self.wine), str(helper), *args], env=dict(self.env, WIN4WINE_EXTERNAL_SHELL='1'),
                       check=True, stdout=subprocess.DEVNULL, timeout=300)

    def appearance(self, theme='classic'):
        """ReactOS's classic look, as a new ReactOS user gets it with the Classic theme.

        Only a new prefix gets it; the user may change these settings later."""
        # The classic colors and metrics are the base under any visual style too.
        self.theme('classic')
        for name, (r, g, b) in COLORS:
            self.reg(r'HKCU\Control Panel\Colors', name, f'{r} {g} {b}')
        metrics = r'HKCU\Control Panel\Desktop\WindowMetrics'
        for name, value in METRICS:
            self.reg(metrics, name, value)
        for name, weight in FONTS:
            self.reg(metrics, name, logfont(-11, weight), 'REG_BINARY')
        desktop = r'HKCU\Control Panel\Desktop'
        self.reg(desktop, 'Wallpaper', r'C:\windows\Web\Wallpaper\Nebula.bmp')
        self.reg(desktop, 'WallpaperStyle', '10')
        self.reg(desktop, 'TileWallpaper', '0')
        self.reg(desktop, 'FontSmoothing', '0')
        explorer = r'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer'
        for name, value in (('StartMenuLogoff', 1), ('HideFileExt', 0), ('ListviewShadow', 1)):
            self.reg(explorer + r'\Advanced', name, value, 'REG_DWORD')
        self.reg(explorer, 'Logon User Name', pwd.getpwuid(os.getuid()).pw_name)
        for clsid in HIDDEN_DESKTOP_ITEMS:
            self.reg(explorer + r'\HideDesktopIcons\ClassicStartMenu', clsid, 1, 'REG_DWORD')
        if theme != 'classic':
            self.wait()
            self.theme(theme)


def quick_launch(dest):
    """The Quick Launch folder, as ReactOS's setup makes it (media/inf/shortcuts.inf).

    Without it the taskbar's Toolbars menu cannot create the Quick Launch
    toolbar ("Cannot create toolbar"). ReactOS 0.4.16's explorer does not keep
    its toolbars: the toolbar is added again in each session."""
    user = pwd.getpwuid(os.getuid()).pw_name
    folder = dest / f'drive_c/users/{user}/AppData/Roaming/Microsoft/Internet Explorer/Quick Launch'
    if not folder.exists() and not folder.is_symlink():
        folder.mkdir(parents=True)


def copy_themes(assets, dest):
    """ReactOS's visual styles from the extracted files, where Windows keeps themes."""
    for directory, _, _ in THEMES.values():
        source = Path(assets) / 'themes' / directory / f'{directory.lower()}.msstyles'
        if source.is_file():
            target = dest / 'drive_c/windows/Resources/Themes' / directory
            target.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target / source.name)


def create(assets, dest, home, wine, lang, theme='classic'):
    assets, dest, home = assets.resolve(strict=True), dest.resolve(), home.resolve(strict=True)
    if dest.exists():
        raise SystemExit(f'Refusing existing prefix: {dest}')
    manifest = json.loads((assets / 'manifest.json').read_text())
    prefix = Prefix(dest, wine, lang)
    dest.parent.mkdir(parents=True, exist_ok=True)
    prefix.run('wineboot', '-u', timeout=900, env=dict(prefix.env, WINEARCH='win64'))
    # A new prefix's own first update may still be writing the 32-bit system directory.
    prefix.wait()
    if not prefix.system.is_dir():
        raise SystemExit('Expected a 64-bit prefix: use the WoW64 build of the patched Wine')
    ros = dest / 'drive_c/ros'
    native = ros / 'native'
    (native / 'sxs').mkdir(parents=True)
    shutil.copyfile(assets / 'shell/ros-explorer.exe', ros / 'ros-explorer.exe')
    for name in SHELL_DLLS + EXTRA_DLLS:
        shutil.copyfile(assets / f'system32/{name}.dll', native / f'{name}.dll')
    shutil.copyfile(assets / 'sxs/comctl32.dll', native / 'sxs/comctl32.dll')
    fonts = dest / 'drive_c/windows/Fonts'
    for name in ('tahoma.ttf', 'tahomabd.ttf', 'DroidSansFallback.ttf'):
        shutil.copyfile(assets / 'fonts' / name, fonts / name)
    # Wine has a Marlett of its own; ReactOS's has the glyphs its menus use.
    shutil.copyfile(assets / 'fonts/Marlett.ttf', fonts / 'ros-marlett.ttf')
    copy_themes(assets, dest)
    quick_launch(dest)
    wallpaper = dest / 'drive_c/windows/Web/Wallpaper'
    wallpaper.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(assets / 'wallpaper/Nebula.bmp', wallpaper / 'Nebula.bmp')
    (dest / 'dosdevices/h:').symlink_to(home)
    try:
        prefix.reg(r'HKCU\Software\Wine', 'Version', 'win2003')
        for suffix in ('Shell Folders', 'User Shell Folders'):
            prefix.reg('HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\' + suffix, 'Personal', 'H:\\')
        prefix.reg(r'HKCU\Software\Wine\X11 Driver', 'Managed', 'Y')
        prefix.reg(r'HKCU\Software\Wine\X11 Driver', 'Decorated', 'N')
        prefix.reg(r'HKCU\Control Panel\Desktop', 'LogPixels', 96, 'REG_DWORD')
        prefix.wait()
        prefix.place_files()
        prefix.registrations()
        prefix.appearance(theme)
    finally:
        prefix.stop()
    update = (dest / '.update-timestamp').read_text().strip()
    (dest / '.ros-registry-version').write_text(f'{VERSION} {update}\n')
    (dest / MARKER).write_text(json.dumps({'assets': str(assets), 'home': str(home), 'wine': str(prefix.wine),
                                           'lang': lang, 'reactos': manifest['reactos']}, indent=2) + '\n')
    print(f'Prefix ready: {dest}')


def registry_values(path, key):
    """The values of KEY (as a .reg section names it) in a prefix's registry file."""
    values, section = {}, False
    try:
        lines = path.read_text(encoding='utf-8', errors='replace').splitlines()
    except OSError:
        return values
    for line in lines:
        if line.startswith('['):
            section = line.startswith(f'[{key}]')
        elif section and line.startswith('"') and '"=' in line:
            name, value = line[1:].split('"=', 1)
            values[name] = value
    return values


def locale_current(dest, lang):
    """Whether Wine's fonts are still set up for the prefix's language.

    Wine rewrites the font links (Tahoma's among them) when the prefix starts
    under other code pages, its record of which is HKCU\\Software\\Wine\\Fonts
    "Codepages". The next start in the prefix's own locale rewrites them again
    to Wine's, so the links themselves are checked too."""
    fonts = registry_values(dest / 'user.reg', r'Software\\Wine\\Fonts')
    if fonts.get('Codepages') != f'"{CODEPAGES[lang]}"':
        return False
    if lang == 'ja':
        links = registry_values(dest / 'system.reg',
                                r'Software\\Microsoft\\Windows NT\\CurrentVersion\\FontLink\\SystemLink')
        return all('DroidSansFallback' in links.get(face, '') for face in LINKED_FONTS)
    return True


def stage(dest, wine=None):
    config = json.loads((dest / MARKER).read_text())
    prefix = Prefix(dest, wine or config['wine'], config.get('lang', 'en'))
    inf = prefix.wine.parent.parent / 'share/wine/wine.inf'
    timestamp = dest / '.update-timestamp'
    version = dest / '.ros-registry-version'
    current = timestamp.read_text().strip() if timestamp.is_file() else ''
    wine_current = inf.is_file() and current == str(int(inf.stat().st_mtime))
    applied = version.read_text().split() if version.is_file() else []
    lang = config.get('lang', 'en')
    quick_launch(dest)
    if wine_current and applied == [str(VERSION), current] and locale_current(dest, lang):
        # Nothing changed, but a first wineboot or an update may have replaced files.
        prefix.place_files()
        return
    print('Updating the ReactOS shell prefix', flush=True)
    try:
        if not wine_current:
            prefix.run('wineboot', '-u', timeout=900)
            prefix.wait()
        prefix.place_files()
        prefix.registrations()
        current = timestamp.read_text().strip() if timestamp.is_file() else ''
        temp = version.with_suffix('.tmp')
        temp.write_text(f'{VERSION} {current}\n')
        temp.replace(version)
    finally:
        prefix.stop()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    p = commands.add_parser('create')
    p.add_argument('assets', type=Path)
    p.add_argument('prefix', type=Path)
    p.add_argument('--home', type=Path, default=Path.home())
    p.add_argument('--wine', default=os.environ.get('WINE', 'wine'))
    p.add_argument('--lang', choices=sorted(LOCALES), default='en')
    p.add_argument('--theme', choices=['classic', *sorted(THEMES)], default='classic')
    p = commands.add_parser('stage')
    p.add_argument('prefix', type=Path)
    p.add_argument('--wine')
    p = commands.add_parser('theme')
    p.add_argument('prefix', type=Path)
    p.add_argument('name', choices=['classic', *sorted(THEMES)])
    p.add_argument('--wine')
    args = parser.parse_args()
    if args.command == 'create':
        create(args.assets, args.prefix, args.home, args.wine, args.lang, args.theme)
    else:
        path = args.prefix.resolve(strict=True)
        if not (path / MARKER).is_file():
            raise SystemExit('Not a ReactOS shell prefix')
        if args.command == 'stage':
            stage(path, args.wine)
        else:
            config = json.loads((path / MARKER).read_text())
            copy_themes(config['assets'], path)
            prefix = Prefix(path, args.wine or config['wine'], config.get('lang', 'en'))
            try:
                prefix.theme(args.name)
            finally:
                prefix.stop()
