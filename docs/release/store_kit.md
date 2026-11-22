# Store Kit: release your game on Steam or the Epic Games Store

> **What this is:** the OkaySpace Store Kit. One set of calls for achievements, stats, leaderboards, cloud saves and
> presence that works the same on Steam, on the Epic Games Store and with no store at all. You never write code
> against Valve's or Epic's SDK.
>
> **Status:** Steam and Epic both work through the library you copy from your own SDK (loaded when the game starts).
> Both were tested on Linux against stand-in libraries we wrote, never against the real Steam client, the real EOS SDK
> or Epic's servers, and never on Windows. Several record layouts and numbers were written from memory of Valve's and
> Epic's documentation and are unconfirmed (see "Unconfirmed details" below). No store: works, saved in the game's save
> folder.
>
> **Last checked:** 2026-11-22 (branch `claude2/steam-epic-stores-2`).

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

   Visual scripts: **Unlock Achievement**, **Achievement Unlocked**, **Submit Score**, **Store Stats**, **Set Store
   Stat**, **Add To Store Stat**, **Get Store Stat**, **Cloud Save Write / Read / Exists / Delete** and **Set
   Presence** go to the same place (Add Node > Steam).

   On Steam and Epic, calls made in the first frames (before the store has answered) are kept and sent once it has.
   Reads that need the server (`Leaderboards.Top`, a cloud file saved on another computer, Epic stat totals) return
   what is known now and fill in a moment later, so read them again on a later frame.

2. **Try it with no store.** File > Examples > **Store Kit**, or open `examples/store_kit/store_kit.okayscene`, and
   press Play. Keys 1 to 5 unlock, add
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
| Achievements, stats | `steam_api64.dll` | `EOSSDK-Win64-Shipping.dll` | save folder |
| In-client achievement popup, overlay | `steam_api64.dll` + Steam client | friends overlay (`EOS_UI_ShowFriends`) | none |
| Cloud saves | `steam_api64.dll` (Steam Cloud) | `EOSSDK-Win64-Shipping.dll` (Player Data Storage) | save folder |
| Presence | `steam_api64.dll` (rich presence) | `EOSSDK-Win64-Shipping.dll` (one line of text, at most 255 bytes) | ignored |
| Ownership check (`Store.OwnsGame`) | `steam_api64.dll` | the launcher only starts owned games | always yes |
| Leaderboards | `steam_api64.dll` (the board is made on first use, best score kept, top rows downloaded) | the score goes to a stat of the board's name; `Leaderboards.Top` shows only the player's best | save folder |
| Launcher login | Steam client | the launcher's `-AUTH_PASSWORD` exchange code (never logged), then EOS Connect | none |
| Check that script ids exist on the store | Steam Web API, our own HTTP code | not written | none |

Without the library (or with the store client closed), the game keeps working with a simulation and logs one line.

## Secrets

No key is ever saved in the project or written to a log:

- `STEAM_WEB_API_KEY` - your Steam publisher Web API key, used only by Build Settings > Stores > *Check them on
  Steam*. Sent in the `x-webapi-key` request header, never in the address.
- `OKAY_EOS_CLIENT_SECRET` - the game's EOS client secret. The build reads it and packs it into the game
  (`<Game>_Data/epic.okaysecret`).
- `EPIC_BPT_CLIENT_SECRET` - BuildPatchTool's secret. Only `upload.bat` reads it, by name.

## Unconfirmed details

These were written from memory of Valve's and Epic's public documentation and have never been checked against the real
SDKs, clients or servers. Each is one constant or one line, so a correction is small. Check them first when something
fails on a real store.

- Steam Web API: the key is sent in the `x-webapi-key` request header (`Steam/SteamWebApi.cpp`).
- SteamPipe depot script: `FileExclusion` with wildcards (`StoreUpload\*`, `*.pdb`) (`StoreExport.cpp`).
- BuildPatchTool: the `-FileIgnoreList` file format (one path per line, relative to the build folder) (`StoreExport.cpp`).
- Steam leaderboards (`Steam/SteamFlat.hpp`): the callback numbers 1104, 1105, 1106; the field layout of the three
  result records and of `LeaderboardEntry_t` (64-bit Windows packing); the enum values (descending 2, numeric 1, keep
  best 1, global 0); that call results can be collected with `ISteamUtils::GetAPICallResult` while
  `SteamAPI_RunCallbacks` is also used.
- EOS (`Epic/EosFlat.hpp`): every `ApiVersion` number; the field order of `EOS_Platform_Options` after `DeploymentId`;
  the result numbers other than success (InvalidUser 3, AlreadyConfigured 15, NotFound 18); `EOS_ECT_EPIC_ID_TOKEN` =
  16; the login types (exchange code 1, developer 4); the read and write result values; the 4096-byte write chunk; the
  login scopes (basic profile, friends list, presence) must match the ones set in the Developer Portal.

## Where the code is

`engine/include/okay/Platform/PlatformServices.hpp` (the calls), `Steam/DynamicSteamService.cpp` (the run-time Steam
library), `Steam/SteamWebApi.hpp` (the Web API check), `Epic/EpicService.hpp` (the Epic seam and the launcher
arguments), `Epic/DynamicEpicService.cpp` (the run-time EOS library), `StoreExport.hpp` (upload files). Visual Script
nodes: `engine/include/okay/VisualScript/Nodes.hpp`. Tests: `tests/test_stores.cpp` (with the stand-in libraries
`tests/fake_steam_api.cpp` and `tests/fake_eos_sdk.cpp`), `tests/test_build_settings.cpp`.
Everything was written from Valve's and Epic's public documentation; no SDK code, header or sample is in the repository.
