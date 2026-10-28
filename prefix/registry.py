"""Minimal native shell COM registrations absent from Wine's generated prefix."""
import os
from pathlib import Path
import pwd
import struct
import subprocess
import tempfile

VERSION = 31
FONT_PROFILE = r'HKCU\Software\Win4Wine\ThemeProfiles\JapaneseXPLuna'
# The classic fonts of a new prefix (prefix/theme-settings.c classic-initialize).
CLASSIC_FONT_PROFILE = r'HKCU\Software\Win4Wine\ThemeProfiles\XPClassic'
WINEBUS = r'HKLM\System\CurrentControlSet\Services\WineBus'
# Wine's Unix root folder, a desktop item of Wine's shell namespace
UNIX_ROOT = '{9D20AAE8-0625-44B0-9CA7-71889C2254D9}'
# The classic theme's fonts per language of the XP media, read with public SPI
# calls on the reference machines: Japanese XP uses MS UI Gothic 9pt, English
# XP Tahoma 8pt (captions bold in both).
CLASSIC_FONTS = {'ja': ('MS UI Gothic', -12), 'en': ('Tahoma', -11)}
# My Computer's names of the drives that are Linux folders, per language of the XP media.
DRIVE_LABELS = {'en': {'Z': 'Linux File System', 'H': 'Home Folder'},
                'ja': {'Z': 'Linux ファイル システム', 'H': 'ホーム フォルダ'}}


def shell_view(args):
    """Address the XP shell's registry view in `wine reg ...` arguments.

    The XP shell is a 32-bit process. In a 64-bit (WoW64) prefix it sees the
    32-bit view of HKCR\\CLSID and HKLM\\Software, while `wine reg` runs the
    64-bit reg.exe; HKCU and the rest of HKCR are shared by both views. In a
    32-bit prefix there is one view and the option changes nothing. Arguments
    that name a view already keep it."""
    if not args or args[0] != 'reg' or any(arg in ('/reg:32', '/reg:64') for arg in args):
        return args
    return (*args, '/reg:32')


