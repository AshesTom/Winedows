#!/bin/bash
# XP Explorer: Windows XP's own shell (from your ISO) in a Wine virtual desktop.
#
#   ./xp.sh wine            build win4wine's patched Wine into .local (optional, 20-40 min)
#   ./xp.sh setup ISO       extract the XP files, build the helpers, create the prefix
#   ./xp.sh run [WxH]       open the XP desktop in a window (default: 1600x900, a mode of your screen)
#   ./xp.sh apps            refresh the Linux applications in All Programs
#   ./xp.sh pins            re-pin the Start menu's left column (pins.txt to choose)
#   ./xp.sh kill            stop everything that runs in the prefix
#   ./xp.sh reset           delete the prefix (the extracted files are kept)
# XP_PREFIX, XP_DESKTOP and XP_LOG choose another prefix, desktop name and log.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")" && pwd)
# The win4wine checkout around xp-explorer/: its Wine patches and build script
repo=$(dirname -- "$root")
assets=$root/.local/assets
export WINEPREFIX=${XP_PREFIX:-$root/.local/prefix}
# The virtual desktop's name (XP_DESKTOP=XP2 runs a second session beside the first).
desktop=${XP_DESKTOP:-XP}
export WINEDEBUG=${WINEDEBUG:--all}
# win4wine's patched Wine, built into .local by ./xp.sh wine, when there is one;
# the system's Wine is never replaced.
if [[ -z ${WINE:-} && -x $root/.local/wine-install/bin/wine ]]; then
    WINE=$root/.local/wine-install/bin/wine
    export PATH=$root/.local/wine-install/bin:$PATH
    patched=1
fi
wine=${WINE:-wine}

need() {
    for tool in "$@"; do
        command -v "$tool" >/dev/null || { echo "Missing $tool: sudo pacman -S --needed wine cabextract mingw-w64-gcc libisoburn" >&2; exit 1; }
    done
}

# Extracts GROUP's files from XP_ISO when one of FILES is missing from the
# assets (made by a version that did not extract them); without XP_ISO, they stay missing.
extract_missing() {
    local group=$1 file
    shift
    [[ -n ${XP_ISO:-} ]] || return 0
    for file in "$@"; do
        if [[ ! -e $assets/$file ]]; then
            python3 -I "$root/scripts/extract.py" --only "$group" "$XP_ISO" "$assets"
            return
        fi
    done
}

case ${1:-} in
wine)
    # Wine's configure cannot take a path with spaces: build outside the project.
    out=${XDG_DATA_HOME:-$HOME/.local/share}/xp-explorer
    mkdir -p "$out" "$root/.local"
    # win4wine's series (../wine/patches), which includes XP Explorer's patches.
    # The source keeps its patches; another series starts again from upstream.
    digest=$(python3 "$repo/wine/patch-state.py" --digest "$repo/wine/patches")
    if [[ -d $out/wine-upstream/.git && $(cat "$out/wine-upstream/.git/xp-explorer-patches" 2>/dev/null) != "$digest" ]]; then
        git -C "$out/wine-upstream" reset -q --hard
        git -C "$out/wine-upstream" clean -q -f -d
    fi
    WINE_SOURCE=$out/wine-upstream WINE_BUILD=$out/wine-build WINE_INSTALL=$out/wine-install \
        JOBS=${JOBS:-$(nproc)} "$repo/wine/build.sh"
    echo "$digest" > "$out/wine-upstream/.git/xp-explorer-patches"
    ln -sfn "$out/wine-install" "$root/.local/wine-install"
    ;;
setup)
    [[ -n ${2:-} ]] || { echo "usage: $0 setup /path/to/WindowsXP-SP3.iso" >&2; exit 2; }
    need "$wine" cabextract xorriso i686-w64-mingw32-gcc
    [[ -d $assets ]] || python3 -I "$root/scripts/extract.py" "$2" "$assets"
    "$root/helpers/build.sh"
    python3 -I "$root/scripts/prefix.py" "$assets" "$WINEPREFIX" --wine "$wine"
    echo "Done. Start it with: $0 run"
    ;;
