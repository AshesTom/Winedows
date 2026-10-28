#!/bin/bash
# Make LightDM show Windows 7's logon screen (greeter/win7-greeter.py).
#   greeter/install.sh ASSETS [SESSION]
#                                copy the greeter and the Windows 7 files it reads from
#                                ASSETS (extract/win7.py's output), then make it LightDM's
#                                greeter; SESSION is the default session (windows7-shell)
#   greeter/install.sh --remove  go back to the greeter LightDM used before
# Restart LightDM afterwards. Without the Windows 7 files the greeter starts lightdm-gtk-greeter.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
share=/usr/local/share/win4wine/greeter
seat=/etc/lightdm/lightdm.conf.d/95-xp-greeter.conf
sudo() { if [[ $EUID -eq 0 ]]; then "$@"; else command sudo "$@"; fi; }
if [[ ${1:-} == --remove ]]; then
    sudo rm -f "$seat"
    printf 'Removed %s; LightDM uses its previous greeter after a restart.\n' "$seat"
    exit 0
fi
assets=${1:?Usage: greeter/install.sh ASSETS [SESSION] | --remove}
ui=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["ui_language"])' "$assets/media.json")
for f in system32/authui.dll "system32/$ui/authui.dll.mui" system32/imageres.dll branding/basebrd.dll pictures/user.bmp; do
    [[ -f $assets/$f ]] || { printf 'Missing %s: extract again with extract/win7.py\n' "$assets/$f" >&2; exit 1; }
done
sudo rm -rf "$share/win7"
sudo install -d -m755 "$share/code" "$share/win7/$ui" "$share/win7/fonts"
sudo install -m644 "$root"/greeter/{welcome,xpres,xdraw,w7logon}.py "$share/code/"
sudo install -m755 "$root/greeter/win7-greeter.py" "$share/code/"
sudo install -m644 "$assets"/system32/{authui,imageres}.dll "$assets/branding/basebrd.dll" "$assets/pictures/user.bmp" \
    "$share/win7/"
sudo install -m644 "$assets/system32/$ui/authui.dll.mui" "$share/win7/$ui/"
# The session's Welcome and log-off screens (greeter/w7-splash.py); extracted since EXTRACT_VERSION 5
for f in winlogon.exe.mui wininit.exe.mui; do
    [[ ! -f $assets/system32/$ui/$f ]] || sudo install -m644 "$assets/system32/$ui/$f" "$share/win7/$ui/"
done
sudo install -m644 "$assets"/fonts/* "$share/win7/fonts/"
printf '{"assets": "%s/win7", "fallback": "lightdm-gtk-greeter"}\n' "$share" |
    sudo install -Dm644 /dev/stdin /etc/win4wine/win7-greeter.json
sudo install -Dm644 "$root/greeter/win7-greeter.desktop" /usr/share/xgreeters/win7-greeter.desktop
printf '[Seat:*]\ngreeter-session=win7-greeter\nuser-session=%s\n' "${2:-windows7-shell}" | sudo install -Dm644 /dev/stdin "$seat"
printf 'Installed the Windows 7 logon screen as %s. Settings: /etc/win4wine/win7-greeter.json\n' "$seat"
