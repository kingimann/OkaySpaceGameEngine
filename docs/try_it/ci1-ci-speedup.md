---
id: ci1
group: appendix
row: | ci1 | CI and release speed-up: how to tell it worked | GitHub Actions (not in the Windows release) | Unverified until a real CI run |
---
## ci1. CI and release speed-up: how to tell it worked

**This change has nothing to click in the editor, launcher or player.** It only changes how GitHub builds and tests
(`.github/workflows/ci.yml`, `.github/workflows/release.yml`, the end of `tests/CMakeLists.txt`). The way to "try" it is
to read the Actions page. **The speed-up is UNVERIFIED until a real CI run has happened.** The first run after this
merges is cold on purpose: the cache is only saved by pushes to `main`, so a PR run restores whatever `main` last saved.

**Steps.**
1. Open the PR's Actions run and look at the job "Build & test (Ubuntu)". Compare its total time with the baseline of
   about 62 minutes (run 43950229920: 10 min waiting for a runner, 53.9 min build, 6.6 min test).
2. (There is no linker step to check: lld was tried and removed, see `docs/automated_testing.md`.)
3. Open "Restore compiler cache". `Cache restored from key: ccache-ubuntu-ci-...` means a cache was found. "Cache not found"
   means the build was cold.
4. Open "Compiler cache statistics" (at the end of the job). Look at the hit rate (`Hits` against `Misses`). Warm is above
   90 percent; the first run after a change is expected to be lower.
5. Open "Test". The `ctest` line must show `-j4 --timeout 300` and the summary must say 100 percent passed, with the
   same count (about 805 tests) as before. No test is skipped or loosened.
6. After the merge, open the push-to-main run. Every job that builds C++ now has a last step "Save compiler cache (main
   only)"; it runs only there. On a PR run that step shows as skipped.
7. Repository Settings > Actions > Caches: the `ccache-*` entries should be one recent entry per job, not a long list of
   old sha keys. Total under 10 GB.
8. Release (`release.yml`): the first step of the "Cross-compile Windows exes" job is "Refuse a version that is already
   released". On a version that already has a GitHub release it must fail in seconds with "already released". Two
   release runs started together must queue (the second shows "Waiting"), not run at the same time. The "Make the release
   visible (latest)" job runs only if the Linux server job succeeded.

**What you should see.** A faster "Build & test (Ubuntu)" (target: well under an hour; the real number is unknown until
measured) and the same tests passing. All other jobs still run on every PR and on main, with the same names.

**Status.** Unverified: no GitHub Actions run was possible where this was written. The YAML was parsed with PyYAML, the
CMake test set was compared (805 tests with and without the launcher and sandbox), and `tests/scripts/test_dev_workflow.py`
passes. Nothing was run on GitHub or Windows. See `docs/automated_testing.md` "CI speed" for the details and what was left
out.
