# Releasing your game on the Epic Games Store

> **What this is:** how a game made with OkaySpace is set up and uploaded for the Epic Games Store.
>
> **Status:** Settings, checks, the BuildPatchTool upload script, the launcher's arguments and the Epic simulation are
> written and tested on Linux. The game's real connection to Epic Online Services (login, achievements, stats, player
> data storage, presence) is **not written yet**: an Epic build runs with the simulation. Nothing was run against
> Epic's servers or on Windows.
>
> **Last checked:** 2026-11-22 (branch `claude2/steam-epic-stores`).

We never ship, commit or download Epic's SDK or BuildPatchTool. You get them from the Epic Developer Portal yourself.

## 1. In the Developer Portal

- Product Settings: the **product id**, a **sandbox id**, a **deployment id**, and a game **client** (its id and secret).
- Artifacts and Binaries: the **organization id** and the **artifact id**, and BuildPatchTool credentials (a client id
  and a secret).

## 2. In OkaySpace

File > Build Settings > **Stores** > Epic Games Store. Type the ids. Leave the two secrets out: they never go in the
project.

- Before you build, set `OKAY_EOS_CLIENT_SECRET` to the game client's secret. The build packs it into
  `<Game>_Data/epic.okaysecret` (scrambled, not readable as text). Without it the build warns and the game cannot log in.
- Optionally set the **EOS SDK folder**; the build copies `EOSSDK-Win64-Shipping.dll` beside the game (from `SDK/Bin`).
- Build. The game's `game.okayconfig` gets `store=epic` and the four ids. `StoreUpload/epic/` gets `upload.bat` and
  `ignore.txt`.

## 3. Upload

```
set EPIC_BPT_CLIENT_SECRET=<BuildPatchTool secret>
StoreUpload\epic\upload.bat
```

It runs `BuildPatchTool.exe -mode=UploadBinary` with your organization, product and artifact ids, the build folder as
`-BuildRoot`, `-BuildVersion=<version>-b<build>-win64`, `-AppLaunch=<Game>.exe`, `-ClientSecretEnvVar=EPIC_BPT_CLIENT_SECRET`
(BuildPatchTool reads the secret itself) and `-FileIgnoreList` (the `StoreUpload` files and `steam_appid.txt` stay out).
Set `BPT` to BuildPatchTool's path if it is not on `PATH` and not set on the Stores page. Its working folder is
`%LOCALAPPDATA%\OkaySpace\BuildPatchTool\<artifact>`. Then label the binary for a sandbox in the portal.

## 4. In the game

The launcher starts the game with `-AUTH_PASSWORD=<exchange code> -AUTH_TYPE=exchangecode -epicapp=... -epicenv=...
-EpicPortal -epicusername=... -epicuserid=... -epiclocale=...`. The player reads these (`okay::ParseEpicLaunchArgs`);
the exchange code is never logged (`okay::RedactEpicArgs`). Scripts use the Store Kit calls (`store_kit.md`).
`OKAY_EOS_DEV_AUTH=host:port|credential` is read for the EOS developer login tool (used once the real connection exists).

## Not done

- The real EOS connection (platform, login with the exchange code, achievements, stats, player data storage,
  presence, the overlay). It is planned as a run-time load of the developer's `EOSSDK-Win64-Shipping.dll` behind
  `IEpicService`, like Steam's. Until then `Epic::Get()` is the simulation.
- Leaderboards on Epic (they are built on stats in the portal; the facade sends the score to a stat of the board's name).
- Any check of the ids against Epic's web services.
- Never run on Windows, never uploaded.
