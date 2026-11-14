# OkaySpace documentation

Pages are grouped by what you are trying to do. New here? Start with
[making_a_game.md](making_a_game.md).

## Get started
- [THIRD_PARTY.md](THIRD_PARTY.md): what is custom code and which outside libraries we build on, with licences.
- [own_code_roadmap.md](own_code_roadmap.md): the plan to replace and remove every third-party library, with the criteria each must meet.
- [crypto_review_package.md](crypto_review_package.md): what an outside security reviewer needs to review our own crypto.
- [UNFINISHED.md](UNFINISHED.md): what is not finished yet, hidden or limited today, and the to-do list.
- [making_a_game.md](making_a_game.md): make your first game, from an empty editor to a shipped one.
- [prefabs.md](prefabs.md): prefab instances, overrides and persistent object ids.
- [prefab_system.md](prefab_system.md): nested prefabs, variants, unpack, per-record and per-field apply and revert, and the prefab validator.
- [prefab_mode.md](prefab_mode.md): open a prefab on its own in the editor, edit it (nested prefabs too), save it, and see every instance update.
- [scene_serializer.md](scene_serializer.md): what a scene round trip keeps, and the component serializer registry.
- [properties.md](properties.md): the property registry: read, write, watch, snapshot and diff any component field by name (the base for per-property undo, live editing and prefab overrides).
- [scene_diff_merge.md](scene_diff_merge.md): canonical scene text, structured diffs, three-way merge and format migration for version control.
- [editor.md](editor.md): the editor, its panels and Build Game.
- [project_panel.md](project_panel.md): the Project panel.
- [editor_undo.md](editor_undo.md): editor undo and redo.
- [build_pipeline.md](build_pipeline.md): the build pipeline.
- [platform_layer.md](platform_layer.md): the okay::platform seam over SDL2, our own Linux / Windows backends, the SDL audit and what is tested.
- [project_upgrade.md](project_upgrade.md): the engine version a project was saved with, the upgrade question when an editor of another version opens it, safe upgrade with a full backup, restore, and the launcher's Versions folder.
- [undo_recovery.md](undo_recovery.md): command based undo, autosave and crash recovery, rolling file backups.
- [web_mobile.md](web_mobile.md): web (WASM) and mobile builds.
- [packaging.md](packaging.md): how a build is laid out for distribution.
- [releasing.md](releasing.md): releasing an update to players.
- [steam_release.md](steam_release.md): releasing on Steam.
- [release_channels.md](release_channels.md): the latest and LTS channels, the update bucket, versions.json, how to cut an LTS.
- [launcher_versions.md](launcher_versions.md): the launcher's Versions page: install, remove and keep other editor versions next to the running one, the Latest / LTS channel setting, project version badges and Open with, `--versions`.

## Scripting
- [scripting.md](scripting.md): the OkayScript language guide.
- [scripting_reference.md](scripting_reference.md): built-in functions, area by area (networking, Steam, JSON, collections, timers, physics events).
- [visual_scripting.md](visual_scripting.md): the visual scripting text format (OkayVS) and Action Lists.
- [engine_api.md](engine_api.md): the engine API reference.
- [core_logging_profiling.md](core_logging_profiling.md): logging (categories, file sink, OKAY_LOG) and the frame profiler (trace and CSV export).
- [debug_tools.md](debug_tools.md): profiling and debugging data layers (frame timeline, GPU and render stats, memory tracking, frame debugger, error console, stats overlay).
- [automated_testing.md](automated_testing.md): play-mode tests (`PlayTest`, `okay-playtest`), scene validation and golden-file / golden-image regression helpers.
- [core_runtime.md](core_runtime.md): the job system, memory arenas and pools, generational handles, string ids, console variables and frame pacing.
- [editor-plugins.md](editor-plugins.md): extend the editor with plugins.

## Characters and animation
- [animation.md](animation.md): keyframe clips, imported animation, the Animator, built-in animations.
- [ik.md](ik.md): inverse kinematics (feet, hands, look-at, full body).
- [ragdolls.md](ragdolls.md): ragdolls and hit reactions.
- [input.md](input.md): input actions, controllers and rebinding.
- [shooter_and_25d_controllers.md](shooter_and_25d_controllers.md): the top-down shooter and 2.5D platformer controllers.
- [vehicle_controller.md](vehicle_controller.md): the arcade vehicle controller.

