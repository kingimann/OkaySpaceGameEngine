# Release channels: latest and LTS

> **What this is:** OkaySpace ships on two channels.
>
> **Status:** Reference page for this feature. Kept as written; not extended (rule 5).
>
> **Last checked:** 2026-11-20 (moved into `release/`, paths and links checked; the text itself was not re-checked against the code).

OkaySpace ships on two channels.

| Channel | What it is | Who it is for |
|---|---|---|
| **Latest** | Every normal release (`2.855.0`, `2.856.0`, ...). New features and fixes. | Everyone who wants the newest editor. |
| **LTS** | One chosen version line that only receives fixes, as patch releases (`2.850.1`, `2.850.2`, ...). | People who want a project to stay on a stable editor. |

(A **test build** is a third, separate thing: one branch built for the owner to try, published to a `dev/` folder and offered only to a launcher with
"Get test builds" switched on. It never changes these two channels: see [Test builds](#test-builds-for-the-owner-to-try-a-branch-not-a-channel-for-players).)

Releases are numbered `2.<build>.<patch>`. Every normal release has patch `0` and a new build number. The first two
numbers (`2.850`) are the **line**. An LTS line is cut from one normal release and keeps its build number; only the patch
number moves. A **patch never changes a file format** and never adds a feature, so two patches of one line always open each
other's projects without asking (the editor's project upgrade question only appears between different lines; see
`docs/editor/project_upgrade.md` when that page exists).

## A release becomes "latest" only after its checksums are published

The in-app updater refuses any file that has no SHA-256 in `versions.json` (on the Supabase release server). If a release
became GitHub's "latest" before that list had it, launchers saw the new version and then failed with a "no checksum" error
(this happened with 2.866.0: the CI step that publishes to Supabase is skipped when the repository has no
`SUPABASE_SERVICE_KEY` secret). So the release workflow now creates the GitHub release as **not latest**, publishes to
Supabase, checks that `versions.json` really lists the version with a checksum for each program, and only then marks it
latest (job "Make the release visible (latest)"). Without the secret that job fails with a message instead of letting the
release out unchecked: publish with `scripts/publish-supabase.sh` (or `scripts/release-local.sh`), then run
`gh release edit v<version> --latest`. `scripts/release-local.sh` follows the same order.

## Where the files live

| Place | What | Kept |
|---|---|---|
| GitHub Releases | Every version's `OkayEngine.exe`, `OkaySpacePlayer.exe`, `OkaySpace.exe`, installer and zip. | Forever. The long-term archive. |
| Update bucket (Supabase, `releases`) | `v<version>/` folders for fast updates and "stay on LTS". | The newest `KEEP` (3) latest versions, plus **every version of the LTS line**. |

Files at the root of the bucket:

| File | Written by | Meaning |
|---|---|---|
| `VERSION.txt` | latest publish | The newest version. Uploaded last, so nobody sees a version before its files. |
| `NOTES.txt` | latest publish | "What's new" lines for the launcher's card. |
| `LTS.txt` | lts publish | The newest patch of the LTS line. Absent until a line is promoted. |
| `NOTES-LTS.txt` | lts publish | Notes for the LTS patch. |
| `versions.json` | every publish | The index below. A failure to update it only warns. |
| `versions.json.sig` | every publish made with a signing key | The ed25519 signature of `versions.json` (see [Signed releases](#signed-releases)). |

An LTS patch never writes `VERSION.txt` or `NOTES.txt`, never becomes GitHub's "latest release" (the in-app updater
downloads `releases/latest/download/<name>`), and never prunes anything. The `dev/` folder (test builds, below) is not part of this list: no
release publish reads or writes it.

## versions.json (what the launcher reads)

```json
{
  "schema": 1,
  "latest": "2.856.0",
  "lts": "2.850.1",
  "archive": "https://github.com/OWNER/REPO/releases/download/v{version}/{file}",
  "bucketPath": "v{version}/{file}",
  "versions": [
    { "version": "2.856.0", "channel": "latest", "date": "2026-11-10", "inBucket": true,
      "files": { "OkayEngine.exe": { "size": 40395912, "sha256": "..." },
                 "OkaySpacePlayer.exe": { "size": 33465602, "sha256": "..." },
                 "OkaySpace.exe": { "size": 8196266, "sha256": "..." } } }
  ]
}
```

- `versions` is sorted newest first. A version stays in the list after it leaves the bucket (`inBucket: false`); it is then
  only on GitHub, at the `archive` address, with the same file names.
- To install a version: use `<bucket>/<bucketPath>` when `inBucket` is true, otherwise the `archive` address; download to a
  temporary file, compare size and SHA-256 with the index, and only then move it into place. Never run an unchecked file.
- `channel` is the channel a version was published on. `latest` and `lts` at the top are the two channel heads.
- Versions published before this index existed are not listed; they are only on GitHub.
- The index is maintained by `scripts/versions_index.py` (self-test: `python3 scripts/versions_index.py --self-test`, also
  run by ctest as `versions_index`).

## What the launcher does with it

The launcher's **Versions** page ([launcher_versions.md](launcher_versions.md)) reads `versions.json` and installs a version next to the
running one: `<bucket>/<bucketPath>` when `inBucket`, then the `archive` address with the same file names; the size and SHA-256 of every file
must match before it is used. It also checks the launcher's own updates against the list when the version is in it. Without a usable index it
falls back to the heads in `VERSION.txt` / `LTS.txt` (no checksums, so it refuses unless the size matches and the person confirms).
Choosing the **LTS** channel makes the in-place updater follow `LTS.txt`: only patches of the LTS line, never a downgrade of the launcher (an
LTS editor is installed next to a newer launcher instead), and nothing at all when no LTS is published or reachable.

What the launcher still wants from the release tooling:

* **`SDL2.dll` in `files`** (and in the bucket at `v<version>/SDL2.dll`). A side-by-side copy needs it next to the editor; the GitHub release
  has it, the bucket and the index do not. Until it is published the launcher copies its own `SDL2.dll` (same SDL 2.x) and cannot check it.
* **The index before the heads.** `versions.json` should be uploaded before `VERSION.txt` / `LTS.txt` (they are the "this version is
  complete" signal). The launcher copes with an index that lags by adding the newer head without checksums, but then the version cannot be
  checked.
* Keep the `archive` GitHub address stable and every asset (`OkayEngine.exe`, `OkaySpacePlayer.exe`, `OkaySpace.exe`, `SDL2.dll`) on
  every release forever: versions that left the bucket come from there.

## Cutting an LTS

1. **Choose the line.** Pick a normal release that has been in use for a while and contains the project version stamp and the
   upgrade question. Its tag (for example `v2.850.0`) must exist.
2. **Branch.** `git switch -c lts/2.850 v2.850.0 && git push -u origin lts/2.850`. The branch needs the release workflow
   (`.github/workflows/release.yml`) from after release channels were added; if the line is older, cherry-pick that commit first.
3. **Promote the line.** Publish the line's current version to the LTS channel so `LTS.txt` points at it: download the version's
   assets from its GitHub release into a `Tools/` layout and run
   `CHANNEL=lts DIST_DIR=<folder> scripts/publish-supabase.sh` (the version file in `<folder>` must say `2.850.0`).
4. **Fix.** Fix the bug on `main` first. Then cherry-pick it onto the LTS branch with `git cherry-pick -x`, raise `VERSION` in
   `CMakeLists.txt` to the next patch (`2.850.1`), add a `dist/CHANGELOG.txt` entry, and open a pull request into `lts/2.850`
   (CI runs on pull requests to any branch).
5. **Release.** After the pull request is merged and green, run the "Release Windows build" workflow on `lts/2.850` with
   `channel = lts`. It tags `v2.850.1`, attaches the assets, does not mark the release as "latest", and (when the repository
   has the Supabase key) publishes with `CHANNEL=lts`. Otherwise run `CHANNEL=lts scripts/publish-supabase.sh` by hand.
6. **Check.** `LTS.txt` shows the new patch, `VERSION.txt` is unchanged, GitHub's "latest release" is still the newest normal
   release, `versions.json` lists the patch as `lts`.

What an LTS accepts: crashes, data loss, security problems, and anything that stops a project of that line from opening or
saving. Nothing else. One LTS line is supported at a time; promoting a new line makes the old one an ordinary line that is
pruned like any other (its versions stay on GitHub).

## Test builds (for the owner to try a branch; not a channel for players)

A **test build** is a build of one branch (or tag, or commit) that the owner can try in the launcher in about **15 to 30 minutes**, without a pull
request, without a GitHub Release and without a normal release. It exists so the owner can test unmerged work faster ("send releases directly through
Supabase without waiting for GitHub"). It is **not** a third channel: nobody gets one unless they switch it on, and it never touches "latest" or LTS.

**Who starts one.** The coordinator, when the owner asks ("ask the coordinator to start a test build of walk-vs-run"). Anyone who can run workflows can:

```bash
gh workflow run dev-build.yml --ref main -f ref=claude/walk-vs-run -f note="try the new run speed"
```

or Actions > **Test build (dev channel)** > Run workflow > *Use workflow from: main*, *ref*: the branch, *note*: one line. It must be started from
`main` (the reviewed copy of the workflow; a run started from another branch stops at once and says so). The workflow file has to be on `main` before it
can be started at all (GitHub only starts workflows from the default branch). One test build at a time publishes; a second waits.

**What happens.** (1) The build job checks out the ref, numbers it, and builds the Windows launcher, editor and player exactly like the release job.
(2) The publish job uploads them to the `dev/` folder of the release bucket and writes the list. (3) The run page says in plain words which version, which
branch and what the owner does next. There is no GitHub Release, no tag, no installer, no Linux server, no zip and no Windows code signing.

### The number

`<newest published release>.<run number>`, for example **2.876.0.17**: the 17th test build, made on top of release 2.876.0. A fourth number is used
because the version parser the launcher already has orders it correctly (a `-dev` suffix would be ignored and compare **equal** to the release):

```
2.876.0   <   2.876.0.17   <   2.876.0.18   <   2.876.1   <   2.877.0
```

A test build sorts just above the release it was made on (so a launcher on that release is offered it) and below every later release (so the next
real release always replaces it). The fourth number is never 0 (`2.876.0.0` would equal the release). The base is the release that is published
**when the build starts** (read from `VERSION.txt`, never written); if a newer release comes out while the build runs, the publish refuses ("start a
new test build"), because the number would be older than what people run and nobody would be offered it. A test build is numbered after the newest
release whatever branch it was made from (an `lts/` branch included). Shared rules: `shared/TestBuild.hpp`; test: `tests/test_dev_channel.cpp`.

### Where the files live

| Path (bucket `releases`) | Written by | What |
|---|---|---|
| `dev/v<version>/OkaySpace.exe`, `OkayEngine.exe`, `OkaySpacePlayer.exe` | a test build | The programs. Uploaded without overwrite: the storage service itself refuses to replace one. |
| `dev/versions.json` | a test build | The list of test builds with the size and SHA-256 of the three programs, and the branch, commit and note of each. The only file the launcher trusts. Same format as `versions.json`, no `latest`, `lts` or `archive`. |
| `dev/versions.json.sig` | a test build made with a signing key | The same ed25519 signature rule as the real list ([Signed releases](#signed-releases)). |
| `dev/NOTES-DEV.txt` | a test build | "Test build of walk-vs-run (abc1234)", the note, the base release: the launcher's "what's new" lines. |
| `dev/DEV.txt` | a test build, **last** | The newest test version (for people and scripts). |

Nothing else is written, ever: not `VERSION.txt`, `NOTES.txt`, `LTS.txt`, `versions.json` or a `v<version>/` folder at the root, and not the bucket
itself. The publisher is its own script, `scripts/publish-supabase-dev.sh` (index: `scripts/dev_index.py`); `scripts/publish-supabase.sh` has no
test-build code at all and refuses `CHANNEL=dev` and any version with four numbers (so a test build can never be published as a release by mistake).
The normal publisher's pruning only looks at `v<digits>` folders at the root, so it never sees `dev/`. Only the newest `DEV_KEEP` (6) test builds stay in
the bucket; older ones are deleted, stay in the list as "not in the bucket", and their numbers are never used again.

### What is the same and what is not

| | Normal release | Test build |
|---|---|---|
| Who gets it | everyone | only a launcher with **Get test builds** switched on |
| GitHub Release, tag, installer, zip, Linux server | yes | no |
| `VERSION.txt`, `versions.json`, "latest" | written | never touched |
| Checksums for every program, refused when missing | yes | yes (same rule, same code) |
| Signed list | when a key exists | when a key exists (same key) |
| Update flow (stage, verify, swap, self-check, rollback) | yes | the same flow |
| Windows code signing | when configured | never |
| Kept in the bucket | the newest 3 and the LTS line | the newest 6 |

**Two limits to know.** (1) A test build replaces only the three programs (launcher, editor, player), like any in-app update: example scenes, templates
files or assets that a branch adds to the zip or the installer are **not** in it, so an example that is meant to be tried from `examples\` is not there
until a real release. (2) Signing: with the repository secret `OKAY_UPDATE_SIGNING_SEED` set, the test list is signed like a release; without it the
list is published unsigned (a warning), which a launcher built with `OKAY_UPDATE_TRUSTED_KEYS` refuses. Nothing is signed today, so that is fine
now, but once launchers trust a key the seed has to be available to this workflow too, or test builds cannot be installed.

### In the launcher

**Settings > Behavior > Startup & updates > Get test builds** (off by default; saved as `test_builds=1` in `launcher.cfg`, and not written at all
while it is off). While it is off the launcher never reads the `dev/` folder: it behaves exactly as before. When it is on, the update check also reads
`dev/versions.json` (signature checked like the real list) and offers the **newer** of the newest release and the newest test build. A test build
shows up as "Test build of walk-vs-run (abc1234)" in the update toast, the sidebar card and the update window, and its version carries "(test
build)" in About, the Versions page and the editor's About and title bar. The long-term-support channel never gets test builds.

**Turning it off.** Switch **Get test builds** off: nothing new is offered. A test build stays installed until you go back: Settings > Behavior shows
"You are running a test build" with **Go back to the stable version**, which installs the newest normal release over it with the same updater (the one
deliberate downgrade; the launcher checks it is really that version after the restart and rolls back if not). When the next normal release comes out
it replaces a test build by itself.

### Safety rules, and what checks each

| Rule | Checked by |
|---|---|
| The normal reader ignores test versions (`ParseIndex`, `ParseVersionTxt`, `LoadIndex` never list or fetch one, never ask for `dev/`) | `dev_channel` |
| The test reader reads only `dev/`, keeps only well-formed test builds, needs the same signature | `dev_channel` |
| A test version never equals a release and sorts below every later one | `dev_channel` (thousands of random versions) |
| The setting is off by default and writes nothing when off | `dev_channel` |
| Going back is exact: the test build coming up again is not "confirmed" | `self_update` |
| The publisher writes only under `dev/`, in order, and the real files stay byte for byte | `dev_publish_script` (fake storage server) |
| A version that exists, leftovers, a damaged list, a stale base, a missing checksum, a bad branch or key are refused with nothing written | `dev_publish_script` |
| The key and the signing seed never appear in output, logs or objects | `dev_publish_script` |
| The real publisher refuses `CHANNEL=dev` and four-number versions | `dev_publish_script` |
| The workflow has no write permission, no release or tag, no secret in the build job, publishes with the reviewed scripts, and its build steps equal `release.yml`'s | `dev_workflow_lint` |

**Not verified:** the workflow has never run on GitHub, the publisher has never written to the real Supabase storage (only to a fake that mimics the
calls the release publisher already uses), and nothing in the launcher or the editor was run on Windows. See `docs/status/UNFINISHED.md`.

**Written from** (CLAUDE.md rule 19a): this repository's own release scripts and docs (`release.yml`, `publish-supabase.sh`, `versions_index.py`,
`sign_index.py`, `updater.md`) and the storage calls they already make. No code, tests or text from another project; no new library or action (every
workflow action is one `release.yml` or `ci.yml` already uses, at the same version).

## Signed releases

`versions.json` lists the SHA-256 of every file, so it is what an attacker who controls the bucket would change. Signing it
closes that: the publish script signs the exact bytes with a private key that only the publisher holds, and a launcher or editor
built with the matching **public** key refuses an index whose `versions.json.sig` is missing or does not verify. The channel
heads (`VERSION.txt`, `LTS.txt`) carry no trust of their own: a version can only be installed when it has checksums in the
signed index.

Nothing is signed until you create a key. The owner steps, the rotation and the transition are in
[updater.md, "Signed releases"](updater.md#signed-releases). In short:

```bash
python3 scripts/sign_index.py genkey      # prints OKAY_UPDATE_SIGNING_SEED=<private> and OKAY_UPDATE_TRUSTED_KEYS=<public>
export OKAY_UPDATE_SIGNING_SEED=<private> # in the shell that runs the publish, never in a file or the repository
scripts/publish-supabase.sh               # now also uploads versions.json.sig
# and build the launcher with OKAY_UPDATE_TRUSTED_KEYS=<public> (repository variable of that name in release.yml)
```

Publishing a channel (latest or LTS) signs the same index, so both channels share one key. `REQUIRE_SIGNED=1` makes the publish
script stop instead of uploading an unsigned index.

## Guard rails in the publish script

- `CHANNEL=latest` refuses a version that is in the LTS line.
- `CHANNEL=lts` refuses a patch older than the current LTS in the same line. The first publish of a different line promotes it
  (the script says so).
- Pruning never deletes a version of the current LTS line.
- `DRY_RUN=1` runs these checks without uploading.
- With `OKAY_UPDATE_SIGNING_SEED` set it signs `versions.json`, uploads `versions.json.sig`, reads both back from the bucket and
  checks the published pair before `VERSION.txt` goes out. Unset: a warning and an unsigned index (`REQUIRE_SIGNED=1` stops instead).
  With `bash -x` and a seed in the environment it refuses to run, because the trace would print the seed.

## What changed

Release channels were added in one change: `CHANNEL` in `scripts/publish-supabase.sh`, `LTS.txt` and `NOTES-LTS.txt`,
`versions.json` (`scripts/versions_index.py`), the `channel` input and `make_latest` in `.github/workflows/release.yml`.

The launcher side (Versions page, the channel setting, keeping the previous editor on update) came after: see
[launcher_versions.md](launcher_versions.md). No change to the feed's format was needed; `SDL2.dll` in `files` is the one wish above.
