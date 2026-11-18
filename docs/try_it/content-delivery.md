---
id: content-delivery
row: | content-delivery | Downloadable game content: signed models, textures, scripts and data that an exported game fetches (off by default) | `okay-content.exe` in `Tools/`, or `content.okayconfig` / `OKAY_CONTENT_URL` for the player | Built; Linux-tested only |
---
## content-delivery. Downloadable game content (off by default, Windows first)

An exported game can download updated **data and scripts** (models, textures, animations, scenes, audio, dialogue, Visual Script
and OkayScript files) from a folder or a server. Never code or programs. Full description: `docs/content_delivery.md`.
Nothing changes in a game unless a content URL is set. Not tested on Windows or an Xbox yet.

You need a Windows PC with the release folder (the `Tools` folder holds `okay-content.exe`), Git Bash or WSL for the
publish script (only for step 6), and Python 3 once to make a key.

1. **Make a key.** In a terminal in the repo: `python3 scripts/sign_index.py genkey`. It prints a seed (private: keep it, never
   share it) and a public key. Write them down for the steps below (`SEED` and `PUB`).
2. **Make a content folder** (for example `C:\test\content-v1`) holding a few real files, in the same layout your game uses:
   `models\ship.obj`, `scripts\boss.okayvs` (copy a graph saved by the Visual Script editor), `strings.okayloc`.
3. **Sign it.** `set OKAY_CONTENT_SIGNING_SEED=SEED` then
   `okay-content.exe make-manifest C:\test\content-v1 --game mygame --version 1`. You should see "wrote content.manifest ... and
   content.manifest.sig". Then `okay-content.exe verify C:\test\content-v1 --keys PUB --game mygame` should say `OK`.
   Try a file named `run.exe` in the folder: make-manifest must refuse it.
4. **Download it like a player.** `okay-content.exe sync C:\test\content-v1 C:\test\player-content --game mygame --keys PUB`.
   You should see each file, then `status=updated ... downloaded=3`. Run it again: `status=up-to-date`, nothing downloaded.
   Change one file, make version 2 (`--version 2`), sync again: only the changed file downloads.
5. **Try a bad key.** Repeat the sync with a different public key: it must refuse and `C:\test\player-content\current.txt` must not
   appear (or stay at the old version).
6. **Use it in a game.** Build a game with Build Game, put a text file `content.okayconfig` beside `OkaySpacePlayer.exe` (next to the
   game's `_Data` folder) with
   ```
   url=file:///C:/test/content-v1
   game=mygame
   keys=PUB
   ```
   (or set the environment variables `OKAY_CONTENT_URL`, `OKAY_CONTENT_GAME`, `OKAY_CONTENT_KEYS`). Start the game. The game's log
   (the player prints it at start-up; a development build also writes it to `game.log`) shows `content: updated, serving downloaded content version 1`. A downloaded
   `strings.okayloc`, `items.okayitems`, `weapons.okayweapon`, a scene named in the scene list, a UI image or an `.obj` mesh now
   wins over the installed one. With no `content.okayconfig` and no variable the game is unchanged.
7. **Publish for real (optional).** With Git Bash and a Supabase service key in `SUPABASE_SERVICE_KEY`:
   `OKAY_CONTENT_SIGNING_SEED=SEED scripts/publish-content.sh C:/test/content-v1 --game mygame --version 3`. It prints the
   `url=` line to use. `DRY_RUN=1` checks everything and uploads nothing.

Tests (Linux): `ctest -R "content_"` and `ctest -R publish_content`. Not verified: Windows, a real bucket, the Xbox app (its
network transport is a stub that says so).
