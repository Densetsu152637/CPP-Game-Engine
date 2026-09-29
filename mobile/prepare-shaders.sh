#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
compiler="${GLSLC:-glslc}"
mkdir -p "$root/mobile/build/shaders"
for shader in mesh_textured.vert mesh_textured.frag; do
    "$compiler" "$root/assets/shaders/$shader" -o "$root/mobile/build/shaders/$shader.spv"
done
