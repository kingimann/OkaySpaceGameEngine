# Store Kit: release your game on Steam or the Epic Games Store

> **What this is:** the OkaySpace Store Kit. One set of calls for achievements, stats, leaderboards, cloud saves and
> presence that works the same on Steam, on the Epic Games Store and with no store at all. You never write code
> against Valve's or Epic's SDK.
>
> **Status:** Steam works through the Steam library you copy from your own SDK (loaded when the game starts; tested on
> Linux with a stand-in library, never against the real Steam client). Epic: settings, upload script and the
> simulation work; the game's real Epic connection is **not written yet**. No store: works, saved in the game's save
> folder.
>
> **Last checked:** 2026-11-22 (branch `claude2/steam-epic-stores`).

## Integrate in 5 minutes

1. **Write the calls.** In any OkayScript:

   ```
   Achievements.Unlock("first_win")          // achievement
   Achievements.Progress("collector", 3, 10) // progress; unlocks at 10 of 10
   Stats.Add("coins", 1)                     // whole-number stats: Set, Add, Get
   Stats.Store()                             // send changed stats now
   Leaderboards.Submit("high", 1200)         // a score; Leaderboards.Top("high", 10) lists the best
   CloudSave.Write("slot1", "level=4")       // cloud save; CloudSave.Read / Has / Delete
   Presence.Set("status", "In the Arena")    // what friends see
   Store.Overlay("achievements")             // open the store's overlay
   Store.Name()  Store.UserName()  Store.OwnsGame()  Store.IsOnline()
   ```

   Visual scripts: **Unlock Achievement**, **Achievement Unlocked**, **Submit Score** and **Store Stats** go to the
   same place.

2. **Try it with no store.** Open `examples/store_kit/store_kit.okayscene` and press Play. Keys 1 to 5 unlock, add
   coins, submit a score, save and read back; the text at the top shows the result. In the editor this is kept in
   memory. Build the scene (File > Build Settings, Build) and run the game: there it is kept in the game's save
   folder under `store/` (`store_local.txt` and `cloud/`), so the next run remembers it ("launches" counts up). No
   account is needed.

3. **Pick the store.** File > Build Settings > **Stores**: choose Steam or Epic Games Store and fill in the ids. The
   page checks them as you type (a missing app id is an error, a missing depot is a warning).

4. **Build.** The game gets `store=steam` (or `epic`) and the ids in its `game.okayconfig`, the store's runtime DLL
   from your SDK folder, and a `StoreUpload/` folder with everything to upload it.

5. **Upload.** Run `StoreUpload/steam/upload.bat` (Steam) or `StoreUpload/epic/upload.bat` (Epic). See
   `steam_release.md` and `epic_release.md`.

To test a store build on your computer without changing settings, set `OKAY_STORE=steam`, `epic` or `none` before
starting the game.

## What needs the store's own library

Some features only exist inside the store's client program, whose protocol is private. We do not copy or
reverse-engineer it. For those, the Store Kit loads the library **you** copy from your SDK, behind our own interface:

| Feature | Steam | Epic | No store |
|---|---|---|---|
| Achievements, stats | `steam_api64.dll` | not written yet (simulation) | save folder |
| In-client achievement popup, overlay | `steam_api64.dll` + Steam client | not written yet | none |
| Cloud saves | `steam_api64.dll` (Steam Cloud) | not written yet (simulation) | save folder |
| Presence | `steam_api64.dll` | not written yet (simulation) | ignored |
| Ownership check (`Store.OwnsGame`) | `steam_api64.dll` | the launcher only starts owned games | always yes |
| Leaderboards | kept for this session (the library route has no leaderboards yet) | stat-based, kept for this session | save folder |
| Launcher login | Steam client | `-AUTH_PASSWORD` exchange code is read and never logged; the login itself is not written yet | none |
| Check that script ids exist on the store | Steam Web API, our own HTTP code | not written | none |

Without the library (or with the store client closed), the game keeps working with a simulation and logs one line.

## Secrets

No key is ever saved in the project or written to a log:

- `STEAM_WEB_API_KEY` - your Steam publisher Web API key, used only by Build Settings > Stores > *Check them on
  Steam*. Sent in the `x-webapi-key` request header, never in the address.
- `OKAY_EOS_CLIENT_SECRET` - the game's EOS client secret. The build reads it and packs it into the game
  (`<Game>_Data/epic.okaysecret`).
- `EPIC_BPT_CLIENT_SECRET` - BuildPatchTool's secret. Only `upload.bat` reads it, by name.

## Where the code is

`engine/include/okay/Platform/PlatformServices.hpp` (the calls), `Steam/DynamicSteamService.cpp` (the run-time Steam
library), `Steam/SteamWebApi.hpp` (the Web API check), `Epic/EpicService.hpp` (the Epic seam and the launcher
arguments), `StoreExport.hpp` (upload files). Tests: `tests/test_stores.cpp`, `tests/test_build_settings.cpp`.
Everything was written from Valve's and Epic's public documentation; no SDK code, header or sample is in the repository.
