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
# Reuse compiled objects between release builds when ccache is installed (the
# release workflow caches its directory): only changed files recompile.
if command -v ccache >/dev/null 2>&1; then
    EXTRA+=("-DCMAKE_C_COMPILER_LAUNCHER=ccache" "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache")
fi
[ -n "${OKAY_IMGUI_SRC:-}" ] && EXTRA+=("-DFETCHCONTENT_SOURCE_DIR_IMGUI=$OKAY_IMGUI_SRC")
# Bake a default account server into the binaries when provided (e.g. from a CI
# secret), so the release build is online with no key file on disk.
# The account server comes from config/public-account.json (CMake reads it itself); OKAY_DEFAULT_ACCOUNT_URL / _KEY in the
# environment (the optional GitHub repository variables) override it. Never print a value.
. "$(dirname "$0")/public-account.sh"
_ACCT_URL="${OKAY_DEFAULT_ACCOUNT_URL:-$(okay_public_account url)}"
_ACCT_KEY="${OKAY_DEFAULT_ACCOUNT_KEY:-$(okay_public_account key)}"
# Keep the key out of the logs (GitHub Actions only; a publishable key is public by design, but keep it out of sight).
[ -n "${GITHUB_ACTIONS:-}" ] && [ -n "$_ACCT_KEY" ] && echo "::add-mask::$_ACCT_KEY"
# CI release/test builds set OKAY_REQUIRE_ACCOUNT=1: stop if neither the override nor the file gives both values, so nothing ships offline.
if [ "${OKAY_REQUIRE_ACCOUNT:-}" = "1" ] && { [ -z "$_ACCT_URL" ] || [ -z "$_ACCT_KEY" ]; }; then
    echo "::error title=No account server for this build::config/public-account.json has no url/key and OKAY_DEFAULT_ACCOUNT_URL / _KEY are not set. See docs/accounts.md." >&2
    exit 1
fi
[ -n "${OKAY_DEFAULT_ACCOUNT_URL:-}" ] && EXTRA+=("-DOKAY_DEFAULT_ACCOUNT_URL=$OKAY_DEFAULT_ACCOUNT_URL")
[ -n "${OKAY_DEFAULT_ACCOUNT_KEY:-}" ] && EXTRA+=("-DOKAY_DEFAULT_ACCOUNT_KEY=$OKAY_DEFAULT_ACCOUNT_KEY")
# The launcher's trusted anti-cheat keys (public ed25519 keys, hex, comma-separated;
# docs/anticheat.md). It applies them to the official game only; the player gets none.
[ -n "${OKAY_AC_TRUSTED_KEYS:-}" ] && EXTRA+=("-DOKAY_AC_TRUSTED_KEYS=$OKAY_AC_TRUSTED_KEYS")
# The public keys that must have signed versions.json (docs/updater.md "Signed releases"). Empty = unsigned feeds still accepted.
[ -n "${OKAY_UPDATE_TRUSTED_KEYS:-}" ] && EXTRA+=("-DOKAY_UPDATE_TRUSTED_KEYS=$OKAY_UPDATE_TRUSTED_KEYS")
# Authenticated packet encryption (libsodium). Point OKAY_SODIUM_MINGW_PREFIX at the
# extracted libsodium-win64 prebuilt (contains include/sodium.h and lib/libsodium.a).
# It's statically linked, so no extra DLL ships. REQUIRED: a release never builds without libsodium (it would fall back to
# our own, unreviewed okay::crypto; CLAUDE.md rule 18), so a missing or wrong prefix stops here, and CMake gets
# -DOKAY_REQUIRE_SODIUM=ON as the second lock. scripts/fetch-libsodium-mingw.sh makes the pinned prefix.
: "${OKAY_SODIUM_MINGW_PREFIX:?Set OKAY_SODIUM_MINGW_PREFIX to the libsodium-win64 prefix (scripts/fetch-libsodium-mingw.sh <dir> prints it); a release must not build without libsodium}"
for f in lib/libsodium.a include/sodium.h; do
    [ -s "$OKAY_SODIUM_MINGW_PREFIX/$f" ] || { echo "error: $OKAY_SODIUM_MINGW_PREFIX/$f is missing: not a libsodium-win64 prefix; refusing to build a release without libsodium" >&2; exit 1; }
done
EXTRA+=("-DOKAY_SODIUM_PREFIX=$OKAY_SODIUM_MINGW_PREFIX" "-DOKAY_REQUIRE_SODIUM=ON")
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

# A release never carries our own unreviewed crypto or the own-stack dogfood switch (CLAUDE.md rule 18, docs/own_stack_rollout.md).
scripts/check_release_own_stack.sh -- "${EXTRA[@]}"

