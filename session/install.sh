#!/bin/bash
# install.sh [--wayland LABWC] : the "Windows 7 Shell" X11 session, which reads
# ~/.config/win4wine/win7-session.json, and with a labwc built by
# wayland/build.sh also its Wayland session.
# Also, per user, ~/.local/bin/win4wine-wine (session/wine.py).
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
[[ $root != *$'\n'* ]] || exit 1
labwc=
if [[ ${1:-} == --wayland ]]; then
    labwc=$(realpath -e -- "${2:?--wayland needs the labwc binary}")
    [[ $labwc != *$'\n'* ]] || exit 1
fi
# As root (a fresh system without sudo) or through sudo
as_root() { if [[ $EUID -eq 0 ]]; then "$@"; else sudo "$@"; fi; }
wrapper=$(mktemp)
trap 'rm -f "$wrapper"' EXIT
printf '#!/bin/bash\nexec python3 %q --config "${XDG_CONFIG_HOME:-$HOME/.config}/win4wine/win7-session.json" "$@"\n' \
    "$root/session/supervisor.py" > "$wrapper"
as_root install -Dm755 "$wrapper" /usr/local/bin/windows7-shell-session
as_root install -Dm644 "$root/session/windows7-shell.desktop" /usr/share/xsessions/windows7-shell.desktop
printf 'Installed the Windows 7 shell Xsession. Configuration: ~/.config/win4wine/win7-session.json\n'
# win4wine-wine: "wine" as the session runs it, for terminals outside the
# session. Per user, as it runs this user's checkout and session.json.
printf '#!/bin/bash\nexec python3 %q "$@"\n' "$root/session/wine.py" > "$wrapper"
install -Dm755 "$wrapper" "$HOME/.local/bin/win4wine-wine"
printf 'Installed ~/.local/bin/win4wine-wine\n'
if [[ -n $labwc ]]; then
    printf '#!/bin/bash\nexec %q %q --config "${XDG_CONFIG_HOME:-$HOME/.config}/win4wine/win7-session.json" "$@"\n' \
        "$root/wayland/session.sh" "$labwc" > "$wrapper"
    as_root install -Dm755 "$wrapper" /usr/local/bin/windows7-shell-wayland-session
    as_root install -Dm644 "$root/session/windows7-shell-wayland.desktop" \
        /usr/share/wayland-sessions/windows7-shell-wayland.desktop
    printf 'Installed the Windows 7 shell Wayland session (%s)\n' "$labwc"
fi
