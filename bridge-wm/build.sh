#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/.local/bridge"
# A target newer than this script and every file it is built from is kept:
# every session start runs this script. DEPENDS names the headers that the
# sources include (paths without spaces), which compile() cannot see.
fresh() {
    local target=$1 file
    shift
    [[ -e $target && $target -nt ${BASH_SOURCE[0]} ]] || return 1
    for file in "$@"; do
        [[ ! -f $file || $target -nt $file ]] || return 1
    done
}
compile() {
    local target=$1
    shift
    # shellcheck disable=SC2086  # DEPENDS is a list of paths
    fresh "$target" "$@" ${DEPENDS:-} && return 0
    local temporary
    temporary=$(mktemp "$target.XXXXXX")
    if "${MINGW_CC:-i686-w64-mingw32-gcc}" -std=c11 -Wall -Wextra -Werror -O2 -municode \
        "$@" -o "$temporary"; then
        chmod 755 "$temporary"
        mv -f -- "$temporary" "$target"
    else
        rm -f -- "$temporary"
        return 1
    fi
}
# Sessions may still map these PE files. Publish new inodes without truncating
# an executable that another Wine process is using.
compile "$root/.local/bridge/proxy.exe" "$root/bridge-wm/proxy.c" -luser32 -lgdi32 -lshell32 -luxtheme
compile "$root/.local/bridge/inspect.exe" "$root/bridge-wm/inspect.c" -luser32 -lgdi32 -lole32 -lshell32 -luuid
compile "$root/.local/bridge/menu-settings.exe" "$root/prefix/menu-settings.c" -lshell32
compile "$root/.local/bridge/theme-settings.exe" "$root/prefix/theme-settings.c" -luxtheme
compile "$root/.local/bridge/wallpaper-convert.exe" -municode "$root/prefix/wallpaper-convert.c" -lole32 -luuid -lwindowscodecs
# Linux applications in the XP Start menu (session/linux_apps.py).
compile "$root/.local/bridge/linux-launch.exe" "$root/session/linux-launch.c" -mwindows -luser32
compile "$root/.local/bridge/linux-links.exe" "$root/session/linux-links.c" -lole32 -lshell32 -luuid
# XP's logon and log off sounds (session/supervisor.py).
compile "$root/.local/bridge/playsound.exe" "$root/session/playsound.c" -mwindows -lwinmm -lshell32
# The session's keyboard layout follows XP's input languages (session/keyboard.py).
compile "$root/.local/bridge/keyboard-watch.exe" "$root/session/keyboard-watch.c" -mwindows -lshell32 -ladvapi32
# XP Explorer's helpers (xp-explorer/helpers), for the session too: the
# notification area's network, battery and volume icons, Start menu pin
# management, and the stand-in iexplore.exe that opens the Linux browser.
helpers=$root/xp-explorer/helpers
if ! fresh "$root/.local/bridge/xptray.exe" "$helpers/xptray.c" "$helpers/unixshell.c" "$helpers/unixshell.h" \
        "$helpers/xptray.rc" "$helpers/xptray.manifest"; then
    i686-w64-mingw32-windres -I "$helpers" "$helpers/xptray.rc" -O coff -o "$root/.local/bridge/xptray.res"
    compile "$root/.local/bridge/xptray.exe" -std=gnu11 "$helpers/xptray.c" "$helpers/unixshell.c" \
        "$root/.local/bridge/xptray.res" -mwindows -lcomctl32 -lgdi32 -luser32 -lkernel32
    rm -f "$root/.local/bridge/xptray.res"
