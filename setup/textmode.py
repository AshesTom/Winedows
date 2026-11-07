#!/usr/bin/env python3
"""The installer's first phase as XP's text-mode setup draws it, in the terminal.

XP's setupdd.sys shows its screens on the 80 x 25 VGA text screen, grey on blue, with
the product's name underlined at the top and the keys in a grey status bar at the
bottom; its words are usetup.exe's message table. Here: Welcome to Setup (0x2329,
without the repair line), the licence (eula.txt, 0x2afb, F8 to agree), then the copy
gauge (0x2379, 0x2fc1) while native-shell/extract.py takes the user's XP files and the
programs are built, then "This portion of Setup has completed successfully" (0x232f)
and its countdown (0x300c) before the graphical phase.

  textmode.py --media ISO --setup FOLDER --log FILE
Exit status: 0 done, 3 the user quit (F3, or ESC on the licence), 1 a step failed.
"""
import argparse
import os
from pathlib import Path
import re
import select
import shutil
import subprocess
import sys
import termios
import time
import tty

import xpsetup

ROOT = Path(__file__).resolve().parents[1]
# The VGA text colours setupdd.sys uses (ANSI SGR): grey on blue, the title and the
# intense lines bright white, the status bar black on grey, the gauge's bar yellow.
BODY, INTENSE, STATUS, GAUGE = '\033[0;37;44m', '\033[1;97;44m', '\033[0;30;47m', '\033[0;30;103m'
TITLE, WELCOME, LICENCE_TITLE = 0x2af8, 0x2329, 0x2afb
COPY_WAIT, COPYING_GAUGE, COPYING, COMPLETED = 0x2379, 0x2fc1, 0x2732, 0x232f
WILL_CONTINUE, RESTART_KEY, REBOOT_IN = 0x2394, 0x2393, 0x300c
ENTER, F3, F8, ESC, PAGE_DOWN, PAGE_UP, RESTART = 0x2712, 0x2711, 0x2769, 0x276a, 0x276b, 0x276c, 0x271e
LEFT = 3                        # the body's margin, in columns
COUNTDOWN = 15


class Screen:
    def __init__(self, text):
        self.text = text
        self.fd = sys.stdin.fileno()
        self.saved = termios.tcgetattr(self.fd)
        tty.setcbreak(self.fd)
        sys.stdout.write('\033[?1049h\033[?25l')
        self.size()

    def size(self):
        self.columns, self.rows = shutil.get_terminal_size((80, 25))

    def close(self):
        termios.tcsetattr(self.fd, termios.TCSADRAIN, self.saved)
        sys.stdout.write('\033[0m\033[2J\033[?25h\033[?1049l')
        sys.stdout.flush()

    def message(self, number, *args):
        text = self.text.get(number, '')
        for i, arg in enumerate(args, 1):
            text = re.sub(rf'%{i}(![^!]*!)?', str(arg), text)
        return text.replace('%%d', '%d')

    def at(self, row, column, text, colour=BODY):
        sys.stdout.write(f'\033[{row};{column}H{colour}{text}')

    def page(self, status):
        """A blank screen with the title and the status bar."""
        self.size()
        sys.stdout.write(BODY + '\033[2J')
        title = xpsetup.key_label(self.message(TITLE))
        self.at(1, 2, title, INTENSE)
        self.at(2, 1, '═' * (len(title) + 2), INTENSE)
        self.at(self.rows, 1, ' ' * self.columns, STATUS)
        self.at(self.rows, 3, status[:self.columns - 4], STATUS)

    def body(self, text, top=4):
        """A message's lines from TOP; those after %%I are intense, as setupdd.sys shows them."""
        row = top
        for line in text.split('\n'):
            colour = BODY
            if line.startswith('%%I'):
                line, colour = line[3:], INTENSE
            self.at(row, LEFT + 1, line[:self.columns - LEFT - 1], colour)
            row += 1
        return row

    def keys(self, *numbers):
        return '  '.join(xpsetup.key_label(self.message(n)) for n in numbers)

    def read_key(self, timeout=None):
        """'enter', 'esc', 'f3', 'f8', 'pgdn', 'pgup', 'r' or None at the timeout."""
        sys.stdout.flush()
        if not select.select([self.fd], [], [], timeout)[0]:
            return None
        data = os.read(self.fd, 16)
        while select.select([self.fd], [], [], 0.03)[0]:
            data += os.read(self.fd, 16)
        return {b'\n': 'enter', b'\r': 'enter', b'\x1b': 'esc', b'\x1bOR': 'f3', b'\x1b[13~': 'f3',
                b'\x1b[19~': 'f8', b'\x1b[6~': 'pgdn', b'\x1b[5~': 'pgup', b' ': 'pgdn'}.get(
            data, data.decode(errors='replace').lower()[:1])

    def gauge(self, title, percent, status):
        """setupdd.sys's gauge: its title over a bar, centred, with the status line under it."""
        width = min(66, self.columns - 8)
        left, top = (self.columns - width) // 2 + 1, self.rows // 2 - 1
        self.at(top, left, f'{title:^{width}}'[:width], BODY)
        filled = width * max(0, min(100, percent)) // 100
        label = f'{percent:3d}%'
        bar = f'{label:^{width}}'
        self.at(top + 2, left, bar[:filled], GAUGE)
        self.at(top + 2, left + filled, bar[filled:], '\033[0;97;44m')
        self.at(top + 1, left - 1, '┌' + '─' * width + '┐', BODY)
        self.at(top + 2, left - 1, '│', BODY)
        self.at(top + 2, left + width, '│', BODY)
        self.at(top + 3, left - 1, '└' + '─' * width + '┘', BODY)
        self.at(self.rows, 1, ' ' * self.columns, STATUS)
        self.at(self.rows, 3, status[:self.columns - 4], STATUS)
        sys.stdout.flush()