## Gameplay systems
- [survival_kit.md](survival_kit.md): health, hunger, thirst, stamina, oxygen and temperature components.
- [npcs_and_crafting.md](npcs_and_crafting.md): NPCs and crafting.
- [inventory.md](inventory.md): Inventory status, moves between bags, weight limits and what is missing.
- [navigation.md](navigation.md): navigation meshes and agents.
- [physics.md](physics.md): hinge limits for doors and levers, moving mesh colliders, and 2D box and capsule casts.
- [volumes.md](volumes.md): box, sphere and cylinder zones: checkpoint, kill, force, fog and sound, drawn and resized in the Scene view.
- [behavior_trees.md](behavior_trees.md): behavior trees for NPC AI.
- [dialogue_quests.md](dialogue_quests.md): dialogue graphs, story variables and quests.
- [cutscenes.md](cutscenes.md): sequences and the Timeline window.
- [timers_and_tweens.md](timers_and_tweens.md): timers that survive scene loads, cancel by tag, and tween behaviour.
- [loop_and_time.md](loop_and_time.md): the shared frame driver (`GameLoop`), frame stepping, debug slow motion, fixed-rate capture, recording and replaying frame times, frame statistics, hit stop and time-scale ramps, clock channels, pause reasons and quit guard, exit codes, the server tick pacer, dice and shuffle bags.
- [build_mode.md](build_mode.md): let players build in your game, logic links, saving creations.
- [save_games.md](save_games.md): save and load the whole game.
- [save_framework.md](save_framework.md): saveable objects, checkpoints, quick save and autosave slots, slot headers, migrations, sync hooks, load menus.
- [day_night_cycle.md](day_night_cycle.md): the day and night cycle.
- [splines.md](splines.md): splines, followers, lines and trails.
- [level_tools.md](level_tools.md): foliage brush, terrain texture layers, decal projector, cable, measuring tool, orthographic views.

## Look, models and sound
- [rendering.md](rendering.md): post-processing, sky, fog and weather.
- [render_status.md](render_status.md): an honest, row-by-row status of what the software, OpenGL, Direct3D 11 and Direct3D 12 renderers each draw today, with the file and function that proves it.
- [lighting.md](lighting.md): lighting and shadows.
- [modeling.md](modeling.md): the built-in mesh editor.
- [optimization_tools.md](optimization_tools.md): LOD generation, occlusion culling, batching, streaming and the scene optimization report.
- [audio.md](audio.md): the mixer, music and sound effects.

## Models and importing
- [model_import.md](model_import.md): which reader handles which model format, and the options that switch to our own readers.
- [model_corpus.md](model_corpus.md): real-file checks of our own model readers against the outside libraries, and the flip-readiness verdict.
- [own_fbx_reader.md](own_fbx_reader.md), [own_3ds_reader.md](own_3ds_reader.md), [own_bvh_reader.md](own_bvh_reader.md): our own FBX, 3DS and BVH readers.
- [own_off_reader.md](own_off_reader.md), [own_smd_reader.md](own_smd_reader.md), [own_md5_reader.md](own_md5_reader.md), [own_ms3d_reader.md](own_ms3d_reader.md), [own_ase_reader.md](own_ase_reader.md): our own OFF, SMD, MD5, MS3D and ASE readers.
- [own_lwo_reader.md](own_lwo_reader.md): our own LWO reader.
- [own_dxf_reader.md](own_dxf_reader.md): our own DXF reader.
- [own_x3d_reader.md](own_x3d_reader.md): our own X3D reader.
- [own_amf_reader.md](own_amf_reader.md): our own AMF reader.
- [own_3mf_reader.md](own_3mf_reader.md): our own 3MF reader (it also describes the XML and zip readers that AMF and X3D share).
- [data_assets.md](data_assets.md): data assets and data packs.
- [particles.md](particles.md): particles, lines and trails.
- [terrain_water.md](terrain_water.md): terrain and water.
- [display_settings.md](display_settings.md): display settings.

