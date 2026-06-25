#!/usr/bin/env bash
# Publish the whole OkaySpace application to Steam (App 1172560, Depot 1172561)
# via SteamPipe, so installed copies auto-update.
#
# It uploads the Windows distribution from build-win-dist/ (OkaySpace.exe + SDL2.dll
# + Tools/), which scripts/build-windows-dist.sh produces. This script builds that
# for you if it's missing.
#
#   STEAM_USER=<your-builder-account> ./upload.sh
#     - first login prompts for password + Steam Guard; steamcmd caches it after.
#     - by default the build uploads but is NOT live: set it live on the Steamworks
#       "Builds" page (default branch). To auto-publish every upload, set
#       "setlive" "default" in app_build_1172560.vdf.
#
# Needs steamcmd on PATH: https://developer.valvesoftware.com/wiki/SteamCMD
# Building the dist needs SDL2_MINGW_PREFIX set (see scripts/build-windows-dist.sh).
set -euo pipefail
cd "$(dirname "$0")"
REPO="$(cd .. && pwd)"

: "${STEAM_USER:?Set STEAM_USER to your Steamworks builder account, e.g. STEAM_USER=me ./upload.sh}"

if ! command -v steamcmd >/dev/null 2>&1; then
    echo "error: steamcmd not found on PATH. Install it: https://developer.valvesoftware.com/wiki/SteamCMD" >&2
    exit 1
fi

# Build the Windows distribution if it isn't there yet.
if [ ! -f "$REPO/build-win-dist/OkaySpace.exe" ]; then
    echo "build-win-dist/ not found — building the Windows distribution..."
    "$REPO/scripts/build-windows-dist.sh"
fi

steamcmd +login "$STEAM_USER" +run_app_build "$(pwd)/app_build_1172560.vdf" +quit
echo "Done. If setlive was empty, set the build live on the Steamworks Builds page."
