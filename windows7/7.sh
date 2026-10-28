#!/bin/bash
# The Windows 7 shell's session in a window of the current desktop, for debugging
# (Arch / CachyOS, no system files touched; scripts/setup.sh installs the real session).
#
#   windows7/7.sh setup ISO       build Wine and the bridge, extract the Windows 7 files, create the prefix
#   windows7/7.sh run [WxH]       open the Windows 7 session in a window (default: 1280x800): a rootful
#                                 Xwayland on a Wayland desktop (drawn by the GPU), Xephyr on an X one
#   windows7/7.sh log [NAME]      follow a log of the running session (default: xp-shell, the shell's)
#   windows7/7.sh kill            stop the session and everything that runs in the prefix
#
# Switches, for a test copy beside your own:
#   W7_PREFIX     the Wine prefix (default: .local/prefix-win7, the one scripts/setup.sh makes)
#   W7_LOG        the session's state and log directory (default: .local/w7-run)
#   W7_WINE       the Wine to run (default: .local/wine-install/bin/wine)
#   W7_DISPLAY    the nested X display (default: the first free one from :5)
#   W7_WINEDEBUG  Wine's debug channels (default: +timestamp,fixme-all)
#   W7_AERO       1 (the default): Aero glass (desktop composition, Wine patches
#                 0152 and 0153; the session starts its compositor, w7-dwm);
#                 0: Windows 7 Basic
#   W7_NESTED     the nested X server: xwayland or xephyr (default: xwayland on a
#                 Wayland desktop that has it, else xephyr)
#   W7_HOME       the home the prefix's folders link to, for a new prefix (default: $HOME)
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
local=$root/.local
assets=$local/assets-win7
prefix=$(realpath -m -- "${W7_PREFIX:-$local/prefix-win7}")
state=$(realpath -m -- "${W7_LOG:-$local/w7-run}")
wine=$(realpath -m -- "${W7_WINE:-$local/wine-install/bin/wine}")
aero=${W7_AERO:-1}
packages='cabextract 7zip mingw-w64-gcc xorg-server-xephyr xorg-xwayland xorg-server-xvfb python-xlib python-gobject python-cairo xterm xorg-xsetroot libxcomposite libxdamage libxfixes mesa'

need() {
    local tool missing=()
    for tool in "$@"; do command -v "$tool" >/dev/null || missing+=("$tool"); done
    python3 -c 'import Xlib, gi, cairo' 2>/dev/null || missing+=(python-xlib)
    if ((${#missing[@]})); then
        printf 'Missing %s: sudo pacman -S --needed %s\n' "${missing[*]}" "$packages" >&2
        exit 1
    fi
}

# Our nested display: the one recorded by the last run while its Xephyr is alive.
running_display() {
    local display
    display=$(cat "$state/display" 2>/dev/null) || return 1
    [[ -e /tmp/.X11-unix/X${display#:} ]] && pgrep -f "^(Xephyr|Xwayland) $display " >/dev/null && echo "$display"
}

stop_prefix() {
    [[ -x $wine ]] || return 0
    WINEPREFIX=$prefix timeout 20 "${wine%/wine}/wineserver" -k 2>/dev/null || true
    WINEPREFIX=$prefix timeout 20 "${wine%/wine}/wineserver" -w 2>/dev/null || true
}

case ${1:-} in
setup)
    media=$(realpath -e -- "${2:?usage: $0 setup /path/to/Windows7SP1x64.iso}")
    need cabextract 7z i686-w64-mingw32-gcc x86_64-w64-mingw32-gcc xvfb-run Xephyr
    [[ $root != *[[:space:]]* ]] || { echo "The checkout path must not contain spaces: $root" >&2; exit 2; }
    [[ $EUID -ne 0 ]] || { echo "Run this as your user, not as root" >&2; exit 2; }
    mkdir -p "$local"
    if [[ -z ${W7_WINE:-} ]]; then
        patches=$(python3 "$root/wine/patch-state.py" --digest "$root/wine/patches")
        if [[ -x $wine && $(cat "$local/wine-install/win4wine-patches" 2>/dev/null) == "$patches" ]]; then
            echo "Wine: built ($("$wine" --version))"
        else
            echo "Building Wine (log: .local/wine-build.log); this takes a long time"
            JOBS=${JOBS:-$(nproc)} "$root/wine/build.sh" > "$local/wine-build.log" 2>&1 ||
                { tail -20 "$local/wine-build.log" >&2; echo 'Wine build failed; see .local/wine-build.log' >&2; exit 1; }
        fi
    fi
    "$root/bridge-wm/build.sh"
    wanted=$(sed -n 's/^EXTRACT_VERSION = //p' "$root/extract/win7.py")
    have=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1])).get("extract_version", 1))' \
        "$assets/media.json" 2>/dev/null || echo 0)
    if [[ ! -f $assets/manifest.json || $have != "$wanted" ]]; then
        mkdir -p "$local/tmp"
        rm -rf "$assets.new"
        TMPDIR=$local/tmp python3 "$root/extract/win7.py" "$media" "$assets.new"
        rm -rf "$assets"
        mv "$assets.new" "$assets"
    fi
    if [[ ! -f $prefix/win7-shell.json ]]; then
        [[ ! -e $prefix ]] || { echo "$prefix exists but is incomplete; move it away and run again" >&2; exit 1; }
        echo "Creating the prefix (log: .local/prefix-win7.log)"
        xvfb-run -a -s '-screen 0 1280x800x24' python3 "$root/prefix/win7.py" create "$assets" "$prefix" \
            --home "${W7_HOME:-$HOME}" --wine "$wine" > "$local/prefix-win7.log" 2>&1 ||
            { tail -20 "$local/prefix-win7.log" >&2; echo 'Prefix creation failed; see .local/prefix-win7.log' >&2; exit 1; }
        stop_prefix
    fi
    echo "Done. Start it with: $0 run"
    ;;
run)
    nested=${W7_NESTED:-}
    if [[ -z $nested ]]; then
        # A rootful Xwayland draws with the GPU (glamor, DRI3) and so does the
        # compositor in it; Xephyr draws in software, which makes Aero slow.
        nested=xephyr
        [[ -n ${WAYLAND_DISPLAY:-} ]] && command -v Xwayland >/dev/null && nested=xwayland
    fi
    case $nested in
    xwayland) need Xwayland dbus-run-session ;;
    xephyr) need Xephyr dbus-run-session ;;
    *) echo "W7_NESTED must be xwayland or xephyr" >&2; exit 2 ;;
    esac
    [[ -x $wine ]] || { echo "Run $0 setup ISO first" >&2; exit 1; }
    [[ -f $prefix/win7-shell.json ]] || { echo "No Windows 7 prefix in $prefix: run $0 setup ISO first" >&2; exit 1; }
    "$root/bridge-wm/build.sh"
    size=${2:-1280x800}
    [[ $size =~ ^[0-9]+x[0-9]+$ ]] || { echo "usage: $0 run [WIDTHxHEIGHT]" >&2; exit 2; }
    "$0" kill >/dev/null 2>&1 || true
    display=${W7_DISPLAY:-}
    if [[ -z $display ]]; then
        for n in $(seq 5 99); do
            [[ -e /tmp/.X11-unix/X$n || -e /tmp/.X$n-lock ]] || { display=:$n; break; }
        done
    fi
    mkdir -p "$state"
    language=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["lang"])' "$assets/media.json" 2>/dev/null || echo en)
    python3 - "$state/win7-session.json" "$wine" "$prefix" "$state" "$language" \
        "${W7_WINEDEBUG:-+timestamp,fixme-all}" "$aero" <<'EOF'
