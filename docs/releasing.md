# Releasing an update

Launchers and editors look for updates in two places and use whichever has the
newer version:

1. **Supabase Storage**: the public `releases` bucket on the project in
   `CMakeLists.txt` (`OKAY_DEFAULT_ACCOUNT_URL`). Checked first.
2. **GitHub Releases**: `releases/latest/download/<file>`. The fallback.

So an update can ship without the GitHub workflow: build, then publish to
Supabase.

## Publish to Supabase

```bash
# 1. Bump the version in CMakeLists.txt (project(... VERSION x.y.z)).
# 2. Build the Windows files (MinGW cross-compile; splits the editor's debug info).
scripts/build-windows-dist.sh
# 3. Upload. Either key works; it is read from the environment only.
SUPABASE_ACCESS_TOKEN=sbp_...   scripts/publish-supabase.sh   # management token
SUPABASE_SERVICE_KEY=eyJ...     scripts/publish-supabase.sh   # or the service_role key
```

The bucket layout:

| Path | What |
| --- | --- |
| `VERSION.txt` | newest version, uploaded last (cached 30 s) |
| `NOTES.txt` | "what's new" lines for the launcher (default: recent commit subjects; set `NOTES_FILE` to choose) |
| `v<ver>/OkaySpace.exe`, `OkayEngine.exe`, `OkaySpacePlayer.exe`, `CHANGELOG.txt` | that version's files |

Only the newest 3 versions are kept (`KEEP=n` to change). `DRY_RUN=1` checks
the build and key without uploading.

Limits: on the current plan each file must be at most 50 MB. The editor is
about 23 MB because its debug info is split into `build-win-symbols/OkayEngine.debug`
(keep that file to decode crash addresses; it isn't uploaded to Supabase).
Storage and download traffic count against the Supabase plan's quotas.

Never commit either key. For a Claude cloud session, add `SUPABASE_ACCESS_TOKEN`
in the environment's settings (Network secrets, or an environment variable).

## Publishing the official game

The official game shows at the top of the launcher's Play page. Players click
Install, then Play, and the launcher keeps it up to date. There are two ways to
ship a version: from the launcher as an admin (recommended), or with the script.
Both upload the same files in the same order, with the same checks.

### From the launcher (recommended)

Once, the project owner runs `docs/sql/official_game_admin.sql` in the Supabase
SQL editor (after `admin.sql`). It lets accounts with the `admin` role, and
nobody else, write under `official/` in the `releases` bucket; nothing outside
`official/` becomes writable, and reads stay public. Until it's run, publishing
from the launcher is refused and the tab says to run it.

Then, signed in as an admin:

1. In the editor: File > Build Game for Windows, into its own folder. Optional
   extras in that folder: `cover.png` / `cover.jpg` and `VERSION.txt`.
2. In the launcher: **Admin > Official game**. "Live now" shows the published
   version, its notes, size and file count.
3. **Browse...** to the built folder. The tab shows the game's name, exe, cover,
   file count and size. The version is filled in for you (the folder's
   `VERSION.txt`, else today's date, as the script does) and must be newer than
   the live one unless you tick "Allow same or older" (the script's `FORCE=1`).
   Type the "What's new" lines (they become `NOTES.txt`).
4. **Check** lists anything that would stop the publish: not exactly one
   Windows game exe, a file over 50 MB, names Windows can't store or that clash,
   an empty or oversized game, a version that isn't newer.
5. **Publish...** uploads with your own sign-in: every file to `official/v<ver>/`,
   then `manifest.json`, then `NOTES.txt`, and `VERSION.txt` last. A progress bar
   shows file X of Y. Cancel works until the last step and leaves the live
   version as it was (the partial `v<ver>` can be deleted from the list). Players'
   launchers pick up the new version on their next update check.

"Versions in the bucket" lists every `official/v*` with its size. **Make live**
switches `VERSION.txt` and `NOTES.txt` back (or forward) to an existing version,
e.g. to roll back a bad release (players who already have a newer version keep
it, since launchers only move forward; new installs get the live one).
**Delete** removes a version (never the live one). **Clean up old versions**
keeps the live version and the newest complete ones (N in all) and deletes the
rest, including unfinished uploads older than the live version; nothing is
deleted automatically. Publishes, "make live" and deletes are written to the audit log.

### With the script

The script needs a Supabase key (the service key, or a management token that
fetches it), so only use it on a machine you trust:

```bash
# 1. In the editor: File > Build Game for Windows, into its own folder.
#    Optional extras in that folder: cover.png / cover.jpg (the card's picture)
#    and VERSION.txt (the version, if you don't pass one).
# 2. Upload it. Same keys as above, read from the environment only.
SUPABASE_ACCESS_TOKEN=sbp_... scripts/publish-official-game.sh "path/to/Okay Sandbox" 1.0.0
DRY_RUN=1 SUPABASE_ACCESS_TOKEN=sbp_... scripts/publish-official-game.sh "path/to/Okay Sandbox"   # check only
```

The version is the second argument, else the first line of `VERSION.txt` in the
folder, else today's date (`2026.10.24.1`, then `.2` for a second publish that
day). It must be newer than the published version (`FORCE=1` to override);
launchers only ever move forward.

The bucket layout, beside the engine's files:

| Path | What |
| --- | --- |
| `official/VERSION.txt` | newest version, uploaded last (cached 30 s) |
| `official/NOTES.txt` | "what's new" lines (`NOTES_FILE` to choose; default `- <name> v<ver>`) |
| `official/v<ver>/manifest.json` | name, version, exe, notes, cover image, total size, and each file's path, size and sha256 |
| `official/v<ver>/<path>` | every file of the built game, as laid out by Build Game |

The game's name is `title=` from `Data/game.okayconfig` (or `NAME=...`); its exe
is `<title>.exe`, or the only `.exe` at the top of the folder (or `EXE=...`). The
script refuses a non-Windows exe, a file over 50 MB, names Windows can't store
(or that only differ in case), a top-level `manifest.json`, and an empty or
over-10 GB game. Hidden files (`.git`, `.DS_Store`) are left out. As with the engine, `KEEP` (default 3) versions are kept (the script, unlike
the launcher, cleans up on its own) and `DRY_RUN=1`
uploads nothing (`DRY_RUN_MANIFEST=out.json` saves the manifest it would upload).

What players get: the launcher installs to `Games/Official` beside it, checks
`official/VERSION.txt` at startup and when the Play page opens (at most every 5
minutes), and every half hour while installed. Only files whose sha256 changed
are downloaded, into a staging folder, and each is verified before anything is
replaced; the swap waits until the game is closed and is undone if it fails, so
a bad download never breaks an install. Settings > Behavior > "Keep the official
game up to date" turns background updates off (the card then offers an
"Update available" button).

## GitHub Releases (still supported)

The `Release Windows build` workflow builds, makes the zip + installer and
publishes a GitHub Release. If the repo has a `SUPABASE_SERVICE_KEY` secret it
also runs `scripts/publish-supabase.sh`, so both places stay in step.

Launchers older than v2.794.0 only know about GitHub, so the first release that
contains the Supabase updater must also go out through GitHub Releases.
