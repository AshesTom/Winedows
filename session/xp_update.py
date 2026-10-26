#!/usr/bin/env python3
"""Windows Update for the Linux system: the helper of session/windowsupdate.

XP's Windows Update site (C:\\xp\\windowsupdate\\default.hta) starts this
through linux-launch.exe and reads what it writes in the prefix, in
C:\\xp\\windowsupdate\\state (Wine's WScript.Shell cannot read a program's
output):

  xp_update.py scan       scan.json: the system's available updates
                          (pacman's checkupdates, Flatpak's), by XP's categories
  xp_update.py install    install.json: installs them all in a terminal with
                          XP's console look (sudo pacman -Syu, flatpak update),
                          as Arch Linux updates the whole system at once
  xp_update.py history    history.json: the package manager's past updates
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

ROOT = Path(__file__).resolve().parents[1]
PAGE = 'drive_c/xp/windowsupdate'
STATE = PAGE + '/state'
PACMAN_LOG = Path('/var/log/pacman.log')
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
        write(state, 'scan', dict(names, status='done', progress=100, updates=updates))
    except (OSError, RuntimeError, subprocess.SubprocessError) as e:
        write(state, 'scan', dict(names, status='error', error=str(e), updates=[]))


def install(state, prefix):
    """All the updates, in a terminal: sudo asks for the password there."""
    write(state, 'install', dict(status='working'))
    result = state / '.install-exit'
    result.unlink(missing_ok=True)
    steps = ['sudo pacman -Syu']
    if shutil.which('flatpak'):
        steps.append('flatpak update -y')
    script = (f'echo "Windows Update is installing the updates of the Linux system."; echo; '
              f'status=0; for step in {" ".join(shlex.quote(s) for s in steps)}; do echo "> $step"; '
              f'$step || status=$?; echo; done; echo $status > {shlex.quote(str(result))}; '
              f'read -r -p "Press Enter to close this window." _')
    subprocess.run([sys.executable, str(ROOT / 'session/xp_cmd.py'), str(prefix), '--title', 'Windows Update',
                    '--icon', 'wupdmgr', '--run', script])
    try:
        code = int(result.read_text().strip())
    except (OSError, ValueError):
        code = -1       # closed before it ended
    result.unlink(missing_ok=True)
    write(state, 'install', dict(status='done', exit=code))


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
    parser.add_argument('command', choices=('scan', 'install', 'history', 'page'))
    parser.add_argument('--prefix', type=Path, default=os.environ.get('WINEPREFIX'))
    args = parser.parse_args()
    if not args.prefix:
        sys.exit('No prefix: give --prefix or WINEPREFIX')
    state = Path(args.prefix) / STATE
    if args.command == 'scan':
        scan(state)
    elif args.command == 'install':
        install(state, Path(args.prefix))
    elif args.command == 'page':
        install_page(args.prefix)
    else:
        history(state)


if __name__ == '__main__':
    main()
