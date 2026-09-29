#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
deps="$root/mobile/android/.deps/SDL"
if [[ ! -f "$deps/CMakeLists.txt" ]]; then
    mkdir -p "$(dirname "$deps")"
    git clone --depth 1 --branch release-3.4.16 https://github.com/libsdl-org/SDL.git "$deps"
fi
if [[ "$(git -C "$deps" rev-parse HEAD)" != 'fa2c02bb6e21974a89ea9824bc53c9932abe5f9c' ]]; then
    echo 'SDL checkout is not the pinned 3.4.16 revision' >&2
    exit 1
fi
bash "$root/mobile/prepare-shaders.sh"
"$deps/android-project/gradlew" -p "$root/mobile/android" :app:assembleDebug
