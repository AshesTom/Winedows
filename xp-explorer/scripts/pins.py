#!/usr/bin/env python3
"""Fill the left column of XP's Start menu.

On XP the top of the left column holds "Internet" and "E-mail": the Start
Menu Internet Applications registered under Software\\Clients\\StartMenuInternet
and Software\\Clients\\Mail (https://learn.microsoft.com/windows/win32/shell/start-menu-reg).
They are registered here as the Linux desktop's web browser (xdg-settings) and
mailto handler (xdg-mime), started through linux-launch.exe with their own
icons. Below them, XP shows its most-used programs in a separate block, using
its own usage data. To pin programs of your own instead, list desktop file IDs
in pins.txt (one per line, at the project root); the most used programs then
follow them. The Start menu keeps the height of its right column, which has
XP's full set of items.
Run with the session stopped. With --clients it only registers the programs
again; with --default TYPE ID it first makes ID the Linux default of TYPE
(Set Program Access and Defaults runs it while the session runs).
"""
import argparse
import json
import os
from pathlib import Path, PureWindowsPath
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / 'session'))
import linux_apps  # noqa: E402
import wine_reg  # noqa: E402
PROGRAMS = r'C:\ProgramData\Microsoft\Windows\Start Menu\Programs'
STATE = r'C:\xp\linux-apps'
LAUNCH = STATE + r'\linux-launch.exe'
RECENT = r'HKEY_CURRENT_USER\Software\XP Explorer\Recent'
ICONS = r'C:\xp\linux-apps\icons'
# Our stand-in (helpers/iexplore.c), which opens the Linux browser; the real
# Internet Explorer 6 stays in Program Files for the desktop's Internet icon.
IEXPLORE = r'C:\xp\bin\iexplore.exe'
DEFAULTS = (
    # Linux apps that older versions added as default pins; unpin them on upgrade.
    ('org.kde.dolphin.desktop', 'org.gnome.Nautilus.desktop', 'thunar.desktop'),
    ('org.kde.konsole.desktop', 'org.gnome.Console.desktop', 'xterm.desktop'),
    ('org.kde.kate.desktop', 'org.kde.kwrite.desktop', 'org.gnome.TextEditor.desktop'),
    ('systemsettings.desktop', 'kdesystemsettings.desktop'),
)
BROWSERS = ('firefox.desktop', 'chromium.desktop', 'google-chrome.desktop', 'org.kde.falkon.desktop')
MAILERS = ('org.mozilla.Thunderbird.desktop', 'thunderbird.desktop', 'org.kde.kmail2.desktop',
           'org.gnome.Evolution.desktop', 'geary.desktop')
# The keys made for the Media and IM types, which XP's own programs share
# (Windows Media Player, Windows Messenger): only these are taken out again.
CLIENTS_STATE = 'drive_c/xp/linux-apps/clients.json'


def linux_clients():
    """The Linux programs of Set Program Access and Defaults' client types
    (https://learn.microsoft.com/windows/win32/shell/reg-middleware-apps), by
    their desktop entries: {type: [(file ID, the MIME types it makes its own)]}."""
    found = {'StartMenuInternet': [], 'Mail': [], 'Media': [], 'IM': []}
    for file_id, (path, entry) in sorted(linux_apps.entries().items()):
        categories = set(linux_apps.strings(entry, 'Categories'))
        types = linux_apps.strings(entry, 'MimeType')
        # A browser opens web pages too, not only links (ChatGPT's app takes http links).
        if 'WebBrowser' in categories or {'x-scheme-handler/http', 'text/html'} <= set(types):
            found['StartMenuInternet'].append((file_id, ()))
        if 'Email' in categories or 'x-scheme-handler/mailto' in types:
            found['Mail'].append((file_id, ('x-scheme-handler/mailto',)))
        media = tuple(t for t in types if t.startswith(('audio/', 'video/')))
        if media and categories & {'AudioVideo', 'Audio', 'Video', 'Player'} and \
                not categories & {'AudioVideoEditing', 'Recorder', 'WebBrowser'}:
            found['Media'].append((file_id, media))
        if 'InstantMessaging' in categories:
            found['IM'].append((file_id, ()))
    return found


def xdg_default(*args):
    try:
        return subprocess.run(args, capture_output=True, text=True, timeout=10).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return ''


def pick(links, preferred, fallbacks):
    return next((i for i in (preferred, *fallbacks) if i and i in links), None)


def pinned(links):
    path = ROOT / 'pins.txt'
    if path.is_file():
        return [line.strip() for line in path.read_text().splitlines() if line.strip() and not line.startswith('#')]
    return []


def name_of(entry, file_id):
    return PureWindowsPath(entry['link']).stem if entry else file_id


def reg_string(text):
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'