cmake -S . -B "$BUILD_DIR" \
      -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DOKAY_BUILD_TESTS=OFF -DOKAY_BUILD_SANDBOX=OFF \
      -DOKAY_BUILD_LAUNCHER=ON -DOKAY_BUILD_PLAYER=ON -DOKAY_BUILD_EDITOR=ON \
      -DOKAY_USE_ASSIMP=ON \
      -DCMAKE_PREFIX_PATH="$SDL2_MINGW_PREFIX" \
      -DCMAKE_FIND_ROOT_PATH="/usr/x86_64-w64-mingw32;$SDL2_MINGW_PREFIX" \
      "${EXTRA[@]}"
# The configured tree too (a stale cache keeps old -D values). This also checks the BUILT result: the cache must say libsodium
# is in the build (OKAY_SODIUM_IN_BUILD=ON) and the compiler flags must carry OKAY_HAVE_SODIUM and not OKAY_OWN_CRYPTO. The
# programs are Windows .exe files and cannot run on this Linux host, so the program's own startup line (--binary) is not
# asked here; the Linux server build in release.yml is checked that way.
scripts/check_release_own_stack.sh --cache "$BUILD_DIR"
# One job per core: a bare -j lets make start every editor file at once, and the
# big ones (editor and player main.cpp) together run a CI runner out of memory.
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
cmake --build "$BUILD_DIR" -j "$JOBS"

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

# Example scenes and sample files to try in the editor (docs/try_it.md). A curated list: only folders whose
# scenes are self-contained (every file a scene names sits beside it, and scenes find their files relative to
# the scene file, so the folder works from any install path). About 1 MB. visual_script/playground is the folder
# File > Examples > Visual Script Node Playground opens its scenes from (tests/test_examples_work.cpp checks that every
# folder a File > Examples entry needs is listed here). Left out on purpose: account-server (a Python server), netdemo
# and physicsdemo (C++ sources), editor-plugins, and the loose graphs in visual_script itself (not scenes).
EXAMPLE_DIRS="vehicle own_wav own_readers obj_texture_paths spatial_audio cutscene behavior_trees node_playground physics physics2d weapons touch_controls visual_script/playground ui_text voxel modeling modeling_tools fp_hand player perf"
for d in $EXAMPLE_DIRS; do
    [ -d "examples/$d" ] || { echo "error: examples/$d is missing (edit EXAMPLE_DIRS in $0)" >&2; exit 1; }
    mkdir -p "$OUT_DIR/examples/$d"
    for f in examples/$d/*; do
        case "$f" in *.py|*.cpp|*.hpp|*.txt|*.pyc|*/__pycache__) continue ;; esac
        [ -f "$f" ] && cp "$f" "$OUT_DIR/examples/$d/"
    done
done
# The texture the obj_texture_paths cubes name (the same starter file that ships in Assets/Textures).
cp assets/textures/grid.png "$OUT_DIR/examples/obj_texture_paths/"
[ -s "$OUT_DIR/examples/vehicle/vehicle_test.okayscene" ] || { echo "error: examples/vehicle/vehicle_test.okayscene did not make it into the distribution" >&2; exit 1; }
[ -s "$OUT_DIR/examples/ui_text/ui_text.okayscene" ] || { echo "error: examples/ui_text/ui_text.okayscene did not make it into the distribution" >&2; exit 1; }

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

examples/ example scenes to try: in the editor use File > Open and pick
                        examples\vehicle\vehicle_test.okayscene, then press Play
                        (get in the car with E), or
                        examples\ui_text\ui_text.okayscene (a nameplate over each of
                        two characters and a sign with rich text; keys 1 2 3 H).
                        More in docs/try_it.md.

Each .exe needs SDL2.dll in its own folder — that's why one sits beside the
launcher and a copy sits in Tools/. Don't separate an .exe from its SDL2.dll.
EOF

echo
echo "Built Windows distribution in $OUT_DIR/:"
find "$OUT_DIR" -type f | sort

# ---- Debug info: ship it separately -------------------------------------------
# The editor is built with -g so crash offsets resolve to source lines, but the
# DWARF is ~60 MB of an ~84 MB exe. Move it to Tools/../symbols/OkayEngine.debug
# (published as its own release asset, never downloaded by the launcher) and keep
# a debuglink in the exe; addr2line -e OkayEngine.debug resolves crash offsets.
SYMBOLS_DIR="${SYMBOLS_DIR:-build-win-symbols}"
mkdir -p "$SYMBOLS_DIR"
OBJCOPY="${OBJCOPY:-x86_64-w64-mingw32-objcopy}"
if command -v "$OBJCOPY" >/dev/null 2>&1 && [ -f "$OUT_DIR/Tools/OkayEngine.exe" ]; then
    "$OBJCOPY" --only-keep-debug "$OUT_DIR/Tools/OkayEngine.exe" "$SYMBOLS_DIR/OkayEngine.debug"
    ( cd "$SYMBOLS_DIR" && "$OBJCOPY" --strip-debug --add-gnu-debuglink=OkayEngine.debug "../$OUT_DIR/Tools/OkayEngine.exe" )
    echo "Split editor debug info -> $SYMBOLS_DIR/OkayEngine.debug"
    ls -la "$OUT_DIR/Tools/OkayEngine.exe" "$SYMBOLS_DIR/OkayEngine.debug"
fi
