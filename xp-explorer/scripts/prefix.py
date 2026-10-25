#!/usr/bin/env python3
"""Create the Wine prefix that runs XP's explorer.exe in a Wine virtual desktop.

Trimmed from win4wine's prefix/create.py, prefix/registry.py and prefix/stage.py
(LGPL-2.1-or-later) for a stock, unpatched Wine: no session, no greeter, no
Japanese profile. Usage: prefix.py ASSETS PREFIX
"""
import argparse
import json
import os
from pathlib import Path
import pwd
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parent))
import media

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / '.local/bin'
USER = pwd.getpwuid(os.getuid()).pw_name

# XP's shell DLLs, loaded native by XP Explorer only; every other program keeps Wine's.
# XP's Display Properties, native in rundll32 (which XP's shell32 starts from
# C:\windows) and in XP Explorer.
# Internet Explorer 6's engine (assets/ie), beside its iexplore.exe.
IE_NATIVE = ('mshtml', 'mshtmled', 'jscript', 'vbscript', 'urlmon', 'wininet', 'mlang', 'msls31', 'iepeers',
             'imgutil', 'pngfilt', 'msrating', 'dxtrans', 'dxtmsft', 'iedkcs32')
IE_REGISTER = ('urlmon.dll', 'mshtml.dll', 'jscript.dll', 'vbscript.dll', 'mlang.dll', 'iepeers.dll',
               'imgutil.dll', 'pngfilt.dll', 'mshtmled.dll', 'dxtrans.dll', 'dxtmsft.dll')
# The Search Companion (copy_search) runs in the shell's process, with IE6's
# engine and XP's msxml3 and oleacc: Wine's msxml3 calls urlmon's CreateUri,
# which IE6's urlmon lacks, and Wine's oleacc lacks CreateStdAccessibleProxyW.
SEARCH_NATIVE = ('msxml3', 'oleacc')
SEARCH_STAGED = ('msxml3.dll', 'msxml3r.dll', 'oleacc.dll', 'oleaccrc.dll')
AGENT_REGISTER = ('agentctl.dll', 'agentdp2.dll', 'agentdpv.dll', 'agentmpx.dll', 'mslwvtts.dll', 'agtintl.dll',
                  'agentsr.dll', 'agentpsh.dll')
CPL = ('desk.cpl', 'themeui.dll', 'deskadp.dll', 'deskmon.dll', 'logon.scr',
       # Image thumbnails and previews (shell group of the assets).
       'shimgvw.dll')
NATIVE = ('shell32', 'shdocvw', 'browseui', 'shlwapi', 'comctl32', 'linkinfo', 'browselc', 'shdoclc',
          'msgina', 'mydocs', 'duser')

# COM servers XP Explorer needs that Wine's prefix does not register (from win4wine's registry.py).
SERVERS = [(c, 'browseui.dll') for c in (
    '{DD313E04-FEFF-11D1-8ECD-0000F87A470C}', '{0BE35200-8F91-11CE-9DE3-00AA004BB851}',
    '{00BB2765-6A77-11D0-A535-00C04FD7D062}', '{01E04581-4EEE-11D0-BFE9-00AA005B4383}',
    '{A08C11D2-A228-11D0-825B-00AA005B4383}', '{F61FFEC1-754F-11D0-80CA-00AA005B4383}',
    '{ECD4FC4D-521C-11D0-B792-00A0C90312E1}', '{ECD4FC4E-521C-11D0-B792-00A0C90312E1}',
    '{ECD4FC4F-521C-11D0-B792-00A0C90312E1}', '{E13EF4E4-D2F2-11D0-9816-00C04FD91972}',
    '{22BF0C20-6DA7-11D0-B373-00A0C9034938}', '{AF604EFE-8897-11D1-B944-00A0C90312E1}',
    '{EF8AD2D1-AE36-11D1-B2D2-006097DF8C11}', '{5E6AB780-7743-11CF-A12B-00AA004AE837}',
    '{AF4F6510-F982-11D0-8595-00AA004CD6D8}',
    '{603D3800-BD81-11D0-A3A5-00C04FD706EC}', '{603D3801-BD81-11D0-A3A5-00C04FD706EC}')] + [
    ('{A5E46E3A-8849-11D1-9D8C-00C04FC99D61}', 'shdocvw.dll'),
    ('{42AEDC87-2188-41FD-B9A3-0C966FEABEC1}', 'shdocvw.dll'),
    ('{EFA24E64-B078-11D0-89E4-00C04FC9E26E}', 'shdocvw.dll'),
] + [(c, 'shell32.dll') for c in (
    '{26FDC864-BE88-46E7-9235-032D8EA5162E}', '{7EB5FBE4-2100-49E6-8593-17E130122F91}',
    '{5B4DAE26-B807-11D0-9815-00C04FD91972}', '{4622AD11-FF23-11D0-8D34-00A0C90F2719}',
    '{A2A9545D-A0C2-42B4-9708-A0B2BADD77C8}',  # Start menu pins (left column)
    '{7849596A-48EA-486E-8937-A2A3009F31A9}', '{D912F8CF-0396-4915-884E-FB425D32943B}',
    '{3F6953F0-5359-47FC-BD99-9F2CB95A62FD}', '{6D5313C0-8C62-11D1-B2CD-006097DF8C11}',
    '{1EBDCF80-A200-11D0-A3A4-00C04FD706EC}', '{3FC0B520-68A9-11D0-8D77-00C04FD70822}',
    '{7487CD30-F71A-11D0-9EA7-00805F714772}')] + [
    # The folder task pane (browseui) and Display Properties' pages (themeui).
    ('{3F4EEF80-BFE8-11D0-A3A5-00C04FD706EC}', 'browseui.dll')] + [(c, 'themeui.dll') for c in (
    '{B12AE898-D056-4378-A844-6D393FE37956}', '{4C892621-6757-4FE0-AD8C-A6301BE7FBA2}',
    '{ADB9F5A4-E73E-49B8-99B6-2FA317EF9DBC}', '{B7BBD408-F09C-4AA8-B65E-A00B8FE0F0B9}')]

SHELL32 = r'@%SystemRoot%\system32\SHELL32.dll,'
NAMESPACE_STRINGS = (
    ('{20D04FE0-3AEA-1069-A2D8-08002B30309D}', SHELL32 + '-9216', SHELL32 + '-22913'),
    ('{208D2C60-3AEA-1069-A2D7-08002B30309D}', SHELL32 + '-9217', SHELL32 + '-22912'),
    ('{645FF040-5081-101B-9F08-00AA002F954E}', SHELL32 + '-8964', SHELL32 + '-22915'),
    ('{450D8FBA-AD25-11D0-98A8-0800361B1103}', SHELL32 + '-9227', SHELL32 + '-22914'),
    ('{2227A280-3AEA-1069-A2DE-08002B30309D}', SHELL32 + '-9319', SHELL32 + '-12696'),
    ('{21EC2020-3AEA-1069-A2DD-08002B30309D}', SHELL32 + '-4161', SHELL32 + '-31361'),
)


class Reg:
    """Collects values and imports them in one regedit run per registry view."""
    def __init__(self):
        self.keys = {}

    def add(self, key, name, value, kind='REG_SZ'):
        self.keys.setdefault(key, []).append((name, value, kind))

    def text(self):
        def esc(s):
            return s.replace('\\', '\\\\').replace('"', '\\"')
        roots = {'HKCU': 'HKEY_CURRENT_USER', 'HKLM': 'HKEY_LOCAL_MACHINE', 'HKCR': 'HKEY_CLASSES_ROOT'}
        out = ['Windows Registry Editor Version 5.00', '']
        for key, values in self.keys.items():
            root, _, rest = key.partition('\\')
            out.append(f'[{roots[root]}\\{rest}]')
            for name, value, kind in values:
                lhs = '@' if name is None else f'"{esc(name)}"'
                if kind == 'REG_DWORD':
                    out.append(f'{lhs}=dword:{int(value):08x}')
                elif kind == 'REG_EXPAND_SZ':
                    data = (value + '\0').encode('utf-16le')
                    out.append(f'{lhs}=hex(2):' + ','.join(f'{b:02x}' for b in data))
                elif kind == 'REG_BINARY':
                    out.append(f'{lhs}=hex:' + ','.join(f'{b:02x}' for b in bytes.fromhex(value)))
                elif kind == 'REG_MULTI_SZ':
                    data = ''.join(v + '\0' for v in value).encode('utf-16le') + b'\0\0'
                    out.append(f'{lhs}=hex(7):' + ','.join(f'{b:02x}' for b in data))
                else:
                    out.append(f'{lhs}="{esc(value)}"')
            out.append('')
        return '\r\n'.join(out) + '\r\n'


