#!/usr/bin/env python3
"""Check greeter/oobe.py's Windows Welcome pages: drawing, keyboard and the names' rules.

python3 tests/oobe_pages.py ASSETS   (extract/extract.py's output, with its 'oobe' group)

Each page is drawn at 800x600, 1024x768, 1280x1024 and 1920x1080: its title
must show (white over the background), its Back and Next buttons must be in
the bottom band and inside the screen, and its controls must not overlap.
Then the pages are gone through with the keyboard as on XP, and the names of
Who will use this computer? are checked against XP's rules (Identities2).
Needs python3-gi, gir1.2-pango-1.0, python3-gi-cairo and GdkPixbuf.
"""
import argparse
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'greeter'))
sys.path.insert(0, str(ROOT / 'extract'))

SIZES = ((800, 600), (1024, 768), (1280, 1024), (1920, 1080))


def pixel(surface, x, y):
    data, stride = surface.get_data(), surface.get_stride()
    b, g, r = data[y * stride + 4 * x:y * stride + 4 * x + 3]
    return r, g, b


def white_rows(surface, box):
    """The rows of BOX with a nearly white pixel (the titles and labels)."""
    x0, y0, x1, y1 = (round(v) for v in box)
    return [y for y in range(max(0, y0), min(surface.get_height(), y1))
            if any(min(pixel(surface, x, y)) > 235 for x in range(max(0, x0), min(surface.get_width(), x1), 2))]


def overlap(a, b):
    return a[0] < b[0] + b[2] and b[0] < a[0] + a[2] and a[1] < b[1] + b[3] and b[1] < a[1] + a[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('assets', type=Path)
    parser.add_argument('--save', type=Path, help='write each drawn page to this directory')
    args = parser.parse_args()
    import media
    import oobe
    import welcome
    missing = oobe.ready(args.assets)
    if missing:
        raise SystemExit(f'{args.assets} lacks the oobe group (extract.py --only oobe): {", ".join(missing)}')
    welcome.use_fonts(args.assets, Path(tempfile.mkdtemp(prefix='oobe-fonts-')))
    source = oobe.Source(args.assets, media.language(args.assets))
    failures = 0

    def check(condition, what):
        nonlocal failures
        if not condition:
            failures += 1
            print('FAIL', what)

    wizard = oobe.Wizard(source, 'Test User')
    for index, page in enumerate(oobe.PAGES):
        for width, height in SIZES:
            wizard.page = index
            wizard.enter()
            if page == 'autoupdt':
                wizard.choose(1)
            image = wizard.render(width, height)
            if args.save:
                args.save.mkdir(parents=True, exist_ok=True)
                image.write_to_png(str(args.save / f'{page}-{width}x{height}.png'))
            frame, k, ox, oy = wizard.geometry(width, height)
            where = f'{page} {width}x{height}'
            # The title (white with its shadow) in the page's upper half.
            check(white_rows(image, (ox + 40 * k, oy + 100 * k, ox + 700 * k, oy + frame['size'][1] / 2 * k)),
                  f'{where}: no title')
            band = oy + frame['bottom'] * k
            rects = []
            for control in wizard.current:
                rect = control.rect
                check(rect is not None, f'{where}: {control.__class__.__name__} not drawn')
                if rect is None:
                    continue
                check(0 <= rect[0] and rect[0] + rect[2] <= width and 0 <= rect[1] and rect[1] + rect[3] <= height,
                      f'{where}: {control.__class__.__name__} outside the screen {rect}')
                if isinstance(control, oobe.Button):
                    check(band <= rect[1] and rect[1] + rect[3] <= oy + frame['size'][1] * k + 1,
                          f'{where}: {control.name} not in the bottom band {rect}')
                    check(control.key is not None, f'{where}: {control.name} has no access key')
                    x, y, w, h = control.label
                    check(white_rows(image, (x, y, x + w, y + h)), f'{where}: {control.name} has no label')
                rects.append(rect)
            check(not any(overlap(a, b) for i, a in enumerate(rects) for b in rects[i + 1:]),
                  f'{where}: controls overlap')

    # Through the pages with the keyboard, as on XP.
    wizard = oobe.Wizard(source, 'Test User')
    wizard.render(1024, 768)
    wizard.key('Return')
    check(wizard.name == 'autoupdt', 'Enter on Welcome does not go to Help protect your PC')
    wizard.render(1024, 768)
    wizard.key('n', 'n', alt=True)
    check(wizard.name == 'autoupdt', 'Help protect your PC goes on without a choice')
    check(wizard.key('o', 'o', alt=True) and wizard.automatic_updates == 2, 'Alt+O does not choose Not right now')
    wizard.render(1024, 768)
    wizard.key('Return')
    check(wizard.name == 'ident2', 'Help protect your PC does not go on after a choice')
    wizard.render(1024, 768)
    check(wizard.fields[0].text == 'Test User' and wizard.fields[0].all, 'the first name is not suggested')
    for ch in 'Ann':
        wizard.key(ch, ch)
    wizard.key('Tab')
    for ch in 'Guest':
        wizard.key(ch, ch)
    wizard.key('Return')
    check(wizard.name == 'ident2' and wizard.explain == 'rules' and wizard.fields[1].invalid,
          'Guest is taken as a name')
    wizard.key('2', '2', alt=True)
    for ch in 'Bob':
        wizard.key(ch, ch)
    wizard.key('Return')
    check(wizard.name == 'fini' and wizard.names == ['Ann', 'Bob'], f'names not taken: {wizard.names}')
    wizard.render(1024, 768)
    wizard.key('f', 'f', alt=True)
    check(wizard.done, 'Alt+F does not finish')

    # Identities2's rules.
    def accepted(*names):
        wizard = oobe.Wizard(source)
        for field, name in zip(wizard.fields, names):
            field.text = name
        return wizard.check_names()
    check(accepted('Ann'), 'one name is refused')
    check(not accepted(''), 'no name is accepted')
    check(not accepted('Ann', 'ann'), 'the same name twice is accepted')
    check(not accepted('a*b'), 'a name with * is accepted')
    check(not accepted(' . .'), 'a name of spaces and periods is accepted')
    check(not accepted('Administrator'), 'Administrator is accepted')
    print('ok' if not failures else f'{failures} failed')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
