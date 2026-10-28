"""Draw Windows 7's logon screen (LogonUI) with cairo and Pango.

The pieces come from the user's Windows 7 files at run time (greeter/install.sh
--win7 copies them): the background (imageres.dll), the user tile's frame, the
password field, the buttons and their glyphs (authui.dll), the strings and
fonts of authui's MUI file, and the edition's name (basebrd.dll). The layout
follows authui's own layout file (UIFILE 12400: sizes, paddings, which bitmap
each state shows) and was measured on Windows 7 SP1 at 1024x768 and 1280x800.

  python3 greeter/w7logon.py ASSETS OUT.png [WIDTH HEIGHT] [USER]
renders the screen to a file (for comparisons); W7LOGON_USERS=a,b lists users (with
USER, that one's tile and Switch User); W7LOGON_STATE=message or menu
renders a failed logon or the shutdown options' menu.
"""
import io
import json
import os
from pathlib import Path
import sys

import cairo
import gi
gi.require_version('Pango', '1.0')
gi.require_version('PangoCairo', '1.0')
gi.require_version('GdkPixbuf', '2.0')
gi.require_version('Gdk', '3.0')
from gi.repository import GdkPixbuf, Gdk, Pango, PangoCairo

sys.path.insert(0, str(Path(__file__).resolve().parent))
from xpres import Resources
from welcome import Bitmap, fontconfig

CONFIG = Path('/etc/win4wine/win7-greeter.json')
REQUIRED = ('authui.dll', 'imageres.dll', 'basebrd.dll', 'user.bmp')
RT_BITMAP = 2

# imageres.dll's logon backgrounds: (width, height) of IMAGE 5031... (5044... are another set)
BACKGROUNDS = {5031: (1280, 1024), 5032: (1280, 960), 5033: (1024, 768), 5034: (1600, 1200), 5035: (1440, 900),
               5036: (1920, 1200), 5037: (1280, 768), 5038: (1360, 768), 5039: (1024, 1280), 5040: (960, 1280),
               5041: (900, 1440), 5042: (768, 1280), 5043: (768, 1360)}
# authui.dll (UIFILE 12400)
TILE_FRAME, TILE_SIZE, PICTURE_INSET, PICTURE_SIZE = 12223, 190, 32, 126
EDIT = {'normal': 11003, 'hot': 11002, 'focused': 11001, 'disabled': 11000}
EDIT_W, EDIT_H, EDIT_BORDER = 225, 25, 4
SUBMIT = {'normal': 12288, 'hot': 12286, 'focused': 12286, 'pressed': 12287}
SUBMIT_SIZE = 30
OPTION = {'normal': 12263, 'focused': 12259, 'hot': 12260, 'hotfocused': 12261, 'pressed': 12262}
ACCESSIBILITY, ACCESSIBILITY_SIZE, ACCESSIBILITY_GLYPH = 'accessibility', (38, 28), 12213
SHUTDOWN = {'normal': 12296, 'focused': 12293, 'hot': 12294, 'hotfocused': 12292, 'pressed': 12295}
SHUTDOWN_SIZE, SHUTDOWN_GLYPH = (38, 28), 12215
MORE = {'normal': 12302, 'focused': 12299, 'hot': 12300, 'hotfocused': 12298, 'pressed': 12301}
MORE_SIZE, MORE_GLYPH = (20, 28), 12217
OPTIONS_HEIGHT, OPTIONS_PADDING = 96, 34
IDS_PASSWORD, IDS_SHUTDOWN, IDS_SHUTDOWN_OPTIONS, IDS_ACCESSIBILITY = 13001, 12128, 12131, 12122
IDS_WRONG = 11601    # "the user name or password is incorrect"
IDS_OK = 12125
# The shutdown options' menu: authui's names of the actions
IDS_RESTART, IDS_SLEEP, IDS_HIBERNATE, IDS_POWER_OFF = 3012, 3016, 3022, 3008
ERROR_ICON = 98      # imageres.dll
# Measured on Windows 7 SP1: the failed logon's message (icon, text) and its OK button
MESSAGE_Y, MESSAGE_TEXT_X, OK_Y, OK_SIZE = 107, 42, 179, (93, 31)
# The menu (aero.msstyles' Menu, measured): border, padding, item and separator heights, text inset
MENU_WIDTH, MENU_PAD, MENU_ITEM, MENU_SEPARATOR, MENU_TEXT_X, MENU_GUTTER = 174, 3, 22, 9, 35, 29
FONTS = {'small': 11800, 'button': 11802, 'message': 11803, 'large': 11804}
# DirectUI's text shadow (shadowintensity 75): box blur radius, passes, offset, opacity (fitted)
SHADOW = (2, 1, 1, 0.45)
BRANDING = 120       # basebrd.dll: the edition's name at 96 DPI
# Measured on Windows 7 SP1 (the screen's centre to the elements' top left)
TILE_X, TILE_Y = -96, -124
NAME_Y = 70          # below the tile's frame
EDIT_X, EDIT_Y = -113, 107   # the arrow's top; the field is 3px lower
# Several users: LogonUI first lists them (UserTileTemplate unzoomed: an 80x80
# frame, the picture 48x48 inside a 16 padding, the name below), in name order,
# one row centred, the frames 180 apart (measured with 2 and 4 users, short
# and long names); the frames' top 31 below the centre. A chosen user's tile
# is zoomed as with one user, 15 higher, with "Switch User" (GenericButton:
# at least 93x28, the text with 17 beside it) where a failed logon's OK is.
LIST_FRAME = {'normal': 12222, 'hot': 12218, 'selected': 12219, 'selectedhot': 12220}
LIST_SIZE, LIST_INSET, LIST_PICTURE = 80, 16, 48
LIST_STEP, LIST_Y, LIST_ROW = 180, 31, 143
SWITCH_SHIFT, IDS_SWITCH_USER = -15, 12123
LIST_NAME_Y = 0      # the name's top below the frame