def save_shortcut(wine, prefix, env, link, target, arguments='', directory='', icon='', description=''):
    """Create an XP Start menu link with the Shell Link API."""
    helper = BIN / 'linux-links.exe'
    overrides = env.get('WINEDLLOVERRIDES', '')
    link_env = dict(env, WINEDLLOVERRIDES=(overrides + ';' if overrides else '') + 'winemenubuilder.exe=d',
                    WINEPREFIX=str(prefix))
    line = '\t'.join((link, target, arguments, directory, icon, description)) + '\n'
    result = subprocess.run([wine, str(helper)], env=link_env, input=line, capture_output=True, text=True,
                            encoding='utf-8', timeout=60)
    if result.returncode or any(item.startswith('fail ') for item in result.stdout.splitlines()):
        raise RuntimeError(f'Could not create {link}: {result.stdout.strip()} {result.stderr.strip()}')


def ie_registry(r):
    r.add(r'HKCU\Software\Wine\DllOverrides', 'iexplore.exe', 'native')
    for dll in NATIVE + IE_NATIVE:
        r.add(r'HKCU\Software\Wine\AppDefaults\iexplore.exe\DllOverrides', dll, 'native')
    # IE6's engine registers itself with regsvr32 (install_ie); an environment
    # override would also reach Wine's 64-bit helpers, which have no native DLLs.
    for dll in IE_NATIVE:
        r.add(r'HKCU\Software\Wine\AppDefaults\regsvr32.exe\DllOverrides', dll, 'native,builtin')
    main = r'HKCU\Software\Microsoft\Internet Explorer\Main'
    # IE6 cannot open today's HTTPS sites: start on a blank page, and do not
    # ask to become the default browser (the Linux browser opens web links).
    r.add(main, 'Start Page', 'about:blank')
    r.add(main, 'Check_Associations', 'no')
    # No first-run or "update finished" page (it needs microsoft.com of 2008).
    r.add(main, 'RunOnceHasShown', '1', 'REG_DWORD')
    r.add(main, 'RunOnceComplete', '1', 'REG_DWORD')
    r.add(main, 'DisableFirstRunCustomize', '1', 'REG_DWORD')
    # Wine registers its own IE's version (11); IE6's About box and pages read it.
    r.add(r'HKLM\Software\Microsoft\Internet Explorer', 'Version', '6.0.2900.5512')
    r.add(r'HKLM\Software\Microsoft\Internet Explorer', 'Build', '62900.5512')
    # Its icon on the desktop, which XP's Start menu setting hides by default.
    r.add(r'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\HideDesktopIcons\NewStartPanel',
          '{871C5380-42A0-1069-A2EA-08002B30309D}', '0', 'REG_DWORD')
    # Folder windows stay in the shell's process: each navigation reuses the
    # same ListView instead of starting a new xp-explorer.exe process, which
    # showed a white window for about a second on Wine. Internet Explorer
    # already runs in its own iexplore.exe process through its desktop icon
    # and its own AppDefaults, so it keeps its native engine there.
    r.add(r'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\BrowseNewProcess', 'BrowseNewProcess', 'no')
    # The Search Companion: IE6's engine in XP Explorer too, from syswow64
    # (stage()), where every other program keeps Wine's (mshtml stays disabled).
    for dll in IE_NATIVE + SEARCH_NATIVE:
        if dll != 'mshtml':
            r.add(r'HKCU\Software\Wine\DllOverrides', dll, 'builtin')
        r.add(r'HKCU\Software\Wine\AppDefaults\xp-explorer.exe\DllOverrides', dll, 'native')
        # Display Properties (rundll32) previews the wallpaper with shimgvw's
        # HTML thumbnail extractor, which needs IE6's mshtml.
        r.add(r'HKCU\Software\Wine\AppDefaults\rundll32.exe\DllOverrides', dll, 'native')
    for dll in SEARCH_NATIVE:
        r.add(r'HKCU\Software\Wine\AppDefaults\iexplore.exe\DllOverrides', dll, 'native')
    r.add(r'HKCU\Software\Wine\AppDefaults\regsvr32.exe\DllOverrides', 'msxml3', 'native,builtin')
    # agentsvr.exe names only the current version of Agent.Server (CurVer),
    # which Wine's CLSIDFromProgID does not follow; agentctl.dll looks it up.
    r.add(r'HKCR\Agent.Server\CLSID', None, '{D45FD2FC-5C6E-11D1-9EC1-00C04FD7081F}')
    return r


def registry():
    """The 32-bit view registry profile: XP Explorer is a 32-bit process."""
    r = Reg()
    cu = 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\'
    advanced = cu + r'Explorer\Advanced'
    r.add(r'HKCU\Software\Wine', 'Version', 'winxp')
    r.add(r'HKCU\Software\Wine\Drivers', 'Graphics', 'x11')
    r.add(r'HKCU\Software\Wine\DllOverrides', 'win4wine-controls', 'builtin')
    for dll in ('mscoree', 'mshtml'):
        r.add(r'HKCU\Software\Wine\DllOverrides', dll, '')
    # XP's Internet Explorer 6 (and our Linux browser stand-in, helpers/iexplore.c,
    # also named iexplore.exe) instead of Wine's; IE6's engine is native in it only.
    ie_registry(r)
    for dll in NATIVE:
        r.add(r'HKCU\Software\Wine\DllOverrides', dll, 'builtin')
        r.add(r'HKCU\Software\Wine\AppDefaults\xp-explorer.exe\DllOverrides', dll, 'native')
    for dll in NATIVE + tuple(c.rsplit('.', 1)[0] if c.endswith('.dll') else c for c in CPL):
        r.add(r'HKCU\Software\Wine\AppDefaults\rundll32.exe\DllOverrides', dll, 'native,builtin')
    for cpl in CPL:
        name = cpl.rsplit('.', 1)[0] if cpl.endswith('.dll') else cpl
        r.add(r'HKCU\Software\Wine\AppDefaults\xp-explorer.exe\DllOverrides', name, 'native')
    # Our Start menu helpers use XP's shell interfaces and icons.
    for program in ('startmenu.exe', 'startmenu-view.exe'):
        for dll in ('shell32', 'shlwapi', 'comctl32', 'linkinfo', 'shdocvw', 'browseui'):
            r.add(rf'HKCU\Software\Wine\AppDefaults\{program}\DllOverrides', dll, 'native')
    r.add(advanced, 'ListviewShadow', '1', 'REG_DWORD')
    # "Use a background image for each folder type": the folder watermarks.
    r.add(advanced, 'ListviewWatermark', '1', 'REG_DWORD')
    r.add(advanced, 'TaskbarSizeMove', '0', 'REG_DWORD')
    # XP's task pane on the left of folder windows ("Use common tasks in folders").
    r.add(advanced, 'WebView', '1', 'REG_DWORD')
    r.add(advanced, 'Start_ShowRun', '1', 'REG_DWORD')
    r.add(advanced, 'Start_ShowNetPlaces', '0', 'REG_DWORD')
    # XP displays its most-used programs below the pinned items, in a separate
    # block. Six entries preserve the native separator and regular text style.
    r.add(advanced, 'Start_MinMFU', '6', 'REG_DWORD')
    r.add(advanced, 'Start_TrackProgs', '1', 'REG_DWORD')
    # StartMenuInit: XP's first logon would reset the usage data and pins.
    r.add(advanced, 'StartMenuInit', '2', 'REG_DWORD')
    r.add(cu + r'Policies\Explorer', 'NoSimpleStartMenu', '0', 'REG_DWORD')
    r.add(cu + 'Explorer', 'Logon User Name', USER)
    r.add(r'HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon', 'LogonType', '1', 'REG_DWORD')
    r.add(r'HKCU\Control Panel\Desktop', 'UserPreferencesMask', '9e3e0780', 'REG_BINARY')
    # XP's shell32 registers this key itself (Open With's lists); XP Explorer's
    # Start menu reads it too, and its AddRemoveApps/AddRemoveNames values.
    associations = r'HKLM\Software\Microsoft\Windows\CurrentVersion\Explorer\FileAssociation'
    r.add(associations, 'KillList', '%1;explorer.exe;dvdplay.exe;mplay32.exe;msohtmed.exe;quikview.exe;'
          'rundll.exe;rundll32.exe;taskman.exe;bck32api.dll;')
    r.add(associations, 'CutList', ['Application File', 'MFC Application'], 'REG_MULTI_SZ')
    # XP's .theme files name their visual style with %ResourceDir%.
    r.add(r'HKLM\System\CurrentControlSet\Control\Session Manager\Environment', 'ResourceDir',
          r'%SystemRoot%\resources', 'REG_EXPAND_SZ')
    r.add(r'HKCU\Control Panel\Desktop', 'LogPixels', '96', 'REG_DWORD')
    r.add(r'HKCU\Control Panel\Desktop', 'Wallpaper', rf'C:\users\{USER}\AppData\Local\Microsoft\Wallpaper1.bmp')
    r.add(r'HKCU\Control Panel\Desktop', 'WallpaperStyle', '2')
    r.add(r'HKCU\Control Panel\Desktop', 'TileWallpaper', '0')
    for suffix in ('Shell Folders', 'User Shell Folders'):
        r.add(cu + 'Explorer\\' + suffix, 'Personal', 'H:\\')
    # Our notification area icons start with the shell, as XP's systray.exe did.
    r.add(cu + 'Run', 'XPTray', r'C:\xp\bin\xptray.exe')
    for clsid, name, tip in NAMESPACE_STRINGS:
        r.add('HKCR\\CLSID\\' + clsid, 'LocalizedString', name, 'REG_EXPAND_SZ')
        r.add('HKCR\\CLSID\\' + clsid, 'InfoTip', tip, 'REG_EXPAND_SZ')
    r.add(r'HKCR\CLSID\{20D04FE0-3AEA-1069-A2D8-08002B30309D}\DefaultIcon', None, r'C:\xp\xp-explorer.exe,0')
    # The Recycle Bin's two icons (XP's shell32 icons 31 and 32), read by
    # Display Properties' Themes sample and Desktop Items, also in the
    # user's key, which hides the class's once the shell writes it.
    for key in (r'HKCR\CLSID', cu + r'Explorer\CLSID'):
        for name, index in (('Empty', 31), ('Full', 32)):
            r.add(key + r'\{645FF040-5081-101B-9F08-00AA002F954E}\DefaultIcon', name,
                  rf'%SystemRoot%\System32\shell32.dll,{index}', 'REG_EXPAND_SZ')
    r.add(r'HKCR\CLSID\{450D8FBA-AD25-11D0-98A8-0800361B1103}\DefaultIcon', None,
          r'%SystemRoot%\system32\mydocs.dll,0', 'REG_EXPAND_SZ')
    for file_type in ('exefile', 'lnkfile'):
        r.add('HKCR\\' + file_type + r'\shellex\ContextMenuHandlers\StartMenuPin', None,
              '{A2A9545D-A0C2-42B4-9708-A0B2BADD77C8}')
    for clsid, dll in SERVERS:
        r.add('HKCR\\CLSID\\' + clsid + '\\InprocServer32', None, dll)
        r.add('HKCR\\CLSID\\' + clsid + '\\InprocServer32', 'ThreadingModel', 'Apartment')
    r.add(r'HKCR\CLSID\{D969A300-E7FF-11d0-A93B-00A0C90F2719}', 'flags', '0', 'REG_DWORD')
    r.add(r'HKCR\Folder\shell\open\command', None, r'C:\xp\xp-explorer.exe /idlist,%I,%L')
    r.add(r'HKCR\Folder\shell\explore\command', None, r'C:\xp\xp-explorer.exe /e,/idlist,%I,%L')
    explore = r'HKCR\Folder\shell\explore'
    r.add(explore, 'BrowserFlags', '34', 'REG_DWORD')
    r.add(explore, 'ExplorerFlags', '33', 'REG_DWORD')
    r.add(explore + r'\ddeexec', None, '[ExploreFolder("%l", %I, %S)]')
    r.add(explore + r'\ddeexec', 'NoActivateHandler', '')
    r.add(explore + r'\ddeexec\application', None, 'Folders')
    r.add(explore + r'\ddeexec\topic', None, 'AppProperties')
    for extension in ('.exe', '.com'):
        r.add('HKCR\\' + extension, None, 'exefile')
    return r


