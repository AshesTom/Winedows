# Reverse-engineering status

This document tracks the parts of the native shell that were reverse-engineered from Windows XP's own
binaries and ported: Windows Media Player 9, Taskbar and Start Menu Properties, shell32's shortcut
menus (folder windows and the desktop), Windows Picture and Fax Viewer, the frame manager's handling
of undecorated windows, the taskband and its menus, the Start panel's keys, the notification area and
the folder windows' bands, the caption animation (`win32k.sys`), desktop icons, file Properties, the menus' drop shadows and the Windows key's hotkeys. Section 13
lists what explorer.exe does that is not ported yet. For each component it records:

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
| Media Library (tree, lists, search, auto playlists), native code in `wmp.dll` | Partial: the task scans the Music folder into a tree (All Music, Auto Playlists with Recently Added, Recently Played and Most Played, Artist, Album) and a Title/Artist/Album/Length list, filters with the Search box and plays on double-click; plays are counted and timed in the player's settings. Auto Playlists can also hold named queries (title/artist/album contains), saved in `library/autoPlaylists`, under `wmploc.dll`'s words (New Auto Playlist 14163, Auto Playlists 2507, 14168); its editor's other attributes and restrictions (14170), wmp.dll's own columns and query syntax are not there |
| Media Guide and Radio Tuner (RT_HTML pages of `wmploc.dll`; the online services are gone) | Partial: their offline RT_HTML pages (`OFFLINE_MEDIAGUIDE.HTM`, `OFFLINE_RADIOTUNER.HTM`) are read from `wmploc.dll` and shown in a text browser, with their words and pictures; their scripts and online links are not run |
| Copy from CD, Copy to CD or Device, CD and DVD playback | Partial: the CD task page says to insert an audio CD; no drive is enumerated and no copy/playback is done |
| Premium Services | Partial: its `OFFLINE_SUBSCRIPTIONS.HTM` page is read and shown; its scripts and online links are not run |
| Full-mode menu bar (show, hide, auto-hide) | Done: `app.titleBarVisible` ("Show menu bar", `appautohidebtn`) puts the skin in a framed window under menu 102, remembered between runs; `app.titleBarAutoHide` (`titleBarAutoHide`) hides the bar, the pointer within the skin's top 3 pixels brings it back, and 500 ms after leaving the bar/skin it hides again (the 3 pixels and 500 ms are not read from wmp.dll or measured: unverified) |
| Visualizations other than Bars (`wmpvis.dll`: Ambience, Battery, Plenoptic, Spikes…) | Partial: `wmpvis.dll` is extracted for its effect names (Musical Colors, Night Lights, Colors in Motion, Aurora, Rhythmic Colors, Star Power, Soft Fire, Silky Wave, CutOut, Rolling Fire, Water Spray, Blue Flame, Yellow Swirl, Critter Rock, Electric Rainbow, Neon Highway, Ice Crystals); Options lists them and each is drawn with the nearest of Bars, Spikes, Ambience, Battery or Plenoptic. `wmpvis.dll`'s own rendering code is not run |
| Audio effect of the equalizer, SRS WOW, crossfading, auto volume levelling | Partial: Options' Equalizer... shows ten bands with Flat/Rock/Pop/Jazz/Classical presets and SRS WOW/TruBass boxes, saved; they do not filter the sound yet. Crossfading and auto volume levelling are missing |
| Full metadata (`WM/Lyrics`, `getItemInfoByType`) | Partial: title, artist, album, duration, genre, year, track, composer, bitrate and file size, with `getItemInfoByType` and a ten-name `getAttributeName`; `WM/Lyrics` falls back to the Comment tag (Qt has no Lyrics key); album art from the file's tags or its folder's `Folder.jpg` / `AlbumArtSmall.jpg`, as `WMPImage_AlbumArtLarge` / `Small` |
| `librarytree` element | Missing (created, not drawn) |
| `editbox`, `listbox` and `popup`: the members of wmp.dll's `IWMPEditCtrl` and `IWMPListBoxCtrl` | Partial: Qt's line edit (or plain text edit when multi-line) and list in the element's place; a popup (a list box with `popup` set, as `<popup>` compiles) is a menu of its items at the pointer. Their look is Qt's, not checked against WMP's |
| Full playlist control (columns, dragging, `dropDownList`, its shortcut menu) | Partial: selection, double-click to play, wheel scrolling, dragging a row to reorder (`player.playlistmove`), the right-click menu's Remove from Playlist and Clear Playlist, and Title/Artist/Album/Duration columns when the width allows. `dropDownVisible` draws the drop-down selector at the right edge; its menu lists the current playlist and All Music (which fills the playlist from the Music folder), while `dropDownList=showCD` or `showRadio` shows an unavailable entry |
| Full-screen mode with `FullScreenTopLayout` and `FullScreenBottomLayout` | Partial: the display fills the screen under both layouts, which hide after 3 s unless `pinFullScreenControls`; `fullScreenRequest`, Alt+Enter, Esc and `view.returnToMediaCenter` enter and leave it. The Customize button's full-screen playlist (`showFullScreenPlaylist`) is drawn as a black 300 px pane down the right side, its items white and the playing one green, and a double-click plays one; it is not wmp.dll's own drawing |
| `theme.openDialog`; additional theme views beyond a plain window | Partial |
| Options, Properties and Statistics dialogs | Partial: Tools > Options opens a basic sheet (Shuffle, Repeat, Mute, Volume, Visualization, Equalizer..., Statistics...); Properties shows the current item's Title/Artist/Album/Duration/Location, play state and volume. Statistics counts each track's plays and last play from the player's history and can reset them; wmploc.dll's own options pages and counters are not read |
| Pixel comparison with the real WMP 9 running in Wine | Partial: XP's `wmplayer.exe` runs under Wine and shows the same skin, task list and controls; a full pixel diff is not automated, and folders/preferences were not checked side by side |

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
| The Toolbars submenu: browseui's band site menu 268 (Desktop 3, Quick Launch 4, New Toolbar 1), the deskbands inserted above it in the order the component categories list them, by CLSID (`sub_30035899`, `sub_30034aad`) | `browselc.dll`, `browseui.dll` | Done: Windows Media Player and Links (IE's deskband) first, then Desktop, Quick Launch and New Toolbar (a folder chosen with a directory dialog, `Taskbar/CustomToolbar`) |
| Quick Launch's Show Text: one width for every button, the widest name's, the name 4 after the icon and 4 before the end, in TaskBar::Toolbar's white; Show Title: the folder's name at the band's start in TaskBar::Rebar's text colour (123 182 232) | XP's taskbar in Wine | Done: identical pixels |
| The band's menu (unlocked, on its gripper or title): View (Large Icons, Small Icons), Open Folder, Show Text, Show Title, Close Toolbar, then the taskbar's | `browselc.dll` menus 258, 256 and 257; XP's taskbar in Wine | Done |
| Quick Launch's View > Large Icons: the 32-pixel icons, a button every 39 pixels (32 + 7, as 16 + 7 for the small ones), the icon 3 in and 7 down; the bar grows from 30 to 46 rows (the icon and 14, as 16 and 14 make 30), under the unlocked bar's 4 sizing rows | XP's taskbar in Wine, unlocked, View > Large Icons on the Quick Launch band: icons at 117 and 156, 32 wide, from row 729, the bar's top at 718 (50 rows with the sizing bar) | Done: identical bar top and icon places (an earlier port used 24-pixel icons in 44-pixel buttons, which XP does not). Windows Classic's height not measured. Saved as `Taskbar/QuickLaunchLargeIcons` |
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
| Moving and resizing the unlocked taskbar (other screen edges, more rows) | Partial: dragging the unlocked bar's sizing bar moves it between the bottom and top edges (`Taskbar/Position`) or, staying on the edge, gives it two or three rows (`Taskbar/Rows`); the Start menu, menus, auto-hide and struts follow, and the task buttons wrap over the rows. The left and right edges are not there |
| Show Quick Launch, and the Quick Launch toolbar itself | Partial: the band shows its folder's shortcuts (`.desktop`, `.scf`, links), saved as `Taskbar/QuickLaunch`, with Show Text, Show Title, its band menu, each item's menu (Open, Cut, Copy, Delete) and drops onto it (untested by script). Missing: the overflow chevron, reordering by dragging, and XP's rebar keeping the bands' earlier widths when Show Text or Show Title change (XP's shrank the band behind a chevron, or moved the task band to 764) |
| The Desktop, Links and New Toolbar bands, and deskbands such as Windows Media Player's | Partial: the Desktop band lists the Desktop folder's files, Links lists `Favorites/Links` and New Toolbar a chosen folder, as 16-pixel buttons (`Taskbar/DesktopBand`, `Taskbar/LinksBand`, `Taskbar/CustomToolbar`). Windows Media Player's deskband is done (see the row in section 13); other deskbands (Address, Language bar) are missing |
| Hide inactive icons and Customize Notifications (dialog 7) | Partial: the rule (an icon idle for TrayIconResidentInterval is hidden, checked every IconDemoteTimerTick) is read from the defaults and their names, not from the demotion code; the dialog now shows Current Items' icons (Past Items have none), and the open chevron closes on a click elsewhere on the bar (but not in another application) |
| Classic Start menu | Done (section 14) |
| Both Customize dialogs (dialogs 9, 1036, 1135) | Partial: the classic Start menu's dialog 9 and the Start panel's property sheet (General 1135, Advanced 1036) open from Taskbar and Start Menu Properties. Dialog 9's four tree options work; the Start panel's places tree, program count, icon size, recent documents and hover-open are saved and read. Add and Remove manage `~/.local/share/win4wine/Start Menu` (the classic menu lists its items above the line, and dragging one reorders them in `StartMenu/ClassicOrder`); Advanced is greyed, Sort by Name clears that order, and Clear List clears the Start menu's launch counts and makes the recent documents list empty from then on (`StartMenu/RecentCleared`) |
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
| Shortcut naming ("Shortcut () to %s.lnk") | String 4152 | Done: the name ends in `.lnk` and `fileops::createShortcut` writes a Shell Link |
| "Compressed (zipped) Folder" Send To target | String 10148 of `zipfldr.dll` | Done |
| "Desktop (create shortcut)" and "Mail Recipient" | Created by XP Setup; absent from the extracted files | Partial (English names) |
| The copy and move progress dialog: shell32's dialog 1020 (animation 106, file 102, "From '%1' to '%2'" 5968 in 103, bar 104, time left 105, Cancel), captioned 5953 + the operation (Moving..., Copying...). Delete: caption 5955 (Deleting...), "From %1" 5969. Its AVI on WM_SHOWWINDOW: move 160, copy 161, delete to the Recycle Bin (FOF_ALLOWUNDO) 162, delete for good 164, and 163 when the progress title is 9025 or 9026, the Recycle Bin's own: emptying it (`sub_300659ff`, FOF_SIMPLEPROGRESS, title 9025 "Emptying the Recycle Bin") and deleting its items (FOF_SIMPLEPROGRESS | FOF_NOCONFIRMATION, title 9026). With FOF_SIMPLEPROGRESS the title takes the file line (102) for good. The dialog thread waits 1000 ms, shows nothing if the work is done, then keeps the dialog at least 1000 ms. Every 2000 ms (timer 1) the time left: the blocks done since the last look per second, evened as (new + 2 old) / 3 (a look under a second keeps the old rate), (total − done) / rate + 1 seconds, shown from 5: "%d Minutes Remaining" 8739 (seconds / 60 + 1) past a minute, else "%d Seconds Remaining" 8740 rounded up to 5, nothing past 4 hours | `sub_300afcf5` (thread), `sub_300ae4d3` (dialog), `sub_300ab727`, `sub_300aa71e`, `sub_300aa3fe` | Done for copy and move (`fileops::transfer`). The work is counted in bytes by 32 KB blocks (`sub_300a9b1e`); its weights for files and folders (`+0xdc`, `+0xe0`, `+0xe4`) were not located, so they count nothing here. Delete too (`fileops::remove`, `fileops::emptyTrash`), counting an item for each file and folder deleted for good and each item sent to the Recycle Bin, since the engine's own weights are not located; each folder goes after what it holds, and an item deleted from the Recycle Bin takes its .trashinfo with it. Checked on a test display: 60,000 files deleted for good, and emptying the Recycle Bin |
| SysAnimate32: AVIs of 8-bit BI_RGB or BI_RLE8 only (the reader checks the `strf` header against `avih`), the frame time `dwMicroSecPerFrame` / 1000, each RLE frame drawn over the last; ACS_CENTER centres the frame, without it the control takes its size; ACS_AUTOPLAY plays on open, without end; while unseen it starts over and looks again after 4000 ms more; painted over the parent's static brush | `comctl32.dll` 6: `sub_300338bd` (window procedure), `sub_30033442` (step), `sub_300333b5` (paint), `sub_30087308` (reader) | Done (`xpdialog::Animation`; frames checked pixel for pixel against ffmpeg's decoding of AVI 1229). ACS_TRANSPARENT takes the frame's first pixel's colour as the background: not checked on XP |

