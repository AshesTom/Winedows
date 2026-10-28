# Win4Wine-XP: the shell's visual style for Qt and GTK 3 applications

A Qt 6 style plugin that draws the session's Linux applications (KDE's and
any other Qt Widgets program) with the XP shell's **current** visual style:
the `.msstyles` that Display Properties applied, in its colour and size
variant, with XP's system colours and fonts. Inspired by
[XPlasma](https://tangled.org/hotsocket.fyi/xplasma), which does the same for
a KDE Plasma desktop; this one is written for win4wine (LGPL-2.1-or-later)
and follows the prefix of the session instead of a theme chosen in Plasma.

## What it reads

From the session's prefix (`WIN4WINE_PREFIX`, which `session/supervisor.py`
sets; else `WINEPREFIX`, `~/.config/win4wine/session.json`'s `prefix`, `~/.wine`),
its `user.reg`:

- `HKCU\Software\Microsoft\Windows\CurrentVersion\ThemeManager`: `DllName`
  (the `.msstyles`), `ColorName`, `SizeName`, `ThemeActive` (`0` is Windows Classic);
- `HKCU\Control Panel\Colors`: the system colours, Qt's palette;
- `HKCU\Control Panel\Desktop\WindowMetrics`: `MessageFont`, `MenuFont`,
  `StatusFont` (LOGFONTs) at `AppliedDPI`.

`user.reg` is watched: Wine writes it back a little after a change, and the
open applications take the new style, colours and fonts.

From the `.msstyles` (a resource-only PE file, read with bounds checks): the
layout INI of the variant (`COLORNAMES`/`SIZENAMES` index `FILERESNAMES`), and
its bitmaps. Parts are drawn as Wine's uxtheme draws them for the shell:
nine-grid `SizingMargins` (Stretch, Tile), `TrueSize`, `BorderFill`,
`BorderOnly`, image glyphs, `ImageFile1..7` by DPI or by size, frames by state,
32-bpp alpha (any pixel not opaque) or the transparent colour.

## What it draws

Push, check and radio buttons, group boxes, line edits, combo and spin boxes,
scroll bars (tracks, arrows, thumb and gripper), sliders and their ticks,
progress bars (XP's chunks, a marquee when busy), tabs and the tab page's
body, header sections, toolbar buttons, grippers and separators, the size
grip, tree glyphs and dotted lines, and XP's flat menus and menu bar. The
rest, and everything under Windows Classic, is Qt's own "Windows" style in
the session's system colours.

KDE's frameworks would give an application Breeze's colours: the style writes
the XP colours as a KDE colour scheme (`$XDG_RUNTIME_DIR/win4wine-qt/`) and
names it in the application's `KDE_COLOR_SCHEME_PATH`, so KColorScheme's
widgets use them too.

## GTK 3

GTK 3 has no theme engines any more, so `xpstyle-gtk` makes a GTK 3 theme
from the same style instead: XP's parts cut from the `.msstyles` as CSS
nine-grids (`border-image` with the parts' `SizingMargins`; the middle slice,
which GTK 3 leaves empty, as the background under it), check boxes, radio
buttons and tree glyphs as icons, the scroll bar's arrows, the drop-down
button and the progress chunks drawn at their size, and the system colours,
over GTK's own Adwaita for the rest.

```sh
.local/qt-style/bin/xpstyle-gtk --out DIR   # a theme in DIR (DIR/gtk-3.0/gtk.css and its assets)
.local/qt-style/bin/xpstyle-gtk --watch     # what the session runs
```

With `--watch` it keeps `~/.local/share/themes/Win4Wine-XP-<id>` up to date
(a new id, so a new name, whenever the look changes: GTK 3 only reloads a
theme under another name) and names it in XSETTINGS, which bridge-wm owns in
the X11 session: `Net/ThemeName`, and `Gtk/FontName` from XP's message font.
Running GTK 3 applications switch at once. `~/.local/share/themes/Win4Wine-XP`
links to the current one, for `GTK_THEME` in the Wayland session.
`session.json`'s `"gtk_style": false` turns it off.

Many applications add CSS of their own (cachyos-hello's buttons have no
border and rounded corners), at a priority above any theme's. The session's
GTK module (`gtk-module/`, in `GTK_MODULES`) lays the theme's
`win4wine.css`, XP's parts alone, just above the applications' CSS (the
user's own `gtk.css` stays above it) and follows the theme XSETTINGS names.
Switches, which XP has not, are a field with a push button for a knob.

The parts are XP's to the pixel: a GTK or Qt push button is the same column
of pixels as the shell's own (uxtheme's), border to border. So that text is
too, without ClearType the session's fontconfig draws it unsmoothed (whole
pixels, the fonts' own hinting with FreeType's classic TrueType
interpreter), as the shell does; with ClearType, in its RGB subpixels.

Firefox, GTK 4 and libadwaita applications take their light or dark look from
the desktop portal's colour scheme (`org.gnome.desktop.interface
color-scheme`, which KDE sets from its colour scheme): with `prefer-dark`
Firefox leaves the GTK theme for Adwaita's dark one. XP has no dark mode, so
the session sets `default` while it runs and gives the user's value back when
it ends (`session/appearance.py`). The theme's `gtk-dark.css` is the same
look, for GTK 3 applications that prefer a dark theme.

## Standard and Accurate; icons

Control Panel > Appearance and Themes has two items of the session
(`session/linuxlook.c`, a `.cpl` of the XP shell's own kind), which write
`~/.config/win4wine/linux-look.conf`; the style and the GTK generator watch it,
so open programs follow:

- **GTK/QT Themes**: the **Standard** style is the look described above. The
  **Accurate** one takes XP's own measures and habits from its sources:
  no pictures on push buttons that have text (`SH_DialogButtonBox_ButtonsHaveIcons`
  too) nor in menus (`AA_DontShowIconsInMenus`; in GTK the module hides the
  images of menu items and labelled buttons, toolbars keep theirs); pop-up menus
  as user32's `mncomput.c` lays them out (items as high as the menu font,
  `CYEDGE` and 2 px, separators half of `SM_CYMENUSIZE`, the check mark in the
  margin before the text, `MNXBORDER`), GTK's pop-overs as XP menus, list rows
  without GTK's extra spacing, 16 x 16 toolbar pictures. Each style's GTK theme is
  made once and kept (`Win4Wine-XP-<id>`, `Win4Wine-XP-Accurate-<id>`):
  switching between them, or a new session, reuses it. It is made again only
  when it is missing, when the shell's look changes, or with the item's
  **Rebuild** button (`theme_build`).
- **Icons**: the icon theme of Linux programs (Qt's `QIcon::themeName`,
  GTK's `Net/IconThemeName` in XSETTINGS), among the installed ones, or the
  desktop's. **Windows XP** is made by the session from the user's own XP
  files (`session/icon_theme.py`, in `~/.local/share/icons/win4wine-xp-icons`,
  like the cursors): shell32's folders, drives, documents and places, Explorer's
  and Internet Explorer's toolbar buttons for the common commands, user32's
  message icons and a few from other XP files, at 16, 24, 32 and 48 pixels;
  what XP has not comes from Breeze, Adwaita or hicolor. Nothing of Microsoft's
  is in the repository. `xpstyle-gtk` draws the themes' samples for the item
  (`~/.cache/win4wine/icon-previews`), as Windows cannot read their SVG.

```
style=accurate              # or standard (the default)
icon_theme=win4wine-xp-icons  # a folder of ~/.local/share/icons or /usr/share/icons; none: the desktop's
theme_build=1               # Rebuild raises it
```

## Building and trying it

`scripts/setup.sh` builds it (Qt 6 development files and CMake); by hand:

```sh
qt-style/build.sh        # .local/qt-style/styles/libwin4wine-xp.so, .local/qt-style/bin/xpstyle-gallery
```

In a running session, an application started by the session gets it; from
any terminal:

```sh
QT_PLUGIN_PATH=$PWD/.local/qt-style QT_STYLE_OVERRIDE=Win4WineXP QML_IMPORT_PATH=$PWD/.local/qt-style/qml WIN4WINE_PREFIX=$PWD/.local/prefix dolphin
```

`xpstyle-gallery` shows the widgets side by side (`--out F.png` captures
them, `--states F.png` draws each part in each state, `--menu F.png` a menu;
`QT_QPA_PLATFORM=offscreen` works for all three).

| Variable | |
|---|---|
| `WIN4WINE_QT_MSSTYLES=PATH[:COLOR]` | draw with another `.msstyles` (and colour variant) than the session's |
| `WIN4WINE_QT_STYLE_CLASSIC=1` | draw as Windows Classic |
| `WIN4WINE_QT_FONTS=0` | keep the application's fonts |
| `WIN4WINE_QT_PALETTE=0` | do not name the KDE colour scheme |
| `WIN4WINE_QT_DEBUG=1` | log the style and variant loaded |

`session.json`'s `"qt_style": false` turns it off for the session.

## Limits

- Qt Quick (QML) applications are not drawn by it, nor GTK 4 and libadwaita
  applications (GTK 4 has no theme it would read from the session).
- The GTK 3 theme cannot do all that a style can: spin buttons stay GTK's
  side-by-side buttons (with XP's button art), and GTK lays out its widgets
  with its own sizes.
- Only top tabs (`QTabBar::RoundedNorth`) take the style's tab art; others
  are drawn as Windows Classic.
- Menus follow XP's flat menus (Luna's `FlatMenus`), not the classic 3D ones.
- The Wayland session gives GTK programs the icon theme only through the
  desktop's own settings (no XSETTINGS there); Qt programs take it.
- KDE programs that ask KIconLoader (rather than `QIcon::fromTheme`) for an
  icon may keep the icon theme of KDE's settings.
