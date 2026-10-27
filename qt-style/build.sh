#!/bin/bash
# Build the Qt 6 style into .local/qt-style: styles/libwin4wine-xp.so (the
# session puts .local/qt-style in QT_PLUGIN_PATH) and bin/xpstyle-gallery.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
build=$root/.local/qt-style-build
install=$root/.local/qt-style
generator=()
command -v ninja >/dev/null && generator=(-G Ninja)
cmake -S "$root/qt-style" -B "$build" "${generator[@]}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$install"
cmake --build "$build" -j"${JOBS:-$(nproc)}"
cmake --install "$build" >/dev/null
echo "Built $install/styles/libwin4wine-xp.so"