## Game UI
- [game_ui.md](game_ui.md): game UI in the launcher's look.
- [minimap.md](minimap.md): the corner map (blips, zoom, heading-up, a map you draw) and turn order.
- [ui_runtime.md](ui_runtime.md): the twelve UI looks, widget roles, radio buttons, windows, damage numbers, dialogue box, UI Gallery and the script and C++ calls.
- [ui_html.md](ui_html.md): menus and HUDs in an HTML and CSS subset.
- [ui_world_space.md](ui_world_space.md): in-world (3D) UI.
- [localization.md](localization.md): string tables, translators and fonts.
- [localization_tools.md](localization_tools.md): translation grid, CSV and JSON, the table checker, key scanner, language previews and font coverage.
- [own_ui_toolkit.md](own_ui_toolkit.md): OkayUI, our own immediate-mode UI toolkit (off by default), its widgets and render backends.
- [own_ui_code_editor.md](own_ui_code_editor.md): the OkayUI code-editor widget.
- [accessibility.md](accessibility.md): subtitles and captions, colour vision, controller navigation, remapping and accessibility settings.

## Multiplayer, accounts and servers
- [launcher-multiplayer.md](launcher-multiplayer.md): multiplayer from the launcher.
- [relay.md](relay.md): the relay and NAT traversal.
- [dedicated-server.md](dedicated-server.md): dedicated servers.
- [voice-server.md](voice-server.md): the voice server.
- [photon.md](photon.md): the opt-in Photon Realtime transport.
- [net_providers.md](net_providers.md): the Photon, PlayFab and Custom provider stubs are C++ extension points only.
- [accounts.md](accounts.md): player accounts.
- [anticheat_quickstart.md](anticheat_quickstart.md): add anti-cheat to your game in 5 minutes (presets, checklist), and what it does not do.
- [anticheat.md](anticheat.md): anti-cheat.
- [supabase-backend.md](supabase-backend.md): the Supabase backend.
- [updater.md](updater.md): the updater: integrity, signed feeds, self-update and rollback.
- [account_data.md](account_data.md): download my data and delete my account.
- [member_moderation.md](member_moderation.md): kick, ban and admins in community servers and group chats.
- [password-reset.md](password-reset.md): the password reset page.
- [multiplayer_audit.md](multiplayer_audit.md): what the multiplayer audit found, fixed and left open.
- [crypto.md](crypto.md): our own crypto (off by default, not yet reviewed by outsiders).

## Core and scene
- [scene_hierarchy_search.md](scene_hierarchy_search.md): active state that follows the hierarchy, find and search helpers, scene validation.
- [scene_management.md](scene_management.md): loading scenes, spawning objects, pools and declaring what a component needs.
- [layers_tags_cameras.md](layers_tags_cameras.md): layers and the collision matrix, tags, project tag and layer lists, the camera stack.
- [ecs.md](ecs.md): the opt-in ECS: safe iteration, the command buffer, delta snapshots and the scene bridge.
- [core_tools.md](core_tools.md): small dependency-free helpers in `okay/Core/`.
- [math_geometry.md](math_geometry.md): math, geometry, noise and random numbers.
- [localization_prefs_time.md](localization_prefs_time.md): localization formats, typed prefs and the fixed timestep.
- [asset_manager.md](asset_manager.md): the opt-in registry for files loaded from disk.
- [asset_database.md](asset_database.md): persistent asset GUIDs, move tracking, the dependency graph and import settings.
- [core_audit_notes.md](core_audit_notes.md): what a bug hunt over the core found and fixed.

## Working on the engine
- [FEATURE_MAP.md](FEATURE_MAP.md): every system in the engine, where its code, tests and docs are, and what is still open (the pages live in [feature_map/](feature_map/)).
- [unfinished_engine.md](unfinished_engine.md) and [unfinished_launcher.md](unfinished_launcher.md): plain-English lists of what is unfinished in the engine and the launcher.
- [WORK_BOARD.md](WORK_BOARD.md): who is working on what.
- [visual_scripting_nodes.md](visual_scripting_nodes.md), [visual_script_coverage.md](visual_script_coverage.md): the visual scripting nodes and how well they are covered.
- [visual_script_clarity_audit.md](visual_script_clarity_audit.md), [visual_scripting_naming.md](visual_scripting_naming.md): audits of how clear the visual scripting nodes and their names are.
- [../CLAUDE.md](../CLAUDE.md): the owner's standing rules for every session and agent (merging, CI, releases, secrets, own-code goal, disk).
- [qa_workflow.md](qa_workflow.md): a developer and a QA session working together.
- [../ROADMAP.md](../ROADMAP.md): releases and plans.
- [archive/](archive/): old reports kept for history.