def wine_lib(wine):
    return Path(shutil.which(wine) or wine).resolve().parent.parent / 'lib/wine'


def patched(wine):
    """A Wine built with win4wine's patches (wine/build.sh) brings its themed controls module."""
    return (wine_lib(wine) / 'i386-windows/win4wine-controls.dll').is_file()


# Classes taken from XP's own self-registration (scripts/xpreg.py): the Start
# menu's Internet and E-mail items (shdocvw) and their icon extractor.
XP_CLASSES = {
    # The Start menu's Internet, E-mail, Search, Help and Support and Run items.
    'shell32.dll': ('{2559a1f4-21d7-11d4-bdaf-00c04f60b9f0}', '{2559a1f5-21d7-11d4-bdaf-00c04f60b9f0}',
                    '{2559a1f0-21d7-11d4-bdaf-00c04f60b9f0}', '{2559a1f1-21d7-11d4-bdaf-00c04f60b9f0}',
                    '{2559a1f3-21d7-11d4-bdaf-00c04f60b9f0}',
                    '{25585dc7-4da0-438d-ad04-e42c8d2d64b9}',
                    # ShellTreeWalker, which folder windows ask for, and the history of
                    # a folder view, saved when a window navigates back or closes.
                    '{95ce8412-7027-11d1-b879-006008059382}', '{7febaf7c-18cf-11d2-993f-00a0c91f3880}'),
    # The desktop's Internet icon (it opens Internet Explorer 6) and ShellWindows,
    # which IE6 asks for; Wine registers both to its own ieframe.dll.
    'shdocvw.dll': ('{871c5380-42a0-1069-a2ea-08002b30309d}', '{9ba05972-f6a8-11cf-a442-00a0c90a8f39}'),
}


def xp_registrations(assets):
    """XP's own registry entries for XP_CLASSES, as .reg text."""
    import xpreg
    lines = ['Windows Registry Editor Version 5.00', '']
    for dll, classes in XP_CLASSES.items():
        # XP Explorer is C:\xp\xp-explorer.exe here; Wine's explorer.exe has no such resources.
        renames = ((r'C:\windows\explorer.exe', r'C:\xp\xp-explorer.exe'), ('@explorer.exe,', r'@C:\xp\xp-explorer.exe,'),
                   # XP's setup defines this string as IE's command line.
                   ('%MSIEXPLORE%', r'"%ProgramFiles%\Internet Explorer\iexplore.exe"'))
        for key, values in xpreg.convert(xpreg.reginst(assets / 'system32' / dll), dll, classes, renames).items():
            lines += [f'[{key}]', *values, '']
    # The association element behind them, which XP's shell32 registers itself.
    lines += [r'[HKEY_CLASSES_ROOT\CLSID\{3c81e7fa-1f3b-464a-a350-114a25beb2a2}\InProcServer32]',
              '@="shell32.dll"', '"ThreadingModel"="Apartment"', '']
    return '\r\n'.join(lines) + '\r\n'


# The DACL that XP's shell32 gives Wine's user's Recycle Bin folder on each drive
# (RECYCLER\S-1-5-21-0-0-0-1000), and checks byte for byte the first time it
# opens it: user, SYSTEM and Administrators get full access, each in one ACE
# for the folder and one inherited by its contents. Wine patch 0152 keeps it
# in this extended attribute (the DACL's control flags, then the ACL).
RECYCLER_SID = 'S-1-5-21-0-0-0-1000'
RECYCLER_DACL_XATTR = 'user.wine.dacl'
RECYCLER_DACL = bytes.fromhex(
    '00000000' '0200a80006000000'
    '00002400ff011f00' '010500000000000515000000000000000000000000000000e8030000'
    '000b240000000010' '010500000000000515000000000000000000000000000000e8030000'
    '00001400ff011f00' '0101000000000005' '12000000'
    '000b140000000010' '0101000000000005' '12000000'
    '00001800ff011f00' '0102000000000005' '2000000020020000'
    '000b180000000010' '0102000000000005' '2000000020020000')


def recycler_dacls(prefix):
    """Give Recycle Bin folders made before Wine kept DACLs (patch 0152) the one
    XP's shell32 expects; otherwise it calls them corrupted and offers to empty
    them."""
    for drive in (prefix / 'dosdevices').glob('?:'):
        folder = drive / 'RECYCLER' / RECYCLER_SID
        try:
            if folder.is_dir() and RECYCLER_DACL_XATTR not in os.listxattr(folder):
                os.setxattr(folder, RECYCLER_DACL_XATTR, RECYCLER_DACL)
        except OSError:
            pass


