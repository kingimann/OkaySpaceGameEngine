# OkaySpace documentation

Pages are grouped by what you are trying to do. New here? Start with
[making_a_game.md](making_a_game.md).

## Get started
- [making_a_game.md](making_a_game.md): make your first game, from an empty editor to a shipped one.
- [editor.md](editor.md): the editor, its panels and Build Game.
- [web_mobile.md](web_mobile.md): web (WASM) and mobile builds.
- [packaging.md](packaging.md): how a build is laid out for distribution.
- [releasing.md](releasing.md): releasing an update to players.
- [steam_release.md](steam_release.md): releasing on Steam.

## Scripting
- [scripting.md](scripting.md): the OkayScript language guide.
- [scripting_reference.md](scripting_reference.md): built-in functions, area by area (networking, Steam, JSON, collections, timers, physics events).
- [visual_scripting.md](visual_scripting.md): the visual scripting text format (OkayVS) and Action Lists.
- [engine_api.md](engine_api.md): the engine API reference.
- [core_logging_profiling.md](core_logging_profiling.md): logging (categories, file sink, OKAY_LOG) and the frame profiler (trace and CSV export).
- [debug_tools.md](debug_tools.md): profiling and debugging data layers (frame timeline, GPU and render stats, memory tracking, frame debugger, error console, stats overlay).
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
- [navigation.md](navigation.md): navigation meshes and agents.
- [behavior_trees.md](behavior_trees.md): behavior trees for NPC AI.
- [dialogue_quests.md](dialogue_quests.md): dialogue graphs, story variables and quests.
- [cutscenes.md](cutscenes.md): sequences and the Timeline window.
- [timers_and_tweens.md](timers_and_tweens.md): timers that survive scene loads, cancel by tag, and tween behaviour.
- [build_mode.md](build_mode.md): let players build in your game, logic links, saving creations.
- [save_games.md](save_games.md): save and load the whole game.
- [day_night_cycle.md](day_night_cycle.md): the day and night cycle.
- [splines.md](splines.md): splines, followers, lines and trails.

## Look, models and sound
- [rendering.md](rendering.md): post-processing, sky, fog and weather.
- [lighting.md](lighting.md): lighting and shadows.
- [modeling.md](modeling.md): the built-in mesh editor.
- [audio.md](audio.md): the mixer, music and sound effects.

## Game UI
- [game_ui.md](game_ui.md): game UI in the launcher's look.
- [ui_html.md](ui_html.md): menus and HUDs in an HTML and CSS subset.
- [ui_world_space.md](ui_world_space.md): in-world (3D) UI.
- [localization.md](localization.md): string tables, translators and fonts.

## Multiplayer, accounts and servers
- [launcher-multiplayer.md](launcher-multiplayer.md): multiplayer from the launcher.
- [relay.md](relay.md): the relay and NAT traversal.
- [dedicated-server.md](dedicated-server.md): dedicated servers.
- [voice-server.md](voice-server.md): the voice server.
- [photon.md](photon.md): the opt-in Photon Realtime transport.
- [accounts.md](accounts.md): player accounts.
- [anticheat.md](anticheat.md): anti-cheat.
- [supabase-backend.md](supabase-backend.md): the Supabase backend.

## Working on the engine
- [qa_workflow.md](qa_workflow.md): a developer and a QA session working together.
- [../ROADMAP.md](../ROADMAP.md): releases and plans.
- [archive/](archive/): old reports kept for history.
