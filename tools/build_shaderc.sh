#!/usr/bin/env bash
# Builds bgfx's shader compiler (shaderc) at the bgfx revision Bismuth pins,
# then regenerates (or checks, with --check) the embedded shader headers.
#
#   tools/build_shaderc.sh [--check]
#
# Output: build-shaderc/ (shaderc binary + bgfx sources).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${BISMUTH_SHADERC_BUILD:-$ROOT/build-shaderc}"

# bgfx.cmake revision whose submodules match cmake/Bgfx.cmake's pins.
BGFX_CMAKE_TAG="v1.118.8493-448"

mkdir -p "$BUILD"
if [ ! -d "$BUILD/bgfx.cmake" ]; then
    git clone -q --branch "$BGFX_CMAKE_TAG" --depth 1 https://github.com/bkaradzic/bgfx.cmake.git "$BUILD/bgfx.cmake"
    git -C "$BUILD/bgfx.cmake" submodule update -q --init --depth 1
fi

SHADERC="$BUILD/out/cmake/bgfx/shaderc"
if [ ! -x "$SHADERC" ]; then
    cmake -S "$BUILD/bgfx.cmake" -B "$BUILD/out" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DBGFX_BUILD_EXAMPLES=OFF -DBGFX_BUILD_TOOLS=ON -DBGFX_BUILD_TOOLS_GEOMETRY=OFF \
        -DBGFX_BUILD_TOOLS_TEXTURE=OFF -DBGFX_INSTALL=OFF -DBGFX_CONFIG_MULTITHREADED=OFF
    cmake --build "$BUILD/out" --target shaderc
fi

python3 "$ROOT/tools/compile_shaders.py" --shaderc "$SHADERC" --bgfx "$BUILD/bgfx.cmake/bgfx" "$@"
