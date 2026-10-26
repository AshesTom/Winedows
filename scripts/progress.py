#!/usr/bin/env python3
"""Run a build, keep its whole output in a log and show only how far it is.

    scripts/progress.py [--append] LOG NAME -- COMMAND [ARG...]

On a terminal, a status line (step, bar, count, elapsed time and an estimate of
the time left) and the build's last few lines are redrawn in place; elsewhere a
line is printed for each step and every 10 percent. The exit status is the
command's. The build scripts announce their steps with lines starting with
">>> "; ">>> Compiling N files" gives the number of compiler runs to count
(WIN4WINE_PROGRESS is set for them, so they count only then), and ninja's own
"[n/m]" counts are used as they are. A step announced as ">>> (n/m) STEP" is
the n-th of m (prefix/extras.py). --append adds to the log instead of
replacing it.
"""
import os
import re
import shutil
import signal
import subprocess
import sys
import time

TAIL = 4                        # lines of the build's output under the status line
STEP = re.compile(r'^>>> (.*)')
TOTAL = re.compile(r'^Compiling (\d+) files')
COUNTED = re.compile(r'^\((\d+)/(\d+)\) (.*)')
NINJA = re.compile(r'^\[(\d+)/(\d+)\] (.*)')
COMPILE = re.compile(r' -c -o (\S+)')
OUTPUT = re.compile(r'^(\S+)(?: .*)? -o (\S+)')


def clock(seconds):
    seconds = int(seconds)
    if seconds >= 3600:
        return f'{seconds // 3600}:{seconds // 60 % 60:02}:{seconds % 60:02}'
    return f'{seconds // 60}:{seconds % 60:02}'


def short(line):
    """A compiler run as the file it makes, another tool's run as the tool and
    the file it makes; other lines as they are."""
    match = COMPILE.search(line)
    if match:
        return 'compile ' + match.group(1)
    match = OUTPUT.match(line)
    if match:
        return f'{os.path.basename(match.group(1))} {match.group(2)}'
    return line


class Status:
    def __init__(self, name, tty):
        self.name, self.tty = name, tty
        self.step, self.done, self.total, self.exact = 'Starting', 0, 0, False
        self.tail = []
        self.continued = ''     # a command that make printed over several lines
        self.start = self.step_start = time.monotonic()
        self.drawn = 0          # lines on the terminal to redraw
        self.shown = -1         # tens of percent printed (not a terminal)
        self.last = 0.0

    def line(self, text):
        if text.endswith('\\'):
            self.continued += text[:-1]
            return
        text, self.continued = self.continued + text, ''
        match = STEP.match(text)
        if match:
            self.set_step(match.group(1))
            return
        match = NINJA.match(text)
        if match:
            self.done, self.total, self.exact = int(match.group(1)), int(match.group(2)), True
            text = match.group(3)
        elif self.total and ' -c -o ' in text:
            self.done += 1
        if text.strip():
            self.tail = (self.tail + [short(text.strip())])[-TAIL:]
        self.update()

    def set_step(self, step):
        match, counted = TOTAL.match(step), COUNTED.match(step)
        self.step, self.step_start = step, time.monotonic()
        self.done, self.total, self.shown = 0, int(match.group(1)) if match else 0, -1
        self.exact = False
        if counted:
            self.step, self.exact = counted.group(3), True
            self.done, self.total = int(counted.group(1)) - 1, int(counted.group(2))
            self.shown = self.done * 100 // self.total // 10
        if not self.tty:
            percent = f' ({self.percent()}%)' if counted else ''
            print(f'{self.name}: {self.step}{percent}', flush=True)
        self.update(force=True)

    def percent(self):
        if not self.total:
            return None
        # make -n does not list every run exactly: hold at 99 until the step ends.
        return min(100 if self.exact else 99, self.done * 100 // self.total)

    def update(self, force=False):
        now = time.monotonic()
        if not self.tty:
            percent = self.percent()
            if percent is not None and percent // 10 > self.shown:
                self.shown = percent // 10
                if percent:
                    print(f'{self.name}: {percent}% ({self.done}/{self.total}), {clock(now - self.start)}',
                          flush=True)
            return
        if not force and now - self.last < 0.2:
            return
        self.last = now
        width = shutil.get_terminal_size((80, 24)).columns
        head = f'{self.name}: {self.step}'
        percent = self.percent()
        if percent is not None:
            bar = 20
            filled = percent * bar // 100
            head += f'  [{"#" * filled}{"." * (bar - filled)}] {percent:3}%  {self.done}/{self.total}'
            elapsed = now - self.step_start
            if self.done >= self.total // 20 and self.done:
                head += f'  ~{clock(elapsed * (self.total - self.done) / self.done)} left'
        head += f'  ({clock(now - self.start)})'
        lines = [head[:width - 1]] + ['  ' + text[:width - 3] for text in self.tail]
        out = ['\x1b[1A' * (self.drawn - 1) + '\r' if self.drawn else '']
        for index, text in enumerate(lines):
            style = '\x1b[1m' if index == 0 else '\x1b[2m'
            out.append(f'\x1b[2K{style}{text}\x1b[0m' + ('\n' if index < len(lines) - 1 else ''))
        # Clear what is left of a taller earlier drawing
        for _ in range(self.drawn - len(lines)):
            out.append('\n\x1b[2K')
        if self.drawn > len(lines):
            out.append('\x1b[1A' * (self.drawn - len(lines)))
        sys.stdout.write(''.join(out))
        sys.stdout.flush()
        self.drawn = len(lines)

    def finish(self, status):
        if self.tty and self.drawn:
            sys.stdout.write('\x1b[1A' * (self.drawn - 1) + '\r\x1b[J')
        result = 'done' if status == 0 else f'failed ({status})'
        print(f'{self.name}: {result} in {clock(time.monotonic() - self.start)}', flush=True)


def main():
    args = sys.argv[1:]
    append = args[:1] == ['--append']
    args = args[append:]
    if len(args) < 4 or args[2] != '--':
        sys.exit(__doc__.strip().splitlines()[2].strip())
    log, name, command = args[0], args[1], args[3:]
    tty = sys.stdout.isatty() and os.environ.get('TERM') not in (None, '', 'dumb')
    status = Status(name, tty)
    env = dict(os.environ, WIN4WINE_PROGRESS='1')
    with open(log, 'ab' if append else 'wb') as out:
        child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 stdin=subprocess.DEVNULL, env=env)
        # The terminal's Ctrl+C reaches the build too; wait for it to stop.
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        pending = b''
        while chunk := child.stdout.read1(65536):
            out.write(chunk)
            pending += chunk
            # git's progress ends its lines with \r
            *lines, pending = re.split(rb'\r\n|\r|\n', pending)
            for line in lines:
                status.line(line.decode('utf-8', 'replace'))
        if pending:
            status.line(pending.decode('utf-8', 'replace'))
        code = child.wait()
    status.finish(code)
    sys.exit(code if code >= 0 else 128 - code)


if __name__ == '__main__':
    main()
