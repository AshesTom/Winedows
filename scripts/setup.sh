#!/bin/bash
# Set up the Windows 7 shell session on this Debian 13 machine, from a fresh install
# (Arch Linux and its derivatives such as CachyOS: the packages come from pacman).
#
#   scripts/setup.sh --media ISO [--greeter | --no-greeter] [--dpi N] [--wayland]
#                    [--wine-install DIR | --jobs N]
#
#   --media ISO    the user's Windows 7 SP1 x64 ISO; the shell is in the language
#                  of its media
#   --greeter      also make LightDM show Windows 7's logon screen (greeter/);
#                  LightDM becomes the display manager
#   --no-greeter   keep the display manager's own login screen (the default);
#                  Windows 7's cursors are still installed for the session
#   --dpi N        the screen's DPI as Windows sets it (LogPixels): 96 (100%),
#                  120 (125%) or any value from 19 to 480
#   --wayland      also build win4wine's labwc (wayland/build.sh) and register the
#                  shell's Wayland session ("Windows 7 Shell (Wine, Wayland)"):
#                  per-monitor and fractional scales (session.json "outputs"),
#                  Wayland applications with the shell's frames
#   --wine-install DIR
#                  use a Wine built elsewhere with this repository's patches (the
#                  .local/wine-install of another Debian 13 machine, copied anywhere)
#                  instead of building it here
#   --jobs N       parallel jobs for the Wine build (default: the number of CPUs)
#   --no-shadowfb  on a virtual machine whose graphics card X draws into directly
#                  (QEMU's standard VGA, Hyper-V, VirtualBox's VBoxVGA/VBoxSVGA),
#                  do not make X draw in memory first (see "Virtual machines" below)
#
# Run it as the user who will log in to the Windows 7 shell (not as root); it uses
# sudo for packages and system files. It builds Wine with this repository's
# patches, extracts the Windows 7 files, creates the Windows 7 prefix and registers
# the "Windows 7 Shell" session. Steps already done are skipped, so it can be run
# again after a failure. Everything it builds stays in .local/ of this checkout.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
local=$root/.local
media=
lang=
greeter=no
jobs=$(nproc)
dpi=
wayland=no
prebuilt=
shadowfb=yes
while (($#)); do
    case $1 in
        --media) media=${2:?--media needs a path}; shift 2 ;;
        --greeter) greeter=yes; shift ;;
        --no-greeter) greeter=no; shift ;;
        --jobs) jobs=${2:?--jobs needs a number}; shift 2 ;;
        --dpi) dpi=${2:?--dpi needs a number}; shift 2 ;;
        --wayland) wayland=yes; shift ;;
        --no-shadowfb) shadowfb=no; shift ;;
        --wine-install) prebuilt=$(realpath -e -- "${2:?--wine-install needs a directory}"); shift 2 ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
        *) printf 'Unknown option %s (see --help)\n' "$1" >&2; exit 2 ;;
    esac
done
[[ -n $media ]] || { printf 'Give the Windows 7 ISO with --media (see --help)\n' >&2; exit 2; }
[[ -z $dpi || ( $dpi =~ ^[0-9]+$ && $dpi -ge 19 && $dpi -le 480 ) ]] ||
    { printf -- '--dpi is a number from 19 to 480\n' >&2; exit 2; }
[[ -z $media ]] || media=$(realpath -e -- "$media")
[[ $EUID -ne 0 ]] || { printf 'Run this as the user who will log in, not as root\n' >&2; exit 2; }
[[ $root != *[[:space:]]* ]] || { printf 'The checkout path must not contain spaces: %s\n' "$root" >&2; exit 2; }
# Debian 13 is the tested system; Arch Linux and its derivatives (CachyOS,
# EndeavourOS, ...) get their packages from pacman instead of apt.
if command -v pacman >/dev/null && ! command -v apt-get >/dev/null; then
    distro=arch
else
    distro=debian
    if ! grep -q '^VERSION_CODENAME=trixie' /etc/os-release; then
        printf 'Warning: written for Debian 13 (trixie); this is %s\n' "$(. /etc/os-release; echo "$PRETTY_NAME")" >&2
    fi
