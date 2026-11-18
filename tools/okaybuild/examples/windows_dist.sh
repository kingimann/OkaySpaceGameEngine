#!/bin/sh
# Builds the three Windows programs (launcher, editor, player) and the relay with okay-build and MinGW (no CMake, no Ninja), checks them
# with `okay-build tool pe`, and assembles the same folder layout as scripts/build-windows-dist.sh with `okay-build package`.
# usage: sh windows_dist.sh OKAY_BUILD_EXE WORK_DIR [JOBS]
# Needs: x86_64-w64-mingw32-g++ (win32 thread model), windres, git, curl (for `okay-build fetch imgui`),
#   SDL2_MINGW_PREFIX    the x86_64-w64-mingw32 folder of the SDL2 MinGW development files (bin/SDL2.dll, lib/libSDL2.dll.a)
#   OKAY_SODIUM_MINGW_PREFIX  the libsodium-win64 folder (scripts/fetch-libsodium-mingw.sh <dir> prints it)
# Differences from build-windows-dist.sh, on purpose or not done: Assimp is off (it is a download and a long build), no ccache (okay-build
# has its own cache), no Steamworks, the release-only checks (check_release_own_stack.sh) are not run, no installer, no zip.
# Linux only; nothing here was run on Windows. Not part of CMake, CI or releases.
set -e
tool=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
work=${2:?usage: windows_dist.sh OKAY_BUILD_EXE WORK_DIR [JOBS]}; mkdir -p "$work"; work=$(cd "$work" && pwd)
jobs=${3:-2}
: "${SDL2_MINGW_PREFIX:?set SDL2_MINGW_PREFIX}"; : "${OKAY_SODIUM_MINGW_PREFIX:?set OKAY_SODIUM_MINGW_PREFIX}"
repo=$(cd "$(dirname "$0")/../../.." && pwd); cd "$repo"
man="$repo/build.windows.okay"; trap 'rm -f "$man"' EXIT
ver=$(grep -m1 -oE 'VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')
part="$work/dist.part"
{
  printf 'dist\n dir = %s\n launcher = %s/OkaySpace.exe\n dll = %s/bin/SDL2.dll\n tool = %s/OkayEngine.exe\n tool = %s/OkaySpacePlayer.exe\n tool = %s/okayspace-relay.exe\n tool = docs/accounts.md\n' \
    "$work/dist" "$work/build/build" "$SDL2_MINGW_PREFIX" "$work/build/build" "$work/build/build" "$work/build/build"
  printf ' version = %s\n textures = assets/textures\n fonts = assets/fonts\n' "$ver"
  # the same curated folder list the shell script ships, read from it (one folder per line between EXAMPLE_DIRS=" and ")
  sed -n '/^EXAMPLE_DIRS="/,/^"/p' scripts/build-windows-dist.sh | grep -v 'EXAMPLE_DIRS=\|^"' | while read -r d; do [ -n "$d" ] && printf ' example = examples/%s\n' "$d"; done
  printf 'end\n'
} > "$part"
"$tool" cmake . -o "$man" --extra "$part" --build-type Release --toolchain-file cmake/mingw-w64-x86_64.toolchain.cmake \
  --opt OKAY_BUILD_TESTS=OFF --opt OKAY_BUILD_SANDBOX=OFF --opt OKAY_BUILD_EDITOR=ON --opt OKAY_BUILD_PLAYER=ON --opt OKAY_USE_ASSIMP=OFF \
  --opt CMAKE_PREFIX_PATH="$SDL2_MINGW_PREFIX" --opt OKAY_SODIUM_PREFIX="$OKAY_SODIUM_MINGW_PREFIX" --opt OKAY_REQUIRE_SODIUM=ON --opt OKAY_STEAM_APP_ID=1172560
"$tool" -f "$man" fetch imgui
out="$work/build"
"$tool" -f "$man" -j"$jobs" --build-dir "$out" okay-player okay-editor okayspace-launcher okayspace-relay
b="$out/build"
for f in OkaySpace OkayEngine OkaySpacePlayer okayspace-relay; do "$tool" tool pe "$b/$f.exe"; done
"$tool" -f "$man" package
echo "dist folder: $work/dist"
