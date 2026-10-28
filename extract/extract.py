#!/usr/bin/env python3
"""Extract only from a user supplied local XP SP3 ISO or Windows directory."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
sys.path.insert(0, str(Path(__file__).resolve().parent))
from media import language as media_language

FILES = {
    'explorer.exe': 'windows',
    **dict.fromkeys(('shell32.dll', 'browseui.dll', 'shdocvw.dll', 'shlwapi.dll',
                     'browselc.dll', 'shdoclc.dll', 'msgina.dll', 'comctl32.dll', 'linkinfo.dll', 'uxtheme.dll', 'shellstyle.dll', 'explorer.exe.manifest',
                     # Only the icons of My Documents and My Pictures are read from mydocs.dll.
                     'mydocs.dll',
                     # The login screen reads images and strings from these (greeter/).
                     'logonui.exe', 'winlogon.exe',
                     # DirectUser: draws the folder task pane (common tasks).
                     'duser.dll'), 'system32'),
    # XP's Display Properties (Themes, Desktop, Screen Saver, Appearance, Settings).
    **dict.fromkeys(('desk.cpl', 'themeui.dll', 'deskadp.dll', 'deskmon.dll', 'logon.scr'), 'cpl'),
    # Icons and dialog templates of the notification area's network, battery
    # and volume icons (xp-explorer/helpers/xptray.c).
    **dict.fromkeys(('stobject.dll', 'powercfg.cpl', 'batmeter.dll', 'netshell.dll', 'sndvol32.exe', 'xpsp2res.dll'), 'tray'),
    # XP's keyboard icon, for the session's Keyboard Shortcuts item (bridge-wm/shortcuts.c).
    'main.cpl': 'keyboard',
    # Internet Explorer 6 for the desktop's Internet icon, installed beside its
    # iexplore.exe (prefix/extras.py), never over Wine's system DLLs.
    **dict.fromkeys(('iexplore.exe', 'mshtml.dll', 'mshtmled.dll', 'mshtmler.dll', 'mshtml.tlb', 'jscript.dll',
                     'vbscript.dll', 'urlmon.dll', 'wininet.dll', 'mlang.dll', 'msls31.dll', 'iepeers.dll',
                     'imgutil.dll', 'pngfilt.dll', 'msrating.dll', 'dxtrans.dll', 'dxtmsft.dll',
                     'iedkcs32.dll'), 'ie'),
    # Windows Media Player 9 (and 6.4, mplayer2.exe) with the Windows Media Format
    # runtime and decoders, installed beside wmplayer.exe (prefix/extras.py).
    **dict.fromkeys(('wmplayer.exe', 'mplayer2.exe', 'wmp.dll', 'wmploc.dll', 'wmpcore.dll', 'wmpui.dll',
                     'wmpband.dll', 'wmpasf.dll', 'wmpdxm.dll', 'wmpns.dll', 'wmpcd.dll', 'wmpshell.dll',
                     'mpvis.dll', 'wmvcore.dll', 'wmasf.dll', 'wmnetmgr.dll', 'wmidx.dll', 'wmerror.dll',
                     'wmerrenu.dll', 'qasf.dll', 'msnetobj.dll', 'dxmasf.dll', 'wmadmod.dll', 'wmvdmod.dll',
                     'wmv8dmod.dll', 'wmspdmod.dll', 'mp43dmod.dll', 'msscp.dll', 'drmclien.dll',
                     'drmstor.dll', 'blackbox.dll', 'drmv2clt.dll', 'setup_wm.exe', 'unregmp2.exe',
                     'custsat.dll', 'wmp.inf'), 'wmp'),
    # XP's Regional and Language Options with its Text Services and Input
    # Languages (input.dll) and what they load, and the INFs that list XP's
    # keyboard layouts (prefix/extras.py).
    **dict.fromkeys(('intl.cpl', 'input.dll', 'syssetup.dll', 'sfcfiles.dll', 'intl.inf', 'hivesys.inf'), 'intl'),
    # The Search Companion and its animated characters (Rover): what XP's
    # srchasst.inf and Microsoft Agent install, and the msxml3 and oleacc
    # they need beside IE6's engine (xp-explorer/scripts/prefix.py copy_search).
    **dict.fromkeys(('srchui.dll', 'srchctls.dll', 'msgr3en.dll', 'nls302en.lex'), 'srchasst'),
    **dict.fromkeys(('rover.acs', 'courtney.acs', 'earl.acs'), 'srchasst/chars'),
    **dict.fromkeys(('balloon.xsl', 'bar.xsl', 'charchsr.xml', 'charctxt.xml', 'error.xml', 'finish.xml',
                     'indxsvc.xml', 'inetfind.xml', 'inetopts.xml', 'inetpref.xml', 'inetsrch.xml',
                     'intents.xml', 'intro.xml', 'lcladv.xml', 'lcladvd.xml', 'lcladvdf.xml', 'lcladvmm.xml',
                     'lclcomp.xml', 'lcldate.xml', 'lcldocs.xml', 'lclkwrds.xml', 'lcllook.xml', 'lclmm.xml',
                     'lclmode.xml', 'lclother.xml', 'lclprog.xml', 'lclrfine.xml', 'lclsize.xml',
                     'lclsrch.xml', 'lcltechy.xml'), 'srchasst/mui'),
    **dict.fromkeys(('agentsvr.exe', 'agentctl.dll', 'agentdp2.dll', 'agentdpv.dll', 'agentmpx.dll',
                     'agentanm.dll', 'agentsr.dll', 'agentpsh.dll', 'agtintl.dll', 'mslwvtts.dll',
                     'agtctl15.tlb'), 'srchasst/msagent'),
    **dict.fromkeys(('agt0409.dll', 'agt0411.dll'), 'srchasst/msagent/intl'),
    **dict.fromkeys(('msxml3.dll', 'msxml3r.dll', 'oleacc.dll', 'oleaccrc.dll'), 'srchasst/system'),
    # XP's Security Center: its service, the notification area's alerts and
    # the Control Panel item (prefix/extras.py).
    **dict.fromkeys(('wscsvc.dll', 'wscntfy.exe', 'wscui.cpl'), 'wsc'),
    # XP's User Accounts (nusrmgr.cpl): its HTML application, which XP's
    # mshta.exe runs (Wine's is a stub), and the user objects it scripts
    # (shgina.dll), where the account picture is chosen (prefix/extras.py).
    **dict.fromkeys(('nusrmgr.cpl', 'shgina.dll', 'mshta.exe'), 'accounts'),
    # XP's Add or Remove Programs (appwiz.cpl, DirectUser pages) with the
    # texts that SP1 added to it, Set Program Access and Defaults among them
    # (xpsp1res.dll, which it loads beside xpsp2res.dll; prefix/extras.py).
    **dict.fromkeys(('appwiz.cpl', 'xpsp1res.dll'), 'appwiz'),
    # The Control Panel items that the Security Center lists under "Manage
    # security settings for": Internet Options (IE6's, its pages in
    # inetcplc.dll) and Automatic Updates (prefix/extras.py).
    **dict.fromkeys(('inetcpl.cpl', 'inetcplc.dll', 'wuaucpl.cpl'), 'security'),
    # XP's Help and Support Center: what PCHEALTH.INF installs in
    # PCHealth\HelpCtr\Binaries (its help topics in pchdt_w3.cab, of
    # Professional), the database engine of its service (esent.dll) and
    # esentutl.exe, which rebuilds the database's indexes with Wine's sort
    # keys, and the type library of Remote Desktop (rdshost.exe) that its
    # service's interfaces import (prefix/extras.py).
    **dict.fromkeys(('hcappres.dll', 'helpctr.exe', 'helphost.exe', 'helpsvc.exe', 'hscupd.exe', 'brpinfo.dll',
                     'notiflag.exe', 'pchshell.dll', 'pchsvc.dll', 'pchdt_w3.cab', 'esent.dll', 'esentutl.exe',
                     'rdshost.exe'), 'helpctr'),
    # XP's Network Connections folder (tray/netshell.dll) and what it loads
    # that Wine lacks: its tracing and wired 802.1X libraries, and the
    # wireless dialogs with their SP3 texts (prefix/extras.py).
    **dict.fromkeys(('rtutils.dll', 'dot3api.dll', 'wzcdlg.dll', 'xpsp3res.dll'), 'netcon'),
    # The programs of XP's default Start menu (MSN, Tour Windows XP, the Files
    # and Settings Transfer Wizard) and the icons of Set Program Access and
    # Defaults (prefix/extras.py).
    **dict.fromkeys(('msnsusii.exe', 'tourstart.exe', 'migwiz.exe', 'moricons.dll'), 'programs'),
    # XP's own programs in place of Wine's (prefix/extras.py xp_programs): the
    # Accessories, the games and the Accessibility tools of XP's Start menu,
    # Task Manager and Registry Editor, with the DLLs they need that Wine
    # lacks (or draws otherwise, cards.dll) beside them in system32.
    **dict.fromkeys(('calc.exe', 'notepad.exe', 'mspaint.exe', 'taskmgr.exe', 'cmd.exe', 'regedit.exe',
                     'charmap.exe', 'getuname.dll', 'sndrec32.exe', 'sndvol32.exe', 'winmine.exe', 'sol.exe',
                     'freecell.exe', 'mshearts.exe', 'spider.exe', 'cards.dll', 'mfc42u.dll', 'magnify.exe',
                     'mag_hook.dll', 'osk.exe', 'msswch.dll', 'accwiz.exe', 'mobsync.exe', 'mobsync.dll', 'cleanmgr.exe', 'mstsc.exe', 'mstscax.dll',
                     'wupdmgr.exe', 'rcimlby.exe', 'clb.dll', 'ulib.dll', 'hypertrm.dll', 'compatui.dll'),
                    'apps/system32'),
    'wordpad.exe': 'apps/programs/Windows NT/Accessories',
    'hypertrm.exe': 'apps/programs/Windows NT',
    # The INFs whose ProfileItems and syssetup.inf's Start menu sections make
    # XP's Start menu, read when it is made (prefix/startmenu.py).
    **dict.fromkeys(('syssetup.inf', 'accessor.inf', 'communic.inf', 'games.inf', 'pinball.inf',
                     'multimed.inf', 'optional.inf', 'shell.inf', 'tsoc.inf', 'wordpad.inf', 'msmsgs.inf',
                     'ieuinit.inf', 'netrass.inf', 'ie.inf'), 'apps/inf'),
    # XP's registry defaults (sounds, Explorer settings), read by prefix/extras.py.
    **dict.fromkeys(('hivedef.inf', 'hivesft.inf'), 'inf'),
    # XP's file types, their names and icons (prefix/extras.py file_types):
    # HIVECLS.INF's, the Compressed (zipped) Folders of zipfldr.dll, and
    # msiexec.exe, whose icon Windows Installer packages show.
    **dict.fromkeys(('hivecls.inf', 'zipfldr.dll', 'msiexec.exe'), 'filetypes'),
    # Image thumbnails and previews in folder windows (and Windows Picture and Fax Viewer).
    'shimgvw.dll': 'shell',
    # Printers and Faxes: XP's shell32 enumerates printers and follows them
    # through printui.dll, which Wine has as stubs (prefix/extras.py PRINTERS).
    'printui.dll': 'printers',
    # XP's System Properties (sysdm.cpl) and the pages it loads: Computer
    # Name (netid.dll), System Restore (srrstr.dll) and Remote (remotepg.dll)
    # (prefix/extras.py SYSDM).
    **dict.fromkeys(('sysdm.cpl', 'netid.dll', 'srrstr.dll', 'remotepg.dll'), 'sysdm'),
    # XP's Windows Classic theme for Display Properties (CLASSIC.THE_ on the CD).
    'classic.the': 'themes',
    **dict.fromkeys(('tahoma.ttf', 'tahomabd.ttf', 'msgothic.ttc', 'marlett.ttf',
                     'trebuc.ttf', 'trebucbd.ttf', 'trebucit.ttf', 'trebucbi.ttf', 'arial.ttf'), 'fonts'),
    'luna.msstyles': 'themes/Luna', 'luna.theme': 'themes',
    # XP's default wallpaper and its 23 default user account pictures (48x48).
    'bliss.jpg': 'web/wallpaper',
    # Only the cursors are read from user32.dll (greeter/cursors.py); it is kept out of
    # system32, whose DLLs prefix/create.py copies into the prefix.
    'user32.dll': 'resources',
    **dict.fromkeys(('airplane.bmp', 'astro.bmp', 'beach.bmp', 'butterfl.bmp', 'car.bmp', 'cat.bmp',
                     'chess.bmp', 'dirtbike.bmp', 'dog.bmp', 'drip.bmp', 'duck.bmp', 'fish.bmp',
                     'frog.bmp', 'guitar.bmp', 'horses.bmp', 'kick.bmp', 'liftoff.bmp', 'palmtree.bmp',
                     'pnkflowr.bmp', 'redflowr.bmp', 'skater.bmp', 'snwflake.bmp', 'soccer.bmp'), 'pictures'),
    'blue_ss.dll': 'themes/Luna/Shell/NormalColor',
    'home_ss.dll': 'themes/Luna/Shell/Homestead',
    'metal_ss.dll': 'themes/Luna/Shell/Metallic',
    # Windows Welcome (system32\oobe) at the session's first start (greeter/xp-oobe.py):
    # its intro video and music, the images of its screens, and the pages and
    # scripts its strings and styles are read from (no code of it runs).
    **dict.fromkeys(('intro.wmv', 'title.wma', 'newmark1.jpg', 'newmark8.jpg', 'newtop1.jpg', 'newtop8.jpg',
                     'newbtm1.jpg', 'newbtm8.jpg', 'mslogo.jpg', 'greenshd.gif', 'redshd.gif',
                     'backup.jpg', 'backover.jpg', 'backdown.jpg', 'backoff.jpg',
                     'nextup.jpg', 'nextover.jpg', 'nextdown.jpg', 'nextoff.jpg',
                     'msobshel.htm', 'oobeutil.js', 'oobestyl.css',
                     'welcome.htm', 'autoupdt.htm', 'ident2.htm', 'fini.htm'), 'oobe'),
}

# English XP's own fonts where Japanese XP uses MS UI Gothic: its Luna theme's
# Franklin Gothic Medium (Start panel), the Welcome screen's Arial with its
# bold and italic faces, Microsoft Sans Serif behind MS Shell Dlg, and the
# console's raster Terminal font (code page 437), which Wine does not have.
LANGUAGE_FILES = {
    'en': dict.fromkeys(('framd.ttf', 'framdit.ttf', 'arialbd.ttf', 'arialbi.ttf', 'ariali.ttf',
                         'micross.ttf', 'vgaoem.fon'), 'fonts'),
}

# Files that XP's setup installs under another name (LAYOUT.INF).
RENAMED = {'classic.the': 'Windows Classic.theme'}
# Groups a later version added; setup.sh adds them to an earlier extraction (--only).
ADDED_GROUPS = ('cpl', 'tray', 'ie', 'wmp', 'inf', 'shell', 'media', 'duser', 'intl', 'themes', 'srchasst',
                'wallpaper', 'wsc', 'programs', 'tour', 'accounts', 'netcon', 'appwiz', 'security', 'helpctr',
                'help', 'apps', 'filetypes', 'printers', 'sysdm', 'oobe', 'keyboard')


def setup_sources(paths):
    """{installed name: name on the medium} for the files that TXTSETUP.SIF
    installs under another name (Japanese XP's xpsp2res.dll is SPRT0411.DLL)."""
    sif = next((p for p in paths if p.name.lower() == 'txtsetup.sif'), None)
    if not sif:
        return {}
    sources, section = {}, None
    for raw in sif.read_text(encoding='latin-1').splitlines():
        line = raw.split(';', 1)[0].strip()
        if line.startswith('['):
            section = line.strip('[]').lower()
        elif section and section.startswith('sourcedisksfiles') and '=' in line:
            name, value = (part.strip().lower() for part in line.split('=', 1))
            fields = [f.strip() for f in value.split(',')]
            if len(fields) > 10 and fields[10] and fields[10] != name:
                sources.setdefault(fields[10], name)
    return sources


def media_files(paths):
    """XP's sounds: what LAYOUT.INF installs into %windir%\\Media (directory 26),
    with the names setup gives them ("Windows XP Start.wav")."""
    layout = next((p for p in paths if p.name.lower() == 'layout.inf'), None)
    if not layout:
        return {}
    strings, files, section = {}, {}, None
    for raw in layout.read_text(encoding='latin-1').splitlines():
        line = raw.split(';', 1)[0].strip()
        if line.startswith('['):
            section = line.strip('[]').lower()
        elif section == 'strings' and '=' in line:
            key, value = line.split('=', 1)
            strings[key.strip().lower()] = value.strip().strip('"')
        elif section and section.startswith('sourcedisksfiles') and '=' in line:
            name, value = (part.strip() for part in line.split('=', 1))
            fields = [f.strip() for f in value.split(',')]
            if name.lower().endswith('.wav') and len(fields) > 7 and fields[7] == '26':
                files[name.lower()] = fields[10] if len(fields) > 10 else ''
    renamed = {}
    for name, rename in files.items():
        key = rename.strip('%').lower()
        renamed[name] = strings.get(key, name) if rename.startswith('%') else (rename or name)
    return renamed


# Tour Windows XP: TXTSETUP.SIF's directories %windir%\Help\Tours\mmTour
# (the animated tour, a Flash projector and its movies) and htmlTour (the
# non-animated one, pages that tourstart.exe opens in the browser).
TOUR_DIRECTORIES = {'91': 'tour/mmTour', '92': 'tour/htmlTour'}


def tour_files(paths):
    """{name on the medium: (group, installed name)} of the Windows XP Tour."""
    sif = next((p for p in paths if p.name.lower() == 'txtsetup.sif'), None)
    if not sif:
        return {}
    files, section = {}, None
    for raw in sif.read_text(encoding='latin-1').splitlines():
        line = raw.split(';', 1)[0].strip()
        if line.startswith('['):
            section = line.strip('[]').lower()
        elif section and section.startswith('sourcedisksfiles') and '=' in line:
            name, value = (part.strip() for part in line.split('=', 1))
            fields = [f.strip() for f in value.split(',')]
            if len(fields) > 7 and fields[7] in TOUR_DIRECTORIES:
                files[name.lower()] = (TOUR_DIRECTORIES[fields[7]], fields[10] if len(fields) > 10 and fields[10] else name)
    return files


def pinball_files(paths, root):
    """{name on the medium: (group, installed name)} of 3D Pinball, which
    PINBALL.INF copies to Program Files\\Windows NT\\Pinball."""
    inf = next((p for p in paths if p.name.lower() in ('pinball.inf', 'pinball.in_')), None)
    if not inf:
        return {}
    return {source: ('apps/programs/Windows NT/Pinball', target.lower())
            for source, target in inf_copies(inf_text(inf, root), ('basecopyprogramfiles',)).items()}


def messenger_files(paths, root, output):
    """Windows Messenger, which MSMSGS.INF installs from MMSSETUP.CAB to
    Program Files\\Messenger: the cabinet's files, as (name, source) pairs."""
    cab = next((p for p in paths if p.name.lower() == 'mmssetup.cab'), None)
    if not cab:
        return []
    target = output / 'apps/programs/Messenger'
    target.mkdir(parents=True, exist_ok=True)
    run('cabextract', '-q', '-d', str(target), str(cab))
    return [(path, cab) for path in sorted(target.iterdir())]


def help_files(paths):
    """{name on the medium: (group, installed name)} of the compiled help
    files (.chm) that TXTSETUP.SIF installs in %windir%\\Help (directory
    21): the Help and Support Center's topics (update1w.chm is update1.chm)."""
    sif = next((p for p in paths if p.name.lower() == 'txtsetup.sif'), None)
    if not sif:
        return {}
    files, section = {}, None
    for raw in sif.read_text(encoding='latin-1').splitlines():
        line = raw.split(';', 1)[0].strip()
        if line.startswith('['):
            section = line.strip('[]').lower()
        elif section and section.startswith('sourcedisksfiles') and '=' in line:
            name, value = (part.strip() for part in line.split('=', 1))
            fields = [f.strip() for f in value.split(',')]
            if name.lower().endswith('.chm') and len(fields) > 7 and fields[7] == '21':
                files[name.lower()] = ('help', fields[10] if len(fields) > 10 and fields[10] else name)
    return files


def inf_text(path, root):
    """The text of the INF PATH, expanded first when the medium has it compressed (.IN_)."""
    if path.name.lower().endswith('_'):
        stage = root / 'inf-expanded'
        if stage.exists():
            shutil.rmtree(stage)
        stage.mkdir()
        run('cabextract', '-q', '-d', str(stage), str(path))
        path = next(stage.iterdir())
    raw = path.read_bytes()
    if raw[:2] in (b'\xff\xfe', b'\xfe\xff'):
        return raw.decode('utf-16')
    for encoding in ('utf-8', 'cp932'):
        try:
            return raw.decode(encoding)
        except UnicodeDecodeError:
            pass
    return raw.decode('latin-1')


def inf_copies(text, sections):
    """{source name: installed name} of the INF TEXT's CopyFiles SECTIONS,
    their %strings% resolved from its [Strings]."""
    strings, lines, section = {}, {}, None
    for raw in text.splitlines():
        line = raw.split(';', 1)[0].strip()
        if line.startswith('['):
            section = line.strip('[]').strip().lower()
        elif section == 'strings' and '=' in line:
            key, value = line.split('=', 1)
            strings[key.strip().lower()] = value.strip().strip('"')
        elif section in sections and line:
            lines.setdefault(section, []).append(line)
    copies = {}
    for section in sections:
        for line in lines.get(section, []):
            fields = [f.strip().strip('"') for f in line.split(',')]
            target, source = fields[0], (fields[1] if len(fields) > 1 and fields[1] else fields[0])
            if target.startswith('%') and target.endswith('%'):
                target = strings.get(target.strip('%').lower(), '')
            if target and source:
                copies[source.lower()] = target
    return copies


def wallpaper_files(paths, root):
    """XP's wallpapers, as its setup installs them: {name on the medium:
    (group, installed name)}. SHL_IMG.INF puts the JPEG ones in
    %windir%\\Web\\Wallpaper ("Red moon desert.jpg"), ACCESSOR.INF the
    bitmaps of the Desktop Wallpaper component in %windir% ("Coffee
    Bean.bmp"). Bliss is extracted on its own (FILES)."""
    lists = (('shl_img', ('wallpaper.copyfiles',), 'wallpaper/web'),
             ('accessor', ('deskpapercopyfilessys',), 'wallpaper/windows'))
    files = {}
    for stem, sections, group in lists:
        inf = next((p for p in paths if p.name.lower() in (stem + '.inf', stem + '.in_')), None)
        if not inf:
            continue
        for source, target in inf_copies(inf_text(inf, root), sections).items():
            if source != 'bliss.jpg':
                files[source] = (group, target)
    return files


def missing_groups(output):
    """The ADDED_GROUPS an extraction made before them lacks."""
    def has(path):
        return (output / path).exists()
    checks = {'cpl': 'cpl/desk.cpl', 'tray': 'tray/xpsp2res.dll', 'ie': 'ie/iexplore.exe',
              'wmp': 'wmp/wmplayer.exe', 'inf': 'inf/hivedef.inf', 'shell': 'shell/shimgvw.dll',
              'media': 'media', 'duser': 'system32/duser.dll', 'intl': 'intl/input.dll',
              'themes': 'themes/Windows Classic.theme', 'srchasst': 'srchasst/srchui.dll',
              'wallpaper': 'wallpaper', 'wsc': 'wsc/wscsvc.dll',
              'programs': 'programs/tourstart.exe', 'tour': 'tour/mmTour/tour.exe',
              'accounts': 'accounts/nusrmgr.cpl', 'netcon': 'netcon/wzcdlg.dll', 'appwiz': 'appwiz/appwiz.cpl',
              'security': 'security/wuaucpl.cpl', 'helpctr': 'helpctr/helpctr.exe', 'help': 'help',
              'apps': 'apps/programs/Messenger/msmsgs.exe', 'filetypes': 'filetypes/hivecls.inf',
              'printers': 'printers/printui.dll', 'sysdm': 'sysdm/sysdm.cpl', 'oobe': 'oobe/msobshel.htm',
              'keyboard': 'keyboard/main.cpl'}
    return [group for group in ADDED_GROUPS if not has(checks[group])]


def run(*args):
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL)

