# OkaySpace documentation: start here

> **What this is:** the one page to open first. It says what the engine is, where every docs folder is, which pages
> matter most, and which pages to read first for what you want to do.
>
> **Status:** rewritten on 2026-11-23 at the owner's request ("update the read me and the docs more in depth and
> detail"). It is grouped by who reads it. Each folder still has its own `README.md` with every page, one line each.
>
> **Last checked:** 2026-11-24, against `main` (version 2.903.9 in `CMakeLists.txt`). Links checked; every page in a
> folder is listed in that folder's `README.md` or here.

## What OkaySpace is

OkaySpace is a game engine written in C++. It has three programs:

- the **launcher** (`OkaySpace.exe`), where you sign in, find projects, play games and update;
- the **editor** (`OkayEngine.exe`), where you build scenes, add scripts and press Play;
- the **player** (`OkaySpacePlayer.exe`), which runs a finished game (and a dedicated server, with no window).

Releases are for Windows only for now. The goal is to write every part ourselves. A few outside libraries are still
used while our own versions are built (SDL2, Dear ImGui, Assimp, curl as the last HTTP fallback, libsodium; ufbx, ImGuiColorTextEdit and `SDL_LoadWAV` were replaced and deleted on 2026-11-23). The overview of every system
is in the repository's [README](../README.md).

**What changed lately:** every release in plain words is in [dist/CHANGELOG.txt](../dist/CHANGELOG.txt), and the
releases and plans are in [ROADMAP.md](../ROADMAP.md). **What is being worked on now:** the live work board, which
`.github/workflows/work-board.yml` rewrites from GitHub every 10 minutes on the `work-board` branch (its
`WORK_BOARD.md`), and the hand-written [status/work_board.md](status/work_board.md) snapshot. The
[WORK_BOARD.md](WORK_BOARD.md) copy on `main` is an older hand-kept board and may be out of date.

## Start here

Pick the line that fits you and read the pages in order.

| I want to... | Read |
|---|---|
| **make my first game** | [Make your first game](gameplay/making_a_game.md), then the [editor](editor/editor.md), then [OkayScript](scripting/scripting.md) or [Visual Script](scripting/visual_scripting.md). |
| **find a feature** | [SYSTEMS.md](SYSTEMS.md) (every system, one line each), then [FEATURE_MAP.md](FEATURE_MAP.md) (where its code, tests and docs are). |
| **know what is not finished** | [status/UNFINISHED.md](status/UNFINISHED.md). |
| **test a Windows release** | [verification/README.md](verification/README.md) (checklists by area), [try_it.md](try_it.md) (older "how to try it" entries). |
| **work on the engine** | [../CLAUDE.md](../CLAUDE.md), [process/collaboration.md](process/collaboration.md), [engine/architecture.md](engine/architecture.md), [engine/shared_code.md](engine/shared_code.md), [process/no_duplicates.md](process/no_duplicates.md). |
| **replace an outside library** | [status/own_code_roadmap.md](status/own_code_roadmap.md), [status/THIRD_PARTY.md](status/THIRD_PARTY.md), [status/provenance_audit.md](status/provenance_audit.md). |
| **ship a release** | [release/releasing.md](release/releasing.md), [release/release_channels.md](release/release_channels.md), [release/packaging.md](release/packaging.md), [release/updater.md](release/updater.md). |

## For game makers

How to build a game with the editor and the ready-made parts.

**Getting started and the editor**
- [Make your first game](gameplay/making_a_game.md): from an empty editor to a double-clickable game, plus the Player Controller.
- [OkaySpace Editor](editor/editor.md): the panels, the toolbar, Play mode and the editor's features.
- [Editor menus](editor/editor_menus.md): the menu bar and right-click menus, item by item.
- [Scene view: right-click to add](editor/scene_right_click_add.md): adding objects from the Scene view.
- [Project panel](editor/project_panel.md): the asset browser.
- [Undo, autosave, crash recovery and backups](editor/undo_recovery.md) and [Editor undo](editor/editor_undo.md).
- [Project versions and upgrading a project](editor/project_upgrade.md).
- [Level tools](editor/level_tools.md): foliage, terrain layers, decals, cables, measuring.
- [Modeling](editor/modeling.md) and [Guided modeling](editor/guided_modeling.md): the built-in mesh editor.
- [Editor plugins](editor/editor-plugins.md): extend the editor with OkayScript.