class Batch:
    """install()'s run and exists for a prefix: its `reg add` and `reg delete`
    go in one regedit import per registry view (a `wine reg` each took 0.2 s,
    half a minute for the profile at each logon after a Wine update), and
    exists() asks `reg query` once per key. Any other command runs as it
    comes, after what was queued. flush() imports what is left."""
    ROOTS = {'HKCU': 'HKEY_CURRENT_USER', 'HKLM': 'HKEY_LOCAL_MACHINE', 'HKCR': 'HKEY_CLASSES_ROOT',
             'HKU': 'HKEY_USERS'}

    def __init__(self, wine, prefix, env, timeout=120):
        self.wine, self.env, self.timeout = str(wine), env, timeout
        # 32-bit regedit writes the XP shell's view (shell_view's /reg:32).
        wow64 = (Path(prefix) / 'drive_c/windows/syswow64').is_dir()
        self.regedit = {'32': r'C:\windows\syswow64\regedit.exe' if wow64 else 'regedit', '64': 'regedit'}
        self.queued = []        # (view, key, name, kind, data); kind None deletes the value
        self.known = {}         # (view, key) -> value names, lowercase

    def run(self, *args, timeout=None):
        args = shell_view(args)
        if args[:2] in (('reg', 'add'), ('reg', 'delete')) and ('/v' in args or '/ve' in args):
            view = '64' if '/reg:64' in args else '32'
            name = None if '/ve' in args else args[args.index('/v') + 1]
            kind = data = None
            if args[1] == 'add':
                kind = args[args.index('/t') + 1] if '/t' in args else 'REG_SZ'
                data = args[args.index('/d') + 1] if '/d' in args else ''
            self.queued.append((view, args[2], name, kind, data))
            return
        self.flush()
        subprocess.run([self.wine, *args], env=self.env, check=True, timeout=timeout or self.timeout,
                       stdout=subprocess.DEVNULL)

    __call__ = run

    def exists(self, key, name):
        view = '32'  # shell_view's
        for queued_view, queued_key, queued_name, kind, _ in reversed(self.queued):
            if (queued_view, queued_key.lower(), (queued_name or '').lower()) == (view, key.lower(), name.lower()):
                return kind is not None
        if (view, key.lower()) not in self.known:
            # Into a file: a wineserver that this command starts keeps a pipe open.
            with tempfile.TemporaryFile() as out:
                code = subprocess.run([self.wine, *shell_view(('reg', 'query', key))], env=self.env, stdout=out,
                                      stderr=subprocess.DEVNULL, timeout=self.timeout).returncode
                out.seek(0)
                text = out.read().decode('utf-8', 'replace')
            self.known[view, key.lower()] = value_names(text, key) if code == 0 else set()
        return name.lower() in self.known[view, key.lower()]

    def text(self, view):
        def esc(text):
            return text.replace('\\', '\\\\').replace('"', '\\"')
        out = ['Windows Registry Editor Version 5.00', '']
        for queued_view, key, name, kind, data in self.queued:
            if queued_view != view:
                continue
            root, _, rest = key.partition('\\')
            lhs = '@' if name is None else f'"{esc(name)}"'
            if kind is None:
                value = '-'
            elif kind == 'REG_SZ':
                value = f'"{esc(data)}"'
            elif kind == 'REG_DWORD':
                value = f'dword:{int(data, 0) & 0xffffffff:08x}'
            elif kind == 'REG_BINARY':
                value = 'hex:' + ','.join(f'{b:02x}' for b in bytes.fromhex(data))
            elif kind in ('REG_EXPAND_SZ', 'REG_MULTI_SZ'):
                # reg's /d separates REG_MULTI_SZ strings with \0
                strings = data.split('\\0') if kind == 'REG_MULTI_SZ' else [data]
                raw = ''.join(t + '\0' for t in strings).encode('utf-16le') + b'\0\0' * (kind == 'REG_MULTI_SZ')
                value = f'hex({2 if kind == "REG_EXPAND_SZ" else 7}):' + ','.join(f'{b:02x}' for b in raw)
            else:
                raise ValueError(f'{kind} in a registry batch')
            out += [f'[{self.ROOTS.get(root.upper(), root)}\\{rest}]', f'{lhs}={value}', '']
        return '\r\n'.join(out) + '\r\n'

    def flush(self):
        with tempfile.TemporaryDirectory() as temp:
            for view in sorted({queued[0] for queued in self.queued}):
                reg = Path(temp) / f'profile{view}.reg'
                reg.write_text(self.text(view), encoding='utf-16')
                subprocess.run([self.wine, self.regedit[view], '/S', 'Z:' + str(reg).replace('/', '\\')],
                               env=self.env, check=True, timeout=self.timeout)
        self.queued = []
        self.known = {}


def value_names(output, key):
    """The value names in `reg query KEY`'s output (its first block: KEY's own
    values, not its subkeys'), lowercase."""
    names, own = set(), False
    for line in output.splitlines():
        if not line.strip():
            continue
        if not line.startswith(' '):
            own = line.strip().lower().endswith(key.partition('\\')[2].lower())
            continue
        if own:
            name = line.strip().split('    ')[0]
            names.add('' if name == '(Default)' else name.lower())
    return names


