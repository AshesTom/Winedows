#!/usr/bin/env python3
"""win4wine-wine [--shell win7] [WINE ARGUMENTS...]

Run "wine" as the session runs it: in the shell's prefix, with the session's
Wine and its opt-ins (WIN4WINE_EXTERNAL_SHELL and the rest, session/wineenv.py),
so that a program started from outside the session, for example over ssh or
from another desktop, looks and behaves as one started from the shell.
Without them Wine runs the program with its own look (no Windows 7 Basic or
Luna frames), and plain "wine" uses ~/.wine and the first Wine in PATH.

The shell is the one set up with setup.sh (~/.config/win4wine); with more
than one, --shell chooses. In a terminal of the session, plain "wine" has all
this already; this runs it unchanged there.
"""
import json
import os
import sys

from wineenv import CONFIGS, SHELLS, config_dir, environment


def fail(message):
    print(f'win4wine-wine: {message}', file=sys.stderr)
    return 2


def main(argv):
    shell = None
    if argv and argv[0] in ('-h', '--help'):
        print(__doc__.strip())
        return 0
    if argv and argv[0].startswith('--shell'):
        if argv[0] == '--shell':
            if len(argv) < 2:
                return fail('--shell needs one of ' + ', '.join(SHELLS))
            shell, argv = argv[1], argv[2:]
        elif argv[0].startswith('--shell='):
            shell, argv = argv[0].split('=', 1)[1], argv[1:]
        if shell not in SHELLS:
            return fail(f'unknown shell {shell!r}; one of ' + ', '.join(SHELLS))

    inside = os.environ.get('WIN4WINE_EXTERNAL_SHELL') is not None and os.environ.get('WINEPREFIX')
    if inside and shell is None:
        # Started from the session: its environment is the session's own.
        os.execvp('wine', ['wine', *argv])

    found = {name: config_dir() / file for name, file in CONFIGS.items() if (config_dir() / file).is_file()}
    if shell is None:
        if not found:
            return fail(f'no session set up in {config_dir()} (scripts/setup.sh)')
        if len(found) > 1:
            return fail('more than one shell is set up; choose with --shell ' + '|'.join(found))
        shell = next(iter(found))
    elif shell not in found:
        return fail(f'the {shell} shell is not set up ({config_dir() / CONFIGS[shell]})')

    config = json.loads(found[shell].read_text())
    if config.get('shell', 'win7') != shell:
        return fail(f'{found[shell]} is for the {config.get("shell", "win7")} shell')
    # Another shell's session (--shell from its terminal) leaves none of its own opt-ins.
    base = {k: v for k, v in os.environ.items() if not k.startswith('WIN4WINE_')}
    try:
        env = environment(base, config, SHELLS[shell])
    except OSError as e:
        return fail(e)
    if not os.path.isfile(os.path.join(env['WINEPREFIX'], SHELLS[shell]['marker'])):
        return fail(f'{env["WINEPREFIX"]} is not the {shell} shell\'s prefix')
    # Wine's fixmes stay out of the terminal, as in the session's logs.
    env.setdefault('WINEDEBUG', 'fixme-all')
    wine = os.path.realpath(config['wine'])
    os.execve(wine, [wine, *argv], env)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
