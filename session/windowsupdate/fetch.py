#!/usr/bin/env python3
"""The Windows Update site of Windows XP (version 4, 2002-2004) from the
Internet Archive's Wayback Machine, into a cache of this computer.

The site is Microsoft's: it is not part of this checkout. Its pages, styles,
images and scripts are downloaded here, as the XP files come from the user's
own media, and xp_update.py's install_page puts them in the prefix with the
session's engine (wu.js) in place of the ActiveX control and the server.

  fetch.py [CACHE]        default: ~/.cache/win4wine/windowsupdate
"""
import os
from pathlib import Path
import sys
import time
import urllib.error
import urllib.request

SITE = 'http://v4.windowsupdate.microsoft.com'
DEFAULT_CACHE = Path(os.environ.get('XDG_CACHE_HOME') or Path.home() / '.cache') / 'win4wine/windowsupdate'
# (capture, path on the site, file in the cache): the English site of late
# 2003 and its scripts and styles of then, the last version 4 before version 5
# (no page of 2002 was archived).
FILES = (
    ('20040113022344', '/en/default.asp', 'en/default.asp'),
    ('20040203135849', '/en/mstoolbar.asp', 'en/mstoolbar.asp'),
    ('20031218012233', '/en/toc.asp', 'en/toc.asp'),
    ('20031204074141', '/en/footer.asp', 'en/footer.asp'),
    ('20031204082852', '/en/splash.asp?page=0&cpuClass=x86&auenabled=false&', 'en/splash0.asp'),
    ('20031204041852', '/en/splash.asp?page=3&cpuClass=x86&auenabled=false&', 'en/splash3.asp'),
    ('20040520025125', '/en/splash.asp?page=4&auenabled=false&', 'en/splash4.asp'),
    ('20040507030138', '/en/splash.asp?page=5&auenabled=false&', 'en/splash5.asp'),
    ('20031204084406', '/en/splash.asp?page=6&cpuClass=x86&auenabled=false&', 'en/splash6.asp'),
    ('20031204080053', '/en/splash.asp?page=7&cpuClass=x86&auenabled=false&', 'en/splash7.asp'),
    ('20031204034805', '/en/splash.asp?page=8&cpuClass=x86&auenabled=false&', 'en/splash8.asp'),
    ('20040206045101', '/en/splash.asp?page=9&auenabled=false&', 'en/splash9.asp'),
    ('20031204040717', '/en/status.asp', 'en/status.asp'),
    ('20040110155857', '/en/history.asp', 'en/history.asp'),
    ('20031204075221', '/en/personalize.asp', 'en/personalize.asp'),
    ('20031204040312', '/en/support.asp', 'en/support.asp'),
    ('20031210084626', '/en/about.asp', 'en/about.asp'),
    ('20031207062158', '/en/error.asp?error=-1&details=&corporate=false&', 'en/error.asp'),
    ('20030215095136', '/shared/css/content.css', 'shared/css/content.css'),
    ('20030412045237', '/shared/css/footer.css', 'shared/css/footer.css'),
    ('20030215095742', '/shared/css/hcp.css', 'shared/css/hcp.css'),
    ('20030412050050', '/shared/css/mstoolbar.css', 'shared/css/mstoolbar.css'),
    ('20030609191138', '/shared/css/toc.css', 'shared/css/toc.css'),
    ('20020220003608', '/shared/images/arrow.gif', 'shared/images/arrow.gif'),
    ('20030625185036', '/shared/images/complete_icon.gif', 'shared/images/complete_icon.gif'),
    ('20020305211937', '/shared/images/failed_icon.gif', 'shared/images/failed_icon.gif'),
    ('20031205111442', '/shared/images/info_icon.gif', 'shared/images/info_icon.gif'),
    ('20030625191603', '/shared/images/install.gif', 'shared/images/install.gif'),
    ('20030625193143', '/shared/images/mstoolbar_curve.gif', 'shared/images/mstoolbar_curve.gif'),
    ('20040117123505', '/shared/images/mstoolbar_icp.gif', 'shared/images/mstoolbar_icp.gif'),
    ('20030625194553', '/shared/images/mstoolbar_ms.gif', 'shared/images/mstoolbar_ms.gif'),
    ('20020922100313', '/shared/images/sortTableD.gif', 'shared/images/sortTableD.gif'),
    ('20030113102903', '/shared/images/toc_endnode.gif', 'shared/images/toc_endnode.gif'),
    ('20030625200126', '/shared/images/toc_expanded.gif', 'shared/images/toc_expanded.gif'),
    ('20040117125542', '/shared/images/warning_icon.gif', 'shared/images/warning_icon.gif'),
    ('20031128181050', '/shared/js/content.js', 'shared/js/content.js'),
    ('20031128114205', '/shared/js/history.js', 'shared/js/history.js'),
    ('20031128181436', '/shared/js/mstoolbar.js', 'shared/js/mstoolbar.js'),
    ('20040204063416', '/shared/js/personalize.js', 'shared/js/personalize.js'),
    ('20040828233539', '/shared/js/toc.js', 'shared/js/toc.js'),
    ('20040204084333', '/shared/js/top.js', 'shared/js/top.js'),
    ('20040415000002', '/shared/js/top2.js', 'shared/js/top2.js'),
    ('20030924214034', '/shared/js/top.vbs', 'shared/js/top.vbs'),
)


def complete(cache=DEFAULT_CACHE):
    return all((Path(cache) / name).is_file() for _, _, name in FILES)


def fetch(cache=DEFAULT_CACHE, log=print):
    """The missing files; False when one could not be downloaded."""
    ok = True
    for capture, path, name in FILES:
        target = Path(cache) / name
        if target.is_file():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        url = f'https://web.archive.org/web/{capture}id_/{SITE}{path}'
        for attempt in range(5):
            try:
                with urllib.request.urlopen(url, timeout=60) as response:
                    data = response.read()
                # The archive answers with its own page while it is offline.
                if b'Internet Archive' in data[:600] and b'Temporarily Offline' in data:
                    raise urllib.error.URLError('the Internet Archive is offline')
                temp = target.with_name('.' + target.name + '.tmp')
                temp.write_bytes(data)
                temp.replace(target)
                log(f'Windows Update site: {name}')
                break
            except (urllib.error.URLError, OSError) as e:
                if attempt == 4:
                    log(f'Windows Update site: {name} not downloaded ({e})')
                    ok = False
                time.sleep(3 * (attempt + 1))
        time.sleep(1)
    return ok


if __name__ == '__main__':
    sys.exit(0 if fetch(Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_CACHE) else 1)
