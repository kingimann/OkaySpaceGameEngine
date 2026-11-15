# Release channels: latest and LTS

OkaySpace ships on two channels.

| Channel | What it is | Who it is for |
|---|---|---|
| **Latest** | Every normal release (`2.855.0`, `2.856.0`, ...). New features and fixes. | Everyone who wants the newest editor. |
| **LTS** | One chosen version line that only receives fixes, as patch releases (`2.850.1`, `2.850.2`, ...). | People who want a project to stay on a stable editor. |

Releases are numbered `2.<build>.<patch>`. Every normal release has patch `0` and a new build number. The first two
numbers (`2.850`) are the **line**. An LTS line is cut from one normal release and keeps its build number; only the patch
number moves. A **patch never changes a file format** and never adds a feature, so two patches of one line always open each
other's projects without asking (the editor's project upgrade question only appears between different lines; see
`docs/project_upgrade.md` when that page exists).

## A release becomes "latest" only after its checksums are published

The in-app updater refuses any file that has no SHA-256 in `versions.json` (on the Supabase release server). If a release
became GitHub's "latest" before that list had it, launchers saw the new version and then failed with a "no checksum" error
(this happened with 2.866.0: the CI step that publishes to Supabase is skipped when the repository has no
`SUPABASE_SERVICE_KEY` secret). So the release workflow now creates the GitHub release as **not latest**, publishes to
Supabase, checks that `versions.json` really lists the version with a checksum for each program, and only then marks it
latest (job "Make the release visible (latest)"). Without the secret that job fails with a message instead of letting the
release out unchecked: publish with `scripts/publish-supabase.sh` (or `scripts/release-local.sh`), then run
`scripts/promote-latest.sh <version>` (the safe form of `gh release edit v<version> --latest`: it never takes "latest" away from
a newer release). `scripts/release-local.sh` follows the same order.

## Several releases at once

You can dispatch the release workflow again before the last release has finished. That is safe. Give each release its own
version number first (bump `VERSION` in `CMakeLists.txt`, as a release PR does), and never run the workflow again for a
version that already exists (see "One version, published once" below).

* **Builds run side by side.** Each run builds its own version on its own runner. Nothing makes one build wait for another.
  (The workflow has no workflow-level concurrency group on purpose.)
* **Publishing takes turns.** Publishing rewrites files that every version shares: `versions.json`, `VERSION.txt`,
  `NOTES.txt`, and it deletes old version folders. If two runs did that at the same moment, one version's checksums could be
  lost (that release would stay hidden with "versions.json has no checksums"), or an older version that finishes late could
  move "latest" backwards. So `scripts/publish-supabase.sh` first takes a **lock**: it creates a small file `release.lock` in
  the bucket. The storage service refuses to create a file that already exists, so only one run can hold it. A run that finds
  the lock held looks again every 10 to 20 seconds (up to 40 minutes locally, 30 in the workflow), then publishes. The lock
  is given back when the script ends, whatever way it ends (done, an error, the job cancelled).
* **"Latest" never goes backwards.** An older version that finishes after a newer one is still published, with its files
  and its checksums (so it can be installed from the Versions page), but it does not write `VERSION.txt` or `NOTES.txt`, and
  `latest` in `versions.json` stays at the newer version. The script says so in its log.
* **GitHub's "latest" release never goes backwards either.** The last job ("Make the release visible (latest)") runs
  `scripts/promote-latest.sh`. It marks the release latest only when no newer release is already latest on GitHub and no
  newer version is already published. These jobs also run one at a time.
* **What the script reads under the lock is fresh.** `VERSION.txt`, `LTS.txt` and `versions.json` are read through the
  authenticated storage API, not through the public address. The public address is served from a cache and can show an
  old copy for a while; a publish that believed it would drop the version published a moment ago.
* **The version being published is never pruned**, even when it is older than the newest three (an older release finishing
  late). A later publish removes it with the other old folders.

### One version, published once (CLAUDE.md rule 12)