def stage(prefix):
    """Put XP's shell DLLs back in syswow64. Wine may restore its own files
    there when it updates the prefix; run this with the wineserver stopped,
    since the next wineserver maps these files as KnownDlls."""
    system = prefix / 'drive_c/windows/syswow64'
    for name in NATIVE:
        source = prefix / f'drive_c/xp/native/{name}.dll'
        target = system / f'{name}.dll'
        if source.is_file() and (not target.is_file() or target.read_bytes() != source.read_bytes()):
            temp = target.with_name(f'.{name}.xp')
            shutil.copyfile(source, temp)
            temp.replace(target)
    # XP's Internet Explorer 6 where the shell starts it, with its engine and
    # XP's common controls beside it (see install_ie); without it, our stand-in
    # (helpers/iexplore.c), which opens the Linux browser.
    ie = prefix / 'drive_c/xp/ie'
    sources = sorted(ie.iterdir()) if (ie / 'iexplore.exe').is_file() else \
        [p for p in (prefix / 'drive_c/xp/bin/iexplore.exe',) if p.is_file()]
    for folder in ('Program Files', 'Program Files (x86)'):
        target = prefix / 'drive_c' / folder / 'Internet Explorer'
        if not target.is_dir():
            continue
        for source in sources:
            if not (target / source.name).is_file() or (target / source.name).read_bytes() != source.read_bytes():
                shutil.copyfile(source, target / source.name)
    # IE6's engine in syswow64 too, for the Search Companion in XP Explorer's
    # process (ie_registry keeps Wine's for other programs), with XP's msxml3
    # and oleacc (copy_search).
    staged = [ie / f'{dll}.dll' for dll in IE_NATIVE] + [ie / 'mshtmler.dll'] if (ie / 'iexplore.exe').is_file() else []
    if (prefix / 'drive_c/xp/native/msxml3.dll').is_file():
        staged += [prefix / 'drive_c/xp/native' / name for name in SEARCH_STAGED]
    for source in staged:
        target = system / source.name
        if source.is_file() and (not target.is_file() or target.read_bytes() != source.read_bytes()):
            temp = target.with_name(f'.{source.name}.xp')
            shutil.copyfile(source, temp)
            temp.replace(target)
    # XP's shell32 runs Control Panel items with C:\windows\rundll32.exe.
    windows = prefix / 'drive_c/windows'
    if (system / 'rundll32.exe').is_file() and not (windows / 'rundll32.exe').is_file():
        shutil.copyfile(system / 'rundll32.exe', windows / 'rundll32.exe')
    # ... and with XP's common controls 6 beside it, which Wine patch 0149 lets
    # shell processes load from their own folder: Luna tabs and buttons.
    xp_controls = prefix / 'drive_c/xp/comctl32.dll'
    if xp_controls.is_file() and (prefix / '.patched-wine').is_file():
        target = windows / 'comctl32.dll'
        if not target.is_file() or target.read_bytes() != xp_controls.read_bytes():
            shutil.copyfile(xp_controls, target)
    # XP's Display Properties and the shell style of the task pane (classic theme).
    for name in CPL + ('shellstyle.dll',):
        source = prefix / 'drive_c/xp/native' / name
        target = system / name
        if source.is_file() and (not target.is_file() or target.read_bytes() != source.read_bytes()):
            temp = target.with_name(f'.{name}.xp')
            shutil.copyfile(source, temp)
            temp.replace(target)
    recycler_dacls(prefix)
    if (prefix / '.patched-wine').is_file():
        return
    # Wine's x86 common controls 6 assembly can be loaded only as native in
    # XP Explorer (where comctl32 is native) and fails there; without it the
    # shell's modules fall back to XP's v6 in syswow64. 32-bit Wine programs
    # still get Wine's builtin comctl32 (load order "b").
    winsxs = prefix / 'drive_c/windows/winsxs'
    for path in list(winsxs.glob('x86_microsoft.windows.common-controls_*_6.0.*')) + \
                list(winsxs.glob('manifests/x86_microsoft.windows.common-controls_*_6.0.*')):
        shutil.rmtree(path) if path.is_dir() else path.unlink()


# Bumped when xp_defaults() changes; xp.sh runs it again on existing prefixes.
DEFAULTS_VERSION = '14'
# Search Results and Computer Search Results (shell32's DocFind folders).
SEARCH_FOLDERS = ('{E17D4FC0-5564-11D1-83F2-00A0C90DC849}', '{1F4DE370-D627-11D1-BA4F-00A0C91EEDBA}')
FILE_SEARCH_BAND = '{2D91EEA1-9932-11D2-BE86-00A0C9A83DA1}'
EXPLORER = r'SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer'
# Wine's Unix root folder ("/"), a desktop item of Wine's shell namespace
UNIX_ROOT = '{9D20AAE8-0625-44B0-9CA7-71889C2254D9}'
IMAGES = ('.bmp', '.dib', '.gif', '.jpg', '.jpeg', '.jpe', '.jfif', '.png', '.tif', '.tiff', '.ico', '.emf', '.wmf')
IMAGE_PROGIDS = ('Paint.Picture', 'giffile', 'jpegfile', 'pjpegfile', 'pngfile', 'TIFImage.Document',
                 'emffile', 'wmffile')
WMP_EXTENSIONS = ('.aif', '.aifc', '.aiff', '.asf', '.asx', '.avi', '.cda', '.dvr-ms', '.ivf', '.m1v',
                  '.m3u', '.mid', '.midi', '.mp2', '.mp2v', '.mp3', '.mpa', '.mpe', '.mpeg', '.mpg', '.mpv2',
                  '.rmi', '.snd', '.wav', '.wax', '.wm', '.wma', '.wmd', '.wmp', '.wms', '.wmv', '.wmx', '.wmz',
                  '.wvx')
# Folder customizations as XP's setup writes them (desktop.ini); names and icons
# from XP's shell32.dll (strings 21779, 21790; icon 237, 21) and mydocs.dll (101).
FOLDER_INIS = {
    'Pictures': ('@%SystemRoot%\\system32\\shell32.dll,-21779', r'%SystemRoot%\system32\mydocs.dll', -101),
    'Music': ('@%SystemRoot%\\system32\\shell32.dll,-21790', r'%SystemRoot%\system32\shell32.dll', -237),
    'AppData/Roaming/Microsoft/Windows/Recent': (None, r'%SystemRoot%\system32\shell32.dll', -21),
    # The public folders (in the prefix), with XP's names for them.
    '../Public/Documents': ('@%SystemRoot%\\system32\\shell32.dll,-21785', None, None),
    '../Public/Music': ('@%SystemRoot%\\system32\\shell32.dll,-28995', r'%SystemRoot%\system32\shell32.dll', -237),
    '../Public/Pictures': ('@%SystemRoot%\\system32\\shell32.dll,-28997', r'%SystemRoot%\system32\mydocs.dll', -101),
}


def folder_ini(name, icon_file, icon_index):
    """XP's own desktop.ini: XP reads IconFile and IconIndex (IconResource is
    for later Windows)."""
    lines = ['[.ShellClassInfo]']
    if name:
        lines.append(f'LocalizedResourceName={name}')
    if icon_file:
        lines += [f'IconResource={icon_file},{icon_index}', f'IconFile={icon_file}', f'IconIndex={icon_index}']
    return '\r\n'.join(lines) + '\r\n'


def set_dos_attributes(path, attributes):
    """Wine keeps DOS attributes in the user.DOSATTRIB extended attribute."""
    try:
        os.setxattr(path, 'user.DOSATTRIB', f'0x{attributes:x}'.encode())
    except OSError:
        pass


def navigating_default(assets):
    """HIVEDEF.INF's navigation click, in the user's XP language.

    The previous code hardcoded the English "%SystemRoot%\\media\\Windows XP
    Start.wav", silent on XP in another language. The INF already names the
    right file; fall back to the English name when it is not there.
    """
    fallback = r'%SystemRoot%\media\Windows XP Start.wav'
    try:
        import xpreg
        text = xpreg.read_inf(assets / 'inf/hivedef.inf')
    except OSError:
        return fallback
    strings, section = {}, None
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith('['):
            section = line.strip('[]').lower()
        elif section == 'strings' and '=' in line and not line.startswith(';'):
            key, value = line.split('=', 1)
            strings[key.strip().lower()] = value.strip().strip('"')

    def expand(value):
        return re.sub(r'%([^%]+)%',
                      lambda m: strings.get(m.group(1).lower(), m.group(0)),
                      value).replace('%%', '%')

    # Join continuation lines as xpreg.hive_entries does.
    logical, current = [], ''
    for raw in text.splitlines():
        line = raw.rstrip()
        if line.endswith('\\'):
            current += line[:-1]
            continue
        logical.append(current + line)
        current = ''
    for line in logical:
        if not line.upper().startswith('HKCU,'):
            continue
        try:
            fields = xpreg.fields(line)
        except Exception:
            continue
        if len(fields) < 4:
            continue
        key = fields[1].replace('\\\\', '\\')
        if key.lower() != r'appevents\schemes\apps\explorer\navigating\.default':
            continue
        # HKCU,key,name,flags,data (name empty for the default value); some
        # lines omit the empty name, so take the last non-flag field.
        for value in reversed(fields[2:]):
            value = value.strip().strip('"')
            if not value or value.lower().startswith('0x'):
                continue
            # Skip the value name when it is not empty (a named value, not
            # the default); the data is the .wav path / %SystemRoot% one.
            if value.startswith(('0', '1', '2', '3', '4', '5', '6', '7', '8', '9')) and ' ' not in value \
                    and '%' not in value and '.wav' not in value.lower():
                continue
            return expand(value)
    return fallback