def setup(cache, media=None):
    """The logon screen's settings and files: those greeter/install.sh
    installed, or else (MEDIA: extract/win7.py's output, for a session
    started without the greeter, such as windows7/7.sh's) links to the
    extracted files in CACHE, in the greeter's layout."""
    if CONFIG.is_file() or not media:
        config = json.loads(CONFIG.read_text())
        root = Path(config['assets'])
    else:
        config, root = {}, media_layout(Path(media), Path(cache) / 'media')
    missing = [name for name in REQUIRED if not (root / name).is_file()]
    if missing:
        raise FileNotFoundError('missing ' + ', '.join(missing))
    os.environ['FONTCONFIG_FILE'] = str(fontconfig(root / 'fonts', Path(cache)))
    return config, Assets(root)


def media_layout(media, target):
    """TARGET with links to MEDIA's files where greeter/install.sh copies them."""
    ui = json.loads((media / 'media.json').read_text())['ui_language']
    links = {'authui.dll': 'system32/authui.dll', 'imageres.dll': 'system32/imageres.dll',
             'basebrd.dll': 'branding/basebrd.dll', 'user.bmp': 'pictures/user.bmp',
             'fonts': 'fonts', ui: f'system32/{ui}'}
    target.mkdir(parents=True, exist_ok=True)
    for name, source in links.items():
        link = target / name
        if link.is_symlink() or link.exists():
            link.unlink()
        link.symlink_to(media / source)
    return target


class Assets:
    def __init__(self, root):
        self.root = Path(root)
        self.authui = Resources(self.root / 'authui.dll')
        mui = sorted(self.root.glob('*/authui.dll.mui'))
        self.strings = Resources(mui[0]) if mui else self.authui
        self.imageres = Resources(self.root / 'imageres.dll')
        self.basebrd = Resources(self.root / 'basebrd.dll')
        self.bitmaps = {}

    def bitmap(self, number, resources=None):
        key = (number, resources)
        if key not in self.bitmaps:
            res = resources or self.authui
            self.bitmaps[key] = Bitmap(res.bitmap(number), alpha=True)
        return self.bitmaps[key]

    def string(self, number):
        return self.strings.string(number) or ''

    def font(self, number):
        """authui's font strings: "size;weight;effects;face" (points at 96 DPI)."""
        size, weight, effects, face = (self.string(number) or '9;normal;shadow;Segoe UI').split(';')
        return face, int(size) * 96 / 72, weight == 'bold', 'shadow' in effects

    def background(self, width, height):
        """The background closest in shape to the screen, scaled to it (as LogonUI does)."""
        number = min(BACKGROUNDS, key=lambda n: (abs(BACKGROUNDS[n][0] / BACKGROUNDS[n][1] - width / height),
                                               abs(BACKGROUNDS[n][0] - width)))
        loader = GdkPixbuf.PixbufLoader()
        loader.write(self.imageres.find('IMAGE', number))
        loader.close()
        pixbuf = loader.get_pixbuf()
        if (pixbuf.get_width(), pixbuf.get_height()) != (width, height):
            pixbuf = pixbuf.scale_simple(width, height, GdkPixbuf.InterpType.BILINEAR)
        return pixbuf

    def icon(self, number, size=32):
        """An icon group of imageres.dll at SIZE, as a pixbuf."""
        import struct
        group = self.imageres.find(14, number)
        _, kind, count = struct.unpack_from('<HHH', group)
        entries = [struct.unpack_from('<BBBBHHIH', group, 6 + 14 * i) for i in range(count)]
        entry = min(entries, key=lambda e: (abs((e[0] or 256) - size), -e[5]))
        data = self.imageres.find(3, entry[7])
        ico = struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', *entry[:6], len(data), 22) + data
        loader = GdkPixbuf.PixbufLoader()
        loader.write(ico)
        loader.close()
        return loader.get_pixbuf()

    def picture(self, path=None):
        """The user's picture (an image LightDM knows of) or Windows 7's default user.bmp."""
        for candidate in (path, self.root / 'user.bmp'):
            if candidate and Path(candidate).is_file():
                try:
                    return GdkPixbuf.Pixbuf.new_from_file(str(candidate))
                except Exception:
                    continue
        return None