def client_keys(root, key, entry, file_id, command, reinstall=None):
    """Registry lines of one client: a Start Menu Internet Application, a
    mail program, a media player or an instant messaging program."""
    name = name_of(entry, file_id).replace('"', '')
    icon = f'{ICONS}\\{file_id}.ico,0'
    base = f'[{root}\\{key}]'
    lines = [base, f'@="{name}"', f'"LocalizedString"="{name}"', '',
             f'[{root}\\{key}\\DefaultIcon]', '@=' + reg_string(icon), '',
             f'[{root}\\{key}\\shell\\open\\command]', '@=' + reg_string(command), '',
             # Set Program Access and Defaults: the icons are shown.
             f'[{root}\\{key}\\InstallInfo]', '"IconsVisible"=dword:00000001']
    if reinstall:
        # What it runs for the program chosen as the default ("Custom"), and
        # for "Enable access to this program", which changes nothing here.
        lines += ['"ReinstallCommand"=' + reg_string(reinstall),
                  '"ShowIconsCommand"=' + reg_string(f'"{LAUNCH}" true'),
                  '"HideIconsCommand"=' + reg_string(f'"{LAUNCH}" true')]
    return lines + ['']


DEFAULT_SCRIPT = 'drive_c/xp/linux-apps/default.sh'


def make_default(kind, file_id, prefix):
    """The ReinstallCommand of a Linux client, run for the program chosen as
    the default: default.sh, which runs this with --default. appwiz.cpl reads
    the command into 260 characters and leaves out a client whose is longer."""
    return f'"{LAUNCH}" sh {prefix / DEFAULT_SCRIPT} {kind} {file_id}'


def write_default_script(prefix, wine):
    again = [sys.executable, str(Path(__file__).resolve()), str(prefix), '--wine',
             str(Path(shutil.which(wine) or wine).resolve())]
    text = '#!/bin/sh\n# Set Program Access and Defaults: make "$2" the default of "$1".\n' + \
           'exec ' + shlex.join(again) + ' --default "$1" "$2"\n'
    path = prefix / DEFAULT_SCRIPT
    if not path.is_file() or path.read_text() != text:
        path.write_text(text)


def set_linux_default(kind, file_id):
    """The Linux default of a client type: the web browser (xdg-settings),
    the mailto handler or the media types (xdg-mime)."""
    types = dict(linux_clients().get(kind, ())).get(file_id)
    if types is None:
        return
    if kind == 'StartMenuInternet':
        subprocess.run(['xdg-settings', 'set', 'default-web-browser', file_id], check=False)
    elif types:
        subprocess.run(['xdg-mime', 'default', file_id, *types], check=False)


def launch_command(links, file_id):
    """A program's own launcher (apps\\<id>.exe) with its arguments."""
    launcher = links[file_id].get('launcher')
    program = f'{STATE}\\apps\\{launcher}' if launcher else LAUNCH
    return f'"{program}" {links[file_id]["arguments"]}'


