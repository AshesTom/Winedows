#!/bin/bash
# Install the native XP session as XP's own setup would: its text-mode screens in this
# terminal (setup/textmode.py: Welcome, the licence, the copy while XP's files are taken
# from the ISO and the programs built), then its graphical phase full screen
# (setup/billboard.py) while the session is copied to PREFIX; Windows Welcome
# (greeter/xp-native-oobe.py) comes at the first logon. Every word and picture of those
# screens is read from the user's ISO.
#
#   ./install.sh --media ISO [--prefix DIR] [--system] [--greeter]
#   ./install.sh --demo [--media ISO]
#
#   --demo          every screen, with nothing built, copied or registered: the copy gauge
#                   and the billboard run on their own (its panels every 6 seconds instead
#                   of 36), then Windows Welcome, whose choices are thrown away. --media is
#                   needed only the first time (for XP's setup files and fonts).
#   --media ISO     the Windows XP ISO (English or Japanese, SP3)
#   --prefix DIR    where the session goes (default: ~/.local/opt/winedows)
#   --system        add "Windows XP (native)" to the display manager's sessions (sudo)
#   --greeter       make XP's Welcome screen LightDM's greeter (sudo; greeter/install.sh)
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")" && pwd)
media= prefix=$HOME/.local/opt/winedows system=() greeter=() demo=()
while (($#)); do
    case $1 in
        --media) media=$2; shift 2 ;;
        --prefix) prefix=$2; shift 2 ;;
        --system) system=(--system); shift ;;
        --greeter) greeter=(--greeter); shift ;;
        --demo) demo=(--demo); shift ;;
        *) sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 2 ;;
    esac
done
setup_media=$root/.local/setup-media assets=$root/.local/native-assets
if [[ -z $media ]]; then
    have=1
    for f in usetup.exe eula.txt winntbbu.dll syssetup.dll; do [[ -f $setup_media/$f ]] || have=0; done
    [[ -n ${demo[*]} && $have == 1 && -f $assets/fonts/tahoma.ttf ]] ||
        { echo "Give the Windows XP ISO: $0 --media /path/to/xp.iso" >&2; exit 2; }
elif [[ ! -f $media ]]; then
    echo "$media: no such file" >&2
    exit 2
fi

missing=()
tools=(python3 cmake bsdtar c++)
((${#demo[@]})) && tools=(python3 bsdtar)
for tool in "${tools[@]}"; do command -v "$tool" >/dev/null || missing+=("$tool"); done
python3 -c 'import Xlib, gi, cairo' 2>/dev/null || missing+=(python-xlib/python-gobject/python-cairo)
if ((${#missing[@]})); then
    echo "Missing: ${missing[*]}" >&2
    echo "Arch: sudo pacman -S --needed cmake qt6-base qt6-multimedia qt6-declarative libarchive python-xlib python-gobject python-cairo" >&2
    echo "Debian: sudo apt install cmake qt6-base-dev qt6-multimedia-dev qt6-declarative-dev libarchive-tools python3-xlib python3-gi python3-gi-cairo" >&2
    exit 1
fi
# What needs the administrator's rights is asked for now, before the screens take the terminal
if ((${#demo[@]})); then
    system=() greeter=()
    # The billboard draws with XP's fonts: take XP's files the first time (nothing is built)
    [[ -f $assets/fonts/tahoma.ttf ]] || python3 "$root/native-shell/extract.py" "$media" "$assets" > /dev/null
fi
((${#system[@]} + ${#greeter[@]})) && sudo -v

mkdir -p "$root/.local"
log=$root/.local/install.log
: > "$log"
status=0
python3 "$root/setup/textmode.py" ${media:+--media "$media"} --setup "$setup_media" --log "$log" "${demo[@]}" || status=$?
case $status in
    0) ;;
    3) echo "Windows XP has not been installed on this computer."; exit 0 ;;
    *) echo "Setup failed; see $log" >&2; exit 1 ;;
esac

work=(python3 "$root/setup/install_files.py" --prefix "$prefix" "${system[@]}" "${greeter[@]}")
panels=()
if ((${#demo[@]})); then
    work=(python3 "$root/setup/install_files.py" --demo)
    panels=(--panel-seconds 6)
fi
if [[ -n ${DISPLAY:-} ]] && python3 -c 'from Xlib import display; display.Display()' 2>/dev/null; then
    python3 "$root/setup/billboard.py" --setup "$setup_media" --assets "$assets" \
        --log "$log" "${panels[@]}" -- "${work[@]}" || { echo "Setup failed; see $log" >&2; exit 1; }
else
    # No X display (a text console): the same work, its lines in the log
    "${work[@]}" >> "$log" 2>&1 || { echo "Setup failed; see $log" >&2; exit 1; }
fi

if ((${#demo[@]})); then
    # Windows Welcome as at the first logon; its choices go to a folder thrown away after
    if [[ -n ${DISPLAY:-} ]]; then
        scratch=$(mktemp -d)
        XDG_CONFIG_HOME=$scratch python3 "$root/greeter/xp-native-oobe.py" --assets "$assets" --force || true
        [[ -f $scratch/win4wine/oobe.json ]] && { echo "Windows Welcome's choices (thrown away):"; cat "$scratch/win4wine/oobe.json"; }
        rm -rf "$scratch"
    fi
    echo "Demo finished: nothing was installed."
    exit 0
fi
echo "Installed in $prefix."
if ((${#system[@]})); then
    echo "Choose \"Windows XP (native)\" at the login screen. Windows Welcome comes at the first logon."
else
    echo "Start it with: $prefix/native-shell/session.sh (as an X session, e.g. from ~/.xinitrc), or add --system."
fi
