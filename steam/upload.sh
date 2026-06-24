#!/usr/bin/env bash
# Upload the game in content/ to Steam (App 1172560, Depot 1172561) via SteamPipe.
#
#   1. Export your game from the editor (File > Build Game) into  steam/content/
#      (so content/ holds YourGame.exe + SDL2.dll + assets).
#   2. STEAM_USER=<your-builder-account> ./upload.sh
#      (first run prompts for password + Steam Guard code; steamcmd caches it after).
#   3. By default the build uploads but is NOT live — go to the Steamworks
#      "Builds" page and set it live on the 'default' branch. To auto-publish on
#      every upload instead, set  "setlive" "default"  in app_build_1172560.vdf.
#
# Needs steamcmd on PATH: https://developer.valvesoftware.com/wiki/SteamCMD
set -euo pipefail
cd "$(dirname "$0")"

: "${STEAM_USER:?Set STEAM_USER to your Steamworks builder account, e.g. STEAM_USER=me ./upload.sh}"

if ! command -v steamcmd >/dev/null 2>&1; then
    echo "error: steamcmd not found on PATH. Install it: https://developer.valvesoftware.com/wiki/SteamCMD" >&2
    exit 1
fi
if [ -z "$(ls -A content 2>/dev/null | grep -v PUT_YOUR_GAME_BUILD_HERE)" ]; then
    echo "error: steam/content/ is empty — export your game there first (editor: File > Build Game)." >&2
    exit 1
fi

steamcmd +login "$STEAM_USER" +run_app_build "$(pwd)/app_build_1172560.vdf" +quit
echo "Done. If setlive was empty, set the build live on the Steamworks Builds page."