def nine_grid(cr, bitmap, x, y, width, height, margins):
    """resbmp(..., 7, ...): the bitmap stretched with its borders (left, top, right, bottom) kept."""
    left, top, right, bottom = margins
    src = bitmap.surface
    sw, sh = bitmap.width, bitmap.height
    columns = ((0, left, x, left), (left, sw - left - right, x + left, width - left - right),
               (sw - right, right, x + width - right, right))
    rows = ((0, top, y, top), (top, sh - top - bottom, y + top, height - top - bottom),
            (sh - bottom, bottom, y + height - bottom, bottom))
    for sx, swidth, dx, dwidth in columns:
        for sy, sheight, dy, dheight in rows:
            if swidth <= 0 or sheight <= 0 or dwidth <= 0 or dheight <= 0:
                continue
            cr.save()
            cr.rectangle(dx, dy, dwidth, dheight)
            cr.clip()
            cr.translate(dx, dy)
            cr.scale(dwidth / swidth, dheight / sheight)
            cr.set_source_surface(src, -sx, -sy)
            cr.get_source().set_filter(cairo.FILTER_NEAREST if (dwidth, dheight) == (swidth, sheight)
                                       else cairo.FILTER_BILINEAR)
            cr.paint()
            cr.restore()


def blur_alpha(surface, radius, passes=3):
    """A box blur, applied PASSES times, of an A8 surface's alpha (in place)."""
    width, height = surface.get_width(), surface.get_height()
    stride = surface.get_stride()
    data = surface.get_data()
    values = [[data[y * stride + x] for x in range(width)] for y in range(height)]
    size = 2 * radius + 1
    for _ in range(passes):
        for row in values:
            total = sum(row[:radius]) + 0
            out = []
            for x in range(width):
                total += row[x + radius] if x + radius < width else 0
                out.append(total / size)
                total -= row[x - radius] if x - radius >= 0 else 0
            row[:] = out
        for x in range(width):
            column = [values[y][x] for y in range(height)]
            total = sum(column[:radius])
            for y in range(height):
                total += column[y + radius] if y + radius < height else 0
                values[y][x] = total / size
                total -= column[y - radius] if y - radius >= 0 else 0
    for y in range(height):
        for x in range(width):
            data[y * stride + x] = min(255, int(values[y][x] + 0.5))
    surface.mark_dirty()