fi
step() { printf '\n=== %s\n' "$*"; }
# Japanese input (session.json "input_method": "fcitx5"), only for a Japanese
# shell: the packages also start fcitx5 in the other desktops of the machine
# (its autostart entry), where its own layout (us) replaces the desktop's.
install_input_method() {
    if [[ $distro == arch ]]; then
        mapfile -t missing < <(pacman -T fcitx5 fcitx5-mozc fcitx5-gtk fcitx5-qt fcitx5-configtool || true)
        ((${#missing[@]} == 0)) || sudo pacman -S --needed --noconfirm "${missing[@]}"
    else
        missing=()
        for package in fcitx5 fcitx5-mozc fcitx5-frontend-gtk3 fcitx5-frontend-gtk4 \
                fcitx5-frontend-qt5 fcitx5-frontend-qt6 fcitx5-config-qt; do
            dpkg-query -W -f='${Status}' "$package" 2>/dev/null | grep -q 'install ok installed' || missing+=("$package")
        done
        ((${#missing[@]} == 0)) ||
            sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${missing[@]}"
    fi
}
mkdir -p "$local"

# The same packages on Arch Linux (pacman). Arch's display manager is the
# target of display-manager.service.
arch_display_manager() {
    basename "$(readlink -f /etc/systemd/system/display-manager.service 2>/dev/null)" .service
}
arch_packages() {
    packages=(
        # Wine build (WoW64: 64-bit Unix libraries, i686 and x86_64 MinGW for the PE modules)
        base-devel flex bison pkgconf git ca-certificates gettext
        libx11 libxext libxrender libxrandr libxi libxcursor libxinerama
        freetype2 fontconfig gnutls libglvnd alsa-lib dbus systemd-libs
        mingw-w64-gcc
        # extraction, prefix creation (xvfb-run is in xorg-server-xvfb)
        cabextract xorg-server-xvfb xorg-xauth
        # the session: X, the WM and tray bridge, recovery terminal, root cursor
        xorg-server xorg-xsetroot xorg-setxkbmap xterm python python-xlib
        python-gobject glib2 gdk-pixbuf2 librsvg
    )
    # 7z: 7zip, unless p7zip (which conflicts with it) already gives one
    command -v 7z >/dev/null || packages+=(7zip)
    if [[ $wayland == yes ]]; then
        packages+=(
            # labwc and wlroots (wayland/build.sh), Xwayland, the outputs' settings
            meson ninja wayland-protocols wayland libxkbcommon libxml2
            cairo pango glib2 libinput pixman libpng
            librsvg libdrm mesa seatd libdisplay-info
            libliftoff lcms2 hwdata libxcb xcb-util-errors xcb-util-wm xcb-util-renderutil
            xorg-xwayland wlr-randr
        )
    fi
    if [[ $greeter == yes ]]; then
        packages+=(lightdm lightdm-gtk-greeter gtk3 python-cairo)
    elif [[ -z $(arch_display_manager) ]]; then
        # No display manager yet: LightDM with its usual login screen.
        packages+=(lightdm lightdm-gtk-greeter)
    fi
    mapfile -t missing < <(pacman -T "${packages[@]}" || true)
    if ((${#missing[@]})); then
        sudo pacman -S --needed --noconfirm "${missing[@]}"
    else
        printf 'All installed\n'
    fi
    if [[ $(arch_display_manager) != lightdm ]] && [[ $greeter == yes || -z $(arch_display_manager) ]]; then
        printf 'Making LightDM the display manager (it was %s)\n' "$(arch_display_manager || true)"
        sudo systemctl enable --force lightdm.service
    fi
}

step 'Packages'
if [[ $distro == arch ]]; then
    arch_packages
else
    packages=(
        # Wine build (WoW64: 64-bit Unix libraries, i686 and x86_64 MinGW for the PE modules)
        build-essential flex bison pkg-config git ca-certificates
        # Wine's translations (msgfmt): Wine's own programs and dialogs in the session's language
        gettext
        libx11-dev libxext-dev libxrender-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev
        libfreetype-dev libfontconfig-dev libgnutls28-dev libgl-dev libasound2-dev libdbus-1-dev libudev-dev
        gcc-mingw-w64-i686 gcc-mingw-w64-x86-64
        # extraction, prefix creation
        cabextract p7zip-full xvfb xauth
        # the session: X, the WM and tray bridge, recovery terminal, root cursor
        xserver-xorg x11-xserver-utils x11-xkb-utils xterm python3 python3-xlib
        python3-gi gir1.2-glib-2.0 gir1.2-gdkpixbuf-2.0 librsvg2-common
        locales
    )
    if [[ $wayland == yes ]]; then
        packages+=(
            # labwc and wlroots (wayland/build.sh), Xwayland, the outputs' settings
            meson ninja-build wayland-protocols libwayland-dev libxkbcommon-dev libxml2-dev
            libcairo2-dev libpango1.0-dev libglib2.0-dev libinput-dev libpixman-1-dev libpng-dev
            librsvg2-dev libdrm-dev libgbm-dev libegl-dev libgles-dev libseat-dev libdisplay-info-dev
            libliftoff-dev liblcms2-dev hwdata libxcb1-dev libxcb-composite0-dev libxcb-dri3-dev
            libxcb-errors-dev libxcb-ewmh-dev libxcb-icccm4-dev libxcb-present-dev libxcb-randr0-dev
            libxcb-render0-dev libxcb-render-util0-dev libxcb-res0-dev libxcb-shm0-dev
            libxcb-xfixes0-dev libxcb-xinput-dev xwayland wlr-randr
        )
    fi
    if [[ $greeter == yes ]]; then
        packages+=(lightdm lightdm-gtk-greeter gir1.2-lightdm-1 gir1.2-gtk-3.0 python3-gi-cairo)
    elif [[ ! -e /etc/X11/default-display-manager ]]; then
        # No display manager yet: LightDM with its usual login screen.
        packages+=(lightdm lightdm-gtk-greeter)
    fi
    missing=()
    for package in "${packages[@]}"; do
        dpkg-query -W -f='${Status}' "$package" 2>/dev/null | grep -q 'install ok installed' || missing+=("$package")
    done
    if ((${#missing[@]})); then
        sudo apt-get update
        sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${missing[@]}"
    else
        printf 'All installed\n'
    fi
    if [[ $greeter == yes && $(cat /etc/X11/default-display-manager 2>/dev/null) != /usr/sbin/lightdm ]]; then
        printf 'Making LightDM the display manager (it was %s)\n' "$(cat /etc/X11/default-display-manager 2>/dev/null || echo none)"
        echo /usr/sbin/lightdm | sudo tee /etc/X11/default-display-manager >/dev/null
        sudo systemctl enable --force lightdm.service
    fi
fi

# The Unix locale of the session and its Wine prefix, in the language of the
# Windows 7 media.
make_locale() {
    locale=ja_JP.UTF-8
    [[ $lang == ja ]] || locale=en_US.UTF-8
    [[ $lang != ja ]] || install_input_method
    step "Locale $locale"
    if ! locale -a | grep -qix "${locale%.UTF-8}.utf8"; then
        sudo sed -i "s/^# *\\($locale UTF-8\\)/\\1/" /etc/locale.gen
        grep -q "^$locale UTF-8" /etc/locale.gen || echo "$locale UTF-8" | sudo tee -a /etc/locale.gen >/dev/null
        sudo locale-gen
    else
        printf '%s exists\n' "$locale"
    fi
}

step 'Wine'
wine=$local/wine-install/bin/wine
# The patches of this checkout; a build records those it was made with.
patches=$(python3 "$root/wine/patch-state.py" --digest "$root/wine/patches")
if [[ -n $prebuilt ]]; then
    [[ -f $prebuilt/lib/wine/x86_64-windows/win4wine-controls.dll && -x $prebuilt/bin/wine ]] ||
        { printf '%s is not a Wine install built by wine/build.sh\n' "$prebuilt" >&2; exit 1; }
    wine=$prebuilt/bin/wine
    printf 'Using %s: %s\n' "$prebuilt" "$("$wine" --version)"
    [[ $(cat "$prebuilt/win4wine-patches" 2>/dev/null) == "$patches" ]] ||
        printf 'Warning: %s was not built with the patches of this checkout\n' "$prebuilt" >&2
elif [[ -x $wine && -f $local/wine-install/lib/wine/x86_64-windows/win4wine-controls.dll &&
        $(cat "$local/wine-install/win4wine-patches" 2>/dev/null) == "$patches" ]]; then
    printf 'Built: %s\n' "$("$wine" --version)"
else
    if [[ -x $wine ]]; then
        printf 'The patches changed since the last build; rebuilding what they touch\n'
    fi
    printf 'Building Wine with %s jobs (log: .local/wine-build.log); this takes a long time\n' "$jobs"
    JOBS=$jobs python3 "$root/scripts/progress.py" "$local/wine-build.log" Wine -- "$root/wine/build.sh" ||
        { tail -20 "$local/wine-build.log" >&2; printf 'Wine build failed; see .local/wine-build.log\n' >&2; exit 1; }
    "$wine" --version
fi

step 'Bridge'
"$root/bridge-wm/build.sh"
printf 'Built .local/bridge\n'

labwc=$local/labwc-install/bin/labwc
if [[ $wayland == yes ]]; then
    step 'Wayland compositor'
    labwc_patches=$(python3 "$root/wine/patch-state.py" --digest "$root/wayland/patches/labwc" "$root/wayland/patches/wlroots")
    if [[ -x $labwc && $(cat "$local/labwc-install/win4wine-patches" 2>/dev/null) == "$labwc_patches" ]]; then
        printf 'Built: %s\n' "$("$labwc" --version)"
    else
        [[ ! -x $labwc ]] || printf 'The patches changed since the last build; rebuilding what they touch\n'
        printf 'Building labwc and wlroots (log: .local/labwc-build.log)\n'
        JOBS=$jobs python3 "$root/scripts/progress.py" "$local/labwc-build.log" labwc -- "$root/wayland/build.sh" ||
            { tail -20 "$local/labwc-build.log" >&2; printf 'labwc build failed; see .local/labwc-build.log\n' >&2; exit 1; }
        "$labwc" --version
    fi
fi

step 'Windows 7 files'
assets=$local/assets-win7
wanted=$(sed -n 's/^EXTRACT_VERSION = //p' "$root/extract/win7.py")
have=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1])).get("extract_version", 1))' \
    "$assets/media.json" 2>/dev/null || echo 0)
if [[ -f $assets/manifest.json && $have == "$wanted" ]]; then
    printf 'Extracted already: %s\n' "$assets"
else
    # 7z needs room for install.wim; keep it out of a small /tmp.
    mkdir -p "$local/tmp"
    rm -rf "$assets.new"
    TMPDIR=$local/tmp python3 "$root/extract/win7.py" "$media" "$assets.new"
    # An earlier extraction: replaced; the prefix's stage takes the new files.
    rm -rf "$assets"
    mv "$assets.new" "$assets"
fi
lang=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["lang"])' "$assets/media.json")
printf 'Windows 7 in %s\n' "$([[ $lang == ja ]] && echo Japanese || echo English)"
make_locale

step 'Windows 7 shell prefix'
prefix=$local/prefix-win7
if [[ -f $prefix/win7-shell.json ]]; then
    printf 'Created already: %s\n' "$prefix"
else
    [[ ! -e $prefix ]] || { printf '%s exists but is incomplete; move it away and run again\n' "$prefix" >&2; exit 1; }
    xvfb-run -a -s '-screen 0 1280x800x24' python3 "$root/prefix/win7.py" create "$assets" "$prefix" \
        --home "$HOME" --wine "$wine" > "$local/prefix-win7.log" 2>&1 ||
        { tail -20 "$local/prefix-win7.log" >&2; printf 'Prefix creation failed; see .local/prefix-win7.log\n' >&2; exit 1; }
    printf 'Created %s\n' "$prefix"
fi
if [[ -n $dpi ]]; then
    WINEPREFIX=$prefix WINE=$wine python3 "$root/prefix/display.py" --dpi "$dpi" >> "$local/prefix-win7.log" 2>&1 ||
        { tail -20 "$local/prefix-win7.log" >&2; printf 'Setting the DPI failed; see .local/prefix-win7.log\n' >&2; exit 1; }
    WINEPREFIX=$prefix "$(dirname "$wine")/wineserver" -w 2>/dev/null || true
    printf 'DPI: %s\n' "$dpi"
fi

step 'Session'
config=${XDG_CONFIG_HOME:-$HOME/.config}/win4wine/win7-session.json
if [[ -f $config ]]; then
    # Kept, with the Wine and the language of the prefix (a new prefix may be
    # made with another Wine, or be in another language)
    python3 - "$config" "$locale" "$wine" <<'EOF'
import json, sys
config, locale, wine = sys.argv[1:]
settings = json.load(open(config))
changes = []
if settings.get('wine') != wine:
    settings['wine'] = wine
    changes.append(f'Wine {wine}')
if settings.get('locale') != locale:
    settings['locale'] = locale
    if locale.startswith('ja'):
        settings['input_method'] = 'fcitx5'
    else:
        settings.pop('input_method', None)
    changes.append(f'locale {locale}')
if settings.get('shell') == 'win7' and settings.get('cursor_theme') == '':
    # Made before the Windows 7 shell had its cursors
    settings['cursor_theme'] = 'win4wine-win7'
    changes.append('Windows 7 cursors')
if changes:
    open(config, 'w').write(json.dumps(settings, indent=2) + '\n')
    print(f'Updated {config}: {", ".join(changes)}')
else:
    print(f'Keeping {config}')
EOF
else
    mkdir -p "$(dirname "$config")"
    python3 - "$config" "$wine" "$prefix" "$locale" <<'EOF'
import json, sys
config, wine, prefix, locale = sys.argv[1:]
settings = {'wine': wine, 'prefix': prefix, 'locale': locale}
if locale.startswith('ja'):
    settings['input_method'] = 'fcitx5'
settings.update(shell='win7', cursor_theme='win4wine-win7')
open(config, 'w').write(json.dumps(settings, indent=2) + '\n')
EOF
    printf 'Wrote %s\n' "$config"
fi
if [[ $wayland == yes ]]; then
    "$root/session/install.sh" --wayland "$labwc"
else
    "$root/session/install.sh"
fi

step 'Virtual machines'
# Without a GPU of its own, X's modesetting driver draws straight into the
# graphics memory of these virtual cards, and every window moved is read back
# from it, which is slow (most of the X server's work while dragging a window
# in QEMU). Drawing in memory first (ShadowFB) leaves only writes to it.
# Cards whose memory is the guest's own (virtio-gpu, VMware's) need nothing.
drivers=()
for card in /sys/class/drm/card*/device/driver; do
    [[ -e $card ]] || continue
    driver=$(basename "$(readlink -f "$card")")
    case $driver in
        bochs-drm|bochs|hyperv_drm|vboxvideo|cirrus|cirrus-qemu) drivers+=("$driver") ;;
    esac
done
xorg_conf=/etc/X11/xorg.conf.d/20-win4wine-shadowfb.conf
if [[ ${#drivers[@]} -eq 0 ]]; then
    printf 'No virtual graphics card that needs it\n'
elif [[ $shadowfb == no ]]; then
    printf 'Left X as it is (--no-shadowfb)\n'
else
    sudo mkdir -p /etc/X11/xorg.conf.d
    sudo tee "$xorg_conf" >/dev/null <<EOF
# Written by win4wine's scripts/setup.sh: X draws in memory first on virtual
# graphics cards whose memory is slow to read back (moving windows). Remove
# this file to undo; it applies to every X session from the next start of X.
Section "OutputClass"
    Identifier "win4wine: shadow framebuffer on virtual graphics cards"
    MatchDriver "$(IFS='|'; echo "${drivers[*]}")"
    Driver "modesetting"
    Option "ShadowFB" "true"
EndSection
EOF
    printf 'Wrote %s (%s)\n' "$xorg_conf" "${drivers[*]}"
fi

step 'Login screen and cursors'
# Windows 7's Aero cursors for the session (and the logon screen).
theme=$(mktemp -d)
python3 "$root/greeter/cursors.py" --win7 "$assets/cursors/user32.dll" "$assets/cursors" "$theme/win4wine-win7"
sudo rm -rf /usr/share/icons/win4wine-win7
sudo cp -r "$theme/win4wine-win7" /usr/share/icons/
sudo chmod -R a+rX /usr/share/icons/win4wine-win7
rm -rf "$theme"
printf 'Installed Windows 7 cursors\n'
if [[ $greeter == yes ]]; then
    "$root/greeter/install.sh" "$assets" \
        "$([[ $wayland == yes ]] && echo windows7-shell-wayland || echo windows7-shell)"
fi

step 'Done'
if [[ $greeter == yes ]]; then
    printf 'Restart the display manager (or reboot): the Windows 7 logon screen logs in to the Windows 7 shell.\n'
else
    printf 'Restart the display manager (or reboot) and choose "Windows 7 Shell (Wine, X11)" at the login screen.\n'
fi
if [[ $wayland == yes ]]; then
    printf 'The Wayland session is "Windows 7 Shell (Wine, Wayland)"; outputs and scales: win7-session.json "outputs".\n'
fi
printf 'Logs of the session: ~/.local/state/win4wine/\n'
