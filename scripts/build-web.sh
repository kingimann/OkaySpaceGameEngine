#!/usr/bin/env bash
# Build the OkaySpace player for the web (WebAssembly) with Emscripten.
#
# One-time setup: install the Emscripten SDK (https://emscripten.org) and run
# `source /path/to/emsdk/emsdk_env.sh` so `emcmake`/`emcc` are on PATH.
#
# Usage:   scripts/build-web.sh [path/to/game.okayscene]
# Output:  build-web/web/okay-player.html (+ .js/.wasm) — serve over HTTP.
set -euo pipefail
cd "$(dirname "$0")/.."

if ! command -v emcmake >/dev/null 2>&1; then
    echo "error: Emscripten not found. Install emsdk and 'source emsdk_env.sh'." >&2
    exit 1
fi

SCENE="${1:-}"
PRELOAD=""
if [ -n "$SCENE" ] && [ -f "$SCENE" ]; then
    # Preload the scene into the virtual filesystem as game.okayscene so the
    # player finds it next to the executable, just like a native build.
    PRELOAD="--preload-file ${SCENE}@game.okayscene"
fi

# libsodium (packet encryption) and Assimp (model import) have no WebAssembly
# build, and the relay / OkayUI's Direct3D backend aren't part of the web player
# (the engine's CMake already skips the last two under Emscripten). SDL2 comes from
# Emscripten's own port (-sUSE_SDL=2), which Emscripten fetches once on the first
# build (needs network access to github); after that it's cached locally.
emcmake cmake -S . -B build-web \
    -DCMAKE_BUILD_TYPE=Release \
    -DOKAY_BUILD_PLAYER=ON -DOKAY_BUILD_TESTS=OFF \
    -DOKAY_BUILD_SANDBOX=OFF -DOKAY_BUILD_LAUNCHER=OFF -DOKAY_BUILD_EDITOR=OFF \
    -DOKAY_USE_SODIUM=OFF -DOKAY_USE_ASSIMP=OFF \
    -DCMAKE_EXE_LINKER_FLAGS="${PRELOAD}"
cmake --build build-web -j

echo
echo "Built build-web/web/okay-player.html"
echo "Serve it (browsers block file://): python3 -m http.server -d build-web/web"
echo "then open http://localhost:8000/okay-player.html"
