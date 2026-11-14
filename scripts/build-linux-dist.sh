#!/usr/bin/env bash
# Build the Linux distribution (launcher, editor, player and, when it builds, the multiplayer relay) natively and lay it
# out like the Windows zip: the launcher at the top, everything else in Tools/. The release workflow
# (.github/workflows/release.yml, job linux-client) runs this on Ubuntu 22.04 so the programs run on glibc 2.35 and newer;
# you can run it on any x86-64 Linux. See docs/packaging.md "Linux".
#
#   build-linux-dist/
#     OkaySpace                the launcher: start here
#     README.txt  VERSION.txt
#     Tools/
#       OkayEngine             the editor
#       OkaySpacePlayer        standalone runtime that plays an exported game (also the dedicated server: --server)
#       okayspace-relay        optional multiplayer relay (if it built)
#       VERSION.txt  accounts.md
#   OkaySpace-linux-x64-v<ver>.tar.gz          that folder as a tarball (one top folder, OkaySpace-linux-x64-v<ver>/)
#   OkaySpace-linux-x64-v<ver>.tar.gz.sha256   its SHA-256 in `sha256sum` format
#   build-linux-symbols/OkayEngine-linux-x64.debug   the editor's debug info (a separate release asset, never downloaded by the launcher)
#
# The programs link SDL2 and libsodium dynamically (no bundled .so files): the machine needs libsdl2-2.0-0 and libsodium23
# (Debian / Ubuntu names) and curl. The script lists each program's shared-library needs at the end.
#
# Requirements (Debian / Ubuntu): sudo apt-get install -y cmake ninja-build g++ pkg-config libsdl2-dev libsodium-dev
#   (the editor also needs the X11/GL development packages libsdl2-dev already pulls in)
#
# Env (same meaning as scripts/build-windows-dist.sh):
#   OKAY_IMGUI_SRC, OKAY_DEFAULT_ACCOUNT_URL, OKAY_DEFAULT_ACCOUNT_KEY, OKAY_AC_TRUSTED_KEYS, OKAY_UPDATE_TRUSTED_KEYS,
#   OKAY_STEAM_APP_ID, JOBS, BUILD_DIR (default build-linux), OUT_DIR (default build-linux-dist), SKIP_BUILD=1 (only lay out
#   and package what BUILD_DIR/bin already holds: used by the test).
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR="${BUILD_DIR:-build-linux}"
OUT_DIR="${OUT_DIR:-build-linux-dist}"
SYMBOLS_DIR="${SYMBOLS_DIR:-build-linux-symbols}"

if [ "$(uname -s)" != Linux ]; then echo "error: this script builds the Linux distribution and must run on Linux" >&2; exit 1; fi
case "$(uname -m)" in x86_64|amd64) ;; *) echo "error: the Linux release is x86-64 only (this machine is $(uname -m))" >&2; exit 1 ;; esac

if [ "${SKIP_BUILD:-0}" != 1 ]; then
    for t in cmake ninja g++; do
        command -v "$t" >/dev/null 2>&1 || { echo "error: $t not found. Install: sudo apt-get install -y cmake ninja-build g++ pkg-config libsdl2-dev libsodium-dev" >&2; exit 1; }
    done
    # Without libsodium the build is silently unencrypted (the engine only uses it when found): a release must not be.
    if ! pkg-config --exists libsodium 2>/dev/null; then
        echo "error: libsodium not found (sudo apt-get install -y libsodium-dev). A release build must have packet encryption." >&2
        exit 1
    fi

    EXTRA=()
    if command -v ccache >/dev/null 2>&1; then
        EXTRA+=("-DCMAKE_C_COMPILER_LAUNCHER=ccache" "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache")
    fi
    [ -n "${OKAY_IMGUI_SRC:-}" ] && EXTRA+=("-DFETCHCONTENT_SOURCE_DIR_IMGUI=$OKAY_IMGUI_SRC")
    [ -n "${OKAY_DEFAULT_ACCOUNT_URL:-}" ] && EXTRA+=("-DOKAY_DEFAULT_ACCOUNT_URL=$OKAY_DEFAULT_ACCOUNT_URL")
    [ -n "${OKAY_DEFAULT_ACCOUNT_KEY:-}" ] && EXTRA+=("-DOKAY_DEFAULT_ACCOUNT_KEY=$OKAY_DEFAULT_ACCOUNT_KEY")
    [ -n "${OKAY_AC_TRUSTED_KEYS:-}" ] && EXTRA+=("-DOKAY_AC_TRUSTED_KEYS=$OKAY_AC_TRUSTED_KEYS")
    [ -n "${OKAY_UPDATE_TRUSTED_KEYS:-}" ] && EXTRA+=("-DOKAY_UPDATE_TRUSTED_KEYS=$OKAY_UPDATE_TRUSTED_KEYS")
    EXTRA+=("-DOKAY_STEAM_APP_ID=${OKAY_STEAM_APP_ID:-1172560}")

    cmake -S . -B "$BUILD_DIR" -G Ninja \
          -DCMAKE_BUILD_TYPE=Release \
          -DOKAY_BUILD_TESTS=OFF -DOKAY_BUILD_SANDBOX=OFF -DOKAY_BUILD_TOOLS=OFF \
          -DOKAY_BUILD_LAUNCHER=ON -DOKAY_BUILD_PLAYER=ON -DOKAY_BUILD_EDITOR=ON \
          -DOKAY_USE_SODIUM=ON -DOKAY_USE_ASSIMP=ON \
          "${EXTRA[@]}"
    JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
    # (-j: a bare ninja starts one job per core plus some; the big editor and player files together can run a small runner out of memory)
    cmake --build "$BUILD_DIR" -j "$JOBS"