def font_profile_defaults():
    """Our opt-in profile, measured with public APIs on the user's Japanese XP.

    LOGFONTW has a documented 92-byte layout; heights are nominal 96 DPI pixels.
    These settings contain no font resources or Microsoft implementation code.
    """
    values = {
        'Enabled': ('REG_DWORD', '1'),
        'Version': ('REG_DWORD', '1'),
        'ThemePath': ('REG_SZ', r'C:\windows\Resources\Themes\Luna\luna.msstyles'),
        'ColorName': ('REG_SZ', 'NormalColor'),
        'SizeName': ('REG_SZ', 'NormalSize'),
    }
    face = 'MS UI Gothic'.encode('utf-16le').ljust(64, b'\0')
    for name, height, weight, italic in (
        ('CaptionFont', -13, 700, 0),
        ('SmallCaptionFont', -12, 700, 0),
        ('MenuFont', -12, 400, 0),
        ('StatusFont', -12, 400, 0),
        ('MessageFont', -12, 400, 0),
        ('IconFont', -12, 400, 0),
        ('StartFont', -16, 700, 1),
        ('ClockFont', -12, 400, 0),
        ('TaskFont', -12, 400, 0),
        ('StartUserFont', -19, 700, 0),
        ('StartProgramsFont', -12, 700, 0),
    ):
        data = struct.pack('<5l8B64s', height, 0, 0, 0, weight,
                           italic, 0, 0, 1, 0, 0, 0, 0, face)
        values[name] = ('REG_BINARY', data.hex())
    return values


def install_classic_fonts(run, exists, language='ja'):
    """Fresh-prefix baseline only; existing-prefix migrations never call this."""
    face, height = CLASSIC_FONTS[language]
    face = face.encode('utf-16le').ljust(64, b'\0')
    for name, weight in (('CaptionFont', 700), ('SmallCaptionFont', 700),
                         ('MenuFont', 400), ('StatusFont', 400),
                         ('MessageFont', 400), ('IconFont', 400)):
        if not exists(CLASSIC_FONT_PROFILE, name):
            data = struct.pack('<5l8B64s', height, 0, 0, 0, weight,
                               0, 0, 0, 1, 0, 0, 0, 0, face)
            run('reg', 'add', CLASSIC_FONT_PROFILE, '/v', name,
                '/t', 'REG_BINARY', '/d', data.hex(), '/f')

# Public class identifiers, cross-checked against the browseui assembly manifest:
# https://lists.reactos.org/hyperkitty/list/ros-diffs%40reactos.org/thread/ENB6UBPTHQSRZOG7GFIV2V4BJLWBTKYT/
# Execution logs require BandSite (ECD4FC4D) and ShellBrowser (AF604EFE).
BROWSEUI_CLASSES = (
    '{DD313E04-FEFF-11D1-8ECD-0000F87A470C}',
    '{0BE35200-8F91-11CE-9DE3-00AA004BB851}',
    '{00BB2765-6A77-11D0-A535-00C04FD7D062}',
    '{01E04581-4EEE-11D0-BFE9-00AA005B4383}',
    '{A08C11D2-A228-11D0-825B-00AA005B4383}',
    '{F61FFEC1-754F-11D0-80CA-00AA005B4383}',
    '{ECD4FC4D-521C-11D0-B792-00A0C90312E1}',
    '{ECD4FC4E-521C-11D0-B792-00A0C90312E1}',
    '{ECD4FC4F-521C-11D0-B792-00A0C90312E1}',
    '{E13EF4E4-D2F2-11D0-9816-00C04FD91972}',
    '{22BF0C20-6DA7-11D0-B373-00A0C9034938}',
    '{AF604EFE-8897-11D1-B944-00A0C90312E1}',
    '{EF8AD2D1-AE36-11D1-B2D2-006097DF8C11}',
    '{5E6AB780-7743-11CF-A12B-00AA004AE837}',
    '{AF4F6510-F982-11D0-8595-00AA004CD6D8}',
)


