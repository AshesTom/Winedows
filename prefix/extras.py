#!/usr/bin/env python3
"""Add XP Explorer's additions (xp-explorer/) to the XP shell session's prefix.

XP Explorer runs the same XP shell in a Wine virtual desktop and added to it:
the notification area's network, battery and volume icons with XP's own
flyouts (xptray.exe), Network Connections, the folder task pane (DirectUser),
XP's Display Properties, image thumbnails and the picture viewer, XP's sound
scheme and Explorer defaults, Internet Explorer 6, Windows Media Player 9, and
the Start menu's full left column (Internet, E-mail and the most used
programs); and XP's Regional and Language Options, whose input languages
the session's keyboard layout follows (session/keyboard.py), and XP's Help
and Support Center. This applies them to a prefix made by prefix/create.py, with
xp-explorer/scripts/prefix.py's own code, and again when they change.

  extras.py PREFIX --wine WINE      apply (scripts/setup.sh runs it)
  extras.py PREFIX --check WINE     exit 1 when they need applying again
  extras.py PREFIX --stage          put XP's files back after a Wine update
"""
import argparse
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import xp_schemes

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = ROOT / '.local/bridge'
# Bumped when apply() changes; setup.sh applies it again on existing prefixes.
VERSION = '31'
MARKER = '.xp-extras'
HELPERS = ('xptray.exe', 'xpmix.dll', 'startmenu.exe', 'startmenu-view.exe', 'iexplore.exe', 'linux-links.exe',
           'xpnetcon.exe')
# XP's Regional and Language Options and what it loads (assets/intl), native
# in rundll32 and in XP Explorer, whose Control Panel lists it.
INTL = ('intl.cpl', 'input.dll', 'syssetup.dll', 'sfcfiles.dll')
INTL_NATIVE = ('intl.cpl', 'input', 'syssetup', 'sfcfiles')
LAYOUTS_KEY = r'System\CurrentControlSet\Control\Keyboard Layouts'
# XP's User Accounts (assets/accounts) in syswow64, as XP installs them:
# nusrmgr.cpl starts %SystemRoot%\system32\mshta.exe on its HTML
# application, whose pages script shgina.dll's user objects.
ACCOUNTS = ('nusrmgr.cpl', 'shgina.dll', 'mshta.exe')
# XP's Add or Remove Programs (assets/appwiz) in syswow64 over Wine's
# appwiz.cpl, native in the programs that start Control Panel items, and the
# SP1 texts it loads. Its shell application manager enumerates the installed
# programs; appwiz.cpl's REGINST registers it with these classes.
APPWIZ = ('appwiz.cpl', 'xpsp1res.dll')
# Internet Options and Automatic Updates, the Control Panel items of the
# Security Center's category (10) with Windows Firewall, which needs XP's
# firewall service and is left out.
SECURITY = ('inetcpl.cpl', 'inetcplc.dll', 'wuaucpl.cpl')
APPWIZ_CLASSES = (('{352EC2B7-8B9A-11D1-B8AE-006008059382}', 'Shell Application Manager'),
                  ('{0B124F8F-91F0-11D1-B8B5-006008059382}', 'Installed Apps Enumerator'),
                  ('{CFCCC7A0-A282-11D1-9082-006008059382}', 'Darwin App Publisher'))
# XP's Network Connections folder in syswow64: netshell.dll (assets/tray) and
# the XP libraries it loads (assets/netcon), with ours for the dialogs it
# loads that Wine has as stubs or not at all (.local/bridge/netcon), native in
# the shell's processes. xpnetcon.exe stands in for XP's Network Connections
# service (netman), with the Linux adapters (xp-explorer/helpers/xpnetcon.c).
NETCON = ('netshell.dll', 'rtutils.dll', 'dot3api.dll', 'wzcdlg.dll', 'xpsp3res.dll')
# XP's wireless dialogs (wzcdlg.dll) get the networks from NetworkManager
# through our wzcsapi.dll (xp-explorer/helpers/netshell-wzcsapi.c).
NETCON_BRIDGE = ('rasdlg.dll', 'dot3dlg.dll', 'wzcsapi.dll')
NETCON_NATIVE = ('rtutils', 'rasdlg')
# netman's connection manager (and its INetConnectionManager2) and its LAN
# connection objects.
NETCON_CLASSES = ('{BA126AD1-2166-11D1-B1D0-00805FC1270E}', '{BA126AE5-2166-11D1-B1D0-00805FC1270E}',
                  '{BA126ADB-2166-11D1-B1D0-00805FC1270E}')
# XP's Compressed (zipped) Folders in syswow64 (Wine has no zipfldr.dll):
# .zip files with its icon, opened as folders, and Send To.
FILE_TYPES = ('zipfldr.dll',)


