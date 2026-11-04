# Reverse-engineering status

This document tracks the parts of the native shell that were reverse-engineered from Windows XP's own
binaries and ported: Windows Media Player 9, Taskbar and Start Menu Properties, shell32's shortcut
menus (folder windows and the desktop), Windows Picture and Fax Viewer, and the frame manager's handling
of undecorated windows. For each component it records:

- what was recovered from XP's files, and from where (function, address, resource or string ID);
- what the port implements;
- what the original does that the port does not yet do.

No XP code runs. Resources are read from the user's own files at run time. The one deliberate exception
is the JScript of Windows Media Player's skins (RT_TEXT resources of `wmploc.dll` and the `.wmz` files),
which Qt's JavaScript engine interprets, in the same way the Search Companion's `.xml` steps are
interpreted.

Tools: retool (`info`, `find`, `xrefs`, `disasm`; capstone from its pipx environment for constant
scans), and REA's Ghidra provider for decompilation. Verification was done on an isolated rootful
Xwayland display, driven through XTEST and captured with python-xlib; the native session was checked
with `native-shell/session.sh`.

**Status legend**

| Mark | Meaning |
|---|---|
| Done | Recovered from XP's code or resources, ported, and tested |
| Partial | Ported in part, or derived from indirect evidence where the code was not located |
| Missing | Not ported |

---

## 1. Windows Media Player 9

Binaries: `wmp.dll`, `wmploc.dll`, `wmplayer.exe`. Port: [`wmplayer.cpp`](../native-shell/src/wmplayer.cpp),
[`mediaplayer.cpp`](../native-shell/src/mediaplayer.cpp), [`wmpengine.cpp`](../native-shell/src/wmpengine.cpp),
[`wmpskin.cpp`](../native-shell/src/wmpskin.cpp).

### Recovered