# XP's own resource references for namespace display names and tips, read with
# tests/shell-names.c on the reference XP machine. Only the references are
# stored here; the strings stay in the user's local XP DLLs in system32.
XP_SHELL32 = r'@%SystemRoot%\system32\SHELL32.dll,'
NAMESPACE_STRINGS = (
    ('{20D04FE0-3AEA-1069-A2D8-08002B30309D}', XP_SHELL32 + '-9216', XP_SHELL32 + '-22913'),
    ('{208D2C60-3AEA-1069-A2D7-08002B30309D}', XP_SHELL32 + '-9217', XP_SHELL32 + '-22912'),
    ('{645FF040-5081-101B-9F08-00AA002F954E}', XP_SHELL32 + '-8964', XP_SHELL32 + '-22915'),
    ('{450D8FBA-AD25-11D0-98A8-0800361B1103}', XP_SHELL32 + '-9227', XP_SHELL32 + '-22914'),
    ('{2227A280-3AEA-1069-A2DE-08002B30309D}', XP_SHELL32 + '-9319', XP_SHELL32 + '-12696'),
    ('{871C5380-42A0-1069-A2EA-08002B30309D}', '@shdoclc.dll,-880', '@shdoclc.dll,-881'),
    # XP's registry leaves Control Panel's name to shell32 itself; its string
    # 4161 is that name, while Wine's reference selects an unrelated string.
    ('{21EC2020-3AEA-1069-A2DD-08002B30309D}', XP_SHELL32 + '-4161', XP_SHELL32 + '-31361'),
)


