#!/usr/bin/env python3
"""The language of extracted XP files: ja (Japanese XP) or en (English XP).

It is the language of the version resource of the extraction's shell32.dll,
read from the PE resource directory (no code is read or run). XP in any other
language is set up as English XP; no other language has been tested.

  media.py [--warn] ASSETS    prints the language of an extract/extract.py output;
                              --warn also warns on stderr about an untested language
"""
from pathlib import Path
import sys

sys.path.append(str(Path(__file__).resolve().parents[1] / 'greeter'))
from xpres import Resources  # noqa: E402

RT_VERSION = 16
LANGUAGES = {0x411: 'ja', 0x409: 'en'}
# The Unix locale of each language's session and Wine processes.
LOCALES = {'ja': 'ja_JP.UTF-8', 'en': 'en_US.UTF-8'}


def resource_languages(path, rtype):
    """The LANGIDs of the resources of type RTYPE in the PE file PATH."""
    return Resources(path).languages(rtype)


def tested(assets):
    """The tested language (ja or en) of the extracted XP files in ASSETS, or None."""
    for lang in resource_languages(Path(assets) / 'system32/shell32.dll', RT_VERSION):
        if lang in LANGUAGES:
            return LANGUAGES[lang]
    return None


def language(assets):
    """ja or en for the extracted XP files in ASSETS; en for XP in other languages."""
    return tested(assets) or 'en'


def warning(assets):
    """A warning when the XP files in ASSETS are in an untested language, or None."""
    if tested(assets):
        return None
    shell32 = Path(assets) / 'system32/shell32.dll'
    found = ', '.join(f'{lang:#06x}' for lang in resource_languages(shell32, RT_VERSION)) or 'none'
    return (f'Warning: this XP is neither Japanese nor English (language of {shell32}: {found}).\n'
            'It is set up as English XP. XP in other languages has not been tested at all.')


if __name__ == '__main__':
    args = sys.argv[1:]
    warn = args[:1] == ['--warn']
    if len(args) != 1 + warn:
        raise SystemExit(__doc__.strip())
    if warn and (text := warning(args[-1])):
        print(text, file=sys.stderr)
    print(language(args[-1]))
