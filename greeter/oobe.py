"""Draw XP's Windows Welcome (the out-of-box experience) with cairo and Pango from the user's XP files.

XP shows Windows Welcome once, at the first start after setup: msoobe.exe
plays intro.wmv, then shows its pages (HTML in system32\\oobe) over the music
of title.wma. msoobe.exe itself needs XP's setup engine (syssetup.dll and
setupapi's private functions), so greeter/xp-oobe.py runs this instead. It
draws the pages a retail XP SP3 without a network connection shows: Welcome,
Help protect your PC, Who will use this computer? and Thank you!, as Internet
Explorer laid them out in msobshel.htm's frame. Their strings are read from
the pages and scripts, their fonts and colors from oobestyl.css and their
images from oobe\\images (extract/extract.py's 'oobe' group); none of their
code runs. Lengths are IE's at 96 DPI in the 1024x768 frame (800x600 on
smaller screens). msobshel.htm selects one of those native-size frames and
centers it on larger screens; it does not scale the frame or its images.
"""
from html.parser import HTMLParser
import io
import math
from pathlib import Path
import re

import cairo
import gi
gi.require_version('GdkPixbuf', '2.0')
from gi.repository import GdkPixbuf

import welcome

# What the pages need from extract/extract.py's output (assets/oobe).
REQUIRED = ('msobshel.htm', 'oobeutil.js', 'oobestyl.css', 'welcome.htm', 'autoupdt.htm', 'ident2.htm',
            'fini.htm', 'newmark1.jpg', 'newmark8.jpg', 'newtop1.jpg', 'newtop8.jpg', 'newbtm1.jpg',
            'newbtm8.jpg', 'mslogo.jpg', 'greenshd.gif', 'redshd.gif', 'backup.jpg', 'backover.jpg',
            'backdown.jpg', 'backoff.jpg', 'nextup.jpg', 'nextover.jpg', 'nextdown.jpg', 'nextoff.jpg')
PAGES = ('welcome', 'autoupdt', 'ident2', 'fini')
# msobshel.htm's frame (ScreenFrame) by its width: the suffix of its images, the
# bottom band's top, and the page's IFRAME: its top and height. The IFRAME is
# 20px narrower than the frame (MainFrameTable's last cell).
FRAMES = {1024: dict(size=(1024, 768), suffix='1', bottom=712, top=28, height=737),
          800: dict(size=(800, 600), suffix='8', bottom=542, top=25, height=564)}
LOGO = (20, 5)                  # NewMicrosoftLogo
MARGIN = 0.07                   # the pages' leftmargincell
BODY_MARGIN = 8                 # IE's default body margin in each page iframe
TOP = 15                        # the pages' first line below the IFRAME's top (measured)
TITLE_BOX = 41                  # .title's height (40) and its line's descent, as XP draws it (measured)
# .title's filter: shadow(color=#003399, direction=115, strength=4).
SHADOW, SHADOW_DIRECTION, SHADOW_STRENGTH = (0, 51, 153), 115, 4
BUTTON = 24                     # the Back and Next images
BUTTONS_TOP = 0.945             # .newbuttonposition
BACK_LEFT, NEXT_RIGHT, LABEL_GAP = 16, 4, 12
# "To continue, click Next." (g_ClickNextStrWelcome): its top by the frame, and its left.
CLICK_NEXT_TOP = {1024: 0.84, 800: 0.78}
CLICK_NEXT_LEFT = 0.02
PRIVACY = (0.075, 0.84, 0.80)   # Help protect your PC's privacy line: left, top, width
SHIELD = 37                     # greenshd.gif and redshd.gif
RADIO = 13
# Who will use this computer?: its table (cellpadding 4), the label cell and
# the input cell, and an input of size 24 as XP's IE draws it.
CELL_PADDING, LABEL_CELL, INPUT_CELL = 4, 100, 180
INPUT_WIDTH, INPUT_HEIGHT = 166, 20
NAMES, NAME_LENGTH = 5, 20
FORBIDDEN = set('"*+,/:;<=>?[\\]|')
RESERVED = ('administrator', 'guest')
LIST_INDENT = 40
# Luna's text box and radio button.
INPUT_BORDER = (127, 157, 185)
SELECTION = (49, 106, 197)
RADIO_BORDER = (28, 81, 128)
RADIO_DOT = ((33, 161, 33), (125, 213, 125))
COLORS = {'white': (255, 255, 255), 'black': (0, 0, 0), 'navy': (0, 0, 128), 'red': (255, 0, 0),
          'yellow': (255, 255, 0)}
VOID = {'br', 'img', 'input', 'hr', 'meta', 'link', 'param', 'col', 'area', 'base', 'wbr'}
HIDDEN = {'script', 'style', 'comment', 'title'}


def ready(root):
    """The files of the 'oobe' group that ROOT (extract/extract.py's output) lacks."""
    return [name for name in REQUIRED if not (Path(root) / 'oobe' / name).is_file()]


