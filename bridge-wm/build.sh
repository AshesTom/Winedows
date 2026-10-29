#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$root/.local/bridge"
compile() {
    local target=$1
    shift
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
compile "$root/.local/bridge/proxy.exe" "$root/bridge-wm/proxy.c" -luser32 -lgdi32 -lshell32 -luxtheme -loleacc -lole32 -loleaut32 -luuid
compile "$root/.local/bridge/inspect.exe" "$root/bridge-wm/inspect.c" -luser32 -lgdi32 -lole32 -lshell32 -luuid
compile "$root/.local/bridge/menu-settings.exe" "$root/prefix/menu-settings.c" -lshell32
compile "$root/.local/bridge/theme-settings.exe" "$root/prefix/theme-settings.c" -luxtheme
compile "$root/.local/bridge/wallpaper-convert.exe" -municode "$root/prefix/wallpaper-convert.c" -lole32 -luuid -lwindowscodecs
# Windows 7's shell prefix (prefix/win7.py).
compile "$root/.local/bridge/theme-fonts.exe" "$root/prefix/theme-fonts.c" -luxtheme
compile "$root/.local/bridge/user-profile.exe" -municode "$root/prefix/user-profile.c" -lole32 -lshell32 -luuid
# Linux applications in the Start menu (session/linux_apps.py).
compile "$root/.local/bridge/linux-launch.exe" "$root/session/linux-launch.c" -mwindows -luser32
compile "$root/.local/bridge/linux-links.exe" "$root/session/linux-links.c" -lole32 -lshell32 -luuid
# XP's logon and log off sounds (session/supervisor.py).
compile "$root/.local/bridge/playsound.exe" "$root/session/playsound.c" -mwindows -lwinmm -lshell32
# The session's keyboard layout follows XP's input languages (session/keyboard.py).
compile "$root/.local/bridge/keyboard-watch.exe" "$root/session/keyboard-watch.c" -mwindows -lshell32 -ladvapi32
# Windows Audio's services for Windows 7's volume icon (windows7/audiosrv.c), 64-bit.
MINGW_CC=x86_64-w64-mingw32-gcc compile "$root/.local/bridge/audiosrv.exe" "$root/windows7/audiosrv.c" -ladvapi32
# Windows 7 Basic while the session's compositor is stopped (windows7/composition.c), 64-bit.
MINGW_CC=x86_64-w64-mingw32-gcc compile "$root/.local/bridge/w7-composition.exe" "$root/windows7/composition.c" -luser32 -ladvapi32
# Aero glass: the Windows 7 session's compositor (windows7/dwm.c), a Linux program.
dwm_libs='x11 xcomposite xdamage xfixes xext egl glesv2'
if pkg-config --exists $dwm_libs; then
    temporary=$(mktemp "$root/.local/bridge/w7-dwm.XXXXXX")
    # shellcheck disable=SC2046
    if "${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -O2 "$root/windows7/dwm.c" -o "$temporary" \
        $(pkg-config --cflags --libs $dwm_libs) -lm; then
        chmod 755 "$temporary"
        mv -f -- "$temporary" "$root/.local/bridge/w7-dwm"
    else
        rm -f -- "$temporary"
        exit 1
    fi
else
    echo "w7-dwm not built (no development files for: $dwm_libs): Windows 7 Basic instead of Aero" >&2
fi
