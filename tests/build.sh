#!/bin/bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
# Another output directory and MINGW_CC/MINGW_WINDRES build the probes for
# x86_64 too, e.g. to check the patches from 64-bit applications.
out=${WIN4WINE_PROBES:-$root/.local/bridge}
mkdir -p "$out"
compile() {
    local target=$1 temporary
    shift
    temporary=$(mktemp "$target.XXXXXX")
    if "${MINGW_CC:-i686-w64-mingw32-gcc}" -std=c11 -Wall -Wextra -Werror -O2 \
        "$@" -o "$temporary"; then
        chmod 755 "$temporary"
        mv -f -- "$temporary" "$target"
    else
        rm -f -- "$temporary"
        return 1
    fi
}
# The reference probe can stay open while a new version is built.
# Publish a new inode instead of truncating a PE mapped by Wine.
for name in shellhook title-change wine-restore exit-windows shell-activation shell-layout-sync wine-maximize workarea no-redraw button-size system-menu rebar print-window foreign-icons mono-advance text-vcenter taskbar-fonts nc-context-menu marlett-caption frame-control default-position ui-state screen-capture text-mask min-rect; do
    compile "$out/$name-test.exe" -municode "$root/tests/$name.c" -luser32 -lgdi32
done
compile "$out/size-move-snap-test.exe" "$root/tests/size-move-snap.c" -luser32
compile "$out/toolbar-layout-test.exe" "$root/tests/toolbar-layout.c" -luser32 -lgdi32
compile "$out/stretch-map-test.exe" "$root/tests/stretch-map.c" -lgdi32 -lmsimg32
compile "$out/margin-shrink-test.exe" "$root/tests/margin-shrink.c" -luxtheme -lgdi32 -luser32
compile "$out/text-wrap-test.exe" "$root/tests/text-wrap.c" -lgdi32 -luser32
compile "$out/text-underline-test.exe" -municode "$root/tests/text-underline.c" -lgdi32 -luser32
compile "$out/foreground-focus-test.exe" -mwindows "$root/tests/foreground-focus.c" -luser32
compile "$out/alpha-blend-test.exe" "$root/tests/alpha-blend.c" -lgdi32
compile "$out/part-black-test.exe" "$root/tests/part-black.c" -luxtheme -lgdi32 -luser32
compile "$out/notify-icon-test.exe" "$root/tests/notify-icon.c" -luxtheme -lshell32 -lgdi32 -luser32
compile "$out/theme-threads-test.exe" "$root/tests/theme-threads.c" -luxtheme -lgdi32 -luser32
compile "$out/menu-layout-test.exe" "$root/tests/menu-layout.c" -luser32 -lgdi32
compile "$out/menu-separator-test.exe" "$root/tests/menu-separator.c" -luser32
compile "$out/menu-height-test.exe" "$root/tests/menu-height.c" -lgdi32 -luser32
compile "$out/menu-theme-layout-test.exe" -municode -mwindows "$root/tests/menu-theme-layout.c" -lgdi32 -luser32
compile "$out/status-height-test.exe" -municode -mwindows "$root/tests/status-height.c" -lcomctl32 -lgdi32 -luser32
compile "$out/tab-focus-test.exe" -municode -mwindows "$root/tests/tab-focus.c" -lcomctl32 -lgdi32 -luser32
compile "$out/trackbar-size-test.exe" -municode -mwindows "$root/tests/trackbar-size.c" -lcomctl32 -lgdi32 -luser32
compile "$out/listview-layout-test.exe" -municode -mwindows "$root/tests/listview-layout.c" -lcomctl32 -luxtheme -lgdi32 -luser32
compile "$out/msgbox-layout-test.exe" -municode -mwindows "$root/tests/msgbox-layout.c" -luser32
compile "$out/theme-part-sizes-test.exe" -municode "$root/tests/theme-part-sizes.c" -luxtheme -lgdi32 -luser32
compile "$out/rebar-bands-test.exe" -municode "$root/tests/rebar-bands.c" -luser32
compile "$out/number-format-test.exe" -municode "$root/tests/number-format.c"
compile "$out/notify-broadcast-test.exe" -municode "$root/tests/notify-broadcast.c" -luser32
compile "$out/dm-reposition-test.exe" -municode "$root/tests/dm-reposition.c" -luser32
compile "$out/shell-item-images-test.exe" -municode "$root/tests/shell-item-images.c" -lshell32 -lole32 -luuid -lgdi32
compile "$out/caption-ncactivate-test.exe" "$root/tests/caption-ncactivate.c" -lgdi32 -luser32
compile "$out/cleartype-test.exe" -municode "$root/tests/cleartype.c" -luser32 -lgdi32 -ladvapi32
compile "$out/icon-alpha-test.exe" "$root/tests/icon-alpha.c" -luser32 -lgdi32
compile "$out/gl-perf-test.exe" "$root/tests/gl-perf.c" -lopengl32 -lgdi32 -luser32
compile "$out/class-probe.exe" -municode "$root/prefix/class-probe.c" -lole32 -luuid

