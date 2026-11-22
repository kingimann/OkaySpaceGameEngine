# Releasing your game on the Epic Games Store

> **What this is:** how a game made with OkaySpace is set up and uploaded for the Epic Games Store.
>
> **Status:** Settings, checks, the BuildPatchTool upload script, the launcher's arguments and the game's connection
> to Epic Online Services (login, achievements, stats, Player Data Storage, presence, the friends overlay) are written.
> The connection loads the EOS SDK library you copy from your own SDK and was tested on Linux against a stand-in
> library we wrote. Nothing was run against the real SDK, Epic's servers or on Windows, and the shapes of the EOS
> records were written from memory of Epic's documentation (store_kit.md, "Unconfirmed details").
>
> **Last checked:** 2026-11-22 (branch `claude2/steam-epic-stores-2`).

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

When the game starts it loads `EOSSDK-Win64-Shipping.dll` from beside the game (the build copies it from your SDK
folder), creates the EOS platform with the four ids and the client secret, and logs in: first the Epic account
(`EOS_Auth_Login` with the exchange code), then the game services (`EOS_Connect_Login` with the account's id token; the
first time a player plays, `EOS_Connect_CreateUser` makes their product user). After that it reads the player's
achievements, stats and the list of cloud files (and fetches the files, so `CloudSave.Read` has them), and sends
whatever the game did before the login finished. Everything runs from the game's per-frame tick.

- Achievements: `EOS_Achievements_UnlockAchievements`. Define them in the portal with the same ids.
- Stats: `Stats.Add` sends the change, `Stats.Set` sends the value (`EOS_Stats_IngestStat`); how a stat adds up (sum,
  largest, latest, smallest) is set per stat in the portal. After each send the totals are read back.
- Cloud saves: Player Data Storage, written and read in 4096-byte pieces. Files are encrypted with a key made from the
  product and deployment ids (the same for every copy of the game, so saves stay readable after updates).
- Presence: one line of rich text (`Presence.Set("status", "In the Arena")`), at most 255 bytes.
- `Store.Overlay(...)` opens the friends overlay.

Without the DLL, the client secret or a launcher login, the game runs with the simulation and logs one line.
To test without the launcher, run Epic's Developer Authentication Tool and set `OKAY_EOS_DEV_AUTH=host:port|credential`
before starting the game; the credential name is never logged.

## Not done

- Leaderboards on Epic (they are built on stats in the portal; the facade sends the score to a stat of the board's name;
  `Leaderboards.Top` shows only the player's own best).
- The player's Epic display name comes from the launcher's `-epicusername`, not from Epic's user info service.
- Achievement progress on Epic: achievements built on stats unlock on Epic's side; `Achievements.Progress` only unlocks
  a plain achievement when it is complete.
- Any check of the ids against Epic's web services.
- Never run on Windows or against the real EOS SDK, never uploaded.