class Strings(HTMLParser):
    """The contents of a page's elements that have an id, as runs.

    A run is ('text', text, flags) with flags from b, u and a (link), ('br',)
    or ('li',). Elements Internet Explorer does not show (scripts, styles,
    IE's <comment>) are left out; an id seen again keeps its first element."""

    def __init__(self, text):
        super().__init__(convert_charrefs=True)
        self.stack, self.runs, self.open = [], {}, set()
        self.feed(text)
        self.close()

    def handle_starttag(self, tag, attrs):
        if tag == 'br':
            self.add(('br',))
            return
        if tag == 'li':
            self.add(('li',))
        if tag in VOID:
            return
        ident = dict(attrs).get('id')
        if ident and ident not in self.runs:
            self.runs[ident] = []
            self.open.add(ident)
        else:
            ident = None
        self.stack.append((tag, ident))

    def handle_endtag(self, tag):
        tags = [t for t, _ in self.stack]
        if tag not in tags:
            return
        while self.stack:
            top, ident = self.stack.pop()
            self.open.discard(ident)
            if top == tag:
                break

    def handle_data(self, data):
        tags = {t for t, _ in self.stack}
        if tags & HIDDEN:
            return
        flags = frozenset(f for f, names in (('b', {'b', 'strong'}), ('u', {'u'}), ('a', {'a'})) if tags & names)
        self.add(('text', data, flags))

    def add(self, run):
        if not self.stack or self.stack[-1][0] not in HIDDEN:
            for _, ident in self.stack:
                if ident in self.open:
                    self.runs[ident].append(run)


def script_strings(text):
    """The localizable strings of a page's or script's JScript (var L_..._Text = "...")."""
    escapes = {'n': '\n', 't': '\t', 'r': ''}
    return {m[1]: re.sub(r'\\(.)', lambda e: escapes.get(e[1], e[1]), m[2])
            for m in re.finditer(r'\bvar\s+(L_\w+)\s*=\s*"((?:[^"\\\n]|\\.)*)"', text)}


def style_sheet(text):
    """oobestyl.css's classes: {class: {property: value}} (a later rule wins, as in IE)."""
    classes = {}
    for m in re.finditer(r'\.([\w-]+)\s*\{([^}]*)\}', text):
        properties = classes.setdefault(m[1].lower(), {})
        for declaration in m[2].split(';'):
            if ':' in declaration:
                name, value = declaration.split(':', 1)
                properties[name.strip().lower()] = value.strip()
    return classes


def color(value, default=(255, 255, 255)):
    value = (value or '').strip().lower()
    if value in COLORS:
        return COLORS[value]
    value = value.lstrip('#')
    if re.fullmatch(r'[0-9a-f]{6}', value):
        return tuple(int(value[i:i + 2], 16) for i in (0, 2, 4))
    return default


class Font:
    """A CSS font of the pages: family, size and line height in points, weight and color."""

    def __init__(self, family='Arial', pt=9, bold=False, rgb=(255, 255, 255), line=None):
        self.family, self.pt, self.bold, self.rgb, self.line = family, pt, bold, rgb, line

    @classmethod
    def css(cls, classes, name):
        p = classes.get(name, {})
        family = p.get('font-family', 'Arial').split(',')[0].strip().strip('"\'')
        size = re.match(r'([\d.]+)\s*pt', p.get('font-size', ''))
        line = re.match(r'([\d.]+)\s*pt', p.get('line-height', ''))
        # A line height of 100% (or none) is the font's size.
        return cls(family, float(size[1]) if size else 9, p.get('font-weight', '').lower() == 'bold',
                   color(p.get('color')), float(line[1]) if line else None)

    def but(self, **changes):
        result = Font(self.family, self.pt, self.bold, self.rgb, self.line)
        result.__dict__.update(changes)
        return result


def surface(path):
    """An image file as a cairo surface (GdkPixbuf decodes it, cairo reads it as PNG)."""
    pixbuf = GdkPixbuf.Pixbuf.new_from_file(str(path))
    ok, data = pixbuf.save_to_bufferv('png', [], [])
    return cairo.ImageSurface.create_from_png(io.BytesIO(data))


def paint(cr, image, x, y, width, height, clip=None, pad=False):
    """IMAGE stretched to WIDTH x HEIGHT at X, Y; with PAD its edges extend over CLIP."""
    pattern = cairo.SurfacePattern(image)
    sx, sy = image.get_width() / width, image.get_height() / height
    pattern.set_matrix(cairo.Matrix(xx=sx, yy=sy, x0=-x * sx, y0=-y * sy))
    pattern.set_filter(cairo.FILTER_GOOD)
    if pad:
        pattern.set_extend(cairo.EXTEND_PAD)
    cr.save()
    cr.set_source(pattern)
    cr.rectangle(*(clip or (x, y, width, height)))
    cr.fill()
    cr.restore()


class Source:
    """Windows Welcome's files in extract/extract.py's output: strings, styles and images."""

    def __init__(self, root, language='en'):
        self.root = Path(root) / 'oobe'
        # The pages name no charset: IE read them in the system's code page.
        self.encoding = 'cp932' if language == 'ja' else 'cp1252'
        self.pages = {page: Strings(self.read(page + '.htm')).runs for page in PAGES}
        self.scripts = {}
        for name in ('msobshel.htm', 'oobeutil.js'):
            self.scripts.update(script_strings(self.read(name)))
        self.classes = style_sheet(self.read('oobestyl.css'))
        self.images = {}

    def read(self, name):
        data = (self.root / name).read_bytes()
        m = re.search(rb'charset=([\w-]+)', data[:2048])
        try:
            return data.decode(m[1].decode() if m else self.encoding, errors='replace')
        except LookupError:
            return data.decode(self.encoding, errors='replace')

    def runs(self, page, ident):
        return self.pages[page].get(ident, [])

    def script(self, name):
        return [('text', self.scripts.get(name, ''), frozenset())]

    def font(self, name):
        return Font.css(self.classes, name)

    def image(self, name):
        if name not in self.images:
            self.images[name] = surface(self.root / name)
        return self.images[name]


def plain(runs):
    return ''.join(r[1] for r in runs if r[0] == 'text')


