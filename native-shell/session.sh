#!/bin/bash
# The native XP session, without Wine: bridge-wm, the tray host and the native
# shell on the current X display. The session ends with the shell (Log Off).
# As a whole X session from a display manager or startx, or in a window: xp.sh native.
#
#   session.sh [--inside]     --inside: leave the current window manager in place
#
# The shell draws the Linux windows' XP frames itself here. session.json's
# "native_shell" runs the native shell inside the Wine session instead, where
# Wine's proxy.exe draws them (session/supervisor.py).
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
shell=$root/.local/native-shell/bin/xp-native-shell
[[ -x $shell ]] || { echo "Build it first: $root/native-shell/build.sh" >&2; exit 1; }
# Started from startx: no session bus yet (the tray host's StatusNotifierItem needs one).
if [[ -z ${DBUS_SESSION_BUS_ADDRESS:-} ]] && command -v dbus-run-session >/dev/null; then
    exec dbus-run-session -- "$0" "$@"
fi
python=${WIN4WINE_PYTHON:-python3}
state=${XDG_STATE_HOME:-$HOME/.local/state}/win4wine
mkdir -p "$state"
ipc=$(mktemp -d "${XDG_RUNTIME_DIR:-/tmp}/win4wine-native-XXXXXX")
pids=()
cleanup() {
    ((${#pids[@]})) && kill "${pids[@]}" 2>/dev/null || true
    rm -rf "$ipc"
}
trap cleanup EXIT
trap 'exit 143' TERM INT HUP

if [[ ${1:-} != --inside ]]; then
    "$python" "$root/bridge-wm/wm.py" --ipc "$ipc" > "$state/native-wm.log" 2>&1 &
    pids+=($!)
    # Until it has taken the display, so that the shell's windows are managed.
    for _ in $(seq 100); do
        kill -0 "${pids[0]}" 2>/dev/null || { cat "$state/native-wm.log" >&2; exit 1; }
        grep -q . "$ipc/windows.tsv" 2>/dev/null || [[ -e $ipc/windows.tsv ]] && break
        sleep 0.05
    done
fi
"$python" "$root/bridge-wm/tray.py" --ipc "$ipc" > "$state/native-tray.log" 2>&1 &
pids+=($!)

# Display Properties' look is for this session's programs too; there is no prefix here.
export WIN4WINE_NATIVE_SHELL=wineless
export XDG_CURRENT_DESKTOP=XP-Shell
# Folders that other programs open come to the native folder windows, in this desktop only
# (xp-shell-mimeapps.list is read for XDG_CURRENT_DESKTOP, before the user's own list).
data=${XDG_DATA_HOME:-$HOME/.local/share}/applications
config=${XDG_CONFIG_HOME:-$HOME/.config}
mkdir -p "$data" "$config"
cat > "$data/xp-explorer.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Windows Explorer
Exec=$shell --explore %f
NoDisplay=true
MimeType=inode/directory;
EOF
printf '[Default Applications]\ninode/directory=xp-explorer.desktop\n' > "$config/xp-shell-mimeapps.list"
assets=$root/.local/native-assets
# XP's icons as an icon theme, made from the user's XP files the first time (session/icon_theme.py).
python3 -c 'import sys; sys.path.insert(0, sys.argv[1]); import icon_theme; icon_theme.ensure(sys.argv[2], sys.argv[3])' \
    "$root/session" "$assets" "${XDG_DATA_HOME:-$HOME/.local/share}/icons/win4wine-xp-icons" || true
# XP's fonts for Linux programs too, Tahoma before the rest, as the supervisor's font_config does.
fonts=$state/fontconfig
mkdir -p "$fonts"
cat > "$fonts/fonts.conf" <<EOF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
  <include ignore_missing="yes">/etc/fonts/fonts.conf</include>
  <dir>$assets/fonts</dir>
  <alias binding="strong"><family>sans-serif</family><prefer><family>Tahoma</family></prefer></alias>
  <alias binding="strong"><family>Sans</family><prefer><family>Tahoma</family></prefer></alias>
  <alias binding="strong"><family>system-ui</family><prefer><family>Tahoma</family></prefer></alias>
</fontconfig>
EOF
export FONTCONFIG_FILE=$fonts/fonts.conf
# Turn Off Computer really turns it off, unless this session runs in a window (xp.sh native).
export WIN4WINE_POWER=${WIN4WINE_POWER:-1}
# Qt applications take XP's style when it is built (qt-style/build.sh).
styles=$root/.local/qt-style
if [[ -e $styles/styles/libwin4wine-xp.so ]]; then
    export QT_PLUGIN_PATH=$styles${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}
    export QML_IMPORT_PATH=$styles/qml${QML_IMPORT_PATH:+:$QML_IMPORT_PATH}
    export QT_STYLE_OVERRIDE=Win4WineXP QT_QPA_PLATFORMTHEME=gtk3
fi
# What asks for the administrator's rights (Date and Time, for one): a desktop's own agent, whichever is installed.
if [[ ${1:-} != --inside ]]; then
    for agent in /usr/lib/polkit-kde-authentication-agent-1 /usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 \
                 /usr/lib/mate-polkit/polkit-mate-authentication-agent-1 /usr/lib/polkit-1-gnome/polkit-gnome-authentication-agent-1; do
        if [[ -x $agent ]]; then
            "$agent" > "$state/native-polkit.log" 2>&1 &
            pids+=($!)
            break
        fi
    done
fi
# GTK programs take a theme made from the same style, named in XSETTINGS (qt-style/tools/gtktheme.cpp).
if [[ -x $styles/bin/xpstyle-gtk ]]; then
    export GTK_OVERLAY_SCROLLING=0
    if [[ -e $styles/gtk-3.0/modules/libwin4wine-xp-gtk.so ]]; then
        export GTK_PATH=$styles/gtk-3.0${GTK_PATH:+:$GTK_PATH}
        export GTK_MODULES=${GTK_MODULES:+$GTK_MODULES:}win4wine-xp-gtk
    fi
    "$styles/bin/xpstyle-gtk" --watch > "$state/native-gtk-theme.log" 2>&1 &
    pids+=($!)
fi
"$shell" --ipc "$ipc" --frames 2>&1 | tee -a "$state/native-shell.log"