def repair_navigating_sound(prefix, wine):
    """Re-mirror the effective navigation click under xp-explorer.

    XP's Explorer plays Navigating through SND_APPLICATION, looked up under
    the calling executable's name; the shell runs as C:\\xp\\xp-explorer.exe.
    A live prefix can lose Explorer's .Current (then it navigates silently
    with SND_NODEFAULT), and an older prefix has the hardcoded English file
    or a stale mirror. Best effort: never wipes the mirror on query failure.
    """
    import subprocess
    base = r'HKCU\AppEvents\Schemes\Apps'
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all')
    env.pop('WINEARCH', None)

    # Keep folder navigation in the shell process even if the sound scheme is
    # missing or temporarily unreadable. This is the migration for prefixes
    # created with BrowseNewProcess=yes, which caused the white flash.
    try:
        subprocess.run([wine, 'reg', 'add',
                        r'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\BrowseNewProcess',
                        '/v', 'BrowseNewProcess', '/d', 'no', '/f'],
                       env=env, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
                       timeout=60, check=False)
    except (OSError, subprocess.TimeoutExpired):
        pass

    def query(key):
        try:
            out = subprocess.run([wine, 'reg', 'query', key, '/ve'],
                                 env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 timeout=60, text=True)
        except (OSError, subprocess.TimeoutExpired):
            return None
        if out.returncode != 0:
            return None
        for line in (out.stdout or '').splitlines():
            for kind in ('REG_EXPAND_SZ', 'REG_SZ'):
                if kind in line:
                    return line.split(kind, 1)[1].strip()
        return ''

    scheme = query(r'HKCU\AppEvents\Schemes')
    current = query(base + r'\Explorer\Navigating\.Current')
    default = query(base + r'\Explorer\Navigating\.Default')
    if current is None and default is None:
        return
    current = current or ''
    default = default or ''
    # Restore an emptied Windows Default event from its .Default.
    if scheme is not None and '.Default' in scheme and not current and default:
        try:
            subprocess.run([wine, 'reg', 'add', base + r'\Explorer\Navigating\.Current',
                            '/ve', '/t', 'REG_EXPAND_SZ', '/d', default, '/f'],
                           env=env, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
                           timeout=60, check=False)
        except (OSError, subprocess.TimeoutExpired):
            pass
        current = current or default
    sound = current or default
    if not sound:
        return
    for leaf in (r'\xp-explorer\Navigating\.Current', r'\xp-explorer\Navigating\.Default'):
        try:
            subprocess.run([wine, 'reg', 'add', base + leaf,
                            '/ve', '/t', 'REG_EXPAND_SZ', '/d', sound, '/f'],
                           env=env, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
                           timeout=60, check=False)
        except (OSError, subprocess.TimeoutExpired):
            pass


def xp_defaults(assets, prefix, wine, folders=True):
    """XP's own defaults for the shell, from the user's files: its sounds
    (Windows\\Media and HIVEDEF.INF's sound scheme), HIVESFT.INF's Explorer
    settings, shimgvw.dll's image handlers and XP's folder icons (FOLDERS:
    win4wine's session marks its own folders, never the user's Linux ones)."""
    import xpreg
    media = assets / 'media'
    if media.is_dir():
        target = prefix / 'drive_c/windows/Media'
        target.mkdir(exist_ok=True)
        for wav in media.iterdir():
            shutil.copyfile(wav, target / wav.name)
    # Every class XP's shell32 registers itself (its REGINST: Namespace Walker for
    # the picture viewer, ...), then XP_CLASSES from the other DLLs over them.
    shell32 = assets / 'system32/shell32.dll'
    module = {'_SYS_MOD_PATH': r'%SystemRoot%\system32\shell32.dll', '_MOD_PATH': r'%SystemRoot%\system32\shell32.dll'}
    texts = [xpreg.hive_entries(xpreg.reginst(shell32), 'HKCR', ('CLSID',), extra=module,
                                renames=((r'C:\windows\explorer.exe', r'C:\xp\xp-explorer.exe'),
                                         ('%MSIEXPLORE%', r'"%ProgramFiles%\Internet Explorer\iexplore.exe"'))),
             xp_registrations(assets)]
    # Search: shell32's search command checks that the window shows a Search
    # Results folder by parsing "::{CLSID}", which XP's desktop accepts only
    # for the classes of Desktop\NameSpace; its REGINST lists them there.
    texts.append(xpreg.hive_entries(xpreg.reginst(shell32), 'HKLM',
                                    tuple(EXPLORER + r'\Desktop\NameSpace' + '\\' + c for c in SEARCH_FOLDERS)))
    # Start > Search makes its window in another thread (CLSID_ShellBrowserWindow)
    # and asks it for IFileSearchBand to open Search Results: the interface of
    # XP's shell32 type library that Wine's registration of shell32 lacks.
    r = Reg()
    band = r'HKCR\Interface' + '\\' + FILE_SEARCH_BAND
    r.add(band, None, 'IFileSearchBand')
    r.add(band + r'\ProxyStubClsid32', None, '{00020424-0000-0000-C000-000000000046}')
    r.add(band + r'\TypeLib', None, '{50A7E9B0-70EF-11D1-B75A-00A0C90564FE}')
    r.add(band + r'\TypeLib', 'Version', '1.0')
    texts.append(r.text())
    inf = assets / 'inf'
    if (inf / 'hivedef.inf').is_file():
        hivedef = xpreg.read_inf(inf / 'hivedef.inf')
        # Not "User Shell Folders": the user's folders are their Linux ones.
        texts.append(xpreg.hive_entries(hivedef, 'HKCU', ('AppEvents', r'Control Panel\Sound',
                                                           EXPLORER.replace('SOFTWARE', 'Software')),
                                        (EXPLORER.replace('SOFTWARE', 'Software') + r'\User Shell Folders',)))
    if (inf / 'hivesft.inf').is_file():
        hivesft = xpreg.read_inf(inf / 'hivesft.inf')
        # Controls Folder\Desk: with its page handler, XP's Display Properties
        # applies visual styles and color schemes (themeui's ApplyPressed).
        texts.append(xpreg.hive_entries(hivesft, 'HKLM', (EXPLORER, r'SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions',
                                                          r'SOFTWARE\Microsoft\Windows\CurrentVersion\Controls Folder\Desk'),
                                        (EXPLORER + r'\Shell Folders', EXPLORER + r'\User Shell Folders')))
    # Prefixes made before these shell classes were included need them too.
    # In particular, XP's Start menu Run item is registered as a shell class;
    # without it, clicking Run... tries to create a shortcut instead.
    texts.append(xp_registrations(assets))
    # XP's Explorer plays the navigation click through SND_APPLICATION, which
    # looks the event up under the calling executable's name; the shell runs
    # as C:\xp\xp-explorer.exe, so the effective Explorer event is mirrored
    # there (in the user's language, from HIVEDEF.INF, not a hardcoded name).
    sounds = Reg()
    click = navigating_default(assets)
    mirror = r'HKCU\AppEvents\Schemes\Apps\xp-explorer\Navigating'
    sounds.add(mirror + r'\.Current', None, click, 'REG_EXPAND_SZ')
    sounds.add(mirror + r'\.Default', None, click, 'REG_EXPAND_SZ')
    # Prefixes made while folders opened in their own process keep that
    # setting (white flash on each navigation); bring them back in-process.
    sounds.add(r'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\BrowseNewProcess',
               'BrowseNewProcess', 'no')
    texts.append(sounds.text())
    startmenu = Reg()
    startmenu.add(r'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced',
                  'Start_MinMFU', '6', 'REG_DWORD')
    startmenu.add(r'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced',
                  'Start_TrackProgs', '1', 'REG_DWORD')
    texts.append(startmenu.text())
    shimgvw = assets / 'shell/shimgvw.dll'
    if shimgvw.is_file():
        native = prefix / 'drive_c/xp/native'
        shutil.copyfile(shimgvw, native / 'shimgvw.dll')
        module = {'_SYS_MOD_PATH': r'%SystemRoot%\system32\shimgvw.dll', '_MOD_PATH': r'%SystemRoot%\system32\shimgvw.dll'}
        text = xpreg.reginst(shimgvw)
        texts += [xpreg.hive_entries(text, root, ('',), extra=module) for root in ('HKCR', 'HKLM')]
        r = Reg()
        # XP's image handlers apply to files whose type is perceived as an image.
        for extension in IMAGES:
            r.add('HKCR\\' + extension, 'PerceivedType', 'image')
        for program in ('xp-explorer.exe', 'rundll32.exe', 'regsvr32.exe'):
            r.add(rf'HKCU\Software\Wine\AppDefaults\{program}\DllOverrides', 'shimgvw', 'native,builtin')
        texts.append(r.text())
        shutil.copyfile(shimgvw, prefix / 'drive_c/windows/syswow64/shimgvw.dll')
    # Wine's Unix root folder is not one of XP's desktop items; XP's Explorer
    # reads these keys for its two Start menu styles (as win4wine's prefix).
    r = Reg()
    for style in ('NewStartPanel', 'ClassicStartMenu'):
        r.add(r'HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\HideDesktopIcons' + '\\' + style,
              UNIX_ROOT, '1', 'REG_DWORD')
    texts.append(r.text())
    # XP's system icons (IDI_APPLICATION and the message box icons) are read
    # from XP's user32.dll as data (win4wine patch 0141), under a name that is
    # not taken for the loaded user32.
    user32 = assets / 'resources/user32.dll'
    if user32.is_file():
        (prefix / 'drive_c/xp/res').mkdir(parents=True, exist_ok=True)
        shutil.copyfile(user32, prefix / 'drive_c/xp/res/user32-xp.dll')
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all')
    with tempfile.TemporaryDirectory() as temp:
        reg = Path(temp) / 'defaults.reg'
        for text in texts:
            reg.write_text(text, encoding='utf-16')
            subprocess.run([wine, r'C:\windows\syswow64\regedit.exe', '/S', 'Z:' + str(reg).replace('/', '\\')],
                           env=env, check=True)
    # shimgvw's image preview control ("Preview Class") is in its ATL registry
    # script, which only its DllRegisterServer applies, as XP's setup ran it.
    if shimgvw.is_file():
        subprocess.run([wine, r'C:\windows\syswow64\regsvr32.exe', '/s', r'C:\windows\system32\shimgvw.dll'], env=env)
        # XP's DllRegisterServer writes the image ProgIDs' open commands and
        # icons with SHRegSetPath, which a Wine without win4wine's patch 0154
        # cuts in half ("...\system32\s"). Restore the viewer verb explicitly
        # after registration so Explorer can launch the real image viewer.
        image_open = Reg()
        command = r'rundll32.exe %SystemRoot%\system32\shimgvw.dll,ImageView_Fullscreen "%1"'
        for progid in IMAGE_PROGIDS:
            image_open.add(rf'HKCR\{progid}\shell\open\command', None, command, 'REG_EXPAND_SZ')
        image_open.add(r'HKCR\SystemFileAssociations\image\shell\open\command', None, command, 'REG_EXPAND_SZ')
        with tempfile.TemporaryDirectory() as temp:
            reg = Path(temp) / 'image-open.reg'
            reg.write_text(image_open.text(), encoding='utf-16')
            subprocess.run([wine, r'C:\windows\syswow64\regedit.exe', '/S', 'Z:' + str(reg).replace('/', '\\')],
                           env=env, check=True)
    # XP's names and icons for My Pictures, My Music and My Recent Documents:
    # a desktop.ini (hidden, system) in a folder marked system. An existing
    # desktop.ini is left alone.
    home = prefix / f'drive_c/users/{USER}'
    for folder, (name, icon_file, icon_index) in FOLDER_INIS.items() if folders else ():
        path = (home / folder).resolve()
        if not path.is_dir():
            continue
        ini = path / 'desktop.ini'
        text = folder_ini(name, icon_file, icon_index)
        # Ours from an earlier version is updated; the user's own is left alone.
        old = ini.read_bytes() if ini.exists() else b''
        if not old or old.startswith((b'[.ShellClassInfo]\r\nLocalizedResourceName=@%SystemRoot%',
                                      b'[.ShellClassInfo]\r\nIconResource=%SystemRoot%')):
            ini.write_text(text, encoding='latin-1')
        set_dos_attributes(ini, 0x6)
        set_dos_attributes(path, 0x4)
    # XP's icon cache keeps the folders' old icons: it is rebuilt.
    (home / 'AppData/Local/IconCache.db').unlink(missing_ok=True)
    save_shortcut(wine, prefix, env,
                  r'C:\ProgramData\Microsoft\Windows\Start Menu\Programs\Accessories\Communications\Network Connections.lnk',
                  r'C:\xp\bin\xptray.exe', 'connections', r'C:\xp\bin', r'C:\xp\tray\netshell.dll',
                  'View and manage network connections')
    save_shortcut(wine, prefix, env,
                  r'C:\ProgramData\Microsoft\Windows\Start Menu\Programs\Accessories\Communications\Wireless Network Connection.lnk',
                  r'C:\xp\bin\xptray.exe', 'wireless', r'C:\xp\bin', r'C:\xp\tray\netshell.dll',
                  'Choose an available wireless network')
    # A live prefix can empty Explorer's .Current again after the import
    # above; re-mirror the effective click now (best effort, never wipes).
    try:
        repair_navigating_sound(prefix, wine)
    except Exception:
        pass
    (prefix / '.xp-defaults').write_text(DEFAULTS_VERSION + '\n')


