# OkaySpace feature map

Say "work on 5.3" (a sub-area) or "work on area 5" (all of its sub-areas) and
the row tells me where the code, docs and editor window live and what is still
open. Built from the repo at version 2.847.0 (engine/include/okay/*, docs/*,
editor/*, ROADMAP.md).

"Open" lists come from ROADMAP.md and the task list. The task list is old in
places, so treat an open item as "ask before assuming it is still open".
Paths are under `engine/include/okay/` unless they start with `editor/`,
`launcher/`, `docs/` or `scripts/`. Components live in `Components/`.

## Areas

| # | Area | Sub-areas |
|---|---|---|
| 1 | Core & scene | 1.1 Loop and time, 1.2 Timers and tweens, 1.3 Scene graph, 1.4 Scene lifecycle, 1.5 Scene serializer, 1.6 Prefabs and templates, 1.7 Saves and prefs, 1.8 Localization, 1.9 Events and ECS, 1.10 Logging and profiling |
| 2 | Rendering & world | 2.1 Renderers, 2.2 Materials and textures, 2.3 Lighting and shadows, 2.4 Sky, fog and weather, 2.5 Post-processing, 2.6 Terrain and water, 2.7 Particles and lines, 2.8 Culling and skinning, 2.9 Fonts and images |
| 3 | 3D modeling & import | 3.1 Edit mode, 3.2 Edge and loop tools, 3.3 Modifiers, 3.4 UV tools, 3.5 Mesh assets and primitives, 3.6 Model import, 3.7 Rigging and playable setup |
| 4 | Characters & cameras | 4.1 Player Controller, 4.2 Stances and movement, 4.3 2D and 2.5D controllers, 4.4 Shooter and top-down, 4.5 Vehicles, 4.6 Third-person camera, 4.7 First-person camera and hands, 4.8 Virtual cameras and dollies, 4.9 Blocky Character and customization |
| 5 | Animation & IK | 5.1 Animator and state machines, 5.2 Clips and curves, 5.3 Model clip player and locomotion, 5.4 Humanoid retarget and library, 5.5 Root motion and skinning, 5.6 Foot, limb and chain IK, 5.7 Look-at, aim and full-body IK, 5.8 Ragdolls and hit reactions, 5.9 Object holding |
| 6 | Physics | 6.1 2D bodies and colliders, 6.2 3D bodies and colliders, 6.3 Joints, 6.4 Fixed step and interpolation, 6.5 Queries and debug draw, 6.6 Destructibles and projectiles |
| 7 | Audio | 7.1 Mixer, 7.2 Formats and clips, 7.3 Sources and music, 7.4 Audio settings |
| 8 | Input | 8.1 Keyboard and mouse, 8.2 Gamepad, 8.3 Input actions and rebinding, 8.4 Cursor and touch |
| 9 | AI & navigation | 9.1 Nav mesh bake and queries, 9.2 Nav agents and obstacles, 9.3 Grid pathfinding, 9.4 Behavior trees, 9.5 NPCs and perception |
| 10 | Story & cinematics | 10.1 Cutscene sequencer, 10.2 Dialogue, 10.3 Quests, 10.4 Story UI |
| 11 | Gameplay systems | 11.1 Inventory, 11.2 Items and loot, 11.3 Crafting, 11.4 Survival, 11.5 Building, 11.6 Spawners and turns, 11.7 Combat actions |
| 12 | In-game UI | 12.1 Canvas and layout, 12.2 Widgets, 12.3 Input widgets, 12.4 Lists, tabs and modals, 12.5 HTML UI, 12.6 World-space UI, 12.7 Themes and UI kit, 12.8 Loading, pause and settings screens, 12.9 Mobile UI |
| 13 | Scripting | 13.1 OkayScript VM, 13.2 C#-style language core, 13.3 C# and Lua, 13.4 Script components, 13.5 Script editor, 13.6 Engine API and docs |
| 14 | Visual scripting | 14.1 Graph editor, 14.2 Types and variables, 14.3 Functions and flow, 14.4 Node library, 14.5 Debugger, 14.6 Action framework |
| 15 | Editor | 15.1 Hierarchy, 15.2 Inspector, 15.3 Scene and Game views, 15.4 Project panel and assets, 15.5 Animation windows, 15.6 World-building windows, 15.7 Build Settings, 15.8 Plugins, 15.9 Layout, undo and autosave, 15.10 File splitting |
| 16 | Networking & multiplayer | 16.1 Transport and channels, 16.2 Authority and prediction, 16.3 Snapshots and sync, 16.4 Matchmaking and browser, 16.5 Dedicated server and relay, 16.6 Anti-cheat, 16.7 Voice, 16.8 Network players |
| 17 | Launcher & platform | 17.1 Launcher shell and updater, 17.2 Accounts, 17.3 Friends and parties, 17.4 Chat, 17.5 Forums, 17.6 Marketplace and Workshop, 17.7 Profiles and achievements, 17.8 Servers page, 17.9 Admin, 17.10 Steam and PlayFab, 17.11 Security |
| 18 | Build, export & release | 18.1 Windows build and installer, 18.2 Web and mobile export, 18.3 CI jobs, 18.4 Release workflow, 18.5 Update feed |
| 19 | Docs, tests & code health | 19.1 README and docs, 19.2 Test suite, 19.3 QA workflow, 19.4 File splitting |
| 20 | Asset management (Critical) | 20.1 Asset browser, 20.2 Dependencies and references, 20.3 GUIDs, 20.4 Import settings, 20.5 Thumbnails |
| 21 | Prefab system (Critical) | 21.1 Reusable objects, 21.2 Nested prefabs, 21.3 Variants, 21.4 Overrides |
| 22 | Undo/redo & recovery (Critical) | 22.1 Undo history, 22.2 Autosave, 22.3 Crash recovery, 22.4 Backups |
| 23 | Profiling & debugging (Critical) | 23.1 CPU/GPU profiler, 23.2 Memory tracking, 23.3 Frame debugger, 23.4 Error console |
| 24 | Plugin & extension system (Critical) | 24.1 C++ plugins, 24.2 Editor extensions, 24.3 Custom nodes, 24.4 Plugin SDK |
| 25 | Project management (High) | 25.1 Templates, 25.2 Project settings, 25.3 Migration, 25.4 Dependency management |
| 26 | Shader & material editor (High) | 26.1 Visual shader graphs, 26.2 Material editor, 26.3 Custom shaders, 26.4 PBR |
| 27 | Optimization tools (High) | 27.1 LOD generation, 27.2 Occlusion culling, 27.3 Batching, 27.4 Streaming |
| 28 | Version control integration (High) | 28.1 Git integration, 28.2 Diffs, 28.3 Project history, 28.4 Merge conflict tools |
| 29 | Terrain & world building tools (High) | 29.1 Terrain sculpting, 29.2 Vegetation painting, 29.3 Procedural generation |
| 30 | Character creator (Medium) | 30.1 Customization, 30.2 Rig generation, 30.3 Clothing, 30.4 Morph targets |
| 31 | Localization tools (Medium) | 31.1 Translation tables, 31.2 Font fallback, 31.3 Language previews |
| 32 | Accessibility (High) | 32.1 Controller navigation, 32.2 Subtitles, 32.3 Colorblind options, 32.4 Remapping |
| 33 | Automated testing (Critical) | 33.1 Play-mode tests, 33.2 Scene validation, 33.3 Regression testing |
| 34 | Live editing (High) | 34.1 Properties during Play, 34.2 Live object inspector, 34.3 Hot reload |
| 35 | Save game framework (High) | 35.1 Save slots, 35.2 Checkpoints, 35.3 Serialization, 35.4 Versioned save data |

Areas 20-35 (the planned areas) are listed in their own table near the end.

## How to point at something

Three ways, use whichever is easiest:

1. **By number:** "fix 4.2" or "work on 12.4 and 12.3".
2. **By what you see:** find the window, page or symptom below.
3. **By word:** look the word up in the A-Z index. If a word is not there,
   say it anyway and I will search the code and tell you which number it is.

You can also say "area 5 but not Grok's parts" or "everything open in 1".

### By what you see (window, page or symptom)

| You see or say | Sub-area |
|---|---|
| The player floats, slides, jumps badly, steps on stairs wrong | 4.2 |
| Player can't crouch or go prone | 4.2 |
| Camera behind the player is jittery, clips walls | 4.6 |
| First-person view, hands, arms, weapon in view | 4.7 |
| Walking animation feels off, feet slide, wrong speed | 5.3 |
| Imported character is in a T-pose or has the wrong animation | 5.3, 5.4 |
| Feet don't touch the ground, hands don't reach things | 5.6, 5.7 |
| Character falls apart or flops (ragdoll) | 5.8 |
| Character holding a gun or tool looks wrong | 5.9 |
| Model imported inside out, dark, or without textures | 3.5, 3.6 |
| Make a model playable, rig a model | 3.7 |
| Modeling window, Tab edit mode, loop cut, bevel | 3.1, 3.2 |
| Shadows look wrong, lights, day and night | 2.3 |
| Sky, clouds, rain, fog, wind | 2.4 |
| Game runs at low FPS in the editor | 2.1, 2.8, 15.3 |
| Hierarchy panel | 15.1 |
| Inspector, Add Component menu | 15.2 |
| Scene view, Game view, gizmos | 15.3 |
| Project panel, importing files, asset manager | 15.4 |
| Animator window, Animation tab, dope sheet | 15.5 |
| Build Settings, exporting a game | 15.7, 18.1, 18.2 |
| Plugins menu | 15.8 |
| Editor crashes, black screen, layout reset, undo | 15.9 |
| Script editor, red squiggles, autocomplete | 13.5 |
| A script does nothing or errors at runtime | 13.1, 13.2, 13.4 |
| Node editor, wires, nodes missing | 14.1, 14.4 |
| A button, slider, menu or HUD in the game | 12.2, 12.3, 12.4 |
| UI is the wrong size on phones or other screens | 12.1, 12.9 |
| Pause menu, settings screen, loading screen | 12.8 |
| Inventory, backpack, chest, equipment | 11.1 |
| Loot drops, item pickups | 11.2 |
| Crafting | 11.3 |
| Hunger, thirst, health, status effects | 11.4 |
| Build mode, placing walls and structures | 11.5 |
| Enemies chase wrongly, get stuck, walk through walls | 9.1, 9.2 |
| NPC behavior, behavior tree | 9.4, 9.5 |
| Cutscene, camera sequence | 10.1 |
| Dialogue, quests, quest log | 10.2, 10.3 |
| Sound missing, volume, music | 7.1, 7.3, 7.4 |
| Keys, mouse, gamepad, rebinding controls | 8.1, 8.2, 8.3 |
| Things fall through floors, collide wrongly, jitter | 6.1, 6.2, 6.4 |
| Joints, ropes, hinges | 6.3 |
| Multiplayer won't connect, lag, rubber-banding | 16.1, 16.2, 16.3 |
| Joining friends, server list, matchmaking | 16.4, 17.8 |
| Hosting a dedicated server | 16.5 |
| Cheating, bans, speed hacks | 16.6 |
| Voice chat or calls | 16.7, 17.4 |
| Launcher home, Play page, Create page, library | 17.1 |
| Launcher update, "new version", What's New | 17.1, 18.5 |
| Sign in, account, username | 17.2 |
| Friends list, following, parties | 17.3 |
| Chat window, messages, GIFs, emoji, polls | 17.4 |
| Forums | 17.5 |
| Marketplace, Workshop, downloading assets | 17.6 |
| Profile page, badges, achievements | 17.7 |
| Admin, bans, moderation | 17.9 |
| A release, the installer, the update feed | 18.1, 18.4, 18.5 |
| CI is red | 18.3 |
| Saves, progress lost, corrupt save | 1.7 |
| Loading a scene crashes or loses things | 1.4, 1.5 |
| Prefab changes don't apply | 1.6 |
| Language, translations, "1 item / 2 items" | 1.8 |

### A-Z index (word to sub-area)

| Word | Sub-area | Word | Sub-area |
|---|---|---|---|
| achievements | 17.7 | joints | 6.3 |
| action list | 14.6 | keybind | 8.3 |
| admin | 17.9 | launcher | 17.1 |
| AI | 9 | layers (physics) | 1.3 |
| anchors (UI) | 12.1 | layers (animation) | 5.1 |
| animator | 5.1 | lightmap | 2.3 |
| anti-cheat | 16.6 | lights | 2.3 |
| audio | 7 | localization | 1.8 |
| autosave | 15.9 | loot | 11.2 |
| avatar (retarget) | 5.4 | marketplace | 17.6 |
| bake (nav mesh) | 9.1 | materials | 2.2 |
| behavior tree | 9.4 | matchmaking | 16.4 |
| blend shapes | 3.6 | mesh | 3.5 |
| blend tree | 5.1 | minimap | 11.6 |
| blocky character | 4.9 | mixer | 7.1 |
| build mode | 11.5 | modifiers (mesh) | 3.3 |
| Build Settings | 15.7 | NPC | 9.5 |
| camera | 4.6, 4.7, 4.8 | nav mesh | 9.1, 9.2 |
| canvas | 12.1 | nodes | 14.4 |
| chat | 17.4 | object pool | 1.4 |
| collider | 6.1, 6.2 | OkayScript | 13.1 |
| console (editor) | 15.9 | particles | 2.7 |
| controller (player) | 4.1 | pathfinding | 9.1, 9.3 |
| crafting | 11.3 | physics | 6 |
| crouch / prone | 4.2 | pin (Hierarchy) | 15.1 |
| CI | 18.3 | plugins | 15.8 |
| cutscene | 10.1 | post-processing | 2.5 |
| day/night | 2.3 | prefab | 1.6 |
| debugger (visual script) | 14.5 | prefs | 1.7 |
| dedicated server | 16.5 | profiler | 1.10 |
| dialogue | 10.2 | quest | 10.3 |
| dolly | 4.8 | ragdoll | 5.8 |
| dope sheet | 15.5 | relay | 16.5 |
| easing | 1.2 | release | 18.4 |
| ECS | 1.9 | render | 2.1 |
| editor | 15 | retarget | 5.4 |
| equipment | 11.1 | rig | 3.7 |
| event bus | 1.9 | root motion | 5.5 |
| export | 18.1, 18.2 | save game | 1.7 |
| feed (update) | 18.5 | scene manager | 1.4 |
| first person | 4.7 | scheduler | 1.2 |
| fog | 2.4 | script editor | 13.5 |
| fonts | 2.9 | serializer | 1.5 |
| forums | 17.5 | shadows | 2.3 |
| friends | 17.3 | skinning | 5.5 |
| gamepad | 8.2 | sky | 2.4 |
| gizmo | 15.3 | spawner | 11.6 |
| glTF / FBX | 3.6 | splines | 2.7 |
| grip / hold | 5.9 | stance | 4.2 |
| gravity | 6.2, 1.5 | state machine | 5.1 |
| HTML UI | 12.5 | Steam | 17.10 |
| humanoid | 5.4 | stride matching | 5.3 |
| IK | 5.6, 5.7 | survival | 11.4 |
| import | 3.6 | terrain | 2.6 |
| input actions | 8.3 | tilemap | 2.6 |
| inventory | 11.1 | time scale | 1.1 |
| item database | 11.2 | trails / lines | 2.7 |
| Lua / C# | 13.3 | tween | 1.2 |
| UV | 3.4 | vehicle | 4.5 |
| UI kit | 12.7 | voice | 16.7 |
| undo | 15.9 | water | 2.6 |
| updater | 17.1, 18.5 | world space UI | 12.6 |
| visual script | 14 | WebGL | 18.2 |

---

## 1. Core & scene

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 1.1 | Loop and time: Application, Game, Time, Random | `Core/Application`, `Game`, `Time`, `Random` | `making_a_game.md`, `core_tools.md` | Fixed-step helper for gameplay code |
| 1.2 | Timers and tweens: Scheduler, Tween, easing | `Core/Scheduler`, `Tween`, `Components/TweenAnimation` | - | Global scheduler PR in review |
| 1.3 | Scene graph: Scene, GameObject, Transform, components, layers and tags | `Scene/Scene`, `GameObject`, `Transform`, `Component`, `Layers` | `making_a_game.md` | Layer collision matrix, named tags, component registry |
| 1.4 | Scene lifecycle: Start/Awake order, destroy, scene manager, load | `Scene/SceneManager`, `Scene` | `making_a_game.md` | Async load, unload, DontDestroyOnLoad, pooling (branches in review) |
| 1.5 | Scene serializer (`okayscene 3`): load, merge, instantiate | `Scene/SceneSerializer` | - | Safe loading in review; stable object IDs; component serializer registry |
| 1.6 | Prefabs and templates: prefab files, starter players and worlds | `Scene/SceneTemplates`, `UITemplates`, `UIStyles` | `making_a_game.md` | Prefab overrides |
| 1.7 | Saves and prefs: save games, prefs, data assets, data packs | `Core/SaveData`, `Prefs`, `DataAsset`, `DataPack` | `save_games.md` | Durable saves in review; typed Prefs |
| 1.8 | Localization: string tables, plurals, font fallback | `Core/Game` (runtime), `Components/*` consumers | `localization.md` | Number and date formatting, plural rules |
| 1.9 | Events and ECS: EventBus, ECS World, SceneBridge | `Core/EventBus`, `ECS/World`, `ECS/SceneBridge`, `ECS/Systems` | `core_tools.md` | ECS command buffer |
| 1.10 | Logging and profiling | `Core/Log`, `Core/Profiler` | `core_logging_profiling.md` (in review) | Trace export in review |

Editor: Scene Manager panel (`editor/SceneManagerPanel.*`), Localization window.

## 2. Rendering & world

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 2.1 | Renderers: OpenGL, D3D11, D3D12, software, console | `Render/GLRenderer`, `D3D11Renderer`, `D3D12Renderer`, `SoftwareRenderer`, `ConsoleRenderer` | `rendering.md` | Editor Play frame rate in software GL |
| 2.2 | Materials and procedural textures | `Render/Material`, `ProcTexture`, `Mesh` | `rendering.md` | - |
| 2.3 | Lighting, shadows, lightmaps, day/night | `Render/Lighting`, `ShadowSetup`, `Lightmap`, `Components/Light`, `DayNightCycle` | `lighting.md`, `day_night_cycle.md` | - |
| 2.4 | Sky, fog, weather, wind | `Render/SkyStars`, `Environment` | `rendering.md` | - |
| 2.5 | Post-processing: exposure, bloom, grading, AA, AO, depth of field | `Render`, `Graphics` | `rendering.md` | - |
| 2.6 | Terrain, voxel terrain, digging, water | `Components/Terrain`, `VoxelTerrain`, `TerrainDigger`, `Water`, `WorldStreamer` | - | - |
| 2.7 | Particles, line and trail renderers | `Components/ParticleSystem` | - | - |
| 2.8 | Culling and GPU skinning | `Render/Culling` | - | - |
| 2.9 | Fonts and images | `Graphics/Font`, `TtfFont`, `Image` | - | - |

Editor: Lighting panel, Scene and Game views.

## 3. 3D modeling & model import

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 3.1 | Edit mode (Tab, G/R/S, axis lock, snapping, mirror) | `Modeling/EditTransform`, `PolyMesh` | `modeling.md` | - |
| 3.2 | Edge and loop tools (loop cut, bridge, bevel, knife, dissolve) | `Modeling/PolyOps` | `modeling.md` | - |
| 3.3 | Modifier stack | `Modeling/Modifiers`, `Components/MeshModifiers` | `modeling.md` | - |
| 3.4 | UV tools: seams, unwrap, packing, UV editor | `Modeling/UVUnwrap` | `modeling.md` | - |
| 3.5 | Mesh assets (`.okaymesh`) and primitives | `Modeling/MeshAsset`, `Render/Mesh` | `modeling.md` | M5b per-face materials; inside-out primitives (Sphere, Cylinder, Cone, Tube, Torus, TorusKnot, Capsule) |
| 3.6 | Model import: glTF, FBX/OBJ, skins, blend shapes | `Render/ModelImport`, `Gltf`, `Components/BlendShapes` | `modeling.md` | - |
| 3.7 | Rigging and playable setup: Rig Model, Make Playable Character, Set Character Model | `Render/ModelImport` | `animation.md` | One-click humanoid import on any rig |

Editor: Modeling window, Project panel import, Import Model dialog.

## 4. Characters, controllers & cameras

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 4.1 | The Player Controller (View = First, Third or Both) | `Components/PlayerController`, `PlayerView` | `making_a_game.md` | - |
| 4.2 | Stances and movement: crouch, prone, crawl, step-up, jump, wall slide | `Components/Stance`, `StanceControl`, `StepUp`, `ControllerFeel` | `making_a_game.md` | Verify prone on first and third person |
| 4.3 | 2D and 2.5D controllers | `Components/CharacterController2D`, `Platformer25DController`, `TopDownController2D` | `shooter_and_25d_controllers.md` | Improve the 2.5D controller |
| 4.4 | Shooter, top-down, click-to-move, free roam | `Components/ThirdPersonShooterController`, `TopDownController`, `ClickToMoveController`, `FreeRoamController` | `shooter_and_25d_controllers.md` | - |
| 4.5 | Vehicles | `Components/VehicleController`, `VehicleController2D` | `vehicle_controller.md` | - |
| 4.6 | Third-person camera (spring arm) | `Components/PlayerCamera`, `CameraFollow` | `making_a_game.md` | - |
| 4.7 | First-person camera feel and hands | `Components/FirstPersonController`, `FirstPersonHand` | `making_a_game.md` | Blocky Character arms in first person |
| 4.8 | Virtual cameras, dolly paths | `Components/VirtualCamera`, `CinemachineBrain`, `DollyPath` | - | Camera stack where depth decides the main camera |
| 4.9 | Blocky Character and player customization | `Components/Character` | - | More customization options |

Editor: Player Controller panel, Character Model panel.

## 5. Animation & IK

Grok's sessions own most open PRs here; ask before touching.

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 5.1 | Animator and state machines (layers, blend trees, 2D blend, interruption) | `Components/Animator`, `AnimStateMachine` | `animation.md` | - |
| 5.2 | Clips and curves, `.okayanim` | `Animation/AnimationClip`, `AnimationCurve`, `Components/AnimClip` | `animation.md` | - |
| 5.3 | Model clip player and locomotion (stride matching, idle fallback, strafe and backpedal) | `Components/ModelAnimator`, `CharacterAnimBackend` | `animation.md` | Strafe and backpedal clips; "two components named Animator" label clarity |
| 5.4 | Humanoid bone mapper, retarget, built-in library | `Components/HumanoidRetarget`, `Animation/HumanoidLibrary`, `HumanoidMotion` | `animation.md` | Retargeting by avatar slot |
| 5.5 | Root motion and skinning (linear and dual quaternion) | `Components/RootMotion`, `SkinnedMesh` | `animation.md` | - |
| 5.6 | Foot, limb and chain IK | `Components/FootIK`, `LimbIK`, `ChainIK` | `ik.md` | - |
| 5.7 | Look-at, aim and full-body IK | `Components/LookAtIK`, `AimIK`, `FullBodyIK`, `IKPose` | `ik.md` | IK rewrite plan |
| 5.8 | Ragdolls and hit reactions | `Components/*Ragdoll*` | `ragdolls.md` | - |
| 5.9 | Procedural object holding (grips, hand IK, body adjust) | `Components/ProceduralObjectHolding`, `HoldableObject` | `animation.md` | Holding PRs 3-4: body adjust, multiplayer sync |

Editor: Animator window, Animation tab, FP Hands window, Timeline preview.

## 6. Physics

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 6.1 | 2D bodies and colliders | `Physics/Physics2D`, `Rigidbody2D`, `Collider2D` | - | - |
| 6.2 | 3D bodies and colliders, collider fit | `Physics/Physics3D`, `Rigidbody3D`, `Collider3D`, `ColliderFit` | - | - |
| 6.3 | Joints | `Components/Joint2D`, `Joint3D` | - | - |
| 6.4 | Fixed step with render interpolation, pose history | `Scene/Scene` (StepPhysics) | - | - |
| 6.5 | Queries and debug draw | `Physics/PhysicsDebug` | - | No dedicated physics doc |
| 6.6 | Destructibles and projectiles | `Components/Destructible`, `Projectile3D` | - | - |

## 7. Audio

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 7.1 | Mixer and Audio Mixer | `Audio/AudioMixer` | `audio.md` | - |
| 7.2 | Formats (WAV, OGG, MP3) and clips | `Audio/AudioClip` | `audio.md` | - |
| 7.3 | Audio sources and music player | `Components/AudioSource` | `audio.md` | - |
| 7.4 | Saved volume settings | `Components/SettingsScreen` | `audio.md` | - |

## 8. Input

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 8.1 | Keyboard and mouse | `Input/Input` | `input.md` | Physical key codes, function keys, left/right modifiers |
| 8.2 | Gamepad (SDL) | `Input/Input` | `input.md` | Dead zones, response curves, multiple controllers |
| 8.3 | Input actions and rebinding UI | `Input/Input`, `Components/UIKeybindField` | `input.md` | Input contexts with consume-on-use |
| 8.4 | Cursor and touch | `Input/Cursor`, `Components/UITouchScroll` | `input.md` | - |

## 9. AI & navigation

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 9.1 | Nav mesh bake and queries | `AI/NavGrid3D`, `Components/NavMeshSurface` | `navigation.md` | - |
| 9.2 | Nav agents and obstacles | `Components/NavMeshAgent`, `NavMeshObstacle` | `navigation.md` | - |
| 9.3 | Grid pathfinding (older) | `AI/Pathfinding` | `navigation.md` | - |
| 9.4 | Behavior trees (assets, blackboard, runner, Start Dialogue task) | `Components/BehaviorTreeRunner` | `behavior_trees.md` | - |
| 9.5 | NPCs and perception | `Components/NPCController` | `npcs_and_crafting.md` | - |

Editor: Navigation window, Behavior Tree window.

## 10. Story & cinematics

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 10.1 | Cutscene sequences and Sequence Player | `Components/SequencePlayer` | `cutscenes.md` | - |
| 10.2 | Dialogue graphs and runner | `Components/DialogueRunner` | `dialogue_quests.md` | - |
| 10.3 | Quests (objectives, prerequisites, rewards), story variables | `Components/QuestLog`, `StoryState` | `dialogue_quests.md` | - |
| 10.4 | Dialogue box, quest tracker and journal | `Components/*` UI | `dialogue_quests.md` | - |

Editor: Timeline window, Dialogue window.

## 11. Gameplay systems

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 11.1 | Inventory: bag, grid, chests, equipment | `Inventory/*`, `Components/Inventory*`, `GridInventory`, `ChestInventory`, `Equipment` | `survival_kit.md` | Unturned-style inventory improvements |
| 11.2 | Items and loot: item database, loot tables, pickups | `Inventory/ItemDatabase`, `LootTable`, `Components/ItemPickup`, `LootSource` | `survival_kit.md` | - |
| 11.3 | Crafting and recipes | `Inventory/Recipe`, `Components/Crafting*` | `npcs_and_crafting.md` | - |
| 11.4 | Survival: stats, afflictions, zones | `Components/Survival*` | `survival_kit.md` | - |
| 11.5 | Building: build mode, structure builder, snapping | `Components/BuildMode`, `StructureBuilder`, `Buildable` | `build_mode.md` | - |
| 11.6 | Spawners, turn manager, minimap | `Components/Spawner`, `TurnManager`, `Minimap` | - | - |
| 11.7 | Combat actions: melee, dodge, dive | `Components/MeleeAttacker` | - | - |

Open at the area level (ROADMAP inventory plan): gameplay features, scripting, UI polish, server-authoritative networking.

## 12. In-game UI

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 12.1 | Canvas, anchors, flex layout, safe area | `Components/Canvas`, `UIAnchor`, `UILayoutGroup`, `UIFlexItem`, `UISafeArea` | `game_ui.md` | - |
| 12.2 | Widgets: button, image, raw image, text, panel, progress, rating | `Components/UIButton`, `UIImage`, `UIRawImage`, `UIPanel`, `UIProgressBar` | `game_ui.md` | - |
| 12.3 | Input widgets: slider, toggle, input field, dropdown, stepper, keybind | `Components/UISlider`, `UIToggle`, `UIInputField`, `UIDropdown`, `UIStepper` | `game_ui.md` | - |
| 12.4 | Lists, tabs, modals, context menu, tooltips, scroll views | `Components/UIListView`, `UITabs`, `UIModal`, `UIContextMenu`, `UITooltip`, `UIScrollView` | `game_ui.md` | 2-D navigation (UI plan P0-P4) |
| 12.5 | HTML UI | `Components/UIDocument` | `ui_html.md` | - |
| 12.6 | World-space UI | `Components/WorldSpaceUI`, `WorldUI` | `ui_world_space.md` | - |
| 12.7 | Themes and the launcher-matching UI kit | `Scene/UIStyles`, `UITemplates` | `game_ui.md` | Full component library |
| 12.8 | Loading, pause and settings screens | `Components/LoadingScreen`, `PauseMenu`, `SettingsScreen` | `game_ui.md` | - |
| 12.9 | Mobile scaling | `UI/UILayoutUpdate` | `game_ui.md`, `web_mobile.md` | - |

Editor: UI mode in the Scene view, UI inspector overlay.

## 13. Scripting

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 13.1 | OkayScript VM | `Scripting/OkayScriptVM` | `SCRIPTING.md`, `scripting.md` | Scripting bug list 1-15 |
| 13.2 | C#-style language core | `Scripting/NextScriptVM` | `scripting.md` | Classes, lambdas, events, inheritance; replace the old VM |
| 13.3 | C# and Lua VMs | `Scripting/CSharpScriptVM`, `LuaScriptVM` | `scripting.md` | - |
| 13.4 | Script components and script UI | `Components/ScriptComponent`, `Scripting/ScriptUI` | `scripting.md` | - |
| 13.5 | Script editor: autocomplete, errors, rename, format | editor Script Editor | `scripting.md` | Meaningful syntax only; classes and templates for New Script |
| 13.6 | Engine API and generated docs | `docs/engine_api.md` | regenerate with `OKAY_WRITE_API_DOCS=1 ./tests/test_vs_types` | - |

## 14. Visual scripting

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 14.1 | Graph editor | `VisualScript/NodeGraph`, `editor/VisualScriptEditor.hpp` | `visual_scripting.md` | Docs update |
| 14.2 | Typed pins, variables and scopes, object references | `VisualScript/VsTypes`, `VsValue`, `VsVariables` | `visual_scripting.md` | - |
| 14.3 | Functions, loops, control flow | `VisualScript/NodesFunctions`, `NodesLoops` | `visual_scripting.md` | - |
| 14.4 | Node library (physics, story, build, math) | `VisualScript/Nodes*` | `visual_scripting.md` | - |
| 14.5 | Debugger (breakpoints, step, call stack, watch) | `VisualScript/VsDebug` | `visual_scripting.md` | - |
| 14.6 | Action framework | `VisualScript/ActionGraph`, `Components/ActionList`, `NoCode` | `visual_scripting.md` | - |

## 15. Editor

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 15.1 | Hierarchy | `editor/main.cpp` | `editor.md` | Polish |
| 15.2 | Inspector, Add Component menu, Character Model panel | `editor/main.cpp` | `editor.md` | Polish |
| 15.3 | Scene and Game views, gizmos, spline tools | `editor/main.cpp` | `editor.md` | - |
| 15.4 | Project panel and asset manager | `editor/main.cpp` | `editor.md` | Polish |
| 15.5 | Animator, Animation tab, Timeline | `editor/AnimatorPreview.hpp`, `main.cpp` | `editor.md`, `animation.md` | - |
| 15.6 | Navigation, Behavior Tree, Dialogue, Localization, Modeling windows | `editor/main.cpp` | `editor.md` | - |
| 15.7 | Build Settings | `editor/main.cpp` | `packaging.md` | - |
| 15.8 | Plugin system and Plugins menu | `editor/main.cpp` | `editor-plugins.md` | Plugins menu improvements |
| 15.9 | Layout, undo, autosave | `editor/EditorState.*` | `editor.md` | - |
| 15.10 | Splitting `editor/main.cpp` into panel files (~45k lines) | `editor/*.cpp` | - | In progress |

## 16. Networking & multiplayer

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 16.1 | Transport, sealed channels, packets | `Net/NetTransport`, `SecureChannel`, `NetChannels`, `Packet` | `relay.md` | - |
| 16.2 | Server authority, prediction, lag compensation | `Net/NetworkManager`, `PlayerInput`, `NetOwnership` | `dedicated-server.md` | Anti-cheat items 12-13 |
| 16.3 | Snapshots and sync, interest | `ECS/NetWorld*`, `Components/NetworkSync` | - | Delta snapshots |
| 16.4 | Matchmaking, queues, server browser, join links | `Net/Matchmaker`, `Matchmaking`, `ServerBrowser`, `JoinLink` | `launcher-multiplayer.md` | - |
| 16.5 | Dedicated server, export, relay | `Net/DedicatedServer`, `RelayServer` | `dedicated-server.md`, `relay.md` | - |
| 16.6 | Anti-cheat | `Net/AntiCheat`, `ClockCheck`, `Components/NetValidator` | `anticheat.md` | Hardening items 5 (bans at ticket issue) and 6 (tickets bound to a server) wait on your OK; 8, 9, 10, 14 |
| 16.7 | Voice chat server | `launcher/VoiceChat.hpp` | `voice-server.md` | - |
| 16.8 | Network player spawning and avatar motion | `Components/NetworkPlayerSpawner`, `NetworkAvatarMotion` | `launcher-multiplayer.md` | - |

## 17. Launcher & online platform

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 17.1 | Launcher shell: Play and Create pages, library, updater, crash reports, What's New | `launcher/main.cpp` | `packaging.md` | Split `launcher/main.cpp` |
| 17.2 | Accounts and sign-in | `Platform/Account` | `accounts.md`, `supabase-backend.md` | - |
| 17.3 | Friends, following, parties | `Platform/Friends`, `Parties` | `launcher-multiplayer.md` | - |
| 17.4 | Chat: DMs, groups, voice notes, calls, polls, GIFs, emoji | `Platform/Messages`, launcher | `supabase-backend.md` | - |
| 17.5 | Forums | `Platform/Forums` | `supabase-backend.md` | - |
| 17.6 | Marketplace and Workshop | `Platform/Workshop` | `supabase-backend.md` | - |
| 17.7 | Profiles, badges, achievements | `Platform/Profiles`, `Achievements` | `accounts.md` | - |
| 17.8 | Servers page | `Platform/Servers` | `launcher-multiplayer.md` | - |
| 17.9 | Admin tools | `Platform/Admin` | `supabase-backend.md` | Create test servers without a game |
| 17.10 | Steam and PlayFab | `Platform/Steam`, `PlayFab` | `steam_release.md`, `photon.md` | - |
| 17.11 | Security plan SEC1 | relay, script paths, updater signing, reactions SQL | `docs/sql` | All four items |

## 18. Build, export & release

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 18.1 | Windows build and installer | `scripts/*`, `CMakeLists.txt` | `packaging.md` | Stop committing dist binaries |
| 18.2 | Web and mobile export | `scripts/*` | `web_mobile.md` | - |
| 18.3 | The five CI jobs (Ubuntu, ASan/UBSan, MinGW, Windows export check, WebGL) | `.github/workflows/*` | `qa_workflow.md` | - |
| 18.4 | Release workflow | `.github/workflows/release.yml` | `releasing.md` | - |
| 18.5 | Supabase publish and update feed | `scripts/publish-supabase.sh` | `releasing.md` | Remove the hardcoded Supabase URL/key from CMake |

## 19. Docs, tests & code health

| ID | Sub-area | Code | Docs | Open |
|---|---|---|---|---|
| 19.1 | README and per-feature docs | `README.md`, `docs/*` | `docs/README.md` | README matches reality plus a component audit doc |
| 19.2 | Test suite (about 330 registered tests) | `tests/*` | `qa_workflow.md` | - |
| 19.3 | QA workflow, rig sweep test | `tests/*` | `qa_workflow.md` | - |
| 19.4 | Splitting the largest files (editor, launcher, OkayScript VM, serializer) | - | - | See 15.10, 17.1, 13.1, 1.5 |

---

## 20-35. Planned areas

Areas 20-35 are planned features you asked for. Each says what exists today
(so nothing is built twice), what is missing, and the priority you gave.
"In flight" means a branch or PR already exists.

| ID | Area (priority) | Exists today | Missing | Related |
|---|---|---|---|---|
| 20 | Asset management (Critical) | Project panel and marketplace asset manager (15.4); asset manager core in flight (typed handles, refcounts, async load, hot reload; PR #568) | Asset browser with search and filters, dependency graph and "used by" lookup, persistent GUIDs and a meta file per asset so renames and moves keep references, per-asset import settings, thumbnails | 15.4, 3.6, 1.6, 21 |
| 21 | Prefab system (Critical) | Prefab files and instantiate (1.6); stable object IDs, prefab overrides and re-apply in flight (branch claude/scene-ids-prefabs) | Nested prefabs, variants, override editor UI (apply, revert, per-field), prefab mode editing, unpack | 1.6, 15.2, 20 |
| 22 | Undo/redo & recovery (Critical) | Undo is a full-scene text snapshot, 100 steps; autosave (15.9) | Command-based undo (cheap, per-property), undo across Play, crash recovery (restore after a crash), rolling backups and a restore UI | 15.9 |
| 23 | Profiling & debugging (Critical) | Profiler with categories and trace export in review (PR #559); log file sink; editor console (15.9) | CPU and GPU timeline window, memory tracking by system, frame debugger (draw call list, state, step through), error console filters and links to the source object | 1.10, 2.1, 15.6 |
| 24 | Plugin & extension system (Critical) | Editor plugin system and Plugins menu (15.8, `editor-plugins.md`) | C++ plugin ABI with versioning and a plugin SDK package, editor extension points (windows, inspectors, menus), custom nodes for visual scripting, plugin manager UI | 15.8, 14.4 |
| 25 | Project management (High) | Templates (1.6), Create page (17.1), Build Settings (15.7) | Full project settings window, project migration across engine versions, dependency management (packages and versions) | 1.6, 17.1, 15.7 |
| 26 | Shader & material editor (High) | Materials and procedural textures (2.2) | Visual shader graph, material editor window with live preview, custom shader files across the renderers, PBR material model | 2.1, 2.2 |
| 27 | Optimization tools (High) | Frustum culling, GPU skinning (2.8) | LOD generation and LOD groups, occlusion culling, static and dynamic batching, asset and scene streaming | 2.8, 2.6, 1.4 |
| 28 | Version control integration (High) | None | Git panel in the editor (status, commit, branch), text diffs of scenes and prefabs (needs diff-friendly scene text, see 1.5), project history, merge conflict helper for scenes | 1.5, 15 |
| 29 | Terrain & world building tools (High) | Terrain, voxel terrain, water, splines (2.6, 2.7) | Terrain sculpting brushes, vegetation and detail painting, procedural generation (noise, erosion, scatter) | 2.6 |
| 30 | Character creator (Medium) | Blocky Character (4.9), Rig Model and Make Playable (3.7), blend shapes (3.6) | Character customization UI, rig generation, clothing and attachment system, morph target sliders | 4.9, 3.7, 5.4 |
| 31 | Localization tools (Medium) | String tables, plurals and font fallback runtime (1.8); number and date formatting in flight | Translation table editor window, missing-key report, import and export (CSV), language preview in the Game view | 1.8 |
| 32 | Accessibility (High) | Input actions and rebinding UI (8.3); extended keys, contexts and gamepad curves in flight | Full controller navigation of all UI, subtitles and captions system, colorblind filters and safe palettes, text scaling, remap everything including UI | 8.3, 12.4, 12.7 |
| 33 | Automated testing (Critical) | About 330 unit tests, five CI jobs (19.2, 18.3) | Play-mode tests (run a scene headless and assert), scene validation (missing references, missing components), regression and golden-image tests, a test runner window | 19.2, 18.3 |
| 34 | Live editing (High) | Editor Play mode (15.3) | Change properties during Play and keep or revert them, live object inspector for runtime-created objects, script hot reload without leaving Play | 15.2, 15.3, 13 |
| 35 | Save game framework (High) | Durable saves: atomic writes, versioned and checksummed slots, backups, migration hook (merged, PR #552, `save_games.md`) | Checkpoints, save slot UI components, scene-object state saving (SaveableObject), cloud save hooks | 1.7, 12 |

---

## Currently moving (as of 2026-11-08)
- Release 2.847.0 is live on GitHub and the update feed.
- Core & scene wave 1 (in review): safe scene loading (1.5), durable saves (1.7),
  core utility fixes (1.1), scene lifecycle fixes (1.4), log and profiler (1.10),
  timers and tweens (1.2), and a Core audit with 14 bug fixes.
- Core & scene wave 3 (branches pushed, PRs open once wave 1 lands): async scene
  load, pooling and RequireComponent (1.4); stable IDs, prefab overrides and
  serializer registry (1.5, 1.6); plural rules, number formatting, typed Prefs and
  fixed timestep (1.8, 1.7, 1.1).
- Animation and IK PRs (#535-#539, #548-#551, #554) belong to Grok's sessions.
