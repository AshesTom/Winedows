#!/bin/bash
# Make LightDM show XP's Welcome screen (greeter/xp-greeter.py).
#   greeter/install.sh ASSETS [SESSION]
#                                copy the greeter and the XP files it reads from ASSETS
#                                (extract/extract.py's output), then make it LightDM's greeter;
#                                SESSION is the default session (xp-shell, or xp-shell-wayland)
#   greeter/install.sh --reactos ASSETS LANG [SESSION]
#                                the same with ReactOS's logon screen (greeter/reactos-greeter.py)
#                                from extract/reactos.py's output, in LANG (ja or en); SESSION
#                                defaults to reactos-shell
#   greeter/install.sh --remove  go back to the greeter LightDM used before
# Restart LightDM afterwards. Without the XP files the greeter starts lightdm-gtk-greeter.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
share=/usr/local/share/win4wine/greeter
seat=/etc/lightdm/lightdm.conf.d/95-xp-greeter.conf
sudo() { if [[ $EUID -eq 0 ]]; then "$@"; else command sudo "$@"; fi; }
# LightDM reads /etc/lightdm/lightdm.conf after its conf.d directories, so a
# greeter-session there (some distributions set lightdm-gtk-greeter) wins over
# ours: say which file sets the greeter LightDM will start.
check_greeter() {
    command -v lightdm >/dev/null || return 0
    local config greeter source
    config=$(lightdm --show-config 2>/dev/null) || return 0
    greeter=$(sed -n 's/^\([A-Z]\)  greeter-session=//p' <<<"$config" | head -1)
    [[ -z $greeter || $greeter == "$1" ]] && return 0
    source=$(sed -n 's/^\([A-Z]\)  greeter-session=.*/\1/p' <<<"$config" | head -1)
    source=$(sed -n "s/^$source  //p" <<<"$config" | tail -1)
    printf 'LightDM still starts %s: %s sets greeter-session after %s. Remove or comment out that line.\n' \
        "$greeter" "${source:-another file}" "$seat" >&2
}
if [[ ${1:-} == --remove ]]; then
    sudo rm -f "$seat"
    printf 'Removed %s; LightDM uses its previous greeter after a restart.\n' "$seat"
    exit 0
fi
if [[ ${1:-} == --reactos ]]; then
    assets=${2:?Usage: greeter/install.sh --reactos ASSETS LANG [SESSION]}
    lang=${3:?Usage: greeter/install.sh --reactos ASSETS LANG [SESSION]}
    for f in system32/msgina.dll wallpaper/Nebula.bmp fonts/tahoma.ttf fonts/tahomabd.ttf fonts/DroidSansFallback.ttf; do
        [[ -f $assets/$f ]] || { printf 'Missing %s: extract again with extract/reactos.py\n' "$assets/$f" >&2; exit 1; }
    done
    sudo install -d -m755 "$share/code" "$share/reactos/system32" "$share/reactos/wallpaper" "$share/reactos/fonts"
    sudo install -m644 "$root"/greeter/{welcome,xpres,xdraw,rosdialog}.py "$share/code/"
    sudo install -m755 "$root/greeter/reactos-greeter.py" "$share/code/"
    sudo install -m644 "$assets/system32/msgina.dll" "$share/reactos/system32/"
    sudo install -m644 "$assets/wallpaper/Nebula.bmp" "$share/reactos/wallpaper/"
    sudo install -m644 "$assets"/fonts/{tahoma.ttf,tahomabd.ttf,DroidSansFallback.ttf} "$share/reactos/fonts/"
    printf '{"assets": "%s/reactos", "lang": "%s", "fallback": "lightdm-gtk-greeter"}\n' "$share" "$lang" |
        sudo install -Dm644 /dev/stdin /etc/win4wine/reactos-greeter.json
    sudo install -Dm644 "$root/greeter/reactos-greeter.desktop" /usr/share/xgreeters/reactos-greeter.desktop
    printf '[Seat:*]\ngreeter-session=reactos-greeter\nuser-session=%s\n' "${4:-reactos-shell}" | sudo install -Dm644 /dev/stdin "$seat"
    printf 'Installed the ReactOS logon screen as %s. Settings: /etc/win4wine/reactos-greeter.json\n' "$seat"
    check_greeter reactos-greeter
    exit 0
