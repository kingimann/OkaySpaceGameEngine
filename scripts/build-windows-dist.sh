#!/usr/bin/env bash
# Cross-compile the full Windows distribution — launcher, editor, player, and the
# multiplayer relay — from Linux with MinGW-w64, bundling SDL2.dll. Output goes to
# build-win-dist/ laid out as: OkaySpace.exe + SDL2.dll at the top (double-click
# the launcher), with the rest in a Tools/ subfolder. See the "Distribution
# layout" section below and docs/packaging.md.
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
# Bake a default account server into the binaries when provided (e.g. from a CI
# secret), so the release build is online with no key file on disk.
[ -n "${OKAY_DEFAULT_ACCOUNT_URL:-}" ] && EXTRA+=("-DOKAY_DEFAULT_ACCOUNT_URL=$OKAY_DEFAULT_ACCOUNT_URL")
[ -n "${OKAY_DEFAULT_ACCOUNT_KEY:-}" ] && EXTRA+=("-DOKAY_DEFAULT_ACCOUNT_KEY=$OKAY_DEFAULT_ACCOUNT_KEY")
# Authenticated packet encryption (libsodium). Point OKAY_SODIUM_MINGW_PREFIX at the
# extracted libsodium-win64 prebuilt (contains include/sodium.h and lib/libsodium.a).
# It's statically linked, so no extra DLL ships. If unset, the build is unencrypted.
[ -n "${OKAY_SODIUM_MINGW_PREFIX:-}" ] && EXTRA+=("-DOKAY_SODIUM_PREFIX=$OKAY_SODIUM_MINGW_PREFIX")
# Bake the Steam App ID into the release build (defaults to OkaySpace's app, 1172560;
# override or set to 480 for a generic/dev build).
OKAY_STEAM_APP_ID="${OKAY_STEAM_APP_ID:-1172560}"
EXTRA+=("-DOKAY_STEAM_APP_ID=$OKAY_STEAM_APP_ID")
# Real Steamworks backend (achievements/cloud/etc.): set STEAMWORKS_SDK_PATH to the
# extracted SDK. Without it the simulation backend is used (still a valid release —
# it just doesn't talk to the live Steam client).
if [ -n "${STEAMWORKS_SDK_PATH:-}" ]; then
    EXTRA+=("-DOKAY_WITH_STEAM=ON" "-DSTEAMWORKS_SDK_PATH=$STEAMWORKS_SDK_PATH")
fi

cmake -S . -B "$BUILD_DIR" \
      -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DOKAY_BUILD_TESTS=OFF -DOKAY_BUILD_SANDBOX=OFF \
      -DOKAY_BUILD_LAUNCHER=ON -DOKAY_BUILD_PLAYER=ON -DOKAY_BUILD_EDITOR=ON \
      -DCMAKE_PREFIX_PATH="$SDL2_MINGW_PREFIX" \
      -DCMAKE_FIND_ROOT_PATH="/usr/x86_64-w64-mingw32;$SDL2_MINGW_PREFIX" \
      "${EXTRA[@]}"
cmake --build "$BUILD_DIR" -j

# ---- Distribution layout ------------------------------------------------------
# Keep the thing you double-click at the top and tuck the rest away:
#
#   OkaySpace.exe        <- the launcher (start here)
#   SDL2.dll             <- required next to OkaySpace.exe (load-time linked)
#   README.txt
#   Tools/
#     OkayEngine.exe         (the editor)
#     OkaySpacePlayer.exe    (standalone game runtime)
#     okayspace-relay.exe    (multiplayer NAT relay, if built)
#     SDL2.dll               (copy — each exe needs the DLL beside it on Windows)
#     VERSION.txt / accounts.md
#
# Note: Windows resolves load-time DLLs from the exe's own directory, so SDL2.dll
# is intentionally duplicated next to the Tools/ exes — that's required, not waste.
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR/Tools"