import json, sys
path, wine, prefix, state, language, debug, aero = sys.argv[1:]
settings = dict(wine=wine, prefix=prefix, state=state, debug=debug, shell='win7',
                locale='ja_JP.UTF-8' if language == 'ja' else 'en_US.UTF-8',
                shutdown_command=['true'], reboot_command=['true'])
if language == 'ja':
    settings['input_method'] = 'fcitx5'
settings['aero'] = aero == '1'
open(path, 'w').write(json.dumps(settings, indent=2) + '\n')
EOF
    echo "$display" > "$state/display"
    dpi=$(python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); import dpi; print(dpi.prefix_dpi(sys.argv[2]))' \
        "$root/session" "$prefix" 2>/dev/null || echo 96)
    if [[ $nested == xwayland ]]; then
        Xwayland "$display" -geometry "$size" -decorate -dpi "$dpi" -nolisten tcp > "$state/xserver.log" 2>&1 &
    else
        Xephyr "$display" -screen "$size" -dpi "$dpi" -title "Windows 7 Shell (debug, $display)" -no-host-grab \
            -nolisten tcp > "$state/xserver.log" 2>&1 &
    fi
    xserver=$!
    trap 'kill $xserver 2>/dev/null || true' EXIT
    for _ in $(seq 50); do
        [[ -e /tmp/.X11-unix/X${display#:} ]] && break
        kill -0 $xserver 2>/dev/null || { cat "$state/xserver.log" >&2; exit 1; }
        sleep 0.1
    done
    # (the session starts w7-dwm for Aero glass, or stays Windows 7 Basic without it)
    [[ $aero != 1 || -x $local/bridge/w7-dwm ]] ||
        echo "W7_AERO=1 needs w7-dwm (sudo pacman -S libxcomposite libxdamage libxfixes mesa): Windows 7 Basic instead" >&2
    echo "Windows 7 session on $display ($size, $nested); logs in $state ($0 log)"
    # Only X in the window: no Wayland, and a D-Bus of its own, so that the
    # session's tray bridge does not take the desktop's StatusNotifierWatcher.
    env -u WAYLAND_DISPLAY -u WIN4WINE_IPC -u XDG_SESSION_TYPE -u SESSION_MANAGER \
        DISPLAY="$display" GDK_BACKEND=x11 QT_QPA_PLATFORM=xcb XDG_SESSION_TYPE=x11 \
        dbus-run-session -- python3 "$root/session/supervisor.py" --config "$state/win7-session.json" ||
        echo "The session ended with an error; see $state" >&2
    ;;
log)
    tail -n 50 -F "$state/${2:-xp-shell}.log"
    ;;
kill)
    if display=$(running_display); then
        pkill -f "^(Xephyr|Xwayland) $display " || true
    fi
    stop_prefix
    ;;
*)
    sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'
    exit 2
    ;;
esac