def xp_explorer():
    """xp-explorer/scripts/prefix.py, with its helpers taken from .local/bridge."""
    spec = importlib.util.spec_from_file_location('xp_explorer_prefix', ROOT / 'xp-explorer/scripts/prefix.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.BIN = BRIDGE
    return module


def registry(xpx):
    """What XP Explorer's registry profile adds to the session's (prefix/registry.py)."""
    r = xpx.Reg()
    cu = 'HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\'
    advanced = cu + r'Explorer\Advanced'
    # XP's Internet Explorer 6 (and the Linux browser stand-in, also named
    # iexplore.exe); IE6's engine is native in it only.
    xpx.ie_registry(r)
    # DirectUser (the folder task pane), mydocs and XP's Display Properties,
    # native in XP Explorer and in rundll32, which XP's shell32 starts
    # Control Panel items with.
    cpl = tuple(c.rsplit('.', 1)[0] if c.endswith('.dll') else c for c in xpx.CPL)
    for dll in ('duser', 'mydocs') + cpl:
        r.add(r'HKCU\Software\Wine\AppDefaults\xp-explorer.exe\DllOverrides', dll, 'native')
    for dll in xpx.NATIVE + cpl + INTL_NATIVE:
        r.add(r'HKCU\Software\Wine\AppDefaults\rundll32.exe\DllOverrides', dll, 'native,builtin')
    for dll in INTL_NATIVE:
        r.add(r'HKCU\Software\Wine\AppDefaults\xp-explorer.exe\DllOverrides', dll, 'native')
    # Add or Remove Programs and the Security Center's items, from the Control
    # Panel and from control.exe, which hands them to rundll32 (Wine patch 0171).
    for program in ('xp-explorer.exe', 'rundll32.exe'):
        for dll in ('appwiz.cpl', 'inetcpl.cpl', 'inetcplc', 'wuaucpl.cpl'):
            r.add(rf'HKCU\Software\Wine\AppDefaults\{program}\DllOverrides', dll,
                  'native' if program == 'xp-explorer.exe' else 'native,builtin')
    for clsid, name in APPWIZ_CLASSES:
        r.add('HKCR\\CLSID\\' + clsid, None, name)
        r.add('HKCR\\CLSID\\' + clsid + '\\InProcServer32', None, r'%SystemRoot%\system32\appwiz.cpl',
              'REG_EXPAND_SZ')
        r.add('HKCR\\CLSID\\' + clsid + '\\InProcServer32', 'ThreadingModel', 'Apartment')
        r.add(r'HKLM\Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved', clsid, name)
    # User Accounts: XP's mshta.exe instead of Wine's stub, with IE6's engine,
    # XP's shell32 (the account picture) and themeui (Theme.Manager, which
    # picks the Luna style sheet of its pages) native in it.
    r.add(r'HKCU\Software\Wine\DllOverrides', 'mshta.exe', 'native')
    for dll in xpx.NATIVE + cpl + xpx.IE_NATIVE + xpx.SEARCH_NATIVE:
        r.add(r'HKCU\Software\Wine\AppDefaults\mshta.exe\DllOverrides', dll, 'native,builtin')
    # The content types that IE6's setup registers (mshtml's FileAssoc.Inst
    # and MIME.Inst), which its DllRegisterServer leaves out: without them
    # IE6 neither applies the pages' behaviors (.htc) nor draws their .ico
    # images.
    for extension, kind in (('.css', 'text/css'), ('.htc', 'text/x-component'), ('.ico', 'image/x-icon')):
        r.add('HKCR\\' + extension, 'Content Type', kind)
    component = r'HKCR\MIME\Database\Content Type\text/x-component'
    r.add(component, 'CLSID', '{3050f4f8-98b5-11cf-bb82-00aa00bdce0b}')
    r.add(component, 'Extension', '.htc')
    # Network Connections: XP's rtutils and our rasdlg in the shell's
    # processes (and in regsvr32, which registers netshell), Wine's elsewhere.
    for program in ('xp-explorer.exe', 'rundll32.exe', 'regsvr32.exe'):
        for dll in NETCON_NATIVE:
            r.add(rf'HKCU\Software\Wine\AppDefaults\{program}\DllOverrides', dll, 'native,builtin')
    for clsid in NETCON_CLASSES:
        r.add('HKCR\\CLSID\\' + clsid + '\\LocalServer32', None, r'C:\xp\bin\xpnetcon.exe')
    # The Start menu helpers use XP's own shell interfaces and icons.
    for program in ('startmenu.exe', 'startmenu-view.exe'):
        for dll in ('shell32', 'shlwapi', 'comctl32', 'linkinfo', 'shdocvw', 'browseui'):
            r.add(rf'HKCU\Software\Wine\AppDefaults\{program}\DllOverrides', dll, 'native')
    # XP's task pane on the left of folder windows ("Use common tasks in folders").
    r.add(advanced, 'WebView', '1', 'REG_DWORD')
    r.add(advanced, 'Start_ShowRun', '1', 'REG_DWORD')
    # XP's default right column, without My Network Places.
    r.add(advanced, 'Start_ShowNetPlaces', '0', 'REG_DWORD')
    # XP's native most-used list is empty under Wine; startmenu-view.exe
    # paints the six usage-ranked entries below the defaults.
    r.add(advanced, 'Start_MinMFU', '6', 'REG_DWORD')
    r.add(advanced, 'Start_TrackProgs', '1', 'REG_DWORD')
    # XP's first logon would reset the Start menu's usage data and pins.
    r.add(advanced, 'StartMenuInit', '2', 'REG_DWORD')
    # The notification area's icons start with the shell, as XP's systray.exe did.
    r.add(cu + 'Run', 'XPTray', r'C:\xp\bin\xptray.exe')
    # XP's shell32 registers this key itself (Open With's lists); the Start menu reads it too.
    associations = r'HKLM\Software\Microsoft\Windows\CurrentVersion\Explorer\FileAssociation'
    r.add(associations, 'KillList', '%1;explorer.exe;dvdplay.exe;mplay32.exe;msohtmed.exe;quikview.exe;'
          'rundll.exe;rundll32.exe;taskman.exe;bck32api.dll;')
    r.add(associations, 'CutList', ['Application File', 'MFC Application'], 'REG_MULTI_SZ')
    # Display Properties lists the wallpapers of this folder (and of the
    # Windows folder and My Pictures); IE6's setup (IE.INF) names it.
    r.add(r'HKLM\Software\Microsoft\Windows\CurrentVersion', 'WallPaperDir', r'%SystemRoot%\Web\Wallpaper',
          'REG_EXPAND_SZ')
    # XP's .theme files name their visual style with %ResourceDir%.
    r.add(r'HKLM\System\CurrentControlSet\Control\Session Manager\Environment', 'ResourceDir',
          r'%SystemRoot%\resources', 'REG_EXPAND_SZ')
    # The folder task pane, Display Properties' pages and the Start menu's
    # other classes (xp-explorer/scripts/prefix.py SERVERS).
    for clsid, dll in xpx.SERVERS:
        r.add('HKCR\\CLSID\\' + clsid + '\\InprocServer32', None, dll)
        r.add('HKCR\\CLSID\\' + clsid + '\\InprocServer32', 'ThreadingModel', 'Apartment')
    return r


def keyboard_layouts(assets):
    """XP's keyboard layouts (HKLM's Keyboard Layouts), as XP's setup
    registers them: HIVESYS.INF's US layouts and INTL.INF's language groups.
    input.dll lists them; without the key it reports an incompatible
    keyboard driver. Input methods (IME) are left out: they need their own
    files."""
    import xpreg
    intl = assets / 'intl'
    texts = [xpreg.hive_entries(xpreg.read_inf(intl / 'hivesys.inf'), 'HKLM', (LAYOUTS_KEY,))]
    raw = (intl / 'intl.inf').read_bytes()
    text = raw.decode('utf-16') if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else raw.decode('latin-1')
    # Its strings and the sections that add the layouts of each language
    # group, named [AddReg] for xpreg (not the ones that remove them).
    lines, section = [], None
    for line in text.splitlines():
        if line.strip().startswith('['):
            section = line.strip().strip('[]').lower()
            if section == 'strings' or section.startswith('kbd.lgreg.install.'):
                lines.append('[Strings]' if section == 'strings' else '[AddReg]')
        elif section == 'strings' or (section or '').startswith('kbd.lgreg.install.'):
            lines.append(line)
    texts.append(xpreg.hive_entries('\n'.join(lines), 'HKLM', (LAYOUTS_KEY,)))
    return texts


def regedit(wine, prefix, text):
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all')
    env.pop('WINEARCH', None)
    with tempfile.TemporaryDirectory() as temp:
        reg = Path(temp) / 'extras.reg'
        reg.write_text(text, encoding='utf-16')
        # The 32-bit regedit writes the view that the 32-bit XP shell reads.
        subprocess.run([wine, r'C:\windows\syswow64\regedit.exe', '/S', 'Z:' + str(reg).replace('/', '\\')],
                       env=env, check=True)


def stop(wine, prefix):
    env = dict(os.environ, WINEPREFIX=str(prefix))
    server = str(Path(shutil.which(wine) or wine).resolve().with_name('wineserver'))
    subprocess.run([server, '-k'], env=env, check=False, timeout=30)
    subprocess.run([server, '-w'], env=env, check=False, timeout=120)


def copy_files(assets, prefix):
    xp = prefix / 'drive_c/xp'
    (xp / 'bin').mkdir(parents=True, exist_ok=True)
    for helper in HELPERS:
        shutil.copyfile(BRIDGE / helper, xp / 'bin' / helper)
    if (assets / 'tray').is_dir():
        shutil.copytree(assets / 'tray', xp / 'tray', dirs_exist_ok=True)
    native = xp / 'native'
    for path in [assets / 'system32/duser.dll', *(assets / 'cpl').glob('*'), *(assets / 'shell').glob('*')]:
        if path.is_file():
            shutil.copyfile(path, native / path.name)
    # XP's Windows Classic theme for Display Properties. A prefix has both
    # Wine's windows/resources/themes and win4wine's Resources/Themes; on
    # Linux they are two folders, and Display Properties lists one of them.
    windows = prefix / 'drive_c/windows'
    for name in ('Resources/Themes', 'resources/themes'):
        if (assets / 'themes').is_dir() and (windows / name).is_dir():
            shutil.copytree(assets / 'themes', windows / name, dirs_exist_ok=True)
    # XP's wallpapers where its setup puts them: the JPEG ones in
    # Web\Wallpaper, the bitmaps in the Windows folder (extract.py).
    for group, target in (('web', windows / 'Web/Wallpaper'), ('windows', windows)):
        for path in sorted((assets / 'wallpaper' / group).glob('*')):
            target.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target / path.name)
    if (assets / 'intl/input.dll').is_file():
        for name in INTL:
            shutil.copyfile(assets / 'intl' / name, native / name)
        (windows / 'inf').mkdir(exist_ok=True)
        shutil.copyfile(assets / 'intl/intl.inf', windows / 'inf/intl.inf')
    if (assets / 'accounts/nusrmgr.cpl').is_file():
        for name in ACCOUNTS:
            shutil.copyfile(assets / 'accounts' / name, native / name)
    if (assets / 'appwiz/appwiz.cpl').is_file():
        for name in APPWIZ:
            shutil.copyfile(assets / 'appwiz' / name, native / name)
    if (assets / 'security/wuaucpl.cpl').is_file():
        for name in SECURITY:
            shutil.copyfile(assets / 'security' / name, native / name)
    if (assets / 'netcon/wzcdlg.dll').is_file() and (assets / 'tray/netshell.dll').is_file():
        for name in NETCON:
            shutil.copyfile(assets / ('tray' if name == 'netshell.dll' else 'netcon') / name, native / name)
    if (assets / 'filetypes/hivecls.inf').is_file():
        for name in FILE_TYPES:
            shutil.copyfile(assets / 'filetypes' / name, native / name)
        # Only its icon: Windows Installer packages show msiexec.exe's, and
        # Wine's msiexec.exe stays the one that installs them.
        (xp / 'res').mkdir(exist_ok=True)
        shutil.copyfile(assets / 'filetypes/msiexec.exe', xp / 'res/msiexec.exe')
    # Ours, so that XP Explorer's stage() keeps the common controls 6 assembly
    # in winsxs and puts XP's comctl32 beside rundll32 (Wine patch 0149).
    (prefix / '.patched-wine').write_text('1\n')


