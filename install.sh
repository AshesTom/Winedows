#!/bin/bash
# Install the native XP session as XP's own setup would: its text-mode screens in this
# terminal (setup/textmode.py: Welcome, the licence, the copy while XP's files are taken
# from the ISO and the programs built), then its graphical phase full screen
# (setup/billboard.py) while the session is copied to PREFIX; Windows Welcome
# (greeter/xp-native-oobe.py) comes at the first logon. Every word and picture of those
# screens is read from the user's ISO.
#
#   ./install.sh --media ISO [--prefix DIR] [--system] [--greeter]
#
#   --media ISO     the Windows XP ISO (English or Japanese, SP3)
#   --prefix DIR    where the session goes (default: ~/.local/opt/winedows)
#   --system        add "Windows XP (native)" to the display manager's sessions (sudo)
#   --greeter       make XP's Welcome screen LightDM's greeter (sudo; greeter/install.sh)
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")" && pwd)
media= prefix=$HOME/.local/opt/winedows system=() greeter=()
while (($#)); do
    case $1 in
        --media) media=$2; shift 2 ;;
        --prefix) prefix=$2; shift 2 ;;
        --system) system=(--system); shift ;;
        --greeter) greeter=(--greeter); shift ;;
        *) sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 2 ;;
    esac
done
[[ -n $media && -f $media ]] || { echo "Give the Windows XP ISO: $0 --media /path/to/xp.iso" >&2; exit 2; }

missing=()
for tool in python3 cmake bsdtar c++; do command -v "$tool" >/dev/null || missing+=("$tool"); done
python3 -c 'import Xlib, gi, cairo' 2>/dev/null || missing+=(python-xlib/python-gobject/python-cairo)
if ((${#missing[@]})); then
    echo "Missing: ${missing[*]}" >&2
    echo "Arch: sudo pacman -S --needed cmake qt6-base qt6-multimedia qt6-declarative libarchive python-xlib python-gobject python-cairo" >&2
    echo "Debian: sudo apt install cmake qt6-base-dev qt6-multimedia-dev qt6-declarative-dev libarchive-tools python3-xlib python3-gi python3-gi-cairo" >&2
    exit 1
fi
# What needs the administrator's rights is asked for now, before the screens take the terminal
((${#system[@]} + ${#greeter[@]})) && sudo -v

mkdir -p "$root/.local"
log=$root/.local/install.log
: > "$log"
status=0
python3 "$root/setup/textmode.py" --media "$media" --setup "$root/.local/setup-media" --log "$log" || status=$?
case $status in
    0) ;;
    3) echo "Windows XP has not been installed on this computer."; exit 0 ;;
    *) echo "Setup failed; see $log" >&2; exit 1 ;;
esac

work=(python3 "$root/setup/install_files.py" --prefix "$prefix" "${system[@]}" "${greeter[@]}")
if [[ -n ${DISPLAY:-} ]] && python3 -c 'from Xlib import display; display.Display()' 2>/dev/null; then
    python3 "$root/setup/billboard.py" --setup "$root/.local/setup-media" --assets "$root/.local/native-assets" \
        --log "$log" -- "${work[@]}" || { echo "Setup failed; see $log" >&2; exit 1; }
else
    # No X display (a text console): the same work, its lines in the log
    "${work[@]}" >> "$log" 2>&1 || { echo "Setup failed; see $log" >&2; exit 1; }
fi

echo "Installed in $prefix."
if ((${#system[@]})); then
    echo "Choose \"Windows XP (native)\" at the login screen. Windows Welcome comes at the first logon."
else
    echo "Start it with: $prefix/native-shell/session.sh (as an X session, e.g. from ~/.xinitrc), or add --system."
fi
