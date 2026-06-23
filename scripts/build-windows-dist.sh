#!/usr/bin/env bash
# Cross-compile the full Windows distribution — launcher, editor, and player —
# from Linux with MinGW-w64, bundling the one SDL2.dll they share. Output goes
# to build-win-dist/.
#
# This is what CI (.github/workflows/release.yml) runs to publish Windows builds,
# and you can run it locally too.
#
# Requirements:
#   * MinGW-w64:            sudo apt-get install -y mingw-w64 cmake ninja-build
#   * SDL2 MinGW dev files: download SDL2-devel-<ver>-mingw.tar.gz from
#                           https://github.com/libsdl-org/SDL/releases, extract,
#                           and point SDL2_MINGW_PREFIX at its x86_64-w64-mingw32
#                           subfolder (the one containing bin/SDL2.dll and
#                           lib/cmake/SDL2).
#
# Env:
#   SDL2_MINGW_PREFIX  (required) SDL2 MinGW prefix, see above.
#   OKAY_IMGUI_SRC     (optional) local Dear ImGui checkout to use instead of
#                      fetching it (handy offline / behind a restricted network).
set -euo pipefail
cd "$(dirname "$0")/.."

: "${SDL2_MINGW_PREFIX:?Set SDL2_MINGW_PREFIX to the SDL2 MinGW prefix (contains bin/SDL2.dll and lib/cmake/SDL2)}"

if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    echo "error: MinGW-w64 not found. Install it with: sudo apt-get install -y mingw-w64" >&2
    exit 1
fi
if [ ! -f "$SDL2_MINGW_PREFIX/bin/SDL2.dll" ]; then
    echo "error: SDL2.dll not found under SDL2_MINGW_PREFIX=$SDL2_MINGW_PREFIX" >&2
    exit 1
fi

BUILD_DIR=build-win
OUT_DIR=build-win-dist

EXTRA=()
[ -n "${OKAY_IMGUI_SRC:-}" ] && EXTRA+=("-DFETCHCONTENT_SOURCE_DIR_IMGUI=$OKAY_IMGUI_SRC")

cmake -S . -B "$BUILD_DIR" \
      -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DOKAY_BUILD_TESTS=OFF -DOKAY_BUILD_SANDBOX=OFF \
      -DOKAY_BUILD_LAUNCHER=ON -DOKAY_BUILD_PLAYER=ON -DOKAY_BUILD_EDITOR=ON \
      -DCMAKE_PREFIX_PATH="$SDL2_MINGW_PREFIX" \
      -DCMAKE_FIND_ROOT_PATH="/usr/x86_64-w64-mingw32;$SDL2_MINGW_PREFIX" \
      "${EXTRA[@]}"
cmake --build "$BUILD_DIR" -j

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"
cp "$BUILD_DIR/bin/OkaySpace.exe" \
   "$BUILD_DIR/bin/OkayEngine.exe" \
   "$BUILD_DIR/bin/OkaySpacePlayer.exe" \
   "$SDL2_MINGW_PREFIX/bin/SDL2.dll" \
   "$OUT_DIR/"
[ -f dist/VERSION.txt ] && cp dist/VERSION.txt "$OUT_DIR/"
[ -f docs/accounts.md ] && cp docs/accounts.md "$OUT_DIR/"

echo
echo "Built Windows distribution in $OUT_DIR/:"
ls -lh "$OUT_DIR"