Running the workflow again for an existing version rebuilds the programs, so their files and checksums change under people
who already have them. The publish script now refuses that: when `versions.json` already lists the version with **different**
files it stops with "already published with DIFFERENT files" before it uploads anything. The **same** files again are a safe
re-run (it says so). `ALLOW_REPUBLISH=1` replaces a version on purpose (`scripts/release-local.sh --force` sets it). This only
guards the Supabase side: the GitHub release of that version is written earlier in the same job, so do not re-run a version
there either.

### What you will see in the log

The "Publish to Supabase" step of a run that had to wait:

```
Taking the release lock (release.lock): releases are published one at a time
  release.lock is held by github-run-123456789-attempt-1 (v2.876.0, renewed 12 s ago); waiting for it, up to 1800 s more
  took the release lock (release.lock) for v2.875.0 after 94 s
v2.875.0 is OLDER than the version that is latest now (v2.876.0). Publishing its files and its checksums so it can be
installed, but VERSION.txt and NOTES.txt are left alone: latest does not move backwards.
  ...
  left VERSION.txt at v2.876.0
  gave the release lock back
Done: v2.875.0 is published and can be installed from the version list; v2.876.0 stays the latest (...)
```

and the last job of that run: `A newer release (v2.876.0) is already latest on GitHub: v2.875.0 stays a normal release (it is
not made latest).` A run that did not have to wait shows `took the release lock ... after 0 s` and `gave the release lock back`.

### If a lock is stuck

A lock is left behind only when a publish dies without a chance to clean up (the machine was lost). The next publish notices
that the lock has not been renewed for 20 minutes, takes it over and prints a loud warning (`TAKING IT OVER`; in GitHub Actions
it also shows as a warning on the run page). A publish that is slower than that keeps its lock alive by renewing it after each
upload, and it checks that the lock is still its own before it writes `versions.json` and `VERSION.txt`. (The lock is renewed
between uploads, not during one: on a connection so slow that a single file takes more than 20 minutes, set
`LOCK_STALE_SECONDS` higher.)

To clear a stuck lock yourself, delete `release.lock` in the bucket (Supabase dashboard > Storage > `releases` > `release.lock`
> Delete), or just wait 20 minutes. A lock whose time cannot be read, or lies in the future, is never taken over by the script:
delete it by hand. If a run says "gave up waiting for the release lock", nothing was published; run the publish again once the
lock is gone (`scripts/publish-supabase.sh`, with the same build, or `scripts/release-local.sh`).

### What is shared, and what protects it

| Shared thing | Written by | Protected by |
|---|---|---|
| `versions.json`, `versions.json.sig` | every publish | the lock, and a fresh read under it |
| `VERSION.txt`, `NOTES.txt` | latest publish | the lock, and the "never backwards" rule |
| `LTS.txt`, `NOTES-LTS.txt` | LTS publish | the same lock; the LTS rules are checked again under it |
| Pruning of old `v<version>/` folders | latest publish | the lock; the version being published is never pruned |
| `v<version>/` files | one publish per version | one version, one publish (rule 12, above) |
| GitHub's "latest" release | the last job, `release-local.sh` | `scripts/promote-latest.sh`, and the last jobs take turns |
| GitHub release assets of one version (zip, installer, exes, `SDL2.dll`, Linux server) | that run only | nothing is shared between versions; two runs of one version would overwrite each other (rule 12) |
| Actions caches | each run | keyed by commit or run: no clash |

Not covered because the release workflow does not run it: `scripts/publish-official-game.sh` (the official game under
`official/`, run by an admin) has its own newer-than-live check and no lock. The workflow has no Steam step.

### Trying it

Automatic: `python3 -I tests/scripts/test_publish_lock.py` runs two publishes at a time against a local fake of the storage
service (about two minutes; it also runs in CI, job "Ticket function, update signing and account server tests"). The fake is
not the real service, so the first real overlap is the real check.

Safest way to rehearse it for real, without touching the real `releases` bucket: publish two throwaway versions into a
throwaway bucket at the same time, from your own computer, in the repository folder (the key stays in your shell, never in a
file or in chat):