**Scenes, objects and data**
- [Scene management](engine/scene_management.md), [Prefabs](engine/prefabs.md), [Save games](engine/save_games.md).
- [Timers and tweens](engine/timers_and_tweens.md), [Splines](engine/splines.md), [Volumes and zones](engine/volumes.md).
- [Data assets](engine/data_assets.md), [Localization](engine/localization.md), [Display settings](engine/display_settings.md), [Accessibility](engine/accessibility.md).

**Characters, animation and gameplay**
- [Cameras, layers and tags](gameplay/cameras.md), [Vehicles](gameplay/vehicles.md), [Shooter and 2.5D controllers](gameplay/shooter_and_25d_controllers.md).
- [Animation](gameplay/animation.md), [Inverse kinematics](gameplay/ik.md), [Active ragdoll](gameplay/active_ragdoll.md), [Walk versus run](gameplay/walk_vs_run.md), [First-person arm](gameplay/fp_hand.md).
- [Navigation meshes](gameplay/navigation.md), [Behavior trees](gameplay/behavior_trees.md), [NPCs and crafting](gameplay/npcs_and_crafting.md).
- [Dialogue and quests](gameplay/dialogue_quests.md), [Cutscenes](gameplay/cutscenes.md).
- [Inventory](gameplay/inventory.md), [Survival kit](gameplay/survival_kit.md), [Weapons](gameplay/weapons.md), [Build mode](gameplay/build_mode.md), [Minimap and turn order](gameplay/minimap.md), [Voxel digging](gameplay/voxel_sandbox_dig.md), [Wind](gameplay/wind.md).

**Looks, physics and sound**
- [Rendering](rendering/rendering.md), [Lighting](rendering/lighting.md), [Textures](rendering/textures.md), [Particles](rendering/particles.md), [Terrain and water](rendering/terrain_water.md), [Shaders](rendering/shaders.md).
- [Physics](physics/physics.md): quality modes, materials, joints, physics toys, ragdolls.
- [Audio](audio/audio.md).

**Scripting and UI**
- [OkayScript](scripting/scripting.md): the language guide and function reference.
- [OkayScript bytecode machine](scripting/okayscript_vm.md): design note for the compiler and stack machine.
- [Visual Script format](scripting/visual_scripting.md) and [Visual Script clarity](scripting/visual_script_clarity.md).
- [engine_api.md](engine_api.md): every function scripts can call (generated).
- [visual_scripting_nodes.md](visual_scripting_nodes.md): every Visual Script node (generated).
- [In-game UI](ui/in_game_ui.md) and [ui_catalogue.md](ui_catalogue.md) (every UI component, style and icon).

**Multiplayer and online**
- [Multiplayer](online/multiplayer.md), [Relay](online/relay.md), [Dedicated servers](online/dedicated-server.md), [Anti-cheat](online/anticheat.md), [OkayAntiCheat audit and design](online/okay_anticheat.md).
- [Accounts](online/accounts.md), [Supabase backend](online/supabase-backend.md) (accounts, cloud saves, leaderboards), [Voice server](audio/voice-server.md).

**Exporting a game**
- [Build pipeline](release/build_pipeline.md): what File > Build Settings makes.
- [Content delivery](release/content_delivery.md): updating a shipped game's data.
- [Web and mobile](platform/web_mobile.md), [Xbox export (experimental)](platform/xbox_uwp.md), [Releasing on Steam](release/steam_release.md).

## For engine developers

How the code is built, how the work is shared, and the replacement work.

**How the work is done**
- [../CLAUDE.md](../CLAUDE.md): the owner's standing rules for every session and agent.
- [How the three Claudes work together](process/collaboration.md) and [lanes.json](lanes.json): lanes, branches, claims.
- [Claude 3: start here](process/claude3_start.md), [Agent brief template](process/agent_brief_template.md).
- [No duplicate code](process/no_duplicates.md) and [Shared code](engine/shared_code.md): look here before writing a helper.
- [Automated testing](process/automated_testing.md), [okay_run](process/okay_run.md), [Templates check](process/templates_check.md), [QA workflow](process/qa_workflow.md), [Review log](process/review_log.md).

