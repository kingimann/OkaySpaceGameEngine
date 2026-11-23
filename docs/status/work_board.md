# Work board

> **What this is:** a hand-written snapshot of what the main session and its agents are doing, the open PRs, the releases and what waits on the owner.
>
> **Status:** Internal record (audit, plan or status): read it as a dated record, newest facts in the code.
>
> **Last checked:** 2026-11-20 (moved into `status/`, paths and links checked; the text itself was not re-checked against the code).

**Live version (rewritten every 10 minutes from GitHub, open PRs and CI state, branches, the #422 thread):** [WORK_BOARD.md on the work-board branch](https://github.com/kingimann/OkaySpaceGameEngine/blob/work-board/WORK_BOARD.md) (written by `scripts/work_board.py` and the workflow `.github/workflows/work-board.yml`). This page is the hand-written part: agents, owner decisions.

A snapshot of what the main session and its agents are doing. It does not update by itself: the main session rewrites
it as work moves. Snapshot time: **2026-11-20, about 02:40 UTC**. Anything here can be out of date by the time you read
it; the pull request list is the source of truth.

Rules that govern it (`CLAUDE.md`): always be working on something real (rule 2); a PR merges only when every CI job is
finished and green, "Build & test (Ubuntu)" and "Own stack" included (rule 6); nothing that replaces third-party code
is the default until the owner approves it (rule 17). Rule 24c (PR #1035) records the owner's C# decision.

## Owner decisions this session

- Visual Script is the main way to script and must be easy for beginners; text scripting is for people who already
  know it (owner, 2026-11-19).
- Our own OkayScript languages are to be replaced by C# and Lua. Nothing is deleted until both work and the owner
  confirms; the plan is `docs/scripting_migration.md`.
- C# runs on the Microsoft .NET runtime loaded at run time (rule 24c, PR #1035), off by default behind
  `OKAY_WITH_CSHARP`. Lua is written by us from the manual.
- Release often: ship what is green now, one small release at a time.

## Releases out

| Version | What | Checked |
|---|---|---|
| 2.890.0 | Boat and water, Visual Script blocks that explain themselves, Xbox editor freeze fix | tag, 10 assets, checksums, Supabase |
| 2.891.0 | Wind, Build Settings rebuilt, launcher and Project Upgrade fixes | same |
| 2.892.0 | Physics materials and toys, lighting, realistic water | same |
| 2.893.0 | Cameras (virtual-camera rigs, collision, blends, shake, 2D rooms), in-game UI quality setting | version bump is PR #1038; dispatch `release.yml` once after it merges, then verify |

## Open pull requests (merge only when every job is green)

| PR | What | Next |
|---|---|---|
| #1038 | Release 2.893.0 version bump | Merge when green, dispatch the release once, verify |
| #1031 | Vehicles: open-world driving model, presets, gamepad, Driving World | Real failure: `templates_work` Driving World save-reload-save differs (wheelBase/trackWidth); agent fixing |
| #1039 | Visual Script: drag the empty background to pan | Waiting for CI; never seen in a window |
| #1040 | Ctrl+S saves the focused thing or everything that changed, with a message | Waiting for CI; never seen in a window |
| #1041 | Modeling side bar follows the UI scale, clearer empty and greyed states | Waiting for CI; never seen in a window |

## Finished branches waiting for a PR (all pushed)

| Branch | What | Note |
|---|---|---|
| `claude/add-component-menu-2` | Add Component menu: 10 categories, rules-based, search with plain words, suggestions, recent and favourites | Editor not run |
| `claude/camera-3` | Camera zones and six presets | Mergeable now that #1023 is in |
| `claude/colliders-1` | Real collider shapes (cylinder, cone, hull, compound, plane, heightfield, 2D composite) and fixes for rotated capsules and solid 2D polygons | Editor not run |
| `claude/vs-easy-1` | Visual Script for beginners: explain this graph, "Why isn't it working?", goal cards | Editor not run |
| `claude/core-perf-1`, `claude/core-assets-1`, `claude/core-jobs-1` | Work-stealing jobs, allocators, trace capture; asset budget; jobs in the Profiler, parallel hashing and LOD | One PR; core-jobs-1 never built |
| `claude/particles-2`, `-3` | Particle rewrite, presets, true additive glow, 2D collision, start curves | Merge main first |
| `claude/time-1`, `claude/ecs-1` | Time upgrade; safe handles, run order, Scene Stats | Both touch Scene.hpp/.cpp; delete their new guide pages (rule 5) |
| `claude/project-upgrade-save-2` | Safer Save As, recovery restore, newer-scene warning | |
| `claude/ui-designer-1` to `-5` | UI designer: rulers, guides, layers, styles, constraints, find and replace | Built on each other; one PR; editor never run |
| `claude/inspector-collapse-1` | Component folds stay as you left them | Editor not run |
| `claude/script-profiler-1` | Per-script profiling table | Unbuilt |
| `claude/scripting-migration-1`, `claude/csharp-host-1` | Our own Lua 5.4 and migration plan; hosted C# (off by default, rule 24c) | Lua first; C# builds on it; never run with a real .NET |
| `claude/okay-build-2`, `claude/own-third-party-1`, `claude/own-platform-6` | Build tool, OGRE skeleton reader, no-SDL launcher and editor | Replacements stay off by default |
| `claude/okayms-3-r23b` | Our own MSVC-style compiler (compile-only: Wine broke) | Do not merge until the full tests run under Wine |

## Own replacements: how far

All are off by default. These are estimates, not measurements, except where a count is given.

| Replacement | State |
|---|---|
| CMake → okay-build | Reads 919 of 924 targets and 938 of 938 tests; built and ran one small real target (platform library plus its test). MinGW path, `install`, `add_custom_target` not done |
| SDL2 → okay::platform | Launcher and editor link with no SDL on Linux (0 SDL symbols); Windows side only syntax-checked |
| Dear ImGui → okayui | Launcher runs on OkayUI (merged, off by default); editor compiles against it, never run |
| MSVC → okay-ms | 82 of 348 engine files compile to objects (honest count) |
| Assimp → own readers | OGRE skeletons added; glTF 1.0, OpenGEX, MDL (Quake 1 / Half-Life 1) and Q3BSP brought onto branch `claude2/own-readers-last` (2026-11-22, off by default); SIB not brought (no public specification); 3D GameStudio MDL3/4/5/7 not written |
| OkayScript → Lua and C# | Lua 5.4 written (unbuilt in CI); hosted C# written (never run with a real .NET) |
| Crypto | libsodium stays (owner decision); own crypto off |

## Waiting on the owner

- Branch protection: the required-checks list for `main` is empty, so GitHub still allows red merges.
- Windows tests: update to the latest release (2.892.0, soon 2.893.0); the Xbox triangle and engine-mode build from the
  `claude/xbox-uwp-3` zip.
- Whether the OkayUI programs should ship in the release zip.
- A Windows machine with the .NET SDK to try C# (nothing C# has run).
- Final deletion of our own OkayScript, once Lua and C# are proven.

## Lessons from this session

- A stray file with a name Windows cannot check out broke the Windows checkout on a PR; the `winpaths` check stops this.
- `far` and `near` are Windows macros: never use them as variable names in code the MinGW build compiles.
- Test programs built from one source file must not share a temp folder: CTest runs them side by side.
- New script builtins need `docs/engine_api.md` regenerated (`OKAY_WRITE_API_DOCS=1 ./test_vs_types`); new component
  types must be added to the list in `tests/test_save_coverage.cpp`.
- No pictures in the repo except golden test references. Screenshots go to a temp folder and into chat.
- Several PRs that touch the same generated files conflict after every merge: combine them into one PR.
- Do not commit files the editor writes while running (`imgui.ini`, `okay_settings.txt`, `okay_recovery/`).

## Machine limits

About 20 GB free disk (stop at 5 GB). The machine is shared by many agents: the load average has been about 17 on
4 CPUs, and full engine builds are often killed for memory or take hours. Agents compile only what they touch and link
against a prebuilt library; CI is the first full build, and many branches above have never been built.

## Windows tests

`.github/workflows/windows-tests.yml` builds the whole unit-test suite with the release compiler (MinGW, cross-compiled on
Ubuntu) and runs it on a real Windows runner (`scripts/run_windows_tests.py`). It runs on pushes to main, nightly at 03:17 UTC
and by hand (input `ref`). It never runs on pull requests, so it blocks no merge or release; the owner decides later whether
to make it required. Results: the run's summary page and the `windows-tests-log` artifact.
