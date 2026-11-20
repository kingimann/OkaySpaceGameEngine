# Releasing

> **What this is:** How an update is released: the normal path and the one-dispatch fast release.
> **Status:** combined on 2026-11-20 from 2 older pages, one section each (listed below). Every fact was kept; text marked **Outdated** is kept for history.
> **Last checked:** 2026-11-20, paths and links against main.

## Contents

- [Releasing an update](#releasing-an-update) (was `docs/releasing.md`)
- [Fast release: one dispatch, no version-bump PR](#fast-release-one-dispatch-no-version-bump-pr) (was `docs/fast_release.md`)

## Releasing an update

*Was `docs/releasing.md` until 2026-11-20.*

Launchers and editors look for updates in two places and use whichever has the
newer version:

1. **Supabase Storage**: the public `releases` bucket on the project in
   `config/public-account.json` (`url`; `docs/online/accounts.md`). Checked first.
2. **GitHub Releases**: `releases/latest/download/<file>`. The fallback.

So an update can ship without the GitHub workflow: build, then publish to
Supabase.

### Publish to Supabase

```bash
# 1. Bump the version in CMakeLists.txt (project(... VERSION x.y.z)).
# 2. Build the Windows files (MinGW cross-compile; splits the editor's debug info).
scripts/build-windows-dist.sh
# 3. Upload (SUPABASE_URL or OKAY_DEFAULT_ACCOUNT_URL override the project address, which is otherwise read from config/public-account.json). Either key works; it is read from the environment only.
SUPABASE_ACCESS_TOKEN=sbp_...   scripts/publish-supabase.sh   # management token
SUPABASE_SERVICE_KEY=eyJ...     scripts/publish-supabase.sh   # or the service_role key
```

The bucket layout:

| Path | What |
| --- | --- |
| `versions.json` | every released version with file sizes and SHA-256 (required by the updater; uploaded before `VERSION.txt`) |
| `VERSION.txt` | newest version, uploaded last (cached 30 s) |
| `NOTES.txt` | "what's new" lines for the launcher (default: recent commit subjects; set `NOTES_FILE` to choose) |
| `v<ver>/OkaySpace.exe`, `OkayEngine.exe`, `OkaySpacePlayer.exe`, `CHANGELOG.txt` | that version's files |

`versions.json` is required: it lists the size and SHA-256 of `OkayEngine.exe`, `OkaySpacePlayer.exe` and `OkaySpace.exe`, and the launcher
refuses to update any file without one. The script checks that it can compute all of them before it uploads anything (also under
`DRY_RUN=1`), uploads `versions.json` before `VERSION.txt`, reads it back from the bucket and compares it with the uploaded files, and
stops (no `VERSION.txt`, so nobody is offered the version) if anything is off. Re-running the publish is safe. A release that exists only
on GitHub has no `versions.json`, so launchers show "cannot update: no checksum list" instead of installing unchecked files.

Only the newest 3 versions are kept (`KEEP=n` to change). `DRY_RUN=1` checks
the build and key without uploading.

Limits: each file must be at most 100 MB (the project and bucket limit, set in Supabase Settings > Storage; the script checks `MAX_MB`, default 100). The editor is
about 23 MB because its debug info is split into `build-win-symbols/OkayEngine.debug`
(keep that file to decode crash addresses; it isn't uploaded to Supabase).
Storage and download traffic count against the Supabase plan's quotas.

Never commit either key. For a Claude cloud session, add `SUPABASE_ACCESS_TOKEN`
in the environment's settings (Network secrets, or an environment variable).

### Fast release (one dispatch, no bump PR; off until the owner switches it on)

`.github/workflows/fast-release.yml` works out the next version itself, refuses a commit that has no fully green CI run on main, takes the
release notes from small files in `dist/changelog.d/` (one per change, so no clashes), builds the checked commit with the version stamped in,
and checks the result. The by-hand way below keeps working. Everything: [fast_release.md](releasing.md#fast-release-one-dispatch-no-version-bump-pr).

### Try a branch before it is released (test builds)

To let the owner try a branch without a pull request or a release, start the **Test build (dev channel)** workflow (`gh workflow run dev-build.yml
--ref main -f ref=<branch> -f note="<what to try>"`). It publishes to the `dev/` folder only, with its own script (`scripts/publish-supabase-dev.sh`),
and never touches `VERSION.txt`, `versions.json` or the GitHub release. Everything about it, including the number it gets and how the owner switches it
on, is in [release_channels.md](release_channels.md#test-builds-for-the-owner-to-try-a-branch-not-a-channel-for-players).

### Publishing the official game

The official game shows at the top of the launcher's Play page. Players click
Install, then Play, and the launcher keeps it up to date. There are two ways to
ship a version: from the launcher as an admin (recommended), or with the script.
Both upload the same files in the same order, with the same checks.

#### From the launcher (recommended)

Once, the project owner runs `docs/sql/official_game_admin.sql` in the Supabase
SQL editor (after `admin.sql`). It lets accounts with the `admin` role, and
nobody else, write under `official/` in the `releases` bucket; nothing outside
`official/` becomes writable, and reads stay public. Until it's run, publishing
from the launcher is refused and the tab says to run it.

Then, signed in as an admin:

1. In the editor: File > Build Settings… (Windows), into its own folder. Optional
   extras in that folder: `cover.png` / `cover.jpg` and `VERSION.txt`.
2. In the launcher: **Admin > Official game**. "Live now" shows the published
   version, its notes, size and file count.
3. **Browse...** to the built folder. The tab shows the game's name, exe, cover,
   file count and size. The version is filled in for you (the folder's
   `VERSION.txt`, else today's date, as the script does) and must be newer than
   the live one unless you tick "Allow same or older" (the script's `FORCE=1`).
   Type the "What's new" lines (they become `NOTES.txt`).
4. **Check** lists anything that would stop the publish: not exactly one
   Windows game exe, a file over the size limit (100 MB), names Windows can't store or that clash,
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

#### With the script

The script needs a Supabase key (the service key, or a management token that
fetches it), so only use it on a machine you trust:

```bash
# 1. In the editor: File > Build Settings… (Windows), into its own folder.
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
script refuses a non-Windows exe, a file over the size limit (100 MB), names Windows can't store
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

### GitHub Releases (still supported)

The `Release Windows build` workflow builds, makes the zip + installer and
publishes a GitHub Release. If the repo has a `SUPABASE_SERVICE_KEY` secret it
also runs `scripts/publish-supabase.sh`, so both places stay in step.

Launchers older than v2.794.0 only know about GitHub, so the first release that
contains the Supabase updater must also go out through GitHub Releases.

## Fast release: one dispatch, no version-bump PR

*Was `docs/fast_release.md` until 2026-11-20.*

Status: **built and tested offline only, switched OFF.** It has never run on GitHub. The first real use is the dry run below, then one
real release. The old way (a "Release x.y.z" PR, then `release.yml` by hand) still works exactly as before.

The owner asked for faster releases, even small ones. Two things made a release slow: the version-bump PR with its own hour of CI, and
the changelog/ROADMAP edit that clashed between releases. This path removes both and keeps every safety rule.

### What you run

```bash
# 1. Always first: prints the version, the CI check and the notes it WOULD use. Builds and publishes nothing.
gh workflow run fast-release.yml --ref main -f dry_run=true
# 2. Read the run's summary page. If it says "Refused", the message says why.
# 3. Once, to switch the path on: repository variable FAST_RELEASE_ENABLED = 1
#    (Settings > Secrets and variables > Actions > Variables). Without it only dry runs work.
gh variable set FAST_RELEASE_ENABLED --body 1
# 4. The real release. One dispatch, nothing else by hand.
gh workflow run fast-release.yml --ref main
```

Optional inputs: `bump=patch` (2.880.0 -> 2.880.1 instead of 2.881.0), `version=2.890.0` (an exact version; must be new and newer than every
release), `allow_empty=true` (release although no new changelog entry exists). To switch the path off again: delete the variable. LTS
patches are not done here (`docs/release/release_channels.md`, "Cutting an LTS").

### What happens (and which safety rule each step keeps)

1. **Plan** (`fast-release.yml`, job `plan`, a minute or two):
   * Refuses unless it was started on `main` and the commit is main's head right now.
   * **CI gate (rule 6, no second CI run).** It asks GitHub for main's own `CI` run (the push run) for that exact commit. It refuses with a
     clear message if there is none, if it is still running or queued, if it finished red, cancelled or skipped, or if any job in it is not
     `success`/`skipped` (a job that was allowed to fail would still make the run "success"). So the commit is only released after the
     full CI, including the slow "Build & test (Ubuntu)" job, passed on main. **If CI is still running it waits** (every 30 s, at most
     90 min; a run that has not started yet gets 10 min) and stops at once, with the job names, when a job has already ended red or
     cancelled. Input `gate=release` (default `full`) waits only for the jobs that build and test what ships (`RELEASE_GATE_JOBS` in
     `scripts/fast_release.py`) and does not wait for the informational ones (Assimp, own HTTP, own stack, no-libsodium crypto, WebGL, SQL);
     a red or cancelled job anywhere still refuses. `full` is what rule 6 asks; use `release` only with the owner's say-so.
   * **Version (rule 12).** Releases are `2.<build>.<patch>`. The next version is counted from the newest published one, read from git tags,
     every GitHub release (drafts too) and `versions.json` on the update server. It refuses a version that already exists, one that is not
     newer than everything published, and one in the LTS line. A `CMakeLists.txt` version that somebody already raised above the newest
     release is used as it is. It also asks GitHub and git directly that `v<version>` is free.
   * **Notes.** The entries in `dist/changelog.d/` that the previous release did not have (below) become the changelog, the update card and
     the GitHub release text. No new entry = refused (players would see nothing), unless `allow_empty`.
   * Prints everything in the run summary and stores it as the artifact `release-notes`.
   * `dry_run=true` stops here.
2. **Release** (`release.yml`, called with the version): the existing release workflow builds the exact commit that was checked. Before the
   build, the version is written into the runner's copy of `CMakeLists.txt` (never committed); the build script then derives the binaries'
   version, `VERSION.txt` (twice), the zip name and the tag from it, so they cannot disagree. A check before publishing confirms that
   `VERSION.txt` and the programs really contain the version. The tag is made on the tested commit. Signing, checksums, the Supabase publish,
   the Linux server (built beside Windows, attached by the Windows job's last steps) and the "mark latest" step (the end of the same job, no separate runner) are release.yml's own: the release stays hidden (not
   "latest") unless `versions.json` has a SHA-256 for `OkaySpace.exe`, `OkayEngine.exe` and `OkaySpacePlayer.exe` (rule 13).
3. **Check the release** (job `after`): what used to be checked by hand. Fails loudly, with a table in the summary, unless: the GitHub
   release is published (not a draft) and its tag is on the tested commit; every asset is attached (three programs, `VERSION.txt`,
   `CHANGELOG.txt`, installer, Linux server); GitHub's "latest" is this release; `VERSION.txt` and `versions.json` on the update server say this
   version; `versions.json` has a SHA-256 for each program and, where GitHub shows an asset digest, it is the same hash as on GitHub (the same
   file in both places); and, when `OKAY_UPDATE_TRUSTED_KEYS` is set, `versions.json.sig` verifies. It is read-only: it changes nothing.

### Changelog entries: one small file per change

Every PR that players should hear about adds `dist/changelog.d/<id>.txt` (use the PR number or a short unique name):

```
section: Fixes and new things      (optional; the heading the bullets go under)
pr: 953                            (optional; shown in the ROADMAP row)
roadmap: One line for ROADMAP.md   (optional; the default is the first bullet)
---
- What changed, in plain words a player can read. Wrap at about 75
  characters; a second line of the same bullet starts with two spaces.
```

Rules: the text starts with `- `, no blank lines, no tabs, lines at most 100 characters. Check with
`python3 scripts/fast_release.py check-entries` (the ctest `fast_release_lint` runs it on every entry, so a bad entry fails CI). Two PRs never
touch the same file, so there are no conflicts. The same file name in two PRs is a conflict, which is the point: pick another id.

How "already released" is decided: no file is ever deleted by a release. An entry counts as released when it exists in the **previous
release's tag**; the new release uses the entries that are not in that tag. Consequences: never rename or delete an entry that was
released (write a new one to correct it), and entries that were in the tree of an old-style release (the by-hand path) count as released
by it, even though that release's changelog was written by hand.

The changelog text the players see is the previous release's published `CHANGELOG.txt` with the new version's section put on top, so the
history is never lost even though the repository's own `dist/CHANGELOG.txt` is not touched by a fast release.

### Is `CMakeLists.txt` kept in step? (it is not written by the bot, on purpose)

The workflow never commits to `main` (main's rules and the CI gate for the next release are not put at risk by a bot commit that CI never
checked). So the repository's `CMakeLists.txt` VERSION, `ROADMAP.md` and `dist/CHANGELOG.txt` fall behind the released version. This is
harmless: every release counts from the newest *published* version, and the binaries, `VERSION.txt`, `versions.json` and the tag all agree
because they are stamped at build time. When you want the repository to catch up, do one small docs-only PR:

```bash
gh run download <fast release run id> -n release-notes -D /tmp/rn
python3 scripts/fast_release.py sync-docs --version 2.881.0 --notes-dir /tmp/rn
git checkout -b docs/catch-up-2.881.0 && git add -A && git commit -m "Catch up: v2.881.0 changelog, roadmap, version" && git push
```

`sync-docs` sets the `CMakeLists.txt` VERSION to the released one, puts the ROADMAP row (newest first) and the changelog section in, and
moves that release's entries to `dist/changelog.d/released/<version>/`. Nothing depends on this PR being merged. A tag from a by-hand
release made later from a stale `CMakeLists.txt` is refused by rule 12 (it exists), so bump it first if you go back to the old path.

### When it goes wrong

* **Refused in `plan`:** nothing was built. Read the red message, fix, dispatch again. Nothing to undo.
* **The build or publish failed:** the release is not "latest" (release.yml makes it latest only after the checksums are published) and the
  `after` job says so. **Do not re-run the same version** (rule 12: it changes the checksums). Fix the cause on main, wait for CI, dispatch a
  new fast release: it takes the next number. If only the Supabase step failed, finish it with `scripts/publish-supabase.sh`
  (docs/release/releasing.md) and `gh release edit v<version> --latest`.
* **The `after` check failed after the release was made latest:** do not re-run. To hide it from launchers: `gh release edit v<version>
  --prerelease` (GitHub), and put the previous release back as latest with `gh release edit v<previous> --latest --prerelease=false`. On the
  update server, "Make live" for an older version is the launcher's Admin tool for the official game only; for the engine, publish the
  previous version again with `scripts/publish-supabase.sh` from its release artifacts (never by rebuilding). Then fix and release forward:
  launchers only move forward, so a fix is a new, higher version.
* **Two releases at once:** `fast-release.yml` queues (one at a time) and, once the concurrency change in PR #953 is in, so does
  `release.yml`. Overlapping publishes are the separate "release-parallel" work (switch off).

### What was not verified

GitHub Actions cannot run in the session that wrote this, so these are unverified until the dry run and a first real release have been
seen: that `workflow_call` of `release.yml` with `secrets: inherit` accepts the `release-notes` artifact and the secrets/variables as
written; that `body_path` together with `generate_release_notes: true` gives the notes you expect (GitHub may append its own generated notes
below ours); that GitHub's release API returns `digest` for the assets (if not, the "same hash on GitHub" row is simply not shown); the
exact output of `gh api --paginate --jq` for the jobs list. Everything that decides something (version, CI gate parsing, entries,
stamp, the after-check) is tested with fake data in `tests/scripts/test_fast_release.py` (ctest `fast_release_lint`), and every shell block
passes `bash -n`.

### Written from

Our own design for this repository. Standard library Python, `gh`, `curl`. No code from other projects.
