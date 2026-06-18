#!/usr/bin/env bash
# Cross-compile a standalone Windows .exe from Linux using MinGW-w64.
# The result is copied to dist/OkaySpace.exe.
#
# One-time setup (Debian/Ubuntu):  sudo apt-get install -y mingw-w64
set -euo pipefail
cd "$(dirname "$0")/.."

if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    echo "error: MinGW-w64 not found. Install it with:" >&2
    echo "    sudo apt-get install -y mingw-w64" >&2
    exit 1
fi

BUILD_DIR=build-win
cmake -S . -B "$BUILD_DIR" \
      -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DOKAY_BUILD_TESTS=OFF
cmake --build "$BUILD_DIR" -j

mkdir -p dist
cp "$BUILD_DIR/bin/sandbox.exe" dist/OkaySpace.exe
echo
echo "Created dist/OkaySpace.exe — copy it to Windows and double-click to play."
