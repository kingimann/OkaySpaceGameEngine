---
id: fast1
row: | fast1 | Fast release: one workflow dispatch, no version-bump PR | GitHub: Actions > "Fast release" > Run workflow (tick dry_run first); a change adds `dist/changelog.d/<id>.txt` | Built and tested offline with fake data; never run on GitHub; off until `FAST_RELEASE_ENABLED=1` |
---
## fast1. Fast release: one dispatch, no version-bump PR

**What it is.** A release without the version-bump PR and its extra hour of CI. One click on "Fast release" works out the next version,
refuses a commit that has no fully green CI run on main, builds exactly that commit, publishes, and then checks the release itself.
The release notes come from small files, one per change (`dist/changelog.d/<id>.txt`), so they never clash. The old way (a bump PR,
then "Release Windows build") still works. Everything: `docs/fast_release.md`.

**Steps (the dry run, safe: builds and publishes nothing).**
1. On GitHub open Actions > **Fast release** > Run workflow. Branch `main`. Tick **dry_run**. Run.
   Or in a terminal: `gh workflow run fast-release.yml --ref main -f dry_run=true`.
2. Open the run and read its summary page.

**What you should see.** A table with the new version (for example "v2.881.0, next build after 2.880.0"), the commit, the CI gate
("passed: run ..., N jobs") and how many new changelog entries there are, then the ROADMAP row, the update card lines and the top of the
new changelog. If main's newest commit has no finished green CI run, the run goes red with "Refused: ..." and says what is wrong
(missing, still running, red). That is the safety check working, not a failure of the tool.

**Steps (the first real release through it).**
1. Set the repository variable `FAST_RELEASE_ENABLED` to `1` (Settings > Secrets and variables > Actions > Variables).
2. Actions > Fast release > Run workflow, branch `main`, dry_run off.
3. Wait for the three jobs: Plan, Build and publish (release.yml), Check the release. The last one prints a table; every row must say "ok".

**What you should see.** A new GitHub release `v<version>` that is "latest", the launcher offering that version, and in the run summary the
check table (release published, tag on the tested commit, three programs with a SHA-256 in versions.json that equals the one on GitHub).

**To add a changelog entry (every PR).** Copy `dist/changelog.d/fast-release.txt`, change the text, run
`python3 scripts/fast_release.py check-entries`.

**Status.** Tested offline only: `python3 tests/scripts/test_fast_release.py` (ctest `fast_release_lint`) with fake GitHub data and a
throw-away git repo; the workflows parse as YAML and every shell block passes `bash -n`. Not run on GitHub Actions. Unverified there:
calling `release.yml` as a reusable workflow with `secrets: inherit`, `body_path` together with `generate_release_notes`, and GitHub's
asset `digest` field. The first dry run and the first real release are the proof.
