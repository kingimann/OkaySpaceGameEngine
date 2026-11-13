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

## Guard rails in the publish script

- `CHANNEL=latest` refuses a version that is in the LTS line.
- `CHANNEL=lts` refuses a patch older than the current LTS in the same line. The first publish of a different line promotes it
  (the script says so).
- Pruning never deletes a version of the current LTS line.
- `DRY_RUN=1` runs these checks without uploading.

## What changed

Release channels were added in one change: `CHANNEL` in `scripts/publish-supabase.sh`, `LTS.txt` and `NOTES-LTS.txt`,
`versions.json` (`scripts/versions_index.py`), the `channel` input and `make_latest` in `.github/workflows/release.yml`.

The launcher side (Versions page, the channel setting, keeping the previous editor on update) came after: see
[launcher_versions.md](launcher_versions.md). No change to the feed's format was needed; `SDL2.dll` in `files` is the one wish above.