compile "$out/display-test.exe" -s "$root/tests/display.c" -luxtheme -lgdi32
compile "$out/dpi-metrics-test.exe" -s "$root/tests/dpi-metrics.c" -luxtheme -lcomctl32 -lgdi32 -luser32
compile "$out/char-widths-dpi-test.exe" -s "$root/tests/char-widths-dpi.c" -lgdi32 -luser32
compile "$out/text-bits-dpi-test.exe" -s "$root/tests/text-bits-dpi.c" -lgdi32 -luser32
compile "$out/caption-glyph-dpi-test.exe" -s "$root/tests/caption-glyph-dpi.c" -luxtheme -lgdi32 -luser32

for name in start-button start-sizing start-metrics theme-font theme-text-shadow theme-metrics classic-fonts font-profile taskbar-background clock-theme scrollbar-theme rebar-theme rebar-gripper rebar-tail reference-metrics shell-layout caption-text nonclient-caption caption-icons gradient-clip halftone-clip clock-metrics clock-pixels taskbar-text theme-part-pixels window-theme-empty theme-props resource-languages theme-parent-background; do
    compile "$out/$name-test.exe" -municode "$root/tests/$name.c" -luxtheme -lgdi32
done

compile "$out/shell-names-test.exe" -municode "$root/tests/shell-names.c" -ladvapi32
compile "$out/desktop-icons.exe" -municode "$root/tests/desktop-icons.c" -ladvapi32 -lshell32
compile "$out/text-widths-test.exe" -municode "$root/tests/text-widths.c" -lgdi32 -luser32
compile "$out/folder-icons-test.exe" -municode "$root/tests/folder-icons.c" -lshell32 -lole32 -luuid
compile "$out/desktop-menu-test.exe" -municode "$root/tests/desktop-menu.c" -lshell32 -lole32 -luuid -ladvapi32 -luser32
compile "$out/reg-dump-test.exe" -municode "$root/tests/reg-dump.c" -ladvapi32
compile "$out/string-id-test.exe" -municode "$root/tests/string-id.c"
compile "$out/transparent-shrink-test.exe" -municode "$root/tests/transparent-shrink.c" -lmsimg32 -lgdi32

compile "$out/nonclient-theme-test.exe" -municode "$root/tests/nonclient-theme.c" -luxtheme -lgdi32

# Synthetic strings exercise the source parser without reading theme assets.
python3 "$root/tests/font_parser.py"

# Self-authored activation manifest selects the public v6 Button API on XP.
"${MINGW_WINDRES:-i686-w64-mingw32-windres}" -I "$root/tests" \
    "$root/tests/classic-start-sizing.rc" "$out/classic-start-sizing-res.o"
compile "$out/classic-start-sizing-test.exe" -municode \
    "$root/tests/classic-start-sizing.c" "$out/classic-start-sizing-res.o" \
    -luxtheme -lcomctl32 -lgdi32
