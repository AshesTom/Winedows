#!/usr/bin/env python3
"""Restore native shell DLLs atomically after Wine prefix updates."""
import filecmp
import argparse
import json
import os
import pwd
from pathlib import Path
import shutil
import subprocess
import tempfile
import registry

def shell_system(prefix):
    """The system directory of the 32-bit XP shell: syswow64 in a 64-bit prefix."""
    wow64 = prefix / 'drive_c/windows/syswow64'
    return wow64 if wow64.is_dir() else prefix / 'drive_c/windows/system32'

def controls_targets(prefix, wine):
    """Our themed controls module per architecture of the prefix."""
    windows = prefix / 'drive_c/windows'
    lib = wine.parent.parent / 'lib/wine'
    if (windows / 'syswow64').is_dir():
        return ((lib / 'x86_64-windows', windows / 'system32'), (lib / 'i386-windows', windows / 'syswow64'))
    return ((lib / 'i386-windows', windows / 'system32'),)

def language(prefix):
    """The language of the prefix's XP media; prefixes made before English XP was supported are Japanese."""
    try:
        return json.loads((prefix / 'xp-shell.json').read_text()).get('language', 'ja')
    except (OSError, ValueError):
        return 'ja'

def progress(percent, step):
    """How far the logon is, for the Welcome screen: session/supervisor.py
    gives a pipe for it and passes the line on to greeter/xp-splash.py."""
    try:
        os.write(int(os.environ['WIN4WINE_PROGRESS_FD']), f'progress {percent} {step}\n'.encode())
    except (KeyError, ValueError, OSError):
        pass

def prepare(prefix, wine):
    inf = wine.parent.parent / 'share/wine/wine.inf'
    timestamp = prefix / '.update-timestamp'
    current = timestamp.read_text().strip() if timestamp.is_file() else ''
    version = prefix / '.xp-registry-version'
    wine_current = inf.is_file() and current == str(int(inf.stat().st_mtime))
    # The profile is recorded with the Wine update it followed. Any Wine
    # program, not only this script, updates an outdated prefix on start
    # and rewrites the shell's registrations; the profile must follow again.
    applied = version.read_text().split() if version.is_file() else []
    if wine_current and applied == [str(registry.VERSION), current]:
        return
    env = dict(os.environ, WINEPREFIX=str(prefix),
               WINEDLLOVERRIDES='explorer,shell32,shdocvw,browseui,shlwapi,comctl32,uxtheme=b;mscoree,mshtml=',
               WINEDEBUG=os.environ.get('WINEDEBUG', '-all'))
    env.pop('WINEARCH', None)  # The existing prefix decides.
    # One regedit import for the profile's values, not a `wine reg` each.
    batch = registry.Batch(wine, prefix, env)
    # Wine upgrades may replace both DLL images and shell COM registrations.
    # Complete that update before restoring the native shell profile.
    print('Updating dedicated prefix Wine/XP registry profile', flush=True)
    try:
        if not wine_current:
            # After a Wine install changes, the update installs every DLL
            # again; on a slow or busy machine it takes minutes.
            progress(0, 'Updating the prefix for this Wine build')
            batch.run('wineboot', '-u', timeout=600)
        progress(0 if wine_current else 70, "The XP shell's registry settings")
        registry.install(batch.run, batch.exists, language(prefix))
        batch.flush()
        current = timestamp.read_text().strip() if timestamp.is_file() else ''
        temp = version.with_suffix('.tmp')
        temp.write_text(f'{registry.VERSION} {current}\n')
        temp.replace(version)
    finally:
        server = str(wine.with_name('wineserver'))
        subprocess.run([server, '-k'], env=env, check=True, timeout=15)
        subprocess.run([server, '-w'], env=env, check=True, timeout=15)