**How the engine is put together**
- [Architecture](engine/architecture.md): layers, the nine subsystems, which folders belong where.
- [Modularization plan](engine/modularization_plan.md): splitting big files, starting with `editor/main.cpp`.
- [Core runtime](engine/core_runtime.md), [Loop and time](engine/loop_and_time.md), [Core tools](engine/core_tools.md), [Logging and profiling](engine/core_logging_profiling.md).
- [Properties (reflection)](engine/properties.md), [Asset database](engine/asset_database.md), [Asset manager](engine/asset_manager.md), [Asset streaming (design note)](engine/asset_streaming.md), [Scene serializer](engine/scene_serializer.md), [Scene diff and merge](engine/scene_diff_merge.md), [Scene hierarchy and validation](engine/scene_hierarchy_search.md).
- [Input](engine/input.md), [Math and geometry](engine/math_geometry.md), [Debug tools](engine/debug_tools.md), [Optimization tools](engine/optimization_tools.md), [Performance log](engine/perf_log.md), [Editor speed](editor/editor_performance.md).
- [Our own physics core](physics/physics_core.md): `okay::phys`, what each file was written from.
- [Script API registry](scripting/script_api_registry.md): one table of engine functions for every script language.
- [Script bindings](scripting/script_bindings.md): every component class, method and property reachable from scripts (generated), and the gap audit.
- [okay.js](scripting/okayjs.md): our own browser library (WebGL, scene graph, input, game loop) with the engine's script API.
- [engine/code_index.md](engine/code_index.md): every source file with one line (generated).

**Our own replacements for outside code**
- [own_code_roadmap.md](status/own_code_roadmap.md): the plan and state for each library.
- [THIRD_PARTY.md](status/THIRD_PARTY.md): what is ours and what is still outside code or assets.
- [own_flip_proposal.md](status/own_flip_proposal.md) and [own_stack_rollout.md](status/own_stack_rollout.md): which own parts became the default, and in what order.
- UI: [OkayUI toolkit](ui/own_ui_toolkit.md), [OkayUI designs](ui/okayui_design.md), [OkayUI code editor](ui/own_ui_code_editor.md), [imgui_port_report.md](imgui_port_report.md).
- Platform: [Platform layer](platform/platform_layer.md), [SDL removal plan](platform/sdl_removal_plan.md), [sdl_inventory.md](sdl_inventory.md), [Own HTTP client](platform/own_http.md), [OkayBackend design](platform/okay_backend.md), [OkayBackend audit](platform/okay_backend_audit.md), [OkayBackend admin dashboard design](platform/okay_backend_admin.md).
- Tools: [Own build tool](platform/own_build_plan.md), [okay-cc](platform/own_compiler_plan.md), [okay-ms](platform/own_msvc_plan.md).
- Models: [Model import](content/model_import.md), [Own model readers](content/model_readers.md), [Real-file tests](content/model_corpus.md), [Speed](content/own_readers_speed.md), [What is left](content/own_readers_remaining.md).
- Sound and pictures: [Own WAV decoder](audio/own_wav_decoder.md), [Own texture library](rendering/own_texture_library.md).
- Crypto (off by default, unreviewed): [crypto](platform/crypto.md), [review package](platform/crypto_review_package.md), [review request](platform/crypto_review_request.md).

**Status and audits**
- [UNFINISHED.md](status/UNFINISHED.md), [unfinished_engine.md](status/unfinished_engine.md), [unfinished_launcher.md](status/unfinished_launcher.md).
- [foundations_gap.md](status/foundations_gap.md), [player_audit.md](status/player_audit.md), [modeling_tab_audit.md](status/modeling_tab_audit.md), [camera_upgrade.md](status/camera_upgrade.md), [provenance_audit.md](status/provenance_audit.md).

## For release and operations

How builds reach players and how the online services run.

- [Releasing](release/releasing.md): the normal path, the one-dispatch fast release, test builds, the official game.
- [Release channels](release/release_channels.md): latest, LTS, and test builds.
- [Packaging](release/packaging.md): the Windows folder layout.
- [The updater](release/updater.md): checksums, all-or-nothing swap, rollback.
- [Install, update and uninstall](release/install.md) and [Launcher versions](release/launcher_versions.md).
- [Releasing on Steam](release/steam_release.md).
- [Security](online/security.md), [Crash reports](online/crash_reports.md), [Account data](online/account_data.md), [Password reset](online/password-reset.md), [Member moderation](online/member_moderation.md).
- [Net providers](online/net_providers.md) and [Photon](online/photon.md): extension points only, off by default.
- [sql/](sql/): the database scripts.

## Generated pages (look things up, do not edit)

These are rebuilt on `main` by scripts (`.github/workflows/regen-docs.yml`), never edited by hand and never committed in
a pull request. To change one, change its source.