def install_ie(assets, prefix, wine):
    """XP's Internet Explorer 6 for the desktop's Internet icon: its files go
    to C:\\xp\\ie (stage() puts them beside the shell's iexplore.exe) and its
    engine registers itself, as XP's setup does (regsvr32)."""
    source = assets / 'ie'
    if not (source / 'iexplore.exe').is_file():
        return
    ie = prefix / 'drive_c/xp/ie'
    ie.mkdir(parents=True, exist_ok=True)
    for path in source.iterdir():
        shutil.copyfile(path, ie / path.name)
    controls = prefix / 'drive_c/xp/comctl32.dll'
    if controls.is_file():
        shutil.copyfile(controls, ie / 'comctl32.dll')
    search = copy_search(assets, prefix)
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all', WINEDLLOVERRIDES='mscoree=')
    # Bring the prefix up to date with this Wine first.
    subprocess.run([wine, 'wineboot', '-u'], env=env)
    wineserver = str(Path(shutil.which(wine) or wine).resolve().with_name('wineserver'))
    # Then with the wineserver stopped (KnownDlls), XP's files go in place.
    subprocess.run([wineserver, '-k'], env=env)
    subprocess.run([wineserver, '-w'], env=env)
    stage(prefix)
    folder = r'C:\Program Files\Internet Explorer'
    with tempfile.TemporaryDirectory() as temp:
        reg = Path(temp) / 'ie.reg'
        for text in (ie_registry(Reg()).text(), xp_registrations(assets)):
            reg.write_text(text, encoding='utf-16')
            subprocess.run([wine, r'C:\windows\syswow64\regedit.exe', '/S', 'Z:' + str(reg).replace('/', '\\')],
                           env=env, check=True)
    for dll in IE_REGISTER:
        subprocess.run([wine, r'C:\windows\syswow64\regsvr32.exe', '/s', f'{folder}\\{dll}'], env=env)
    if search:
        register_search(wine, env)
    install_wmp(assets, prefix, wine, env)
    # Wine's updates (wineboot) register Wine's own mshtml and wmp again: xp.sh
    # runs this again when the Wine build differs from the one recorded here.
    (prefix / '.ie-installed').write_text(wine_build(wine) + '\n')


def copy_search(assets, prefix):
    """The Search Companion's files where XP's setup puts them (srchasst.inf,
    and Microsoft Agent's in %windir%\\msagent), and XP's msxml3 and oleacc
    for stage(). False without them in the assets."""
    source = assets / 'srchasst'
    if not (source / 'srchui.dll').is_file():
        return False
    windows = prefix / 'drive_c/windows'
    lcid = '0411' if media.language(assets) == 'ja' else '0409'
    for folder, target in (('.', windows / 'srchasst'), ('chars', windows / 'srchasst/chars'),
                           ('mui', windows / 'srchasst/mui' / lcid), ('msagent', windows / 'msagent'),
                           ('msagent/intl', windows / 'msagent/intl'), ('system', prefix / 'drive_c/xp/native')):
        target.mkdir(parents=True, exist_ok=True)
        for path in (source / folder).glob('*'):
            if path.is_file():
                shutil.copyfile(path, target / path.name)
    return True


def register_search(wine, env):
    """What XP's setup registers for the Search Companion (syssetup.inf):
    Microsoft Agent, which animates Rover, and srchui.dll, whose DllInstall
    puts the Search Companion in place of the Windows 2000 search pane
    (FileFindBandHook) and of IE's Search Assistant; and XP's msxml3, for
    the types IE6 shows with it."""
    regsvr32 = r'C:\windows\syswow64\regsvr32.exe'
    agent = r'C:\windows\msagent'
    subprocess.run([wine, agent + r'\agentsvr.exe', '/regserver'], env=env)
    # agentctl.dll runs "AgentSvr /REGSERVER" from the search path.
    for dll in AGENT_REGISTER:
        subprocess.run([wine, regsvr32, '/s', f'{agent}\\{dll}'], env=dict(env, WINEPATH=agent))
    srchui = r'C:\windows\srchasst\srchui.dll'
    subprocess.run([wine, regsvr32, '/s', srchui], env=env)
    subprocess.run([wine, regsvr32, '/s', '/n', '/i', srchui], env=env)
    subprocess.run([wine, regsvr32, '/s', r'C:\windows\system32\msxml3.dll'], env=env)


