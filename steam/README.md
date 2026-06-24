# Releasing OkaySpace on Steam (SteamPipe) — App 1172560

These scripts publish the **whole OkaySpace application** (launcher + editor +
player + relay) to Steam so installed copies **auto-update**: upload a new build,
set it live, and every player's Steam client patches itself on next launch.

## Files
- `app_build_1172560.vdf` — build script (app + depot + auto-publish branch)
- `depot_build_1172561.vdf` — depot 1172561 contents (everything in the dist)
- `upload.sh` / `upload.bat` — wrappers that run `steamcmd`

The depot content is the Windows distribution in `../build-win-dist/`
(`OkaySpace.exe` + `SDL2.dll` + `Tools/`), produced by
[`scripts/build-windows-dist.sh`](../scripts/build-windows-dist.sh) — see the
layout in [../docs/packaging.md](../docs/packaging.md). `upload.sh` builds it if
it's missing.

## One-time setup
1. Install [SteamCMD](https://developer.valvesoftware.com/wiki/SteamCMD) on PATH.
2. Steamworks account with **builder** access to App 1172560, with Depot 1172561
   attached to the app's build/package in the dashboard.
3. On the Steamworks **Store/App** pages, set up the store listing, then the app
   has to pass Valve review before the first public release (a one-time gate;
   updates after launch don't need re-review).

## Each release
```bash
export SDL2_MINGW_PREFIX=/path/to/SDL2-<ver>/x86_64-w64-mingw32   # to build the dist
STEAM_USER=your-builder-account ./upload.sh
```
- `upload.sh` builds `../build-win-dist/` (if absent) and uploads it.
- First login prompts for password + Steam Guard; SteamCMD caches the session
  afterward (good for a CI builder account).
- **Go live:** by default the build uploads but isn't published — open the
  Steamworks **Builds** page, pick the new build, set it live on `default`.
  Players auto-update from there.

## Auto-publish (skip the manual "set live")
Set `"setlive" "default"` in `app_build_1172560.vdf` and every upload goes live
immediately. Many teams instead set `"setlive" "beta"`, test on the beta branch,
then promote to `default` in the dashboard.

## Notes
- `"preview" "1"` in the app-build VDF = dry run (validate, no upload).
- SteamPipe ships only changed chunks, so updates are small/fast.
- Bump the `desc` field per release — it's the label on the Builds page.
- Cross-compiling the dist from Linux is fine for upload; Steam stores the bytes
  regardless of where they were built. (A native Windows build works too.)
