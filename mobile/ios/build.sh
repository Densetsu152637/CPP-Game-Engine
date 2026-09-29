#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
deps="$root/mobile/ios/.deps/SDL"
if [[ "$(uname -s)" != Darwin ]]; then
    echo 'iOS builds require macOS/Xcode' >&2
    exit 1
fi
if [[ -z "${VULKAN_SDK:-}" ]]; then
    echo 'Set VULKAN_SDK to a Vulkan SDK with its iOS package installed' >&2
    exit 1
fi
if [[ -f "$VULKAN_SDK/iOS/setup-env.sh" ]]; then
    source "$VULKAN_SDK/iOS/setup-env.sh"
elif [[ -f "$VULKAN_SDK/../iOS/setup-env.sh" ]]; then
    source "$VULKAN_SDK/../iOS/setup-env.sh"
else
    echo 'Vulkan SDK iOS/setup-env.sh was not found' >&2
    exit 1
fi
if [[ ! -f "$deps/CMakeLists.txt" ]]; then
    mkdir -p "$(dirname "$deps")"
    git clone --depth 1 --branch release-3.4.16 https://github.com/libsdl-org/SDL.git "$deps"
fi
if [[ "$(git -C "$deps" rev-parse HEAD)" != 'fa2c02bb6e21974a89ea9824bc53c9932abe5f9c' ]]; then
    echo 'SDL checkout is not the pinned 3.4.16 revision' >&2
    exit 1
fi
export GLSLC="${GLSLC:-$(command -v glslc)}"
bash "$root/mobile/prepare-shaders.sh"
cmake -S "$root/mobile" -B "$root/build/mobile-ios" -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
    -DCMAKE_OSX_ARCHITECTURES="$(uname -m)" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=16.3 \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
    -DSDL3_SOURCE_DIR="$deps"
cmake --build "$root/build/mobile-ios" --config Debug --target CPPGameEngineMobile
