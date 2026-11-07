# The installer

`./install.sh --media XP.iso [--prefix DIR] [--system] [--greeter]` installs the native session as XP's own
setup would show it. Every word and picture comes from the user's ISO; none of XP's code runs.

1. **Text mode** (`setup/textmode.py`, in the terminal): Welcome to Setup, the licence (F8), the copy gauge
   while `native-shell/extract.py` takes XP's files and the programs are built, then "This portion of Setup
   has completed successfully" and its countdown.
2. **Graphical mode** (`setup/billboard.py`, full screen): winntbbu.dll's billboard while
   `setup/install_files.py` copies the session to the prefix and registers it.
3. **Windows Welcome** (`greeter/xp-native-oobe.py`) at the first logon, from `native-shell/session.sh`.

## Demo

`./install.sh --demo` shows every screen with nothing built, copied or registered: the copy gauge runs over
the names of the files `extract.py` would take (about 20 s), the billboard over what `install_files.py`
would print (about 40 s, its panels every 6 s instead of 36), then Windows Welcome, whose choices go to a
folder thrown away afterwards. `--media` is needed only the first time. `./xp.sh setup-demo [ISO] [WxH]`
does the same with the billboard and Windows Welcome in a 1024 × 768 window instead of full screen.

## Where each thing comes from

| Part | Source | How it was read |
|---|---|---|
| Text-mode words | `usetup.exe`'s message table: 0x2af8 title, 0x2329 Welcome, 0x2afb licence title, 0x2379 / 0x2fc1 / 0x2732 copying, 0x232f / 0x2394 / 0x2393 completed, 0x300c countdown, 0x2711 / 0x2712 / 0x2769 / 0x276a / 0x276b / 0x276c / 0x271e keys | parsed (`setup/xpsetup.py`); `%%I` lines intense, 0x07 is CP437's bullet |
| Licence | `eula.txt` (I386's, the larger one) | as is |
| Billboard geometry | `winntbbu.dll` `sub_59ff2ec1` | retool: band H×13/200, left panel W×28/100; for W ≥ 800 the steps at W×0.039, top `text_pt − ftol(H×−0.059) + title_pt + band`, right `left − ftol(W×−0.2005)`, column width W×0.1705; time left at steps' bottom − ftol(H×−0.063), 5f high; status 3f high under it; bar f/3 below, 3f/2 high; panel text at W×0.06 in the right panel, its right edge at panel width − 2 margins; the W < 800 branch likewise |
| Fonts and sizes | strings 1–3 (steps: 9, 8, 8 pt by width ≥ 1024 / = 800 / other, `sub_59ff215e`), 4 Tahoma, 5 Arial, 6–17 the panels' sizes by width (`sub_59ff2bc8`'s tables at 0x59ffe4ac…), 18 / 20 weights 700 / 400, 19 Tahoma | `MulDiv(pt, 96, 72)`; steps' second font weight 0x258 (600) in `sub_59ff21d9` |
| Steps | strings 23, 24, 25, 63, 64; bullets 104 done, 109 current, 105 to come (`sub_59ff2dbd`), key colour 78,111,214 (0x5a001a50) | `sub_59ff234c`: text 1.5 × the bullet's width in, pitch (most lines + 1) × tmHeight, the last max(tmHeight × lines, bullet height) |
| Colours | `sub_59ff2aed` (more than 8 bpp): white text, shadow 0,37,109 (0x6d2500), current step orange 255,128,0 (0x80ff) | |
| Shadow | `sub_59ff5aea` → `sub_59ff5825(dc, 2, 2, …)` | 2 px right and down |
| Panels | Professional's table at 0x59ffe490 (15 title/text pairs, 27–60); `SetTimer(…, 0x8ca0)`: 36 s each | title pitch f + 2 × title pt, line pitch text pt + f (`sub_59ff67c5`) |
| Time left, status | STATIC controls (`sub_59ff43df`), SS_NOPREFIX, WM_CTLCOLORSTATIC white; time left in Tahoma 600, status in Tahoma 400; words from `syssetup.dll` 1179 / 1180 and 54, 205, 1164, 1165, 1168 | |
| Progress bar | `msctls_progress32`, PBS_SMOOTH, background white (0x5a001a48), bar 7,158,5 (0x5a001a4c); the subclass frames it on WM_NCPAINT with `Rectangle`, NULL_BRUSH, BLACK_PEN | the fill inside the 1 px frame, as comctl32's classic bar |
| Logo | bitmap 102 / 101 / 100 by width, at (W×0.0195, H×0.0053) | `sub_59ff2dbd`, `sub_59ff43df` |

Not compared with XP's own setup on screen yet: everything above is read from the code, and the background
(bitmap 103) is stretched to the screen. The text-mode screens use the terminal's font, not VGA's.
