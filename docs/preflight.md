# Preflight: run it before every push

```
bash scripts/preflight.sh            # about 1 minute, no build
bash scripts/preflight.sh --no-fetch # do not fetch origin/main first
```

It must print `PREFLIGHT OK`. It is **not** the full CI (it builds and runs no tests). It finds the mistakes that used to
cost a 25 to 90 minute CI run. The CI "Quick checks (no build)" job runs the same checks first, so a mistake you push
anyway fails in a few minutes instead of an hour (`docs/ci_failures.md` has the numbers).

| Step | What it catches | Example from the failure log |
|---|---|---|
| Merge conflicts with `origin/main` | A PR that conflicts gets **no CI at all** on GitHub. Uses `git merge-tree`, touches nothing. | generated docs, `docs/try_it.md` |
| `branch_lint.py includes` | `<mutex>`, `<thread>`, `std::call_once` in Windows code (rule 21a) | `std_mutex.h: '__gthread_cond_t' does not name a type` |
| `branch_lint.py secrets`, `binaries` | keys in the diff, built programs or big binaries in git | |
| `lint_test_registration.py` | one test program registered under two names; the same `/tmp/...` file in two tests; a NEW fixed `/tmp/...` path in a test | `guided_roundtrip` / `guided_quality` race |
| `gen_try_it.py --check-entries`, `gen_feature_map.py --check` | broken try-it entries, stale feature map | |
| `branch_lint.py mingw --strict` | the MinGW compiler (`-fsyntax-only`) on every changed `.hpp`/`.cpp` under `engine/ shared/ okayui/ launcher/ editor/ player/`. Headers are compiled alone, with `<intrin.h>` before and with `<intrin.h>` after. | `Sha256.hpp:193: too many arguments to function 'void __cpuid(int*, int)'` |
| `branch_lint.py tests` | the host compiler `-fsyntax-only` on each changed `tests/*.cpp` | |

Files that include SDL, Dear ImGui, OpenGL, X11, sodium, curl or OpenSSL headers are skipped by the MinGW step (those
SDKs are not installed in the check), and it says which. The full Windows build in CI still compiles them.

## Install MinGW once

```
sudo apt-get install -y g++-mingw-w64-x86-64-win32 mingw-w64-x86-64-dev      # Debian and Ubuntu
```

Without it the step is skipped and says so. (Set `MINGW_CXX` to use another compiler.)

## Conflict helpers

`.gitattributes` makes a local `git merge origin/main` friendlier. `preflight.sh` sets up the `regen` driver for you
(`git config merge.regen.driver true`).

* `scripts/dist_example_dirs.txt` (the example folders the Windows release ships; one per line) is `merge=union`: two PRs that
  each add a line merge without a conflict. It replaced the one very long `EXAMPLE_DIRS="..."` line in
  `scripts/build-windows-dist.sh`, which conflicted on nearly every PR.
* `docs/FEATURE_MAP.md`, `docs/feature_map/*.md` and `docs/try_it.md` are generated. On a conflict a local merge keeps
  **your** copy and reports no conflict. After the merge run `python3 scripts/gen_feature_map.py && python3 scripts/gen_try_it.py`.
  GitHub's own "Update branch" button and its merge button do not use custom drivers, so merge `origin/main` locally.

## How to prove the check works (what was run when it was written)

```
git show a8939f9c^:engine/include/okay/Crypto/Sha256.hpp > engine/include/okay/Crypto/Sha256.hpp   # the broken version
git commit -am demo && bash scripts/preflight.sh --no-fetch     # FAILED: MinGW ... __cpuid ... Sha256.hpp:193
git reset --hard HEAD~1
```