| Page | Made by | Source |
|---|---|---|
| [SYSTEMS.md](SYSTEMS.md) | `scripts/gen_feature_map.py` | the `System:` lines of [feature_map/](feature_map/) pages |
| [FEATURE_MAP.md](FEATURE_MAP.md), `feature_map/_index_*.md` | `scripts/gen_feature_map.py` | [feature_map/](feature_map/) pages (format in [feature_map/README.md](feature_map/README.md)) |
| [engine/code_index.md](engine/code_index.md) | `scripts/gen_code_index.py` | the source files |
| [try_it.md](try_it.md) | `scripts/gen_try_it.py` | [try_it/](try_it/) (no longer required, rule 4a; kept) |
| [verification/README.md](verification/README.md) | `scripts/gen_verify_checklist.py` | the feature map |
| [engine_api.md](engine_api.md) | `test_vs_types` | the script bindings |
| [visual_scripting_nodes.md](visual_scripting_nodes.md), [visual_scripting_topics.md](visual_scripting_topics.md), [visual_script_category_audit.md](visual_script_category_audit.md), [visual_scripting_naming.md](visual_scripting_naming.md), [visual_script_coverage.md](visual_script_coverage.md) | Visual Script tools | the node library |
| [ui_catalogue.md](ui_catalogue.md) | `scripts/gen_ui_catalogue.py` | the UI components |
| [imgui_port_report.md](imgui_port_report.md) | `scripts/imgui_usage_report.py` | the editor and launcher code |
| [sdl_inventory.md](sdl_inventory.md) | `scripts/sdl_inventory.py` | the SDL2 calls left |
| `WORK_BOARD.md` on the `work-board` branch | `work-board.yml` | GitHub |

## The folders

Each folder has a `README.md` that lists every page with one line each.

| Folder | What is in it |
|---|---|
| [status/](status/README.md) | What is going on and what is left: the work board, the unfinished lists, the plan to replace outside libraries, where code came from. |
| [process/](process/README.md) | How the work is done: the three Claudes, agent briefs, reviews, testing, QA, okay_run. |
| [engine/](engine/README.md) | The core: runtime, time, maths, scenes, prefabs, assets, saving, input, languages, settings, architecture, debug and speed tools. |
| [gameplay/](gameplay/README.md) | Game pieces: animation, IK, ragdolls, cameras, AI and paths, dialogue, cutscenes, vehicles, weapons, survival, wind, your first game. |
| [rendering/](rendering/README.md) | How things look: the renderers and their status, lighting, terrain and water, particles, textures, shaders. |
| [physics/](physics/README.md) | Physics in one page, the physics core, the collider upgrade. |
| [audio/](audio/README.md) | Sound, our own WAV decoder, the voice server. |
| [scripting/](scripting/README.md) | OkayScript, Visual Script, the shared table of engine functions. |
| [editor/](editor/README.md) | The editor: panels, menus, undo, projects, level tools, modeling, Build Settings. |
| [ui/](ui/README.md) | In-game UI, and OkayUI (our own UI toolkit). |
| [online/](online/README.md) | Multiplayer, servers, accounts, the Supabase backend, anti-cheat, security, crash reports. |
| [platform/](platform/README.md) | Our own platform layer, build tool, compilers, crypto, HTTP client, Xbox and web exports. |
| [content/](content/README.md) | Getting models in: import, our own model readers, real-file tests. |
| [release/](release/README.md) | Getting builds to players: build pipeline, packaging, releasing, channels, the updater, installing, downloadable content. |
| [verification/](verification/README.md) | The owner's test checklists for the Windows release (generated). |
| [design/](design/modular_gameplay_contracts.md) | The modular engine: [the design](design/modular_gameplay_contracts.md) and [the state of each stage](design/modular_status.md); Live Updates: [the foundations](design/live_updates.md) (no folder README). |
| [feature_map/](feature_map/README.md) | The hand-written feature map pages, one per area; the source of FEATURE_MAP.md and SYSTEMS.md. |
| [try_it/](try_it/) | Old "how to try it" entries, the source of try_it.md. |

## Notes

- Pictures are not stored in the repository (owner rule, 2026-11-17). Pages describe them in words.
- Keep this page in step when a page is added or moved (CLAUDE.md rule 4b).
- The old index linked an `archive/` folder of old reports. That folder is not in the repository, so the link was dropped.