WMP_REGISTER = ('wmp.dll', 'wmpdxm.dll', 'wmpshell.dll', 'wmpasf.dll', 'mpvis.dll', 'wmpcore.dll',
                'qasf.dll', 'wmvcore.dll')


def install_wmp(assets, prefix, wine, env):
    """Windows Media Player 9 from the user's ISO, beside its wmplayer.exe, with
    what XP's setup did: wmp.inf's registry (InstallWMP7 and the per user
    PerUserStub), its DLLs' self-registration, Open With and Play verbs, and a
    Start menu shortcut. Wine's own DirectShow (quartz, with GStreamer) plays
    the media."""
    import xpreg
    source = assets / 'wmp'
    if not (source / 'wmplayer.exe').is_file():
        return
    for folder in ('Program Files', 'Program Files (x86)'):
        target = prefix / 'drive_c' / folder / 'Windows Media Player'
        target.mkdir(parents=True, exist_ok=True)
        for path in source.iterdir():
            shutil.copyfile(path, target / path.name)
        # Made by earlier versions, whose unexpanded paths the player took
        # for a folder of its own.
        shutil.rmtree(target / '%ProgramFiles%', ignore_errors=True)
    r = Reg()
    natives = [p.stem for p in source.glob('*.dll')]
    r.add(r'HKCU\Software\Wine\DllOverrides', 'wmplayer.exe', 'native')
    for program in ('wmplayer.exe', 'setup_wm.exe', 'unregmp2.exe', 'mplayer2.exe', 'regsvr32.exe'):
        for dll in natives:
            r.add(rf'HKCU\Software\Wine\AppDefaults\{program}\DllOverrides', dll,
                  'native,builtin' if program == 'regsvr32.exe' else 'native')
    # wmp.inf's PerUserStub sets AcceptedPrivacyStatement to 0 below: as on
    # XP, the first start shows the player's privacy and file types wizard.
    preferences = r'HKCU\Software\Microsoft\MediaPlayer\Preferences'
    r.add(preferences, 'FirstTime', '1', 'REG_DWORD')
    texts = [r.text()]
    inf = xpreg.read_inf(source / 'wmp.inf')
    # XP's setup writes these folders expanded: most of wmp.inf's values are
    # REG_SZ, and wmplayer.exe and setup_wm.exe use them as they are (the
    # current directory from App Paths, then setup_wm.exe found in it; the
    # player started again after the first run's wizard).
    folder = r'C:\Program Files\Windows Media Player'
    # wmp.inf's system32 files are beside wmplayer.exe here.
    dirs = {'11': folder, '16422': r'C:\Program Files', '49001': r'C:\Program Files', '49301': folder,
            '24': 'C:', '10': r'C:\windows'}
    runonce = (r'Software\Microsoft\Windows\CurrentVersion\RunOnce',)
    for root in ('HKLM', 'HKCR', 'HKCU'):
        texts.append(xpreg.hive_entries(inf, root, ('',), runonce, extra=dirs, install=('InstallWMP7', 'PerUserStub')))
    with tempfile.TemporaryDirectory() as temp:
        reg = Path(temp) / 'wmp.reg'
        for text in texts:
            reg.write_text(text, encoding='utf-16')
            subprocess.run([wine, r'C:\windows\syswow64\regedit.exe', '/S', 'Z:' + str(reg).replace('/', '\\')],
                           env=env, check=True)
    for dll in WMP_REGISTER:
        subprocess.run([wine, r'C:\windows\syswow64\regsvr32.exe', '/s',
                        rf'C:\Program Files (x86)\Windows Media Player\{dll}'], env=env)
    r = Reg()
    player = r'"%ProgramFiles%\Windows Media Player\wmplayer.exe"'
    r.add(r'HKCR\Applications\wmplayer.exe\shell\open\command', None, player + r' /Open "%1"',
          'REG_EXPAND_SZ')
    r.add(r'HKCR\Applications\wmplayer.exe\shell\play\command', None, player + r' /Play "%1"',
          'REG_EXPAND_SZ')
    for media_type in ('audio', 'video'):
        verb = rf'HKCR\SystemFileAssociations\{media_type}\shell\play'
        r.add(verb, 'MUIVerb', 'Play with Windows Media Player')
        r.add(verb + r'\command', None, player + r' /Play "%1"', 'REG_EXPAND_SZ')
    for extension in WMP_EXTENSIONS:
        r.add(rf'HKCR\{extension}\OpenWithList\wmplayer.exe', None, '')
    with tempfile.TemporaryDirectory() as temp:
        reg = Path(temp) / 'wmp-shell.reg'
        reg.write_text(r.text(), encoding='utf-16')
        subprocess.run([wine, r'C:\windows\syswow64\regedit.exe', '/S', 'Z:' + str(reg).replace('/', '\\')],
                       env=env, check=True)
    player_path = r'C:\Program Files\Windows Media Player'
    save_shortcut(wine, prefix, env,
                  r'C:\ProgramData\Microsoft\Windows\Start Menu\Programs\Accessories\Entertainment\Windows Media Player.lnk',
                  player_path + r'\wmplayer.exe', directory=player_path, icon=player_path + r'\wmplayer.exe',
                  description='Windows Media Player')


def wine_build(wine):
    """An identifier of the Wine build: win4wine's patch digest, or the version."""
    install = Path(shutil.which(wine) or wine).resolve().parents[1]
    # "+media4": IE6, Windows Media Player 9 and its Start menu integration,
    # and the Search Companion.
    if (install / 'win4wine-patches').is_file():
        return (install / 'win4wine-patches').read_text().strip() + '+media4'
    return subprocess.run([wine, '--version'], capture_output=True, text=True).stdout.strip() + '+media4'


def wallpaper(assets, prefix, size=None):
    """XP converts its JPEG wallpaper to Wallpaper1.bmp; do the same with GdkPixbuf.
    Wine's desktop only centres the bitmap, so it is stretched here to SIZE."""
    source = assets / 'web/wallpaper/bliss.jpg'
    target = prefix / f'drive_c/users/{USER}/AppData/Local/Microsoft/Wallpaper1.bmp'
    if not source.is_file():
        return
    import gi
    gi.require_version('GdkPixbuf', '2.0')
    from gi.repository import GdkPixbuf
    target.parent.mkdir(parents=True, exist_ok=True)
    image = GdkPixbuf.Pixbuf.new_from_file(str(source))
    if size:
        image = image.scale_simple(size[0], size[1], GdkPixbuf.InterpType.BILINEAR)
    image.savev(str(target), 'bmp', [], [])
    bliss = prefix / 'drive_c/windows/Web/Wallpaper/Bliss.jpg'
    bliss.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, bliss)
    # XP's "Windows XP" theme (Luna.theme) names %WinDir%web\wallpaper\Bliss.bmp.
    if not bliss.with_suffix('.bmp').is_file():
        GdkPixbuf.Pixbuf.new_from_file(str(source)).savev(str(bliss.with_suffix('.bmp')), 'bmp', [], [])


