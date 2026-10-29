#!/bin/bash
# Builds the small Win32 helpers (32-bit, like the XP shell) into .local/bin.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
repo=$(dirname -- "$root")
src=$root/helpers
out=$root/.local/bin
cc=${MINGW_CC:-i686-w64-mingw32-gcc}
mkdir -p "$out"
compile() {
    local target=$1
    shift
    "$cc" -std=gnu11 -Wall -Wextra -O2 -municode "$@" -o "$out/$target"
}
compile theme-settings.exe "$repo/prefix/theme-settings.c" -luxtheme
compile startmenu.exe "$src/startmenu.c" -lshell32 -lole32 -luuid -ladvapi32 -luser32
compile xpstart.exe "$src/xpstart.c" -mwindows -luser32 -lwinmm
"$cc" -std=gnu11 -Wall -Wextra -O2 -shared "$src/xpcompat.c" -o "$out/xpcompat.dll" -Wl,--kill-at -luxtheme -lgdi32 -luser32
compile iexplore.exe "$src/iexplore.c" -mwindows -lshell32
compile linux-launch.exe "$repo/session/linux-launch.c" -mwindows -luser32
compile linux-links.exe "$repo/session/linux-links.c" -lole32 -lshell32 -luuid
i686-w64-mingw32-windres -I "$src" "$src/xptray.rc" -O coff -o "$out/xptray.res"
compile xptray.exe "$src/xptray.c" "$src/unixshell.c" "$out/xptray.res" -mwindows -lcomctl32 -lgdi32 -luser32 \
    -lkernel32
rm -f "$out/xptray.res"
"$cc" -std=gnu11 -Wall -Wextra -O2 -municode -shared "$src/xpmix.c" "$src/unixshell.c" "$src/xpmix.def" \
    -o "$out/xpmix.dll" -lwinmm
echo "Helpers built in $out"
