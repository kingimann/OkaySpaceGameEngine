#!/usr/bin/env bash
# Build the OkaySpace player for the web (WebAssembly) with Emscripten, and package a game for hosting.
#
# One-time setup: install the Emscripten SDK (https://emscripten.org) and run
# `source /path/to/emsdk/emsdk_env.sh` so `emcmake`/`emcc` are on PATH.
#
# Usage:   scripts/build-web.sh [--game DIR | scene.okayscene] [--out DIR] [--zip FILE] [--fit fill|aspect]
#                               [--no-compress] [--debug]
#   (no game)      builds the player only: build-web/web/okay-player.html/.js/.wasm (what CI checks)
#   --game DIR     an exported game folder (game.okayscene, game.okayconfig, assets; Build Settings > Web writes one)
#   scene file     a single scene: packaged as game.okayscene (the old one-argument form)
#   --out DIR      where the hostable game goes (default build-web/site): index.html, okay-player.js,
#                  okay-player.wasm.gz, game.okaypack.gz (scripts/web_package.py)
#   --zip FILE     also a zip with index.html at its root, for itch.io-style hosts
#   --fit          fill (default): the game fills the page; aspect: the game's width:height, letterboxed
#   --no-compress  plain .wasm and pack, for a host that compresses by itself
#   --debug        a debug build (no -Oz, no Closure) in build-web-debug/
# Serve the output over HTTP (browsers block file://): python3 -m http.server -d build-web/site, or the editor's
# Build Settings > Web > Run in browser.
set -euo pipefail
cd "$(dirname "$0")/.."

GAME="" OUT="" ZIP="" FIT="fill" COMPRESS=1 BUILD_TYPE=Release BUILD_DIR="${OKAY_WEB_BUILD_DIR:-build-web}"
while [ $# -gt 0 ]; do
    case "$1" in
        --game) GAME="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --zip) ZIP="$2"; shift 2 ;;
        --fit) FIT="$2"; shift 2 ;;
        --no-compress) COMPRESS=0; shift ;;
        --debug) BUILD_TYPE=Debug; BUILD_DIR="${OKAY_WEB_BUILD_DIR:-build-web-debug}"; shift ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) if [ -z "$GAME" ] && [ -f "$1" ]; then GAME="$1"; shift; else echo "error: unknown argument '$1' (see --help)" >&2; exit 2; fi ;;
    esac
done

if ! command -v emcmake >/dev/null 2>&1; then
    echo "error: Emscripten not found. Install emsdk and 'source emsdk_env.sh'." >&2
    exit 1
fi

# libsodium (packet encryption) has no WebAssembly
# build, and the relay / OkayUI's Direct3D backend aren't part of the web player
# (the engine's CMake already skips the last two under Emscripten). No Emscripten port is
# used (SDL2 left the web player on 2026-11-27: engine/src/Platform/WebBackend.cpp), so the
# build downloads nothing.
# With OKAY_WEB_CCACHE=1 and ccache installed, compiles go through ccache (CI uses this so an
# unchanged engine is not recompiled from scratch on every run).
LAUNCHER=()
if [ "${OKAY_WEB_CCACHE:-0}" = "1" ] && command -v ccache >/dev/null 2>&1; then
    LAUNCHER=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

emcmake cmake -S . -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    ${LAUNCHER[@]+"${LAUNCHER[@]}"} \
    -DOKAY_BUILD_PLAYER=ON -DOKAY_BUILD_TESTS=OFF \
    -DOKAY_BUILD_SANDBOX=OFF -DOKAY_BUILD_LAUNCHER=OFF -DOKAY_BUILD_EDITOR=OFF \
    -DOKAY_USE_SODIUM=OFF
cmake --build "$BUILD_DIR" --target okay-player -j "${OKAY_WEB_JOBS:-$(nproc 2>/dev/null || echo 2)}"

echo
echo "Built $BUILD_DIR/web/okay-player.html (.js, .wasm):"
ls -l "$BUILD_DIR/web/" | sed 1d

[ -z "$GAME" ] && exit 0

# A single scene becomes a one-file game folder.
GAME_DIR="$GAME"
if [ -f "$GAME" ]; then
    GAME_DIR="$(mktemp -d)"
    trap 'rm -rf "$GAME_DIR"' EXIT
    cp "$GAME" "$GAME_DIR/game.okayscene"
fi
OUT="${OUT:-$BUILD_DIR/site}"
ARGS=(--build "$BUILD_DIR/web" --game "$GAME_DIR" --out "$OUT" --fit "$FIT")
[ -n "$ZIP" ] && ARGS+=(--zip "$ZIP")
[ "$COMPRESS" = "0" ] && ARGS+=(--no-compress)
echo
echo "Packaged for the web in $OUT:"
python3 -I scripts/web_package.py "${ARGS[@]}"
echo "Serve it (browsers block file://): python3 -m http.server -d \"$OUT\", then open http://localhost:8000/"