def create(assets, prefix, wine):
    assets, prefix = assets.resolve(strict=True), prefix.resolve()
    if prefix.exists():
        raise SystemExit(f'Refusing existing prefix: {prefix} (./xp.sh reset to start over)')
    if not (assets / 'windows/explorer.exe').is_file():
        raise SystemExit('Incomplete XP assets: run the extraction first')
    for helper in ('theme-settings.exe', 'startmenu.exe', 'startmenu-view.exe', 'xptray.exe', 'linux-launch.exe',
                   'linux-links.exe'):
        if not (BIN / helper).is_file():
            raise SystemExit(f'Missing {helper}: run helpers/build.sh first')
    language = media.language(assets)
    win4wine = patched(wine)
    locale = media.LOCALES[language]
    wineserver = str(Path(shutil.which(wine) or wine).resolve().with_name('wineserver'))
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEARCH='win64', LANG=locale, LC_ALL=locale,
               WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d', WINEDEBUG='-all')

    def wine_run(*args, extra=None):
        subprocess.run([wine, *args], env=dict(env, **(extra or {})), check=True)

    print('* Initialising the Wine prefix (a minute or two)...', flush=True)
    wine_run('wineboot', '-u')
    subprocess.run([wineserver, '-w'], env=env, check=True)
    windows = prefix / 'drive_c/windows'
    system = windows / 'syswow64'
    if not system.is_dir():
        raise SystemExit('Expected a 64-bit (WoW64) prefix')

    print('* Copying the XP shell files...', flush=True)
    xp = prefix / 'drive_c/xp'
    (xp / 'native').mkdir(parents=True)
    shutil.copytree(BIN, xp / 'bin')
    shutil.copyfile(assets / 'windows/explorer.exe', xp / 'xp-explorer.exe')
    if (assets / 'tray').is_dir():
        shutil.copytree(assets / 'tray', xp / 'tray')
    for dll in list((assets / 'system32').glob('*.dll')) + list((assets / 'cpl').glob('*')) + \
            list((assets / 'shell').glob('*')):
        shutil.copyfile(dll, xp / 'native' / dll.name)
    for font in (assets / 'fonts').glob('*'):
        shutil.copyfile(font, windows / 'Fonts' / font.name)
    if (assets / 'themes').exists():
        # Into Wine's own folder (windows/resources/themes, lower case): on Linux
        # a second "Resources/Themes" folder would hide one of the two from
        # XP's Display Properties, which list the folder.
        themes = windows / 'resources/themes'
        if not themes.is_dir() and (windows / 'Resources/Themes').is_dir():
            themes = windows / 'Resources/Themes'
        shutil.copytree(assets / 'themes', themes, dirs_exist_ok=True)
    # XP's common controls 6 (SP3's 6.0.2600.5512): XP's taskbar and toolbars need it.
    # With win4wine's Wine (patch 0149) XP Explorer takes it from beside itself and
    # the assembly goes in winsxs as win4wine does; with a stock Wine, XP's v6
    # replaces comctl32 in syswow64 for XP Explorer (stage() drops Wine's x86 v6).
    for manifest in (assets / 'sxs').rglob('controls.man'):
        identity = ET.parse(manifest).getroot().find('{urn:schemas-microsoft-com:asm.v1}assemblyIdentity')
        dll = manifest.with_name('comctl32.dll')
        if identity is None or not dll.is_file():
            continue
        attrs = identity.attrib
        if (attrs.get('processorArchitecture') != 'x86' or attrs.get('version') != '6.0.2600.5512'
                or attrs.get('name') != 'Microsoft.Windows.Common-Controls'):
            continue
        shutil.copyfile(dll, xp / 'comctl32.dll')
        if win4wine:
            name = 'x86_microsoft.windows.common-controls_6595b64144ccf1df_6.0.2600.5512_none_deadbeef'
            (windows / 'winsxs' / name).mkdir(parents=True, exist_ok=True)
            shutil.copyfile(dll, windows / 'winsxs' / name / 'comctl32.dll')
            (windows / 'winsxs/manifests').mkdir(exist_ok=True)
            shutil.copyfile(manifest, windows / 'winsxs/manifests' / (name + '.manifest'))
        else:
            shutil.copyfile(dll, xp / 'native/comctl32.dll')
    if win4wine:
        (prefix / '.patched-wine').write_text('1\n')
        lib = wine_lib(wine)
        shutil.copyfile(lib / 'x86_64-windows/win4wine-controls.dll', windows / 'system32/win4wine-controls.dll')
        shutil.copyfile(lib / 'i386-windows/win4wine-controls.dll', system / 'win4wine-controls.dll')
    home = prefix / 'dosdevices/h:'
    if not home.exists():
        home.symlink_to(Path.home())
    pictures = prefix / 'drive_c/ProgramData/Microsoft/User Account Pictures'
    picture = assets / 'pictures/chess.bmp'
    if picture.is_file():
        pictures.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(picture, pictures / f'{USER}.bmp')
    wallpaper(assets, prefix)

    print('* Writing the registry profile...', flush=True)
    with tempfile.TemporaryDirectory() as temp:
        reg = Path(temp) / 'xp.reg'
        reg.write_text(registry().text(), encoding='utf-16')
        windows_reg = 'Z:' + str(reg).replace('/', '\\')
        # The 32-bit regedit writes the view that the 32-bit XP shell reads.
        wine_run(r'C:\windows\syswow64\regedit.exe', '/S', windows_reg)
        reg.write_text(xp_registrations(assets), encoding='utf-16')
        wine_run(r'C:\windows\syswow64\regedit.exe', '/S', windows_reg)
        if language != 'ja':
            fonts = Reg()
            fonts.add(r'HKLM\Software\Microsoft\Windows NT\CurrentVersion\FontSubstitutes',
                      'MS Shell Dlg', 'Microsoft Sans Serif')
            reg.write_text(fonts.text(), encoding='utf-16')
            wine_run('regedit', '/S', windows_reg)
    subprocess.run([wineserver, '-w'], env=env, check=True)

    print('* Applying the Luna theme and the XP Start menu...', flush=True)
    wine_run(r'C:\xp\bin\theme-settings.exe', 'luna',
             extra={'WINEDLLOVERRIDES': 'uxtheme=b;mscoree,mshtml=', 'WIN4WINE_EXTERNAL_SHELL': '1'})
    # The running wineserver keeps Wine's shell32 mapped; restart it so XP's is used.
    subprocess.run([wineserver, '-k'], env=env)
    # Then with the wineserver stopped (KnownDlls), XP's files go in place.
    subprocess.run([wineserver, '-k'], env=env)
    subprocess.run([wineserver, '-w'], env=env)
    stage(prefix)
    wine_run(r'C:\xp\bin\startmenu.exe', 'panel')
    wine_run(r'C:\xp\bin\startmenu.exe', 'webview')
    (prefix / 'xp-shell.json').write_text(json.dumps({'assets': str(assets), 'wine': wine,
                                                      'language': language}, indent=2) + '\n')

    print("* XP's sounds, Explorer settings and image previews...", flush=True)
    xp_defaults(assets, prefix, wine)

    print('* Installing Internet Explorer 6...', flush=True)
    install_ie(assets, prefix, wine)

    print('* Adding the Linux applications to All Programs...', flush=True)
    subprocess.run([sys.executable, str(ROOT / 'scripts/linux_apps.py'), str(prefix), '--wine', wine],
                   env=env, check=True)
    subprocess.run([sys.executable, str(ROOT / 'scripts/pins.py'), str(prefix), '--wine', wine], env=env, check=True)
    subprocess.run([wineserver, '-w'], env=env)
    print(f'Prefix ready: {prefix}')


if __name__ == '__main__':
    if sys.argv[1:2] == ['--defaults-version']:
        print(DEFAULTS_VERSION)
        raise SystemExit
    if sys.argv[1:2] == ['--defaults']:
        # --defaults PREFIX [--wine WINE]: XP's shell defaults on an existing prefix
        prefix = Path(sys.argv[2]).resolve(strict=True)
        info = json.loads((prefix / 'xp-shell.json').read_text())
        xp_defaults(Path(info['assets']), prefix, sys.argv[4] if sys.argv[3:4] == ['--wine'] else info['wine'])
        raise SystemExit
    if sys.argv[1:2] == ['--wine-build']:
        print(wine_build(sys.argv[2]))
        raise SystemExit
    if sys.argv[1:2] == ['--ie']:
        # --ie PREFIX [--wine WINE]: add Internet Explorer 6 to an existing prefix
        prefix = Path(sys.argv[2]).resolve(strict=True)
        info = json.loads((prefix / 'xp-shell.json').read_text())
        wine = sys.argv[4] if sys.argv[3:4] == ['--wine'] else info['wine']
        install_ie(Path(info['assets']), prefix, wine)
        raise SystemExit
    if sys.argv[1:2] == ['--repair-sounds']:
        # --repair-sounds PREFIX [--wine WINE]: re-mirror the navigation click
        # on every start (a live prefix can empty Explorer's .Current again).
        prefix = Path(sys.argv[2]).resolve(strict=True)
        info = json.loads((prefix / 'xp-shell.json').read_text())
        wine = sys.argv[4] if sys.argv[3:4] == ['--wine'] else info['wine']
        repair_navigating_sound(prefix, wine)
        raise SystemExit
    if sys.argv[1:2] == ['--stage']:
        # --stage PREFIX [WxH]: before each start of the desktop
        prefix = Path(sys.argv[2]).resolve(strict=True)
        stage(prefix)
        if len(sys.argv) > 3:
            info = json.loads((prefix / 'xp-shell.json').read_text())
            wallpaper(Path(info['assets']), prefix, tuple(int(v) for v in sys.argv[3].split('x')))
        raise SystemExit
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assets', type=Path)
    parser.add_argument('prefix', type=Path)
    parser.add_argument('--wine', default=os.environ.get('WINE', 'wine'))
    args = parser.parse_args()
    create(args.assets, args.prefix, args.wine)