fi

BIN="$BUILD_DIR/bin"
for f in OkaySpace OkayEngine OkaySpacePlayer; do
    [ -f "$BIN/$f" ] || { echo "error: $BIN/$f was not built" >&2; exit 1; }
    [ "$(head -c4 "$BIN/$f" | od -An -c | tr -d ' ')" = '177ELF' ] || { echo "error: $BIN/$f is not a Linux (ELF) program" >&2; exit 1; }
done

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR/Tools"
cp "$BIN/OkaySpace" "$OUT_DIR/"
cp "$BIN/OkayEngine" "$BIN/OkaySpacePlayer" "$OUT_DIR/Tools/"
[ -f "$BIN/okayspace-relay" ] && cp "$BIN/okayspace-relay" "$OUT_DIR/Tools/"

# Debug info: split the editor's off before stripping (crash offsets resolve with addr2line -e <file>.debug).
mkdir -p "$SYMBOLS_DIR"
if command -v objcopy >/dev/null 2>&1; then
    objcopy --only-keep-debug "$OUT_DIR/Tools/OkayEngine" "$SYMBOLS_DIR/OkayEngine-linux-x64.debug"
fi
for f in "$OUT_DIR/OkaySpace" "$OUT_DIR"/Tools/*; do
    [ -f "$f" ] && [ -x "$f" ] && strip --strip-unneeded "$f" 2>/dev/null || true
done
if [ -f "$SYMBOLS_DIR/OkayEngine-linux-x64.debug" ]; then
    objcopy --add-gnu-debuglink="$SYMBOLS_DIR/OkayEngine-linux-x64.debug" "$OUT_DIR/Tools/OkayEngine"   # (records the file's name and checksum)
fi
chmod 755 "$OUT_DIR/OkaySpace" "$OUT_DIR"/Tools/*

OKAY_VER="$(grep -m1 -oE 'VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')"
: "${OKAY_VER:=0.0.0}"
printf '%s\n' "$OKAY_VER" > "$OUT_DIR/VERSION.txt"
printf '%s\n' "$OKAY_VER" > "$OUT_DIR/Tools/VERSION.txt"
[ -f docs/accounts.md ] && cp docs/accounts.md "$OUT_DIR/Tools/"
if [ -d assets/textures ]; then
    mkdir -p "$OUT_DIR/Assets/Textures"
    cp assets/textures/*.png "$OUT_DIR/Assets/Textures/" 2>/dev/null || true
fi
if [ -d assets/fonts ]; then
    mkdir -p "$OUT_DIR/Assets/Fonts"
    cp assets/fonts/* "$OUT_DIR/Assets/Fonts/" 2>/dev/null || true
fi

cat > "$OUT_DIR/README.txt" <<'README'
OkaySpace for Linux (x86-64)
============================

Run  ./OkaySpace  to start (the launcher: sign in, create/play games).

Everything else lives in  Tools/:
  OkayEngine        the editor (make games)
  OkaySpacePlayer   the standalone runtime that plays an exported game; with --server it is the dedicated server
  okayspace-relay   optional multiplayer relay for NAT traversal (see docs/relay.md)

Needs (Debian / Ubuntu package names): libsdl2-2.0-0, libsodium23, curl.
Optional: zenity or kdialog for native file dialogs (without them the built-in file browser is used).
Built on Ubuntu 22.04 (glibc 2.35): it runs on that or anything newer.

Keep the launcher and the Tools folder together: the launcher updates itself and the programs in Tools/ in place.
If the files lost their execute bit (some archive tools do that):  chmod +x OkaySpace Tools/*
README

NAME="OkaySpace-linux-x64-v${OKAY_VER}"
TARBALL="$NAME.tar.gz"
rm -rf "$NAME" "$TARBALL" "$TARBALL.sha256"
mkdir -p "$NAME"
cp -a "$OUT_DIR"/. "$NAME"/
tar --owner=0 --group=0 --numeric-owner --sort=name -czf "$TARBALL" "$NAME"
rm -rf "$NAME"
sha256sum "$TARBALL" > "$TARBALL.sha256"

echo
echo "Built the Linux distribution in $OUT_DIR/:"
find "$OUT_DIR" -type f | sort
echo
echo "Packaged: $TARBALL ($(( $(stat -c %s "$TARBALL") / 1024 )) KB)"
cat "$TARBALL.sha256"
echo
echo "Shared libraries each program needs (outside the C/C++ runtime):"
for f in "$OUT_DIR/OkaySpace" "$OUT_DIR"/Tools/*; do
    [ -x "$f" ] && [ -f "$f" ] || continue
    printf '  %s: ' "${f#"$OUT_DIR"/}"
    ldd "$f" 2>/dev/null | awk '{print $1}' | grep -vE '^(linux-vdso|/lib|libc\.|libm\.|libdl\.|libpthread\.|librt\.|libstdc\+\+|libgcc_s|ld-linux)' | grep -E '\.so' | tr '\n' ' ' || true
    echo
done
