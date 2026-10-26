#!/usr/bin/env python3
"""Windows Update for the Linux system: the helper of session/windowsupdate.

XP's Windows Update site (C:\\xp\\windowsupdate\\default.hta) starts this
through linux-launch.exe and reads what it writes in the prefix, in
C:\\xp\\windowsupdate\\state (Wine's WScript.Shell cannot read a program's
output):

  xp_update.py scan       scan.json: the system's available updates
                          (pacman's checkupdates, Flatpak's), by XP's categories,
                          and Winedows XP's (this checkout's GitHub releases)
  xp_update.py install    install.json: installs them all in a terminal with
                          XP's console look (sudo pacman -Syu, flatpak update),
                          as Arch Linux updates the whole system at once
  xp_update.py history    history.json: the package manager's past updates, and
                          Winedows XP's
  xp_update.py winedows TAG   install's step for Winedows XP's own updates: the
                          releases of this checkout's repository on GitHub
  xp_update.py page       puts the site in the prefix (prefix/extras.py's stage):
                          Windows Update's site of XP (version 4), from the
                          Internet Archive, with the engine of this checkout

The prefix is $WINEPREFIX (the session's), or --prefix.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
PAGE = 'drive_c/xp/windowsupdate'
STATE = PAGE + '/state'
PACMAN_LOG = Path('/var/log/pacman.log')
# Winedows XP's own updates: the releases of this checkout's repository on
# GitHub (its origin, or XP_UPDATE_REPO=owner/name), each one a cumulative update.
RELEASES = 'https://api.github.com/repos/{}/releases?per_page=100'
WINEDOWS_TITLE = 'Cumulative Security Update for Winedows XP (KB{})'
# The updates of Winedows XP this computer installed, for the history.
INSTALLED = 'installed.json'
# Windows Update's categories (version 4, 2002) for the system's packages.
CRITICAL = re.compile(r'^(linux(-[a-z0-9]+)*|glibc|lib32-glibc|systemd.*|openssl.*|sudo|pacman|.*-keyring|gnupg|'
                      r'openssh|polkit|dbus.*|.*-ucode|ca-certificates.*|gnutls|krb5|pam|shadow|coreutils)$')
DRIVERS = re.compile(r'^(nvidia.*|lib32-nvidia.*|mesa.*|lib32-mesa.*|vulkan-.*|lib32-vulkan-.*|.*-firmware.*|'
                     r'xf86-.*|.*-dkms|opencl-.*|lib32-opencl-.*)$')
SIZE = re.compile(r'([\d.]+)\s*(B|KiB|MiB|GiB)')
UNITS = {'B': 1, 'KiB': 1024, 'MiB': 1024 ** 2, 'GiB': 1024 ** 3}


def category(name):
    if DRIVERS.match(name):
        return 'drivers'
    if CRITICAL.match(name) and not name.endswith('-docs'):
        return 'critical'
    return 'system'


def write(state, name, data):
    """STATE/NAME.json, replaced at once: the page reads it while it waits."""
    state.mkdir(parents=True, exist_ok=True)
    temp = state / f'.{name}.tmp'
    temp.write_text(json.dumps(dict(data, time=time.strftime('%Y-%m-%d %H:%M')), ensure_ascii=True))
    temp.replace(state / f'{name}.json')


def pacman_updates(progress):
    """[update] from checkupdates (a copy of the sync databases: no root, the
    system's own stay as they are), with pacman's details of each."""
    env = dict(os.environ, LC_ALL='C')
    result = subprocess.run(['checkupdates'], capture_output=True, text=True, env=env, timeout=600)
    # 2: no updates; 1: an error (no network, databases locked)
    if result.returncode == 1:
        raise RuntimeError(result.stderr.strip() or 'checkupdates failed')
    updates = []
    for line in result.stdout.splitlines():
        match = re.match(r'(\S+) (\S+) -> (\S+)', line)
        if match:
            name, old, new = match.groups()
            updates.append(dict(name=name, old=old, version=new, size=0, description='', repository='',
                                kind='pacman', category=category(name)))
    if not updates:
        return []
    progress(60)
    database = Path(env.get('CHECKUPDATES_DB') or Path(env.get('TMPDIR', '/tmp')) / f'checkup-db-{os.getuid()}')
    details, current = {}, {}
    info = subprocess.run(['pacman', '-Si', '--dbpath', str(database), *(u['name'] for u in updates)],
                          capture_output=True, text=True, env=env, timeout=120).stdout
    for line in info.splitlines() + ['']:
        if not line.strip():
            # The first repository that has a package is the one pacman takes.
            if current.get('Name') and current['Name'] not in details:
                details[current['Name']] = current
            current = {}
        elif ' : ' in line:
            key, _, value = line.partition(' : ')
            current[key.strip()] = value.strip()
    for update in updates:
        found = details.get(update['name'], {})
        size = SIZE.match(found.get('Download Size', ''))
        update.update(description=found.get('Description', ''), repository=found.get('Repository', ''),
                      size=int(float(size.group(1)) * UNITS[size.group(2)]) if size else 0)
    return updates


def flatpak_updates():
    if not shutil.which('flatpak'):
        return []
    result = subprocess.run(['flatpak', 'remote-ls', '--updates', '--columns=name,application,version'],
                            capture_output=True, text=True, timeout=600)
    updates = []
    for line in result.stdout.splitlines():
        fields = line.split('\t')
        if len(fields) >= 2 and fields[1]:
            updates.append(dict(name=fields[0] or fields[1], old='', version=fields[2] if len(fields) > 2 else '',
                                size=0, description=fields[1], repository='flathub', kind='flatpak',
                                category='applications'))
    return updates


def git(*args, timeout=120):
    return subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True, timeout=timeout)


def project_repository():
    """owner/name of this checkout on GitHub."""
    if os.environ.get('XP_UPDATE_REPO'):
        return os.environ['XP_UPDATE_REPO']
    url = git('remote', 'get-url', 'origin').stdout.strip()
    match = re.search(r'github\.com[:/]([^/]+/[^/]+?)(?:\.git)?/?$', url)
    return match.group(1) if match else None


def kb_numbers(releases):
    """{tag: KB number}: the day it was published (YYMMDD, UTC) and its rank
    that day, so 26 October 2026's first is KB2610261 and its second
    KB2610262; an older release always has a smaller number."""
    numbers, days = {}, {}
    for release in sorted(releases, key=lambda r: r['published_at']):
        day = release['published_at'][2:10].replace('-', '')
        days[day] = days.get(day, 0) + 1
        numbers[release['tag_name']] = f'{day}{days[day]}'
    return numbers


def winedows_updates():
    """[update]: the releases whose tag this checkout does not contain yet."""
    repository = project_repository()
    if not repository:
        return []
    request = urllib.request.Request(RELEASES.format(repository), headers={'Accept': 'application/vnd.github+json',
                                                                          'User-Agent': 'win4wine-windows-update'})
    with urllib.request.urlopen(request, timeout=30) as response:
        releases = [r for r in json.load(response) if not r['draft'] and not r['prerelease'] and r['published_at']]
    if not releases:
        return []
    git('fetch', '--quiet', '--tags', f'https://github.com/{repository}.git')
    numbers = kb_numbers(releases)
    updates = []
    for release in sorted(releases, key=lambda r: r['published_at']):
        tag = release['tag_name']
        if git('merge-base', '--is-ancestor', f'refs/tags/{tag}', 'HEAD').returncode == 0:
            continue
        name = (release['name'] or tag).partition(': ')[2] or release['name'] or tag
        updates.append(dict(name=f'winedows-{tag}', title=WINEDOWS_TITLE.format(numbers[tag]), old='', version=tag,
                            size=0, description=f'Winedows XP {tag}: {name}', details=release['html_url'],
                            repository='Winedows XP', kind='winedows', category='critical', kb=numbers[tag]))
    return updates


def system_name():
    """The distribution's name (os-release), the site's "Windows"."""
    try:
        for line in Path('/etc/os-release').read_text().splitlines():
            if line.startswith('NAME='):
                return line.partition('=')[2].strip().strip('"')
    except OSError:
        pass
    return 'Linux'


def scan(state):
    names = dict(system=system_name(), applications='Applications (Flatpak)')
    write(state, 'scan', dict(names, status='working', progress=0))
    try:
        updates = pacman_updates(lambda percent: write(state, 'scan', dict(names, status='working', progress=percent)))
        write(state, 'scan', dict(names, status='working', progress=85))
        updates += flatpak_updates()
        write(state, 'scan', dict(names, status='working', progress=90))
        try:
            updates = winedows_updates() + updates
        except (OSError, ValueError, KeyError, subprocess.SubprocessError) as e:
            print(f'Winedows XP updates: {e}', file=sys.stderr)
        write(state, 'scan', dict(names, status='done', progress=100, updates=updates))
    except (OSError, RuntimeError, subprocess.SubprocessError) as e:
        write(state, 'scan', dict(names, status='error', error=str(e), updates=[]))


def install(state, prefix):
    """The updates the last scan found, in a terminal: sudo asks for the
    password there. Winedows XP's are cumulative: this checkout goes to the
    newest release, and Wine is built again when its patches changed."""
    write(state, 'install', dict(status='working'))
    result = state / '.install-exit'
    result.unlink(missing_ok=True)
    try:
        updates = json.loads((state / 'scan.json').read_text())['updates']
    except (OSError, ValueError, KeyError):
        updates = []
    kinds = {u['kind'] for u in updates}
    steps = []
    if 'pacman' in kinds or not updates:
        steps.append('sudo pacman -Syu')
    if 'flatpak' in kinds:
        steps.append('flatpak update -y')
    winedows = [u for u in updates if u['kind'] == 'winedows']
    if winedows:
        steps.append(f'{shlex.quote(sys.executable)} {shlex.quote(__file__)} winedows {shlex.quote(winedows[-1]["version"])}'
                     f' --prefix {shlex.quote(str(prefix))}')
    script = (f'echo "Windows Update is installing the updates."; echo; '
              f'status=0; for step in {" ".join(shlex.quote(s) for s in steps)}; do echo "> $step"; '
              f'eval "$step" || status=$?; echo; done; echo $status > {shlex.quote(str(result))}; '
              f'read -r -p "Press Enter to close this window." _')
    subprocess.run([sys.executable, str(ROOT / 'session/xp_cmd.py'), str(prefix), '--title', 'Windows Update',
                    '--icon', 'wupdmgr', '--run', script])
    try:
        code = int(result.read_text().strip())
    except (OSError, ValueError):
        code = -1       # closed before it ended
    result.unlink(missing_ok=True)
    write(state, 'install', dict(status='done', exit=code, restart=bool(winedows) and code == 0))


def install_winedows(state, tag):
    """This checkout at release TAG (its updates since are cumulative), the
    Wine and the bridge it builds; the prefix takes the rest at the next
    logon (xp.sh run, the session's additions)."""
    print(f'Winedows XP {tag}')
    if git('merge', '--ff-only', f'refs/tags/{tag}', timeout=600).returncode:
        print(f'{ROOT} has changes of its own that {tag} does not contain: update it with git yourself.')
        return 1
    local = ROOT / '.local'
    digest = subprocess.run([sys.executable, str(ROOT / 'wine/patch-state.py'), '--digest', str(ROOT / 'wine/patches')],
                            capture_output=True, text=True).stdout.strip()
    built = local / 'wine-install/win4wine-patches'
    if not built.is_file() or built.read_text().strip() != digest:
        print('Building Wine (log: .local/wine-build.log); this takes a long time')
        with open(local / 'wine-build.log', 'w') as log:
            if subprocess.run([str(ROOT / 'wine/build.sh')], stdout=log, stderr=subprocess.STDOUT,
                              env=dict(os.environ, JOBS=str(os.cpu_count() or 1))).returncode:
                print('The Wine build failed; see .local/wine-build.log')
                return 1
    if subprocess.run([str(ROOT / 'bridge-wm/build.sh')]).returncode:
        return 1
    try:
        installed = json.loads((state / INSTALLED).read_text())
    except (OSError, ValueError):
        installed = []
    scan_updates = json.loads((state / 'scan.json').read_text()).get('updates', [])
    for update in scan_updates:
        if update['kind'] == 'winedows':
            installed.append(dict(date=time.strftime('%Y-%m-%d %H:%M'), title=update['title'], source='iu_site'))
    (state / INSTALLED).write_text(json.dumps(installed))
    print('Log off, and then log on again, to finish installing Winedows XP\'s update.')
    return 0


def history(state):
    """The package manager's updates, newest first (pacman.log)."""
    entries = []
    try:
        lines = PACMAN_LOG.read_text(errors='replace').splitlines()
    except OSError:
        lines = []
    for line in lines:
        match = re.match(r'\[([^\]]+)\] \[ALPM\] (upgraded|installed|removed) (\S+) \((.*)\)$', line)
        if match and match.group(2) != 'removed':
            stamp, action, name, versions = match.groups()
            entries.append(dict(date=stamp[:16].replace('T', ' '), action=action, name=name,
                                title=f"{name} {versions.split(' -> ')[-1]}", source='pacman',
                                category=category(name)))
    try:
        entries += json.loads((state / INSTALLED).read_text())
    except (OSError, ValueError):
        pass
    entries.sort(key=lambda e: e['date'])
    write(state, 'history', dict(status='done', entries=entries[::-1][:300]))


# The site's pages that the builder takes, and what each is called here.
PAGES = ('mstoolbar', 'toc', 'footer', 'splash0', 'splash3', 'splash4', 'splash5', 'splash6', 'splash7',
         'splash8', 'splash9', 'status', 'history', 'personalize', 'support', 'about', 'error')
SITE_LINK = re.compile(r'(?<=["\'])(?:/en/)?(\w+)\.asp(?:\?([^"\'#]*))?(#[^"\']*)?(?=["\'])')
OUTSIDE_LINK = re.compile(r'href="(?:https?:|mailto:|/troubleshoot/|/catalog/)[^"]*"')
# The installation's result, which status.asp drew from what the site posted.
STATUS_SCRIPT = '''
		<script language="JScript" type="text/javascript">
var L_StatusFailedTitle_Text = "Installation Failed";
var L_StatusFailedDesc_Text = "The following updates were not installed:";
(function(){
	var oXML, vUpdates, sInstalled, sFailed, sTitle, i;

	if(self == top || parent.g_sPostedXML == null) return;
	oXML = parent.xmlNewXML();
	oXML.loadXML(parent.g_sPostedXML);
	vUpdates = oXML.selectNodes("catalog/item");
	sInstalled = sFailed = "";
	for(i = 0; i < vUpdates.length; i++){
		sTitle = vUpdates[i].selectSingleNode("title").text.replace(/&/g, "&amp;").replace(/</g, "&lt;");
		if(vUpdates[i].getAttribute("installed") == "1") sInstalled += "<li>" + sTitle + "</li>"; else sFailed += "<li>" + sTitle + "</li>";
	}
	if(sFailed != ""){
		eStatusIcon.source = "shared/images/failed_icon.gif";
		eStatusTitle.innerText = L_StatusFailedTitle_Text;
		eStatusDesc.innerText = L_StatusFailedDesc_Text;
		sInstalled = sFailed;
	}
	eStatusUpdates.innerHTML = "<ul>" + sInstalled + "</ul>";
})();
		</script>
	'''


def site_text(text, page=True):
    """A page, script or style of the site, for its folder in the prefix:
    its links to the site's own, the scripts of the server's out. The
    scripts' links are wu.js's."""
    text = re.sub(r'(?<=["\'(])/shared/', 'shared/', text)
    if not page:
        return text.replace('"news.asp?ln=" + parent.conLangCode', '"about:blank"')

    def link(match):
        page, query, anchor = match.group(1).lower(), match.group(2) or '', match.group(3) or ''
        if page == 'default':
            return 'javascript:top.fnDisplaySplashPage(3);'
        if page == 'splash':
            number = re.match(r'page=(\d+)', query)
            if not number:         # the scripts' own, which wu.js replaces
                return match.group(0)
            page += number.group(1)
        return f'{page}.htm{anchor}' if page in PAGES else 'javascript:void(0);'
    text = SITE_LINK.sub(link, text)
    # Microsoft's other sites are gone; the links stay, and lead nowhere.
    text = OUTSIDE_LINK.sub('href="javascript:void(0);"', text)
    text = re.sub(r'<script[^>]*src="[^"]*(?:redirect|survey)\.js[^"]*"></script>\s*', '', text, flags=re.I)
    text = text.replace('"news.asp?ln=" + parent.conLangCode', '"about:blank"')
    return text


def site_page(name, text):
    text = site_text(text)
    if name == 'splash3':
        # "Protect your PC", of 2004, is not the site of XP
        text = re.sub(r'\s*<td align="right" style="padding-right:3px;.*?</td>', '', text, count=1, flags=re.S)
    elif name == 'status':
        text = text.replace('<img source=', '<img id="eStatusIcon" source=', 1)
        text = text.replace('<span class="sys-font-heading3', '<span id="eStatusTitle" class="sys-font-heading3', 1)
        text = text.replace('<span>The following', '<span id="eStatusDesc">The following', 1)
        text = text.replace('</span><br /><br />\n\n\t\t<br />',
                            '</span><br />\n\t\t<div id="eStatusUpdates"></div>\n\t\t<br />', 1)
        text = text.replace('</body>', STATUS_SCRIPT + '</body>', 1)
    return text


def site_application(text):
    """default.asp's frameset as XP's mshta.exe runs it, with its strings, the
    site's scripts and wu.js in place of the control."""
    strings = re.search(r'<script language="JScript" type="text/javascript" defer="true">\s*(var conWinNT.*?)</script>',
                        text, re.S).group(1)
    strings = (strings.replace('var conWinNT = ;', 'var conWinNT = true;')
               .replace('var conBrowserVersion = ;', 'var conBrowserVersion = 6;')
               .replace('var conConsumerURL = "/en/";', 'var conConsumerURL = "";')
               .replace('var conCatalogURL = "/catalog/en/";', 'var conCatalogURL = "";'))
    strings += ('var L_InstallAllText_Text = "The updates of the Linux system are installed together: '
                'Windows Update will install all |UPDATES| updates.\\n\\nTo continue, click OK.";\r\n')
    strings += ('var L_WinedowsRestart_Text = "To finish installing the update of Winedows XP, log off, '
                'and then log on again.";\r\n')
    title = re.search(r'<title>.*?</title>', text).group(0)
    script = '\t\t<script language="{}" type="text/{}" src="{}"></script>\r\n'
    frame = ' application="yes" frameborder="0"'
    return (
        '<html dir="ltr">\r\n\t<head>\r\n'
        '\t\t<meta http-equiv="Content-Type" content="text/html;charset=windows-1252" />\r\n'
        '\t\t<meta http-equiv="MSThemeCompatible" content="Yes" />\r\n'
        f'\t\t{title}\r\n'
        '\t\t<HTA:APPLICATION ID="WindowsUpdate" APPLICATIONNAME="Windows Update" '
        'ICON="C:\\windows\\system32\\wupdmgr.exe" SINGLEINSTANCE="yes" SCROLL="no" CONTEXTMENU="no" />\r\n'
        '\t\t<script language="JScript" type="text/javascript">\r\n'
        '\t\t\twindow.resizeTo(800, 600);\r\n'
        '\t\t\twindow.moveTo(Math.max(0, (screen.availWidth - 800)/2), Math.max(0, (screen.availHeight - 600)/2));\r\n'
        '\t\t\tfunction fnSupressError(sMessage, sURL, sLine){ return true; }\r\n'
        '\t\t\twindow.onerror = fnSupressError ;\r\n'
        '\t\t</script>\r\n'
        + script.format('JScript', 'javascript', 'shared/js/top.js')
        + script.format('JScript', 'javascript', 'shared/js/top2.js')
        + script.format('VBScript', 'vbscript', 'shared/js/top.vbs')
        + script.format('JScript', 'javascript', 'config.js')
        + script.format('JScript', 'javascript', 'wu.js')
        + '\t\t<script language="JScript" type="text/javascript" defer="true">\r\n' + strings + '\t\t</script>\r\n'
        '\t</head>\r\n'
        f'<frameset rows="80, *, 24" border="0"><frame name="eToolbar" src="mstoolbar.htm" noresize="yes" scrolling="no"{frame} style="border-bottom: 1px solid white;" />'
        f'<frameset cols="250,*" framespacing="3"><frame name="eTOC" src="toc.htm"{frame} />'
        f'<frame name="eContent" src="splash0.htm"{frame} /></frameset>'
        f'<frame name="eFooter" src="footer.htm" noresize="yes" scrolling="no"{frame} style="border-top: 1px solid white;" /></frameset>\r\n'
        '</html>\r\n')


def update_file(path, data):
    if not path.is_file() or path.read_bytes() != data:
        path.parent.mkdir(parents=True, exist_ok=True)
        temp = path.with_name('.' + path.name + '.tmp')
        temp.write_bytes(data)
        temp.replace(path)


def install_page(prefix, wait=True):
    """The Windows Update site in C:\\xp\\windowsupdate: Microsoft's, from the
    Internet Archive (session/windowsupdate/fetch.py, once into a cache of
    this computer), with this checkout's engine and pages of updates, and
    config.js (where this helper and the prefix are). Without wait, a site
    not downloaded yet comes in the background (the logon goes on): a page
    says so meanwhile."""
    sys.path.insert(0, str(ROOT / 'session/windowsupdate'))
    import fetch
    target = Path(prefix) / PAGE
    if not fetch.complete():
        if not wait:
            update_file(target / 'default.hta', OFFLINE_PAGE.encode())
            subprocess.Popen([sys.executable, __file__, 'page', '--prefix', str(prefix)], start_new_session=True,
                             stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return False
        fetch.fetch(log=lambda line: print(line, file=sys.stderr))
    if not fetch.complete():
        update_file(target / 'default.hta', OFFLINE_PAGE.encode())
        return False
    cache = fetch.DEFAULT_CACHE
    for _, _, name in fetch.FILES:
        source = cache / name
        if name.startswith('shared/'):
            data = source.read_bytes()
            if source.suffix in ('.js', '.css', '.vbs'):
                data = site_text(data.decode('latin-1'), page=False).encode('latin-1')
            update_file(target / name, data)
        elif source.stem in PAGES:
            update_file(target / f'{source.stem}.htm', site_page(source.stem, source.read_text('latin-1')).encode('latin-1'))
    update_file(target / 'default.hta', site_application(site_text((cache / 'en/default.asp').read_text('latin-1'))).encode('latin-1'))
    for name in ('wu.js', 'results.htm'):
        update_file(target / name, (ROOT / 'session/windowsupdate' / name).read_bytes())
    update_file(target / 'config.js', (f'var WU_HELPER = {json.dumps(str(Path(__file__).resolve()))};\r\n'
                                       f'var WU_PREFIX = {json.dumps(str(Path(prefix).resolve()))};\r\n').encode())
    return True


OFFLINE_PAGE = '''<html>
<head><title>Microsoft Windows Update</title>
<HTA:APPLICATION ID="WindowsUpdate" APPLICATIONNAME="Windows Update" ICON="C:\\windows\\system32\\wupdmgr.exe" SINGLEINSTANCE="yes" />
<script language="JScript">window.resizeTo(520, 220);</script></head>
<body style="font: messagebox; margin: 16px;">
<b>Windows Update is not ready.</b><br /><br />
The Windows Update site comes from the Internet Archive once, when the session starts. Try again in a minute;
if this page stays, check the connection to the Internet, and then start the session again.
</body>
</html>
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('command', choices=('scan', 'install', 'history', 'page', 'winedows'))
    parser.add_argument('tag', nargs='?', help="winedows: the release to install (install's own step)")
    parser.add_argument('--prefix', type=Path, default=os.environ.get('WINEPREFIX'))
    args = parser.parse_args()
    if not args.prefix:
        sys.exit('No prefix: give --prefix or WINEPREFIX')
    state = Path(args.prefix) / STATE
    if args.command == 'scan':
        scan(state)
    elif args.command == 'install':
        install(state, Path(args.prefix))
    elif args.command == 'winedows':
        sys.exit(install_winedows(state, args.tag))
    elif args.command == 'page':
        install_page(args.prefix)
    else:
        history(state)


if __name__ == '__main__':
    main()
