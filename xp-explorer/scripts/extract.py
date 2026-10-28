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
# The language of the extracted files, as win4wine's extraction finds it
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'extract'))
from media import language as media_language  # noqa: E402

FILES = {
    'explorer.exe': 'windows',
    **dict.fromkeys(('shell32.dll', 'browseui.dll', 'shdocvw.dll', 'shlwapi.dll',
                     'browselc.dll', 'shdoclc.dll', 'msgina.dll', 'comctl32.dll', 'linkinfo.dll', 'uxtheme.dll', 'shellstyle.dll', 'explorer.exe.manifest',
                     # Only the icons of My Documents and My Pictures are read from mydocs.dll.
                     'mydocs.dll',
                     # DirectUser: draws the folder task pane (common tasks).
                     'duser.dll'), 'system32'),
    # XP's Display Properties (Themes, Desktop, Screen Saver, Appearance, Settings).
    **dict.fromkeys(('desk.cpl', 'themeui.dll', 'deskadp.dll', 'deskmon.dll', 'logon.scr'), 'cpl'),
    # Icons only, for our notification area helper (helpers/xptray.c).
    **dict.fromkeys(('stobject.dll', 'powercfg.cpl', 'batmeter.dll', 'netshell.dll', 'sndvol32.exe', 'xpsp2res.dll'), 'tray'),
    # Internet Explorer 6 for the desktop's Internet icon, installed beside its
    # iexplore.exe (scripts/prefix.py), never over Wine's system DLLs.
    **dict.fromkeys(('iexplore.exe', 'mshtml.dll', 'mshtmled.dll', 'mshtmler.dll', 'mshtml.tlb', 'jscript.dll',
                     'vbscript.dll', 'urlmon.dll', 'wininet.dll', 'mlang.dll', 'msls31.dll', 'iepeers.dll',
                     'imgutil.dll', 'pngfilt.dll', 'msrating.dll', 'dxtrans.dll', 'dxtmsft.dll',
                     'iedkcs32.dll'), 'ie'),
    # Windows Media Player 9 (and 6.4, mplayer2.exe) with the Windows Media Format
    # runtime and decoders, installed beside wmplayer.exe (scripts/prefix.py).
    **dict.fromkeys(('wmplayer.exe', 'mplayer2.exe', 'wmp.dll', 'wmploc.dll', 'wmpcore.dll', 'wmpui.dll',
                     'wmpband.dll', 'wmpasf.dll', 'wmpdxm.dll', 'wmpns.dll', 'wmpcd.dll', 'wmpshell.dll',
                     'mpvis.dll', 'wmvcore.dll', 'wmasf.dll', 'wmnetmgr.dll', 'wmidx.dll', 'wmerror.dll',
                     'wmerrenu.dll', 'qasf.dll', 'msnetobj.dll', 'dxmasf.dll', 'wmadmod.dll', 'wmvdmod.dll',
                     'wmv8dmod.dll', 'wmspdmod.dll', 'mp43dmod.dll', 'msscp.dll', 'drmclien.dll',
                     'drmstor.dll', 'blackbox.dll', 'drmv2clt.dll', 'setup_wm.exe', 'unregmp2.exe',
                     'custsat.dll', 'wmp.inf'), 'wmp'),
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
    # XP's registry defaults (sounds, Explorer settings), read by scripts/prefix.py.
    **dict.fromkeys(('hivedef.inf', 'hivesft.inf'), 'inf'),
    # Image thumbnails and previews in folder windows (and Windows Picture and Fax Viewer).
    'shimgvw.dll': 'shell',
    # Read as data only: XP's system icons (win4wine patch 0141) and cursors.
    'user32.dll': 'resources',
    **dict.fromkeys(('tahoma.ttf', 'tahomabd.ttf', 'msgothic.ttc', 'marlett.ttf',
                     'trebuc.ttf', 'trebucbd.ttf', 'trebucit.ttf', 'trebucbi.ttf', 'arial.ttf'), 'fonts'),
    'luna.msstyles': 'themes/Luna', 'luna.theme': 'themes',
    # XP's Windows Classic theme for Display Properties (CLASSIC.THE_ on the CD).
    'classic.the': 'themes',
    # XP's default wallpaper and a default user account picture (48x48).
    'bliss.jpg': 'web/wallpaper',
    **dict.fromkeys(('chess.bmp', 'beach.bmp'), 'pictures'),
    'blue_ss.dll': 'themes/Luna/Shell/NormalColor',
    'home_ss.dll': 'themes/Luna/Shell/Homestead',
    'metal_ss.dll': 'themes/Luna/Shell/Metallic',
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
RENAMED = {'classic.the': 'Windows Classic.theme', 'luna.theme': 'Luna.theme'}

def media_files(paths):
    """XP's sounds: what LAYOUT.INF installs into %windir%\\Media (directory 26),
    with the names setup gives them (\"Windows XP Start.wav\")."""
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


def run(*args):
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def extract(source, output, only=None):
    source, output = source.resolve(strict=True), output.resolve()
    if output.exists() and not only:
        raise SystemExit(f'Refusing existing destination: {output}')
    with tempfile.TemporaryDirectory(prefix='xp-extract-') as temp:
        root = Path(temp)
        if source.is_file():
            media = root / 'media'
            media.mkdir()
            run('xorriso', '-osirrox', 'on', '-indev', str(source), '-extract', '/', str(media))
        else:
            media = source
        paths = sorted(p for p in media.rglob('*') if p.is_file())
        output.mkdir(parents=True, exist_ok=bool(only))
        manifest = []
        def copy(files):
            for name, group in files.items():
                copy_one(name, group)
        def copy_one(name, group):
            stem, extension = name.rsplit('.', 1)
            compressed = stem + '.' + extension[:2] + '_'
            matches = [p for p in paths if p.name.lower() in (name, compressed)]
            # System32 controls v5 must not accidentally be replaced with SxS v6.
            matches.sort(key=lambda p: (any(s.lower() in ('winsxs', 'asms') for s in p.parts), len(p.parts)))
            if not matches:
                return
            original = matches[0]
            target = output / group / ('shellstyle.dll' if name.endswith('_ss.dll')
                                       else RENAMED.get(name, name))
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
        if only:
            # Add one group of files to assets extracted before it existed.
            group_files = dict.fromkeys(sounds, 'media') if only == 'media' else \
                {name: group for name, group in FILES.items() if group.split('/')[0] == only}
            copy(group_files)
            listed = output / 'manifest.json'
            old = json.loads(listed.read_text()) if listed.is_file() else []
            names = {entry['name'] for entry in manifest}
            listed.write_text(json.dumps([e for e in old if e['name'] not in names] + manifest, indent=2) + '\n')
            print(f'Extracted {len(manifest)} files ({only}) to {output}')
            return
        copy(FILES)
        copy(dict.fromkeys(sounds, 'media'))
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
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--only', metavar='GROUP', help='add only this group (e.g. ie) to existing assets')
    args = parser.parse_args()
    extract(args.source, args.output, args.only)
