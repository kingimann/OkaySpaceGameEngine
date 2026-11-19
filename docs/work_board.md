# Work board

**Live version (rewritten every 10 minutes from GitHub, open PRs and CI state, branches, the #422 thread):** [WORK_BOARD.md on the work-board branch](https://github.com/kingimann/OkaySpaceGameEngine/blob/work-board/WORK_BOARD.md) (written by `scripts/work_board.py` and the workflow `.github/workflows/work-board.yml`). This page is the hand-written part: agents, owner decisions.

A snapshot of what the main session and its agents are doing. It does not update by itself: the main session rewrites
it as work moves. Snapshot time: **2026-11-19, about 05:00 UTC**. Anything here can be out of date by the time you read
it; the pull request list is the source of truth.

Rules that govern it (`CLAUDE.md`): always be working on something real (rule 2); a PR merges only when every CI job is
finished and green, "Build & test (Ubuntu)" and "Own stack" included (rule 6); nothing that replaces third-party code
is the default until the owner approves it (rule 17).

## Releases out

| Version | What | Checked |
|---|---|---|
| 2.887.0 | Visual Script saved values, about 30 new blocks, Rule block, recipe cards | tag, 10 assets, checksums, Supabase |
| 2.888.0 | Editor frame-time overlay, Xbox One test export (experimental), downloadable game content | same |
| 2.889.0 | Settings > Experimental switches, texture tools, Xbox export fixes, own platform layer event loops | same |
| 2.890.0 | Boat and water, Visual Script blocks that explain themselves, Xbox editor freeze fix | version bump is PR #1026; dispatch `release.yml` once after it merges, then verify |

## Open pull requests (merge only when every job is green)

| PR | What | Next |
|---|---|---|
| #1026 | Release 2.890.0 version bump | Merge, dispatch the release once, verify tag, assets, checksums, Supabase |
| #1018 | Physics: stable stacking at any step rate, Physics Material asset, Physics Toys, three example scenes | Main merged in (head e6e83496); waiting for CI |
| #1019 | Wind: global and zone wind, sway, drag, sound, editor tools | Waiting for CI on 96ce49bc |
| #1022 | Lighting: stable cascades, soft edges, masks, panel lights, probes (GPU shaders unrun) | Fixing `fp_hand` and a `lighting_upgrade` segfault |
| #1023 | Camera: rigs, collision, blends, shake, 2D rooms | Main merged in; waiting for CI |
| #1024 | Launcher on our own UI (OkayUI), window mode, platform rounds 13 and 14 | Own stack job: `okayui_port_project2` golden fix in progress |
| #1025 | Build Settings: searchable pages, profiles, many settings the game honours | Waiting for CI on 698e6630 |

## Finished branches waiting for a PR (all pushed)

| Branch | What | Note |
|---|---|---|
| `claude/core-perf-1` and `claude/core-assets-1` | Work-stealing jobs, allocators, trace capture; asset manager budget, scene GUIDs, Validate Scene | Open as one PR so one CI run decides both |
| `claude/particles-2` and `-3` | Particle system rewrite, 17 presets, preview, scripts | Needs wind merged first (same update loop) |
| `claude/add-component-menu-1` | New Add Component menu | Editor not run |
| `claude/build-settings-3` | More Build Settings (render scale, crash reports, args, per-platform, Web, Android, installer text) | Goes after #1025 |
| `claude/project-upgrade-save-1` | Fixes for "project shows not upgraded" and editor save/load | Work in progress |
| `claude/okayui-next-3` | CI job that builds the launcher and editor on OkayUI, OkayUI programs in the dist script, `winpaths` lint | Goes after #1024 |
| `claude/own-platform-6`, `claude/wind-2`, `claude/water-visual-1`, `claude/vehicle-1`, `claude/ingame-ui-crisp-1`, `claude/ecs-1`, `claude/time-1`, `claude/xbox-uwp-3` | Platform work, wind follow-on, water visuals, GTA-style vehicle controller and settings, crisp in-game UI, entity/component system, time management, Xbox fixes | Some are WIP commits pushed for safety |
| `claude/okayms-3-r23b` | Our own MSVC-style compiler rounds 23 to 33 (compile-only: Wine broke in the container) | Do not merge until the full tests run under Wine |

## Own replacements: how far

All are off by default. These are estimates, not measurements, except where a count is given.

| Replacement | State |
|---|---|
| CMake → okay-build | Reads the project's CMakeLists (815 of 820 targets, 830 of 834 tests); built and linked the launcher, player and relay for Linux and MinGW; the editor did not build here (memory) |
| SDL2 → okay::platform | Direct SDL calls in programs fell from 931 to about 128; own window surface and painter work continues on `own-platform-6` |
| Dear ImGui → okayui | 99.8% of used names answered; the real launcher ran headless on OkayUI (13 pages); the editor compiles against it but has never run |
| MSVC → okay-ms | 82 of 348 engine files compile to objects (honest count); crypto and math were linked and run under Wine earlier |
| Crypto | libsodium stays (owner decision); own crypto off |

## Waiting on the owner

- Branch protection: the required-checks list for `main` is empty, so GitHub still allows red merges.
- Windows tests: update to the latest release; Settings > Experimental; the Xbox page (should no longer freeze); the
  Xbox triangle and engine-mode build from the `claude/xbox-uwp-3` zip.
- Whether the OkayUI programs should ship in the release zip (the dist step is on `claude/okayui-next-3`).
- A reference package signed with SignTool, for own signing stage 5.

## Lessons from this session

- A stray file with a name Windows cannot check out (a control character, or `ssets`) broke the Windows checkout on a
  PR. The `winpaths` check on `claude/okayui-next-3` stops this.
- New script builtins need `docs/engine_api.md` regenerated (`OKAY_WRITE_API_DOCS=1 ./test_vs_types`); new component
  types must be added to the list in `tests/test_save_coverage.cpp`.
- No pictures in the repo except golden test references. Tests that write pictures use a temp folder.
- Goldens must not depend on the network, the clock, fonts or threads still running.
- Several PRs that touch the same generated files conflict after every merge: combine them into one PR.

## Machine limits

About 16 GB free disk (stop at 5 GB). The machine is shared by many agents: the load average has been 15 to 17 on
4 CPUs, and full engine builds are often killed for memory. Agents compile only what they touch and link against a
prebuilt library; CI is the first full build.