| Item | Source | Status |
|---|---|---|
| Full mode, Now Playing and its panes are skins, not native UI | RT_TEXT (type 256) of `wmploc.dll`: `MainAppSkin.wsz`, `NowPlaying.wsz`, `Viz.wsz`, `Video.wsz`, `Equalizer.wsz`, `MediaInfo.wsz`… | Done |
| Compiled skin format (`.wsz`): node and attribute records, value types | `wmp.dll` loader (`FUN_4b6312dd`, `FUN_4b635136`); all 30 compiled skins parse with no trailing bytes | Done |
| Element kinds (1 button … 17 automenu) and class IDs | Node kind field; `wmp.dll` REGISTRY scripts | Done |
| Attribute names (DISPID to name) | `wmp.dll` TYPELIB (MSFT format), read at run time | Done |
| `jscript:`, `wmpprop:`, `wmpenabled:` and `wmpdisabled:` prefixes | `FUN_4b630f93` | Done |
| Predefined `.wms` objects (PlayButton, SeekSlider, PlayElement…) and their default attributes | `wmp.dll` REGISTRY scripts under `MediaPlayer\Objects` | Done |
| `mediacenter` defaults: AppColorLight `#6071BF`, AppColorMedium `#394472`, AppColorDark `#212741`, ItemPlayingColor `#89E116`, ItemErrorColor `#FAAC4C`, PlaylistWidth 186, MetadataHeight 160, SettingsHeight 135, UserDisplayView `VizView`; ShowTitles 1, ShowEffects 1, ShowCaptions 0, SnapToVideo 1, VizAutoSelect 0, VideoZoom 100, CaptionsHeight 100, PlaylistHeight 100, SettingsWidth 100, UserWMPShowSettings 0, UserWMPShowMetadata 1 | Second argument of the registry getters (`sub_4b5de80d`, `sub_4b71b186`): each is a "push default, push name, call" sequence | Done; ShowPlaylist, ShowResizeBars and QuickHide have no getter found and are chosen by hand |
| Default album art: 520/521 any media, 525/526 audio, 527/528 video, 529/530 CD, 531/532 DVD (75 and 200 pixels) | `wmploc.dll` RT_BITMAP sizes and content; `MediaInfo.js` asks `helper.getCurrentArtID(large)` | Partial: the pairing is read from the bitmaps, not from `getCurrentArtID`'s code, which was not located |
| Plugin views (VizView, VideoView, BorderView, EQView, SRSView…) and their class IDs | `wmp.dll` `.data` table at `0x4b9b3130` | Partial: the view-to-skin mapping is inferred from names, not from the plugins' code |
| Event argument names (`NewState`, `Item`, `bstrItemName`…) | `_WMPOCXEvents` in the type library | Done (transcribed) |
| Main menu (File, View, Play, Tools, Help) and command IDs | Menu 102 of `wmploc.dll` | Done |
| Skin Chooser task | Dialog 203 of `wmploc.dll`; strings 1302–1320 | Done |
| `transparencyColor="auto"`: resolved to the picture's top-left pixel, whatever its colour | Parser and printer `FUN_4b61269c`, `FUN_4b621df8` (flags 0x40 auto, 0x80 none); resolution `FUN_4b60df5f`, which reads pixel (0, 0) through the DIB reader's GetPixel `FUN_4b61b905` | Done |
| Keyboard shortcuts: full mode and skin mode | Accelerator tables 102 (33 entries) and 4002 (12 entries) of `wmploc.dll` | Done |
| `customslider`: `image` is a strip of the slider at each value, one element width per frame; `positionImage` is a grey map from black (minimum) to white (maximum) | Derived from the skins' files (Pyrite: 399 px strip, 21 frames, 21 grey levels; Splat: 2599 px, 23 frames) | Partial: consistent with every shipped skin, but the drawing code in `wmp.dll` was not located |
| Sliders' `ondragend` event | Used by Revert and Splat to seek | Done |
| `helper.calcLayout` (how Now Playing's panes share the room) | Not located in `wmp.dll` | Partial: reimplemented from the limits `NowPlaying.js` documents |

### Implemented

- **Skin engine**: `.wsz`, `.wms` and `.wmz` documents; view, subview, button, buttongroup with mapping-image
  hit regions, buttonelement, text (scrolling, wrapping, `textWidth`), slider and progressbar,
  customslider (image only), video, effects, playlist, menu, automenu, plugin and taskcenter.
- **Custom sliders**: the frame for the value drawn from the strip, and the value read from the grey map
  under the mouse.
- **Evaluation rules**: `jscript:` values evaluated once at load, in document order, with the element's
  own properties in scope (a view's own `width` and `height` first, so `height="jscript:GetViewHeight();"`
  sizes its window); the player's objects found whatever their case (`mediaCenter`); `wmpprop:` bindings that track their dependencies, with write-back from
  sliders; alignment applied only to resizes after load; intrinsic sizes from images; window region
  built from the views' backgrounds; `clippingColor`, transparency colours, hue and saturation shifts;
  `moveTo` and `alphaBlendTo`; event handlers with an `event` object; `theme.loadString`,
  `loadPreference` (returning `"--"` when unset), `savePreference` and `openView`; view window requests;
  JScript indexed property assignments (`obj.prop(i) = v`); typed values for scripts.
- **Player**: playback through Qt Multimedia (play, pause, stop, previous, next, fast forward, seek,
  volume, mute, rate, shuffle, repeat), playlist, drag and drop, Now Playing (visualization, video,
  equalizer display, media information, playlist), the task bar, skin mode with all 20 XP skins, the
  Skin Chooser with rendered previews, return to full mode, the last mode restored at start-up, and the
  Play menu.

### Missing

| Original feature | Status |
|---|---|
| Media Library (tree, lists, search, auto playlists), native code in `wmp.dll` | Missing (the task shows its name only) |
| Media Guide and Radio Tuner (RT_HTML pages of `wmploc.dll`; the online services are gone) | Missing |
| Copy from CD, Copy to CD or Device, CD and DVD playback | Missing |
| Premium Services | Missing |
| Full-mode menu bar (show, hide, auto-hide) | Partial: `app.titleBarVisible` ("Show menu bar", `appautohidebtn`) puts the skin in a framed window under menu 102, remembered between runs; the auto-hidden bar does not appear on hovering the top |
| Visualizations other than Bars (`wmpvis.dll`: Ambience, Battery, Plenoptic, Spikes…) | Missing (an approximate FFT bar display stands in) |
| Audio effect of the equalizer, SRS WOW, crossfading, auto volume levelling | Missing |
| Full metadata (`WM/Lyrics`, `getItemInfoByType`) | Partial (title, artist, album, duration; album art from the file's tags or its folder's `Folder.jpg` / `AlbumArtSmall.jpg`, as `WMPImage_AlbumArtLarge` / `Small`) |
| `librarytree` element | Missing (created, not drawn) |
| `editbox`, `listbox` and `popup`: the members of wmp.dll's `IWMPEditCtrl` and `IWMPListBoxCtrl` | Partial: Qt's line edit (or plain text edit when multi-line) and list in the element's place; a popup (a list box with `popup` set, as `<popup>` compiles) is a menu of its items at the pointer. Their look is Qt's, not checked against WMP's |
| Full playlist control (columns, dragging, `dropDownList`, its shortcut menu) | Partial (selection, double-click to play, wheel scrolling) |
| Full-screen mode with `FullScreenTopLayout` and `FullScreenBottomLayout` | Partial: the display fills the screen under both layouts, which hide after 3 s unless `pinFullScreenControls`; `fullScreenRequest`, Alt+Enter, Esc and `view.returnToMediaCenter` enter and leave it. The Customize button's full-screen playlist (`showFullScreenPlaylist`) is not drawn |
| `theme.openDialog`; additional theme views beyond a plain window | Partial |
| Options, Properties and Statistics dialogs | Missing |
| Pixel comparison with the real WMP 9 running in Wine | Missing |

---

## 2. Taskbar and Start Menu Properties

Binary: `explorer.exe`. Port: [`taskbarproperties.cpp`](../native-shell/src/taskbarproperties.cpp).

### Recovered

| Item | Source | Status |
|---|---|---|
| The sheet (string 810), the Taskbar page (dialog 6) and the Start Menu page (dialog 205) | `explorer.exe` resources | Done |
| Taskbar preview: 145 when auto-hidden, otherwise 146 + 1 (unlocked) + 2 (not grouping) + 4 (no Quick Launch) | `FUN_01038be8`, `FUN_0103750d`; `{control, step}` table at `0x1038c7c` | Done |
| Notification area preview: 180 + 1 (no clock) + 2 (not hiding inactive icons) | Table at `0x1038c6c` | Done |
| Start menu preview: 170, or 171 for the classic menu | `FUN_01038a95`, `FUN_0103a4d5` | Done |
| Page initialisation (TaskbarGlomming, HideClock, policy restrictions) | `FUN_01039650` | Done for the four supported settings |
| Quick Launch: its buttons themed `TaskBar::Toolbar.Button` (the task buttons' art without an edge), 23 wide and the band's height, the icon at (3, 7); the task band 4 pixels after the last button | `luna.msstyles`; XP's taskbar in Wine, locked and unlocked (grippers at 104 and 164) | Done; the 4-pixel band gap is read from the unlocked bar's grippers, since that test bar showed no task buttons |
| Unlocked: `TaskBar.SizingBarBottom` (4 rows) above the bar, and a 10-pixel band header with `TaskBar::Rebar.Gripper` (6 wide, tiled down) before each band | XP's taskbar in Wine, unlocked: identical rows 734 to 767 apart from the icons | Done; saved as `Taskbar/TaskbarSizeMove`, from the sheet and the taskbar menu's Lock the Taskbar |
| Keep the taskbar on top: off, the bar is in the windows' order and comes up when clicked | XP's behaviour (ABS_ALWAYSONTOP) | Done: `_NET_WM_STATE_BELOW`, which bridge-wm honours; saved as `Taskbar/AlwaysOnTop` |
| The Toolbars submenu: browseui's band site menu 268 (Desktop 3, Quick Launch 4, New Toolbar 1) | `browselc.dll` | Done for Quick Launch; the others are greyed |
| Quick Launch's Show Text: one width for every button, the widest name's, the name 4 after the icon and 4 before the end, in TaskBar::Toolbar's white; Show Title: the folder's name at the band's start in TaskBar::Rebar's text colour (123 182 232) | XP's taskbar in Wine | Done: identical pixels |
| The band's menu (unlocked, on its gripper or title): View (Large Icons, Small Icons), Open Folder, Show Text, Show Title, Close Toolbar, then the taskbar's | `browselc.dll` menus 258, 256 and 257; XP's taskbar in Wine | Done; Large Icons is greyed |
| Hide inactive icons: TrayNotify's defaults, IconDemoteTimerTick 300000 ms (set at `0x101811d`), TrayIconResidentInterval 3600000 ms (`0x10180d2`), EnableAutoTray on | `explorer.exe` (`FUN_0101809f`) | Done |
| The chevron: `TrayNotifyHoriz::Button` (and `TrayNotifyHorizOpen` once open), centred on the area's visible edge, the icons 13 after that edge; tooltips 543 "Show hidden icons" and 542 "Hide" | `luna.msstyles`; `explorer.exe`'s preview 180 and strings | Done |
| Customize Notifications: dialog 7, its list (Name 1000, Behavior 1001; Current Items 1403, Past Items 1404) and the three behaviours (1002 to 1004), with Restore Defaults | `explorer.exe` | Done; items are known by their program (the tray host's `tray-names.tsv`), not their tooltip |
| Show Desktop: `Show Desktop.scf` with `[Taskbar] Command=ToggleDesktop`, explorer.exe's icon index 3 | XP's setup (`shell.inf`) | Done |
| Auto-hide: flags at tray `+0x44` (1 on, 2 hidden); a 500 ms check (timer 2) hides the bar when the pointer is off it; a move on the hidden bar wider than SM_CXDOUBLECLK x SM_CYDOUBLECLK arms timer 3 (50 ms), which shows it | `FUN_01030c59`, `FUN_01021ff7`, `FUN_0101f7be`, `FUN_01032b15` | Done |
| The hidden rectangle: the bar's top at the monitor's bottom less SM_CYFRAME / 2 (2 rows) | `FUN_0100a3cb`, `FUN_01030b04`; metrics stored at `0x101a0ae` | Done |
| The slide: the centre moved in proportion to the time, a frame each 20 ms, 400 ms hiding and 200 ms showing (`+0x4f0`, `+0x4f4`, set at `0x101858a`) | `FUN_01032865` | Done; the slide always runs (XP's runs only with menu animation on) |

### Missing

| Original feature | Status |
|---|---|
| Moving and resizing the unlocked taskbar (other screen edges, more rows) | Missing: unlocked, it shows its sizing bar and grippers but stays one row at the bottom |
| Show Quick Launch, and the Quick Launch toolbar itself | Partial: the band shows its folder's shortcuts (`.desktop`, `.scf`, links), saved as `Taskbar/QuickLaunch`, with Show Text, Show Title, its band menu, each item's menu (Open, Cut, Copy, Delete) and drops onto it (untested by script). Missing: Large Icons, the overflow chevron, reordering by dragging, and XP's rebar keeping the bands' earlier widths when Show Text or Show Title change (XP's shrank the band behind a chevron, or moved the task band to 764) |
| The Desktop, Links and New Toolbar bands, and deskbands such as Windows Media Player's | Missing (greyed in the Toolbars submenu) |
| Hide inactive icons and Customize Notifications (dialog 7) | Partial: the rule (an icon idle for TrayIconResidentInterval is hidden, checked every IconDemoteTimerTick) is read from the defaults and their names, not from the demotion code; the dialog lists names without icons, and the open chevron does not close by itself |
| Classic Start menu and both Customize dialogs (dialogs 9, 1036, 1135) | Missing |
| The "?" button and context help | Missing |

---

## 3. Shortcut menus in folder windows and on the desktop

Binary: `shell32.dll` (and `zipfldr.dll` for one string). Port: [`shellmenus.cpp`](../native-shell/src/shellmenus.cpp),
`FileBrowser::fillItemMenu` and `FileBrowser::fillBackgroundMenu` in [`filebrowser.cpp`](../native-shell/src/filebrowser.cpp),
and `DesktopIcons::showMenu` in [`desktopicons.cpp`](../native-shell/src/desktopicons.cpp).

> This component is not yet committed: it shares files with uncommitted work from another session.

### Recovered

| Item | Source | Status |
|---|---|---|
| Cut, Copy, Paste, Create Shortcut, Delete, Rename | Menu 210 (MENUEX; IDs 24, 25, 26, 16, 17, 18) | Done |
| Properties | Menu 211 (ID 19) | Done |
| Send To | Menu 223 (ID 79) | Done |
| Folder background: View, Arrange Icons By, Refresh, Customize This Folder, Paste, Paste Shortcut, Undo | Menu 215 | Done |
| Desktop background: menu 215 without View and Customize This Folder; menu 218 (Show Desktop Icons, Lock Web Items on Desktop, Run Desktop Cleanup Wizard) merged into Arrange Icons By | Menus 215 and 218 | Done |
| Restore, Empty Recycle Bin, Map and Disconnect Network Drive | Menus 226, 227 and 225 | Done |
| Verb names: Open 8496, Explore 8502, Search… 8503, Open With 5376 | `shell32.dll` strings | Done |
| Shortcut naming ("Shortcut () to %s.lnk") | String 4152 | Done (symbolic links stand in for `.lnk` files) |
| "Compressed (zipped) Folder" Send To target | String 10148 of `zipfldr.dll` | Done |
| "Desktop (create shortcut)" and "Mail Recipient" | Created by XP Setup; absent from the extracted files | Partial (English names) |

### Missing

| Original feature | Status |
|---|---|
| File-type verbs from the registry (Print, Edit, Play, Add to Playlist…) | Missing |
| Open With (submenu and the Open With dialog) | Missing |
| Context menu handlers from shell extensions | Missing |
| Send To: removable drives and the actual contents of the SendTo folder | Missing |
| Show in Groups, Choose Details, Customize This Folder | Missing (greyed) |
| Undo (shell32's undo stack) | Missing (greyed) |
| Full New submenu (Shortcut, Briefcase, Bitmap Image, WordPad Document, Wave Sound, Compressed Folder… from the registry's ShellNew keys) | Partial (Folder and Text Document only) |
| Auto Arrange in folder windows | Missing (greyed; implemented on the desktop) |
| My Computer: Manage | Missing |
| Recycle Bin Properties | Missing (greyed) |
| Real `.lnk` shortcuts | Missing (symbolic links) |

---

## 4. Windows Picture and Fax Viewer

Binary: `shimgvw.dll`. Port: `pictures::Viewer` in [`pictures.cpp`](../native-shell/src/pictures.cpp).

### Recovered

| Item | Source | Status |
|---|---|---|
| Toolbar for a picture: buttons, strip-105 images, commands (which double as tooltip string IDs), hidden fax and page buttons | TBBUTTON table at file offset `0x1700` | Done |
| Keyboard shortcuts | Accelerator tables 50 (picture) and 51 (multi-page) | Done (table 50) |
| Command dispatch for IDs 400–425 | `FUN_5cb16a08` → `FUN_5cb16169` | Done |
| Zoom In: ×1.2 per step, capped at 16× the picture's size | `FUN_5cb21bc6`; double 1.2 at `0x5cb03fa8` | Done |
| Zoom Out: ×0.833 per step; once the picture fits both the window and its own size, back to Best Fit | `FUN_5cb21c47`; double 0.833 at `0x5cb03fb0` | Done |
| Mouse wheel zooms (up in, down out) | Zoom window's wheel handler `FUN_5cb21d34` | Done |
| A click zooms about the clicked point, Shift reverses it; after Zoom Out a click zooms out; Ctrl or the middle button pans | Zoom window's button handler `FUN_5cb21e16`; mode switch `FUN_5cb210d9`; command handler `FUN_5cb14301` | Done |
| Strings (title 200, tooltips 400–425, Preview 550…) | String table | Done |
| Slide show | Strip 109, strings 600–604 | Done (ported earlier) |
| Background: COLOR_WINDOW for the window class and the zoom window; messages in COLOR_WINDOWTEXT | `GetSysColorBrush(5)` at `0x5cb06861`, `GetSysColor(5)` at `0x5cb202eb`, `GetSysColor(8)` at `0x5cb20ec7` | Done |
| Multi-page pictures: Previous Page (406), the page control (421, a toolbar control of style 0xd0), Next Page (407), and accelerator table 51 | TBBUTTON table; table 51 | Done (the page control's own text format was not located: "n / m") |

### Missing

| Original feature | Status |
|---|---|
| Print (Photo Printing Wizard) | Partial: printed through Qt's print dialog, the picture fitted to the page; the wizard itself is not ported |
| Help (`ImgPrev.chm`) | Missing (greyed) |
| Annotations (409–415, 402, 422, 419) | Missing |
| Margins and remembered window size | Partial (not checked against XP) |
| Animated images (GIF) | Done (played at their own frame delays) |
| Comparison with the real viewer running in Wine | Missing |

---

## 5. Frames for undecorated windows

Port: [`frames.cpp`](../native-shell/src/frames.cpp).

| Item | Status |
|---|---|
| Windows that ask for no decorations (`_MOTIF_WM_HINTS`) get no frame, matching bridge-wm | Done (checked in the native session with Windows Media Player) |
