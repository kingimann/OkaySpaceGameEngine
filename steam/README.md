# Steam upload (SteamPipe) — App 1172560

These scripts push your game to Steam so players **auto-update**. Upload a new
build, set it live, and every installed copy patches itself on next launch.

## Files
- `app_build_1172560.vdf` — the build script (app + which depot + auto-publish branch)
- `depot_build_1172561.vdf` — what files go in depot 1172561 (everything in `content/`)
- `upload.sh` / `upload.bat` — wrappers that run `steamcmd`
- `content/` — **put your exported game here** (not committed)

## One-time
1. Install [SteamCMD](https://developer.valvesoftware.com/wiki/SteamCMD) and put it on PATH.
2. Have a Steamworks account with **builder** access to App 1172560.
3. In the Steamworks dashboard, make sure App 1172560 has Depot 1172561 attached
   to its build / package.

## Each release
1. In the editor: **File → Build Game**, and point the output at `steam/content/`
   (so `content/` holds `YourGame.exe` + `SDL2.dll` + your assets — see
   [../docs/packaging.md](../docs/packaging.md) for the recommended layout).
2. Upload:
   ```bash
   STEAM_USER=your-builder-account ./upload.sh      # macOS/Linux
   ```
   ```bat
   set STEAM_USER=your-builder-account
   upload.bat                                        REM Windows
   ```
   First login prompts for your password + Steam Guard code; SteamCMD caches the
   session afterward (good for CI with a dedicated builder account).
3. **Go live**: by default the build uploads but isn't published. Open the
   Steamworks **Builds** page, pick the new build, and set it live on the
   `default` branch. Players auto-update from there.

## Auto-publish (skip the manual "set live")
Set `"setlive" "default"` in `app_build_1172560.vdf` and every `upload` goes live
immediately. Convenient, but there's no safety review — many teams instead set
`"setlive" "beta"`, test on the beta branch, then promote to `default` in the
dashboard.

## Tips
- Set `"preview" "1"` in the app-build VDF for a dry run (validates without uploading).
- SteamPipe only ships changed chunks, so updates are small and fast.
- The `desc` field in the app-build VDF is the label you'll see on the Builds page —
  bump it per release (e.g. a version string).