def access_key(runs):
    """The underlined letter of a label (<u>N</u>ext), in lower case."""
    for run in runs:
        if run[0] == 'text' and 'u' in run[2] and run[1].strip():
            return run[1].strip()[0].lower()
    return None


class Line:
    def __init__(self, indent=0, bullet=False):
        self.fragments, self.ascent, self.descent, self.indent, self.bullet = [], 0, 0, indent, bullet


class Paragraph:
    """Runs laid out as IE's inline boxes: words wrap at WIDTH, lines are as tall as their
    fonts' line heights (CSS half-leading), <br> ends a line and <li> starts a bulleted one."""

    def __init__(self, lines, width, height):
        self.lines, self.width, self.height = lines, width, height


class Ink:
    """Text drawn with greeter/welcome.py's GDI-like text, at a scale of the 96 DPI layout."""

    def __init__(self, fonts):
        self.text = welcome.Text(type('Fonts', (), {'fonts': Path(fonts), 'face': 'Arial'})())
        self.blocks = {}
        self.k = 1

    def px(self, pt):
        """Pixels of PT points at the scale's DPI, rounded as MulDiv does (welcome.points)."""
        return int((pt * round(96 * self.k) + 36) // 72) if pt else 0

    def block(self, text, font, underline=False):
        key = (text, font.family, self.px(font.pt), font.bold, underline)
        if key not in self.blocks:
            self.blocks[key] = self.text.layout(text, self.px(font.pt), bold=font.bold, underline=underline,
                                                family=font.family)
        return self.blocks[key]

    def width(self, text, font):
        return self.text.size(self.block(text, font))[0] if text else 0

    def box(self, font):
        """The ascent and descent of FONT's line box: its glyphs centered in its line height."""
        ascent, descent = self.block('x', font).win[:2]
        line = self.px(font.line) if font.line else self.px(font.pt)
        above = ascent + (line - ascent - descent) // 2
        return above, line - above

    def lay(self, runs, font, width):
        """A Paragraph of RUNS in FONT (bold, underline and links change it), wrapped at WIDTH.

        A text run may carry its own Font as a fourth item, as a span of another class."""
        width = width if width else 1 << 30
        lines = [Line()]
        space = True                    # at a line's start, or after a space: spaces collapse
        for run in runs:
            if run[0] == 'br':
                self.finish(lines[-1], font)
                lines.append(Line(lines[-1].indent))
                space = True
                continue
            if run[0] == 'li':
                if lines[-1].fragments:
                    self.finish(lines[-1], font)
                    lines.append(Line())
                lines[-1].indent, lines[-1].bullet = round(LIST_INDENT * self.k), True
                space = True
                continue
            flags, base = run[2], run[3] if len(run) > 3 else font
            style = base.but(bold=base.bold or 'b' in flags)
            underline = bool(flags & {'u', 'a'})
            text = re.sub(r'[ \t\r\n]+', ' ', run[1])
            for token in self.tokens(text):
                if space:
                    token = token.lstrip(' ')
                if not token:
                    continue
                line = lines[-1]
                x = line.fragments[-1][3] + line.fragments[-1][4] if line.fragments else line.indent
                trimmed = token.rstrip(' ')
                if line.fragments and trimmed and x + self.width(trimmed, style) > width:
                    self.finish(line, font)
                    lines.append(Line(line.indent))
                    line, x = lines[-1], lines[-1].indent
                    token = token.lstrip(' ')
                    if not token:
                        continue
                line.fragments.append((token, style, underline, x, self.width(token, style)))
                space = token.endswith(' ')
        self.finish(lines[-1], font)
        if len(lines) > 1 and not lines[-1].fragments and not lines[-1].bullet:
            lines.pop()                 # a <br> at the end only ends its line
        widest = max((f[3] + f[4] for line in lines for f in line.fragments), default=0)
        return Paragraph(lines, widest, sum(line.ascent + line.descent for line in lines))

    def tokens(self, text):
        """Words with their following space; each wide character on its own (Japanese wraps anywhere)."""
        for word in re.findall(r'[^ ]+ *| +', text):
            if any(welcome.wide(ch) for ch in word):
                for part in re.findall(r'[^ ](?: *)| +', word):
                    yield part
            else:
                yield word

    def finish(self, line, font):
        boxes = [self.box(f[1]) for f in line.fragments] or [self.box(font)]
        line.ascent = max(a for a, _ in boxes)
        line.descent = max(d for _, d in boxes)

    def draw(self, cr, paragraph, x, y, align_right=False):
        top = y
        for line in paragraph.lines:
            baseline = top + line.ascent
            shift = paragraph.width - (line.fragments[-1][3] + line.fragments[-1][4]) \
                if align_right and line.fragments else 0
            if line.bullet and line.fragments:
                font = line.fragments[0][1]
                cr.save()
                cr.set_source_rgb(*(c / 255 for c in font.rgb))
                size = max(2, self.px(font.pt) // 3)
                cr.arc(x + line.indent - 13 * self.k + size / 2, baseline - self.px(font.pt) * 0.35, size / 2,
                       0, 2 * math.pi)
                cr.fill()
                cr.restore()
            for token, font, underline, left, _ in line.fragments:
                block = self.block(token, font, underline)
                self.text.draw(cr, block, round(x + left + shift), round(baseline - block.win[0]), font.rgb)
            top += line.ascent + line.descent
        return top


class Button:
    """Back or Next: its images (backup.jpg ...), its label and its access key."""

    def __init__(self, name, runs, action, left=None, right=None, enabled=True):
        self.name, self.runs, self.action, self.enabled = name, runs, action, enabled
        self.left, self.right = left, right
        self.key = access_key(runs)
        self.rect = self.label = None

    def image(self, hover, pressed):
        state = 'off' if not self.enabled else 'down' if pressed else 'over' if hover else 'up'
        return f'{self.name}{state}.jpg'


class Radio:
    def __init__(self, value, runs, explanation, shield):
        self.value, self.runs, self.explanation, self.shield = value, runs, explanation, shield
        self.key = access_key(runs)
        self.rect = self.label = None


class Field:
    """A text box of Who will use this computer?."""

    def __init__(self, index, runs, error, text=''):
        self.index, self.runs, self.error, self.text = index, runs, error, text
        self.key = access_key(runs)
        self.caret, self.all, self.invalid = len(text), False, False
        self.rect = None


class Wizard:
    """Windows Welcome's pages and what was chosen on them.

    render() draws the current page for a screen; press(), release(), motion()
    and key() change the state and return True when it needs drawing again.
    After Finish, done is True; automatic_updates and names hold the choices."""

    def __init__(self, source, name=''):
        self.source = source
        self.ink = Ink(source.root.parent / 'fonts')
        self.page = 0
        self.automatic_updates = None   # 1: turned on, 2: not right now (g_AU_radiosticky)
        self.radios = [Radio(1, source.runs('autoupdt', 'txtAU13a'), source.runs('autoupdt', 'txtAU13b'),
                             'greenshd.gif'),
                       Radio(2, source.runs('autoupdt', 'txtAU15a'), source.runs('autoupdt', 'txtAU15b'),
                             'redshd.gif')]
        self.fields = [Field(i, source.runs('ident2', ident), source.runs('ident2', error))
                       for i, (ident, error) in enumerate(zip(
                           ('txtIdentities_21', 'txtIdentities_23', 'txtIdentities_25',
                            'txtIdentities_27', 'txtIdentities_29'),
                           ('txtIdentities_22', 'txtIdentities_24', 'txtIdentities_26',
                            'txtIdentities_28', 'txtIdentities_210')))]
        # XP suggests the first name (InfoObj.SuggestIdentity0): here the Linux user's.
        self.fields[0].text = name[:NAME_LENGTH]
        self.fields[0].caret = len(self.fields[0].text)
        self.explain = None             # the rules shown after a wrong name: 'rules' or 'empty'
        self.focus = None
        self.hover = self.pressed = None
        self.done = False
        # XP hides access keys' underlines and focus rectangles until the keyboard is used.
        self.keyboard = False
        self.background = None
        self.enter()

    @property
    def name(self):
        return PAGES[self.page]

    @property
    def names(self):
        return [f.text.strip() for f in self.fields if f.text.strip()]

    # The page's controls, in tab order.
    def controls(self):
        page, s = self.name, self.source
        back = Button('back', s.runs(page, 'LocalBtnBack_Text'), self.back, left=BACK_LEFT)
        if page == 'welcome':
            return [Button('next', s.runs(page, 'LocalBtnNext_Text'), self.next, right=NEXT_RIGHT)]
        if page == 'autoupdt':
            return [*self.radios, back,
                    Button('next', s.runs(page, 'LocalBtnNext_Text'), self.next, right=NEXT_RIGHT,
                           enabled=self.automatic_updates is not None)]
        if page == 'ident2':
            return [*self.fields, back, Button('next', s.runs(page, 'LocalBtnNext_Text'), self.next,
                                               right=NEXT_RIGHT)]
        return [Button('next', s.runs(page, 'LocalBtnFinish_Text'), self.finish, right=NEXT_RIGHT)]

    def enter(self):
        """The first focus of a page, as its LoadMe function sets it."""
        self.current = self.controls()
        if self.name == 'autoupdt':
            self.focus = ('back', None)
        elif self.name == 'ident2':
            self.focus = ('field', 0)
            self.fields[0].all = True
        else:
            self.focus = ('next', None)
        self.hover = self.pressed = None

    def ident(self, control):
        if isinstance(control, Button):
            return (control.name, None)
        if isinstance(control, Radio):
            return ('radio', control.value)
        return ('field', control.index)

    def find(self, ident):
        return next((c for c in self.current if self.ident(c) == ident), None)

    def back(self):
        if self.page > 0:
            self.page -= 1
            self.explain = None
            self.enter()

    def next(self):
        if self.name == 'autoupdt' and self.automatic_updates is None:
            return
        if self.name == 'ident2' and not self.check_names():
            return
        self.page += 1
        self.enter()

    def finish(self):
        self.done = True

    def check_names(self):
        """Identities2's rules: at least one name; names of at most 20 characters without
        " * + , / : ; < = > ? [ \\ ] |, not only spaces and periods, all different, and not
        Administrator or Guest."""
        seen = set()
        for field in self.fields:
            name = field.text.strip()
            field.invalid = bool(name) and (
                bool(set(name) & FORBIDDEN) or not name.strip(' .') or name.lower() in RESERVED
                or name.lower() in seen or len(name) > NAME_LENGTH)
            if name:
                seen.add(name.lower())
        if not self.names:
            self.explain = 'empty'
        elif any(f.invalid for f in self.fields):
            self.explain = 'rules'
        else:
            self.explain = None
        if self.explain:
            first = next((f for f in self.fields if f.invalid), self.fields[0])
            self.focus, first.all = ('field', first.index), True
        return self.explain is None

    # Drawing.
    def geometry(self, width, height):
        frame = FRAMES[1024 if width >= 1024 and height >= 768 else 800]
        fw, fh = frame['size']
        # msobshel.htm's InitApplication chooses a fixed 800x600 or 1024x768
        # ScreenFrame, then centers it. Smaller screens clip it at (0, 0).
        ox = (width - fw) / 2 if width > fw else 0
        oy = (height - fh) / 2 if height > fh else 0
        return frame, 1, ox, oy

    def render(self, width, height):
        frame, k, ox, oy = self.geometry(width, height)
        if self.background is None or self.background[0] != (width, height):
            self.background = ((width, height), self.draw_background(width, height, frame, k, ox, oy))
        result = cairo.ImageSurface(cairo.FORMAT_RGB24, width, height)
        cr = cairo.Context(result)
        cr.set_source_surface(self.background[1])
        cr.paint()
        self.ink.k = k
        self.current = self.controls()
        fw = frame['size'][0]
        # The page's IFRAME, in screen pixels.
        self.frame = (ox, oy + frame['top'] * k, (fw - 20) * k, frame['height'] * k, frame, k)
        cr.rectangle(*self.frame[:4])
        cr.clip()
        getattr(self, 'draw_' + self.name)(cr)
        self.draw_buttons(cr)
        result.flush()
        return result

    def draw_background(self, width, height, frame, k, ox, oy):
        s, suffix = self.source, frame['suffix']
        fw, fh = frame['size']
        result = cairo.ImageSurface(cairo.FORMAT_RGB24, width, height)
        cr = cairo.Context(result)
        # InitApplication sets OOBEDocBody's background to black; the centered
        # ScreenFrame leaves those pixels visible on screens larger than it.
        cr.set_source_rgb(0, 0, 0)
        cr.paint()
        canvas = (ox, oy, fw * k, fh * k)
        paint(cr, s.image(f'newmark{suffix}.jpg'), *canvas, canvas)
        top = s.image(f'newtop{suffix}.jpg')
        band = top.get_height() * k
        paint(cr, top, ox, oy, top.get_width() * k, band, (ox, oy, fw * k, band))
        bottom = s.image(f'newbtm{suffix}.jpg')
        y = oy + frame['bottom'] * k
        paint(cr, bottom, ox, y, bottom.get_width() * k, bottom.get_height() * k,
              (ox, y, fw * k, bottom.get_height() * k))
        logo = self.matched(s.image('mslogo.jpg'), top)
        paint(cr, logo, ox + LOGO[0] * k, oy + LOGO[1] * k, logo.get_width() * k, logo.get_height() * k)
        result.flush()
        return result

    @staticmethod
    def matched(logo, band):
        """LOGO with its background's color made the band's: the two JPEGs' blues differ by a few
        levels, which shows as a box around the logo; on XP's screen there is none."""
        def corner(image):
            image.flush()
            data = image.get_data()
            return data[0], data[1], data[2]
        shift = [b - l for b, l in zip(corner(band), corner(logo))]
        result = cairo.ImageSurface(cairo.FORMAT_ARGB32, logo.get_width(), logo.get_height())
        cr = cairo.Context(result)
        cr.set_source_surface(logo)
        cr.paint()
        result.flush()
        data = result.get_data()
        for i in range(0, len(data), 4):
            for c in range(3):
                data[i + c] = max(0, min(255, data[i + c] + shift[c]))
        result.mark_dirty()
        return result

    def unit(self, value):
        return round(value * self.frame[5])

    def content(self):
        """The left edge and width of maincell after IE's body margin and 7% spacer."""
        x, _, w = self.frame[:3]
        body_margin = self.unit(BODY_MARGIN)
        body_width = max(0, w - 2 * body_margin)
        margin = round(body_width * MARGIN)
        return x + body_margin + margin, body_width - margin

    def text_font(self):
        return self.source.font('text-primary')

    def line(self):
        return sum(self.ink.box(self.text_font()))

    def flow(self, cr, parts):
        """Draw PARTS from the top of the page: XP's IE puts maincell's content at the top
        (its 100% table height does not reach the IFRAME's), not in the middle.

        A part is ('gap', blank_lines), ('title', runs), ('text', runs, font[, width]) or
        (height, draw) for a part a page lays out itself: draw(cr, top) draws it."""
        x, width = self.content()
        laid = []
        for part in parts:
            if part[0] == 'gap':
                laid.append((part[1] * self.line(), None))
            elif part[0] == 'title':
                laid.append((self.unit(TITLE_BOX), lambda cr, y, runs=part[1]: self.draw_title(cr, runs, x, y)))
            elif part[0] == 'text':
                paragraph = self.ink.lay(part[1], part[2], part[3] if len(part) > 3 else width)
                laid.append((paragraph.height, lambda cr, y, p=paragraph: self.ink.draw(cr, p, x, y)))
            else:
                laid.append(part)
        y = round(self.frame[1]) + self.unit(TOP)
        for height, draw in laid:
            if draw:
                draw(cr, y)
            y += height

    def draw_title(self, cr, runs, x, y):
        font = self.source.font('title')
        paragraph = self.ink.lay(runs, font, None)
        top = y + (self.ink.px(font.pt) - paragraph.height) // 2
        angle = math.radians(SHADOW_DIRECTION)
        dx, dy = math.sin(angle), -math.cos(angle)
        strength = max(1, self.unit(SHADOW_STRENGTH))
        for step in range(strength, 0, -1):
            cr.push_group()
            self.ink.draw(cr, self.recolor(paragraph, SHADOW), x + round(dx * step), top + round(dy * step))
            cr.pop_group_to_source()
            cr.paint_with_alpha(1 - (step - 1) / strength)
        self.ink.draw(cr, paragraph, x, top)

    @staticmethod
    def recolor(paragraph, rgb):
        lines = []
        for line in paragraph.lines:
            copy = Line(line.indent, line.bullet)
            copy.ascent, copy.descent = line.ascent, line.descent
            copy.fragments = [(t, f.but(rgb=rgb), u, x, w) for t, f, u, x, w in line.fragments]
            lines.append(copy)
        return Paragraph(lines, paragraph.width, paragraph.height)

    def absolute(self, cr, runs, font, left, top, width=None):
        x, y, w, h = self.frame[:4]
        paragraph = self.ink.lay(runs, font, round(w * width) if width else None)
        self.ink.draw(cr, paragraph, round(x + w * left) + self.unit(3), round(y + h * top) + self.unit(3))

    def draw_welcome(self, cr):
        s, font = self.source, self.text_font()
        self.flow(cr, [('gap', 2), ('title', s.runs('welcome', 'welc_s1')), ('gap', 1),
                       ('text', s.runs('welcome', 'w21'), font), ('gap', 1),
                       ('text', s.runs('welcome', 'welc_s3'), font)])
        # g_ClickNextStrWelcome
        runs = [*s.script('L_ContStr_Text'), ('text', ' ', frozenset())]
        width = self.frame[2]
        paragraph = self.ink.lay(runs, font, None)
        x = round(self.frame[0] + width * CLICK_NEXT_LEFT) + self.unit(3)
        y = round(self.frame[1] + self.frame[3] * CLICK_NEXT_TOP[self.frame[4]['size'][0]]) + self.unit(5)
        self.ink.draw(cr, paragraph, x, y)
        x += paragraph.width
        # Its span is emphasis-lesser, but XP draws it in the line's font (measured).
        rest = self.ink.lay(s.script('L_NextStr_Text'), font, None)
        self.ink.draw(cr, rest, x, y)
        x += rest.width
        self.ink.draw(cr, self.ink.lay(s.script('L_ClickNextLocStr_Text') + s.script('L_Period_Text'),
                                       font, None), x, y)

    def draw_autoupdt(self, cr):
        s, font = self.source, self.text_font()
        x, width = self.content()
        table = round(width * 0.95)
        intro = self.ink.lay(s.script('L_IntroSlipstream_Text'), font, table - self.unit(6))
        rows = [self.radio_row(radio, x, table) for radio in self.radios]
        self.flow(cr, [('gap', 2), ('title', s.runs('autoupdt', 'AutoUpdateTitle')), ('gap', 2),
                       (intro.height + self.unit(6),
                        lambda cr, y: self.ink.draw(cr, intro, x + self.unit(3), y + self.unit(3))),
                       ('gap', 1), *rows])
        privacy = s.runs('autoupdt', 'au_privacy_policy')
        self.absolute(cr, privacy, font, PRIVACY[0], PRIVACY[1], PRIVACY[2])

    def radio_row(self, radio, x, table):
        """A row of Help protect your PC's choices: the shield, the radio button and its
        label (emphasis-greater), then the explanation on the same line."""
        s, font = self.source, self.text_font()
        greater = s.font('emphasis-greater')
        shield = s.image(radio.shield)
        left = x + self.unit(3 + SHIELD + 4)
        label_left = left + self.unit(RADIO + 7)
        width = x + table - label_left - self.unit(3)
        label = self.ink.lay(self.cues(radio.runs), greater, width)
        # The explanation (text-primary) follows the label on its line.
        text = self.ink.lay([(*r, greater) if r[0] == 'text' else r for r in self.cues(radio.runs)]
                            + [('text', ' ', frozenset(), greater)] + radio.explanation, font, width)
        gap = self.line() if radio.value == 1 else 0      # the two <br> after the first choice
        height = max(self.unit(46 + 2), text.height + gap + self.unit(2)) + self.unit(2)

        def draw(cr, y):
            paint(cr, shield, x + self.unit(3), y + self.unit(3), shield.get_width() * self.frame[5],
                  shield.get_height() * self.frame[5])
            first = text.lines[0]
            center = y + self.unit(3) + first.ascent - self.ink.px(greater.pt) * 0.35
            box = (left, round(center - self.unit(RADIO) / 2), self.unit(RADIO), self.unit(RADIO))
            radio.rect = box
            radio.label = (label_left, y + self.unit(3), label.width, label.height)
            self.draw_radio(cr, box, self.automatic_updates == radio.value)
            self.ink.draw(cr, text, label_left, y + self.unit(3))
            if self.focus == ('radio', radio.value) and self.keyboard:
                self.focus_rect(cr, (label_left - 1, y + self.unit(3), label.width + 2, first.ascent + first.descent))
        return (height, draw)

    def draw_radio(self, cr, box, checked):
        x, y, size, _ = box
        r = size / 2
        cx, cy = x + r, y + r
        cr.save()
        gradient = cairo.LinearGradient(x, y, x + size, y + size)
        gradient.add_color_stop_rgb(0, 220 / 255, 220 / 255, 215 / 255)
        gradient.add_color_stop_rgb(1, 1, 1, 1)
        cr.arc(cx, cy, r - 0.5, 0, 2 * math.pi)
        cr.set_source(gradient)
        cr.fill_preserve()
        cr.set_source_rgb(*(c / 255 for c in RADIO_BORDER))
        cr.set_line_width(max(1, self.frame[5]))
        cr.stroke()
        if checked:
            dot = cairo.RadialGradient(cx - r / 4, cy - r / 4, 0, cx, cy, r * 0.45)
            dot.add_color_stop_rgb(0, *(c / 255 for c in RADIO_DOT[1]))
            dot.add_color_stop_rgb(1, *(c / 255 for c in RADIO_DOT[0]))
            cr.arc(cx, cy, r * 0.42, 0, 2 * math.pi)
            cr.set_source(dot)
            cr.fill()
        cr.restore()

    def focus_rect(self, cr, rect):
        x, y, w, h = rect
        cr.save()
        cr.set_source_rgb(1, 1, 1)
        cr.set_line_width(1)
        cr.set_dash([1, 1])
        cr.rectangle(round(x) + 0.5, round(y) + 0.5, round(w) - 1, round(h) - 1)
        cr.stroke()
        cr.restore()

    def draw_ident2(self, cr):
        s, font = self.source, self.text_font()
        x, width = self.content()
        error_font = s.font('text-error')
        row = self.unit(INPUT_HEIGHT + 2 * CELL_PADDING)

        def table(cr, y):
            for field in self.fields:
                top = y + field.index * row
                label = self.ink.lay(self.cues(field.runs), font, None)
                lx = x + self.unit(1 + 2 * CELL_PADDING + CELL_PADDING)
                self.ink.draw(cr, label, lx, top + (row - label.height) // 2)
                ix = x + self.unit(1 + 2 * CELL_PADDING + LABEL_CELL + 2 * CELL_PADDING + CELL_PADDING)
                field.rect = (ix, top + self.unit(CELL_PADDING), self.unit(INPUT_WIDTH), self.unit(INPUT_HEIGHT))
                self.draw_field(cr, field)
                if field.invalid:
                    note = self.ink.lay(field.error, font.but(rgb=(0x99, 0x00, 0x33)), None)
                    ex = ix - self.unit(CELL_PADDING) + self.unit(INPUT_CELL + 2 * CELL_PADDING + CELL_PADDING)
                    self.ink.draw(cr, note, ex, top + (row - note.height) // 2)
        parts = [('gap', 2), ('title', s.runs('ident2', 'Ident2_str1')), ('gap', 1),
                 ('text', s.runs('ident2', 'txtIdentities_2'), font), ('gap', 1),
                 (row * NAMES, table)]
        if self.explain:
            idents = ('txtIdentities_213', 'txtIdentities_214', 'txtIdentities_215', 'txtIdentities_216',
                      'txtIdentities_218') if self.explain == 'rules' else ('txtIdentities_217',)
            runs = [r for ident in idents for r in [('li',), *s.runs('ident2', ident)]]
            parts += [('gap', 1), ('text', runs, error_font), ('gap', 1)]
        parts.append(('gap', 1))        # g_ClickNextStr: a non-breaking space
        self.flow(cr, parts)

    def draw_field(self, cr, field):
        x, y, w, h = field.rect
        font = Font('MS UI Gothic' if self.source.encoding == 'cp932' else 'MS Shell Dlg', 9 if
                    self.source.encoding == 'cp932' else 8, rgb=(0, 0, 0))
        cr.save()
        cr.rectangle(x, y, w, h)
        cr.set_source_rgb(1, 1, 1)
        cr.fill_preserve()
        cr.set_source_rgb(*(c / 255 for c in INPUT_BORDER))
        cr.set_line_width(1)
        cr.rectangle(x + 0.5, y + 0.5, w - 1, h - 1)
        cr.stroke()
        cr.rectangle(x + 1, y + 1, w - 2, h - 2)
        cr.clip()
        focused = self.focus == ('field', field.index)
        tx = x + self.unit(3)
        block_h = sum(self.ink.block('x', font).win[:2])
        ty = y + (h - block_h) // 2
        if field.text:
            if focused and field.all:
                cr.set_source_rgb(*(c / 255 for c in SELECTION))
                cr.rectangle(tx, ty, self.ink.width(field.text, font), block_h)
                cr.fill()
                self.ink.text.draw(cr, self.ink.block(field.text, font), tx, ty, (255, 255, 255))
            else:
                self.ink.text.draw(cr, self.ink.block(field.text, font), tx, ty, (0, 0, 0))
        if focused and not field.all:
            cx = tx + self.ink.width(field.text[:field.caret], font)
            cr.set_source_rgb(0, 0, 0)
            cr.rectangle(cx, ty, 1, block_h)
            cr.fill()
        cr.restore()
        self.field_font = font

    def draw_fini(self, cr):
        s, font = self.source, self.text_font()
        self.flow(cr, [('gap', 2), ('title', s.runs('fini', 'fini_str1')), ('gap', 1),
                       ('text', s.runs('fini', 'FiniSummaryTitle1'), font),
                       ('text', s.runs('fini', 'fini_str3'), font)])

    def draw_buttons(self, cr):
        x, y, w, h = self.frame[:4]
        top = round(y + h * BUTTONS_TOP) + self.unit(2)
        font = self.text_font()
        size = self.unit(BUTTON)
        for button in self.current:
            if not isinstance(button, Button):
                continue
            label = self.ink.lay(self.cues(button.runs), font, None)
            if button.left is not None:
                bx = round(x + self.unit(button.left))
                lx = bx + size + self.unit(LABEL_GAP)
            else:
                bx = round(x + w - self.unit(button.right) - size)
                lx = bx - self.unit(LABEL_GAP) - label.width
            ident = self.ident(button)
            image = self.source.image(button.image(self.hover == ident, self.pressed == ident))
            paint(cr, image, bx, top, size, size)
            ly = top + (size - label.height) // 2 + self.unit(1)
            self.ink.draw(cr, label, lx, ly)
            button.rect = (bx, top, size, size)
            button.label = (lx, ly, label.width, label.height)
            if self.focus == ident and button.enabled and self.keyboard:
                self.focus_rect(cr, (bx + 2, top + 2, size - 4, size - 4))

    def cues(self, runs):
        """RUNS without their access key's underline until the keyboard is used."""
        if self.keyboard:
            return runs
        return [(r[0], r[1], r[2] - {'u'}, *r[3:]) if r[0] == 'text' else r for r in runs]

    # Input.
    def hit(self, x, y):
        for control in self.current:
            for rect in (control.rect, getattr(control, 'label', None)):
                if rect and rect[0] <= x < rect[0] + rect[2] and rect[1] <= y < rect[1] + rect[3]:
                    return control
        return None

    def motion(self, x, y):
        control = self.hit(x, y)
        hover = self.ident(control) if isinstance(control, Button) and control.enabled else None
        if hover != self.hover:
            self.hover = hover
            return True
        return False

    def press(self, x, y):
        control = self.hit(x, y)
        if isinstance(control, Button):
            if control.enabled:
                self.pressed = self.focus = self.ident(control)
            return True
        if isinstance(control, Radio):
            self.choose(control.value)
            return True
        if isinstance(control, Field):
            self.focus = ('field', control.index)
            control.all = False
            control.caret = self.caret_at(control, x)
            return True
        return False

    def release(self, x, y):
        pressed, self.pressed = self.pressed, None
        control = self.hit(x, y)
        if pressed and control is not None and self.ident(control) == pressed and control.enabled:
            control.action()
            self.hover = None
        return pressed is not None

    def caret_at(self, field, x):
        font = getattr(self, 'field_font', None)
        if font is None:
            return len(field.text)
        left = field.rect[0] + self.unit(3)
        for i in range(len(field.text) + 1):
            if left + self.ink.width(field.text[:i], font) >= x - 2:
                return i
        return len(field.text)

    def choose(self, value):
        self.automatic_updates = value
        self.focus = ('radio', value)

    def key(self, name, char='', alt=False, shift=False, control=False):
        """A key: NAME is its X keysym name (Return, Tab, a ...), CHAR the text it types."""
        if not self.keyboard and (alt or name in ('Tab', 'ISO_Left_Tab', 'Alt_L', 'Alt_R')):
            self.keyboard = True
            if name in ('Alt_L', 'Alt_R'):
                return True
        focused = self.find(self.focus)
        if alt and char:
            for c in self.current:
                if c.key == char.lower():
                    if isinstance(c, Button):
                        if c.enabled:
                            c.action()
                    elif isinstance(c, Radio):
                        self.choose(c.value)
                    else:
                        self.focus, c.all = ('field', c.index), True
                    return True
            return False
        if name in ('Tab', 'ISO_Left_Tab'):
            order = [c for c in self.current if not isinstance(c, Button) or c.enabled]
            if not order:
                return False
            if self.name == 'autoupdt':
                # The two choices are one group: Tab goes to the chosen one, or the first.
                chosen = next((c for c in order if isinstance(c, Radio) and c.value == self.automatic_updates),
                              order[0])
                order = [c for c in order if not isinstance(c, Radio) or c is chosen]
            index = next((i for i, c in enumerate(order) if self.ident(c) == self.focus), -1)
            if index < 0 and isinstance(focused, Radio):
                index = 0
            index = (index + (-1 if shift or name == 'ISO_Left_Tab' else 1)) % len(order)
            self.focus = self.ident(order[index])
            if isinstance(order[index], Field):
                order[index].all = True
            return True
        if name in ('Return', 'KP_Enter'):
            if isinstance(focused, Button):
                if focused.enabled:
                    focused.action()
            else:
                target = next((c for c in self.current if isinstance(c, Button) and c.name == 'next'), None)
                if target and target.enabled:
                    target.action()
            return True
        if isinstance(focused, Radio):
            if name in ('space', 'Up', 'Down', 'Left', 'Right'):
                value = focused.value if name == 'space' else (2 if focused.value == 1 else 1)
                self.choose(value)
                return True
            return False
        if isinstance(focused, Button):
            if name == 'space' and focused.enabled:
                focused.action()
                return True
            return False
        if isinstance(focused, Field):
            return self.edit(focused, name, char, control)
        return False

    def edit(self, field, name, char, control):
        if field.all and name in ('BackSpace', 'Delete'):
            field.text, field.caret, field.all = '', 0, False
            return True
        if control and name in ('a', 'A'):
            field.all = True
            return True
        if name in ('Left', 'Right', 'Home', 'End'):
            if field.all:
                field.all = False
                field.caret = 0 if name in ('Left', 'Home') else len(field.text)
                return True
            field.caret = {'Left': max(0, field.caret - 1), 'Right': min(len(field.text), field.caret + 1),
                           'Home': 0, 'End': len(field.text)}[name]
            return True
        if name == 'BackSpace' and field.caret:
            field.text = field.text[:field.caret - 1] + field.text[field.caret:]
            field.caret -= 1
            return True
        if name == 'Delete' and field.caret < len(field.text):
            field.text = field.text[:field.caret] + field.text[field.caret + 1:]
            return True
        if char and char.isprintable() and not control:
            if field.all:
                field.text, field.caret, field.all = '', 0, False
            if len(field.text) < NAME_LENGTH:
                field.text = field.text[:field.caret] + char + field.text[field.caret:]
                field.caret += 1
            return True
        return False
