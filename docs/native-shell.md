# The native shell

`native-shell/` ports XP's `explorer.exe` to Linux: an ELF program, without Wine, in the way the Aqua
session rebuilds Mac OS X's Finder and Dock. It runs no XP code. The user's own XP files only give it
resources, read at run time: the visual style's art and rules (`luna.msstyles`, through
`qt-style/src/msstyles.cpp`), the strings, the flag and the menus (`explorer.exe`), the icons and place names
(`shell32.dll`), the dialogs of Display Properties (`themeui.dll`), the wallpapers, the pictures and the fonts.

## What it does so far

| Part | State |
|---|---|
| Wallpaper | Bliss stretched by default, or what Display Properties chose, followed live; the background colour; each monitor |
| Desktop icons | Recycle Bin (empty or full) in XP's lower right corner and the Desktop folder's files in XP's grid. Select, double-click, drag to place (remembered), rename, send to the trash, Arrange Icons |
| Taskbar | Luna or Classic: a button per window (normal, hot, active in bold, flashing), minimize by clicking the active button, a window menu, its strut |
| Start menu | XP's two columns from the style's StartPanel parts: the user's picture and name, the default browser and mail, the most used programs, the places, All Programs and My Recent Documents with their cascades, Log Off and Turn Off Computer |
| Folder windows | The menus, the toolbar from shell32's pictures, the address bar and Go, the Common Tasks pane from the shell style's art in each colour scheme, the Folders tree, a search, and Thumbnails, Tiles, Icons, List and Details |
| File operations | Copy, cut and paste (with the desktop's other file managers), Move To and Copy To, drag and drop, a thread with a progress window and XP's replace question, the Recycle Bin with Restore and Empty, Delete and Shift+Delete, rename, new folder, new text document, Properties with the size counted and the drive's pie |
| Virtual folders | My Computer (with its groups and the drives' free space), the Recycle Bin, Control Panel, search results |
| Control Panel | Category view and Classic view, with XP's own names and icons. Display, Folder Options, Fonts, Network Connections, Add or Remove Programs (a list), Sounds and Audio Devices, System, Mouse, Keyboard, Printers, User Accounts, Date and Time |
| Title bars | XP's frames for Linux windows, without Wine: caption, active and inactive, the three buttons, the system menu, moving and sizing from every edge, maximizing as XP does (the borders off the screen), rounded corners as the window's shape |
| Display Properties | Themes (Windows XP, Windows Classic), Desktop (wallpaper, position, colour), Appearance (visual style, colour scheme, font size) and Linux Programs (the Qt and GTK style, the icon theme), with previews. Apply changes everything at once, open programs included |
| Qt and GTK programs | Drawn with the current theme by `qt-style/`, from the same choices, with XP's fonts |
| XP's icons | `session/icon_theme.py` makes the `win4wine-xp-icons` theme from the user's files the first time; it is the session's default for Qt and GTK programs too |
| Clock | The locale's short time, the date as its tooltip; wakes once a minute |
| Notification area | Linux tray icons from `bridge-wm/tray.py`'s files, left click, right click and their menus |
| Windows key, Ctrl+Esc | Open the Start menu; Win+R and the other hotkeys do nothing yet |

Not ported yet: XP's zip folders, Send To, shortcuts (`.lnk`), network places, Back's use of the tree for virtual
folders (the Folders tree shows the file system from `/`, without XP's Desktop at its root), Help and the Start
menu's Search (shown disabled, as is Printers and Faxes without `system-config-printer`), the Start menu's keyboard
navigation, grouping of similar taskbar buttons (they shrink to
40 pixels, then those that do not fit are not shown), Quick Launch, the Screen Saver and Settings pages, the
menus' drop shadows, XP's own tray icons (`xptray.exe`), the taskbar's menu. Strings that XP's files do not give
(the desktop's menus, the Linux Programs page) are in English.

## How to run it

In a window of the current desktop, without Wine (bsdtar, Qt 6, python-xlib and python-gobject):

```bash
./xp.sh native /path/to/WindowsXP-SP3.iso
```

`native-shell/extract.py` takes only the files above into `.local/native-assets`; later runs need only
`./xp.sh native --`. `qt-style/build.sh` builds what draws Qt and GTK programs, which `session.sh` uses when it is
there. `native-shell/session.sh` is the same session for a whole X display, where Turn Off Computer really turns
the computer off (`WIN4WINE_POWER=0` keeps it from doing so; `xp.sh native` sets it); with `--inside` it leaves the
current window manager in place.

`xp-native-shell --explore FOLDER` opens only a folder window and `--applet ID` only an applet (`--help` lists them).
In this session `xdg-open` brings folders to `--explore`, through `xp-explorer.desktop` and
`~/.config/xp-shell-mimeapps.list`, which only this desktop reads.

In the Wine session: build it (`native-shell/build.sh`) and set `"native_shell": true` in `session.json`. The
supervisor then starts it instead of XP's explorer, and Wine's proxy.exe draws the frames. This mode has not been
run yet: it needs a Wine build.

## The Control Panel's category view

It is what `shell32.dll`'s UIFILE 4 lays out and the shell style's UIFILE 2 (`blue_ss.dll`, `home_ss.dll`,
`metal_ss.dll`) draws, whose colours are read from that file at run time:

| Element | Value in the blue style |
|---|---|
| The view | `rgb(99,117,214)`, a white line at the bottom, on its left a one-pixel gradient `rgb(148,187,255)` to `rgb(99,117,214)` |
| Watermark, in the lower right corner | Bitmap 25 for the categories, 26 inside a category |
| Directive ("Pick a category") | Franklin Gothic Medium, 21 pt, bold, `rgb(214,223,245)`; 30 px above it, inside the container's padding of 20 |
| Links | Tahoma 8 bold, white, underlined under the pointer; margin 12, a 32-pixel icon, 5 before the text; two columns |
| A category's banner | 35 tall, gradient `rgb(51,102,204)` to `rgb(99,117,214)`, title at 50, its icon at (10, 7) over the edge |

A category is an address of its own (`::controlpanel/N`), so Back and Up work. Its tasks are shell32's words, for the
tasks that do something here; Add or Remove Programs goes straight to its applet, as on XP. Under Windows Classic
the view takes the system colours.

## The Control Panel's applets

An applet is listed only if it can do its work here: Network needs `nmcli` or `nm-connection-editor`, Sounds needs
`pactl`, Date and Time needs `timedatectl`, Printers needs `system-config-printer`.

| Applet | What it changes |
|---|---|
| Date and Time | `timedatectl set-time`, `set-timezone`, `set-ntp`, which ask for the administrator's rights through the polkit agent that `session.sh` starts. Only on Apply, and each only if the user changed it |
| Sounds and Audio Devices | `pactl`: the default output, its volume, muting |
| Mouse | Pointer speed and swapped buttons through the X server's own requests; again at each session (`native-input.conf`) |
| Keyboard | The repeat delay and rate through the X server's XKB controls; again at each session |
| User Accounts | Writes `~/.face`, which the Start menu and login screens read, from XP's pictures |
| Folder Options | Common tasks or classic folders, hidden files (`native-explorer.conf`), followed at once by open windows |
| Add or Remove Programs | Lists the programs. It removes nothing: that is the distribution's package manager |
| System, Network | Show what is there. Network opens `nm-connection-editor` when it is installed |

## How it fits

- The shell's windows are an EWMH desktop and dock, so any window manager can manage them. They also have
  the `_WIN4WINE_CLASS` that bridge-wm and the supervisor look for (`Progman`, `Shell_TrayWnd`).
- `FrameManager` (`--frames`) speaks proxy.exe's side of bridge-wm's IPC files (`docs/architecture.md`): it reads
  `windows.tsv` and `geometry.tsv`, writes `frames.tsv` and `*.cmd`, and each frame is an X window of class
  `Win4WineProxy` whose `_WIN4WINE_HWND` is its own ID. `bridge-wm/wm.py` keeps it just under its client and moves
  each with the other, the same for both programs. Its only change: a window `WM_TRANSIENT_FOR` something that is
  no client (Qt's dialogs without a parent name their hidden group leader) gets a frame of its own.
- The buttons come from `_NET_CLIENT_LIST`, `_NET_ACTIVE_WINDOW` and each window's properties; they act through
  `_NET_ACTIVE_WINDOW`, `WM_CHANGE_STATE`, `_NET_WM_STATE` and `_NET_CLOSE_WINDOW`.
- bridge-wm takes the activation from a window when the taskbar is pressed, so `WindowList` remembers which
  one was active at the press: clicking its button then minimizes it, and the next window takes over, as on XP.
- The look lives in `~/.config/win4wine/native-look.conf` (style, colour, size, wallpaper) and `linux-look.conf`
  (Qt and GTK style, icon theme), read by `qt-style/src/session.cpp` when `WIN4WINE_NATIVE_SHELL` is set: the shell,
  the Qt style in every Qt program and `xpstyle-gtk --watch` all watch them, so Apply needs nothing else. Without a
  prefix the system colours come from the style's `[SysMetrics]`, or Windows Classic's scheme.
- The Start panel is the only Qt popup. Its cascades are windows of the tooltip type, as Qt closes open popups
  when any other kind of window is shown, and it drives them from the mouse events it gets.
- The shell sets `QT_QPA_PLATFORMTHEME=generic` for itself: it is a desktop of its own, and the running desktop's
  platform theme has no place in it. Programs it starts get `gtk3`, as in the Wine session.
- A window's properties are watched by adding to the event mask this client already has on it: replacing it on the
  shell's own windows took Qt's `Expose` away, and raised windows stopped redrawing.
- `QFileSystemModel` asks for icons in a thread of its own, so it has no icon provider: `FolderModel` answers
  them in the GUI thread, from `fileicons`.
- It only wakes for X events, for files in the IPC directory, and when the clock's minute changes.

## The measurements (96 DPI)

From `docs/images/screenshot-en.png`, XP's real explorer, compared pixel by pixel. Parts' art has its edges
inside, so the rectangles are larger than what shows. The Start button in that picture is in its pressed frame,
because its menu is open.

| Rectangle | Value |
|---|---|
| Taskbar | 30 tall (Classic: 28) |
| Start button | y = 0, 32 tall (its last rows, baked taskbar colours, are under the screen's edge); the width from the style's margins, the flag, 2 pixels and the word |
| First task button | 5 pixels after the Start button's rectangle |
| Task button | y = 0, the bar's height (25 rows show, from y = 3), at most 160 wide; icon at (10, 7), text 3 pixels after it |
| Notification area | The art starts with 10 transparent pixels; icons from 16, every 18; 7 pixels to the clock, 17 after it |
| Window frame | 4 on three sides, 30 on top (the style's CaptionBarHeight + 5); buttons `side = top - 9` at the style's Offset; system icon at (6, 8). Maximized: the client at the work area's top + 26, the frame's borders off the screen |
| Start panel | 380 wide; user pane 64, bottom pane 40, the columns 190 each and at least 336 tall; places every 30 with 24-pixel icons; cascade rows 23 |
| Desktop icons | Cells of 75 x 75 |