fi
# Control Panel's Keyboard Shortcuts item (bridge-wm/hotkeys.py runs them).
wm=$root/bridge-wm
if ! fresh "$root/.local/bridge/shortcuts.cpl" "$wm/shortcuts.c" "$wm/shortcuts.rc" "$wm/shortcuts.h" \
        "$wm/shortcuts.manifest" "$wm/shortcuts.def"; then
    i686-w64-mingw32-windres -I "$wm" "$wm/shortcuts.rc" -O coff -o "$root/.local/bridge/shortcuts.res"
    compile "$root/.local/bridge/shortcuts.cpl" -std=gnu11 -shared "$wm/shortcuts.c" "$wm/shortcuts.def" \
        "$root/.local/bridge/shortcuts.res" -lcomctl32 -lcomdlg32 -luser32 -lkernel32
    rm -f "$root/.local/bridge/shortcuts.res"
fi
# Control Panel's GTK/QT Themes and Icons items (qt-style/ and session/icon_theme.py follow them).
session=$root/session
if ! fresh "$root/.local/bridge/linuxlook.cpl" "$session/linuxlook.c" "$session/linuxlook.rc" "$session/linuxlook.h" \
        "$session/linuxlook.manifest" "$session/linuxlook.def"; then
    i686-w64-mingw32-windres -I "$session" "$session/linuxlook.rc" -O coff -o "$root/.local/bridge/linuxlook.res"
    compile "$root/.local/bridge/linuxlook.cpl" -std=gnu11 -shared "$session/linuxlook.c" "$session/linuxlook.def" \
        "$root/.local/bridge/linuxlook.res" -lcomctl32 -luxtheme -lmsimg32 -lole32 -luuid -lwindowscodecs -lgdi32 \
        -luser32 -lkernel32
    rm -f "$root/.local/bridge/linuxlook.res"
fi
# XP's Volume Control (sndvol32) on the Linux sound server, for xptray.exe.
DEPENDS=$helpers/unixshell.h compile "$root/.local/bridge/xpmix.dll" -std=gnu11 -shared "$helpers/xpmix.c" \
    "$helpers/unixshell.c" "$helpers/xpmix.def" -lwinmm
# XP's Security Center service (wscsvc.dll) loads these from beside it: its
# firewall and network imports, which XP's own DLLs bring only with theirs.
mkdir -p "$root/.local/bridge/wsc"
compile "$root/.local/bridge/wsc/HNetCfg.dll" -std=gnu11 -shared "$helpers/wscfirewall.c" "$helpers/wscfirewall.def" -ladvapi32
compile "$root/.local/bridge/wsc/netshell.dll" -std=gnu11 -shared "$helpers/wscnetshell.c" "$helpers/wscnetshell.def" -lole32
# XP's Network Connections folder (netshell.dll): a stand-in for XP's Network
# Connections service with the Linux adapters, and the Remote Access and wired
# 802.1X dialogs that netshell loads in the shell's process.
mkdir -p "$root/.local/bridge/netcon"
compile "$root/.local/bridge/xpnetcon.exe" -std=gnu11 "$helpers/xpnetcon.c" -mwindows -lole32 -loleaut32 -luuid
for dll in rasdlg dot3dlg; do
    compile "$root/.local/bridge/netcon/$dll.dll" -std=gnu11 -shared "$helpers/netshell-$dll.c" "$helpers/netshell-$dll.def"
done
# XP's wireless dialogs (wzcdlg.dll) with NetworkManager for the Wireless Zero Configuration service.
DEPENDS=$helpers/unixshell.h compile "$root/.local/bridge/netcon/wzcsapi.dll" -std=gnu11 -shared \
    "$helpers/netshell-wzcsapi.c" "$helpers/unixshell.c" "$helpers/netshell-wzcsapi.def" -lole32 -lshell32 -luuid
compile "$root/.local/bridge/startmenu.exe" -std=gnu11 "$helpers/startmenu.c" -lshell32 -lole32 -luuid -ladvapi32 -luser32
compile "$root/.local/bridge/startmenu-view.exe" "$helpers/startmenu-view.c" -mwindows -lshell32 -lole32 -luuid \
    -luxtheme -lgdi32 -luser32 -ladvapi32
compile "$root/.local/bridge/iexplore.exe" -std=gnu11 "$helpers/iexplore.c" -mwindows -lshell32
