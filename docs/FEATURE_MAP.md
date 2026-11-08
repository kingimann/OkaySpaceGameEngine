# OkaySpace feature map

Say "work on area 5" (or its name) and the row tells me where the code, docs and
editor window for it live and what is still open. Built from the repo at
version 2.845.0 (engine/include/okay/*, docs/*, editor/*, ROADMAP.md).

"Open" lists come from ROADMAP.md and my task list. The task list is old in
places, so treat an open item as "ask before assuming it is still open".

| # | Area | One line |
|---|---|---|
| 1 | Core & scene | Game loop, scenes, GameObjects, saving, localization |
| 2 | Rendering & world | Renderers, lighting, sky, weather, terrain, water, particles |
| 3 | 3D modeling & model import | Modeling tools, glTF/FBX import, rigs |
| 4 | Characters, controllers & cameras | Player controllers, stances, cameras |
| 5 | Animation & IK | Animator, clips, humanoid retarget, IK, ragdolls, held objects |
| 6 | Physics | 2D and 3D bodies, colliders, joints |
| 7 | Audio | Mixer, music, sound sources |
| 8 | Input | Keyboard, mouse, gamepad, input actions, rebinding |
| 9 | AI & navigation | Nav meshes, pathfinding, behavior trees, NPCs |
| 10 | Story & cinematics | Cutscenes, dialogue, quests |
| 11 | Gameplay systems | Inventory, crafting, survival, building, spawners |
| 12 | In-game UI | Canvas UI, widgets, HTML UI, world-space UI |
| 13 | Scripting | OkayScript, the C#-style language, Lua, script editor |
| 14 | Visual scripting | Node graphs and the action framework |
| 15 | Editor | The editor app and its windows |
| 16 | Networking & multiplayer | Netcode, servers, relay, anti-cheat, voice |
| 17 | Launcher & online platform | Accounts, friends, chat, marketplace, forums, profiles |
| 18 | Build, export & release | Packaging, CI, releases, updater |
| 19 | Docs, tests & code health | README, tests, file splits, QA workflow |

---

## 1. Core & scene
- **Covers:** application and game loop, time, scheduler, tweens, event bus,
  scenes and scene manager, GameObject and component model, ECS bridge,
  scene serializer (`okayscene 3`), scene templates (starter players and
  worlds), prefabs, save games, prefs, data packs, profiler, localization
  runtime (string tables, plurals, font fallback).
- **Code:** `engine/include/okay/Core`, `Scene`, `ECS`, `Math`.
- **Docs:** `making_a_game.md`, `save_games.md`, `localization.md`, `prefabs.md`,
  `scene_serializer.md`.
- **Editor:** Scene Manager panel, Localization window.
- **Open:** Scene Manager window polish; move the remaining components onto the
  component serializer registry (only `trailrenderer` is on it so far; the rest
  is still one very large if/else chain); editor Apply/Revert wiring for prefab
  overrides; component order is not saved; old-scene migration whenever a
  component changes shape.

## 2. Rendering & world
- **Covers:** OpenGL, D3D11, D3D12, software and console renderers; materials,
  procedural textures; lighting, shadows, lightmaps, day/night cycle;
  post-processing (exposure, bloom, grading, AA, AO, depth of field),
  procedural sky, fog, weather and wind; frustum culling, GPU skinning;
  terrain, voxel terrain and digging, water; particle system; line and trail
  renderers; fonts.
- **Code:** `Render`, `Graphics`, `Environment`; components `Light`,
  `MeshRenderer`, `Terrain`, `VoxelTerrain`, `Water`, `ParticleSystem`,
  `DayNightCycle`, `LineRenderer`, `WorldStreamer`.
- **Docs:** `rendering.md`, `lighting.md`, `day_night_cycle.md`.
- **Editor:** Lighting panel, Scene and Game views.
- **Open:** Editor Play frame rate in software GL (24-32 FPS vs ~100 in Edit,
  unconfirmed on real GPUs); per-face materials need renderer submeshes (see 3).

## 3. 3D modeling & model import
- **Covers:** in-editor modeling (edit mode, loop cut, bevel, knife, bridge,
  modifier stack, UV seams and unwrap/packing, `.okaymesh` assets); model
  import (glTF, FBX/OBJ via Assimp), skins and blend shapes, importer fixes,
  Rig Model, Make Playable Character, Set Character Model.
- **Code:** `Modeling`, `Render/ModelImport`, `Render/Gltf`.
- **Docs:** `modeling.md`.
- **Editor:** Modeling window, Project panel import, Import Model dialog.
- **Open:** M5b per-face materials; inside-out primitives (Sphere, Cylinder,
  Cone, Tube, Torus, TorusKnot, Capsule); one-click humanoid import on any
  rig.

## 4. Characters, controllers & cameras
- **Covers:** the single **Player Controller** (View = First, Third or Both),
  First/Third Person controllers, shooter and top-down controllers, 2D and
  2.5D controllers, click-to-move, vehicle controllers, free roam; stances
  (stand, crouch, prone, crawl), step-up, rotating platforms, jump, wall
  slide; third-person spring-arm camera, first-person camera feel, Cinemachine-
  style virtual cameras and dolly paths; the blocky default Character.
- **Code:** `Components/PlayerController`, `FirstPersonController`,
  `ThirdPersonController`, `Stance`, `StepUp`, `PlayerView`, `ControllerFeel`,
  `VehicleController`, `Camera*`, `VirtualCamera`, `Character`.
- **Docs:** `shooter_and_25d_controllers.md`, `vehicle_controller.md`,
  `making_a_game.md` ("The Player Controller").
- **Editor:** Player Controller panel, Character Model panel.
- **Open:** improve the 2.5D controller; blocky Character arms (first person);
  prone option on first and third person (task list; prone exists since
  2.834, so verify before starting); player customization options.

## 5. Animation & IK
- **Covers:** Animator and animation state machines (layers, blend trees,
  2D blend), clips and curves, model clip player (stride matching, built-in
  fallback), humanoid bone mapper and retarget, built-in humanoid library,
  root motion, skinning (linear and dual quaternion), foot IK, limb, chain,
  look-at, aim and full-body IK, ragdolls and hit reactions, first-person
  hands, procedural object holding.
- **Code:** `Animation`, `Components/Animator`, `AnimStateMachine`,
  `ModelAnimator`, `HumanoidRetarget`, `FootIK`, `LimbIK`, `ChainIK`,
  `LookAtIK`, `FullBodyIK`, `Ragdoll`, `FirstPersonHands`,
  `ProceduralObjectHolding`.
- **Docs:** `animation.md`, `ik.md`, `ragdolls.md`.
- **Editor:** Animator window, Animation tab (dope sheet, curves), FP Hands
  window, Timeline preview.
- **Open (ROADMAP):** rest-pose normalisation and retargeting by avatar slot;
  one-click humanoid import with the default library on any rig and a
  character script API. **Also:** backpedal and strafe animations; joint
  hinges and model detail; procedural holding PRs 3-4 (body adjust,
  multiplayer sync). **Recent:** PR #541 (draft) fits default move speeds to
  a Mixamo model's walk/run clips; "two components both called Animator"
  on a player is the base and second built-in graph layers, labels could be
  clearer.

## 6. Physics
- **Covers:** 2D and 3D rigidbodies, colliders, joints, collider fit,
  fixed-step physics with render interpolation, pose history for lag
  compensation, physics debug draw, destructibles, projectiles.
- **Code:** `Physics`, `Components/Rigidbody*`, `Joint*`, `Destructible`.
- **Docs:** no dedicated physics doc.
- **Open:** none tracked.

## 7. Audio
- **Covers:** stereo mixer and Audio Mixer, WAV/OGG/MP3, music player, audio
  sources, saved volume settings, scene audio loading.
- **Code:** `Audio`, `Components/AudioSource`, `MusicPlayer`.
- **Docs:** `audio.md`.
- **Editor:** Audio Mixer window, Settings.
- **Open:** none tracked.

## 8. Input
- **Covers:** keyboard, mouse, gamepad (SDL), input actions and rebinding UI,
  cursor handling.
- **Code:** `Input`, `Components/ControllerFeel`, `UIKeybindField`.
- **Docs:** `input.md`.
- **Open:** none tracked.

## 9. AI & navigation
- **Covers:** navigation meshes (bake, queries, agents, obstacles), the older
  grid pathfinder, behavior trees (assets, blackboard, perception, runner,
  Start Dialogue task), NPC controllers, click-to-move.
- **Code:** `AI`, `Components/NavMeshAgent`, `NavMeshSurface`,
  `NavMeshObstacle`, `BehaviorTreeRunner`, `NPCController`.
- **Docs:** `navigation.md`, `behavior_trees.md`, `npcs_and_crafting.md`.
- **Editor:** Navigation window, Behavior Tree window.
- **Open:** none tracked.

## 10. Story & cinematics
- **Covers:** cutscene sequences and the Sequence Player, dialogue graphs and
  runner, story variables, quests (objectives, prerequisites, rewards),
  dialogue box, quest tracker and journal.
- **Code:** `Components/Sequence`, `SequencePlayer`, `DialogueGraph`,
  `DialogueRunner`, `QuestLog`, `StoryState`.
- **Docs:** `cutscenes.md`, `dialogue_quests.md`.
- **Editor:** Timeline window, Dialogue window.
- **Open:** none tracked.

## 11. Gameplay systems
- **Covers:** inventory (bag, grid, chests, equipment, consumables, item
  database, loot tables, recipes, persistence), crafting, survival (stats,
  afflictions, zones), building (build mode, structure builder, snapping),
  spawners, turn manager, minimap, pickups, melee, dodge and dive actions.
- **Code:** `Inventory`, `Components/Inventory*`, `Crafting*`, `Survival*`,
  `BuildMode`, `StructureBuilder`, `Spawner`, `MeleeAttacker`.
- **Docs:** `survival_kit.md`, `build_mode.md`, `npcs_and_crafting.md`.
- **Open (ROADMAP, inventory plan):** gameplay features, scripting, UI
  polish, server-authoritative networking; Unturned-style inventory
  improvements.

## 12. In-game UI
- **Covers:** canvas UI and the widget set (buttons, sliders, inputs, lists,
  dropdowns, tabs, modals, context menu, toasts, tooltips, keybind field,
  scroll views, grid slots), layout (anchors, flex, safe area, mobile
  scaling), HTML UI, world-space UI, themes, the launcher-matching UI kit,
  loading screens, pause menu and settings screen.
- **Code:** `UI`, `Components/UI*`, `Canvas`, `WorldSpaceUI`, `PauseMenu`,
  `SettingsScreen`, `LoadingScreen`.
- **Docs:** `game_ui.md`, `ui_html.md`, `ui_world_space.md`.
- **Editor:** UI mode in the Scene view, UI inspector overlay.
- **Open:** full UI component library (essential, game, app, layout,
  data-driven); the UI plan P0-P4 (UIFrame, modals, 2-D navigation, touch).

## 13. Scripting
- **Covers:** OkayScript (the VM, engine API, script editor with
  autocomplete, errors, rename, format), the newer C#-style language core,
  C# and Lua VMs, script components, `engine_api.md` generated from the API.
- **Code:** `Scripting`, `Components/ScriptComponent`; editor Script Editor.
- **Docs:** `SCRIPTING.md`, `scripting.md`, `engine_api.md` (regenerate with
  `OKAY_WRITE_API_DOCS=1 ./tests/test_vs_types`).
- **Editor:** Script Editor tab.
- **Open:** language P2-P5 (classes, physics/events, shared layer with
  visual scripting, debugger, export, replace the old VM); lambdas, events
  and inheritance; Script editor "meaningful syntax only" cleanup; classes
  and templates for New Script; scripting bug list 1-15.

## 14. Visual scripting
- **Covers:** node graph editor with typed pins, functions, loops, variables
  and scopes, object references, debugger (breakpoints, step, watch), the
  action framework, many node waves (physics, story, build, math).
- **Code:** `VisualScript`, `Components/VisualScriptComponent`, `NoCode`.
- **Docs:** `visual_scripting.md`.
- **Editor:** Visual Script editor.
- **Open:** docs update for Visual Script and Script Editor.

## 15. Editor
- **Covers:** Hierarchy, Inspector (Add Component menu, Character Model
  panel), Scene and Game views, Project panel and asset manager, Console,
  Animator, Timeline, Navigation, Behavior Tree, Dialogue, Localization and
  Modeling windows, spline tools, Build Settings, plugin system, editor
  layout, undo, autosave.
- **Code:** `editor/*` (note `editor/main.cpp` is ~45k lines).
- **Docs:** `editor.md`, `editor-plugins.md`.
- **Open:** split `editor/main.cpp` into panel files (in progress);
  Hierarchy, Inspector, Project panel polish; Plugins menu; Scene Manager.

## 16. Networking & multiplayer
- **Covers:** UDP netcode with sealed channels, server authority, client
  prediction, lag compensation, interest, snapshots, matchmaking and queues,
  server browser, dedicated server and export, relay, session tickets,
  network player spawning, voice chat server, and the **anti-cheat** plan.
- **Code:** `Net`, `ECS/NetWorld*`, `Components/Network*`, `NetValidator`,
  `launcher/VoiceChat.hpp`.
- **Docs:** `dedicated-server.md`, `relay.md`, `anticheat.md`,
  `voice-server.md`, `launcher-multiplayer.md`, `photon.md`.
- **Open (ROADMAP, anti-cheat hardening):** 5 bans at ticket issue, 6 tickets
  bound to a server, 8 budgets survive reconnect, 9 join checks, 10 message
  and spawn limits, 12 lag-comp inputs, 13 safe defaults, 14 small fixes.
  Items 5 and 6 and some SQL / Edge-function changes **wait for your OK**.

## 17. Launcher & online platform
- **Covers:** launcher app (Play and Create pages, library, updater, crash
  reports, What's New), accounts and sign-in, friends and following, chat
  (DMs, groups, voice notes, calls, polls, GIFs, emoji, markdown), forums,
  marketplace and Workshop, profiles, badges, achievements, parties, server
  browser, admin tools, notifications, privacy settings, Steam and PlayFab.
- **Code:** `launcher/main.cpp` (very large), `engine/.../Platform`.
- **Docs:** `accounts.md`, `supabase-backend.md`, `launcher-multiplayer.md`,
  `steam_release.md`; SQL in `docs/sql`.
- **Open:** admin test servers without a game; polish pass and bug fixes;
  split `launcher/main.cpp`; security plan SEC1 (relay, script paths,
  updater signing, reactions SQL).

## 18. Build, export & release
- **Covers:** Windows build and installer, WebGL and mobile export, dedicated
  server export, the five CI jobs (Ubuntu, ASan/UBSan, MinGW, Windows export
  check, WebGL), release workflow, Supabase publish and updater feed.
- **Code:** `scripts/*`, `.github/workflows/*`, `CMakeLists.txt`.
- **Docs:** `packaging.md`, `releasing.md`, `web_mobile.md`, `steam_release.md`.
- **Open:** publish 2.843.0-2.845.0 to Supabase (feed reads 2.842.0); remove the
  hardcoded Supabase URL/key from CMake; stop committing dist binaries.

## 19. Docs, tests & code health
- **Covers:** README and per-feature docs, the 314 registered tests, QA
  workflow, rig sweep test, file splitting.
- **Docs:** `qa_workflow.md`.
- **Open:** README matches reality plus a component audit doc; split the
  largest files (editor, launcher, OkayScript VM).

---

## Currently moving (as of 2026-11-08)
- PR #541 (draft): walk/run speeds fitted to a Mixamo model's clips. CI not yet checked.
- Main is at 2.845.0 (includes #528: VRoid, Character Creator and Daz rigs, the
  crouch-height fix and the rig sweep test).
- The public update feed (Supabase `VERSION.txt`) reads 2.842.0, so 2.843.0-2.845.0
  have not been published there. See area 18.
