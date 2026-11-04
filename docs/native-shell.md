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
| Desktop icons | Its shortcut menus are `shell32.dll`'s: menu 215 without View and Customize This Folder, Arrange Icons By with Name, Size, Type, Modified, Auto Arrange and menu 218 (Show Desktop Icons, the other two greyed); an item's as in folder windows, the Recycle Bin's Empty Recycle Bin (menu 227), My Computer's Search and menu 225 (greyed). Customize Desktop's Desktop Items. Recycle Bin (empty or full) in XP's lower right corner and the Desktop folder's files in XP's grid. Select, double-click, drag to place (remembered), rename, send to the trash, Arrange Icons |
| Taskbar | Luna or Classic: a button per window (normal, hot, active in bold, flashing), minimize by clicking the active button, a window menu, its strut. The buttons are sized and grouped as `explorer.exe`'s taskband does it (below): a group shows its count and its program's name, lists its windows in TaskBandGroupMenu's colours and has menu 210 (Cascade, Tile, Minimize Group, Close Group). Menu 205 on the taskbar and the notification area: Adjust Date/Time, Cascade, Tile and their Undo as user32 lays windows out, Show the Desktop and Show Open Windows, Task Manager (the desktop's system monitor). Taskbar and Start Menu Properties (menu 205's Properties, and the Control Panel's Taskbar and Start Menu): explorer.exe's sheet, its Taskbar page (dialog 6) and Start Menu page (dialog 205) with their pictures as explorer.exe picks them (the taskbar 146 + 1 unlocked + 2 not grouping + 4 without Quick Launch, 145 auto-hidden; the notification area 180 + 1 without the clock + 2 not hiding icons; the Start menu 170). Group similar taskbar buttons and Show the clock change this taskbar, from any process; the rest is greyed in the state this taskbar is in |
| Start menu | XP's two columns from the style's StartPanel parts: the user's picture and name, the default browser and mail, the six recent programs (those started here, then those the desktop's recent files name, then everyday ones and the rest for a new account), a line, the places, All Programs and My Recent Documents with their cascades, Log Off and Turn Off Computer. XP's keys (arrows, letters, Enter, Escape), access keys, selection and infotips (below) |
| Folder windows | A white line down the left edge and along the bottom, the menus, the toolbar from shell32's pictures, the address bar and Go, the Common Tasks pane from the shell style's art in each colour scheme, the Folders tree under the Desktop, the Search Companion, Favorites, web pages (with Qt WebEngine, otherwise in the desktop's browser), XP's warning on system folders, and Thumbnails, Tiles, Icons, List and Details (its sorted column shaded under a visual style) |
| Pictures | Thumbnails made in the background, a folder's first four pictures on its own; in the Details group the selected picture's preview and its dimensions, and for several items their count and total size, in shell32's words. Picture folders (My Pictures, or one holding mostly pictures, as XP decides) get Filmstrip and the Picture Tasks: View as a slide show and Set as desktop background. Filmstrip and the context menu turn pictures as Windows Picture and Fax Viewer does: saved under their own name, JPEG without loss through `jpegtran` when its size allows, after the viewer's question otherwise. The slide show is the viewer's, on black over the whole screen with its toolbar in the top right corner. The buttons and words come from `shimgvw.dll` |
| Command Prompt | `console.cpp`, in All Programs > Accessories and as Open Command Prompt Here: 80 x 25 in XP's raster font (`vgaoem.fon`) with the console's 16 colours and XP's buffer of 300 lines, running bash with the Wine session's `session/xp-cmd.bashrc`, the root of the system as a `C:` of its own (`C:\\home\\you>`). It understands what Linux's programs send to an xterm. Drag to mark, right click to copy or paste, as XP's QuickEdit |
| Notepad | `notepad.cpp`, in All Programs > Accessories and for text files in this session: `notepad.exe`'s menus, keys (`MAINACC`), words and its Go To and Page Setup dialogs, `comdlg32.dll`'s Find, Replace and Font, in XP's Lucida Console 10. ANSI (the locale's encoding here), Unicode, Unicode big endian and UTF-8 with their marks, chosen in the Open and Save As dialogs' Encoding line; lines end as the file's did. `.LOG` files get the time and date, Time/Date (F5), Word Wrap (which takes the status bar and Go To away), Edit's one level of undo, the status bar's two parts, Print with the header and footer codes (`&f`, `&p`, `&d`, `&t`, `&l`, `&c`, `&r`), files dropped on it, About Notepad (shell32's ShellAbout) |
| Calculator | `calculator.cpp`: Standard and Scientific are `calc.exe`'s dialogs 102 and 101, its menus 106 to 108 and its keys (table 105), the buttons' words from its strings and their colours as it paints them (numbers blue, operators and memory red, functions magenta). 32 significant digits with libquadmath (18 without), fixed while they fit in 32 places, `e+39` otherwise, as XP shows them. Standard works each operator out as it comes, Scientific has XP's precedence and 25 levels of parentheses; Inv and Hyp, degrees, radians and grads, Hex, Oct and Bin in Qword, Dword, Word and Byte (two's complement, F5 to F8 and F12, F2 to F4), memory, Copy and Paste, Digit grouping, the Statistics Box, its errors' words |
| Windows Media Player | `wmplayer`, a program of its own (`wmplayer.cpp`, `mediaplayer.cpp`, `wmpengine.cpp`, `wmpskin.cpp`): WMP 9's full mode is a skin, and so are Now Playing, its visualization, video, equalizer and media information panes: the compiled `.wsz` files of the user's `wmploc.dll` (below), laid out, drawn and scripted here as `wmp.dll` does it, with their own JScript run by Qt's JavaScript engine. Qt Multimedia plays (WMA, MP3, Ogg, FLAC, WAV, MIDI, AVI, WMV, MP4...): play, pause, stop, previous and next, seek, volume and mute, shuffle and repeat, the playlist, the taskbar of tasks, the colour of the player's art (hue and saturation, as `wmp.dll` shifts it), the window's own shape, moving and sizing it, the Play menu (menu 102 of `wmploc.dll`), files dropped on it. Skin mode with the `.wmz` skins of XP's Skins folder (`extract.py` takes all 20, by the names LAYOUT.INF or their `.wms` give them) and the user's `~/.local/share/win4wine/wmp-skins`; the Skin Chooser task is `wmploc.dll`'s dialog 203, with each skin drawn as its preview; View > Skin Mode and Full Mode, `view.returnToMediaCenter`, the mode it was left in. In All Programs, and for music and videos in this session (`xp-mediaplayer.desktop`) |
| Shortcut menus | `shell32.dll`'s own: an item's is its verbs (Open, Explore, Search..., words 8496, 8502, 8503; Open With; text files' Edit and Print; media Play), Send To (menu 223: Compressed (zipped) Folder as `zipfldr.dll` names it, made with `bsdtar`; Desktop (create shortcut); Mail Recipient with `xdg-email`; My Documents), then menu 210 (Cut, Copy, Paste into a folder, Create Shortcut, Delete, Rename) and 211 (Properties); a folder's background is menu 215 (View, Arrange Icons By with the folder's columns, Refresh, Customize This Folder, Paste, Paste Shortcut, Undo), New and Properties. A shortcut is a link named as string 4152 has it ("Shortcut to ..."). Desktop (create shortcut) and Mail Recipient are in English: XP's setup makes those files, no extracted file has their names |
| Picture and Fax Viewer | `pictures::Viewer` (`explorer --picture FILE`, Preview for pictures, and pictures in this session): `shimgvw.dll`'s window with its toolbar as its TBBUTTON table has it for a picture (Previous and Next Image, Best Fit, Actual Size, Start Slide Show, Zoom In and Out, Rotate, Delete, Copy To, Edit; Print and Help greyed), its strip 105, its tooltips, its keys (accelerator table 50), its zoom window's ways (Zoom In ×1.2 up to 16 times the picture, Zoom Out ×0.833 back to Best Fit, the wheel zooming, a click zooming about its point with Shift the other way, Ctrl or the middle button moving the picture), the folder's other pictures, the toolbar 8 pixels above the bottom and the picture 8 above it, and its last size and maximized state |
| File operations | Copy, cut and paste (with the desktop's other file managers), Move To and Copy To, drag and drop, a thread with a progress window and XP's replace question, the Recycle Bin with Restore and Empty, Delete and Shift+Delete, rename, new folder, new text document, Properties with the size counted and the drive's pie |
| Virtual folders | My Computer (with its groups and the drives' free space), the Recycle Bin, Control Panel, search results |
| Control Panel | Category view and Classic view, with XP's own names and icons. Display, Folder Options, Fonts, Taskbar and Start Menu, Network Connections, Add or Remove Programs (a list), Sounds and Audio Devices, System, Mouse, Keyboard, Printers, User Accounts, Date and Time |
| Title bars | Not for windows that ask for none (`_MOTIF_WM_HINTS` without decorations: Windows Media Player, splash screens), as bridge-wm leaves them. XP's frames for Linux windows, without Wine: caption, active and inactive, the three buttons, the system menu, moving and sizing from every edge, maximizing as XP does (the borders off the screen), rounded corners as the window's shape |
| Property sheets | `comctl32.dll`'s template 1006 (the tabs, and buttons of 50 x 14 dialog units, 4 apart and 4 from the edges), tabs on as many rows as they need with the selected row at the bottom, dialogs with only Close and "?" (`_NET_WM_CONTEXT_HELP`) |
| System Properties | XP's six pages from `sysdm.cpl`, `netid.dll`, `wuaucpl.cpl` and `remotepg.dll`: General and the computer's name are filled in, the rest is greyed |
| Display Properties | `desk.cpl`'s sheet: its caption and its pages' names, and its parameters (`--applet display,,1` as `control desk.cpl,,1`: Windows 9x's page numbers, `@` and a page's name, `/Action:OpenTheme /File:"..."`). The pages are `themeui.dll`'s dialogs, the Desktop page `shell32.dll`'s (dialog 29952, as desk.cpl takes it from shell32): Themes (Windows XP, Windows Classic, and those imported by Browse... from a `.theme` or a `.msstyles`, which Delete removes: the style and its `Shell` folder are copied to `~/.local/share/win4wine/themes`, and only their resources are read), Desktop (wallpaper, position, colour), Screen Saver (the X server's blanking), Appearance (visual style, colour scheme, font size), Settings (the size, through `xrandr` when it is there), and Linux (the Qt and GTK style, the icon theme), with previews. Apply changes everything at once, open programs included. Save As, Effects and Advanced are greyed, and the title bar has no Help button; Customize Desktop's Change Icon and Restore Default work |
| Qt and GTK programs | Drawn with the current theme by `qt-style/`, from the same choices, with XP's fonts |
| XP's icons | `session/icon_theme.py` makes the `win4wine-xp-icons` theme from the user's files the first time; it is the session's default for Qt and GTK programs too |
| Clock | The locale's short time, the date as its tooltip; wakes once a minute |
| Notification area | Linux tray icons from `bridge-wm/tray.py`'s files, left click, right click and their menus; in a window (`xp.sh native`) also those of the desktop around it, whose programs open on that desktop. XP's volume (`pactl`: its slider and Mute) and network (`nmcli`: its status, the wireless networks) icons, with the pictures of `sndvol32.exe` and `netshell.dll` |
| Windows key, Ctrl+Esc | Open the Start menu; Win+R is Run, Win+E My Computer, Win+F Search |
| Sounds | XP's own from `%windir%\\Media` (LAYOUT.INF's names): startup, logon, logoff, shutdown, the message boxes', the click on going somewhere, the Recycle Bin. The Search Companion's character's, from its `.acs` |
| Session screens | In a window, XP's Welcome screen first (no password: this user is logged in already), "Logging off..." and back; Log Off Windows and Turn off computer from `msgina.dll`, the screen fading to grey behind it |

Not ported yet: Windows Media Player's tasks that `wmp.dll` draws itself other than the Skin Chooser (Media Guide, Copy from CD, Media Library, Radio Tuner, Copy to CD or Device, Premium Services: their place shows the task's name), its menu bar, visualizations other than Bars, the equalizer's effect on the sound, DVDs and CDs, Notepad and Calculator's Help Topics (their `.chm`), XP's own Open and Save As dialogs (Qt's file dialog in XP's style stands in, with Notepad's Encoding line), Calculator's What's This?, XP's art behind a folder's thumbnail (no extracted file has it, so the folder's icon stands in its corner), Show in Groups and Choose Details, the Picture Tasks' printing and ordering prints, XP's zip folders (Send To makes them), Show in Groups, Auto Arrange and Customize This Folder (greyed), shortcuts (`.lnk`), network places, Help (shown disabled, as is Printers
and Faxes without `system-config-printer`), the Accessibility Options applets (the category is there, empty), the
Search Companion's sounds, its other characters and its balloon mode, Map Network Drive and Synchronize (greyed), the
the Start panel's own MFU ranking (UserAssist's counts and their decay are shell32's; here, launches counted by the shell), the taskband's extra rows when the buttons do not fit even grouped (its scroll arrows are
there, but a taller bar is not), the Start panel and classic Customize dialogs in Taskbar and Start Menu Properties (their Add/Remove/Clear lists are still greyed), the Screen Saver and Settings pages,
a wireless picture for the network icon. Strings that XP's files do not give
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

`explorer --explore FOLDER` opens only a folder window, `--applet ID` only an applet (`--help` lists them), `--notepad [FILE]` Notepad, `--calc` the Calculator. Both need `notepad.exe`, `calc.exe`, `comdlg32.dll` and `lucon.ttf` from the media: an extraction from before them needs running again. Windows Media Player is a program of its own beside it, as on XP: `wmplayer [FILE...]` (it needs `wmploc.dll` and `wmp.dll`, and Qt Multimedia and Qt Qml at build time).
In this session `xdg-open` brings folders to `--explore` and text files to `--notepad`, through `xp-explorer.desktop`, `xp-notepad.desktop` and
`~/.config/xp-shell-mimeapps.list`, which only this desktop reads.

In the Wine session: build it (`native-shell/build.sh`) and set `"native_shell": true` in `session.json`. The
supervisor then starts it instead of XP's explorer, and Wine's proxy.exe draws the frames. This mode has not been
run yet: it needs a Wine build.

## The Control Panel's category view

It is what `shell32.dll`'s UIFILE 4 lays out and the shell style's UIFILE 2 (`blue_ss.dll`, `home_ss.dll`,
`metal_ss.dll`) draws, whose colours are read from that file at run time:

| Element | Value in the blue style |
|---|---|
| The view | `rgb(99,117,214)`, on its left a one-pixel gradient `rgb(148,187,255)` to `rgb(99,117,214)` |
| Watermark, in the lower right corner | Bitmap 26, the Control Panel's own picture |
| Directive ("Pick a category") | Franklin Gothic Medium, 21 pt, `rgb(214,223,245)`, in GDI's made-up bold (drawn twice a pixel apart, each letter a pixel wider); at the top of the container's padding of 20, 30 px above a second one |
| Links | Tahoma 8 bold, white, underlined under the pointer; 12 between items, none at the container's edge, 1 of padding, 5 before the text |
| Icons | 48 pixels for a category (five rows, the first column first), 32 for an applet, 16 for a task |
| A category's banner | 35 tall, gradient `rgb(51,102,204)` to `rgb(99,117,214)`, title at 50, its icon at (10, 7) over the edge |

A category is an address of its own (`::controlpanel/N`), so Back and Up work. Its tasks are shell32's words, for the
tasks that do something here; Add or Remove Programs goes straight to its applet, as on XP. Under Windows Classic
the view takes the system colours.

## The warning on system folders

`/` (XP's system drive) and `/usr`, `/etc`, `/boot` and `/opt` (XP's Windows and Program Files) first show "These files
are hidden.", as `shell32.dll`'s UIFILE 3 lays it out and the shell style's UIFILE 1 draws it, over bitmap 25, the
computer. Showing the contents is remembered for each folder, and the pane's System Tasks can hide them again.

## The Search Companion

Its steps are the `.xml` files of XP's `srchasst` folder, built into widgets at run time; the icons and the buttons'
words come from `srchui.dll`. A web search opens Bing in the folder window; `inetpref.xml`'s dead engines are left out
of the choice. `acs.cpp` plays the character from the user's own `rover.acs`, which `extract.py` takes from the media:
nothing of it is in this repository, and without the file the bar simply has no character. An extraction from before
this needs running again (`./xp.sh native /path/to/WindowsXPSP3.iso`).

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

## Windows Media Player's skins

`wmp.dll` builds its full mode from skins: `MainAppSkin.wsz` (the frame, the taskbar of tasks, the transport
controls), `NowPlaying.wsz` in its taskcenter, and in Now Playing's plugins the views its table names
(`VizView` is `Viz.wsz`, `VideoView` `Video.wsz`, `EQView` `Equalizer.wsz`, `MediaInfoView` `MediaInfo.wsz`...),
all in RT_TEXT (type 256) of `wmploc.dll` with their scripts (`MainAppSkin.js`, `#142`, `#132`...). They are
compiled `.wms` files:

| Part | Layout |
|---|---|
| A node | u16 offset of its next sibling (0 for the last), u16 offset of its first child, u8 kind (bit 7: a CLSID follows the name), u8 children, u8 attributes, its name in UTF-16 (`player`, `mediacenter`: the objects whose events it handles), the CLSID |
| Kinds | 1 button, 2 buttongroup, 3 buttonelement, 6 playlist, 7 text, 8 slider, 9 video, 10 effects, 11 theme, 12 view, 13 subview, 14 plugin, 15 menu, 16 taskcenter, 17 automenu; a CLSID names the rest, as `wmp.dll`'s REGISTRY scripts have them |
| An attribute | u16 offset of the next one, u8 type, a DISPID of the element's interfaces in `wmp.dll`'s type library (`0x7d4` width, `0x8fd` backgroundColor, `0x14b4` image...) or, for type & 0x1f = 0, its name; then its value: UTF-16 (type & 8), a string of `wmploc.dll` (type & 0x10, `upToolTip`), a u32 (type & 4) or a VARIANT_BOOL (type & 1). Type 0x80 is a `jscript:` value without its prefix, 0x40 a `wmpprop:` one, 0x20 keeps `wmpenabled:` |

What `wmp.dll` does with them, followed here: `jscript:` values are worked out once at load, with the
element's own properties in scope (`width="svEntireApp.width - left - 9"`); `wmpprop:` values follow what they
read; a size changed after the load moves or stretches children by their `horizontalAlignment` and
`verticalAlignment`; an element without a size takes its picture's; the window's region is its views'
backgrounds, what buttons draw outside them is not seen; colours may be `none` or `auto` (a button's default:
`wmploc.dll`'s pictures have magenta where they are not). `mediacenter`'s defaults are `wmp.dll`'s
(`AppColorLight` `#6071BF`, `AppColorMedium` `#394472`, `AppColorDark` `#212741`, `ItemPlayingColor` `#89E116`,
`PlaylistWidth` 186, `MetadataHeight` 160, `SettingsHeight` 135, `UserDisplayView` `VizView`), found as the
second argument of its registry getters. `helper.calcLayout`, which shares Now Playing out between its panes, is
not located in `wmp.dll`; here it follows the limits `NowPlaying.js` says it shares with it.

`.wms` skins (a `.wmz` is a zip of one with its pictures and scripts) are the same elements in XML, with
the objects `wmp.dll`'s REGISTRY scripts register under `MediaPlayer\Objects`: PlayButton, SeekSlider,
VolumeSlider, CurrentPositionText, ReturnButton and the rest are buttons, sliders and texts with the
attributes those scripts give them (`onclick="player.controls.play()"`, `enabled="wmpenabled:player.controls.play"`),
and a buttongroup's PlayElement, StopElement... are its buttonelements. Their attributes are text, which
scripts see as numbers and booleans, worked out in the file's order; `theme.loadPreference` gives "--" for
what was never saved; a view without `titleBar="false"` has the system's frame.

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

Measured on XP's own browseui, comctl32 6.0.2600.5512, shell32 and sysdm.cpl, which the Wine prefix
loads natively for `xp-explorer.exe` (its AppDefaults DllOverrides), captured losslessly at 1024 x 768:

| Rectangle | Value |
|---|---|
| Folder window | 800 x 600 when nothing is saved (its bag's `WinPos1024x768`: 44,58 to 844,658) |
| Toolbar band | 38 tall, then the band border's two lines; first button 2 in. With words: 3, picture 24, 4, words, 8. Without: 4, 24, 4. Back: 7, 24, 4, words, 7, then 13 for its arrow; Forward: 8, 24, 6, then 13. Views: 4, 24, 2, a 5 x 3 arrow, 4. Separators 6 wide, the line 2 in |
| Address band | 22 tall, the combo box filling it; Go (shell32's bitmap 230, hot 231, 20 x 20) 2 after it, 7 from the edge |
| Menu band | shell32's menu band, a comctl32 toolbar of text-only buttons: each its words and 16 (the words 8 in), 19 tall and 2 under the band's top, the first 2 in from the client's edge. "Address" in the style's Rebar TextColor (127 124 115 in Luna) |
| Task pane | A section's header the shell style's rcbmp(112 or 110, 6, ...): a nine-grid of 6 over its 25 rows; the title 13 in, centred under the header's top border of 2; the arrow 2 from the right; a task's words 7 after its icon. File and Folder Tasks has the ordinary header (only System Tasks and the like have the blue one), Details starts collapsed, as XP's own shell32 shows them |
| Tiles, groups | Tiles 218 x 56, icon 16 in and 4 down, words 69 in; a group's words 14 in, its line 17 down, 300 long from GradientActiveCaption to white; 31 to the first tile, 12 between groups |
| Start panel | Both columns 7 under the user pane; places' separators 5 into their 8; the body ends 7 under the last place; All Programs 36 under its line, the arrow at its own size 11 after the words (P underlined); Log Off and Turn Off are picture, 4, words, 11 apart and 10 from the edge. Set Program Access and Defaults (moricons.dll icon 114, on two lines) and Connect To (shell32 icon 175, explorer.exe string 8233) |
| Property sheet tabs | Laid 2 inside the control, the selected one 2 wider each side; System Properties has srrstr.dll's System Restore page (dialog 11), so its tabs make XP's 4 + 3 rows |
| Start button | Luna's "Franklin Gothic Medium, 14, italic", emboldened by half a pixel (XP's word has a fifth more ink than the plain face, in its width), a pixel above the middle |
| Classic Start panel | 380 x the body, on the Start button's top left corner (2 in, over the bar's top 4 rows): a raised edge of 2; the caption's gradient 3 in and 40 tall, the name in bold italic and no picture; both columns in the face colour, an etched line between them at 193; the places 2 under the header, 4 further in than Luna's; All Programs 32 tall in plain words with the menus' arrow 13 after them; the bottom bar 37 tall under an etched line, Log Off's and Turn Off's pictures the 16-colour ones at the VGA palette's half strength (olive, maroon) |
| Menus and tooltips | XP's drop shadow: a layered SysShadow window five pixels right of and below the popup (win32k's xxxAddShadow), its profile measured on XP: 0.442, 0.328, 0.168, 0.055 and 0.012 of the light across the five pixels, fading from 0.113 to 1 along the edge and 0.882 to 0.018 at the corner. An override-redirect window beneath each menu, combo list, tooltip and group list, darkened from what lies under it; the Start panel has none. `Effects/DropShadow` (native-explorer.conf) and `WIN4WINE_NO_MENU_SHADOW` turn it off |
| Caption animation | Minimizing, maximizing and restoring, as user32's DrawAnimatedRects(IDANI_CAPTION): 250 ms (CMS_ANIMATION), a strip one pixel shorter than SM_CYCAPTION in the classic active caption's gradient with the small icon 2 in and the title 2 after it, moving between the top left corners and stretching between the widths inside the frames' borders, to and from the taskbar button. XP has none for closing. `WIN4WINE_NO_ANIMATION` turns it off |

Windows Classic's colour schemes are XP's: `native-shell/extract.py` turns the user's `HIVEDEF.INF` and `themeui.dll`
into `schemes.conf` as XP's themeui does at first logon (`prefix/xp_schemes.py`), 22 schemes in their sizes with
their 31 colours. Display Properties lists them under Windows Classic style, and Apply writes the chosen one's
colours into `native-look.conf` as `classic.<colour>`, which the shell, Qt and GTK programs read. The Windows Classic
theme is Windows Standard, as XP's. Under Windows Classic the Common Tasks pane stays blue: without a visual style
XP's shell32 takes system32's `shellstyle.dll`, the same file as Luna's `blue_ss.dll`.

### The taskband, from explorer.exe's code

Read with `retool` (disassembly only) in the user's `explorer.exe` 6.0.2900.5512 and `user32.dll`, and checked
against XP's explorer in the Wine prefix (12, 11, 7 and 5 Notepad windows, captured losslessly at 1024 x 768):

| What | XP's rule |
|---|---|
| Button width | The band's width over the number of buttons (less `cxButtonSpacing` with more than one), at most SM_CXMINIMIZED (160) |
| Band | From the first button (x = 104 with Luna's Start button) to 3 pixels before the notification area's rectangle (743 wide with the area at 850; the measures allow 742 and 4) |
| Spacing | TB_SETMETRICS: `cxButtonSpacing` 0 and `cxPad` 20 with a visual style, 3 and 8 without |
| Grouping | `TaskbarGlomming` (on unless set to 0 or SHRestricted's NoTaskGrouping). When the width comes to (SM_CYSIZE + 2 SM_CXEDGE) x 1.8, truncated, plus 8 x `tmAveCharWidth` of the toolbar's font or less (92 with Luna), the program with at least two windows whose most recent one was chosen least recently gets one button; again until they fit. Then they shrink, down to the first term alone (52) |
| Ungrouping | When (band - buttons x that width) / that width is 2 or more, the group with fewer windows than that, chosen most recently, comes apart |
| `TaskbarGroupSize` | 0 (default) as above; 1: the program with the most windows first (and the group with the fewest taken apart when room comes); 2 or more: every program with that many windows, kept grouped |
| Programs | A window's program file (each `rundll32.exe` window on its own); here WM_CLASS's class, named by the XP file's FileDescription (`notepad.exe`: "Notepad") or the desktop entry |
| "Chosen" | When the window got its button, or was activated from it (just before `SwitchToThisWindow`): not every activation |
| Group button | Icon, then the count centred in a gap as wide as the count with a "0" more, in `TaskBand.GroupCount` (Tahoma 8 bold, 255 204 109), then the name; comctl32's drop-down arrow (drawn here as a 5 x 3 triangle 4 after the words, not measured) |
| Group list | A menu band at least as wide as the button, over it, in `TaskBandGroupMenu`: the toolbar's content margins (9, 0, 0, 0) and each button's (9, 0, 3, 3) around the 16-pixel icon, so 22-pixel rows with Luna |
| Taskband scrollbar | When the buttons do not fit at their least width, a themed vertical scrollbar 17 wide at the band's right (`TaskBand::Scrollbar.ArrowBtn`, 17 x 17, 16 states) and the buttons in pages of `view / button width`; the arrows, the wheel and the keyboard scroll a page. At 30 pixels the two arrows take the whole bar, as a standard scrollbar does; the thumb and track would need a taller bar |
| Menu 205 | Adjust Date/Time, Customize Notifications and their line only from the notification area; "&Undo %s" (534) with Cascade (535) or Tile (536) after one; Show the Desktop as "&Show Open Windows" (850) while it shows; Cascade and Tile greyed without a window; Task Manager greyed by DisableTaskMgr |
| Cascade | user32's CascadeWindows on the work area: steps of SM_CXFRAME + SM_CXSIZE and SM_CYFRAME + SM_CYSIZE (22 x 29 with Luna, whose style leaves CaptionWidth at 18), as many as a third of the height holds; every window the area less those steps; the bottom window at the corner, wrapping after steps + 1 |
| Tile | user32's TileWindows: S = 1 under 4 windows, else one less than the smallest E with E x E > N; Horizontally (MDITILE_HORIZONTAL) S columns of N / S rows, Vertically N / S columns of S rows; the last N mod S columns a row more; the topmost window first |

### The Start panel's keys and selection

`explorer.exe`'s DV2ControlHost routes the arrows between the panes: the focused pane moves first, and at
its edge the host takes the next pane round them all (GetNextDlgGroupItem) whose rectangle holds the
item's height (Left, Right) and selects the item nearest it; Right opens a cascade, Left and Right swap
under a right-to-left layout. `Start_MinMFU` is 6 when unset. The rest was watched on XP's own explorer in
the Wine prefix, key by key, captured losslessly:

| What | XP's |
|---|---|
| Opening | Nothing selected; Down takes the first item, Up the last (Turn Off Computer) |
| Up, Down | Through a pane, then the next round them all: programs, All Programs, places, Log Off and Turn Off; entered at the first item going down, the last going up |
| Left, Right | The other column at the nearest item (round them too); Right opens My Recent Documents' or Connect To's cascade, with its first entry selected; Left and Escape close the last cascade, the item that opened it staying selected |
| Letters | An item's access key, or the first letter of one that has none, in every pane: alone, it runs or opens its cascade; with others, the next is selected |
| Access keys | explorer.exe's names: My Recent &Documents (8236), &Control Panel (8234), Connect &To, &Help and Support, &Search, &Run..., &Log Off, T&urn Off Computer; shell32's have none. Underlined only when the keys opened the panel or once a key is pressed; All &Programs' always |
| Selection | COLOR_HIGHLIGHT, not the style's HotTracking, for the pointer as for the keys: 2 inside the row, 8 in from the programs' pane and 6 from its right (a program's icon 2 inside it, its words 36 after), 6 and 8 in the places; All Programs' 8 in and 6 from top and bottom. Log Off and Turn Off: filled 3 around the picture and 8 after the words with the keys, the picture lightened under the pointer; their pictures and words a pixel above the bar's middle, All Programs' words too |
| Infotips | explorer.exe's strings (300 to 307, 7000 to 7005, 7015, 7016) and the programs' comments; none for All Programs. With the keys, the tip's border at the item's words and just under the selection; for Log Off and Turn Off, 7 back from the selection's right; with the pointer, at the pointer |
| (Empty) | A greyed entry that is never selected |

### Font rendering without smoothing

The native shell and Qt widgets share `qt-style/src/fontpolicy.h`: full hinting,
no kerning and monochrome glyphs at every UI size, including large headings.
The themed Start button alone uses grayscale antialiasing for its Franklin
Gothic label. Other text stays monochrome; subpixel antialiasing is disabled.

XP source references (under `Source/XPSP1/NT` in the reference checkout):
- `windows/core/ntgdi/gre/lfntobj.cxx`, around line 191: `FE_AA_ON` and the
  requested quality gate `FO_GRAY16`; `NONANTIALIASED_QUALITY` excludes smoothing.
- `windows/core/ntuser/kernel/rare.c`, around line 1924: `SPI_SETFONTSMOOTHING`
  toggles `FE_AA_ON` independently of the smoothing type.
- `shell/cpls/plus/store.cpp`, around line 390: the Effects dialog saves the
  smoothing type and its enabled state separately. Standard smoothing and
  smoothing disabled are different settings.

Appearance's Normal, Large and Extra Large sizes supply menu, message, status
and desktop icon fonts. The Start panel grows for larger labels. Registry
LOGFONT heights remain device pixels when the prefix DPI differs from the host.
`tests/native-fonts.cpp` checks that small labels and large headings have binary
pixel coverage, with no grayscale edges or colour fringes. Build and run
instructions are at the top of that file.

### Explorer layout verification

`tests/native-explorer-layout.py OUTPUT_DIR FIXTURE_DIR --verify` links a UI
probe against the native-shell build. Run it on an isolated X11 display with
`WIN4WINE_NATIVE_SHELL=wineless`, the extracted assets in `WIN4WINE_ASSETS`, and
`.local/qt-style` in `QT_PLUGIN_PATH`. Its settings are isolated in a temporary
configuration directory. It captures actual widgets and draws their outer
frame with the shell's own `drawFrame` function.

At 96 DPI with Tahoma 8, List and Details rows are 17 pixels and the report
header is 20, as comctl32 v6 makes it (its art is 18): the words 12 in from a
column's left, or ending 7 before its right for Size; the sort arrow a 9 x 5
triangle in COLOR_BTNSHADOW, 9 after the words and 7 down, pointing up for an
ascending sort (qt-style draws the header so for every Qt program). List columns grow to fit names. Selection paints the name
instead of the whole report row; losing keyboard focus uses the inactive
selection colours. Blank icon-cell margins and blank label space are not item
hits. The native selection marquee uses XP's alpha 70 and highlight outline.
My Computer supports Tiles, Icons, List and Details; its report includes type,
total capacity and free space, with numeric capacity sorting. Classic Control
Panel uses Name and Comments instead. View changes preserve item selection.

The probe checks menu targeting, multiple selection, native menu row heights,
report geometry, virtual-folder views, the marquee pixels, and caption space
when min/max buttons are absent. The formulas are compared to XP's
`comctl32/v6/listview.c`, `lvlist.c`, `lvrept.c`, `lvtile.c` and `header.c`.
