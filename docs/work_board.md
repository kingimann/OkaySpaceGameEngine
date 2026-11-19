# Work board

**Live version (rewritten every 10 minutes from GitHub, open PRs and CI state, branches, the #422 thread):** [WORK_BOARD.md on the work-board branch](https://github.com/kingimann/OkaySpaceGameEngine/blob/work-board/WORK_BOARD.md) (written by `scripts/work_board.py` and the workflow `.github/workflows/work-board.yml`). This page is the hand-written part: agents, owner decisions.

A snapshot of what the main session and its agents are doing. It does not update by itself: the main session rewrites
it as work moves. Snapshot time: **2026-11-19, about 20:40 UTC**. Anything here can be out of date by the time you read
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
| 2.889.0 | Settings > Experimental switches, texture tools, Xbox export fixes, own platform layer event loops | tag, 10 assets, checksums, Supabase |
| 2.890.0 | Boat and water, Visual Script blocks that explain themselves, Xbox editor freeze fix | same |
| 2.891.0 | Wind, Build Settings rebuilt, launcher and Project Upgrade fixes | same |
| 2.892.0 | Physics materials and toys, lighting, realistic water | version bump is PR #1036; dispatch `release.yml` once after it merges, then verify |

## Open pull requests (merge only when every job is green)

| PR | What | Next |
|---|---|---|
| #1036 | Release 2.892.0 version bump | Merge when green, dispatch the release once, verify |
| #1023 | Camera: rigs, collision, blends, shake, 2D rooms | Ubuntu job re-run after a flaky-test race (see #1034) |
| #1031 | Vehicles: open-world driving model, 9 presets, gamepad, Driving World | MinGW fix pushed (`far` is a Windows macro); waiting for CI |
| #1032 | In-game UI quality setting (sharper edges, text, shadows, pictures) | Waiting for CI |
| #1033 | Remove editor runtime files committed by mistake | Waiting for CI |
| #1034 | Fix a flaky anti-cheat integrity test (shared temp folder) | Waiting for CI |
| #1035 | CLAUDE.md rule 24c: C# through .NET, Lua by us | Docs-only checks |

## Finished branches waiting for a PR (all pushed)

| Branch | What | Note |
|---|---|---|
| `claude/camera-3` | Camera zones and six presets | After #1023 |
| `claude/core-perf-1`, `claude/core-assets-1`, `claude/core-jobs-1` | Work-stealing jobs, allocators, trace capture; asset budget; jobs in the Profiler, parallel hashing and LOD (unbuilt) | Open as one PR so one CI run decides all |
| `claude/particles-2`, `-3` | Particle rewrite, presets, true additive glow, 2D collision, start curves | Merge main first (wind is in) |
| `claude/time-1`, `claude/ecs-1` | Time upgrade (exact clock, day-night, Time settings); safe handles, run order, Scene Stats | Both touch Scene.hpp/.cpp; delete their new guide pages (rule 5) |
| `claude/project-upgrade-save-2` | Safer Save As, recovery restore, newer-scene warning | |
| `claude/ui-designer-1` to `-5` | UI designer: model, rulers, guides, layers, styles, constraints, find and replace | Built on each other; editor never run |
| `claude/inspector-collapse-1` | Component folds stay as you left them, saved across restarts | Editor not run |
| `claude/script-profiler-1` | Per-script profiling table (any language) | Unbuilt |
| `claude/scripting-migration-1`, `claude/csharp-host-1` | Our own Lua 5.4, migration plan; hosted C# (off by default) | Lua first; C# builds on it; unbuilt |
| `claude/vs-easy-1` | Visual Script for beginners: start cards, explain this graph, checklist | In progress |
| `claude/colliders-1` | Real collider shapes (sphere, capsule, hull, mesh and more) | In progress |
| `claude/okay-build-2`, `claude/own-third-party-1`, `claude/own-platform-6` | Build tool (generated sources, configure_file, MD5), OGRE skeleton reader, no-SDL launcher and editor | Replacements stay off by default |
| `claude/okayms-3-r23b` | Our own MSVC-style compiler (compile-only: Wine broke in the container) | Do not merge until the full tests run under Wine |

## Own replacements: how far

All are off by default. These are estimates, not measurements, except where a count is given.

| Replacement | State |
|---|---|
| CMake → okay-build | Reads 919 of 924 targets and 938 of 938 tests; built and ran one small real target (platform library plus its test). MinGW path, `install`, `add_custom_target` not done |
| SDL2 → okay::platform | Launcher and editor link with no SDL on Linux (0 SDL symbols); Windows side only syntax-checked |
| Dear ImGui → okayui | Launcher runs on OkayUI (merged, off by default); editor compiles against it, never run |
| MSVC → okay-ms | 82 of 348 engine files compile to objects (honest count) |
| Assimp → own readers | OGRE skeletons added; glTF 1.0, Q3BSP, SIB, OpenGEX, MDL are on unmerged branches |
| OkayScript → Lua and C# | Lua 5.4 written (unbuilt in CI); hosted C# written (never run with a real .NET) |
| Crypto | libsodium stays (owner decision); own crypto off |

## Waiting on the owner

- Branch protection: the required-checks list for `main` is empty, so GitHub still allows red merges.
- Windows tests: update to the latest release (2.891.0, then 2.892.0); the Xbox triangle and engine-mode build from the
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
