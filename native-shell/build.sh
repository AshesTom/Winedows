#!/bin/bash
# Build the native shell into .local/native-shell/bin/explorer.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
build=$root/.local/native-shell-build
install=$root/.local/native-shell
generator=()
command -v ninja >/dev/null && generator=(-G Ninja)
cmake -S "$root/native-shell" -B "$build" "${generator[@]}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$install" >/dev/null
cmake --build "$build" -j"${JOBS:-$(nproc)}"
cmake --install "$build" >/dev/null
echo "Built $install/bin/explorer"
