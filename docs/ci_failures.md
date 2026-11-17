# Why PR checks fail, and what stops it

Evidence first, then what was changed, then what is still open. Everything under "Evidence" was read from the GitHub
API on 2026-11-17 (authenticated `gh api`, read only). The job logs were classified by searching each failed job's log
for `***Failed`, `error:`, `ld returned`, `Subprocess aborted` and similar lines, so the cause column is a good reading,
not a proof. "Unverified" is said where it applies.

## Evidence

Sample: the last 300 completed runs of the `CI` workflow on pull requests (2026-11-13 17:54 to 2026-11-17 03:20 UTC),
164 different branches, plus the latest 100 runs of every workflow for the PR-text check.

| Result | Runs | Wall-clock hours spent |
|---|---|---|
| success | 156 | 160 |
| failure | 56 | 74 |
| cancelled | 88 | 48 |

* A passing run takes a median of **62 minutes** (90th percentile 87). A failing run takes a median of **73 minutes**
  (90th percentile 120, longest 158): the typical failure is only reported after more than an hour.
* 88 of 300 runs (29 percent) were cancelled by a newer push to the same PR (`cancel-in-progress`, working as designed).
  Those runs used 48 hours of runner time and told nobody anything. Fewer, better-checked pushes cut this directly.
* 72 runs had at least one failed job (56 failed outright, 16 were cancelled with a failed job already in them).
* Failures per day: 11-13: 4, 11-14: **39**, 11-15: 0, 11-16: 12, 11-17: 1.

### Ranked causes (runs with at least one failed job of that cause)

