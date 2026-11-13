#!/usr/bin/env bash
# One command that builds the whole release on THIS machine and publishes it,
# without waiting for GitHub Actions runners (which stall on apt mirrors, lose
# jobs, and queue for hours).
#
#   scripts/release-local.sh                 build + GitHub release + Supabase
#   scripts/release-local.sh --build-only    build and package, publish nothing
#   scripts/release-local.sh --dry-run       build, then show what would be published
#   scripts/release-local.sh --no-server     skip the Linux game server build
#   scripts/release-local.sh --no-installer  skip the NSIS installer
#   scripts/release-local.sh --lts           publish as an LTS patch (never "latest")
#
# It runs the same build scripts CI uses (scripts/build-windows-dist.sh,
# scripts/publish-supabase.sh), so a local release matches a CI one.
#
# Needs: git, cmake, ninja, x86_64-w64-mingw32-g++ (g++-mingw-w64-x86-64), zip,
#        python3, curl, tar, and gh (GitHub CLI) logged in or GITHUB_TOKEN set
#        (contents:write). Optional: makensis (installer), ccache, g++ + libsdl2-dev
#        + libsodium-dev (Linux server).
# Supabase: SUPABASE_SERVICE_KEY or SUPABASE_ACCESS_TOKEN in the environment
#        (never in a file). Without it the Supabase step is skipped and said so.
#
# Safety: refuses to publish unless the tree is clean and HEAD is already on
# origin (the release tags that exact commit), and refuses to republish a
# version whose GitHub release already exists unless --force is given.
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_ONLY=0 DRY=0 SERVER=1 INSTALLER=1 CHANNEL=latest FORCE=0
for a in "$@"; do
  case "$a" in
    --build-only) BUILD_ONLY=1 ;;
    --dry-run) DRY=1 ;;
    --no-server) SERVER=0 ;;
    --no-installer) INSTALLER=0 ;;
    --lts) CHANNEL=lts ;;
    --force) FORCE=1 ;;
    -h|--help) sed -n 2,28p "$0"; exit 0 ;;
    *) echo "unknown option: $a" >&2; exit 2 ;;
  esac
done