def file_types(assets, prefix):
    """XP's file types as its setup registers them, as .reg texts: HIVECLS.INF's
    extensions and their types (names, icons, verbs), the Compressed (zipped)
    Folders of zipfldr.dll's REGINST, and the icon that msi.dll gives Windows
    Installer packages and patches (msiexec.exe's first). A verb or an icon
    whose program the prefix does not have is left out."""
    import xpreg
    source = assets / 'filetypes'
    if not (source / 'hivecls.inf').is_file():
        return []
    text = xpreg.read_inf(source / 'hivecls.inf')
    windows = prefix / 'drive_c/windows'

    def program(command):
        """The file of the program (or rundll32's DLL) that COMMAND names, or None."""
        command = command.strip().lstrip('@')
        if command.startswith('"'):
            name, _, rest = command[1:].partition('"')
        else:
            name, _, rest = command.partition(' ')
        name = name.split(',')[0].strip()
        if name.lower().endswith('rundll32.exe') and rest.strip():
            name = rest.strip().split(',')[0].strip('"')
        path = re.sub(r'%+(SystemRoot|windir)%+', r'C:\\windows', name, flags=re.IGNORECASE)
        if not re.match(r'^[A-Za-z]:\\', path):
            folders = ('', 'syswow64', 'system32')
            candidates = [Path(f) / path for f in folders]
            candidates += [Path(f) / (path + '.dll') for f in folders if '.' not in path]
            root = windows
        elif path[3:].lower().startswith('windows\\'):
            relative = path[len('C:\\windows\\'):]
            candidates = [Path(relative.replace('\\', '/'))]
            if relative.lower().startswith('system32\\'):
                # The 32-bit XP shell reads syswow64 for system32.
                candidates.insert(0, Path('syswow64') / relative[len('system32\\'):].replace('\\', '/'))
            root = windows
        else:
            candidates, root = [Path(path[3:].replace('\\', '/'))], prefix / 'drive_c'
        return next((f for f in (find_nocase(root, c) for c in candidates) if f), None)

    def xp_file(command):
        """Whether COMMAND's file is XP's: icons and strings are found by
        number, which Wine's own files number otherwise."""
        path = program(command)
        if not path:
            return False
        with open(path, 'rb') as stream:
            return b'Wine builtin DLL' not in stream.read(256)

    # The extensions and the types they name.
    extensions, types = [], set()
    for line in text.splitlines():
        parts = xpreg.fields(line)
        if len(parts) >= 5 and parts[0].upper() == 'HKCR' and re.fullmatch(r'\.[^\\]+', parts[1]) and not parts[2]:
            extensions.append(parts[1])
            types.add(parts[4].lower())
    excluded, lines = [], []
    for line in text.splitlines():
        parts = xpreg.fields(line)
        if len(parts) >= 5 and parts[0].upper() == 'HKCR' and parts[1].split('\\')[0].lower() in types:
            key, value = parts[1], parts[4]
            if value.startswith('@') and not xp_file(value.rsplit(',', 1)[0]):
                continue  # a name or verb from one of Wine's files: the plain one stays
            if re.search(r'\\shell\\[^\\]+\\command$', key, re.IGNORECASE) and not parts[2] and \
                    not value.lstrip('"').startswith('%1') and not program(value):
                excluded.append(key.rsplit('\\', 1)[0])
            elif key.lower().endswith('\\defaulticon') and not parts[2] and \
                    not value.startswith('%1') and not xp_file(value.rsplit(',', 1)[0]):
                excluded.append(key)
        lines.append(line)
    text = '\n'.join(lines)
    prefixes = tuple(extensions) + tuple(t for t in types if t)
    texts = [xpreg.hive_entries(text, 'HKCR', prefixes, exclude=tuple(excluded))]
    if (source / 'zipfldr.dll').is_file():
        zip_reginst = xpreg.reginst(source / 'zipfldr.dll')
        texts.append(xpreg.hive_entries(zip_reginst, 'HKCR', ('',), extra={
            '_SYS_MOD_PATH': r'C:\windows\system32\zipfldr.dll', '25': r'C:\windows'},
            install=('RegAlways', 'RegTakeDefault')))
        texts.append(xpreg.hive_entries(zip_reginst, 'HKLM', ('',), extra={
            '_SYS_MOD_PATH': r'C:\windows\system32\zipfldr.dll'}, install=('RegAlways', 'RegTakeDefault')))
    r = xp_explorer().Reg()
    for package, name in (('Msi.Package', 'Windows Installer Package'), ('Msi.Patch', 'Windows Installer Patch')):
        r.add('HKCR\\' + package, None, name)
        r.add('HKCR\\' + package + '\\DefaultIcon', None, r'C:\xp\res\msiexec.exe,0')
    texts.append(r.text())
    return texts


def find_nocase(root, relative):
    """RELATIVE under ROOT in any case (Windows names on Linux), or None."""
    path = root
    for part in Path(relative).parts:
        if not path.is_dir():
            return None
        path = next((p for p in path.iterdir() if p.name.lower() == part.lower()), None)
        if path is None:
            return None
    return path if path.is_file() else None


def themes(assets, prefix, wine):
    """Display Properties' Windows XP and Windows Classic themes."""
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all', WINEDLLOVERRIDES='themeui=n')
    env.pop('WINEARCH', None)
    # The Windows XP theme (luna.theme) names %WinDir%\web\wallpaper\Bliss.bmp,
    # XP's only Bliss there; a Bliss.jpg beside it (stage.py before) would be
    # listed twice in Display Properties.
    jpeg, bitmap = assets / 'web/wallpaper/bliss.jpg', prefix / 'drive_c/windows/Web/Wallpaper/Bliss.bmp'
    converter = BRIDGE / 'wallpaper-convert.exe'
    if jpeg.is_file() and converter.is_file() and not bitmap.exists():
        bitmap.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([wine, str(converter), 'Z:' + str(jpeg).replace('/', '\\'),
                        'Z:' + str(bitmap).replace('/', '\\')], env=env, check=False)
    if bitmap.is_file():
        bitmap.with_suffix('.jpg').unlink(missing_ok=True)
    # themeui's own registration: Theme.Manager, the visual style folders,
    # .theme and .msstyles files.
    subprocess.run([wine, r'C:\windows\syswow64\regsvr32.exe', '/s', r'C:\windows\system32\themeui.dll'],
                   env=env, check=False)
    # The Windows Classic style's color schemes and the themes' visual style
    # settings, which XP's themeui writes at first logon and Wine's does not:
    # made from the user's HIVEDEF.INF and themeui.dll (prefix/xp_schemes.py).
    hivedef, themeui = assets / 'inf/hivedef.inf', assets / 'cpl/themeui.dll'
    if hivedef.is_file() and themeui.is_file():
        language = json.loads((prefix / 'xp-shell.json').read_text()).get('language', 'ja')
        regedit(wine, prefix, xp_schemes.registry(xp_schemes.xpreg.read_inf(hivedef), themeui, language))