def extract(source, output, only=None):
    source, output = source.resolve(strict=True), output.resolve()
    if output.exists() and not only:
        raise SystemExit(f'Refusing existing destination: {output}')
    with tempfile.TemporaryDirectory(prefix='xp-extract-') as temp:
        root = Path(temp)
        if source.is_file():
            media = root / 'media'
            run('7z', 'x', '-y', f'-o{media}', str(source))
        else:
            media = source
        paths = sorted(p for p in media.rglob('*') if p.is_file())
        output.mkdir(parents=True, exist_ok=bool(only))
        manifest = []
        sources = setup_sources(paths)
        def copy(files):
            for name, group in files.items():
                copy_one(name, group)
        def copy_one(name, group):
            stem, extension = name.rsplit('.', 1)
            compressed = stem + '.' + extension[:2] + '_'
            matches = [p for p in paths if p.name.lower() in (name, compressed)]
            if not matches and name in sources:
                # The medium names it otherwise; it lands under NAME all the same.
                RENAMED.setdefault(sources[name], name)
                return copy_one(sources[name], group)
            # System32 controls v5 must not accidentally be replaced with SxS v6.
            # In a Windows directory, Windows Welcome's pages are the ones under oobe.
            matches.sort(key=lambda p: (group == 'oobe' and 'oobe' not in (s.lower() for s in p.parts),
                                        any(s.lower() in ('winsxs', 'asms') for s in p.parts), len(p.parts)))
            if not matches:
                return
            original = matches[0]
            target = output / group / ('shellstyle.dll' if name.endswith('_ss.dll') else RENAMED.get(name, name))
            target.parent.mkdir(parents=True, exist_ok=True)
            if original.name.lower() == compressed:
                stage = root / 'expanded'
                if stage.exists():
                    shutil.rmtree(stage)
                stage.mkdir()
                run('cabextract', '-q', '-d', str(stage), str(original))
                expanded = list(stage.iterdir())
                if len(expanded) != 1:
                    raise RuntimeError(f'Expected one expanded file: {original}')
                shutil.copyfile(expanded[0], target)
            else:
                shutil.copyfile(original, target)
            manifest.append({'name': str(target.relative_to(output)),
                             'source': str(original.relative_to(media)),
                             'sha256': hashlib.sha256(target.read_bytes()).hexdigest()})
        sounds = media_files(paths)
        RENAMED.update(sounds)
        wallpapers = wallpaper_files(paths, root)
        RENAMED.update({name: target for name, (_, target) in wallpapers.items()})
        tour = tour_files(paths)
        RENAMED.update({name: target for name, (_, target) in tour.items()})
        chm = help_files(paths)
        RENAMED.update({name: target.lower() for name, (_, target) in chm.items()})
        pinball = pinball_files(paths, root)
        RENAMED.update({name: target for name, (_, target) in pinball.items()})

        def copy_messenger():
            for path, cab in messenger_files(paths, root, output):
                manifest.append({'name': str(path.relative_to(output)), 'source': str(cab.relative_to(media)),
                                 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
        if only:
            # Add groups of files to assets extracted before they existed.
            groups = set(only)
            group_files = {name: group for name, group in FILES.items() if group.split('/')[0] in groups}
            if 'duser' in groups:
                group_files['duser.dll'] = 'system32'
            if 'media' in groups:
                group_files.update(dict.fromkeys(sounds, 'media'))
            if 'wallpaper' in groups:
                group_files.update({name: group for name, (group, _) in wallpapers.items()})
            if 'tour' in groups:
                group_files.update({name: group for name, (group, _) in tour.items()})
            if 'help' in groups:
                group_files.update({name: group for name, (group, _) in chm.items()})
            if 'apps' in groups:
                group_files.update({name: group for name, (group, _) in pinball.items()})
                copy_messenger()
            copy(group_files)
            listed = output / 'manifest.json'
            old = json.loads(listed.read_text()) if listed.is_file() else []
            names = {entry['name'] for entry in manifest}
            listed.write_text(json.dumps([e for e in old if e['name'] not in names] + manifest, indent=2) + '\n')
            print(f'Extracted {len(manifest)} files ({", ".join(only)}) to {output}')
            return
        copy(FILES)
        copy(dict.fromkeys(sounds, 'media'))
        copy({name: group for name, (group, _) in wallpapers.items()})
        copy({name: group for name, (group, _) in tour.items()})
        copy({name: group for name, (group, _) in chm.items()})
        copy({name: group for name, (group, _) in pinball.items()})
        copy_messenger()
        for original in paths:
            if original.name.lower() != 'shellstyle.dll':
                continue
            parent = original.parent.name.lower()
            if parent not in ('normalcolor', 'homestead', 'metallic'):
                continue
            target = output / 'themes/Luna/Shell' / parent / 'shellstyle.dll'
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original, target)
            manifest.append({'name': str(target.relative_to(output)),
                             'source': str(original.relative_to(media)),
                             'sha256': hashlib.sha256(target.read_bytes()).hexdigest()})
        # Keep SxS material separate, including its assembly identity manifest.
        for p in paths:
            if p.name.lower() not in ('comctl32.dll', 'comctl32.dl_', 'controls.man'):
                continue
            if not any(s.lower() in ('asms', 'winsxs') for s in p.parts):
                continue
            dest = output / 'sxs' / str(p.relative_to(media)).lower()
            if p.name.lower() == 'comctl32.dl_':
                dest = dest.with_name('comctl32.dll')
                stage = root / 'sxs-expanded'
                if stage.exists():
                    shutil.rmtree(stage)
                stage.mkdir()
                run('cabextract', '-q', '-d', str(stage), str(p))
                source_file = next(stage.iterdir())
            else:
                source_file = p
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source_file, dest)
            manifest.append({'name': str(dest.relative_to(output)), 'source': str(p.relative_to(media)),
                             'sha256': hashlib.sha256(dest.read_bytes()).hexdigest()})
        # Installed XP keeps manifests in WinSxS/Manifests, separate from DLLs.
        for original in paths:
            if original.suffix.lower() != '.manifest' or original.parent.name.lower() != 'manifests':
                continue
            if not any(part.lower() == 'winsxs' for part in original.parts):
                continue
            try:
                identity = ET.parse(original).getroot().find('{urn:schemas-microsoft-com:asm.v1}assemblyIdentity')
            except ET.ParseError:
                continue
            if identity is None or identity.get('name', '').lower() != 'microsoft.windows.common-controls':
                continue
            # Match the actual sibling assembly folder without inferring its hash.
            assembly_name = original.stem.lower()
            siblings = [folder for folder in original.parent.parent.iterdir()
                        if folder.is_dir() and folder.name.lower() == assembly_name]
            if not siblings:
                continue
            target = output / 'sxs' / str(siblings[0].relative_to(media)).lower() / 'controls.man'
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original, target)
            manifest.append({'name': str(target.relative_to(output)),
                             'source': str(original.relative_to(media)),
                             'sha256': hashlib.sha256(target.read_bytes()).hexdigest()})
        required = ['windows/explorer.exe'] + ['system32/' + n for n in ('shell32.dll', 'browseui.dll', 'shdocvw.dll', 'shlwapi.dll')]
        missing = [n for n in required if not (output / n).is_file()]
        if missing:
            raise SystemExit('Incomplete XP assets: ' + ', '.join(missing))
        language = media_language(output)
        copy(LANGUAGE_FILES.get(language, {}))
        # Read PE header architecture; no disassembly.
        pe = (output / 'windows/explorer.exe').read_bytes()
        offset = int.from_bytes(pe[60:64], 'little')
        if pe[offset:offset+6] != b'PE\0\0\x4c\x01':
            raise SystemExit('Only x86 XP media is supported')
        (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'Extracted {len(manifest)} files ({language}) to {output}')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, nargs='?', help='the XP ISO or Windows directory')
    parser.add_argument('output', type=Path)
    parser.add_argument('--only', metavar='GROUP', action='append', choices=ADDED_GROUPS,
                        help='add only this group of files to an existing extraction (repeatable)')
    parser.add_argument('--missing', action='store_true', help='print the groups an existing extraction lacks')
    args = parser.parse_args()
    if args.missing:
        print(' '.join(missing_groups(args.output.resolve())))
        raise SystemExit
    if not args.source:
        parser.error('the XP media is needed')
    extract(args.source, args.output, args.only)