# Top level: the launcher + its SDL2.dll.
cp "$BUILD_DIR/bin/OkaySpace.exe" "$SDL2_MINGW_PREFIX/bin/SDL2.dll" "$OUT_DIR/"

# Tools/: the editor, player, relay (if present), and their own SDL2.dll copy.
cp "$BUILD_DIR/bin/OkayEngine.exe" \
   "$BUILD_DIR/bin/OkaySpacePlayer.exe" \
   "$SDL2_MINGW_PREFIX/bin/SDL2.dll" \
   "$OUT_DIR/Tools/"
[ -f "$BUILD_DIR/bin/okayspace-relay.exe" ] && cp "$BUILD_DIR/bin/okayspace-relay.exe" "$OUT_DIR/Tools/"
# OkayUI's standalone Direct3D 11 demo (no SDL): a raw DX11 app rendering OkayUI.
[ -f "$BUILD_DIR/bin/okayui_d3d11_demo.exe" ] && cp "$BUILD_DIR/bin/okayui_d3d11_demo.exe" "$OUT_DIR/Tools/"
# VERSION.txt is the single source of truth for the self-updater: the launcher
# compares its baked version against releases/latest/download/VERSION.txt and only
# updates when the published one is strictly newer. Derive it from CMakeLists so it
# is always in sync with the binaries (never a stale dist/VERSION.txt), and write it
# at BOTH the dist root (where the launcher lives + the release workflow reads it)
# and in Tools/ (beside the editor, whose in-app updater reads its own folder).
OKAY_VER="$(grep -m1 -oE 'VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
: "${OKAY_VER:=0.0.0}"
printf '%s\n' "$OKAY_VER" > "$OUT_DIR/VERSION.txt"
printf '%s\n' "$OKAY_VER" > "$OUT_DIR/Tools/VERSION.txt"
[ -f docs/accounts.md ] && cp docs/accounts.md "$OUT_DIR/Tools/"

# Starter texture pack (grass/dirt/stone/grid). Drop these into a project's Assets/
# folder, or point a MeshRenderer's Texture at them directly.
if [ -d assets/textures ]; then
    mkdir -p "$OUT_DIR/Assets/Textures"
    cp assets/textures/*.png "$OUT_DIR/Assets/Textures/" 2>/dev/null || true
fi
# Starter font (DejaVu Sans, permissive license). Set a Text/Button Font to this path.
if [ -d assets/fonts ]; then
    mkdir -p "$OUT_DIR/Assets/Fonts"
    cp assets/fonts/* "$OUT_DIR/Assets/Fonts/" 2>/dev/null || true
fi

# When built against the real Steamworks SDK, ship its redistributable DLL next to
# every exe that initializes Steam (the launcher at top, the tools in Tools/).
if [ -n "${STEAMWORKS_SDK_PATH:-}" ] && [ -f "$STEAMWORKS_SDK_PATH/redistributable_bin/win64/steam_api64.dll" ]; then
    cp "$STEAMWORKS_SDK_PATH/redistributable_bin/win64/steam_api64.dll" "$OUT_DIR/"
    cp "$STEAMWORKS_SDK_PATH/redistributable_bin/win64/steam_api64.dll" "$OUT_DIR/Tools/"
    echo "Bundled steam_api64.dll (Steamworks backend)."
fi

cat > "$OUT_DIR/README.txt" <<'EOF'
OkaySpace
=========

Double-click  OkaySpace.exe  to start (the launcher: sign in, create/play games).
Keep SDL2.dll next to it.

Everything else lives in  Tools/:
  OkayEngine.exe        the editor (make games)
  OkaySpacePlayer.exe   the standalone runtime that plays an exported game
  okayspace-relay.exe   optional multiplayer relay for NAT traversal
                        (run on a reachable host; see docs/relay.md)

Each .exe needs SDL2.dll in its own folder — that's why one sits beside the
launcher and a copy sits in Tools/. Don't separate an .exe from its SDL2.dll.
EOF

echo
echo "Built Windows distribution in $OUT_DIR/:"
find "$OUT_DIR" -type f | sort
