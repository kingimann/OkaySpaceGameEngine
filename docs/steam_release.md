# Releasing on Steam

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
OkaySpaceEngine.exe          # your build (editor or your game)
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