say() { printf '\n==> %s\n' "$*"; }
die() { echo "error: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "missing tool: $1 ($2)"; }

say "Checking tools"
need git "install git"; need cmake "install cmake"; need ninja "install ninja-build"
need x86_64-w64-mingw32-g++ "sudo apt-get install g++-mingw-w64-x86-64 mingw-w64-x86-64-dev"
need zip "sudo apt-get install zip"; need python3 "install python3"; need curl "install curl"
PUBLISH=1; { [ $BUILD_ONLY = 1 ] || [ $DRY = 1 ]; } && PUBLISH=0
if [ $BUILD_ONLY = 0 ]; then need gh "install the GitHub CLI and run: gh auth login"; fi
HAVE_NSIS=0; if [ $INSTALLER = 1 ] && command -v makensis >/dev/null 2>&1; then HAVE_NSIS=1; fi
[ $INSTALLER = 1 ] && [ $HAVE_NSIS = 0 ] && echo "note: makensis not found, skipping the installer (sudo apt-get install nsis)"

SHA=$(git rev-parse HEAD)
if [ $PUBLISH = 1 ]; then
  [ -z "$(git status --porcelain --untracked-files=no)" ] || die "uncommitted changes: commit or stash first (the release tags HEAD)"
  git fetch -q origin
  [ -n "$(git branch -r --contains "$SHA" 2>/dev/null)" ] || die "HEAD $SHA is not on origin: push it first"
fi

# ---- dependencies (cached under .release-cache, downloaded once) -------------
CACHE="$PWD/.release-cache"; mkdir -p "$CACHE"
SDL2_VERSION="${SDL2_VERSION:-2.30.9}"
if [ ! -f "$CACHE/sdl2/x86_64-w64-mingw32/bin/SDL2.dll" ]; then
  say "Fetching SDL2 $SDL2_VERSION (MinGW)"
  ( cd "$CACHE" && bash "$OLDPWD/scripts/fetch-tarball.sh" sdl2-mingw.tar.gz \
      "https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-mingw.tar.gz" \
      "https://www.libsdl.org/release/SDL2-devel-${SDL2_VERSION}-mingw.tar.gz" )
  mkdir -p "$CACHE/sdl2"; tar xzf "$CACHE/sdl2-mingw.tar.gz" -C "$CACHE/sdl2" --strip-components=1
fi
export SDL2_MINGW_PREFIX="$CACHE/sdl2/x86_64-w64-mingw32"
if [ ! -d "$CACHE/libsodium/libsodium-win64" ]; then
  say "Fetching libsodium (MinGW prebuilt)"
  ( cd "$CACHE" && bash "$OLDPWD/scripts/fetch-tarball.sh" libsodium-mingw.tar.gz \
      "https://download.libsodium.org/libsodium/releases/libsodium-1.0.20-stable-mingw.tar.gz" ) || echo "warning: libsodium download failed; the build will be unencrypted"
  mkdir -p "$CACHE/libsodium"; tar xzf "$CACHE/libsodium-mingw.tar.gz" -C "$CACHE/libsodium" 2>/dev/null || true
fi
[ -d "$CACHE/libsodium/libsodium-win64" ] && export OKAY_SODIUM_MINGW_PREFIX="$CACHE/libsodium/libsodium-win64"
[ -n "${OKAY_AC_TRUSTED_KEYS:-}" ] || echo "warning: OKAY_AC_TRUSTED_KEYS is empty: the launcher's anti-cheat checks are telemetry only in this build"

# ---- build -------------------------------------------------------------------
say "Building the Windows distribution (this is the long step; ccache makes reruns fast)"
scripts/build-windows-dist.sh
VER=$(cat build-win-dist/VERSION.txt)
[ -n "$VER" ] || die "no version in build-win-dist/VERSION.txt"
say "Built version $VER"
for f in OkaySpace.exe Tools/OkayEngine.exe Tools/OkaySpacePlayer.exe SDL2.dll VERSION.txt; do
  [ -s "build-win-dist/$f" ] || die "build output missing: $f"
done
for f in OkaySpace.exe Tools/OkayEngine.exe Tools/OkaySpacePlayer.exe; do
  [ "$(head -c2 "build-win-dist/$f")" = "MZ" ] || die "$f is not a Windows executable"
done
[ -f build-win-dist/CHANGELOG.txt ] || cp dist/CHANGELOG.txt build-win-dist/CHANGELOG.txt

OUT="$PWD/release-out"; rm -rf "$OUT"; mkdir -p "$OUT"
NAME="OkaySpace-windows-v${VER}"
rm -rf "$OUT/$NAME"; mkdir -p "$OUT/$NAME"; cp -r build-win-dist/* "$OUT/$NAME/"
( cd "$OUT" && zip -qr "$NAME.zip" "$NAME" && rm -rf "$NAME" )
ASSETS=("$OUT/$NAME.zip" build-win-dist/OkaySpace.exe build-win-dist/Tools/OkayEngine.exe \
        build-win-dist/Tools/OkaySpacePlayer.exe build-win-dist/SDL2.dll build-win-dist/VERSION.txt dist/CHANGELOG.txt)
[ -f build-win-symbols/OkayEngine.debug ] && ASSETS+=(build-win-symbols/OkayEngine.debug)

if [ $HAVE_NSIS = 1 ]; then
  say "Building the installer"
  makensis -DVERSION="$VER" -DDISTDIR="$PWD/build-win-dist" -DOUTFILE="$OUT/OkaySpace-Setup.exe" scripts/okayspace-installer.nsi >/dev/null
  ASSETS+=("$OUT/OkaySpace-Setup.exe")
fi

SERVER_BIN=""
if [ $SERVER = 1 ]; then
  if command -v g++ >/dev/null 2>&1 && pkg-config --exists sdl2 libsodium 2>/dev/null; then
    say "Building the Linux game server"
    cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DOKAY_BUILD_PLAYER=ON \
          -DOKAY_BUILD_EDITOR=OFF -DOKAY_BUILD_LAUNCHER=OFF -DOKAY_BUILD_TESTS=OFF \
          -DOKAY_BUILD_SANDBOX=OFF -DOKAY_BUILD_TOOLS=OFF >/dev/null
    cmake --build build-linux --target okay-player
    cp build-linux/bin/okay-player "$OUT/okay-server-linux-x64"; strip "$OUT/okay-server-linux-x64"
    "$OUT/okay-server-linux-x64" --help | head -n 1 >/dev/null
    SERVER_BIN="$OUT/okay-server-linux-x64"; ASSETS+=("$SERVER_BIN")
  else
    echo "note: skipping the Linux server (needs g++, libsdl2-dev, libsodium-dev, pkg-config)"
  fi
fi

say "Built and packaged:"; for f in "${ASSETS[@]}"; do ls -l "$f" | awk '{printf "  %10d  %s\n",$5,$9}'; done
if [ $PUBLISH = 0 ]; then
  [ $DRY = 1 ] && echo "dry run: would publish v$VER ($CHANNEL) at $SHA to GitHub and Supabase"
  echo "Files are in $OUT and build-win-dist"; exit 0
fi

# ---- GitHub release ----------------------------------------------------------
TAG="v$VER"
# Created as NOT latest: launchers read releases/latest, and refuse a version whose checksums (Supabase versions.json)
# are not published yet ("no checksum" error). It is promoted below, after the checksums are in.
LATEST=--latest=false
if gh release view "$TAG" >/dev/null 2>&1; then
  [ $FORCE = 1 ] || die "release $TAG already exists (use --force to replace its files)"
  say "Updating release $TAG"
  gh release upload "$TAG" "${ASSETS[@]}" --clobber
else
  say "Creating release $TAG at $SHA"
  gh release create "$TAG" "${ASSETS[@]}" --target "$SHA" --title "OkaySpace v$VER" --generate-notes $LATEST
fi

# ---- Supabase ----------------------------------------------------------------
if [ -n "${SUPABASE_SERVICE_KEY:-}${SUPABASE_ACCESS_TOKEN:-}" ]; then
  say "Publishing to Supabase ($CHANNEL)"
  CHANNEL=$CHANNEL DIST_DIR="$PWD/build-win-dist" scripts/publish-supabase.sh
  if [ $CHANNEL != lts ]; then
    say "Checksums are published: marking $TAG as the latest release"
    gh release edit "$TAG" --latest
  fi
else
  echo "warning: no SUPABASE_SERVICE_KEY / SUPABASE_ACCESS_TOKEN set: the checksums were NOT published, so launchers would refuse v$VER."
  echo "         $TAG was left as NOT latest. Publish the checksums (scripts/publish-supabase.sh), then: gh release edit $TAG --latest"
fi

# ---- verify what users will download matches what we built --------------------
say "Verifying the GitHub assets"
gh release view "$TAG" --json assets --jq '.assets[] | "\(.name) \(.size) \(.digest // "")"' | while read -r n s d; do
  case "$n" in OkaySpace.exe|OkayEngine.exe|OkaySpacePlayer.exe)
    src=$(ls build-win-dist/$n build-win-dist/Tools/$n 2>/dev/null | head -1)
    want=$(sha256sum "$src" | cut -d' ' -f1)
    if [ "${d#sha256:}" = "$want" ]; then echo "  ok   $n"; else echo "  FAIL $n (GitHub ${d:-no digest}, local $want)"; exit 1; fi ;;
  esac
done
say "Done: https://github.com/kingimann/OkaySpaceGameEngine/releases/tag/$TAG"