```bash
D=$(mktemp -d); mkdir -p "$D"/a/Tools "$D"/b/Tools
for pair in a:2.900.0 b:2.901.0; do
  dir=${pair%%:*}; ver=${pair##*:}; echo "$ver" > "$D/$dir/VERSION.txt"
  for f in OkaySpace.exe Tools/OkayEngine.exe Tools/OkaySpacePlayer.exe; do printf 'MZ %s %s' "$ver" "$f" > "$D/$dir/$f"; done
done
export SUPABASE_SERVICE_KEY=...        # your service key, typed into your own shell
BUCKET=zz-publish-test DIST_DIR="$D/a" scripts/publish-supabase.sh &
BUCKET=zz-publish-test DIST_DIR="$D/b" scripts/publish-supabase.sh
wait
```

You should see: one run says `took the release lock ... after 0 s`, the other `release.lock is held by local-pid-...; waiting
for it`, both end with `gave the release lock back`, and the bucket's `VERSION.txt` says `2.901.0` whichever one went first. If
the older one (2.900.0) happens to publish second it also says `is OLDER than the version that is latest now`. Then delete the
bucket `zz-publish-test` in the Supabase dashboard. For a real release: dispatch two releases with consecutive versions a minute apart and read the "Publish to
Supabase" step of each; the second prints the waiting lines only if its build finished while the first was publishing.

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
| `versions.json` | every publish | The index below. The publish stops (no `VERSION.txt`) if it cannot be written and read back. |
| `versions.json.sig` | every publish made with a signing key | The ed25519 signature of `versions.json` (see [Signed releases](#signed-releases)). |
| `release.lock` | every publish, while it runs | The lock (see [Several releases at once](#several-releases-at-once)). Present only while a publish runs; it names the version, the run and the time. |

An LTS patch never writes `VERSION.txt` or `NOTES.txt`, never becomes GitHub's "latest release" (the in-app updater
downloads `releases/latest/download/<name>`), and never prunes anything.

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
- Both are checked twice: at the start (so a wrong channel fails in seconds) and again under the lock, on what is published at
  that moment.
- Pruning never deletes a version of the current LTS line, or the version being published.
- A version older than the current latest is published but never moves `VERSION.txt` or `NOTES.txt`.
- A version already listed in `versions.json` with different files is refused (`ALLOW_REPUBLISH=1` overrides).
- Only one publish runs at a time (the lock `release.lock`; `LOCK_WAIT_SECONDS`, `LOCK_STALE_SECONDS`, `LOCK_POLL_SECONDS`,
  `LOCK_RENEW_SECONDS` tune it). See [Several releases at once](#several-releases-at-once).
- `DRY_RUN=1` runs these checks without uploading and without taking the lock.
- With `OKAY_UPDATE_SIGNING_SEED` set it signs `versions.json`, uploads `versions.json.sig`, reads both back from the bucket and
  checks the published pair before `VERSION.txt` goes out. Unset: a warning and an unsigned index (`REQUIRE_SIGNED=1` stops instead).
  With `bash -x` and a seed in the environment it refuses to run, because the trace would print the seed.

## What changed

Release channels were added in one change: `CHANNEL` in `scripts/publish-supabase.sh`, `LTS.txt` and `NOTES-LTS.txt`,
`versions.json` (`scripts/versions_index.py`), the `channel` input and `make_latest` in `.github/workflows/release.yml`.

Overlapping releases were made safe in a later change: the lock, the "never backwards" rule, fresh reads and the rule-12 check in
`scripts/publish-supabase.sh` (and `scripts/versions_index.py status`), `scripts/promote-latest.sh` with the one-at-a-time
`finalize` job in `.github/workflows/release.yml`, and `tests/scripts/test_publish_lock.py`.

The launcher side (Versions page, the channel setting, keeping the previous editor on update) came after: see
[launcher_versions.md](launcher_versions.md). No change to the feed's format was needed; `SDL2.dll` in `files` is the one wish above.