fi
assets=${1:?Usage: greeter/install.sh ASSETS | --reactos ASSETS LANG | --remove}
for f in system32/logonui.exe system32/msgina.dll system32/winlogon.exe fonts/msgothic.ttc; do
    [[ -f $assets/$f ]] || { printf 'Missing %s: extract again with extract/extract.py\n' "$assets/$f" >&2; exit 1; }
done
# LightDM runs the greeter as its own user, which cannot read a private home or checkout.
sudo install -d -m755 "$share/code" "$share/assets/system32" "$share/assets/fonts" "$share/assets/pictures" \
    "$share/assets/themes/Luna" "$share/assets/media"
sudo install -m644 "$root"/greeter/{welcome,xpres,xdraw}.py "$share/code/"
sudo install -m755 "$root/greeter/xp-greeter.py" "$share/code/"
sudo install -m644 "$assets"/system32/{logonui.exe,msgina.dll,winlogon.exe} "$share/assets/system32/"
fonts=(fonts/msgothic.ttc fonts/arial.ttf)
if [[ $(python3 "$root/extract/media.py" "$assets") == en ]]; then
    # English XP's screen is drawn in Arial, Tahoma and Franklin Gothic Medium with
    # their own bold and italic faces, its dialog in Microsoft Sans Serif.
    fonts+=(fonts/{arialbd,arialbi,ariali,tahoma,tahomabd,framd,framdit,micross}.ttf)
fi
# Only this media's fonts (another language's may be left from an earlier install).
sudo find "$share/assets/fonts" -maxdepth 1 -type f -delete
# XP's shut down sound, for the Welcome screen's Turn Off and Restart.
for f in "${fonts[@]}" themes/Luna/luna.msstyles "media/Windows XP Shutdown.wav"; do
    [[ ! -f $assets/$f ]] || sudo install -m644 "$assets/$f" "$share/assets/$f"
done
shopt -s nullglob
pictures=("$assets"/pictures/*.bmp)
[[ ${#pictures[@]} -eq 0 ]] || sudo install -m644 "${pictures[@]}" "$share/assets/pictures/"
if [[ ! -f /etc/win4wine/greeter.json ]]; then
    printf '{"assets": "%s/assets", "fallback": "lightdm-gtk-greeter"}\n' "$share" |
        sudo install -Dm644 /dev/stdin /etc/win4wine/greeter.json
fi
# XP's cursors as an X cursor theme, for the greeter, Wine and Linux applications in the session.
if [[ -f $assets/resources/user32.dll ]]; then
    theme=$(mktemp -d)
    python3 "$root/greeter/cursors.py" "$assets/resources/user32.dll" "$theme/win4wine-xp"
    sudo rm -rf /usr/share/icons/win4wine-xp
    sudo cp -r "$theme/win4wine-xp" /usr/share/icons/
    sudo chmod -R a+rX /usr/share/icons/win4wine-xp
    rm -rf "$theme"
else
    printf 'No resources/user32.dll in %s: XP cursors are not installed (extract again)\n' "$assets" >&2
fi
sudo install -Dm644 "$root/greeter/xp-greeter.desktop" /usr/share/xgreeters/xp-greeter.desktop
# The Welcome screen has no session menu: users without a last session get the XP shell.
printf '[Seat:*]\ngreeter-session=xp-greeter\nuser-session=%s\n' "${2:-xp-shell}" | sudo install -Dm644 /dev/stdin "$seat"
printf 'Installed the XP Welcome screen as %s. Settings: /etc/win4wine/greeter.json\n' "$seat"
check_greeter xp-greeter
