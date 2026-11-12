# 19. Docs, tests & code health

The written docs (`docs/`, `README.md`, `ROADMAP.md`), the test suite (`tests/`: 406 test files and 414 `add_test` lines in one
`tests/CMakeLists.txt`), the developer and QA working loop, and the housekeeping that keeps the repository healthy (file sizes,
stray files, warnings, sanitizers). 19.1 holds the index of every docs file; 19.2 explains how the tests are organised and how to
run one. Since 2.848.0 the suite grew by about 65 `add_test` lines and 13 docs pages: #571 (core wave 4), #572 (undo, recovery, play-tests, save framework), #574, #575 and #578, the loop and time passes #579, #581 and #585 (`docs/loop_and_time.md`), the scene graph wave #586 and the timers and tweens wave #587 are all merged. Nothing else is moving here.

## 19. Docs, tests & code health

### 19.1 README and docs
- **What:** The user-facing documentation: `README.md` at the repo root, `ROADMAP.md`, and 71 Markdown pages, 16 SQL files and 21 screenshots under `docs/`. `docs/README.md` is the index players and developers start from (this feature map, `docs/FEATURE_MAP.md` and `docs/feature_map/`, and `docs/archive/` are not listed below).
- **Parts:**
  - 19.1.1 Root README - `README.md` - what OkaySpace is, how to download and build, the repo layout (checked against the code in 2.847.0, #544 and #546)
  - 19.1.2 Docs index - `docs/README.md` - links grouped by goal (get started, scripting, characters, gameplay, look and sound, game UI, multiplayer, engine work)
  - 19.1.3 Generated API page - `docs/engine_api.md` - generated from the engine binding table; `test_vs_types` fails if it is stale (`OKAY_WRITE_API_DOCS=1 ./tests/test_vs_types` rewrites it)
  - 19.1.4 SQL schema files - `docs/sql/*.sql` - the Supabase schema, run once in the SQL editor in the order given in each file header (mapped in 41.6)
  - 19.1.5 Screenshots - `docs/images/anticheat/*.png` - 21 images used by `docs/anticheat.md` (taken from the launcher's built-in demo data)
- **Code:** `README.md`, `docs/README.md`, `docs/engine_api.md`
- **API:** `OKAY_WRITE_API_DOCS=1`
- **Editor:** none
- **Docs:** `docs/README.md`
- **Tests:** `vs_types` (the engine API page must be current)
- **Status:** Works - docs exist for most areas; the index is missing ten pages (below)
- **Open:** `docs/README.md` links none of ten newer pages (`asset_database.md`, `asset_manager.md`, `core_audit_notes.md`, `layers_tags_cameras.md`, `scene_management.md`, `scene_hierarchy_search.md`, `core_tools.md`, `ecs.md`, `localization_prefs_time.md`, `math_geometry.md`); "README matches reality plus a component audit doc" (old map) is open; `ROADMAP.md` is stale on anti-cheat hardening (19.5); `asset_database.md` and `asset_manager.md` say nothing uses them yet (recheck)
- **Related:** 19.5, 41.9
- **Words:** docs, documentation, README, docs index, engine_api, markdown, screenshots, guides, how to
- **Seen as:** a doc says a feature isn't built but it is; a doc page isn't linked from the index; the API page diverges from the engine
- **Ask me like:** "which doc explains the relay?", "add the missing pages to the docs index"

**Docs index: every file under `docs/` (except `archive/`, `FEATURE_MAP.md` and `feature_map/`)**

| File | What it covers | Area |
|---|---|---|
| `docs/README.md` | the index of the docs, grouped by goal | 19.1 |
| `docs/accessibility.md` | accessibility settings, captions, colour vision, focus graph, remap model (engine side) | 32 |
| `docs/accounts.md` | player accounts: local, custom server, Supabase; cloud saves, leaderboards, script use | 17.2 |
| `docs/animation.md` | custom keyframe clips, imported animation, the Animator, built-in animations | 5 |
| `docs/anticheat.md` | server rules, encryption, session tickets, client integrity, bans, keys, limits | 39 |
| `docs/asset_database.md` | AssetDatabase data layer: GUIDs, dependencies, import settings, thumbnails, search | 20 |
| `docs/asset_manager.md` | AssetManager: typed handles, refcounts, async load, hot reload | 20 |
| `docs/audio.md` | AudioSource, mixer groups, music, volume settings, scripting | 7 |
| `docs/automated_testing.md` | play-test runner, scene validator, golden files, scene digest and golden images | 33 |
| `docs/behavior_trees.md` | NPC behavior trees, blackboard, runner | 9.4 |
| `docs/build_mode.md` | player build mode and logic links, saving creations | 11.5 |
| `docs/core_audit_notes.md` | bug-hunt notes for area 1 (ECS, math, camera, layers, DataAsset, input) | 1 |
| `docs/core_tools.md` | core helpers: quit handling, CommandLine, Paths, EventBus extras, Cvars and Console, Stopwatch, StateMachine | 1 |
| `docs/core_logging_profiling.md` | Log categories and file sink, frame profiler, trace and CSV export | 1.10 |
| `docs/core_runtime.md` | job system, arenas and pools, slot map handles, string ids, frame pacing | 1.18 |
| `docs/cutscenes.md` | sequences (`.okayseq`) and the Timeline window | 10.1 |
| `docs/day_night_cycle.md` | the DayNightCycle component | 2.3 |
| `docs/debug_tools.md` | profiling and debugging data layers: frame history, GPU timer, memory tracker, draw call recorder, console model | 23 |
| `docs/dedicated-server.md` | running and exporting dedicated servers, server.cfg, flags | 16.5 |
| `docs/dialogue_quests.md` | dialogue graphs, story state, quests | 10.2 |
| `docs/ecs.md` | ECS iteration under mutation, command buffer, delta snapshots in NetWorld | 1.9 |
| `docs/editor-plugins.md` | editor plugins written in OkayScript | 15.8 |
| `docs/editor.md` | the editor, its panels and Build Game | 15 |
| `docs/engine_api.md` | generated engine API reference (do not edit by hand) | 13.6 |
| `docs/game_ui.md` | game UI drawn in the launcher's look | 12.7 |
| `docs/ik.md` | inverse kinematics: feet, hands, look-at, full body | 5.6 |
| `docs/input.md` | input actions, keys, contexts, controllers (dead zones and curves), rebinding | 8.3 |
| `docs/launcher-multiplayer.md` | multiplayer from the launcher: join, invites, browser, matchmaking, tick, authority | 16, 17.15 |
| `docs/layers_tags_cameras.md` | layer collision matrix and presets, tags, project tag list and layer names (`ProjectLists`, the editor hook), camera stack | 1.3 |
| `docs/lighting.md` | lights, shadows, lightmaps | 2.3 |
| `docs/localization.md` | string tables, translators, fonts | 1.8 |
| `docs/localization_prefs_time.md` | plural rules, number formats, typed Prefs, fixed timestep | 1.8 |
| `docs/localization_tools.md` | translation table model, checker, key scanner, previews | 31 |
| `docs/loop_and_time.md` | GameLoop, clocks, hit stop, pause and quit reasons, random extras, frame stepping, recording, capture, frame statistics, Countdown and Cooldown, time snapshot, loop console, adopting GameLoop in the editor (three passes) | 1.1 |
| `docs/math_geometry.md` | math additions: Mat3, bounds and shape tests, noise, curves, colour spaces, Rng | 1 |
| `docs/making_a_game.md` | first game from an empty editor to a shipped one | 15 |
| `docs/modeling.md` | the Modeling window (mesh editor) | 3 |
| `docs/navigation.md` | navigation meshes and agents | 9.1 |
| `docs/npcs_and_crafting.md` | NPC Controller and crafting components | 9.5 |
| `docs/optimization_tools.md` | mesh simplifier, LOD selection, occlusion buffer, portal cells, batch planner, streaming grid, optimization report | 27 |
| `docs/packaging.md` | distribution layout of the Windows build | 18.1 |
| `docs/photon.md` | the opt-in Photon Realtime transport | 16.10 |
| `docs/prefab_system.md` | nested prefabs, variants, per-field apply and revert, unpack (`PrefabTools`) | 21 |
| `docs/prefabs.md` | prefab instances, overrides, persistent object ids | 1.6 |
| `docs/properties.md` | the property registry: values, paths, change hook, snapshots, builder | 1.19 |
| `docs/qa_workflow.md` | developer and QA sessions working together | 19.3 |
| `docs/ragdolls.md` | ragdoll death and hit reactions | 5.8 |
| `docs/relay.md` | the relay, NAT traversal, what it enforces | 16.14 |
| `docs/releasing.md` | publishing updates (Supabase, GitHub) and the official game | 18.4 |
| `docs/rendering.md` | post-processing, sky, fog, weather | 2.4 |
| `docs/save_framework.md` | saveable objects, checkpoints, slot headers, migrations, sync hooks | 35 |
| `docs/save_games.md` | save slots, durable saves, what a save keeps | 1.7 |
| `docs/scene_diff_merge.md` | scene canonical form, diff, three-way merge and migration libraries | 1.5, 28 |
| `docs/scene_hierarchy_search.md` | hierarchy-aware active, FindByPath, validation, FixedUpdate, load errors; the scene graph audit (deep hierarchies, Transform and spawn API, Mover, Spinner, Lifetime) | 1.17, 1.3 |
| `docs/scene_management.md` | loading scenes, spawning and destroying, pooling, RequireComponent | 1.4 |
| `docs/scene_serializer.md` | what a scene round trip keeps (transient objects, NaN, layers, optional trailing fields), why component order is not saved, the component serializer registry | 1.5 |
| `docs/scripting.md` | the OkayScript language guide | 13.1 |
| `docs/scripting_reference.md` | built-in functions area by area | 13.6 |
| `docs/shooter_and_25d_controllers.md` | top-down shooter and 2.5D platformer controllers | 4.3 |
| `docs/splines.md` | splines, followers, lines, trails | 2.7 |
| `docs/steam_release.md` | releasing on Steam, the Steam service | 17.10 |
| `docs/supabase-backend.md` | the Supabase tables and calls behind accounts, cloud saves, leaderboards, matchmaking | 17 |
| `docs/survival_kit.md` | health, hunger, thirst, stamina, oxygen, temperature components | 11.4 |
| `docs/timers_and_tweens.md` | Scheduler and TweenManager: clock channels, task owners, ease curves, tweening any property, springs, debug view | 1.2 |
| `docs/ui_html.md` | menus and HUDs in an HTML and CSS subset | 12.5 |
| `docs/ui_world_space.md` | in-world (3D) UI canvases and labels | 12.6 |
| `docs/undo_recovery.md` | command undo stack, autosave and crash recovery, backups (engine side) | 22 |
| `docs/vehicle_controller.md` | the arcade vehicle controller | 4.5 |
| `docs/visual_scripting.md` | the OkayVS text format and Action Lists | 14 |
| `docs/voice-server.md` | running the voice server (the relay) | 16.7 |
| `docs/web_mobile.md` | web (WASM) and mobile builds | 18.2 |
| `docs/sql/admin.sql` | site roles, site bans, audit log | 17.9 |
| `docs/sql/anticheat.sql` | anti-cheat reports, bans, appeals, hardware-hash matching | 39.6 |
| `docs/sql/app_config.sql` | site-wide settings the launcher reads (default voice server) | 16.7 |
| `docs/sql/chat_extras.sql` | message edit history and shared pins | 17.4 |
| `docs/sql/chat_media.sql` | public Storage bucket for chat files and voice notes | 17.4 |
| `docs/sql/follows.sql` | one-way follows | 17.3 |
| `docs/sql/forum_sections.sql` | forum sections, members, bans | 17.5 |
| `docs/sql/game_servers.sql` | the `game_sessions` server-browser listing | 16.4 |
| `docs/sql/group_chats.sql` | group chats | 17.4 |
| `docs/sql/matches.sql` | match results, player stats, leaderboards | 16.9 |
| `docs/sql/matchmaking.sql` | matchmaking tickets and assignments | 16.4 |
| `docs/sql/official_game_admin.sql` | admin write access under `official/` in the releases bucket | 17.14 |
| `docs/sql/parties.sql` | parties and members | 17.3 |
| `docs/sql/profile_extras.sql` | playtime, social links, verified tag, staff badges | 17.7 |
| `docs/sql/profile_showcase.sql` | profile showcase picks and mirrored achievements | 17.7 |
| `docs/sql/social.sql` | profiles, friendships, messages, reactions, forums, workshop, community servers | 17 |
| `docs/images/anticheat/*.png` | 21 screenshots for `docs/anticheat.md` (admin pages, launcher settings, player ban and appeal) | 39 |

### 19.2 Test suite
- **What:** About 418 tests (414 `add_test` lines; one sits in a loop that registers five) in one `tests/CMakeLists.txt`, almost all of them a small program that prints `FAIL` lines and returns non-zero. Plus a few shell, SQL and CMake checks. This sub-area is how it is organised; each test belongs to the area it tests.
- **Parts:**
  - 19.2.1 Layout of `tests/CMakeLists.txt` - `tests/CMakeLists.txt` - 1,541 lines, roughly in the order tests were added (no grouping by area): each test is `add_executable(test_x test_x.cpp)`, a link to `okay::engine` and `add_test(NAME x COMMAND test_x)` under a `#` comment. ctest names drop `test_` except `remove_component` (`test_remove`), `physics_layers` (`test_layers`), `physics_queries` (`test_queries`)
  - 19.2.2 Test harness - `tests/test_framework.hpp` - `CHECK`, `CHECK_NEAR`, `RUN_SUITE(name)`, `TEST_MAIN_RESULT()`; no framework dependency
<<<<<<< HEAD
  - 19.2.3 Conditional tests - `tests/CMakeLists.txt` - `okayui` (needs SDL2), `ui_renderer_golden` (needs SDL2), `core_audit_input` (UNIX only, uses a pty), `publish_*`, `anticheat_sql` and `reports_sql` (UNIX only, shell); `vs_editor_model` needs Dear ImGui headers (fetched when tests are configured alone)
  - 19.2.4 Slow and special tests - `tests/CMakeLists.txt` - `loaders_robust` has `TIMEOUT 120`; tests with real sockets sleep and wait, judged from `sleep_for` counts in the sources, not timed (`net`, `dedicated_server`, `net_rooms`, `launcher_join`, `net_spawn`, `net_auth`); `ui_render_bench` is a benchmark; `anticheat_sql` and `reports_sql` exit 77 (skipped) unless `OKAY_SQL_TEST_PGHOST` names a local Postgres; `anticheat_keys_bad_*` are `WILL_FAIL` on purpose
=======
  - 19.2.3 Conditional tests - `tests/CMakeLists.txt` - `okayui` (needs SDL2), `ui_renderer_golden` (needs SDL2), `core_audit_input` (UNIX only, uses a pty), `publish_*`, `anticheat_sql` and `blocks_sql` (UNIX only, shell); `vs_editor_model` needs Dear ImGui headers (fetched when tests are configured alone)
  - 19.2.4 Slow and special tests - `tests/CMakeLists.txt` - `loaders_robust` has `TIMEOUT 120`; tests with real sockets sleep and wait, judged from `sleep_for` counts in the sources, not timed (`net`, `dedicated_server`, `net_rooms`, `launcher_join`, `net_spawn`, `net_auth`); `ui_render_bench` is a benchmark; `anticheat_sql` and `blocks_sql` exit 77 (skipped) unless `OKAY_SQL_TEST_PGHOST` names a local Postgres; `anticheat_keys_bad_*` are `WILL_FAIL` on purpose
>>>>>>> origin/main
  - 19.2.5 ASan and UBSan job - `.github/workflows/ci.yml` - 38 executables built and run directly, not through ctest: `visualscript`, `vs_nodes`, `vs_types`, `okayscript_next`, `scripting`, `vs_editor_model`, all 22 `script_*` (new ones join automatically), `anim_sm_realloc`, `prefs_misc`, `loaders_robust`, `scene_records`, `physics_callback_removal`, `physics_nan`, `parent_cycles`, `scheduler_reentry`, `net_hardening`, `net_races`
  - 19.2.6 Test data and helpers - `tests/rig_harness.hpp`, `tests/ui_golden_scenes.hpp`, `tests/assets/rigs/` (generated by `tools/make_test_rigs.py`), `tests/golden/ui/` (golden PNGs), `tests/data/audio/`, `tests/export/` (export check), `tests/scripts/*.sh` and `tests/sql/*.sql` (shell and SQL tests), `tests/anticheat_keys_check.cmake` (CMake driver)
  - 19.2.7 Tests that read the source tree - `tests/CMakeLists.txt` - tests given `OKAY_SOURCE_DIR` read files from the repo (`audio_formats`, `scene_safe_load`, `vs_types` which reads `docs/engine_api.md`, `rig_sweep`, golden images), so run them with the source tree present
  - 19.2.8 How to run one - `tests/CMakeLists.txt` - configure with tests on (the default), build one target, run it: `cmake --build build --target test_net_predict`, then `ctest --test-dir build -R '^net_predict$' --output-on-failure` or `./build/tests/test_net_predict`
<<<<<<< HEAD
- **Code:** `tests/CMakeLists.txt`, `tests/test_framework.hpp`, `tests/rig_harness.hpp`, `tests/ui_golden_scenes.hpp`, `tests/anticheat_keys_check.cmake`, `tests/scripts/test_anticheat_sql.sh`, `tests/scripts/test_reports_sql.sh`, `tests/scripts/test_publish_official_cleanup.sh`, `tests/scripts/test_publish_supabase_checks.sh`, `tests/sql/test_anticheat.sql`, `tests/sql/test_reports.sql`, `tests/sql/supabase_stub.sql`
- **API:** `ctest --test-dir build -R <name> --output-on-failure`, `ctest -j4`, `RUN_SUITE`, `CHECK`, `OKAY_SANITIZE`, `OKAY_BUILD_TESTS`, `OKAY_SQL_TEST_PGHOST`
- **Editor:** none (`OkayEngine --selftest` is the headless editor check, 18.3)
- **Docs:** `docs/qa_workflow.md`
- **Tests:** `anticheat_keys_ok`, `anticheat_keys_two_ok`, `anticheat_sql`, `reports_sql`, `publish_official_cleanup`, `publish_supabase_checks`, `ui_renderer_golden`, `rig_sweep`, `qa_pr250`
=======
- **Code:** `tests/CMakeLists.txt`, `tests/test_framework.hpp`, `tests/rig_harness.hpp`, `tests/ui_golden_scenes.hpp`, `tests/anticheat_keys_check.cmake`, `tests/scripts/test_anticheat_sql.sh`, `tests/scripts/test_blocks_sql.sh`, `tests/scripts/test_publish_official_cleanup.sh`, `tests/scripts/test_publish_supabase_checks.sh`, `tests/sql/test_anticheat.sql`, `tests/sql/supabase_stub.sql`
- **API:** `ctest --test-dir build -R <name> --output-on-failure`, `ctest -j4`, `RUN_SUITE`, `CHECK`, `OKAY_SANITIZE`, `OKAY_BUILD_TESTS`, `OKAY_SQL_TEST_PGHOST`
- **Editor:** none (`OkayEngine --selftest` is the headless editor check, 18.3)
- **Docs:** `docs/qa_workflow.md`
- **Tests:** `anticheat_keys_ok`, `anticheat_keys_two_ok`, `anticheat_sql`, `blocks_sql`, `publish_official_cleanup`, `publish_supabase_checks`, `ui_renderer_golden`, `rig_sweep`, `qa_pr250`
>>>>>>> origin/main
- **Status:** Works - the suite runs in CI on every PR (ctest on Ubuntu plus the sanitizer subset)
- **Open:** possible gaps: nothing is grouped or labelled (`ctest -L` has no labels), no TSan job for `net_races`, tests that need SDL2 or a pty silently disappear when the dependency is missing, six tests run on Windows; the "about 330 tests" figure in the old map is now about 418; #572 added a play-mode, scene-validation and regression test runner (`okay-playtest`, area 33) that runs as ctests `playtest_runner` and `playtest_runner_list`
- **Related:** 18.3, 19.7, 33, 19.3
- **Words:** tests, ctest, add_test, test_framework, CHECK, ASan, sanitizer, golden image, test suite, run one test, RUN_SUITE, TIMEOUT, WILL_FAIL
- **Seen as:** CI red on one test; a test passes locally but fails in CI (missing SDL2 or libsodium); a test is skipped; a test needs files that only exist in the repo
- **Ask me like:** "run only the lag compensation test", "which tests does the sanitizer job run?"

**Test groups (approximate, by test name; the 414 `add_test` lines; the groups were counted at 349 and not recounted, so they add up to less)**

| Group | About | Examples |
|---|---|---|
| Networking, anti-cheat, relay, matchmaking, dedicated server, launcher join | 44 | `net_*`, `net_anticheat_*`, `relay_security`, `matchmaker`, `dedicated_server`, `launcher_join` |
| Core, scene, saves, math, events, time, assets, accounts, Steam | 60 | `scene_*`, `serialize`, `durable_saves`, `prefs`, `ecs`, `account`, `steam` |
| Animation, IK, characters, rigs | 53 | `anim_*`, `ik_*`, `char_*`, `humanoid_*`, `rig_sweep` |
| Gameplay: inventory, building, NPC, navigation, dialogue, cameras, controllers | 46 | `inventory_*`, `build_mode`, `navmesh`, `behavior_tree`, `cinemachine` |
| Modeling, rendering, terrain, particles | 34 | `polymesh`, `modifiers`, `shadows`, `terrain`, `gltf_import` |
| Scripting (OkayScript, typed C#-style) | 30 | `script_*`, `scripting`, `okayscript_next` |
| UI | 28 | `ui_*`, `okayui`, `uidocument`, `ui_renderer_golden` |
| Physics | 19 | `physics*`, `hinge*`, `angular*`, `destructible` |
| Visual scripting | 12 | `vs_*`, `visualscript`, `actionlist` |
| Audio | 6 | `audio*`, `wav`, `master_volume` |
| Engine tools data layers (undo, recovery, backup, `debug_*`, `optimize_*`, `playtest*`, `regression`, `save_framework`, `prefab_*`, `property_registry`, `scene_diff`/`scene_merge`, `loc_tools`, `accessibility`, `jobs`) | 46 | `undo`, `recovery`, `debug_overlay`, `optimize_batching`, `playtest`, `prefab_nested`, `property_registry`, `scene_merge` |
| Launcher helpers, publish scripts and a few leftovers | 16 | `chat_json`, `friends_json`, `official_game`, `crash_reports`, `project_delete`, `publish_*`, `qa_pr250`, `scriptapi`, `subtree` |

### 19.3 QA workflow, rig sweep test
- **What:** How a developer session and a QA session work on the same repository (PR comments as the channel, a bug and a verdict format), and the rig sweep test that runs every test rig through the same motions.
- **Parts:**
  - 19.3.1 The loop - `docs/qa_workflow.md` - developer opens a draft PR and messages QA; QA builds, tests for real (scripted probes, editor under Xvfb), posts one comment per bug and one verdict; merge only after a PASS on the latest commit and green CI
  - 19.3.2 Bug and verdict formats - `docs/qa_workflow.md` - what each comment must contain; QA never pushes to the developer's branch, merges, releases or touches keys
  - 19.3.3 Headless editor driving - `docs/qa_workflow.md` - Xvfb, `OKAY_DEV=1`, `xdotool`, `import -window root` screenshots, quick engine probes linked against `libokay_engine.a`
  - 19.3.4 Rig sweep - `tests/test_rig_sweep.cpp`, `tests/rig_harness.hpp` - every rig in `tests/assets/rigs/` idles, walks, runs, jumps, crouches and turns; skeletons must stay finite and not spike
  - 19.3.5 QA regression tests - `tests/test_qa_pr250.cpp` - one block per bug QA found on PR #250 (QA writes them on its own branch in a separate PR)
  - 19.3.6 Test rig generator - `tools/make_test_rigs.py` - deterministic pure-Python glTF rigs (one 25-joint humanoid under many naming and axis conventions, plus a non-humanoid quadruped)
- **Code:** `tests/rig_harness.hpp`, `tools/make_test_rigs.py`
- **API:** `python3 tools/make_test_rigs.py [out_dir]`, `OKAY_DEV=1`, `OkayEngine --selftest`
- **Editor:** none
- **Docs:** `docs/qa_workflow.md`
- **Tests:** `rig_sweep`, `qa_pr250`
- **Status:** Works - the doc is current for the process; the sweep test is one of the 418
- **Open:** the workflow needs two cloud sessions and a scheduled message each way (cloud sessions cannot message each other)
- **Related:** 19.2, 5.4, 33
- **Words:** QA, quality assurance, workflow, verdict, PASS, bug comment, rig sweep, Xvfb, regression, developer session
- **Seen as:** a PR waits for a verdict; a rig's skeleton spikes during a walk
- **Ask me like:** "write the QA verdict for PR 575", "add a rig to the sweep"

### 19.4 Splitting the largest files (editor, launcher, OkayScript VM, serializer)
- **What:** Keeping source files small enough to work in. The biggest files today (lines, as of main at 224cda8c on 2026-11-09, after #587) are `editor/main.cpp` 45,207, `launcher/main.cpp` 24,802, `engine/src/Scripting/OkayScriptVM.cpp` 10,536, `engine/src/Scene/SceneSerializer.cpp` 8,958, `engine/include/okay/Render/Mesh.hpp` 4,079, `editor/VisualScriptEditor.hpp` 3,800, `engine/src/Net/NetworkManager.cpp` 3,522.
- **Parts:**
  - 19.4.1 Editor - `editor/main.cpp` - already partly split (windows live in `editor/*.cpp`); see 15.10
  - 19.4.2 Launcher - `launcher/main.cpp` - one file for the shell and every page (17.1)
  - 19.4.3 OkayScript VM - `engine/src/Scripting/OkayScriptVM.cpp` - see 13.1
  - 19.4.4 Scene serializer - `engine/src/Scene/SceneSerializer.cpp` - see 1.5
  - 19.4.5 Network manager - `engine/src/Net/NetworkManager.cpp` and `.hpp` - one class for 16.1 to 16.13 (not on the old list)
- **Code:** `launcher/main.cpp`, `engine/src/Net/NetworkManager.cpp`
- **API:** -
- **Editor:** none
- **Docs:** `ROADMAP.md` ("Later: split the largest source files")
- **Tests:** -
- **Status:** Planned - on the roadmap's "Later" list; no branch exists
- **Open:** splitting `launcher/main.cpp` needs a plan per page (17.1 to 17.16 are the natural seams); `player/main.cpp` is 2,658 lines and also carries UI drawing, matchmaking windows and the server entry point
- **Related:** 15.10, 17.1, 13.1, 1.5, 16.2
- **Words:** split, refactor, large file, main.cpp, file size, god file, OkayScriptVM, SceneSerializer
- **Seen as:** slow builds when one file changes; merge conflicts in `launcher/main.cpp` or `editor/main.cpp`
- **Ask me like:** "split the launcher chat code into its own file"

### 19.5 Roadmap and PR template
- **What:** The two files that steer day-to-day work: `ROADMAP.md` (releases table, current work, plans) and the pull-request template.
- **Parts:**
  - 19.5.1 Roadmap - `ROADMAP.md` - releases table (latest 2.851.0), "Now" table of sessions, plan checklists, "Waiting on Iman", "Later"
  - 19.5.2 PR template - `.github/PULL_REQUEST_TEMPLATE.md` - summary, changes, two verification ticks
- **Code:** `ROADMAP.md`, `.github/PULL_REQUEST_TEMPLATE.md`
- **API:** -
- **Editor:** none
- **Docs:** `ROADMAP.md`
- **Tests:** -
- **Status:** Partial - kept current by the coordinating session, but parts are stale
- **Open:** the anti-cheat hardening list still shows items 5, 6, 8, 9, 10, 12, 13, 14 as open, although `git log` has #350, #360, #363, #365, #368, #376, #384, #392 and #394 on main; the "Now" table is dated 2026-10-26; rows up to 2.851.0 are in (each merged with its release PR); 2.852.0 (draft PR #588) will add its own
- **Related:** 18.4, 19.1
- **Words:** roadmap, ROADMAP.md, plan, release notes, pull request template, what is next
- **Seen as:** the roadmap disagrees with what is merged
- **Ask me like:** "update the roadmap's hardening list from git log"

### 19.6 Repository hygiene
- **What:** Files that should not be in the repository, or checks that are missing, found while mapping.
- **Parts:**
  - 19.6.1 Stray prefs files at the repo root - `game.okayprefs` and seven files named `$who`, `1`, `arr`, `map`, `msg`, `target`, `v`, `x` - each is a 163-byte copy of the same prefs text (keys like health, map, msg), probably written by a script test run from the repo root (first added 2026-07-09)
  - 19.6.2 Old binaries in `dist/` - `dist/OkaySpace.exe`, `OkaySpacePlayer.exe`, `SDL2.dll`, `OkaySpace-Launcher.exe`, `OkaySpace-Multiplayer.exe`, `OkaySpace-Physics.exe` from June 2026 and `dist/VERSION.txt` 2.412.0, although `*.exe` is in `.gitignore`; only `dist/CHANGELOG.txt` is live (the release attaches it)
  - 19.6.3 Compiled Python - `examples/account-server/__pycache__/server.cpython-311.pyc` is committed
  - 19.6.4 Two static-SDL switches - `OKAY_STATIC_SDL` (read by the MinGW toolchain and root CMake) and `OKAY_EDITOR_STATIC_SDL` (read by the editor, launcher and player CMake) do similar things under different names
- **Code:** `.gitignore`, `dist/CHANGELOG.txt`, `dist/VERSION.txt`, `launch.sh`, `launch.bat`
- **API:** -
- **Editor:** none
- **Docs:** -
- **Tests:** -
- **Status:** Basic - nothing enforces it; listed so someone can clean up
- **Open:** delete the stray files and add them to `.gitignore`, stop committing `dist/` binaries (old map item), drop the `.pyc`, add `__pycache__/` to `.gitignore`
- **Related:** 18.1, 41.9, 42.4
- **Words:** stray files, junk, dist, committed binaries, .gitignore, pycache, hygiene, cleanup
- **Seen as:** odd files named `x` or `map` at the repo root; a huge clone because of binaries
- **Ask me like:** "clean the repo root of generated files"

### 19.7 Compiler warnings and sanitizers
- **What:** The switches that catch bugs at build time: strict warnings everywhere, and an opt-in sanitizer build used by the CI job.
- **Parts:**
  - 19.7.1 Warnings - `CMakeLists.txt` - `-Wall -Wextra -Wpedantic` (`/W4 /permissive-` on MSVC); the PR template asks for a warning-clean build
  - 19.7.2 Sanitizer option - `CMakeLists.txt` - `-DOKAY_SANITIZE=address,undefined,float-cast-overflow` adds `-fsanitize` and `-fno-sanitize-recover=all` so findings abort
  - 19.7.3 Sanitizer CI job - `.github/workflows/ci.yml` - the subset in 19.2.5, with `detect_leaks=1` and `vm.mmap_rnd_bits=28` to keep ASan's address layout reliable
- **Code:** `CMakeLists.txt`, `.github/workflows/ci.yml`
- **API:** `OKAY_SANITIZE`, `ASAN_OPTIONS`, `UBSAN_OPTIONS`
- **Editor:** none
- **Docs:** `docs/qa_workflow.md`
- **Tests:** `net_races`, `loaders_robust`, `physics_nan`
- **Status:** Works - used by CI on every PR
- **Open:** no ThreadSanitizer job even though `net_races` is written for it; only a subset of tests run under sanitizers
- **Related:** 19.2, 18.3
- **Words:** warnings, Wall, Wextra, sanitizer, ASan, UBSan, TSan, memory error, leak, undefined behaviour
- **Seen as:** CI red with "AddressSanitizer" or "runtime error"; a warning in the build log
- **Ask me like:** "run this test under the sanitizers locally"
