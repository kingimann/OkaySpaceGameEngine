# Releasing on Steam

> **What this is:** OkaySpace has Steam built in via `ISteamService` (achievements, stats, rich presence, identity).
>
> **Status:** Reference page for this feature. Kept as written; not extended (rule 5).
>
> **Last checked:** 2026-11-20 (moved into `release/`, paths and links checked; the text itself was not re-checked against the code).

OkaySpace has Steam built in via `ISteamService` (achievements, stats, rich
presence, identity). By default it uses an in-memory **simulation** backend so
everything runs without the SDK; to ship on Steam you compile against the real
**Steamworks SDK** and upload through Steamworks.

## 1. One-time Steamworks setup

1. Join the [Steamworks](https://partner.steamgames.com/) program and create an
   app to get your **App ID**.
2. Define your achievements/stats in the Steamworks dashboard (the IDs must match
   the strings you pass to `UnlockAchievement` / `SetStat`, e.g. `FIRST_OBJECT`).
3. Download the **Steamworks SDK**.

## 2. Build against the real backend

```bash
cmake -S . -B build -DOKAY_BUILD_EDITOR=ON \
      -DOKAY_WITH_STEAM=ON \
      -DSTEAMWORKS_SDK_PATH=/path/to/steamworks_sdk
cmake --build build -j
```

This swaps `SteamworksService` in for the simulation backend automatically — no
code changes. The editor's **Services** panel will then show
`Backend: steamworks (live)` when the Steam client is running.

## 3. Run locally

- Put a `steam_appid.txt` containing your App ID next to the executable (a `480`
  one — Valve's Spacewar test app — ships in `dist/` for development).
- Have the Steam client running and be logged in.

## 4. Ship it

Lay out the depot like this and upload with `steamcmd` / the Steamworks tool:

```
<YourGame>.exe               # your exported game (File → Build Settings…)
steam_api64.dll              # from the Steamworks SDK redistributable_bin/win64
steam_appid.txt              # optional for release (Steam injects the id)
<your assets / .okayscene files>
```

## How the engine uses Steam

- `EditorState` creates the service on startup and pumps callbacks every frame
  (`SteamManager` does the same for games).
- The editor unlocks demo achievements (`FIRST_OBJECT`, `FIRST_SAVE`, `HIT_PLAY`)
  to show the wiring — replace these with your own.
- The same pattern applies to **PlayFab** (LiveOps, leaderboards) and
  **multiplayer** (`NetworkManager`), which are also part of the engine and
  surfaced in the editor's **Services** panel.

## Releasing YOUR game on Steam (Build Settings > Stores)

The sections above publish the OkaySpace program itself (app 1172560). For a game made with OkaySpace:

1. Download the Steamworks SDK yourself from partner.steamgames.com (we never ship it).
2. File > Build Settings > **Stores** > Steam: your game's **app id**, its **depot id** (empty = app id + 1), the
   **Steamworks SDK folder**, optionally a **branch to set live** (empty = upload only, the safe choice) and a build
   description.
3. Build. Into the build folder go:
   - `steam_api64.dll` from `<SDK>/redistributable_bin/win64` (only when the SDK folder is set);
   - `steam_appid.txt` with your app id, **only in a development build** (it lets the game start without Steam
     launching it; a shipping build removes it, so Steam starts the game and the player asks Steam to relaunch it
     when it was started directly);
   - `StoreUpload/steam/app_build_<app>.vdf`, `depot_build_<depot>.vdf` (every file of the build except
     `StoreUpload\*`, `steam_appid.txt` and `*.pdb`), `upload.bat`, `upload.sh`, and `StoreUpload/README.txt`;
   - `store=steam` and `steam_app_id=<app>` in the game's `game.okayconfig`.
4. Upload: `set STEAM_USER=<your build account>` then `StoreUpload\steam\upload.bat`. steamcmd asks for the password
   and Steam Guard code itself; nothing secret is stored. `STEAMCMD` overrides the steamcmd path. Then set the build
   live on the Steamworks Builds page.

The game reaches Steam through that `steam_api64.dll`: the Store Kit (`store_kit.md`) loads it when the game starts
and calls its documented flat C functions; neither the engine nor your game is compiled against the SDK. Without the
DLL, or when Steam is not running, the game logs one line and uses the simulation. Building the engine with
`-DOKAY_WITH_STEAM=ON` (section 2) still works and takes priority.

**Check your ids:** set `STEAM_WEB_API_KEY` to your publisher Web API key and press *Check them on Steam* on the
Stores page: it lists every achievement id and stat name your scripts use that the app does not define. The key is
only read from the environment and sent in a request header.

Not done for games: Steam leaderboards through the run-time library (scores are kept for the session), Workshop and
lobbies through it, a check of depot ids, anything run on Windows or against the real Steam client.
