"""XP's Start menu shortcuts, as XP's setup makes them, from the media's INFs.

syssetup.inf lists the shortcuts of its groups (Accessories, Accessibility,
System Tools, Communications) and of the Start menu's top
([StartMenuCommon]: Windows Update, Windows Catalog, Set Program Access and
Defaults), one per line: NAME = FILE, COMMAND LINE, ICON FILE, ICON INDEX,
INFOTIP. The optional components' INFs make theirs with ProfileItems
sections (Name, CmdLine = DIRID,SUBDIR,FILE, SubDir, WorkingDir, IconPath,
IconIndex, InfoTip). Names and folders are the media's own, in its language.

items(INF_DIRECTORY) gives them as dicts: folder (relative to the common
Programs folder, or None for the Start menu's top), name, target (a Windows
path, or a URL), arguments, directory, icon (FILE or FILE,INDEX), infotip and
display: the shortcut's localized name (@FILE,-ID), which XP shows from the
folder's desktop.ini (LocalizedFileNames) in place of the file's name.
"""
import re
from pathlib import Path

# Directory IDs of the INFs (setupapi's DIRID_*), for the 32-bit XP shell.
DIRIDS = {'10': r'C:\windows', '11': r'C:\windows\system32', '17': r'C:\windows\inf', '18': r'C:\windows\help',
          '16422': r'C:\Program Files', '16427': r'C:\Program Files\Common Files'}
# The install sections of the optional components that XP Professional
# installs, whose ProfileItems make Start menu shortcuts.
COMPONENTS = (
    ('accessor.inf', ('calc', 'charmap', 'paint')),
    ('communic.inf', ('hypertrm',)),
    ('games.inf', ('freecell', 'hearts', 'minesweeper', 'solitaire', 'spider')),
    ('pinball.inf', ('pinball',)),
    ('multimed.inf', ('rec', 'vol')),
    ('optional.inf', ('accessopt',)),
    ('shell.inf', ('defaultinstall',)),
    ('tsoc.inf', ('terminalservices.freshinstall.pro',)),
    ('wordpad.inf', ('mswordpad',)),
    ('msmsgs.inf', ('showicons',)),
    ('ieuinit.inf', ('ieaccess.install.winxp',)),
    ('netrass.inf', ('ndi-rascli',)),
    ('ie.inf', ('defaultinstall',)),
)
# syssetup.inf's sections of shortcuts, by their group in [StartMenuGroups].
GROUPS = ('Accessories', 'Accessibility', 'SystemTools', 'Communications', 'Entertainment', 'StartMenuCommon',
          'StartMenuItems')
# Profile item flags (setupapi's FLG_PROFITEM_*): delete, a group, another CSIDL.
DELETE, GROUP, CSIDL = 0x2, 0x4, 0x8


def read(path):
    raw = Path(path).read_bytes()
    if raw[:2] in (b'\xff\xfe', b'\xfe\xff'):
        return raw.decode('utf-16')
    return raw.decode('latin-1')


def fields(line):
    """The comma-separated fields of an INF line, quotes removed."""
    out, current, quoted = [], '', False
    for c in line:
        if c == '"':
            quoted = not quoted
        elif c == ',' and not quoted:
            out.append(current.strip())
            current = ''
        else:
            current += c
    out.append(current.strip())
    return out


class Inf:
    def __init__(self, path):
        self.sections, section = {}, None
        for raw in read(path).splitlines():
            line = raw.strip()
            if not line or line.startswith(';'):
                continue
            if line.startswith('[') and ']' in line:
                section = line[1:line.index(']')].strip().lower()
                self.sections.setdefault(section, [])
            elif section is not None:
                self.sections[section].append(line)
        self.strings = {}
        for line in self.sections.get('strings', []):
            if '=' in line:
                key, value = line.split('=', 1)
                self.strings[key.strip().lower()] = value.split(';')[0].strip().strip('"')

    def expand(self, value):
        value = re.sub(r'%([^%]+)%', lambda m: self.strings.get(m.group(1).lower(), m.group(0)), value)
        return value.replace('%%', '%')

    def entries(self, section):
        """KEY = VALUE lines of SECTION, values expanded; a key may repeat."""
        out = []
        for line in self.sections.get(section.lower(), []):
            if '=' in line:
                key, value = line.split('=', 1)
                out.append((self.expand(key.strip()), self.expand(value.split(';')[0].strip()) if not
                            value.strip().startswith('"') else self.expand(value.strip())))
        return out


def split_command(command):
    """(program, arguments) of a command line."""
    command = command.strip()
    if command.startswith('"'):
        end = command.find('"', 1)
        return command[1:end], command[end + 1:].strip()
    program, _, arguments = command.partition(' ')
    return program, arguments.strip()