def register_clients(prefix, wine, links):
    browser = pick(links, xdg_default('xdg-settings', 'get', 'default-web-browser'), BROWSERS)
    mailer = pick(links, xdg_default('xdg-mime', 'query', 'default', 'x-scheme-handler/mailto'), MAILERS)
    others = {kind: [(i, types) for i, types in found if i in links] for kind, found in linux_clients().items()}
    write_default_script(prefix, wine)
    lines = ['Windows Registry Editor Version 5.00', '']
    clients = r'HKEY_LOCAL_MACHINE\Software\Clients'
    # Start over: earlier registrations (Wine's winebrowser.exe among them).
    lines += [f'[-{clients}\\StartMenuInternet]', '', f'[-{clients}\\Mail]', '']
    state = prefix / CLIENTS_STATE
    try:
        made = json.loads(state.read_text())
    except (OSError, ValueError):
        made = []
    for key in made:
        lines += [f'[-{clients}\\{key}]', '']
    made = []
    if browser:
        # XP's explorer realigns the Internet item with the program that opens
        # http links; both are our stand-in (also named iexplore.exe), which
        # opens this browser.
        lines += [f'[{clients}\\StartMenuInternet]', '@="IEXPLORE.EXE"', '']
        lines += client_keys(clients + '\\StartMenuInternet', 'IEXPLORE.EXE', links[browser], browser, f'"{IEXPLORE}"',
                             make_default('StartMenuInternet', browser, prefix))
    # The other Linux programs of each type, which Set Program Access and
    # Defaults offers as the default (Custom); the Linux defaults are the
    # current ones.
    for kind, found in others.items():
        current = {'Mail': mailer}.get(kind) or \
            next((i for i, types in found if types and xdg_default('xdg-mime', 'query', 'default', types[0]) == i), None)
        for file_id, types in found:
            if file_id in (browser, mailer) and kind in ('StartMenuInternet', 'Mail'):
                continue
            key = file_id.removesuffix('.desktop')
            lines += client_keys(f'{clients}\\{kind}', key, links[file_id], file_id, launch_command(links, file_id),
                                 make_default(kind, file_id, prefix))
            if kind in ('Media', 'IM'):
                made.append(f'{kind}\\{key}')
        if kind in ('Media', 'IM') and current and current in dict(found):
            lines += [f'[{clients}\\{kind}]', '@=' + reg_string(current.removesuffix('.desktop')), '']
        for protocol in ('http', 'https', 'ftp'):
            lines += [f'[HKEY_CLASSES_ROOT\\{protocol}\\shell\\open\\command]',
                      '@="\\"' + IEXPLORE.replace('\\', '\\\\') + '\\" \\"%1\\""', '']
    if mailer:
        key = mailer.removesuffix('.desktop')
        lines += [f'[{clients}\\Mail]', f'@="{key}"', '']
        # A mail client opens its inbox; a browser handling mailto opens a new message.
        # Through the mailer's own launcher, so that it is not also counted as
        # a most used program.
        command = (launch_command(links, mailer) if mailer in MAILERS or mailer != browser
                   else f'"{LAUNCH}" xdg-open mailto:')
        lines += client_keys(clients + '\\Mail', key, links[mailer], mailer, command,
                             make_default('Mail', mailer, prefix))
    # The Wine MFU list stays empty; the Start menu viewer draws the usage-ranked
    # programs in the same lower block. Keep XP's full right column height.
    advanced = r'HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced'
    # XP's first logon would reset the usage data and pins; leave its
    # initialized state alone.
    lines += [f'[{advanced}]', '"Start_MinMFU"=dword:00000006', '"Start_TrackProgs"=dword:00000001',
              '"Start_ShowNetPlaces"=dword:00000000',
              '"StartMenuInit"=dword:00000002', '']
    # The user's choice wins over the machine's on XP: leave none behind.
    lines += [r'[-HKEY_CURRENT_USER\Software\Clients\StartMenuInternet]', '',
              r'[-HKEY_CURRENT_USER\Software\Clients\Mail]', '']
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all')
    with tempfile.TemporaryDirectory() as temp:
        reg = Path(temp) / 'clients.reg'
        reg.write_text('\r\n'.join(lines) + '\r\n', encoding='utf-16')
        # The 32-bit view, which XP Explorer reads.
        subprocess.run([wine, r'C:\windows\syswow64\regedit.exe', '/S', 'Z:' + str(reg).replace('/', '\\')],
                       env=env, check=True)
    state.write_text(json.dumps(made) + '\n')
    return browser, mailer


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prefix', type=Path)
    parser.add_argument('--wine', default=os.environ.get('WINE', 'wine'))
    parser.add_argument('--clients', action='store_true', help='only register the programs again')
    parser.add_argument('--default', nargs=2, metavar=('TYPE', 'ID'),
                        help='make ID the Linux default of the client TYPE, then register the programs again')
    args = parser.parse_args()
    if args.default:
        set_linux_default(*args.default)
        args.clients = True
    prefix = args.prefix.resolve(strict=True)
    try:
        links = json.loads((prefix / 'drive_c/xp/linux-apps/state.json').read_text())['links']
    except (OSError, ValueError, KeyError):
        raise SystemExit('Run session/linux_apps.py first')
    browser, mailer = register_clients(prefix, args.wine, links)
    print(f'Internet: {browser or "none"}, E-mail: {mailer or "none"}')
    if args.clients:
        return
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all')
    current = subprocess.run([args.wine, r'C:\xp\bin\startmenu.exe', 'list'], env=env,
                             capture_output=True, text=True).stdout.lower()
    defaults = [PROGRAMS + '\\' + links[i]['link']
                for i in (next((c for c in choices if c in links), '') for choices in DEFAULTS) if i]
    own = [PROGRAMS + '\\' + links[i]['link'] for i in pinned(links) if i in links]
    # Earlier versions pinned the KDE essentials as defaults. Remove those
    # generated pins now that the viewer draws recent programs separately.
    if not (ROOT / 'pins.txt').is_file():
        old = [p for p in defaults if p.lower() in current and not managed(prefix, args.wine, p)]
        if old:
            subprocess.run([args.wine, r'C:\xp\bin\startmenu.exe', 'unpin', *old], env=env)
    # XP's own Internet and E-mail items stay first; the others follow them.
    missing = [p for p in own if p.lower() not in current]
    if missing:
        subprocess.run([args.wine, r'C:\xp\bin\startmenu.exe', 'pin', *missing], env=env)


def managed(prefix, wine, path):
    """Whether startmenu.exe previously managed PATH as a recent program."""
    env = dict(os.environ, WINEPREFIX=str(prefix), WINEDEBUG='-all')
    pins = wine_reg.query(wine, env, RECENT.replace('HKEY_CURRENT_USER', 'HKCU'), 'Pins') or ''
    return path.lower() in pins.lower()


if __name__ == '__main__':
    main()