class Screen:
    """What the logon screen shows: one user's tile, its password field and the options."""

    def __init__(self, assets, user='', picture=None, users=None):
        """USERS: [(name, picture)] of the users to list; with one, or with USER
        given, that user's tile is shown directly."""
        self.assets = assets
        self.users = users or [(user, picture)]
        self.user = user
        self.picture_path = picture
        self.listing = not user and len(self.users) > 1
        self.selected = 0            # the listed user with the selection
        if not self.listing and not user:
            self.user, self.picture_path = self.users[0]
        self.password = ''
        # tiles (the list), password, submit, switch, accessibility, shutdown, more
        self.focus = 'tiles' if self.listing else 'password'
        self.hover = None
        self.pressed = None
        self.caret = True
        self.message = None          # LogonUI's text after a failed logon
        self.busy = False
        self.menu = None             # the shutdown options' menu: [(id, text)]
        self.menu_hover = None
        self.subpixel = True
        self.shadow = SHADOW

    # Geometry -------------------------------------------------------

    def choose(self, index):
        """Show the listed user INDEX's tile and password field."""
        self.user, self.picture_path = self.users[index]
        self.selected, self.listing = index, False
        self.password, self.focus, self.message = '', 'password', None

    def back_to_list(self):
        """Switch User: the list again, the user just shown selected."""
        names = [name for name, _ in self.users]
        self.selected = names.index(self.user) if self.user in names else 0
        self.listing, self.password, self.focus, self.message = True, '', 'tiles', None

    def list_rects(self, width, height):
        """The listed users' frames, in rows as wide as the screen allows."""
        count = len(self.users)
        per_row = max(1, min(count, (width + LIST_STEP - LIST_SIZE) // LIST_STEP))
        rows = (count + per_row - 1) // per_row
        top = height // 2 + LIST_Y - (rows - 1) * LIST_ROW // 2
        rects = []
        for index in range(count):
            row, column = divmod(index, per_row)
            in_row = min(per_row, count - row * per_row)
            left = width // 2 - (in_row * LIST_SIZE + (in_row - 1) * (LIST_STEP - LIST_SIZE)) // 2
            rects.append((left + column * LIST_STEP, top + row * LIST_ROW, LIST_SIZE, LIST_SIZE))
        return rects

    def switch_rect(self, width, height):
        # The text's width, 10 padding and 4 border each side, and 3 more each side
        # (measured: 196 wide around 162 of text in Japanese)
        w = max(93, self.text_width(self.button_label(IDS_SWITCH_USER), FONTS['button']) + 34)
        return ((width - w) // 2, height // 2 + OK_Y, w, OK_SIZE[1])

    def button_label(self, number):
        """A button's string without its access key's '&' (LogonUI hides the underline until Alt)."""
        return self.assets.string(number).replace('&', '')

    def text_width(self, text, font_number):
        surface = cairo.ImageSurface(cairo.FORMAT_A8, 1, 1)
        return self.layout(cairo.Context(surface), text, font_number).get_pixel_extents()[1].width

    def rects(self, width, height):
        cx, cy = width // 2, height // 2
        oy = height - OPTIONS_HEIGHT + OPTIONS_PADDING
        accessibility = (OPTIONS_PADDING, oy) + ACCESSIBILITY_SIZE
        more = (width - OPTIONS_PADDING - MORE_SIZE[0], oy) + MORE_SIZE
        shutdown = (more[0] - SHUTDOWN_SIZE[0], oy) + SHUTDOWN_SIZE
        options = {'accessibility': accessibility, 'shutdown': shutdown, 'more': more}
        if self.listing:
            # A user's tile takes its frame and the name below (UserTile: 123 high)
            tiles = {f'tile:{i}': (x - (LIST_STEP - LIST_SIZE) // 2, y, LIST_STEP, 123 - 20)
                     for i, (x, y, _, _) in enumerate(self.list_rects(width, height))}
            return dict(tiles, **options)
        cy += SWITCH_SHIFT if len(self.users) > 1 else 0
        tile = (cx + TILE_X, cy + TILE_Y, TILE_SIZE, TILE_SIZE)
        edit = (cx + EDIT_X, cy + EDIT_Y + 3, EDIT_W, EDIT_H)
        submit = (cx + EDIT_X + EDIT_W + 4, cy + EDIT_Y, SUBMIT_SIZE, SUBMIT_SIZE)
        rects = {'tile': tile, 'password': edit, 'submit': submit}
        if len(self.users) > 1:
            rects['switch'] = self.switch_rect(width, height)
        return dict(rects, **options)

    def hit(self, width, height, px, py):
        rects = self.rects(width, height)
        if self.message:
            rects = {'ok': self.ok_rect(width, height), 'accessibility': rects['accessibility'],
                     'shutdown': rects['shutdown'], 'more': rects['more']}
        for name, (x, y, w, h) in rects.items():
            if name != 'tile' and x <= px < x + w and y <= py < y + h:
                return name
        return None

    # Drawing --------------------------------------------------------

    def layout(self, cr, text, font_number):
        face, pixels, bold, _ = self.assets.font(font_number)
        layout = PangoCairo.create_layout(cr)
        options = cairo.FontOptions()
        # ClearType, as LogonUI draws (on the opaque screen surface)
        options.set_antialias(cairo.ANTIALIAS_SUBPIXEL if self.subpixel else cairo.ANTIALIAS_GRAY)
        options.set_subpixel_order(cairo.SUBPIXEL_ORDER_RGB)
        options.set_hint_style(cairo.HINT_STYLE_SLIGHT)
        PangoCairo.context_set_font_options(layout.get_context(), options)
        description = Pango.FontDescription(face)
        description.set_absolute_size(pixels * Pango.SCALE)
        if bold:
            description.set_weight(Pango.Weight.BOLD)
        layout.set_font_description(description)
        layout.set_text(text, -1)
        return layout

    def text(self, cr, text, font_number, x, y, color=(1, 1, 1), center_width=None, shadow=None):
        """Text in authui's font; DirectUI's "shadow" is a soft dark glow behind white text."""
        layout = self.layout(cr, text, font_number)
        ink, logical = layout.get_pixel_extents()
        if center_width is not None:
            x += (center_width - logical.width) // 2
        if shadow is None:
            shadow = self.assets.font(font_number)[3]
        if shadow:
            radius, passes, offset, strength = self.shadow
            pad = passes * radius + 2
            mask = cairo.ImageSurface(cairo.FORMAT_A8, logical.width + 2 * pad, logical.height + 2 * pad)
            mc = cairo.Context(mask)
            self.subpixel, subpixel = False, self.subpixel
            layout2 = self.layout(mc, text, font_number)
            self.subpixel = subpixel
            mc.move_to(pad + offset, pad + offset)
            PangoCairo.show_layout(mc, layout2)
            mask.flush()
            blur_alpha(mask, radius, passes)
            cr.save()
            cr.set_source_rgba(0, 0, 0, strength)
            cr.mask_surface(mask, x - pad, y - pad)
            cr.restore()
        cr.set_source_rgb(*color)
        cr.move_to(x, y)
        PangoCairo.show_layout(cr, layout)
        return logical.width

    def state(self, name):
        if self.pressed == name:
            return 'pressed'
        hot, focused = self.hover == name, self.focus == name
        return 'hotfocused' if hot and focused else 'hot' if hot else 'focused' if focused else 'normal'

    def button(self, cr, rect, backgrounds, margins, glyph, name):
        x, y, w, h = rect
        state = self.state(name)
        nine_grid(cr, self.assets.bitmap(backgrounds.get(state, backgrounds['normal'])), x, y, w, h, margins)
        image = self.assets.bitmap(glyph)
        cr.set_source_surface(image.surface, x + (w - image.width) // 2, y + (h - image.height) // 2)
        cr.paint()

    def render(self, width, height):
        surface = cairo.ImageSurface(cairo.FORMAT_RGB24, width, height)
        cr = cairo.Context(surface)
        Gdk.cairo_set_source_pixbuf(cr, self.assets.background(width, height), 0, 0)
        cr.paint()
        r = self.rects(width, height)

        if self.message:
            self.draw_message(cr, width, height, r)
            return self.draw_options(cr, surface, width, height, r)
        if self.listing:
            self.draw_list(cr, width, height)
            return self.draw_options(cr, surface, width, height, r)
        # The user's tile: the picture inside the frame, the name below
        tx, ty, _, _ = r['tile']
        picture = self.assets.picture(self.picture_path)
        if picture:
            scaled = picture.scale_simple(PICTURE_SIZE, PICTURE_SIZE, GdkPixbuf.InterpType.HYPER)
            Gdk.cairo_set_source_pixbuf(cr, scaled, tx + PICTURE_INSET, ty + PICTURE_INSET)
            cr.paint()
        frame = self.assets.bitmap(TILE_FRAME)
        cr.set_source_surface(frame.surface, tx, ty)
        cr.paint()
        self.text(cr, self.user, FONTS['large'], -1, ty + TILE_SIZE - 1, center_width=width)

        if not self.busy:
            # The password field, its cue while empty, and the arrow
            ex, ey, ew, eh = r['password']
            # LogonUI keeps the field's normal look while it has the caret (measured)
            state = 'hot' if self.hover == 'password' else 'normal'
            nine_grid(cr, self.assets.bitmap(EDIT[state]), ex, ey, ew, eh, (EDIT_BORDER,) * 4)
            cr.save()
            cr.rectangle(ex + 4, ey + 2, ew - 8, eh - 4)
            cr.clip()
            if self.password:
                caret_x = ex + 6 + self.text(cr, '●' * len(self.password), FONTS['small'], ex + 6, ey + 5,
                                             color=(0, 0, 0), shadow=False)
            else:
                self.text(cr, self.assets.string(IDS_PASSWORD), FONTS['small'], ex + 6, ey + 5,
                          color=(0x6d / 255, 0x6d / 255, 0x6d / 255), shadow=False)
                caret_x = ex + 4
            if self.focus == 'password' and self.caret:
                cr.set_source_rgb(0, 0, 0)
                cr.rectangle(caret_x, ey + 5, 1, eh - 10)
                cr.fill()
            cr.restore()
            sx, sy, sw, sh = r['submit']
            image = self.assets.bitmap(SUBMIT.get(self.state('submit'), SUBMIT['normal']))
            cr.set_source_surface(image.surface, sx, sy)
            cr.paint()
            if 'switch' in r:
                self.generic_button(cr, r['switch'], self.button_label(IDS_SWITCH_USER), 'switch')

        return self.draw_options(cr, surface, width, height, r)

    def generic_button(self, cr, rect, label, name):
        bx, by, bw, bh = rect
        nine_grid(cr, self.assets.bitmap(OPTION[self.state(name)]), bx, by, bw, bh, (4, 3, 4, 3))
        self.text(cr, label, FONTS['button'], bx, by + 3, center_width=bw)

    def draw_list(self, cr, width, height):
        """The users to choose from: pictures in small frames, names below."""
        for index, (x, y, w, h) in enumerate(self.list_rects(width, height)):
            name, path = self.users[index]
            picture = self.assets.picture(path)
            if picture:
                scaled = picture.scale_simple(LIST_PICTURE, LIST_PICTURE, GdkPixbuf.InterpType.HYPER)
                Gdk.cairo_set_source_pixbuf(cr, scaled, x + LIST_INSET, y + LIST_INSET)
                cr.paint()
            selected = index == self.selected and self.focus == 'tiles'
            hot = self.hover == f'tile:{index}'
            frame = self.assets.bitmap(LIST_FRAME['selected' * selected + 'hot' * hot or 'normal'])
            cr.set_source_surface(frame.surface, x, y)
            cr.paint()
            self.text(cr, name, FONTS['small'], x + w // 2 - 500, y + w + LIST_NAME_Y, center_width=1000)

    def draw_options(self, cr, surface, width, height, r):
        # The options along the bottom
        self.button(cr, r['accessibility'], OPTION, (4, 2, 4, 2), ACCESSIBILITY_GLYPH, 'accessibility')
        brand = self.assets.bitmap(BRANDING, self.assets.basebrd)
        cr.set_source_surface(brand.surface, (width - brand.width) // 2,
                              height - OPTIONS_HEIGHT + (OPTIONS_HEIGHT - brand.height) // 2)
        cr.paint()
        self.button(cr, r['shutdown'], SHUTDOWN, (4, 1, 4, 1), SHUTDOWN_GLYPH, 'shutdown')
        self.button(cr, r['more'], MORE, (4, 3, 4, 3), MORE_GLYPH, 'more')
        if self.menu:
            self.draw_menu(cr, width, height, r)
        return surface

    def ok_rect(self, width, height):
        w, h = OK_SIZE
        return ((width - w) // 2, height // 2 + OK_Y, w, h)

    def draw_message(self, cr, width, height, r):
        """A failed logon: the error icon and LogonUI's message in place of the tile, and OK."""
        text = self.message
        layout = self.layout(cr, text, FONTS['small'])
        text_width = layout.get_pixel_extents()[1].width
        x = (width - (MESSAGE_TEXT_X + text_width)) // 2 - 3
        y = height // 2 + MESSAGE_Y
        Gdk.cairo_set_source_pixbuf(cr, self.assets.icon(ERROR_ICON), x, y)
        cr.paint()
        self.text(cr, text, FONTS['small'], x + MESSAGE_TEXT_X, y + 10)
        bx, by, bw, bh = self.ok_rect(width, height)
        state = 'pressed' if self.pressed == 'ok' else 'hotfocused' if self.hover == 'ok' else 'focused'
        nine_grid(cr, self.assets.bitmap(OPTION[state]), bx, by, bw, bh, (4, 3, 4, 3))
        self.text(cr, self.assets.string(IDS_OK) or 'OK', FONTS['button'], bx, by + 3, center_width=bw)

    def menu_items(self):
        """[(action, label)], None standing for a separator."""
        return self.menu or []

    def menu_rect(self, width, height, r):
        mx, my, mw, _ = r['more']
        h = 2 * MENU_PAD + sum(MENU_SEPARATOR if item is None else MENU_ITEM for item in self.menu_items())
        return (mx + mw - MENU_WIDTH, my - h, MENU_WIDTH, h)

    def menu_positions(self, width, height):
        x, y, w, h = self.menu_rect(width, height, self.rects(width, height))
        top = y + MENU_PAD
        for index, item in enumerate(self.menu_items()):
            size = MENU_SEPARATOR if item is None else MENU_ITEM
            yield index, item, top, size
            top += size

    def draw_menu(self, cr, width, height, r):
        """The Aero (Basic) popup menu, as aero.msstyles draws it."""
        x, y, w, h = self.menu_rect(width, height, r)

        def fill(color, rx, ry, rw, rh):
            cr.set_source_rgb(*(c / 255 for c in color))
            cr.rectangle(rx, ry, rw, rh)
            cr.fill()
        fill((151, 151, 151), x, y, w, h)
        fill((245, 245, 245), x + 1, y + 1, w - 2, h - 2)
        fill((240, 240, 240), x + 3, y + 3, w - 6, h - 6)
        fill((226, 227, 227), x + MENU_GUTTER, y + 3, 1, h - 6)
        fill((255, 255, 255), x + MENU_GUTTER + 1, y + 3, 1, h - 6)
        for index, item, top, size in self.menu_positions(width, height):
            if item is None:
                fill((224, 224, 224), x + MENU_GUTTER + 1, top + 4, w - MENU_GUTTER - 4, 1)
                fill((255, 255, 255), x + MENU_GUTTER + 1, top + 5, w - MENU_GUTTER - 4, 1)
                continue
            if self.menu_hover == index:
                cr.set_source_rgba(0.2, 0.56, 0.93, 0.18)
                cr.rectangle(x + 3, top, w - 6, size)
                cr.fill()
                cr.set_source_rgba(0.5, 0.7, 0.95, 1)
                cr.rectangle(x + 3.5, top + 0.5, w - 7, size - 1)
                cr.set_line_width(1)
                cr.stroke()
            self.text(cr, item[1], FONTS['small'], x + MENU_TEXT_X, top + 2, color=(0, 0, 0), shadow=False)

    def menu_hit(self, width, height, px, py):
        x, y, w, h = self.menu_rect(width, height, self.rects(width, height))
        if not (x <= px < x + w):
            return None
        for index, item, top, size in self.menu_positions(width, height):
            if item is not None and top <= py < top + size:
                return index
        return None

    def background(self, width, height):
        """The background alone, left on the root window while the session starts."""
        surface = cairo.ImageSurface(cairo.FORMAT_RGB24, width, height)
        cr = cairo.Context(surface)
        Gdk.cairo_set_source_pixbuf(cr, self.assets.background(width, height), 0, 0)
        cr.paint()
        return surface


if __name__ == '__main__':
    root = Path(sys.argv[1])
    os.environ['FONTCONFIG_FILE'] = str(fontconfig(root / 'fonts', Path('/tmp/w7logon-fc')))
    width, height = (int(sys.argv[3]), int(sys.argv[4])) if len(sys.argv) > 4 else (1024, 768)
    names = os.environ.get('W7LOGON_USERS', '').split(',') if os.environ.get('W7LOGON_USERS') else []
    if names:
        # W7LOGON_USERS=a,b,c lists them; with USER, that one's tile with Switch User
        screen = Screen(Assets(root), sys.argv[5] if len(sys.argv) > 5 else '', users=[(n, None) for n in names])
    else:
        screen = Screen(Assets(root), sys.argv[5] if len(sys.argv) > 5 else 'tester')
    mode = os.environ.get('W7LOGON_STATE', '')
    if mode == 'message':
        screen.message = screen.assets.string(IDS_WRONG)
    elif mode == 'menu':
        a = screen.assets
        screen.menu = [('restart', a.string(IDS_RESTART)), None, ('hibernate', a.string(IDS_HIBERNATE)),
                       ('shutdown', a.string(IDS_POWER_OFF))]
    screen.render(width, height).write_to_png(sys.argv[2])
