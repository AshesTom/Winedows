#!/bin/bash
# The native XP session in a window of the current desktop, for debugging
# (no system files touched; install.sh installs the real session).
#
#   ./xp.sh ISO|-- [WxH]    the session in a window: its files from the ISO the
#                           first time, then -- will do (docs/native-shell.md)
#   ./xp.sh oobe [WxH]      Windows Welcome in a window, as at the first logon
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")" && pwd)
local=$root/.local
packages='cmake qt6-base qt6-multimedia qt6-declarative libarchive python-xlib python-gobject python-cairo'

need() {
    local tool missing=()
    for tool in "$@"; do command -v "$tool" >/dev/null || missing+=("$tool"); done
    python3 -c 'import Xlib, gi, cairo' 2>/dev/null || missing+=(python-xlib)
    if ((${#missing[@]})); then
        printf 'Missing %s: sudo pacman -S --needed %s\n' "${missing[*]}" "$packages" >&2
        exit 1
    fi
}

mode=session
[[ ${1:-} == oobe ]] && { mode=oobe; shift; }
source=--
if [[ $mode == session ]]; then
    source=${1:---}
    (($#)) && shift
fi
size=${1:-1280x800}
[[ $size =~ ^[0-9]+x[0-9]+$ ]] || { echo "usage: $0 ISO|-- [WIDTHxHEIGHT] | $0 oobe [WIDTHxHEIGHT]" >&2; exit 2; }
need bsdtar cmake dbus-run-session
if [[ ! -e $local/native-assets/system32/browseui.dll || ! -e $local/native-assets/fonts/arial.ttf ]]; then
    [[ $source != -- ]] || { echo "The first time, give the XP ISO: $0 /path/to/the.iso" >&2; exit 2; }
    python3 "$root/native-shell/extract.py" "$source" "$local/native-assets"
fi
"$root/native-shell/build.sh" | tail -n 1

for n in $(seq 5 99); do
    [[ -e /tmp/.X11-unix/X$n || -e /tmp/.X$n-lock ]] || { display=:$n; break; }
done
# Xephyr, or Xwayland's own window under a Wayland desktop.
if command -v Xephyr >/dev/null; then
    Xephyr "$display" -screen "$size" -title "XP native shell ($display)" -no-host-grab -nolisten tcp &
else
    Xwayland "$display" -geometry "$size" -decorate &
fi
server=$!
trap 'kill $server 2>/dev/null || true' EXIT
for _ in $(seq 50); do
    [[ -e /tmp/.X11-unix/X${display#:} ]] && break
    sleep 0.1
done
# Nothing of this window's session reaches the desktop around it; Turn Off Computer
# ends this window's session, never the machine.
run() {
    env -u WAYLAND_DISPLAY -u WIN4WINE_IPC -u SESSION_MANAGER -u DBUS_SESSION_BUS_ADDRESS \
        WIN4WINE_DESKTOP_BUS="${DBUS_SESSION_BUS_ADDRESS:-}" WIN4WINE_NATIVE_WELCOME=1 \
        DISPLAY="$display" GDK_BACKEND=x11 QT_QPA_PLATFORM=xcb XDG_SESSION_TYPE=x11 WIN4WINE_POWER=0 "$@"
}
if [[ $mode == oobe ]]; then
    run python3 "$root/greeter/xp-native-oobe.py" --assets "$local/native-assets" --force
else
    run "$root/native-shell/session.sh"
fi