| # | Runs | Cause | Examples | Where it was found | What stops it now |
|---|---|---|---|---|---|
| 1 | 37 | **A failure inherited from a broken state of main, on 2026-11-14.** The same red tests or link error appear in many unrelated PRs on one day: `fbx_reader` 20 runs, MinGW link error `libassimp.a undefined __imp__fstat32` 20 runs, `ui_renderer_golden` 19, `player` 11, `ui_render_bench` 6. (CLAUDE.md rule 6 says PRs #842 to #856 merged with `player` red.) | run 43552377440, 43642944202 | after the full build, 45 to 90 min | Rule 6 (no merge unless every job is green) and a quick red-main signal; see "Policy" |
| 2 | 19 | **One test red or aborting in an otherwise fine PR**: `stance_save` 2, `examples_work` 3, `model_readers` 3, `visualscript`, `vs_refs`, `player_course`, `project_create`, `player_deep`, `voxel_materials`, `collab_scripts`, `turn_interpolation`, `net_anticheat`, `guided_roundtrip`. Mixed: real bugs in the PR and tests that depend on timing or shared files. Not classified one by one (unverified which are flaky). | 43895737809 (`stance_save`), 44000641560 (`guided_roundtrip`) | in the Test step, 40 to 80 min | per-test temp folders; list of suspicious tests below |
| 3 | 13 | **Other or infrastructure**: WebGL job red without a log line (4), runner shutdown signal (1), `apt` failure exit 100 (1), cmake anticheat-keys error (1), ASan script test "expression nested too deeply" (4, a log line of a passing negative test; the real cause was another line), own-HTTP link error (1), editor self-test (1) | 43550239000, 43660220987 | various | retries only for runner shutdown (rule 7); WebGL cache and timeout already added |
| 4 | 8 | **Compile error that only one platform sees, found late**: MinGW `<cpuid.h>` vs `<intrin.h>` in `Sha256.hpp` (2 runs, last one 2026-11-17), MinGW `<mutex>` in Windows code (1, the rule 21a case), macOS `shared/AppRestart.hpp:694` (2 runs, 4 jobs), `SDL_HINT_WINDOWS_DPI_AWARENESS` and `g_statsUI` undeclared in a program `main.cpp` (3), MSVC `C2143` in a test (1) | 44001379830 (cpuid), 43725599125 (`<mutex>`) | **25 to 60 min** into the Windows or macOS job | `preflight.sh` and the CI quick-checks job: MinGW `-fsyntax-only` in seconds |
| 5 | 1 | **Shared temp folder race**: `guided_roundtrip` and `guided_quality` run the same program, both use `temp_directory_path()/okay_guided_roundtrip`, and `ctest -j4` runs them together. An earlier `Subprocess aborted ... terminate ... stof` under `-j4` (run 43978316384: `visualscript`, `examples_work`, `play_start_bench`, `vs_nodes`, ... aborting at once) is the same family: tests that share files or state when run in parallel. | 44000641560, 43978316384 | Test step | one `TMPDIR` per test, test-registration lint |
| 6 | 8 of 56 hygiene runs | **PR text check**: "the PR body has no '## How to try it' section (rule 4a)" (PR hygiene workflow, last 100 runs: 43 passed, 8 failed, 5 cancelled) | 44001379316, 44000967435 | within 4 min, cheap | already a quick check; fix by writing the section when the PR is opened |
| 7 | not in the sample | **Merge conflicts.** GitHub starts no pull-request CI for a PR that conflicts with main, so these show up as "no checks", not as red runs. Known hot spots: the one very long `EXAMPLE_DIRS=` line in `scripts/build-windows-dist.sh`, `docs/try_it.md`, `docs/FEATURE_MAP.md`, `docs/feature_map/*` | rule 8 | at merge time | see "Conflict-proofing" |
| 8 | not in the sample | **Cold compiler cache**: the 90 minute job timeout and the WebGL timeout (already fixed with a cache and a timeout). Not measurable from run conclusions alone. | | | unchanged |
| 9 | not in the sample | **Runner starvation**: about 15 PRs queued at once, each with 16 jobs (the sample shows 100 CI runs on 2026-11-14 alone). Not provable from the API (queue times were not collected). | | | see "Policy" |

Numbers 1 to 3 overlap in a few runs (a run is counted once, in the first matching row).

## What changed (this branch `claude/pr-hardening-1`)

| Item | Change | Check that it works |
|---|---|---|
| Per-test temp folders | `tests/CMakeLists.txt` sets `TMPDIR`, `TMP` and `TEMP` to `<build>/tests/test_tmp/<test name>` for every test (created at configure time) | `ctest --show-only=json-v1` lists a different `ENVIRONMENT` for `guided_quality` and `guided_roundtrip` (run when written) |
| Registration lint | `scripts/lint_test_registration.py`: fatal if one command is registered under two test names without a `# okay-lint: same-command-twice <name>: <why>` comment, if one `"/tmp/..."` literal is in two test sources, or (with `--base`) if a branch adds a new `"/tmp/..."` literal. Runs as ctest tests `lint_test_registration` and `lint_test_registration_self`, in `branch-lint.yml` and in the CI quick job. | `--self-test`; on the real tree it found exactly `guided_roundtrip`, now marked |
| Windows problems found early | `branch_lint.py mingw` now covers `shared/ okayui/ launcher/ editor/ player/` too, compiles headers alone and with `<intrin.h>` before and after, and has `--strict` and `--files` | reproduced the `Sha256.hpp` `__cpuid` error with the broken version from `a8939f9c^` |
| Preflight | `scripts/preflight.sh`, `docs/preflight.md`, line in `docs/agent_brief_template.md` | broken `Sha256.hpp` and an added `#include <mutex>` in `launcher/NetGuard.hpp` were both reported; a conflicting pair of scratch branches was reported |
| Conflict-proofing | the example folder list moved to `scripts/dist_example_dirs.txt` (one per line, `merge=union`); `tests/test_examples_work.cpp` reads it; `.gitattributes` `merge=regen` on generated docs | copied the example folders with the old line and the new file into two folders: `diff -r` empty (21 folders). A scratch merge of two branches that edited the same feature map line and each added a list line: no conflict, both list lines kept, my copy of the map kept |
| Fast first CI job | `quick-checks` in `ci.yml`; all 14 heavy jobs have `needs: quick-checks` | YAML parsed; the job's steps were run locally one by one; **not run on Actions** |

### Conflict-proofing, and the choice for generated docs

Options for `docs/FEATURE_MAP.md`, `docs/feature_map/*`, `docs/try_it.md`:

1. Stop committing them and generate in CI or at release. Cleanest, but rule 5 and the "owner must be able to read it in the repo"
   idea depend on them being there, and `docs/try_it.md` is already half-way (entries live in `docs/try_it/`, the page is
   regenerated by the coordinator).
2. Keep committing, mark `merge=regen` (keep our copy) and regenerate after every merge. Least risky: nothing changes
   for readers, and a wrong choice is fixed by one command. **This is what was implemented.**

Limit: a custom merge driver only works in a local `git merge`. GitHub's "Update branch" and its merge button ignore it,
so a PR can still show a conflict in these files on GitHub. The fix stays: merge `origin/main` locally (rule 8).
Option 1 is the real cure; it is a decision for the coordinator and the owner and was not done here.

## The quick-checks job (what it costs, what it can break)

* Steps: rule 21a includes, secrets, binaries, test registration, try-it entries, MinGW `-fsyntax-only` on changed
  Windows-reachable files (installs `g++-mingw-w64-x86-64-win32` only when such files changed), host `-fsyntax-only` on
  changed tests, feature map (warning only, because a map that is fine on the branch can be stale after main moved).
* Timing (estimates, **unmeasured on Actions**): checkout and Python checks about 30 seconds; the MinGW install about 1 to 2 minutes;
  compiling a few changed files seconds. No changed Windows file means no install. On a push to main nothing differs from
  `origin/main`, so it takes seconds.
* Heavy jobs wait for it. If it is red they do not start (they show "skipped"). **Make "Quick checks (no build)" a required
  status check** in the branch protection settings; a skipped required job counts as passed, so without this a red quick job
  would not block a merge on its own. (Setting not touched; owner or coordinator.)
* A false alarm in the quick job would block everything. The deterministic checks are fatal; the feature map is a warning.
  Before it was enabled the MinGW check was run over every `.hpp`/`.cpp` in scope: see "False alarms" below.

## Policy proposal for CLAUDE.md rule 9 (for the coordinator; this file does not edit CLAUDE.md)

> 9. **Keep the open PR count low and the queue short.** At most **4** open PRs with checks running at once; others wait
> as branches (a pushed branch is already covered by branch lint). Open a PR only after `bash scripts/preflight.sh` printed
> `PREFLIGHT OK`. Do not push again to a PR while its run is still going, unless the push fixes a red job (each push cancels
> the run and wastes the hours already spent). When main is red, merge nothing and open nothing until main is fixed, because
> every PR inherits the red job (2026-11-14: 37 runs). Merge a PR's `origin/main` into the branch locally, never with the
> "Update branch" button, so the generated-file helpers in `.gitattributes` apply.

A hard cap in GitHub Actions is not possible with `concurrency` alone: one shared group would keep one pending run and
cancel the others, which loses PRs' checks. The per-PR group in `ci.yml` (`ci-<PR number>`, cancel in progress) stays as it is.

## Suspicious tests (listed, not retried: rule 7 forbids blind retries)

Reading the test sources, not from failing evidence unless said.

* **Failed in the sample**: `player` (8 to 15 s, 11 runs on 11-14, in the main-cluster), `ui_render_bench` and `ui_renderer_golden` (11-14), `stance_save`
  (2 runs, 11-16), `examples_work` (3), `model_readers`, `fbx_reader`, `visualscript` and about six tests aborting together in run 43978316384.
* **Timing based** (a limit on elapsed time): `physics_deep_improved`, `loop_audit2`, `bughunt_round3`, `frame_pacing`, `platform`,
  `own_http_net`, `launcher_download_own`, `launcher_versions`, `native_fetch`, `playfab_own` (already `RUN_SERIAL`),
  `ui_render_bench`, `play_start_bench`, `guided_perf`, `player` (13 s). 88 test sources read `steady_clock`; 67 call `sleep_for`.
* **Thread based**: 32 test sources use `std::thread` or `std::async`. Tests may use threads (the Windows rule is for engine code).
* **Shared files**: 36 test sources use fixed `"/tmp/..."` literals (none shared between two files today, checked by the lint;
  a test using one twice in parallel with itself is not covered). Examples: `test_blend_shapes`, `test_gltf_import`,
  `test_datapack`, `test_instantiate`, `test_anim_ik_components`, `test_net_anticheat_*`. Moving them to
  `temp_directory_path()` is the safe fix; the lint now forbids adding new ones.
* **Fixed network port**: `test_dedicated_server.cpp` uses `47391` and `47002`; two copies on one machine would collide.
* Also registered twice: `guided_roundtrip` (marked; isolated now).

## What is unverified

* Nothing here ran on GitHub Actions. The new `quick-checks` job, `needs`, and its run time are untested there.
  Dry-run plan: push the branch, open a draft PR, look at the "Quick checks" job time and log; then push a branch that
  adds `#include <cpuid.h>` before `#include <immintrin.h>` in a header under `engine/include/okay/Crypto/` and confirm that
  the job goes red in minutes and the heavy jobs are skipped; then a branch with a conflict-free docs-only change must run
  every job as before. Also check the required-check setting.
* `merge=union` and `merge=regen` were tested with local `git merge` only.
* Whether tests are really independent with per-test `TMPDIR`: only checked by `ctest --show-only=json-v1` and the two guided tests
  (see the branch report for what was run). The full `ctest -j4` was not run here.
* The failure classification comes from log keywords; row 2 was not read test by test.
* Queue wait times (item 9) were not collected.

## False alarms

See the branch report: the MinGW check was run over every Windows-reachable file to find files it would flag wrongly.