def security_center(assets, prefix, wine, xpx):
    """XP's Security Center: its service (wscsvc.dll) in Wine's 32-bit
    svchost, which starts the notification area's alerts (wscntfy.exe) in the
    session, and the Control Panel item (wscui.cpl) that they open. The
    service loads our HNetCfg.dll and netshell.dll from beside it (C:\\xp\\wsc):
    the Windows Firewall, which it checks through the SharedAccess service,
    is the Linux system's (xp-explorer/helpers/wscfirewall.c)."""
    source = assets / 'wsc'
    if not (source / 'wscsvc.dll').is_file():
        return
    system = prefix / 'drive_c/windows/syswow64'
    folder = prefix / 'drive_c/xp/wsc'
    folder.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source / 'wscsvc.dll', folder / 'wscsvc.dll')
    for name in ('HNetCfg.dll', 'netshell.dll'):
        shutil.copyfile(BRIDGE / 'wsc' / name, folder / name)
    for name in ('wscntfy.exe', 'wscui.cpl'):
        shutil.copyfile(source / name, system / name)
    # Their icons and texts.
    if (assets / 'tray/xpsp2res.dll').is_file():
        shutil.copyfile(assets / 'tray/xpsp2res.dll', system / 'xpsp2res.dll')
    r = xpx.Reg()
    services = r'HKLM\System\CurrentControlSet\Services' + '\\'
    svchost = r'C:\windows\syswow64\svchost.exe -k wscsvc'
    for name, display, dll in (('SharedAccess', 'Windows Firewall/Internet Connection Sharing (ICS)', 'HNetCfg.dll'),
                               ('wscsvc', 'Security Center', 'wscsvc.dll')):
        r.add(services + name, 'Type', '32', 'REG_DWORD')
        r.add(services + name, 'Start', '2', 'REG_DWORD')
        r.add(services + name, 'ErrorControl', '1', 'REG_DWORD')
        r.add(services + name, 'ObjectName', 'LocalSystem')
        r.add(services + name, 'DisplayName', display)
        r.add(services + name, 'ImagePath', svchost, 'REG_EXPAND_SZ')
        r.add(services + name + r'\Parameters', 'ServiceDll', 'C:\\xp\\wsc\\' + dll, 'REG_EXPAND_SZ')
    # As on XP, after the RPC service; Terminal Services tells it the user's
    # session, which Wine starts only on demand.
    r.add(services + 'wscsvc', 'DependOnService', ['RpcSs', 'SharedAccess', 'TermService'], 'REG_MULTI_SZ')
    r.add(services + 'TermService', 'Start', '2', 'REG_DWORD')
    r.add(r'HKLM\Software\Microsoft\Windows NT\CurrentVersion\Svchost', 'wscsvc',
          ['SharedAccess', 'wscsvc'], 'REG_MULTI_SZ')
    # As XP's netsvcs group, which runs them there: its services' threads use
    # COM (the Security Center asks Automatic Updates' settings through it).
    r.add(r'HKLM\Software\Microsoft\Windows NT\CurrentVersion\Svchost\wscsvc', 'CoInitializeSecurityParam', '1',
          'REG_DWORD')
    # XP's Automatic Updates service starts with the system.
    r.add(services + 'wuauserv', 'Start', '2', 'REG_DWORD')
    for dll in ('hnetcfg', 'netshell'):
        r.add(r'HKCU\Software\Wine\AppDefaults\svchost.exe\DllOverrides', dll, 'native')
    regedit(wine, prefix, r.text())
    # Wine's 32-bit svchost reads its groups from the 64-bit view too.
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all')
    env.pop('WINEARCH', None)
    subprocess.run([wine, 'reg', 'add', r'HKLM\Software\Microsoft\Windows NT\CurrentVersion\Svchost', '/v', 'wscsvc',
                    '/t', 'REG_MULTI_SZ', '/d', 'SharedAccess\\0wscsvc', '/f', '/reg:64'], env=env, check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run([wine, 'reg', 'add', r'HKLM\Software\Microsoft\Windows NT\CurrentVersion\Svchost\wscsvc', '/v',
                    'CoInitializeSecurityParam', '/t', 'REG_DWORD', '/d', '1', '/f', '/reg:64'], env=env, check=True,
                   stdout=subprocess.DEVNULL)
    # Automatic Updates set to the recommended setting (Automatic), which XP's
    # setup offers, unless chosen otherwise; the Windows Update Agent reads it
    # from the 64-bit view.
    auto_update = r'HKLM\Software\Microsoft\Windows\CurrentVersion\WindowsUpdate\Auto Update'
    if subprocess.run([wine, 'reg', 'query', auto_update, '/v', 'AUOptions', '/reg:64'], env=env,
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
        subprocess.run([wine, 'reg', 'add', auto_update, '/v', 'AUOptions', '/t', 'REG_DWORD', '/d', '4', '/f',
                        '/reg:64'], env=env, check=False, stdout=subprocess.DEVNULL)


# XP's Help and Support Center (assets/helpctr) in PCHealth\HelpCtr\Binaries,
# as PCHEALTH.INF installs it, and the programs that run it there.
HELPCTR = r'C:\windows\PCHealth\HelpCtr'
HELPCTR_PROGRAMS = ('helpctr.exe', 'helphost.exe', 'helpsvc.exe')
# The WebBrowser control (and its version 1), which Wine registers to its
# ieframe.dll: XP's Help and Support Center hosts it, and its pages find the
# Help Center through the control's site (IServiceProvider), which only XP's
# shdocvw passes on. Programs without XP's shdocvw load Wine's, which hands
# the classes to ieframe.
WEB_BROWSER_CLASSES = ('{8856F961-340A-11D0-A96B-00C04FD705A2}', '{EAB22AC3-30C1-11CF-A7EB-0000C05BAE0B}')


def help_center(assets, prefix, wine, xpx):
    """XP's Help and Support Center: its files, its service (pchsvc.dll in
    Wine's 32-bit svchost, which starts HelpSvc.exe), and its help topics,
    which HelpSvc.exe /install2 installs from pchdt_w3.cab as XP's setup
    does (syssetup.inf). The topics' database (ESE) comes with indexes on
    XP's sort keys, which Wine's LCMapString does not give: esentutl /d
    builds it again, and the topics under each heading are found."""
    source = assets / 'helpctr'
    if not (source / 'helpctr.exe').is_file():
        return
    windows = prefix / 'drive_c/windows'
    binaries = windows / 'PCHealth/HelpCtr/Binaries'
    binaries.mkdir(parents=True, exist_ok=True)
    for path in source.iterdir():
        shutil.copyfile(path, windows / 'syswow64' / path.name if path.name == 'rdshost.exe' else binaries / path.name)
    # XP's common controls 6 beside it, as for rundll32 (Wine patch 0149):
    # the toolbar's images.
    controls = prefix / 'drive_c/xp/comctl32.dll'
    if controls.is_file() and (prefix / '.patched-wine').is_file():
        shutil.copyfile(controls, binaries / 'comctl32.dll')
    # The help files of its topics, in %windir%\Help (the prefix's "help").
    help_folder = next((p for p in windows.iterdir() if p.name.lower() == 'help'), windows / 'Help')
    help_folder.mkdir(exist_ok=True)
    for path in sorted((assets / 'help').glob('*.chm')):
        shutil.copyfile(path, help_folder / path.name)
    r = xpx.Reg()
    # IE6's engine and XP's shell in its programs; the service's database
    # engine is XP's (Wine's esent is a stub).
    for program in HELPCTR_PROGRAMS + ('esentutl.exe',):
        dlls = ('esent',) if program == 'esentutl.exe' else xpx.NATIVE + xpx.IE_NATIVE + xpx.SEARCH_NATIVE + ('esent',)
        for dll in dlls:
            r.add(rf'HKCU\Software\Wine\AppDefaults\{program}\DllOverrides', dll, 'native,builtin')
    # HIVESYS.INF's Terminal Server key: without it the service's host
    # (pchsvc.dll) stops as soon as it starts.
    r.add(r'HKLM\System\CurrentControlSet\Control\Terminal Server', 'fAllowToGetHelp', '1', 'REG_DWORD')
    r.add(r'HKLM\Software\Microsoft\Windows\CurrentVersion\App Paths\HELPCTR.EXE', None,
          HELPCTR + r'\Binaries\HelpCtr.exe')
    for clsid in WEB_BROWSER_CLASSES:
        r.add('HKCR\\CLSID\\' + clsid + '\\InProcServer32', None, r'%SystemRoot%\system32\shdocvw.dll',
              'REG_EXPAND_SZ')
        r.add('HKCR\\CLSID\\' + clsid + '\\InProcServer32', 'ThreadingModel', 'Apartment')
    # Its service in a group of its own, as XP's netsvcs runs it: with COM
    # (Wine patch 0156).
    r.add(r'HKLM\Software\Microsoft\Windows NT\CurrentVersion\Svchost', 'helpsvc', ['helpsvc'], 'REG_MULTI_SZ')
    r.add(r'HKLM\Software\Microsoft\Windows NT\CurrentVersion\Svchost\helpsvc', 'CoInitializeSecurityParam', '1',
          'REG_DWORD')
    regedit(wine, prefix, r.text())
    language = json.loads((prefix / 'xp-shell.json').read_text()).get('language', 'en')
    locale = 'ja_JP.UTF-8' if language == 'ja' else 'en_US.UTF-8'
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all', LANG=locale, LC_ALL=locale)
    env.pop('WINEARCH', None)

    def run(*args, cwd=None):
        subprocess.run([wine, *args], env=env, cwd=cwd, check=False, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)

    # Wine's 32-bit svchost reads its groups from the 64-bit view too.
    run('reg', 'add', r'HKLM\Software\Microsoft\Windows NT\CurrentVersion\Svchost', '/v', 'helpsvc', '/t',
        'REG_MULTI_SZ', '/d', 'helpsvc', '/f', '/reg:64')
    run('reg', 'add', r'HKLM\Software\Microsoft\Windows NT\CurrentVersion\Svchost\helpsvc', '/v',
        'CoInitializeSecurityParam', '/t', 'REG_DWORD', '/d', '1', '/f', '/reg:64')
    # The type library that the service's interfaces import (Remote Desktop's).
    run(r'C:\windows\syswow64\rdshost.exe', '/regserver')
    # syssetup.inf's registrations; the service runs in Wine's 32-bit svchost.
    run(HELPCTR + r'\Binaries\HelpSvc.exe', '/svchost', 'helpsvc', '/regserver')
    run('reg', 'add', r'HKLM\System\CurrentControlSet\Services\helpsvc', '/v', 'ImagePath', '/t', 'REG_EXPAND_SZ',
        '/d', r'C:\windows\syswow64\svchost.exe -k helpsvc', '/f')
    run(HELPCTR + r'\Binaries\HelpHost.exe', '/regserver')
    run(HELPCTR + r'\Binaries\HelpCtr.exe', '/regserver')
    run(r'C:\windows\syswow64\regsvr32.exe', '/s', HELPCTR + r'\Binaries\brpinfo.dll')
    # The help topics, once, in the system's language (the package's). The
    # service keeps a copy of its installed packages in the registry, from
    # which it would only restore the list of a database that is missing.
    database = windows / 'PCHealth/HelpCtr/Database'
    if not (database / 'HCdata.edb').is_file():
        run('reg', 'delete', r'HKLM\Software\Microsoft\PCHealth\HelpSvc\Backup', '/f', '/reg:32')
        run(HELPCTR + r'\Binaries\HelpSvc.exe', '/install2')
        stop(wine, prefix)
        if (database / 'HCdata.edb').is_file():
            run(HELPCTR + r'\Binaries\esentutl.exe', '/d', 'HCdata.edb', '/o', cwd=database)
    stop(wine, prefix)


def user_accounts(prefix, wine):
    """XP's User Accounts, where the account picture of the Start menu and
    the Welcome screen is chosen: shgina.dll's user objects and nusrmgr.cpl's
    own (its toolbar) register themselves and their type libraries, as XP's
    setup does. stage() has put them in syswow64."""
    system = prefix / 'drive_c/windows/syswow64'
    if not (system / 'nusrmgr.cpl').is_file():
        return
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all')
    env.pop('WINEARCH', None)
    for name in ('shgina.dll', 'nusrmgr.cpl'):
        subprocess.run([wine, r'C:\windows\syswow64\regsvr32.exe', '/s', 'C:\\windows\\system32\\' + name],
                       env=env, check=False)


def network_connections(prefix, wine):
    """XP's Network Connections folder (netshell.dll) registers itself in My
    Computer's Control Panel, with the proxies of its connection objects,
    which xpnetcon.exe serves. stage() has put it in syswow64."""
    if not (prefix / 'drive_c/windows/syswow64/netshell.dll').is_file():
        return
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all')
    env.pop('WINEARCH', None)
    subprocess.run([wine, r'C:\windows\syswow64\regsvr32.exe', '/s', r'C:\windows\system32\netshell.dll'],
                   env=env, check=False)


# XP's default Start menu programs, where its setup puts them, and their
# shortcuts as named by syssetup.inf and msnmsn.inf, available in All Programs
# and included in XP's recent-program list when launched.
START_MENU = r'C:\ProgramData\Microsoft\Windows\Start Menu\Programs'
XP_PROGRAMS = (
    ('msnsusii.exe', r'C:\Program Files\MSN\MSNCoreFiles\Install', START_MENU + r'\MSN.lnk', 'MSN'),
    ('tourstart.exe', r'C:\windows\system32', START_MENU + r'\Accessories\Tour Windows XP.lnk',
     'Learn about the exciting new features in Windows XP.'),
    ('migwiz.exe', r'C:\windows\system32\usmt', START_MENU + r'\Accessories\System Tools\Files and Settings Transfer Wizard.lnk',
     'Transfers files and settings from one computer to another.'),
)


def start_menu_programs(assets, prefix, wine, xpx):
    """XP's default Start menu: its programs' shortcuts, and Set Program
    Access and Defaults in the right column (shell.inf's registration)."""
    source = assets / 'programs'
    if not (source / 'tourstart.exe').is_file():
        return
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all')
    env.pop('WINEARCH', None)
    for name, folder, link, description in XP_PROGRAMS:
        target = prefix / 'drive_c' / folder[3:].replace('\\', '/').replace('system32', 'syswow64')
        target.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / name, target / name)
        xpx.save_shortcut(wine, prefix, env, link, folder + '\\' + name, directory=folder,
                          icon=folder + '\\' + name, description=description)
    shutil.copyfile(source / 'moricons.dll', prefix / 'drive_c/windows/syswow64/moricons.dll')
    # The tours that tourstart.exe offers, where XP's setup puts them: the
    # animated one (a Flash projector) and the pages of the non-animated one.
    tours = prefix / 'drive_c/windows/help/Tours'
    for kind in ('mmTour', 'htmlTour'):
        if (assets / 'tour' / kind).is_dir():
            shutil.copytree(assets / 'tour' / kind, tours / kind, dirs_exist_ok=True)
    r = xpx.Reg()
    # MSN's installer (msninst.exe, which msnsusii.exe installs and starts)
    # draws its wizard with IE6's engine, as Internet Explorer itself runs
    # it: with Wine's shlwapi, IE6's wininet called its XP-only ordinal 563
    # and crashed. Wine's comctl32 stays (XP's is not in its winsxs).
    for dll in xpx.NATIVE + xpx.IE_NATIVE + xpx.SEARCH_NATIVE:
        if dll != 'comctl32':
            r.add(r'HKCU\Software\Wine\AppDefaults\msninst.exe\DllOverrides', dll, 'native')
    # As on XP, Internet Explorer opens web pages saved on the computer (the
    # non-animated tour's scripts are IE's); Wine sends them to winebrowser.
    # Without IE6 it is our stand-in, which opens the Linux browser.
    r.add(r'HKCR\htmlfile\shell\open\command', None, r'"%ProgramFiles%\Internet Explorer\iexplore.exe" "%1"',
          'REG_EXPAND_SZ')
    clsid = r'HKCR\CLSID\{2559a1f7-21d7-11d4-bdaf-00c04f60b9f0}'
    r.add(clsid, None, 'Set Program Access and Defaults')
    r.add(clsid, 'InfoTip', 'Specify which programs are used by default for activities such as Web browsing, '
          'sending e-mail and playing music.')
    r.add(clsid + r'\DefaultIcon', None, 'moricons.dll,-114', 'REG_EXPAND_SZ')
    r.add(clsid + r'\InProcServer32', None, r'%SystemRoot%\system32\shdocvw.dll', 'REG_EXPAND_SZ')
    r.add(clsid + r'\InProcServer32', 'ThreadingModel', 'Apartment')
    r.add(clsid + r'\Instance', 'CLSID', '{3f454f0e-42ae-4d7c-8ea3-328250d6e272}')
    bag = clsid + r'\Instance\InitPropertyBag'
    for name, value in (('CLSID', '{13709620-C279-11CE-A49E-444553540000}'), ('command', 'Set Program Access and Defaults'),
                        ('method', 'ShellExecute'), ('Param1', 'appwiz.cpl,,3'), ('Param2', 'control.exe')):
        r.add(bag, name, value)
    r.add(clsid + r'\ShellFolder', 'Attributes', '0', 'REG_DWORD')
    # Its own context menu (shdocvw's automation menu above) gives the item
    # its default command; without it the Start menu's click offered to
    # create a shortcut instead ("Windows cannot create a shortcut here").
    r.add(clsid + r'\shellex\ContextMenuHandlers\{2559a1f7-21d7-11d4-bdaf-00c04f60b9f0}', None, '')
    r.add(clsid + r'\shellex\MayChangeDefaultMenu', None, '')
    # The SP1 texts that shell.inf names, with XP's Add or Remove Programs.
    if (assets / 'appwiz/xpsp1res.dll').is_file():
        r.add(clsid, 'LocalizedString', '@xpsp1res.dll,-10077')
        r.add(clsid, 'InfoTip', '@xpsp1res.dll,-10078')
    r.add(r'HKLM\Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved',
          '{2559a1f7-21d7-11d4-bdaf-00c04f60b9f0}', 'Set Program Access and Defaults')
    r.add(r'HKLM\Software\Microsoft\Windows\CurrentVersion', 'SM_ConfigureProgramsName', 'Set Program Access and Defaults')
    regedit(wine, prefix, r.text())


# XP's programs in system32 that replace Wine's own (or that Wine lacks), and
# the DLLs they need; the files of the others' folders in Program Files.
PROGRAM_FOLDERS = ('Windows NT', 'Messenger')
# Shortcuts of XP's INFs left out, as what they start needs XP's services,
# drivers or components: Backup, activation, the network wizards, Narrator
# (XP's speech engine and voices), Utility Manager (its service) and Scheduled
# Tasks (its folder, D6277990: XP's Task Scheduler).
LEFT_OUT = ('ntbackup.exe', 'msoobe.exe', 'netsetup.cpl', 'hnetwiz.dll', 'narrator.exe', 'utilman.exe',
            '{d6277990-4c6a-11cf-8d87-00aa0060f5bf}')
EXPLORER = r'C:\xp\xp-explorer.exe'
XP_SHELL_PROGRAMS = ('taskmgr.exe', 'osk.exe', 'magnify.exe', 'accwiz.exe')


def xp_programs(assets, prefix, wine, xpx):
    """XP's own programs and XP's All Programs: the Accessories, the games, the
    Accessibility tools, Task Manager, Registry Editor and Windows Messenger in
    place of Wine's programs (DllOverrides), installed where XP's setup puts
    them, and the shortcuts that XP's INFs make (prefix/startmenu.py), in the
    media's language. Windows Messenger has MSMSGS.INF's registry entries, as
    its optional component installs it."""
    import startmenu
    import xpreg
    source = assets / 'apps'
    if not (source / 'system32/notepad.exe').is_file():
        return
    drive = prefix / 'drive_c'
    system = drive / 'windows/syswow64'
    r = xpx.Reg()
    for path in sorted((source / 'system32').iterdir()):
        shutil.copyfile(path, system / path.name)
        r.add(r'HKCU\Software\Wine\DllOverrides', path.name.removesuffix('.dll'), 'native,builtin')
    for folder in PROGRAM_FOLDERS:
        for path in sorted((source / 'programs' / folder).rglob('*')):
            if path.is_file():
                target = drive / 'Program Files' / path.relative_to(source / 'programs')
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, target)
                if path.suffix.lower() == '.exe':
                    r.add(r'HKCU\Software\Wine\DllOverrides', path.name, 'native,builtin')
    # Windows Messenger: its optional component's install section and the
    # per-user one that Active Setup runs at the first logon.
    inf = source / 'inf/msmsgs.inf'
    shutil.copyfile(inf, drive / 'windows/inf/msmsgs.inf')
    dirids = {'10': r'C:\windows', '11': r'C:\windows\system32', '17': r'C:\windows\inf',
              '16422': r'C:\Program Files'}
    text = xpreg.read_inf(inf)
    for root in ('HKLM', 'HKCR'):
        regedit(wine, prefix, xpreg.hive_entries(text, root, ('',), extra=dirids, install=('msmsgs', 'ShowIcons')))
    regedit(wine, prefix, xpreg.hive_entries(text, 'HKCU', ('',), extra=dirids, install=('BLC.Install.PerUser',)))
    # XP's shell32 where they call it by ordinal (Task Manager's Run dialog and
    # restrictions) or use its classes (the Accessibility tools' "Link Window").
    for program in XP_SHELL_PROGRAMS:
        for dll in xpx.NATIVE:
            r.add(rf'HKCU\Software\Wine\AppDefaults\{program}\DllOverrides', dll, 'native,builtin')
    regedit(wine, prefix, r.text())

    def exists(windows):
        """Whether a Windows path of the 32-bit XP shell names a file of the prefix."""
        if not windows[1:3] == ':\\':
            return True
        relative = windows[3:].replace('\\', '/')
        relative = relative.replace('windows/system32/', 'windows/syswow64/', 1) \
            if relative.lower().startswith('windows/system32/') else relative
        return (drive / relative).exists() or (drive / relative.lower()).exists()

    programs = r'C:\ProgramData\Microsoft\Windows\Start Menu\Programs'
    lines, names = [], {}
    for item in startmenu.items(source / 'inf'):
        target, arguments = item['target'], item['arguments']
        named = (target + ' ' + arguments).lower()
        if any(name in named for name in LEFT_OUT):
            continue
        folder = programs.rsplit('\\', 1)[0] if item['folder'] is None else \
            programs + ('\\' + item['folder'] if item['folder'] else '')
        if target.lower().startswith('hcp://'):
            # A Help and Support Center page (the Program Compatibility
            # Wizard), opened as XP's own Help menu opens them.
            (drive / folder[3:].replace('\\', '/') / (item['name'] + '.url')).unlink(missing_ok=True)
            target, arguments = HELPCTR + r'\Binaries\HelpCtr.exe', f'-url {target}'
        if '://' in target:
            # An Internet shortcut, as XP's setup makes for a URL.
            path = drive / folder[3:].replace('\\', '/') / (item['name'] + '.url')
            path.parent.mkdir(parents=True, exist_ok=True)
            icon, _, index = item.get('icon', '').partition(',')
            body = f'[InternetShortcut]\r\nURL={target}\r\n'
            if icon and exists(icon):
                body += f'IconFile={icon}\r\nIconIndex={index or 0}\r\n'
            path.write_text(body, encoding='utf-8')
            if item['display']:
                names.setdefault(path.parent, {})[path.name] = item['display']
            continue
        if not exists(target) or (target.lower().endswith('rundll32.exe') and
                                  not exists('C:\\windows\\system32\\' + arguments.split(',')[0])):
            continue
        if item['display']:
            names.setdefault(drive / folder[3:].replace('\\', '/'), {})[item['name'] + '.lnk'] = item['display']
        # The shortcuts that XP Explorer makes itself stay (Network Connections).
        if exists(f'{folder}\\{item["name"]}.lnk'):
            continue
        icon = item.get('icon', '')
        # XP's explorer.exe is C:\xp\xp-explorer.exe here; Wine's keeps its name.
        target, icon = (EXPLORER if value.lower() == r'c:\windows\explorer.exe' else value for value in (target, icon))
        icon = icon.replace(r'C:\windows\explorer.exe', EXPLORER)
        if icon and not exists(icon.rsplit(',', 1)[0]):
            icon = ''
        lines.append('\t'.join((f'{folder}\\{item["name"]}.lnk', target, arguments, item['directory'], icon,
                                 item['infotip'])))
    # Windows Media Player at the top of All Programs too, as unregmp2.exe
    # /ShowWMP puts it.
    media = drive / 'ProgramData/Microsoft/Windows/Start Menu/Programs/Accessories/Entertainment/Windows Media Player.lnk'
    if media.is_file():
        shutil.copyfile(media, media.parents[2] / media.name)
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all',
               WINEDLLOVERRIDES='winemenubuilder.exe=d')
    env.pop('WINEARCH', None)
    result = subprocess.run([wine, str(BRIDGE / 'linux-links.exe')], env=env, input='\n'.join(lines) + '\n',
                            capture_output=True, text=True, encoding='utf-8', timeout=600)
    for line in result.stdout.splitlines():
        if line.startswith('fail'):
            print('xp-programs:', line, file=sys.stderr)
    for folder, entries in names.items():
        localized_names(xpx, folder, entries)
    # XP sorts All Programs (folders first, then by name) until the user moves
    # an item; the order an earlier menu kept would leave the new items last.
    regedit(wine, prefix, 'Windows Registry Editor Version 5.00\r\n\r\n'
            '[-HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\MenuOrder\\'
            'Start Menu2\\Programs]\r\n')