def stage(prefix, wine=None):
    if not (prefix / 'xp-shell.json').is_file():
        raise SystemExit('Not a dedicated XP prefix')
    for name in ('browselc', 'shdoclc'):
        if not (prefix / f'drive_c/xp/native/{name}.dll').is_file():
            raise SystemExit(f'Missing {name}.dll: re-extract local XP media and add it to C:\\xp\\native')
    system = shell_system(prefix)
    for lib, directory in controls_targets(prefix, wine) if wine else ():
        source = lib / 'win4wine-controls.dll'
        if not source.is_file():
            raise SystemExit('Rebuild patched Wine: missing self-built themed controls module')
        target = directory / 'win4wine-controls.dll'
        if not target.is_file() or not filecmp.cmp(source, target, shallow=False):
            with tempfile.NamedTemporaryFile(dir=directory, prefix='.wine-controls-', delete=False) as temp:
                path = Path(temp.name)
            try:
                shutil.copyfile(source, path)
                os.replace(path, target)
            finally:
                path.unlink(missing_ok=True)
    # msgina supplies XP's own shut down dialog and mydocs the icons of My
    # Documents and My Pictures; older extractions lack them. comctl32 is
    # XP's version 5 that create.py places; a new prefix's own first wineboot
    # could still replace it (seen once in the WoW64 test VM).
    for name in ('shell32', 'shdocvw', 'browseui', 'shlwapi', 'comctl32', 'linkinfo', 'browselc', 'shdoclc',
                 'msgina', 'mydocs'):
        source = prefix / f'drive_c/xp/native/{name}.dll'
        target = system / f'{name}.dll'
        if name in ('msgina', 'mydocs') and not source.is_file():
            continue
        if target.is_file() and filecmp.cmp(source, target, shallow=False):
            continue
        # Never truncate an image that a running process may have mapped.
        with tempfile.NamedTemporaryFile(dir=system, prefix='.xp-stage-', delete=False) as temp:
            path = Path(temp.name)
        try:
            shutil.copyfile(source, path)
            os.replace(path, target)
        finally:
            path.unlink(missing_ok=True)

# Folders that Wine's wineboot makes in lower case and that XP's files and
# installers name with capitals (Windows\Resources\Themes).
FOLDER_TWINS = ('windows/resources', 'windows/resources/themes')

def merge(source, target):
    """Move SOURCE's entries into the folder TARGET; of two files, the newer stays."""
    for entry in list(source.iterdir()):
        there = target / entry.name
        if entry.is_dir() and not entry.is_symlink() and there.is_dir():
            merge(entry, there)
            entry.rmdir()
        elif not there.exists() or entry.stat().st_mtime > there.stat().st_mtime:
            if there.is_dir() and not there.is_symlink():
                shutil.rmtree(there)
            os.replace(entry, there)
        elif entry.is_dir():
            shutil.rmtree(entry)
        else:
            entry.unlink()

def merge_folder_twins(prefix):
    """One folder for the names that differ only in case.

    On Linux "Resources" and Wine's "resources" are two folders: Wine opens
    one, while a program or installer that named the other wrote there; a
    theme installed in Resources\\Themes was not in Display Properties. The
    capitalised one goes into Wine's; Wine finds that one for either name (a
    link would be listed as a second folder)."""
    drive = prefix / 'drive_c'
    for relative in FOLDER_TWINS:
        folder = drive / relative
        if not folder.is_dir():
            continue
        for twin in folder.parent.iterdir():
            if twin.name != folder.name and twin.name.lower() == folder.name:
                if twin.is_dir() and not twin.is_symlink():
                    merge(twin, folder)
                    twin.rmdir()
                else:
                    twin.unlink()