def in_directory(dirid, subdir, name):
    base = DIRIDS.get(dirid.strip(), '')
    parts = [p for p in (base, subdir.strip().strip('"'), name) if p]
    return '\\'.join(parts)


def program_path(program, directory):
    r"""A program named without a directory (cmd.exe, rundll32) is in DIRECTORY;
    %SystemRoot% is C:\windows."""
    program = re.sub(r'(?i)%systemroot%', r'C:\\windows', program)
    if '\\' not in program:
        program = directory + '\\' + program
    if not re.search(r'\.[a-z]{3}$', program, re.IGNORECASE):
        program += '.exe'
    return program


def syssetup_items(inf):
    groups = {}
    for key, value in inf.entries('StartMenuGroups'):
        groups[key.lower()] = fields(value)[0]
    items = []
    for group in GROUPS:
        if group == 'StartMenuItems':
            folder = ''
        elif group == 'StartMenuCommon':
            folder = None
        else:
            folder = groups.get(group.lower(), inf.strings.get(group.lower()))
            if group == 'Entertainment':
                folder = inf.strings.get('accessories_entertainment', folder)
        for name, value in inf.entries(group):
            file, command, icon, index, infotip, display_file, display_id = (fields(value) + [''] * 7)[:7]
            if not command:
                continue
            item = dict(folder=folder, name=name.strip().strip('"'), arguments='', directory='', infotip=infotip,
                        display=f'@{display_file},-{display_id}' if display_file and display_id else '')
            if re.match(r'(?i)(https?|hcp)://', command):
                item['target'] = command
            else:
                program, arguments = split_command(command)
                item['target'] = program_path(program, DIRIDS['11'])
                item['arguments'] = arguments
                item['directory'] = DIRIDS['11']
            if icon:
                icon = program_path(icon, DIRIDS['11']) if not re.search(r'\.(dll|exe|cpl)$', icon, re.I) or \
                    '\\' not in icon else re.sub(r'(?i)%systemroot%', r'C:\\windows', icon)
                item['icon'] = f'{icon},{index}' if index and index != '0' else icon
            items.append(item)
    return items


def profile_items(inf, sections):
    items = []
    for install in sections:
        for key, value in inf.entries(install):
            if key.lower() != 'profileitems':
                continue
            for section in (s.strip() for s in value.split(',') if s.strip()):
                entry = {}
                for k, v in inf.entries(section):
                    entry.setdefault(k.lower(), v)
                if 'name' not in entry or 'cmdline' not in entry:
                    continue
                name, *flags = fields(entry['name'])
                flag = int(flags[0], 0) if flags and flags[0] else 0
                if flag & (DELETE | GROUP | CSIDL):
                    continue
                folder = entry.get('subdir', '').strip('"')
                # Remote Desktop Connection only, not the server's tools.
                if folder.lower() == inf.strings.get('admintools', 'administrative tools').lower() or \
                        'administrative tools' in folder.lower():
                    continue
                dirid, subdir, command = (fields(entry['cmdline']) + [''] * 3)[:3]
                program, arguments = split_command(command)
                base = in_directory(dirid, subdir, '')
                display_file, display_id = (fields(entry.get('displayresource', '')) + [''] * 2)[:2]
                item = dict(folder=folder, name=name.strip('"'), target=program_path(program, base),
                            arguments=arguments, infotip=entry.get('infotip', '').strip('"'), directory='',
                            display=f'@{display_file},-{display_id}' if display_file and display_id else '')
                if 'workingdir' in entry:
                    work_dirid, work_subdir = (fields(entry['workingdir']) + [''] * 2)[:2]
                    item['directory'] = in_directory(work_dirid or dirid, work_subdir, '')
                if 'iconpath' in entry:
                    icon_dirid, icon_subdir, icon_file = (fields(entry['iconpath']) + [''] * 3)[:3]
                    icon = in_directory(icon_dirid, icon_subdir, icon_file)
                    index = entry.get('iconindex', '0')
                    item['icon'] = f'{icon},{index}' if index and index != '0' else icon
                items.append(item)
    return items


def items(directory):
    directory = Path(directory)
    found = []
    if (directory / 'syssetup.inf').is_file():
        found += syssetup_items(Inf(directory / 'syssetup.inf'))
    for name, sections in COMPONENTS:
        if (directory / name).is_file():
            found += profile_items(Inf(directory / name), sections)
    # One shortcut per name and folder (the first INF wins).
    seen, result = set(), []
    for item in found:
        key = (item['folder'], item['name'].lower())
        if key not in seen:
            seen.add(key)
            result.append(item)
    return result


if __name__ == '__main__':
    import sys
    for item in items(sys.argv[1]):
        print(item)
