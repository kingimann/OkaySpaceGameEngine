# Fast release: one dispatch, no version-bump PR

Status: **built and tested offline only, switched OFF.** It has never run on GitHub. The first real use is the dry run below, then one
real release. The old way (a "Release x.y.z" PR, then `release.yml` by hand) still works exactly as before.

The owner asked for faster releases, even small ones. Two things made a release slow: the version-bump PR with its own hour of CI, and
the changelog/ROADMAP edit that clashed between releases. This path removes both and keeps every safety rule.

## What you run

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

## What happens (and which safety rule each step keeps)

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

## Changelog entries: one small file per change

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

## Is `CMakeLists.txt` kept in step? (it is not written by the bot, on purpose)

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

## When it goes wrong

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

## What was not verified

GitHub Actions cannot run in the session that wrote this, so these are unverified until the dry run and a first real release have been
seen: that `workflow_call` of `release.yml` with `secrets: inherit` accepts the `release-notes` artifact and the secrets/variables as
written; that `body_path` together with `generate_release_notes: true` gives the notes you expect (GitHub may append its own generated notes
below ours); that GitHub's release API returns `digest` for the assets (if not, the "same hash on GitHub" row is simply not shown); the
exact output of `gh api --paginate --jq` for the jobs list. Everything that decides something (version, CI gate parsing, entries,
stamp, the after-check) is tested with fake data in `tests/scripts/test_fast_release.py` (ctest `fast_release_lint`), and every shell block
passes `bash -n`.

## Written from

Our own design for this repository. Standard library Python, `gh`, `curl`. No code from other projects.