def localized_names(xpx, folder, entries):
    """XP shows a shortcut under the name of its folder's desktop.ini
    [LocalizedFileNames] (the INFs' DisplayResource: "FreeCell" for
    Freecell.lnk), when the folder is a system one (Wine keeps a folder's
    system attribute, not its read-only one)."""
    path = folder / 'desktop.ini'
    try:
        raw = path.read_bytes()
        text = raw.decode('utf-16') if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else raw.decode('utf-8')
    except (OSError, UnicodeDecodeError):
        text = ''
    lines, section, done = [], None, set()
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith('['):
            if section == '[localizedfilenames]':
                lines += [f'{name}={value}' for name, value in entries.items() if name not in done]
                done.update(entries)
            section = stripped.lower()
        elif section == '[localizedfilenames]' and '=' in stripped:
            name = stripped.split('=', 1)[0].strip()
            if name in entries:
                line = f'{name}={entries[name]}'
                done.add(name)
        lines.append(line)
    if section == '[localizedfilenames]':
        lines += [f'{name}={value}' for name, value in entries.items() if name not in done]
        done.update(entries)
    if set(entries) - done:
        lines += ['[LocalizedFileNames]'] + [f'{name}={value}' for name, value in entries.items() if name not in done]
    path.write_bytes(b'\xff\xfe' + ('\r\n'.join(lines) + '\r\n').encode('utf-16-le'))
    xpx.set_dos_attributes(path, 0x6)
    xpx.set_dos_attributes(folder, 0x4)