def welcome(screen):
    # 0x2329 without its repair line: there is nothing to repair here
    text = re.sub(r'\n[^\n]*press R\.\n', '\n', screen.message(WELCOME))
    while True:
        screen.page(screen.keys(ENTER, F3))
        screen.body(text)
        key = screen.read_key()
        if key == 'enter':
            return True
        if key == 'f3':
            return False


def licence(screen, eula):
    lines = []
    for paragraph in eula.split('\n'):
        while len(paragraph) > 72:
            cut = paragraph.rfind(' ', 0, 72)
            cut = cut if cut > 0 else 72
            lines.append(paragraph[:cut])
            paragraph = paragraph[cut:].lstrip()
        lines.append(paragraph)
    first = 0
    while True:
        height = screen.rows - 6
        screen.page(screen.keys(F8, ESC, PAGE_DOWN) if first + height < len(lines) else screen.keys(F8, ESC, PAGE_UP))
        screen.at(1, 2, ' ' * 60)
        title = xpsetup.key_label(screen.message(LICENCE_TITLE))
        screen.at(1, 2, title, INTENSE)
        screen.at(2, 1, '═' * (len(title) + 2), INTENSE)
        for i, line in enumerate(lines[first:first + height]):
            screen.at(4 + i, LEFT + 1, line[:screen.columns - LEFT - 1])
        key = screen.read_key()
        if key == 'f8':
            return True
        if key == 'esc':
            return False
        if key == 'pgdn' and first + height < len(lines):
            first += height
        elif key == 'pgup':
            first = max(0, first - height)


def copy(screen, args):
    """The copy gauge over the work: the XP files, then the programs (0-10, 10-85, 85-100)."""
    title = xpsetup.key_label(screen.message(COPYING_GAUGE))
    copying = xpsetup.key_label(screen.message(COPYING))
    log = open(args.log, 'a')

    def run(command, low, high, env=None):
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                   env=env, bufsize=1)
        percent = low
        for line in process.stdout:
            log.write(line)
            done = re.match(r'\[\s*(\d+)%\]', line) or re.match(r'\[(\d+)/(\d+)\]', line)
            if done:
                fraction = int(done[1]) / 100 if done.lastindex == 1 else int(done[1]) / max(1, int(done[2]))
                percent = low + int((high - low) * fraction)
            name = re.search(r'([\w.+-]+\.(?:cpp|c|ttf|dll|exe|cpl|wav|jpg|bmp|msstyles|ocx|png))\b', line)
            screen.gauge(title, percent, f'{copying} {name[1]}' if name else copying)
        return process.wait() == 0

    assets = ROOT / '.local/native-assets'
    screen.page('')
    screen.body(screen.message(COPY_WAIT))
    screen.gauge(title, 0, copying)
    if not run([sys.executable, str(ROOT / 'native-shell/extract.py'), args.media, str(assets)], 0, 10):
        return False
    env = dict(os.environ, NINJA_STATUS='[%f/%t] ')
    if not run([str(ROOT / 'native-shell/build.sh')], 10, 85, env):
        return False
    # XP's look for the session's other programs; the session runs without it
    if (ROOT / 'qt-style/build.sh').is_file():
        run([str(ROOT / 'qt-style/build.sh')], 85, 100, env)
    screen.gauge(title, 100, copying)
    return True


def completed(screen):
    """0x232f, then the countdown setupdd.sys shows before it restarts; ENTER goes on at once."""
    text = '\n'.join((screen.message(COMPLETED).rstrip('\n'), '', screen.message(WILL_CONTINUE).rstrip('\n'),
                      '', screen.message(RESTART_KEY).rstrip('\n')))
    for left in range(COUNTDOWN, 0, -1):
        screen.page(screen.keys(RESTART))
        screen.body(text)
        screen.gauge(xpsetup.key_label(screen.message(REBOOT_IN)) % left, 100 * left // COUNTDOWN, '')
        screen.at(screen.rows, 1, ' ' * screen.columns, STATUS)
        screen.at(screen.rows, 3, screen.keys(RESTART), STATUS)
        if screen.read_key(1) == 'enter':
            return


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--media', required=True, help='the Windows XP ISO')
    parser.add_argument('--setup', required=True, type=Path, help="where XP's setup files are kept")
    parser.add_argument('--log', required=True)
    args = parser.parse_args()
    folder = xpsetup.media(args.media, args.setup)
    text = xpsetup.messages(folder / 'usetup.exe')
    eula = (folder / 'eula.txt').read_bytes().decode('cp1252', errors='replace').replace('\r\n', '\n')
    screen = Screen(text)
    try:
        if not welcome(screen) or not licence(screen, eula):
            return 3
        if not copy(screen, args):
            return 1
        completed(screen)
        return 0
    finally:
        screen.close()


if __name__ == '__main__':
    sys.exit(main())