### Missing

| Original feature | Status |
|---|---|
| File-type verbs from the registry (Print, Edit, Play, Add to Playlist…) | Partial: text, INI, INF, BAT and CMD files get Edit (Notepad) and Print (`lpr`), media files get Play (Media Player), from `hivecls.inf`'s `txtfile`/`batfile` and `AVIFile` verbs; the registry is not read, so other types and Add to Playlist are missing |
| Open With (submenu and the Open With dialog) | Partial: a submenu of the installed desktop entries (the first 12 by name) and a Choose Program... list; XP's registry's recommended programs and its own dialog are not read |
| Context menu handlers from shell extensions | Missing |
| Send To: removable drives and the actual contents of the SendTo folder | Done: `~/.local/share/win4wine/SendTo`'s `.desktop` targets (their `%f`/`%F`/`%u`/`%U`) and links to folders, plus the ready, writable mounted volumes other than the root and the home (copied into); `shellmenus.cpp` `addSendToFolder` |
| Show in Groups, Choose Details, Customize This Folder | Partial: Show in Groups (View menu and the background menu) groups Details by the sorted column — the name's first letter, size bands, type or date — drawing the 31-pixel header and 12-pixel gap measured on My Computer's icon view (not measured in Details) on the group's first row; the header is painted in that row, not a separate selectable row, and Icons/Tiles are not grouped for real folders. Choose Details opens dialog 1096 and shows/hides Size, Type and Date Modified, with widths (`folder/hiddenColumns`, `folder/columnWidths`); Move Up/Down remain greyed. Customize This Folder offers Documents, Pictures, Photo Album, Music and Videos templates, saved per folder (`folder/template/<path>`) and optionally for its subfolders; Pictures and Photo Album add the picture tasks and Filmstrip |
| Undo (shell32's undo stack) | Partial: a rename can be undone from Edit > Undo or the background menu (`QFileSystemModel::fileRenamed` remembers the two names); deletes, copies and moves are not in the stack |
| Full New submenu (Shortcut, Briefcase, Bitmap Image, WordPad Document, Wave Sound, Compressed Folder… from the registry's ShellNew keys) | Partial: Folder, Shortcut (a `.lnk` to a chosen file), Text Document, Bitmap Image (1 x 1 BMP), WordPad Document (empty RTF), Wave Sound (44-byte header), Briefcase (a folder) and Compressed Folder (an empty ZIP) are there; the registry's ShellNew keys are not read and a shortcut is still a link |
| Auto Arrange in folder windows | Partial: View > Auto Arrange and Align to Grid are checkable and saved as `folder/autoArrange` and `folder/alignToGrid`; with Auto Arrange on icons stay on their grid, off an icon view can be dragged about, and Align to Grid snaps them (positions are not kept) |
| My Computer: Manage (HIVECLS.INF: the CLSID's `shell\Manage` verb, words `mycomput.dll,-400`, `mmc.exe /s compmgmt.msc`) | Partial: the desktop's My Computer icon has Manage after Search..., in `mycomput.dll`'s words (extracted from now on; "Mana&ge" until then); it opens the desktop's system monitor (plasma-systemmonitor, gnome-system-monitor, ksysguard or xfce4-taskmanager) when there is one. XP's Computer Management is not there. XP has no Manage in the My Computer window's tasks or background menu |
| Recycle Bin Properties | Partial: File > Properties or the background menu opens a sheet with "Display delete confirmation dialog" (`fileops::remove` reads it); the drives' quota pages are not there |
| Real `.lnk` shortcuts | Partial: `fileops::shortcutTarget` reads the Shell Link header, LinkInfo local base path/common suffix or relative path, and opening/resolving the icon uses it; a drive letter is mapped through the Wine prefix's `dosdevices` (`$WINEPREFIX`, else `~/.wine`), and a target on a drive that is not there resolves to nothing; `fileops::createShortcut` writes a Shell Link with LinkInfo's local base path. Its target ID list, relative path and extra data are not written |

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
| Print (Photo Printing Wizard) | Partial: `photowiz.dll` is extracted for its strings and the viewer shows a wizard of its own with Picture Selection (all the folder's pictures, check marks), Printing Options (printer and paper), Layout Selection (full page, 2x2, 3x3, 4x4, Wallet, Contact sheet) and a progress dialog, printing each chosen picture on the page. photowiz.dll's own dialogs 200/201 are not read, but the layout page draws a preview of the chosen grid with the first picture |
| Help (`ImgPrev.chm`, named in `shimgvw.dll`) | Partial: the Help button runs XP's own `ImgPrev.chm` through Wine's `hh.exe` when `wine` is on the PATH, otherwise it shows a shortcut summary; the CHM is not read by the port itself |
| Annotations (409–415, 402, 422, 419) | Partial: for a TIFF, a pencil button opens an annotation bar with XP's tools by their tooltips 409 to 415: Freehand, Highlight, Straight Line, Frame, Solid Rectangle, Text and Attached Note (dragged out, the words typed after); then Undo, Clear and Save, which XP does not have. The ink is a transparent image over the picture and Save writes a `(annotated)` copy, not XP's annotations in the TIFF. Select (402), Delete Annotation and Edit Annotation Info (493, 494), the line width check (219), the save prompt (216), the colours and widths are missing or not measured |
| Margins and remembered window size | Done: measured on XP's own viewer (`shimgvw.dll` run with rundll32 in Wine): the 22-pixel toolbar sits 8 rows above the client's bottom and the picture's frame 8 rows above the toolbar; on closing XP writes `Bounds` (a RECT) and `Maximize` (a DWORD) under `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\ShellImageView`, here `Viewer/Bounds` and `Viewer/Maximize` in `native-explorer.conf` |
| Animated images (GIF) | Done (played at their own frame delays) |
| Comparison with the real viewer running in Wine | Done: `shimgvw.dll`'s own viewer ran under Wine (32-bit `rundll32.exe`); its default window was 3/4 of the screen wide and 2/3 high (not read in `shimgvw.dll`: it may be Wine's own default size), its toolbar has the same 12 buttons for a bitmap, and it shows no annotation pencil for BMP (TIFF only). The port now opens at that size and hides the pencil for non-TIFF. The title bar itself is the window manager's in both |

---

## 5. Frames for undecorated windows

Port: [`frames.cpp`](../native-shell/src/frames.cpp).

| Item | Status |
|---|---|
| Windows that ask for no decorations (`_MOTIF_WM_HINTS`) get no frame, matching bridge-wm | Done (checked in the native session with Windows Media Player) |

---

## 6. The taskband: task buttons, grouping and the taskbar's menus

Binaries: `explorer.exe`, `user32.dll`. Port: [`taskbar.cpp`](../native-shell/src/taskbar.cpp),
[`windowlist.cpp`](../native-shell/src/windowlist.cpp), [`arrange.cpp`](../native-shell/src/arrange.cpp),
`bridge-wm/wm.py`. Committed in `5ca90ee`.

### Recovered

| Item | Source | Status |
|---|---|---|
| Grouping settings: `TaskbarGlomming` (on unless 0, or SHRestricted `NoTaskGrouping` 0x40000056) at taskband `+0x34`, `TaskbarGroupSize` at `+0x38`, read with SHRegGetUSValueW and defaulting to 0 | `sub_1015ddf` | Done (0, 1 and 2 or more) |
| Button width: the band over the visible buttons (less `cxButtonSpacing` with more than one), at most SM_CXMINIMIZED; `TB_SETBUTTONWIDTH` | `sub_1004e4c`, `sub_1004fe2`; globals filled by `sub_101a001` (SM_CYSIZE `+0xbc`, SM_CXEDGE `+0xc0`, SM_CXMINIMIZED `+0xc4`) | Done |
| Least width: `_ftol((SM_CYSIZE + 2 SM_CXEDGE) x 1.8)` (constant at `0x1004c88`) plus `8 x tmAveCharWidth` of the toolbar's font while grouping is on (`+0x70`, `0x1009f61`) | `sub_10049a7`, `sub_1004f6b` | Done (92 px with Luna) |
| Which program groups: with `TaskbarGroupSize` 0, the one with two windows or more whose most recent window was chosen least recently (`sub_102eae3`: the maximum of the items' `+0xc`); ungrouping when two more buttons fit, the most recent group with fewer windows than that. Mode 1 groups the program with the most windows first and takes apart the group with the fewest; mode 2 or more groups every program with at least that many windows and leaves them grouped | `sub_10050ec`, branches `0x1023dc8`, `0x1023def`, `0x1023e0f` | Done |
| "Chosen": an item's `+0xc` is set at creation and before `SwitchToThisWindow` only, not on every activation | `sub_1005cf7`, `sub_102e07c` | Done |
| Metrics: `cxButtonSpacing` 0 and `cxPad` 20 with a style, 3 and 8 without (`TB_SETMETRICS`) | `sub_10163ba` | Done |
| Group button: the count centred in a gap as wide as the count with a "0" more, in `TaskBand.GroupCount` (Tahoma 8 bold, 255 204 109), `DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX`; the text moved by `iListGap`; the dropdown arrow, Marlett '6' in a 13-pixel (SM_CYMENUCHECK) rect at the right | `sub_102ee02`, custom draw at `0x1023a91` and `0x1023b4a` | Done for the count; the arrow is Partial: Marlett's '6' in a 13-pixel rect, not measured (XP's explorer never groups under Wine) and not yet found in comctl32's toolbar drawing (its Marlett font is made in `sub_7740fec0`: ((SM_CYBORDER + tmExternalLeading + tmHeight) & ~1) - 1, 13 with Tahoma 8) |
| Group list in `TaskBandGroupMenu` (menu band) | `sub_102eee0`; `luna.msstyles` (toolbar content margins 9, 0, 0, 0; button 9, 0, 3, 3) | Done: the row is the icon and the button's top and bottom margins (22 px with Luna), the toolbar's and button's left margins in |
| The taskband's scrollbar: when the buttons do not fit at their least width, the taskband takes a vertical scrollbar at its right (SM_CXVSCROLL) and the toolbar's rows become pages; the arrows scroll a page and the toolbar is shifted by one row | `sub_102de3e`: columns and rows from the band over the button size and row height; when the buttons need more rows than fit, SetScrollInfo SB_VERT with a page of the rows that fit, the band's width less SM_CXVSCROLL (global `0x104756c`), the toolbar moved up by nPos rows (SetWindowPos); `sub_1004fa3` clears the other bar. `luna.msstyles` `TaskBand::Scrollbar.ArrowBtn` (17 x 17, 16 states) | Partial: the scrollbar, its arrows, paging, the wheel and the keyboard's scroll-into-view are ported; the taskband's extra rows (a resized bar), the thumb and the tracks are not |
| Menu 210 (Cascade, Tile Horizontally, Tile Vertically, Minimize Group, Close Group) | `explorer.exe` MENUEX resource | Done |
| Menu 205 prepared as `sub_10331c8` does: Adjust Date/Time and Customize Notifications only on the notification area, "&Undo %s" (534 with 535 Cascade / 536 Tile), Show Open Windows (850), Cascade and Tile greyed without windows | `sub_10331c8` | Done |
| Cascade and Tile: user32's `CascadeWindows` (`sub_30054e2e`, position `sub_3002fae3`) and `TileWindows` (`sub_30055060`) | `user32.dll` | Done |
| Band end 3 px before the notification area | XP's taskbar in Wine (5, 7, 11, 12 buttons) | Done |
| Group names: the program file's `FileDescription` | `sub_1005cf7` (each `rundll32.exe` window on its own) | Done, by WM_CLASS here |
| The shell hook (`SHELLHOOK` messages, `sub_1002819`): HSHELL_FLASH (0x8006) sets the item's flash bit (`sub_1004730`); HSHELL_REDRAW marks it and redraws on timer 4 after 100 ms; HSHELL_ENDTASK calls EndTask; HSHELL_WINDOWACTIVATED arms timer 5 (1000 ms) | `sub_1002819`, `sub_1004730` | Read; the flash bit follows `_NET_WM_STATE_DEMANDS_ATTENTION` here |
| Dragging over the taskband (its IDropTarget at TaskBand+8, vtable at `0x1015b50`; DragOver `sub_102f663`): the button under the pointer held 250 ms brings its window forward (`sub_102f5aa`), the band's empty part held 500 ms shows the desktop (`sub_1002d65(1)`), then the delay grows by 10 s; DROPEFFECT_MOVE over a button. A drop there posts 0x436: ShellMessageBox(711, 518, MB_ICONHAND) | `sub_102f663`, `sub_10020e9` | Done; not tested by script (XTEST could not start a drag from a folder window) |
| The taskband's window procedure: timer 2 (2000 ms, or 125 after a first try) waits for WM_NULL before switching to a window that may hang (`sub_102ed44`); timer 4 coalesces redraws; timers 5 to 9, armed by WM_WINDOWPOSCHANGED, retry each second to bring the right window forward | `sub_10020e9` | Read |
| A window that starts flashing while another has the foreground (`sub_102d585`) gets flag 0x10 and brings the auto-hidden bar back (`sub_101f7be`); `sub_1002ab8` passes the flags to group buttons; the 500 ms check (`0x1025a26`) keeps the bar shown while the taskband answers message 0x439 | `sub_1004730`, `sub_1002ab8` | Done (`Taskbar::attentionChanged`) |

### Missing

| Original feature | Status |
|---|---|
| The taskband's extra rows when buttons do not fit even grouped, and its scrollbar's thumb and tracks | Partial: `Taskbar/Rows` gives the bar two or three rows of 30 (not measured on XP), the buttons fill a row before the next, and the scrollbar's thumb and tracks page it. XP's scrollbar moves one row at a time (`sub_102de3e`); here a page is all the rows. The thumb's size is not measured |
| Group visuals checked against XP | Not possible in Wine: XP's explorer never groups there (no exe path for a window) |


---

## 7. The Start panel's keyboard, selection and infotips

Binary: `explorer.exe`. Port: [`startmenu.cpp`](../native-shell/src/startmenu.cpp). Committed in `5ca90ee`.

### Recovered

| Item | Source | Status |
|---|---|---|
| Arrow routing between panes (`GetNextDlgGroupItem`, the pane whose rectangle holds the item's height, the nearest item; Left/Right swapped right-to-left) | `DV2ControlHost`, `sub_103ea34` | Done |
| `Start_MinMFU` 6 by default, policy 0x40000069 | `sub_1008c35` | Read; the ranking is ported as an approximation (a launch count and the time of the last use, its weight halved every two weeks; shell32.dll's own decay is not read) |
| Keys watched on XP's own panel in Wine: nothing selected at opening, Down first / Up last, panes in a ring, letters by access key or first letter, Right into a cascade with its first entry, Left and Escape close it | XP's explorer in Wine, captured after each key | Done |
| Access-key names (8236 My Recent Documents, 8234 Control Panel, 7020–7023, 730, 731) underlined only when the keys opened the panel | `explorer.exe` strings; XP in Wine | Done |
| Selection in COLOR_HIGHLIGHT (not HotTracking), its rectangles; infotips (300–307, 7000–7005, 7015, 7016) and their places | XP in Wine, pixel-measured | Done: identical pixels |

### Missing

| Original feature | Status |
|---|---|
| The MFU ranking (UserAssist counts and their decay in `shell32.dll`) | Partial: launches keep a count and a last-used time in `native-programs.conf`, and `programs::mostUsed` halves a count's weight every two weeks; shell32.dll's exact decay is not reproduced, and only launches through this shell are counted |
| The infotip's own drawing (pale gradient, 6 px margin) | Done: a `QLabel` tooltip window with XP's pale gradient, a 1-pixel border and 6-pixel padding, placed at the item's words and under the selection (`StartPanel::showInfoTip`), instead of Qt's 2-pixel tooltip; the menus' shadow gives it XP's drop shadow |

---

## 8. Notification area, clock and the folder windows' bands

Binaries: `explorer.exe`, `shell32.dll`, `comctl32.dll` 6.0, `luna.msstyles`. Port: [`taskbar.cpp`](../native-shell/src/taskbar.cpp),
[`filebrowser.cpp`](../native-shell/src/filebrowser.cpp), [`taskpane.cpp`](../native-shell/src/taskpane.cpp),
`qt-style/src/xpstyle.cpp`. Committed in `34bae1a`, `32bc7ae`, `5cb3d16`.

### Recovered

| Item | Source | Status |
|---|---|---|
| Clock width: the longer of 11:00 and 23:00 in the user's format with "00" (`0x101dd5c`) appended, measured with `GetThemeTextExtent`; the time centred | `sub_101e19c`, `sub_101df4f`, `sub_102029b` | Done (+1 px for uxtheme's extent) |
| Tray layout: the style's content margins (19, 7), 18-pixel buttons, icons 1 inside | `luna.msstyles`; XP in Wine (4 and 5 icons) | Done |
| Details columns: shell32's column tables (width in characters x the font's "0", LVCFMT_RIGHT) for folders, My Computer, Control Panel, search results | `shell32.dll` data tables (Name 30, Size 16 right, Type 20, Date Modified 20…) | Done: 180/96/120/120 px as XP |
| Details header: 20 tall, words 12 in (right-aligned ending 7 in), sort arrow a 9 x 5 BtnShadow triangle 9 after the words | XP in Wine | Done: identical pixels (qt-style, every Qt program) |
| Menu band: shell32's menu band is a comctl32 toolbar of text-only buttons (words + 16, 19 tall, 2 under the band's top, first 2 in) | XP in Wine | Done: identical pixels, hot item included |
| "Address" in Rebar TextColor (127 124 115) | `luna.msstyles` | Done |
| Task pane: `rcbmp(112/110, 6, …)` nine-grid over the 25-row header, the header's border of 2, title 13 in, arrow 2 from the right, words 7 after the icon; File and Folder Tasks ordinary, Details collapsed | shell style `blue_ss.dll`'s sheet; XP in Wine | Done: first section identical |

### Missing

| Original feature | Status |
|---|---|
| Task pane entries XP adds: Publish this folder to the Web, Local Disk / Documents / My Network Places in Other Places | Partial: folder windows' Other Places end with My Network Places, and My Network Places is a folder (Local Network, 4263) of the places added with Network Tasks (31343: Add a network place 31347, View network connections 31345) and GVfs mounts, its Other Places Desktop, My Computer and My Documents. Set up a home or small office network (31349), View workgroup computers (31351), Entire Network (9218) and Publish this folder to the Web are missing |
| The caption under XP's own uxtheme | Not comparable in Wine (Wine draws it) |
| The toolbar band's remaining differences | Wine's text rendering in the capture; not addressed |

---

## 9. Minimize, maximize and restore animation

Binaries: `user32.dll` (only a system call, 0x1168), `win32k.sys` (extracted from the ISO's `WIN32K.SY_`). Port:
[`captionanimation.cpp`](../native-shell/src/captionanimation.cpp). Committed in `c6d01ae`.

| Item | Source | Status |
|---|---|---|
| Service table entry 0x168 (table at `0xbf999b80`, registered by `KeAddSystemServiceTable` in `entry`) | `win32k.sys` | Done |
| `xxxDrawAnimatedRects` (`0xbf8cd733`, worker `0xbf8cd7ec`): frames while under 251 ms (`0xfb`) have passed, a 1 ms `KeDelayExecutionThread` (`0xbf855bfe`) between them, each value `start + (elapsed x change + 125) / 250`, the strip removed at the end | REA Ghidra decompilation | Done |
| The strip: SM_CYCAPTION − 1 tall, the window's borders left out, DrawCaptionTemp's gradient, icon 2 in, title 2 after | Earlier work (`977d5ab`) | Done |

---

## 10. Desktop icons and Customize Desktop

Binary: `shell32.dll`. Port: [`desktopicons.cpp`](../native-shell/src/desktopicons.cpp),
[`displayproperties.cpp`](../native-shell/src/displayproperties.cpp).

| Item | Source | Status |
|---|---|---|
| Desktop Items: My Documents (9227, icon 235), My Computer (9216, 16), My Network Places (9217, 18), Internet Explorer (9222; the default browser here), hidden by default | `shell32.dll` | Done, saved in `native-explorer.conf` `[desktop]` |
| Customize Desktop: the Desktop Items sheet (string 30371), General (dialog 29953) and Web (29954); its check boxes are `BS_3STATE` | `shell32.dll` | Partial: the four check boxes work; the icon list is shown; Change Icon (30070) and Restore Default (30071) change `desktop/icon.<key>` from a file's icon groups (shell32.dll and moricons.dll first, Browse... for another file); the cleanup wizard and the Web page are greyed |
| Icon spacing | comctl32 v6 uses SM_CXICONSPACING / SM_CYICONSPACING (`sub_30006d74`, `sub_3004487f`); the prefix says 75 x 75 | Partial: 75 px kept; XP in Wine showed 82 vertically, not explained |
| Recycle Bin's corner place | XP in Wine (10 px left, 7 px higher than the native) | Done: the default place is the last cell's left less 10 and its top less 7, until the bin is moved |
| Labels' drop shadow: the desktop's listview draws each label with comctl32's `DrawShadowText` (call at `0x7741a8de`: white text, black shadow, offset 2, 2). DrawShadowText (`0x77446973`) draws the text into a 32-bit DIB 10 larger, every lit pixel to 255, blurs it (`sub_774466d1`: the mean of the 4 pixels to the right, then the sum of 5 rows over 4, clamped, premultiplied with the shadow colour + 1) and AlphaBlends it at (-4, -8) plus the offset, then draws the text | `comctl32.dll` 6.0.2600.5512 | Done (`ShellTheme::drawShadowedText`, which already followed it, now used for the desktop's labels instead of a hard black copy 1 pixel off); the comparison with XP was limited by the icons' different places on the wallpaper |

---

## 11. File and folder Properties

Binary: `shell32.dll`. Port: [`fileproperties.cpp`](../native-shell/src/fileproperties.cpp), wired through
`filebrowser.cpp` and `desktopicons.cpp`.

| Item | Source | Status |
|---|---|---|
| General page: dialog 1041 (a file), 1044 (a folder), 1042 (several items), filled with shell32's words (4113, 4118 sizes; 8705 Multiple Types; 8706 All in; 8711 Files, Folders; 28956 Unknown application) | `shell32.dll` | Done |
| Opens with (the default application), Size on disk (allocated blocks), dates as GetDateFormat's long date and the time | XP in Wine | Done |
| Read-only (write permissions) and renaming, applied by OK and Apply; Hidden (a leading dot) and Archive greyed, Advanced hidden | — | Done |
| Version page: dialog 1043, `VS_VERSIONINFO` read from the file, the item names 8752–8765 sorted, Language as VerLanguageName | `shell32.dll` | Done |
| Etched separators (`SS_ETCHEDHORZ`, `SS_ETCHEDVERT`, `SS_ETCHEDFRAME`) | dialog 1041 | Done: the dialog builder makes a sunken `QFrame` (HLine, VLine, Box) for the Static control's 0x10, 0x11 and 0x12 kinds |
| The sheet's size and caption | XP in Wine: 58 px taller; no caption icon | Done: the tab area is 58 pixels taller (scaled), and the caption has no icon |
| Change… (Open With), Advanced attributes, Summary page, drives' page from shell32 | Partial: Change... opens the Open With list and makes the chosen program the MIME type's default (xdg-mime), updating Opens with; Advanced... opens shell32's dialogs 1054/1055 with the archive/index/compress/encrypt boxes greyed (no Linux equivalent); Summary is a simple file page (Title/Author/Size/dates/Location; document fields empty); the drives' shell32 page remains (drives keep the earlier hand-made page) |

---

## 12. The Windows key's hotkeys

Binary: `explorer.exe`. Port: [`taskbar.cpp`](../native-shell/src/taskbar.cpp) (`Taskbar::hotkey`, `keyboardStep`),
[`arrange.cpp`](../native-shell/src/arrange.cpp) (`minimizeAll`, `undo`), [`tray.cpp`](../native-shell/src/tray.cpp);
bridge-wm passes Shift and Ctrl with the key ([`wm.py`](../bridge-wm/wm.py), [`proxy.c`](../bridge-wm/proxy.c)).

| Item | Source | Status |
|---|---|---|
| The table: 12 entries `{virtual key, modifiers}` at `0x10114c4`, registered as IDs 500 to 511 on the tray (`sub_101146e`), skipped for MOD_WIN when `NoWinKeys` (0x40000044) is set: Win+R, Win+M, Win+Shift+M, Win+F1, Win+E, Win+F, Win+Ctrl+F, Win+Tab, Win+Shift+Tab, Win+Pause, Win+D, Win+B | `sub_101146e`; unregistered by `sub_103257d` | Done |
| WM_HOTKEY: `id - 500` through the jump table at `0x102162d` | `sub_1021388` | Done |
| Win+R: the Run dialog, or the one already open (FindWindow on `#32770` with string 722) | `sub_10213b8` | Done |
| Win+M: the Start menu closes, then Minimize All. The taskband first asks whether any window can be minimized (message 0x434); 0x435 runs `sub_102f245` | `0x1021510`, `sub_10320ad`, `sub_10320d7` | Done |
| Minimize All saves every visible window that is not minimized, except the tray and the desktop, with the undo string 537 "Minimize All" (`sub_1031b90`, `sub_1031b20`). A thread then minimizes the taskband's items from the last to the first, with SPI_SETANIMATION off (`sub_102e686`): SC_MINIMIZE, or SW_SHOWMINNOACTIVE for a hung window | `sub_102f245` | Done: the caption animation is paused for 1 s; the hung-window case does not apply here |
| Win+Shift+M and the menu's Undo: the saved placements put back from the end of the list with SetWindowPlacement, animation off, the list freed. Cascade, Tile and Minimize All share the list, so Undo names whichever saved it last (534 with 535, 536 or 537) | `sub_10335c0`, `sub_10334d1`; command 0x19f | Done |
| Win+E: ShellExecuteEx with the "explore" verb on CSIDL_DRIVES (0x11), unless restricted | `sub_103adad` | Partial: My Computer opens without the Folders pane (`filebrowser.cpp` was being changed elsewhere) |
| Win+F: command 0xa085 (Search) unless REST_NOFIND (0x80). Win+Ctrl+F: 0xa086 (Search for Computers) | `0x102159e`, `0x10215b8` | Win+F Done; Win+Ctrl+F opens the local Search folder (no network browser) |
| Win+Tab and Win+Shift+Tab: the tray to the foreground, then message 0x43d to the taskband with +1 or -1. The taskband (`sub_10020e9`) sets the focus on its toolbar and moves the hot item (TB_GETHOTITEM, taken as 0 when under 1) by one, round the ends, skipping hidden buttons: the first press lands on the second button | `0x10215dc`, `sub_10020e9` | Done; the keyboard is grabbed on the task buttons, the arrows move the hot button, Enter chooses it, the menu key and Shift+F10 open its menu |
| Win+Pause: SHRunControlPanel on sysdm.cpl | `0x102161a` | Done |
| Win+D: Show Desktop's toggle (`sub_1002d89`): the foreground window is kept to give back; Minimize All with lParam 1, which posts 0x453 to the desktop; the second press undoes it | `sub_1002d89` | Done: bridge-wm's `_NET_SHOWING_DESKTOP` (the same toggle as the taskbar's menu); the native `toggleDesktop` records the placements and leaves "&Undo Minimize All" in menu 205, whose Undo also clears `_NET_SHOWING_DESKTOP` |
| Win+B: SwitchToThisWindow on the tray, the focus on the notification area (`sub_101cdac`) | `0x1021567` | Done: arrows move between icons, Enter clicks one, the menu key opens its menu; the focus rectangle is DrawFocusRect's dots on the 18-pixel button (not measured against XP) |
| Win+F1: command 503 (Help and Support) | `0x1021543` | Partial: Win+F1 and the Start menu's Help and Support launch khelpcenter or yelp when installed; XP's Help and Support Center is not there |
| The Start button's tooltip, string 533 "Click here to begin" | `explorer.exe` | Done |
| At logon (`sub_100eb38`, `sub_100ebca`): `StartButtonBalloonTip` (Explorer\Advanced) decides. With the Start panel, while it is under 2, explorer.exe brings the tray forward and presses the Start button itself (BM_SETSTATE on control 0x130), then writes 2. With the classic menu, while it is 0, a balloon (`sub_1030259`: tooltips_class32 with TTS_BALLOON, 300 wide, TTI_INFO, title 800, text 801) points at the Start button's top middle and goes after 10 s (timer 0x16); 1 becomes 2 at the next logon. Policy 0x40000042 turns it off | `sub_100eb38`, `sub_1030259` | Done (`StartMenu/StartButtonBalloonTip` in `native-explorer.conf`); 1 is written when the Start button is used with the balloon up |
| Tray balloons' timeout: clamped to 10 000–60 000 ms (`sub_101fcc8`) | `sub_101fcc8` | Read (the native balloons clamp to 10–30 s, from Shell_NotifyIcon's documentation) |
| Shift and Ctrl held with the Windows key | bridge-wm wrote only the keys | Done: written as VK_SHIFT and VK_CONTROL before the key; `proxy.c` holds them for Wine (needs a MinGW rebuild of `proxy.exe`) |
| A keyboard layout set after the window manager started (Xwayland starts with the host's) | bridge-wm kept the first key map: Win+M was lost under a layout change | Done: MappingNotify refreshes the map and the shortcuts' grabs |

---

## 13. What explorer.exe does that is not ported yet

The largest blocks still open, across the sections: Windows Media Player's own visualization rendering and the
closed service pages, Show in Groups for icons and tiles, the taskbar's left and right edges, resizing and the deskbands other than Windows Media Player's,
Open With from XP's registry and shell-extension handlers, the Picture and Fax Viewer's XP tools and its comparison
with the real viewer under Wine, and the file Properties' drives' page.

Values from other contributors are kept only once checked in XP's binaries or measured on XP running in Wine;
where that was not possible the row says "not measured" or "unverified".

From `explorer.exe`'s strings, menus and dialogs, compared with the native shell. Sections 2, 6, 7 and 12 list
the gaps in what was ported; this is the rest. The desktop, folder windows and their menus are shell32's and
browseui's code; their gaps are in sections 3, 8, 10 and 11.

| Area | Original feature | Evidence |
|---|---|---|
| Taskbar | Flashing buttons on and off: explorer only mirrors HSHELL_FLASH and HSHELL_REDRAW, the rhythm is user32's FlashWindowEx | `sub_1002819`; user32 not reversed | Partial: when a window starts wanting attention (`_NET_WM_STATE_DEMANDS_ATTENTION`) its button flashes 3 times at 530 ms, `ForegroundFlashCount` and `CursorBlinkRate` in `hivedef.inf` (`sub_1004730` lights the button on HSHELL_FLASH, 0x8006), then stays lit until the window is used; that it ends lit is XP as seen, not read |
| Taskbar | Closing a hung window from its button's menu: 603 and 604 ("…unable to respond… force it to close?") | Strings 603, 604; Close is `EndTask(window, FALSE, Ctrl held)` (`sub_102fea7`), and a hung window's button menu offers only Close (`sub_102ed44`, `IsHungAppWindow`). Done: Ctrl+Close kills the X client at once; Close sends WM_CLOSE and, if the window is still there after HungAppTimeout (5000, `hivedef.inf`) with no window of its own owned by it (a save question), asks 603 and 604 (Yes/No) under the window's title. X has no IsHungAppWindow; `_NET_WM_PING` is not used. The hung window's reduced menu is missing |
| Taskbar | Moving and sizing the bar, one auto-hide bar per screen edge (705) | String 705 |
| Taskbar | The taskband's extra rows (a resized bar) | Section 6 |
| Taskbar | Windows Media Player's toolbar (wmpband.dll, named by its string 101) | `wmpband.dll` hosts wmploc.dll's `taskbar.wsz` (170 x 25; `vtaskbar.wsz` on a vertical bar; HKCU `TaskbarPlayer\HorizontalMode` may name another) in a `WMP9DeskBand` window and hides its band (RB_SHOWBAND 0, command 0x1000) until the player minimizes: wmp.dll's `sub_4b6384b1` finds that window in Shell_TrayWnd's rebar and sends it WM_USER with the show command, else shows wmploc.dll's dialog 1226 (mini-mode, AVI 1229) unless `ShowMinimizeDialog` is cleared. The band registers Ctrl+Alt+P ("Windows Media Player Restore Key"); `Deskband.OnMediaChange` shows `bugger.wsz` above it for 5 s (timer 1), faded in and out in 25 ms steps of 25 (timers 2 and 3, SetLayeredWindowAttributes); `ShowPopupWindow` shows `flyout.wsz` (video or visualization); its right click is the band site's menu (wmpband.dll has no menu). Done: the Toolbars menu's entry (`Taskbar/MediaPlayerBand`), the band as a child window of the bar left of the notification area (the player's window, through `_WIN4WINE_WMP_BAND_SITE` and `_WIN4WINE_WMP_DESKBAND` on the root), the player leaving the taskbar while minimized to it, Restore, Ctrl+Alt+P, the toast, the flyout, the right click and dialog 1226. Measured on XP's explorer and WMP 9 in Wine (a 30-pixel bar, Luna): the skin ends 4 before the notification area, at the bottom of a band window 2 short of the bar; the task band stops 4 before it, and unlocked the band's gripper sits in a header of 10 just left of the skin (pixel-identical locked and unlocked). Both popups are 170 wide (65 high, 129 for video), at the band's left, against the taskbar window's edge that faces the screen (wmpband.dll's `sub_4c4b1d9d`: above a bar whose top is past 1, else under it). Dialog 1226 plays AVI 1229 in its animate control 1228, as wmp.dll opens it. Not done: side bars (the native taskbar has none) |
| Balloons | The Windows tour (821, 822), low screen resolution (811, 812), "Hiding your inactive notification icons…" (590, 591) the first time, "New programs installed." (8229) | The tour and the low resolution are post-boot reminders: `sub_101343b` and `sub_101389f` file `Microsoft.OfferTour` (icon `tourstart.exe,0`, runs `tourstart.exe`, ShowTime 60000) and `Microsoft.FixScreenResolution` (icon `explorer.exe,9`, runs `desk.cpl`, a check of the display's modes in `sub_100e569`) through shell32's PostBootReminder ({7849596a-48ea-486e-8937-a2a3009f31a9}), which shows them one after the other from a notification icon of their own when the tray starts its shell service objects. The chevron's (590/591, `sub_101d6b6`) shows for 30 s when the chevron first appears, unless restricted. Done: low resolution (a screen under 800 x 600, opening Display Properties' Settings) then the tour, 6 s after the start (that delay is not read), pointed at the notification area's left end since no icon of their own is added; the chevron's when the chevron first appears; the tour runs tourstart.exe under Wine or opens the Start menu shortcut |
| Start menu | The most-used programs ranked by shell32's UserAssist counts; "Remove from This List" (8225) and the prompt for an item that is gone (8224) | Strings 8224, 8225; section 7. Remove from This List is done (right-click a recent program, `programs::removeUse`); the ranking now decays this shell's counts over time; the Start Menu folder's rows can be dragged to reorder them and Sort by Name clears the order. The gone-item prompt (`sub_1040e22`: ShellMessageBox MB_YESNO, no icon, 8224 under the item's name; Yes takes it off its list) is done for a pinned or most used program whose desktop entry or command is gone |
| Start menu | Pin to Start menu, dragging items in the panel and in All Programs, Sort by Name | shell32's verbs; StartMenu band. Pin/Unpin works from a recent program's menu (`StartMenu/Pinned`, shell32 strings 5381/5382); dragging and Sort by Name are missing |
| Start menu | Newly installed programs: "New programs installed." (8229) | `sub_1012d9e`/`sub_1012eaa`: a comctl32 balloon tooltip (style 0x43) with no title, in the bold menu font, its stem at All Programs' middle row, closed after 10 s. Done: shown each time the panel opens while programs have come since All Programs was last opened (opening it makes them known); the balloon has the shell's close button, which XP's untitled tooltip lacks. The highlighted new items in All Programs are missing |
| Start menu | Favorites (8235), Windows Security (7022), Undock Computer (7012, 7014), Disconnect (7017) | Strings |
| Start menu | "Some items cannot be shown" when the panel is too small (8231, 8232); the panel's size values 8256–8260 | `sub_101cc15` gives the panes the room between the Start button and the monitor's edge (the larger side) less the user and Log Off panes, All Programs' band too for the programs pane, and flags a pane that wants more; `sub_101ca03` then shows, once per shell, `sub_1012eaa`'s tooltip with TTM_SETTITLE (TTI_INFO, 8231) and 8232 at the programs pane's bottom middle, closed after 10 s. 8256–8260 are read by `sub_10158c0` (MulDiv by 380) and not ported. Done: most used programs, then pinned ones, are left out until the pane fits, the places below the room are not shown, and the balloon shows once; only the room above the button is used, as the panel always opens upward here |
| Start menu | The Customize dialogs (1311; dialogs 9, 1036, 1135) | Dialogs; section 2 and 14: dialogs 9, 1135 and 1036 now open, with the classic four options and the Start panel's places/count/icons/recent/hover saved; their Add/Remove/Clear lists work, and the classic menu's rows can be dragged to reorder them |
| Start menu | Help and Support (`helpctr.exe -FromStartHelp`, 732) | String 732 |
| Session | Safe mode notice (538, 539); "could not load or run" for the Run key's programs (515) | Strings |
| Menus | Drop shadows under menus, tooltips and the Start menu: win32k's xxxAddShadow (`sub_bf854347`) puts a layered SysShadow window (WS_EX_LAYERED, TRANSPARENT, TOOLWINDOW; TOPMOST with a topmost owner) behind a CS_DROPSHADOW window when SPI_GETDROPSHADOW is on; its picture (`sub_bf8544b3`) is the window's size plus 5 each way | `win32k.sys`; the profile from wine/patches/0033-winex11-external-drop-shadow.patch (0.442, 0.328, 0.168, 0.055, 0.012 across, fading 0.113 to 1.0 along, corners 0.882 to 0.018) | Partial: `menushadow.cpp` under a visual style (an override-redirect window masked to the right and bottom strips, the screen beneath darkened; `Effects/DropShadow` and `WIN4WINE_NO_MENU_SHADOW` turn it off; the Start panel has none). Under Windows Classic menus have no shadow here, as the user chose. The profile cannot be checked in this setup (Wine does not run win32k's shadow) and `sub_bf850d1b` is not decompiled |

---

## 14. The classic Start menu

Binary: `explorer.exe` (menu 204, banners 158–167), `shell32.dll` (icons), measured on XP's own classic menu in Wine
(policy `NoSimpleStartMenu` set for the capture, then put back). Port: [`startmenu.cpp`](../native-shell/src/startmenu.cpp)
(`StartPanel::buildClassicMenu`, `paintClassicMenu`, `Cascade` with `flat`), [`taskbarproperties.cpp`](../native-shell/src/taskbarproperties.cpp).

| Item | Source | Status |
|---|---|---|
| The entries: MENUEX 204 (Programs 504, Favorites 507, Documents 501, Settings 508 with Control Panel 505, Windows Security 5001, Network Connections 557, Printers and Faxes 510, Taskbar and Start Menu 413; Search 520, Help and Support 503, Run 401, Synchronize 553, Log Off %s 402, Disconnect 5000, Undock 410, Shut Down 506) | `explorer.exe` menu 204 | Done: Favorites, Windows Security, Synchronize, Disconnect and Undock are left out as XP leaves them by default; Shut Down... becomes 731 "T&urn Off Computer..." as on XP |
| The banner: by edition (`sub_1015839`, IsOS: 162, 164, 163, 158, `winbrand.dll` 1304/1305, 166 Home, 167 Professional), loaded by `sub_10157f6` | `explorer.exe` bitmaps | Done with 167 (the edition is not known here); drawn at the bottom, its top-left colour above |
| Layout: 1-pixel COLOR_BTNSHADOW border; rows 32; the Start Menu folder's items with 32-pixel icons 2 after the banner; menu 204's with 24-pixel icons 6 after it; words 42 after it; lines 9 rows (on the fifth) and 7 rows (on the fourth), 2 in from each side; width = widest words + 91 | XP in Wine, pixel-measured | Done: identical lines, border and words (icons differ only in Wine's rendering of their masks) |
| Cascades (flat menus): 1-pixel border, rows 20, icon 3 in, words 27 in, separator 8 rows with its line on the fourth, arrow 4 x 7 9 from the right, width = widest words + 50; a cascade covers the right border of the menu it opens from, its top a row above the row's | XP in Wine | Done |
| Settings: Control Panel (shell32 137), a line, Network Connections (ncpa.cpl 1001), Printers and Faxes (138), Taskbar and Start Menu (40); Documents: My Documents, My Pictures, a line, the recent documents or (Empty); Search: shell32's 23232 "For &Files or Folders..." | XP in Wine; `shell32.dll` | Done; Search's other find extensions (computers, people) are not there |
| Keys: the arrows, Right opening a cascade, access keys underlined when the keys opened the menu | StartPanel's routing, one column | Done |
| The Start Menu page: Start menu or Classic Start menu (controls 1132, 1133), preview 170 or 171 | dialog 205; `sub_1038a95` | Done; saved as `StartMenu/Classic` |
| The Start Menu folder's own items | XP: Set Program Access and Defaults, Windows Catalog, Windows Update | Partial: Set Program Access and Defaults only |
| Customize Classic Start Menu (dialog 9) and its Advanced options (Display Administrative Tools, Favorites, Log Off, Run; Expand Control Panel, My Documents…; small icons) | `explorer.exe` dialog 9 ("Customize Classic Start Menu"; 1135 is the Start panel's General page and 1036 its Advanced page) | Partial: dialog 9 opens and its Display Favorites, Display Log Off, Display Run and Show Small Icons options change the classic menu; Add/Remove manage `~/.local/share/win4wine/Start Menu`, its rows can be dragged to reorder them and Sort by Name clears that order, Advanced is greyed, Clear clears the recent documents list, and the other Advanced options are not wired |
| The desktop's icons in classic mode (XP keeps HideDesktopIcons per mode: ClassicStartMenu shows them) | XP in Wine (icons shown) | Done: `desktop/hideIcons` for the Start panel and `desktop/hideIconsClassic` for the classic menu, chosen by `StartMenu/Classic` |

---

## 15. Windows Classic

Binaries: `user32.dll` (DrawFrameControl), `explorer.exe`, XP's `hivedef.inf` (the schemes' SCHEMEDATA). Port:
[`frames.cpp`](../native-shell/src/frames.cpp), [`taskbar.cpp`](../native-shell/src/taskbar.cpp),
[`taskbarproperties.cpp`](../native-shell/src/taskbarproperties.cpp).

| Item | Source | Status |
|---|---|---|
| Windows Standard's metrics: iBorderWidth 1, iScrollWidth/Height 16, iCaptionWidth/Height 18, small caption 12, menu 18 (Large 21/22, Extra Large 24) | SCHEMEDATA's NONCLIENTMETRICS in `hivedef.inf` | Read: the native caption is 18 rows and a line, the frame 4, the menu bar 19, as these give |
| Caption buttons: DrawFrameControl(DFC_CAPTION) (`sub_7e43e7c4`) draws DrawEdge EDGE_RAISED (EDGE_SUNKEN pressed) with BF_SOFT \| BF_ADJUST (`sub_7e432712`), fills the face, then Marlett as high as the inside's smaller side in a centred square, a pixel down and right when pressed, embossed in the highlight then shadow colour when inactive (`sub_7e437327`); glyphs r (Close), 0, 1, 2, s (Help) (`sub_7e46d69e`) | `user32.dll` | Done: the soft edge's inner shadow, the glyph's size and place, and Help's "?" (it was drawn as Close's cross) |
| Their places: SM_CXSIZE - 2 by SM_CYSIZE - 4, Close 2 inside the caption's right and top, Maximize (or Help) 2 left of it, Minimize against Maximize | Windows' nonclient layout | Done (Close was against the frame and a row high) |
| The Taskbar and Start Menu sheet's previews: LoadImage with LR_LOADMAP3DCOLORS (`sub_1037f98`), the same Luna bitmaps under Windows Classic, their colour tables' 128/192/223 greys mapped to the 3D colours | `explorer.exe` | Done |
| The notification area's chevron: a flat toolbar button under Windows Classic, its edge only while hot or pressed; the hidden icons shown go back 5 s after the pointer leaves (TrayNotify's timer 3, `sub_1021370`) | `explorer.exe` | Done; the code that clears the open state on that timer was not located (the 5 s and the pointer test are what was read) |
| Drop shadows under menus | win32k's xxxAddShadow (section 13) | Missing |