def xp_user_defaults(prefix, wine):
    """XP's defaults that earlier versions set otherwise: the access keys'
    underlines hidden until Alt is pressed (UserPreferencesMask's
    KeyboardCues bit, 0x20 of its first byte), and the links' colour
    (HotTrackingColor) of the blue and olive styles, which name none: XP keeps
    HIVEDEF.INF's 0 0 128 (Wine patch 0160 when a style is applied); and the
    tooltips' colours, which Luna names in none of its styles: XP's uxtheme
    sets them to its defaults, black on 255 255 225 (Wine patch 0181), where
    the scheme used before stayed (Eggplant's purple text on white)."""
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all')
    env.pop('WINEARCH', None)

    def query(key, name):
        out = subprocess.run([wine, 'reg', 'query', key, '/v', name], env=env, capture_output=True,
                             text=True).stdout
        line = next((l for l in out.splitlines() if l.strip().startswith(name)), '')
        return line.split(None, 2)[2].strip() if len(line.split(None, 2)) > 2 else ''

    def add(key, name, kind, value):
        subprocess.run([wine, 'reg', 'add', key, '/v', name, '/t', kind, '/d', value, '/f'], env=env,
                       stdout=subprocess.DEVNULL, check=False)

    desktop = r'HKCU\Control Panel\Desktop'
    mask = query(desktop, 'UserPreferencesMask')
    if len(mask) >= 2 and int(mask[:2], 16) & 0x20:
        add(desktop, 'UserPreferencesMask', 'REG_BINARY', f'{int(mask[:2], 16) & ~0x20:02x}' + mask[2:])
    themes = r'HKCU\Software\Microsoft\Windows\CurrentVersion\ThemeManager'
    if query(themes, 'ColorName').lower() != 'metallic':
        add(r'HKCU\Control Panel\Colors', 'HotTrackingColor', 'REG_SZ', '0 0 128')
    if query(themes, 'ThemeActive') == '1':
        add(r'HKCU\Control Panel\Colors', 'InfoText', 'REG_SZ', '0 0 0')
        add(r'HKCU\Control Panel\Colors', 'InfoWindow', 'REG_SZ', '255 255 225')