def install(run, exists, language='ja'):
    """The XP shell's registry profile. LANGUAGE is the XP media's (extract/media.py).

    English XP needs no font profile and keeps Wine's 75px icon spacing: Wine's
    reading of the English Luna theme and its defaults give the fonts and
    metrics that public calls report on the English reference machine."""
    japanese = language == 'ja'
    # Wine's game controller driver (winebus.sys) waits for SDL's events in
    # steps of a millisecond: a thousand wakeups a second of winedevice.exe
    # for the whole session, with or without a controller. Controllers are
    # read through udev (hidraw and evdev) instead; a 1 brings SDL back.
    if not exists(WINEBUS, 'Enable SDL'):
        run('reg', 'add', WINEBUS, '/v', 'Enable SDL', '/t', 'REG_DWORD', '/d', '0', '/f')
    # Seed defaults once. Migrations preserve disabled profiles, custom fonts,
    # and even unrecognized values: the builtin reader validates or falls back.
    for name, (kind, data) in font_profile_defaults().items() if japanese else ():
        if not exists(FONT_PROFILE, name):
            run('reg', 'add', FONT_PROFILE, '/v', name, '/t', kind, '/d', data, '/f')
    advanced = r'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced'
    # Wine's Unix root folder ("/") is not one of XP's desktop items; XP's
    # Explorer reads these keys for its two Start menu styles. Only an absent
    # value is seeded, so a user's own choice stays.
    for style in ('NewStartPanel', 'ClassicStartMenu'):
        hidden = r'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\HideDesktopIcons' + '\\' + style
        if not exists(hidden, UNIX_ROOT):
            run('reg', 'add', hidden, '/v', UNIX_ROOT, '/t', 'REG_DWORD', '/d', '1', '/f')
    # XP's default user draws desktop icon labels with drop shadows; without
    # the value XP fills the label background over the wallpaper.
    if not exists(advanced, 'ListviewShadow'):
        run('reg', 'add', advanced, '/v', 'ListviewShadow', '/t', 'REG_DWORD', '/d', '1', '/f')
    # "Use a background image for each folder type", on for XP's default
    # user: the watermarks in the corner of folder views (Search Results'
    # magnifier). shell32 shows none without the value.
    if not exists(advanced, 'ListviewWatermark'):
        run('reg', 'add', advanced, '/v', 'ListviewWatermark', '/t', 'REG_DWORD', '/d', '1', '/f')
    # XP's default user profile starts with the taskbar locked: no grippers or
    # top sizing bar, 30px high at 96 DPI. Explorer writes this value when the
    # user changes the lock, so only an absent value is seeded.
    if not exists(advanced, 'TaskbarSizeMove'):
        run('reg', 'add', advanced, '/v', 'TaskbarSizeMove', '/t', 'REG_DWORD', '/d', '0', '/f')
    # Japanese XP lays desktop icons out 78px apart (SPI_GETICONMETRICS on the
    # reference machine); Wine's 75px default wraps labels such as My Computer
    # that are 74px wide in MS UI Gothic. Seed once, then keep user changes.
    marker = r'HKCU\Software\Win4Wine\Profile'
    if japanese and not exists(marker, 'IconSpacing'):
        run('reg', 'add', r'HKCU\Control Panel\Desktop\WindowMetrics', '/v', 'IconSpacing',
            '/t', 'REG_SZ', '/d', '-1170', '/f')
        run('reg', 'add', marker, '/v', 'IconSpacing', '/t', 'REG_DWORD', '/d', '1', '/f')
    # XP's default user desktop: Bliss stretched over the screen. stage.py
    # converts the user's extracted Bliss.jpg to Wallpaper1.bmp as XP does. Seed once and keep any later choice.
    if not exists(marker, 'WallpaperBitmap'):
        user = pwd.getpwuid(os.getuid()).pw_name
        desktop = r'HKCU\Control Panel\Desktop'
        for name, value in (('Wallpaper', rf'C:\users\{user}\AppData\Local\Microsoft\Wallpaper1.bmp'),
                            ('WallpaperStyle', '2'), ('TileWallpaper', '0')):
            run('reg', 'add', desktop, '/v', name, '/t', 'REG_SZ', '/d', value, '/f')
        run('reg', 'add', marker, '/v', 'WallpaperBitmap', '/t', 'REG_DWORD', '/d', '1', '/f')
    # XP's Welcome-screen logon type: the Start panel shows the account
    # picture and offers "Turn Off Computer", as on the reference XP machine.
    if not exists(marker, 'LogonType'):
        run('reg', 'add', r'HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon',
            '/v', 'LogonType', '/t', 'REG_DWORD', '/d', '1', '/f')
        run('reg', 'add', marker, '/v', 'LogonType', '/t', 'REG_DWORD', '/d', '1', '/f')
    # XP's default visual effects (HIVEDEF.INF): drop shadows under menus and
    # tooltips, hot tracking and the fades, and the underlined access keys
    # hidden until Alt is pressed (KeyboardCues off). Seed once and keep the user's later choice.
    if not exists(marker, 'UserPreferences'):
        run('reg', 'add', r'HKCU\Control Panel\Desktop', '/v', 'UserPreferencesMask',
            '/t', 'REG_BINARY', '/d', '9e3e0780', '/f')
        run('reg', 'add', marker, '/v', 'UserPreferences', '/t', 'REG_DWORD', '/d', '1', '/f')
    # Japanese XP's console font. XP leaves HKCU\Console's values empty and
    # draws 8x18 cells (GetConsoleFontSize on the reference machine) with the
    # glyphs of MS Gothic at 16 pixels (pixel-equal; the half-width ones come
    # from its raster font vga932.fon). Wine's conhost would pick the first
    # fixed-pitch font, which has no Japanese glyphs; with these values and
    # Wine patch 0059 it lays the 16-pixel font out in 8x18 cells as XP does.
    # The English family name keeps reg.exe's command line ASCII. Seed once.
    # English XP's console draws 8x12 cells with its raster Terminal font
    # (vgaoem.fon, extracted for English media), as on the English reference
    # machine; Wine's conhost would take a TrueType fixed-pitch font instead.
    if not exists(marker, 'ConsoleFont'):
        console = r'HKCU\Console'
        font = (('MS Gothic', '0x00120008', '0x36') if japanese else ('Terminal', '0x000c0008', '0x30'))
        for name, kind, value in (('FaceName', 'REG_SZ', font[0]), ('FontSize', 'REG_DWORD', font[1]),
                                  ('FontFamily', 'REG_DWORD', font[2]), ('FontWeight', 'REG_DWORD', '400')):
            run('reg', 'add', console, '/v', name, '/t', kind, '/d', value, '/f')
        run('reg', 'add', marker, '/v', 'ConsoleFont', '/t', 'REG_DWORD', '/d', '1', '/f')
    if not japanese:
        # English XP maps MS Shell Dlg to Microsoft Sans Serif (its micross.ttf,
        # extracted for English media); Wine's default for code page 1252 is
        # Tahoma. Wine reads the 64-bit view and rewrites the value only when
        # the prefix's code page changes, so it is set at every update.
        run('reg', 'add', r'HKLM\Software\Microsoft\Windows NT\CurrentVersion\FontSubstitutes',
            '/v', 'MS Shell Dlg', '/t', 'REG_SZ', '/d', 'Microsoft Sans Serif', '/f', '/reg:64')
    for clsid, name, tip in NAMESPACE_STRINGS:
        key = 'HKCR\\CLSID\\' + clsid
        if name:
            run('reg', 'add', key, '/v', 'LocalizedString', '/t', 'REG_EXPAND_SZ', '/d', name, '/f')
        elif exists(key, 'LocalizedString'):
            run('reg', 'delete', key, '/v', 'LocalizedString', '/f')
        run('reg', 'add', key, '/v', 'InfoTip', '/t', 'REG_EXPAND_SZ', '/d', tip, '/f')
    # Native My Computer defaults to explorer.exe's first icon. Our XP Explorer
    # has a separate name so Wine's explorer.exe can remain the desktop manager.
    run('reg', 'add', r'HKCR\CLSID\{20D04FE0-3AEA-1069-A2D8-08002B30309D}\DefaultIcon',
        '/ve', '/t', 'REG_SZ', '/d', r'C:\xp\xp-explorer.exe,0', '/f')
    # XP's shell32 also writes the user's own My Computer icon, which comes
    # first: %SystemRoot%\explorer.exe, Wine's, an application's icon.
    run('reg', 'add', r'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\CLSID'
        r'\{20D04FE0-3AEA-1069-A2D8-08002B30309D}\DefaultIcon',
        '/ve', '/t', 'REG_SZ', '/d', r'C:\xp\xp-explorer.exe,0', '/f')
    # The Recycle Bin's two icons, as XP's shell32 registers them (its icons
    # 31 and 32): Display Properties' Themes sample and Desktop Items read
    # them; Wine's registration has only the current one. The user's own key,
    # which the shell writes the current icon to, comes first and hides the
    # class's, so it gets them too.
    for key in (r'HKCR\CLSID', r'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\CLSID'):
        for name, index in (('Empty', 31), ('Full', 32)):
            run('reg', 'add', key + r'\{645FF040-5081-101B-9F08-00AA002F954E}\DefaultIcon', '/v', name,
                '/t', 'REG_EXPAND_SZ', '/d', rf'%SystemRoot%\System32\shell32.dll,{index}', '/f')
    # Publicly documented Pin to Start Menu handler; users choose their own pins.
    for file_type in ('exefile', 'lnkfile'):
        run('reg', 'add', 'HKCR\\' + file_type + '\\shellex\\ContextMenuHandlers\\StartMenuPin',
            '/ve', '/t', 'REG_SZ', '/d', '{A2A9545D-A0C2-42B4-9708-A0B2BADD77C8}', '/f')
    # This alias contains only our source-built Wine controls, never XP code.
    run('reg', 'add', r'HKCU\Software\Wine\DllOverrides', '/v', 'win4wine-controls',
        '/d', 'builtin', '/f')
    servers = [(c, 'browseui.dll') for c in BROWSEUI_CLASSES]
    servers.append(('{A5E46E3A-8849-11D1-9D8C-00C04FC99D61}', 'shdocvw.dll'))
    # Providers verified with DllGetClassObject on the user's local XP SP3 DLLs.
    servers.extend((
        ('{26FDC864-BE88-46E7-9235-032D8EA5162E}', 'shell32.dll'),
        ('{7EB5FBE4-2100-49E6-8593-17E130122F91}', 'shell32.dll'),
        ('{5B4DAE26-B807-11D0-9815-00C04FD91972}', 'shell32.dll'),
        ('{4622AD11-FF23-11D0-8D34-00A0C90F2719}', 'shell32.dll'),
        ('{A2A9545D-A0C2-42B4-9708-A0B2BADD77C8}', 'shell32.dll'),
        ('{603D3800-BD81-11D0-A3A5-00C04FD706EC}', 'browseui.dll'),
        ('{603D3801-BD81-11D0-A3A5-00C04FD706EC}', 'browseui.dll'),
        ('{42AEDC87-2188-41FD-B9A3-0C966FEABEC1}', 'shdocvw.dll'),
        # Explorer Band: the tree shown by the folder window's Folders button.
        ('{EFA24E64-B078-11D0-89E4-00C04FC9E26E}', 'shdocvw.dll'),
        ('{7849596A-48EA-486E-8937-A2A3009F31A9}', 'shell32.dll'),
        ('{D912F8CF-0396-4915-884E-FB425D32943B}', 'shell32.dll'),
        ('{3F6953F0-5359-47FC-BD99-9F2CB95A62FD}', 'shell32.dll'),
        # Folder Options' General and View pages; XP's registry names shell32.
        ('{6D5313C0-8C62-11D1-B2CD-006097DF8C11}', 'shell32.dll'),
        # The taskbar's folder toolbars (Quick Launch, Desktop, New Toolbar):
        # ISFBand and QuickLinks, and the icons of Show Desktop's .scf file
        # (CmdFileIcon). XP's shell32 registers them (its [RegShellObjects]);
        # browseui's entries are for the Windows versions before XP.
        ('{D82BE2B0-5764-11D0-A96E-00C04FD705A2}', 'shell32.dll'),
        ('{0E5CBF21-D15F-11D0-8301-00AA005B4383}', 'shell32.dll'),
        ('{57651662-CE3E-11D0-8D77-00C04FC99D61}', 'shell32.dll'),
    ))
    for clsid, dll in servers:
        key = 'HKCR\\CLSID\\' + clsid + '\\InprocServer32'
        run('reg', 'add', key, '/ve', '/t', 'REG_SZ', '/d', dll, '/f')
        run('reg', 'add', key, '/v', 'ThreadingModel', '/t', 'REG_SZ', '/d', 'Apartment', '/f')
    run('reg', 'add', r'HKCR\CLSID\{0E5CBF21-D15F-11D0-8301-00AA005B4383}', '/v', 'MenuTextPUI',
        '/t', 'REG_SZ', '/d', '@browselc.dll,-13138', '/f')
    # Shell command files, as XP's shell32 registers them: Quick Launch's Show
    # Desktop. XP Explorer's DDE server runs their command; without a running
    # shell the file opens XP Explorer.
    command = r'HKCR\SHCmdFile'
    run('reg', 'add', r'HKCR\.scf', '/ve', '/t', 'REG_SZ', '/d', 'SHCmdFile', '/f')
    run('reg', 'add', command, '/ve', '/t', 'REG_SZ', '/d', 'Windows Explorer Command', '/f')
    run('reg', 'add', command, '/v', 'NeverShowExt', '/t', 'REG_SZ', '/d', '', '/f')
    run('reg', 'add', command, '/v', 'InfoTip', '/t', 'REG_SZ', '/d', 'prop:DocComments', '/f')
    for name in ('CLSID', r'shellex\IconHandler'):
        run('reg', 'add', command + '\\' + name, '/ve', '/t', 'REG_SZ',
            '/d', '{57651662-CE3E-11D0-8D77-00C04FC99D61}', '/f')
    run('reg', 'add', command + r'\shell\open\command', '/ve', '/t', 'REG_SZ', '/d', r'C:\xp\xp-explorer.exe', '/f')
    run('reg', 'add', command + r'\shell\open\ddeexec', '/ve', '/t', 'REG_SZ', '/d', '[ShellFile("%1","%1",%S)]', '/f')
    run('reg', 'add', command + r'\shell\open\ddeexec', '/v', 'NoActivateHandler', '/t', 'REG_SZ', '/d', '', '/f')
    run('reg', 'add', command + r'\shell\open\ddeexec\Application', '/ve', '/t', 'REG_SZ', '/d', 'Folders', '/f')
    run('reg', 'add', command + r'\shell\open\ddeexec\Topic', '/ve', '/t', 'REG_SZ', '/d', 'AppProperties', '/f')
    # XP registers the New menu handler with flags=0. Without the value XP
    # shell32 puts the handler's New item at the start of the folder's
    # background menu instead of in the handler group, and its cleanup then
    # drops the separator after Paste Shortcut (tests/desktop-menu.c).
    run('reg', 'add', r'HKCR\CLSID\{D969A300-E7FF-11d0-A93B-00A0C90F2719}', '/v', 'flags',
        '/t', 'REG_DWORD', '/d', '0', '/f')

    for dll in ('browselc', 'shdoclc'):
        run('reg', 'add', r'HKCU\Software\Wine\DllOverrides', '/v', dll, '/d', 'builtin', '/f')
        run('reg', 'add', r'HKCU\Software\Wine\AppDefaults\xp-explorer.exe\DllOverrides',
            '/v', dll, '/d', 'native', '/f')
    # XP's own shut down dialog. Only XP Explorer loads it; Wine has no
    # builtin module of that name. Its log off entry names the user that
    # Winlogon records at logon.
    run('reg', 'add', r'HKCU\Software\Wine\AppDefaults\xp-explorer.exe\DllOverrides',
        '/v', 'msgina', '/d', 'native', '/f')
    run('reg', 'add', r'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer',
        '/v', 'Logon User Name', '/t', 'REG_SZ', '/d', pwd.getpwuid(os.getuid()).pw_name, '/f')

    # XP's Folder class also has the explore verb that the Win+E hotkey and
    # the folder menu's Explore use; Wine registers only open. Like Wine's open
    # verb, it is served by the running shell's DDE server. XP's ShellExecute
    # also needs each verb's command, so Run could not open a folder; XP's
    # command starts Explorer, which is XP Explorer here, when no shell answers.
    run('reg', 'add', r'HKCR\Folder\shell\open\command', '/ve', '/t', 'REG_SZ',
        '/d', r'C:\xp\xp-explorer.exe /idlist,%I,%L', '/f')
    run('reg', 'add', r'HKCR\Folder\shell\explore\command', '/ve', '/t', 'REG_SZ',
        '/d', r'C:\xp\xp-explorer.exe /e,/idlist,%I,%L', '/f')
    explore = r'HKCR\Folder\shell\explore'
    run('reg', 'add', explore, '/v', 'BrowserFlags', '/t', 'REG_DWORD', '/d', '34', '/f')
    run('reg', 'add', explore, '/v', 'ExplorerFlags', '/t', 'REG_DWORD', '/d', '33', '/f')
    run('reg', 'add', explore + r'\ddeexec', '/ve', '/t', 'REG_SZ', '/d', '[ExploreFolder("%l", %I, %S)]', '/f')
    run('reg', 'add', explore + r'\ddeexec', '/v', 'NoActivateHandler', '/t', 'REG_SZ', '/d', '', '/f')
    run('reg', 'add', explore + r'\ddeexec\application', '/ve', '/t', 'REG_SZ', '/d', 'Folders', '/f')
    run('reg', 'add', explore + r'\ddeexec\topic', '/ve', '/t', 'REG_SZ', '/d', 'AppProperties', '/f')

    # The drives of the Linux system: Z: is its root folder and H: the user's
    # home (create.py); XP's shell names them from DriveIcons instead of
    # "Local Disk", as it does for drives whose volume has no label.
    for letter, label in DRIVE_LABELS['ja' if japanese else 'en'].items():
        run('reg', 'add', rf'HKLM\Software\Microsoft\Windows\CurrentVersion\Explorer\DriveIcons\{letter}\DefaultLabel',
            '/ve', '/t', 'REG_SZ', '/d', label, '/f')

    # Wine's builtin ShellExecute handles executable extensions directly;
    # native XP ShellExecute requires their explicit association.
    for extension in ('.exe', '.com'):
        run('reg', 'add', 'HKCR\\' + extension, '/ve', '/t', 'REG_SZ', '/d', 'exefile', '/f')