run)
    [[ -d $WINEPREFIX ]] || { echo "Run $0 setup ISO first" >&2; exit 1; }
    size=${2:-1600x900}
    # Refresh our helpers in the prefix after a rebuild.
    cp -f "$root/.local/bin/"*.{exe,dll} "$WINEPREFIX/drive_c/xp/bin/" 2>/dev/null || true
    # XP's shell DLLs go back in place while no wineserver runs (KnownDlls);
    # this also closes an XP desktop that is already open.
    wineserver -k 2>/dev/null || true; wineserver -w 2>/dev/null || true
    python3 -I "$root/scripts/prefix.py" --stage "$WINEPREFIX" "$size"
    # XP's sounds, Explorer settings, image viewer and shell shortcuts, again when they change
    # (XP_ISO=/path/to/the.iso extracts their files when the assets predate them).
    if [[ $(cat "$WINEPREFIX/.xp-defaults" 2>/dev/null) != "$(python3 -I "$root/scripts/prefix.py" --defaults-version)" ]]; then
        extract_missing media media
        extract_missing inf inf/hivedef.inf inf/hivesft.inf
        extract_missing shell shell/shimgvw.dll
        extract_missing resources resources/user32.dll
        python3 -I "$root/scripts/prefix.py" --defaults "$WINEPREFIX" --wine "$wine"
        wineserver -k; wineserver -w
        python3 -I "$root/scripts/prefix.py" --stage "$WINEPREFIX" "$size"
    fi
    # Internet Explorer 6 and the Search Companion, for prefixes made before they were extracted
    # (XP_ISO=/path/to/the.iso extracts it when the assets predate it.)
    # After a Wine update too: wineboot registers Wine's own mshtml again.
    if [[ $(cat "$WINEPREFIX/.ie-installed" 2>/dev/null) != "$(python3 -I "$root/scripts/prefix.py" --wine-build "$wine")" ]]; then
        extract_missing ie ie/iexplore.exe
        extract_missing wmp wmp/wmplayer.exe
        extract_missing srchasst srchasst/srchui.dll
        if [[ -f $assets/ie/iexplore.exe ]]; then
            python3 -I "$root/scripts/prefix.py" --ie "$WINEPREFIX" --wine "$wine"
            wineserver -k; wineserver -w
        fi
    fi
    # A live prefix can empty Explorer's .Current again (silent navigation
    # with SND_NODEFAULT); re-mirror the effective click on every start.
    # Best effort: never blocks the desktop when Wine is busy.
    timeout 60 python3 -I "$root/scripts/prefix.py" --repair-sounds "$WINEPREFIX" --wine "$wine" 2>/dev/null || true
    # A desktop not named "Default": Wine's desktop then does not claim the
    # shell window, so XP Explorer starts as the shell (taskbar, Start menu).
    if [[ -n ${patched:-} ]]; then
        export WIN4WINE_EXTERNAL_SHELL=1 XPEXPLORER_COMPAT=0
        # mshtml stays disabled in the registry, except in Internet Explorer 6.
        export WINEDLLOVERRIDES='explorer=b;xp-explorer=n;mscoree=;uxtheme=b'
    fi
    # Wine's messages go to .local/xp-session.log (XP_WINEDEBUG=-all to silence them).
    export WINEDEBUG=${XP_WINEDEBUG:-err+all,+explorer}
    # Keeps XP's desktop window under the taskbar on the X side (see the script).
    python3 -I "$root/scripts/xstack.py" "$desktop - Wine Desktop" &
    exec "$wine" explorer "/desktop=$desktop,$size" 'C:\xp\bin\xpstart.exe' >"${XP_LOG:-$root/.local/xp-session.log}" 2>&1
    ;;
apps)
    python3 -I "$repo/session/linux_apps.py" "$WINEPREFIX" --wine "$wine" --flat --helpers "$root/.local/bin"
    ;;
pins)
    wineserver -k 2>/dev/null || true; wineserver -w 2>/dev/null || true
    python3 -I "$root/scripts/prefix.py" --stage "$WINEPREFIX"
    python3 -I "$root/scripts/pins.py" "$WINEPREFIX" --wine "$wine"
    ;;
kill)
    wineserver -k || true
    ;;
reset)
    read -r -p "Delete $WINEPREFIX? [y/N] " answer
    [[ $answer == [yY]* ]] && { wineserver -k 2>/dev/null || true; rm -rf -- "$WINEPREFIX"; echo "Prefix deleted."; }
    ;;
*)
    sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
    ;;
esac