# The categories of XP's Control Panel (its category view's "or pick a
# Control Panel icon"): 1 Appearance and Themes, 2 Printers and Other
# Hardware, 3 Network and Internet Connections, 4 Sounds, Speech and Audio
# Devices, 5 Performance and Maintenance, 6 Date, Time, Language and Regional
# Options, 7 Accessibility Options, 8 Add or Remove Programs, 9 User Accounts,
# 10 Security Center. XP's shell32 knows its own applets' only under
# %SystemRoot%\system32, which a 32-bit process here sees as syswow64.
CPL_CATEGORIES = {'access.cpl': 7, 'appwiz.cpl': 8, 'desk.cpl': 1, 'hdwwiz.cpl': 2, 'inetcpl.cpl': '3,10',
                  'intl.cpl': 6, 'joy.cpl': 2, 'main.cpl': 2, 'mmsys.cpl': 4, 'ncpa.cpl': 3,
                  'nusrmgr.cpl': 9, 'powercfg.cpl': 5, 'sapi.cpl': 4, 'sysdm.cpl': 5, 'telephon.cpl': 2,
                  'timedate.cpl': 6, 'wuaucpl.cpl': 10, 'wscui.cpl': -1}
# Folder Options and Taskbar and Start Menu, which XP's shell32 registers in
# the Control Panel (its REGINST).
CONTROL_PANEL_CLASSES = ('{6DFD7C5C-2451-11d3-A299-00C04F8EF6AF}', '{0DF44EAA-FF21-4412-828E-260A8728E7F1}')