def stage_media(prefix, wine=None):
    """Copy XP's default wallpaper and user pictures from the local extraction.

    The files are copied unchanged to the places XP uses; existing files,
    including a user's own picture, are never replaced.
    """
    try:
        assets = Path(json.loads((prefix / 'xp-shell.json').read_text())['assets'])
    except (OSError, ValueError, KeyError):
        return
    user = pwd.getpwuid(os.getuid()).pw_name
    pictures = prefix / 'drive_c/ProgramData/Microsoft/User Account Pictures'
    # XP's 23 default pictures, which User Accounts offers ("Change my
    # picture"); the one it saves as <user>.bmp is the Start menu's.
    defaults = [(source, pictures / 'Default Pictures' / source.name)
                for source in sorted((assets / 'pictures').glob('*.bmp'))]
    for source, target in defaults + [(assets / 'pictures/beach.bmp', pictures / f'{user}.bmp')]:
        if source.is_file() and not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
    # XP's system icons (IDI_APPLICATION and the message box icons) are read
    # from XP's user32.dll as data (Wine 0141), under a name that is not taken
    # for the loaded user32.
    source, target = assets / 'resources/user32.dll', prefix / 'drive_c/xp/res/user32-xp.dll'
    if source.is_file() and not (target.is_file() and filecmp.cmp(source, target, shallow=False)):
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    # XP converts a JPEG wallpaper to Wallpaper1.bmp; without it, XP's desktop
    # switches to the HTML Active Desktop that this shell does not provide.
    # Bliss is the extracted JPEG: XP's Web\Wallpaper has it only as Bliss.bmp
    # (prefix/extras.py), and Display Properties would list it twice.
    jpeg = assets / 'web/wallpaper/bliss.jpg'
    bitmap = prefix / f'drive_c/users/{user}/AppData/Local/Microsoft/Wallpaper1.bmp'
    converter = Path(__file__).resolve().parents[1] / '.local/bridge/wallpaper-convert.exe'
    if wine and jpeg.is_file() and converter.is_file() and not bitmap.exists():
        bitmap.parent.mkdir(parents=True, exist_ok=True)
        env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all')
        env.pop('WINEARCH', None)
        try:
            subprocess.run([str(wine), str(converter), 'Z:' + str(jpeg).replace('/', '\\'),
                            'Z:' + str(bitmap).replace('/', '\\')], env=env, check=False, timeout=120,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        finally:
            # Do not leave a desktop started without the external shell flag.
            server = str(wine.with_name('wineserver'))
            subprocess.run([server, '-k'], env=env, check=False, timeout=15)
            subprocess.run([server, '-w'], env=env, check=False, timeout=15)


# XP's default user marks its special folders in desktop.ini, as read on the
# reference machine: [DeleteOnCopy] names a folder by its CSIDL while it keeps
# its created name, and [.ShellClassInfo] gives its icon and tip.
SPECIAL_FOLDERS = (
    ('users/{user}/Pictures', [('DeleteOnCopy', [('Owner', None), ('Personalized', '39'), ('PersonalizedName', None)]),
                  ('.ShellClassInfo', [('InfoTip', '@Shell32.dll,-12688'),
                                       ('IconFile', r'%SystemRoot%\system32\mydocs.dll'), ('IconIndex', '-101')])]),
    ('users/{user}/Music', [('DeleteOnCopy', [('Owner', None), ('Personalized', '13'), ('PersonalizedName', None)]),
               ('.ShellClassInfo', [('InfoTip', '@Shell32.dll,-12689'),
                                    ('IconFile', r'%SystemRoot%\system32\SHELL32.dll'), ('IconIndex', '-237')])]),
    ('users/{user}/AppData/Roaming/Microsoft/Windows/Recent',
     [('.ShellClassInfo', [('InfoTip', '@Shell32.dll,-12692'), ('IconFile', r'%SystemRoot%\system32\SHELL32.dll'),
                           ('IconIndex', '-21'), ('LocalizedResourceName', '@shell32.dll,-12691')])]),
    ('users/{user}/Favorites',
     [('.ShellClassInfo', [('IconFile', r'%SystemRoot%\system32\SHELL32.dll'), ('IconIndex', '-173'),
                           ('LocalizedResourceName', '@shell32.dll,-12693')])]),
) + tuple((root + menu, [('.ShellClassInfo', [('LocalizedResourceName', name)])])
          for root in ('users/{user}/AppData/Roaming/', 'ProgramData/')
          for menu, name in (('Microsoft/Windows/Start Menu', '@shell32.dll,-21786'),
                             ('Microsoft/Windows/Start Menu/Programs', '@shell32.dll,-21782'),
                             ('Microsoft/Windows/Start Menu/Programs/StartUp',
                              r'@%SystemRoot%\system32\shell32.dll,-21787')))

def hidden(path):
    """Whether Wine records the DOS hidden attribute of PATH."""
    try:
        return bool(int(os.getxattr(path, 'user.DOSATTRIB').split(b'\0')[0], 16) & 2)
    except (OSError, ValueError):
        return False

def stage_folders(prefix, wine):
    """Give the prefix's own special folders XP's names and icons.

    Only real directories inside the prefix are marked, never a link to the
    user's home, and a desktop.ini other than ours is kept as it is.
    """
    user = pwd.getpwuid(os.getuid()).pw_name
    mydocs = (shell_system(prefix) / 'mydocs.dll').is_file()
    marked = []
    for relative, sections in SPECIAL_FOLDERS:
        relative = relative.format(user=user)
        folder = prefix / 'drive_c' / relative
        ini = folder / 'desktop.ini'
        if not folder.is_dir() or folder.is_symlink() or ini.is_symlink():
            continue
        if any(value.endswith('mydocs.dll') for _, entries in sections for _, value in entries if value) and not mydocs:
            continue
        lines = []
        for section, entries in sections:
            lines.append(f'[{section}]')
            for key, value in entries:
                lines.append(f'{key}={value or (user if key == "Owner" else folder.name)}')
        content = '\r\n'.join(lines) + '\r\n'
        if ini.exists():
            # Earlier versions left ours visible; repair only those.
            if hidden(ini) or ini.read_bytes() != content.encode('ascii'):
                continue
        else:
            ini.write_text(content, encoding='ascii', newline='')
        marked.append('C:\\' + relative.replace('/', '\\'))
    icon = prefix / '.xp-mydocs-icon'
    if not marked and (not mydocs or icon.is_file()):
        return
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all')
    env.pop('WINEARCH', None)
    try:
        # The shell reads desktop.ini only in system or read-only folders.
        for folder in marked:
            subprocess.run([str(wine), 'attrib', '+s', folder], env=env, check=False, timeout=120,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            # Wine's attrib applies only the last of several flags.
            for flag in ('+h', '+s'):
                subprocess.run([str(wine), 'attrib', flag, folder + '\\desktop.ini'], env=env, check=False,
                               timeout=120, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if mydocs:
            subprocess.run([str(wine), *registry.shell_view((
                            'reg', 'add', r'HKCR\CLSID\{450D8FBA-AD25-11D0-98A8-0800361B1103}\DefaultIcon',
                            '/ve', '/t', 'REG_EXPAND_SZ', '/d', r'%SystemRoot%\system32\mydocs.dll,0', '/f'))],
                           env=env, check=False, timeout=120, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            icon.write_text('1\n')
    finally:
        server = str(wine.with_name('wineserver'))
        subprocess.run([server, '-k'], env=env, check=False, timeout=15)
        subprocess.run([server, '-w'], env=env, check=False, timeout=15)


# Show Desktop in XP's Quick Launch: the names of XP's setup (shell.inf's
# ShowDesktop string of the Japanese and the English media) and the command
# file's documented format. Its icon is XP Explorer's, whose fourth icon is
# Show Desktop's; "explorer.exe" would be Wine's own explorer.
SHOW_DESKTOP = {'ja': 'デスクトップの表示.scf', 'en': 'Show Desktop.scf'}
SHOW_DESKTOP_COMMAND = ('[Shell]', 'Command=2', r'IconFile=C:\xp\xp-explorer.exe,3',
                        '[Taskbar]', 'Command=ToggleDesktop')

def stage_quick_launch(prefix):
    """XP's Quick Launch folder, with Show Desktop in it, as XP's setup makes it.

    Without the folder XP's taskbar cannot create the Quick Launch toolbar.
    Show Desktop is put only into a folder made here, so that a user's own
    choice of items stays."""
    user = pwd.getpwuid(os.getuid()).pw_name
    folder = prefix / f'drive_c/users/{user}/AppData/Roaming/Microsoft/Internet Explorer/Quick Launch'
    if folder.exists() or folder.is_symlink():
        return
    folder.mkdir(parents=True)
    (folder / SHOW_DESKTOP[language(prefix)]).write_text('\r\n'.join(SHOW_DESKTOP_COMMAND) + '\r\n',
                                                         encoding='ascii', newline='')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', type=Path)
    parser.add_argument('--wine', type=Path)
    args = parser.parse_args()
    prefix = args.prefix.resolve(strict=True)
    if not (prefix / 'xp-shell.json').is_file():
        raise SystemExit('Not a dedicated XP prefix')
    if args.wine:
        prepare(prefix, args.wine.resolve(strict=True))
    progress(85, "XP's files")
    merge_folder_twins(prefix)
    stage(prefix, args.wine.resolve(strict=True) if args.wine else None)
    # XP Explorer's additions (prefix/extras.py): IE6, Display Properties, DirectUser...
    import extras
    if extras.state(prefix):
        extras.stage(prefix)
    progress(92, 'Pictures and wallpaper')
    stage_media(prefix, args.wine.resolve(strict=True) if args.wine else None)
    stage_quick_launch(prefix)
    if args.wine:
        progress(96, 'Special folders')
        stage_folders(prefix, args.wine.resolve(strict=True))