def control_panel(assets, prefix, wine, xpx):
    """XP's Control Panel categories and its Folder Options and Taskbar and
    Start Menu items."""
    import xpreg
    category = (r'HKLM\Software\Microsoft\Windows\CurrentVersion\Control Panel\Extended Properties'
                r'\{305CA226-D286-468e-B848-2B2E8E697B74} 2')
    r = xpx.Reg()
    # A list of categories is a string, as HIVESFT.INF has Windows Firewall's ("3,10").
    for name, number in CPL_CATEGORIES.items():
        for folder in ('system32', 'syswow64'):
            if isinstance(number, str):
                r.add(category, rf'C:\windows\{folder}\{name}', number)
            else:
                r.add(category, rf'C:\windows\{folder}\{name}', str(number & 0xffffffff), 'REG_DWORD')
    # XP's shell32 keeps the items' names in a cache, which it does not read
    # again for items that changed (Wine's "Internet Settings" stayed).
    texts_cache = 'Windows Registry Editor Version 5.00\r\n\r\n' \
        '[HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Controls Folder]\r\n' \
        '"Presentation Cache"=-\r\n'
    texts = [r.text(), texts_cache]
    text = xpreg.reginst(assets / 'system32/shell32.dll')
    module = {'_SYS_MOD_PATH': r'%SystemRoot%\system32\shell32.dll', '_MOD_PATH': r'%SystemRoot%\system32\shell32.dll'}
    namespace = r'Software\Microsoft\Windows\CurrentVersion\Explorer\ControlPanel\NameSpace'
    texts.append(xpreg.hive_entries(text, 'HKCR', tuple('CLSID\\' + c for c in CONTROL_PANEL_CLASSES), extra=module))
    texts.append(xpreg.hive_entries(text, 'HKLM', tuple(namespace + '\\' + c for c in CONTROL_PANEL_CLASSES),
                                    extra=module))
    for t in texts:
        regedit(wine, prefix, t)


def stage(prefix, xpx=None):
    """XP's files back in syswow64 after Wine updates the prefix: XP
    Explorer's, the Regional and Language Options over Wine's intl.cpl,
    User Accounts with XP's mshta.exe over Wine's stub, and the Network
    Connections folder."""
    (xpx or xp_explorer()).stage(prefix)
    # The helpers as bridge-wm/build.sh last built them.
    for helper in HELPERS:
        source, target = BRIDGE / helper, prefix / 'drive_c/xp/bin' / helper
        if source.is_file() and target.parent.is_dir() and \
                (not target.is_file() or target.read_bytes() != source.read_bytes()):
            temporary = target.with_name(f'.{helper}.new')
            shutil.copyfile(source, temporary)
            temporary.replace(target)
    system = prefix / 'drive_c/windows/syswow64'
    netcon = (prefix / 'drive_c/xp/native/wzcdlg.dll').is_file()
    sources = [prefix / 'drive_c/xp/native' / name for name in INTL + ACCOUNTS + APPWIZ + SECURITY + FILE_TYPES +
               (NETCON if netcon else ())]
    sources += [BRIDGE / 'netcon' / name for name in NETCON_BRIDGE] if netcon else []
    for source in sources:
        name, target = source.name, system / source.name
        if source.is_file() and (not target.is_file() or target.read_bytes() != source.read_bytes()):
            temporary = target.with_name(f'.{name}.xp')
            shutil.copyfile(source, temporary)
            temporary.replace(target)


def state(prefix):
    try:
        return (prefix / MARKER).read_text().strip()
    except OSError:
        return ''


def wanted(xpx, wine):
    # IE6 and Windows Media Player register again after each Wine build:
    # wineboot registers Wine's own mshtml and wmp over them.
    return f'{VERSION} {xpx.wine_build(wine)}'


def apply(prefix, wine):
    config = json.loads((prefix / 'xp-shell.json').read_text())
    assets = Path(config['assets'])
    missing = [h for h in HELPERS if not (BRIDGE / h).is_file()]
    if missing:
        raise SystemExit(f'Missing {", ".join(missing)}: run bridge-wm/build.sh first')
    xpx = xp_explorer()
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all', WINEDLLOVERRIDES='mscoree,mshtml=')
    env.pop('WINEARCH', None)
    print('* XP Explorer additions: files', flush=True)
    stop(wine, prefix)
    copy_files(assets, prefix)
    # XP's shell keeps the icons it extracted in IconCache.db; drop it so that
    # it extracts them again with this Wine (16x16 images: patch 0151).
    for cache in (prefix / 'drive_c/users').glob('*/AppData/Local/IconCache.db'):
        cache.unlink(missing_ok=True)
    print('* XP Explorer additions: registry', flush=True)
    regedit(wine, prefix, registry(xpx).text())
    regedit(wine, prefix, xpx.xp_registrations(assets))
    # Before IE6, Windows Media Player and XP's programs, which register
    # their own types over these, as on XP.
    for text in file_types(assets, prefix):
        regedit(wine, prefix, text)
    if (assets / 'intl/input.dll').is_file():
        for text in keyboard_layouts(assets):
            regedit(wine, prefix, text)
    # The Linux system's layout becomes XP's input language, once.
    subprocess.run([sys.executable, str(ROOT / 'session/keyboard.py'), 'seed', str(prefix), '--wine', wine],
                   check=False)
    # With the wineserver stopped (KnownDlls), XP's files go in place.
    stop(wine, prefix)
    stage(prefix, xpx)
    # SHGetSetSettings writes the task pane setting where XP's shell32 reads it.
    subprocess.run([wine, r'C:\xp\bin\startmenu.exe', 'webview'], env=env, check=False)
    print("* XP's sounds, Explorer settings, image previews and Network Connections", flush=True)
    xpx.xp_defaults(assets, prefix, wine, folders=False)
    stop(wine, prefix)
    print("* XP's Windows XP and Windows Classic themes", flush=True)
    themes(assets, prefix, wine)
    stop(wine, prefix)
    print('* Internet Explorer 6 and Windows Media Player 9', flush=True)
    xpx.install_ie(assets, prefix, wine)
    # Its wineboot registers XP's shell32 again, whose My Computer icon is
    # %SystemRoot%\Explorer.exe,0: Wine's explorer.exe, a blank page. Ours is
    # XP's explorer under its own name (prefix/registry.py).
    regedit(wine, prefix, 'Windows Registry Editor Version 5.00\r\n\r\n'
            '[HKEY_CLASSES_ROOT\\CLSID\\{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\DefaultIcon]\r\n'
            '@="C:\\\\xp\\\\xp-explorer.exe,0"\r\n\r\n'
            # The user's own, which XP's shell32 writes too and reads first.
            '[HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\CLSID\\'
            '{20D04FE0-3AEA-1069-A2D8-08002B30309D}\\DefaultIcon]\r\n'
            '@="C:\\\\xp\\\\xp-explorer.exe,0"\r\n')
    stop(wine, prefix)
    print("* XP's Security Center", flush=True)
    security_center(assets, prefix, wine, xpx)
    # IE6's wineboot may have put Wine's files back: XP's go in place again
    # (with the wineserver stopped) before they register.
    stop(wine, prefix)
    stage(prefix, xpx)
    print("* XP's User Accounts", flush=True)
    user_accounts(prefix, wine)
    print("* XP's Network Connections", flush=True)
    network_connections(prefix, wine)
    print("* XP's Start menu programs", flush=True)
    start_menu_programs(assets, prefix, wine, xpx)
    print("* XP's programs and All Programs", flush=True)
    xp_programs(assets, prefix, wine, xpx)
    xp_user_defaults(prefix, wine)
    print("* XP's Control Panel categories", flush=True)
    control_panel(assets, prefix, wine, xpx)
    stop(wine, prefix)
    print("* XP's Help and Support Center", flush=True)
    help_center(assets, prefix, wine, xpx)
    stage(prefix, xpx)
    (prefix / MARKER).write_text(wanted(xpx, wine) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('prefix', type=Path)
    parser.add_argument('--wine')
    parser.add_argument('--check', metavar='WINE')
    parser.add_argument('--stage', action='store_true')
    args = parser.parse_args()
    prefix = args.prefix.resolve(strict=True)
    if not (prefix / 'xp-shell.json').is_file():
        raise SystemExit('Not a dedicated XP prefix')
    if args.check:
        sys.exit(state(prefix) != wanted(xp_explorer(), args.check))
    if args.stage:
        if state(prefix):
            stage(prefix)
        raise SystemExit
    apply(prefix, args.wine or json.loads((prefix / 'xp-shell.json').read_text())['wine'])
