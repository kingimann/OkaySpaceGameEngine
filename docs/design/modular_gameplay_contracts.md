# Modular engine: modules, shared contracts, per-project stripping

Status: design only, no code. Written against origin/main a73fdb22.
Goal: the WHOLE engine is a set of modules (rendering features, physics, animation, audio, UI, AI, gameplay,
networking, scripting, tools) with a manifest each. A project turns modules on and off; off means the code is
not in the exported game. Modules work together by default through a few shared contracts (health, damage,
interaction, weapon, seat) with no manual wiring, and nothing is duplicated: whatever a contract or module replaces
is deleted in the same PR. Sections: 1 what exists, 2 the module system (list, graph, stripping options),
3 gameplay contracts, 4 working together, 5 project UX, 6 migration of old files, 7 multiplayer,
8 verification, 9 PR plan, 10 open questions.

"verified" means read in code on this commit (header or source, path given). The earlier grep-based audit
was checked; where it was wrong this document says so (section 1.9).

## 1. What exists today

### 1.1 Health (four models, verified)

| Model | File | What it has | Who reads/writes it |
|---|---|---|---|
| `HealthStat` | `engine/include/okay/Components/SurvivalComponents.hpp:82` | maxHealth/health, flat armor, `equipArmor`, resistance, minDamage, godMode, i-frames, regen + delay, overheal, death/respawn/lives, `dead`, `lowHealth`, `LastHitFrom(Vec3)`, `SetHealthFromServer` | projectiles, afflictions, VS nodes (`NodesGameplay.hpp:53 GpHealth`), Ragdoll, Equipment, NetworkManager (`__dmg`), QuestLog kill hook (`HealthDeathHook`), LootSource, BehaviorTreeRunner, DeathLock, HumanoidActions |
| `SurvivalStats` | `Components/SurvivalStats.hpp:28` | its own `health/maxHealth/armor/resistance/godMode`, plus hunger/thirst/oxygen/warmth that call its own `Damage()` | `DamageHealthOn`/`HealOn` in `SurvivalAfflictions.hpp:10` (prefers SurvivalStats, falls back to HealthStat), Equipment (`ss->equipArmor`) |
| `Stats` (RPG) | `Components/Stats.hpp:13` | `health/maxHealth`, `TakeDamage` with defense (min 1), levels, mana; fires `on_death` | Equipment (`st->equipDefense`); nothing in the shooter/projectile path |
| `NPCController` | `Components/NPCController.hpp:104,138` | private `health/maxHealth`, `Damage()`, `fleeHealthPct`, `npc_died`, provoke on hit | `MeleeAttacker` (`npc->Damage`, NPC targets only, `MeleeAttacker.hpp:54`); NPC attacks use `DamageHealthOn` (`NPCController.hpp:447`) |

Consequences, all verified:
- `DamageGameObject` (`Projectile3D.hpp:17`) walks up parents for a `HealthStat` only. A projectile cannot
  hurt an NPC, a `Stats` character or a `SurvivalStats` player. `TopDownShooterController.hpp:380` uses the
  same helper for hitscan.
- `MeleeAttacker` cannot hurt a `HealthStat`; shots cannot hurt an NPC. Two incompatible damage paths.
- Visual-script nodes (`DamageNode`, `HealNode`, `gplib_rows.inc` `GlGetHealth`, `GlSetArmor`...) work on
  `HealthStat` only and raise "its object has no Health component" otherwise.
- `Equipment::ApplyBonuses` (`engine/src/Inventory/Equipment.cpp:301`) pushes armor into three models by hand.
- Scene saving: `HealthStat` is record `stat_health` (`SceneSerializer.cpp:2221` write, `:5999` read, with
  trailing optional fields added over time). `SurvivalStats`, `Stats`, NPC health each have their own records.
- Death signals are also four: `HealthDeathHook` + broadcast `died`, `on_death` script event, `npc_died`,
  `starving`-style messages.

### 1.2 Damage (no shared event, verified)
- Damage is a bare `float`: `HealthStat::Damage(float)` and `Damage(float, Vec3 fromWorld)`. No type, source
  object, hit zone, hit point, or direction (only `lastHitFrom`).
- Callers: `DamageGameObject`, `DamageHealthOn`, VS nodes, `NetworkManager::ApplyHit`
  (`engine/src/Net/NetworkManager.cpp:1796`), `NativeUIActions.hpp:45` (button "Damage"), NoCode
  `DamageOnTouch` (`Components/NoCode.hpp`).
- Floating numbers: `UIDamageNumbers` is driven by script/UI actions, not by a damage event.

### 1.3 Interaction (no shared contract, verified)
- `InventoryInteractable` (`Components/InventoryPlayer.hpp:66`): abstract base with `open`, `range`,
  `playerName`, `InteractKey()`, `Nearest()` (so one chest opens at a time). Subclasses: `ChestInventory`,
  `CraftingStation`. Fires `on_chest_opened/closed` through `InventoryEvents::Fire`.
  (The audit said "no generic Interactable": there is a base, but it is inventory-specific, toggle-only,
  key = a single `char`, no prompt, no hold.)
- Not on it, each with its own range + key + prompt code: `ItemPickup` (`interactKey`, `interactRange`,
  static `BlockInteract()` flag), `DialogueRunner` (`interactKey`, `interactRange`, `promptText`),
  `Seat` (`useRadius`, `prompt`, `playerInRange`), `HoldableObject`, `LogicSource` buttons (`LogicLinks.hpp`).
- Shared prompt UI exists only as `HoldPrompt` (label object named "Hold Prompt"; `ExtraPrompt()` static string).
- "One key press is used once" is solved three ways: `ItemPickup::InteractBlocked`, `Nearest()`, and per
  component checks.
- No "inspect" action anywhere.

### 1.4 Weapons (verified)
- `TopDownShooterController` (`Components/TopDownShooterController.hpp`): fire modes (built-in bullet, prefab,
  hitscan), `range`, `pellets`, `spread`, `magazineSize/ammo/reloadKey/reloadTime/autoReload`, dash, script
  events `on_fire/on_reload/on_reloaded/on_empty`. Ammo is a plain int, not drawn from `Inventory`.
- `ThirdPersonShooterController`: aim camera, only calls the sibling script's `on_fire()`. No damage, ammo,
  reload (verified: its header says it calls the script). Used by `SceneTemplates::AddThirdPersonShooterPlayer`
  (`SceneTemplates.hpp:314`), editor GameObject menu (`editor/main.cpp:6426`) and the "Third Person Shooter"
  project template (`editor/main.cpp:17918`).
- `FirstPersonController`: no weapon code found (hands exist: `FirstPersonHands`).
- `Projectile3D` (`Components/Projectile3D.hpp`): swept ray, pierce, bounce, `owner`, pool reuse.
- `MeleeAttacker`: NPC targets only.
- Multiplayer: `NetworkManager::ClaimHit(targetNetId, weapon, origin, direction)` -> server validates (range,
  line of sight, lag-compensated pose history, `NetValidator "hit/<weapon>"` fire rate and ammo bucket) ->
  `ApplyHit` -> `HealthStat::Damage` -> `__dmg` reliable message -> `SetHealthFromServer`.
  Damage amount in the claim is server-side (`claim.damage`). Hit zone and damage type are not in the claim.
- `ItemDefinition` (`Inventory/ItemDatabase.hpp:42`) has `category`, `tags`, `equip` (EquipSlot incl.
  MainHand/OffHand), `useAction/useAmount`, `custom` key/value list. No weapon stats; `Equipment` armor
  comes from the `custom` "armor" field (`Equipment.hpp:23`).
- No recoil, no attachments (verified by grep and by reading the controllers). `Sockets.hpp` exists.

### 1.5 Seats and vehicles (verified)
- `Seat` (`Components/Seat.hpp`; implementation inside `engine/src/Animation/HumanoidActions.cpp`): sit point,
  exit point, `type` (chair/ground/bench), `useRadius`, `prompt`, `occupied`. `Claim(who)` / `Release()`,
  parks the occupant (disables gravity, blends position). Saved as record `seat` (`SceneSerializer.cpp:2229`).
  Driven by player "interact" in the Character path and VS `sit` node (`NodesExtra.hpp:991`). Chair only: no
  controls hand-over, no seat roles.
- `VehicleController` (+2D): arcade driving, optional raycast suspension (`suspension=false` default,
  `VehicleController.hpp:45`), no health, no seats, no doors, no occupants.
- Doors: only `LogicTarget::Kind::Door` (`LogicLinks.hpp:158`), scene doors driven by `LogicSource`.

### 1.6 Inventory and equipment (verified)
`Inventory`, `GridInventory`, `Equipment`, `Sockets`, `InventoryOps`, `ItemDatabase`/`ItemInstanceData`
(per-item values), `InventoryEvents::Fire(go, scriptEvent, vsEntry, args)` (script event + VS entry in one
call: the pattern every new event should use), `NetInventory`. Two inventory shapes (slot vs grid) are not
unified; that stays out of scope here.

### 1.7 Input (verified)
`Input/InputActions.hpp`: named actions with `InputBinding` (key, mouse, pad button, Keys4, sticks, DPad,
mouse delta), per-action `defaults`, contexts. Controllers mostly read raw keys (`Input::GetKeyDown(char)`,
for example `reloadKey = 'r'`, `interactKey = 'e'`). `RemapModel` exists but has no screen (only
`tests/test_focus_remap.cpp` uses it). Project Settings has no input section (`ProjectSettings` in
`editor/ProjectState.hpp:304`: company, version, size, gravity, ambient, skybox, splash only).

### 1.8 Network sync (verified)
- `NetworkSync` (`Components/NetworkSync.hpp:181`): `netId`, `authority {Host, Mine, Manual}`, `owned`,
  `syncAnimation`, `clientClaimable`. Replicates transform, plus Character animation and punch for proxies.
- Other channels: `SyncVars`/RPC (`NetworkManager::Rpc/RpcReliable/OnRpc`, `Components/NetworkSync.hpp` neighbours
  `NetworkObject`, `NetInventory`), `__dmg` health result, `NetValidator` per-RPC rules, `AntiCheatConfig`.
- Health is replicated only as the `__dmg` message to `HealthStat`; seats, doors, ammo, interactables are not.

### 1.9 Where the audit was wrong or incomplete
- "Ragdoll bone colliders" for limb damage: false. `Ragdoll` is a Verlet particle chain over the 15
  humanoid bones (`Ragdoll.hpp` header comment), no per-bone colliders. A hit zone has to come from the hit
  point nearest a humanoid bone (`humanoid::MapHumanoidBones`, `HumanoidRetarget`), not from colliders.
- "FocusGraph unused": it is used by `UINavigation.hpp:14,51`. `MipBudget` is referenced by
  `SceneOptimizationReport.cpp`, which has no editor caller. `RemapModel` really is unused by UI.
  These are unrelated to gameplay modules and are not touched here.
- "No generic Interactable": there is an inventory-only base (1.3).
- Health models: four confirmed, with the exact consumers in 1.1.
- Traversal flags default off (`Traversal.hpp` header comment): confirmed; out of scope except as a manifest
  default (section 3).

### 1.10 Wiring facts the contracts must fit
- Components are `Behaviour` subclasses (`Scene/Component.hpp`) with `Start/Update/OnDestroy`,
  `ExecutionOrder()`, `OnAnimationEvent`.
- Scene text is line records (`SceneSerializer.cpp`), version `kSceneFormatVersion = 3`
  (`SceneSerializer.hpp:72`). New components can register through `ComponentSerializerRegistry`
  (`Scene/ComponentSerializer.hpp`) without touching the if/else chain; an unknown record is skipped with a
  warning, a newer file version loads "what is understood" (`SceneSerializer.cpp:4072`). Upgrades are
  `SceneMigration::Step` (`from -> from+1`, text rewrite or `needsEngine`).
- Properties: `PropertyBuilder` / `PropertyRegistry` (`Scene/BuiltinPropertiesGameplay.cpp`) describe fields with
  tooltips and ranges for the Inspector and for path access ("Light.intensity"); `ComponentTypeInfo` has a
  `category` used by the Add Component menu.
- Add Component is a hand-written menu in `editor/main.cpp` around `39281` (search box, category tabs,
  recent list; items such as `NetworkSync` at `:39679`).
- Visual-script nodes: hand-written node structs plus generated tables (`editor/generated/gplib_*.inc`,
  `editor/VisualScriptNodeDocs.inc` from `scripts/gen_vs_node_docs.py`; test `tests/test_vs_node_docs.cpp`).
- Script events: `InventoryEvents::Fire(go, "on_x", "vsEntry", args)`.

### 1.11 How the engine is built and exported today (verified)
- One engine library: `engine/CMakeLists.txt:2` is a single `add_library(okay_engine ...)` with an explicit list of
  258 `.cpp` files (static, `BUILD_SHARED_LIBS` is forced OFF at `:408`). Tests (`tests/CMakeLists.txt`, 525 test
  files), the editor, the player, the sandbox and the launcher all link `okay::engine`. There is no per-area target.
- The player (`player/main.cpp`) is one generic program that loads a scene at run time; the dedicated server is
  the same program (`Net/DedicatedServer.hpp`). Because scenes are data, "which components exist" is decided by the
  binary, never by the project.
- Build Game (`editor/main.cpp`, `builder::Build`) copies a prebuilt player (`FindPlayer()`, line ~3767: next to
  the editor, `bin/`, `runtime/`), or downloads it (`EnsurePlayer`) from the release, plus `Data/`. Users do not
  compile anything. `dist/OkaySpacePlayer.exe` is 6.5 MB; the editor is a separate 4-6 MB launcher-sized set.
- Release builds are cross-compiled in CI with MinGW (`scripts/build-windows-dist.sh`, `.github/workflows/
  release.yml`); CI also builds a player and an editor and runs the whole `ctest` once (`ci.yml:27-52`). Web is a
  separate Emscripten build (`scripts/build-web.sh`). No toolchain ships with the editor.
- PR #683 (`claude/build-pipeline`, not merged) adds `Core/BuildPipeline.hpp`: `WalkAssets` dependency walk from the
  scenes, `Report::AddExcluded`, `FindUnshipped`, layouts. It strips assets, not code.

### 1.12 Engine-wide coupling (measured from `#include "okay/..."` lines, verified)
Areas are the folders under `engine/include/okay` and `engine/src`. Rough size in header+source lines:
Components 59k (210 headers + 49 sources), Core 33k, Scene 29k, Render 22k, VisualScript 14k, Scripting 11k +
Script 7k, Net 11k, Platform 10k, Physics 4.5k, AI 4.7k, Animation 4.7k, Audio 4.6k, Debug 4.8k, Math 3.4k,
Modeling 3.4k, Optimize 3.3k, UI 4k, Testing 2.9k, Inventory 2.7k, Crypto 1.4k, ECS 1k.

Findings:
1. **`Components/` is not a layer, it is every feature.** The 210 component headers belong to ~25 different
   features (UI kit 48, controllers 24, rendering 25, animation 14, inventory 15 ...). Folders do not equal modules;
   modules must be defined by component groups (table in 2.2).
2. **Everything points at `Components`.** Include counts by area into `Components/`: Scene 373, Scripting 75,
   VisualScript 63, Inventory 47, AI 45, Script 44, Render 43, Testing 37, Debug 18, Core 18, Input 15, Net 5.
   The lower layers (`Scene`, `Core`, `Input`, `Render`, `Math`) include feature components.
3. **Hard-wired registries (the real blockers), each with the count of components it names:**
   - `Scene/SceneSerializer.cpp` (9752 lines, 175 component includes): the if/else chain writes and reads every
     component. `ComponentSerializerRegistry` exists (`Scene/ComponentSerializer.hpp`) but only some components use it.
   - `Scripting/OkayScriptVM.cpp` (10869 lines, 75 component includes) and `Script/EngineApi.cpp` (33): script
     bindings reference every system directly.
   - `Scene/BuiltinProperties.cpp`, `BuiltinPropertiesGameplay.cpp` (36), `BuiltinPropertiesUI.cpp` (25): the
     Inspector property tables. `PropertyRegistry` already has `ComponentTypeInfo::add`, i.e. the mechanism for
     registration exists.
   - `Testing/SceneValidator.cpp` (35), `Debug/ProjectDiagnostics.cpp` (18), `Scene/SceneLocalization.cpp` (12),
     `Core/Tween.cpp` (9), `Core/SaveFrameworkScene.cpp` (5): per-component rules and savers.
   - `VisualScript/` nodes (`NodesGameplay`, `NodesInventory`, `NodesStory`, `NodesPhysics`... header-only, built into
     one registry in `VsTypes.cpp`/`NodeGraph.cpp:878` factory chain) plus editor generated tables
     (`editor/generated/gplib_*.inc`).
   - `Input/GameInput.cpp` (UI-blocking logic) includes InventoryUI, ChestInventory, CraftingStation, ItemPickup,
     HoldPrompt, DialogueRunner, SettingsScreen, Minimap, SequencePlayer: the input layer knows the features.
   - `Core/` includes `Components` 18, `Render` 8, `Physics` 6, `Audio` 3 times (SaveFramework scene savers, Tween,
     GameLoop).
   - `include/Okay.hpp` is a 306-line umbrella including everything (fine for users, must not be used inside the
     engine). No `#ifdef OKAY_*` exists in component headers (0 hits), which is good: nothing is conditional yet.
4. **Layers that are already clean:** `Math` (almost none outward), `Platform` (-> Core only), `Crypto` (0),
   `Graphics` (-> Core, Render), `Audio` (-> Scene, Math, Core; 3 Components), `Net` (-> Platform, Scene, Math,
   Core; 5 Components), `Physics` (-> Scene, Math; 8 Components), `Optimize`, `Modeling`, `Environment`.
   These are the leaves.
5. **Already data-driven:** `ComponentSerializerRegistry`, `PropertyRegistry`, `InputActions` (named actions, saved
   bindings). Nothing needs inventing; the existing registries are finished and used for everything.
6. **Input facts relevant to interaction:** `InputActions::Define("Interact", {"key:e", "pad:x"})`
   (`Input/InputActions.cpp:67`) already exists, also Reload (R / pad Y), Fire, Aim, Jump... but no engine component
   reads "Interact": `Seat.cpp` uses `Input::GetKeyDown('e')` (`HumanoidActions.cpp:2177`),
   `VisualScriptComponent.cpp:240-271` uses `GetKeyDown('e')` for `interact`/`use` and `OnInteract`, `ItemPickup`,
   `DialogueRunner`, `HoldPrompt` and `InventoryInteractable` each store their own `char interactKey`.

## 2. Module system, engine-wide

Owner requirement: every engine area is a module with a manifest; a project turns modules on and off; turning one
off removes its code from the exported game, not only its menus. Requirement 2: no duplicates: when a contract or
module replaces something, the old thing is deleted in the same PR.

### 2.1 Core (never strippable)
Math, `Core` (application, loop, time, paths, cvars, console, logging, prefs, jobs, scheduler, asset database,
localization tables), `Scene` (GameObject, Transform, Component/Behaviour, Scene, prefabs, serializer framework,
migration, `PropertyRegistry`, `ComponentSerializerRegistry`), `Platform` base + window/SDL backend, `Input` (Input,
InputActions, GameInput), `Graphics` (Image, Font), the renderer base (`Render/Renderer.hpp`, Camera, MeshRenderer,
Light, SpriteRenderer, Material, Mesh, software renderer) and `Debug` base (logging, profiler), `Crypto` (used by
save and net). Editor shell (window, dock, Hierarchy, Inspector, Console, Project panel, Build Settings) is core of
the editor. Core must not include any module; this is checked (2.4).

### 2.2 Module table (derived from the code; sizes are approximate lines, headers + sources)

| Module | Area | Contains (code) | Requires | Provides | Size | Strip |
|---|---|---|---|---|---|---|
| `physics3d` | Physics | Physics3D, Rigidbody3D, Collider3D, MeshCollider3D, TriMesh, Joint3D | core | IPhysics3D | 3k | yes |
| `physics2d` | Physics | Physics2D, Rigidbody2D, Collider2D, Joint2D, TilemapCollider2D | core | IPhysics2D | 2k | yes |
| `render3d_effects` | Render | Decal, LODGroup, Occluder, Volume, Water, LineRenderer, Cable, MeshModifiers; the `Optimize/` LOD, batch, occlusion, streaming code | core | - | 8k | yes (components); renderer-internal post effects, shadows, SSAO sit inside `GLRenderer.cpp`/`D3D*Renderer.cpp` and are not separable yet (see 2.6) |
| `particles` | Render | ParticleSystem | core | IFx | 1k | yes |
| `terrain_foliage` | Render | Terrain, TerrainDigger, VoxelTerrain, VoxelDigger, Foliage, WorldStreamer | physics3d | - | 5k | yes |
| `sky_weather` | Environment | DayNightCycle, Atmosphere, SkyStars, Weather, Wind | core | - | 1.5k | yes |
| `tilemap2d` | Render | Tilemap, SpriteAnimator | physics2d | - | 1.5k | yes |
| `splines` | Geometry | Spline, SplineMesh, DollyPath | core | - | 1k | yes |
| `modeling` | Modeling | PolyMesh, PolyOps, Modifiers, UVUnwrap, MeshAsset, EditTransform + editor Modeling window | core | - | 3.4k | yes (editor + engine) |
| `model_import` | Render | FbxReader/FbxScene, ColladaReader, Gltf, ModelImport, Assimp bridge | core | IModelImport | 6k | yes in the player (editor-time only; player needs only the baked mesh) |
| `audio` | Audio | AudioMixer, AudioClip, Mp3/Vorbis decoders, AudioSource, MusicPlayer | core | IAudio | 5k | yes |
| `animation` | Animation | Animator, AnimStateMachine, AnimClip/Control, Character(+AnimBackend), ModelAnimator, SkinnedMesh, BlendShapes, RootMotion, HumanoidLibrary/Actions/Motion, PlayerSetup, HumanoidRetarget | core | IAnimation | 11k | yes |
| `ik` | Animation | CharacterIK, ChainIK, FootIK, FullBodyIK, LimbIK, LookAtIK, AimIK, IKTarget | animation | - | 2.3k | yes |
| `cutscenes` | Animation | Sequence, SequencePlayer, VirtualCamera, camera brain, CameraFollow | animation | - | 1.7k | yes |
| `ragdoll` | Physics | Ragdoll | animation, physics3d | - | 0.5k | yes |
| `character_controllers` | Gameplay | Character/First/Third person, TopDown, Platformer, FreeRoam, ClickToMove, Stance, Traversal, StepUp, MoveSpeed, ControllerFeel | physics (2D or 3D), animation (soft) | IController | 10k | yes |
| `ui_kit` | UI | Canvas, UI* components, UIRenderer/Batcher/Layout, FocusGraph, EventSystem, TextRenderer, WorldUI, PauseMenu, SettingsScreen, LoadingScreen, Minimap | core | IUI | 14k | yes |
| `ai_navigation` | AI | NavMesh*, Navigation, Pathfinding, NavMeshAgent/Surface/Link/Modifier/Obstacle | physics3d | INav | 3k | yes |
| `ai_behavior` | AI | BehaviorTree, BehaviorTreeRunner, NPCController, perception (sight/hearing in NPCController) | ai_navigation, health | IPerception | 3k | yes |
| `health_damage` | Gameplay | HealthStat (the one model), HitZones, Damage types, Destructible, DeathLock, UIDamageNumbers hook | core | IHealth, IDamageable | 2k | no-by-default: core of gameplay; strippable only when no gameplay module is on |
| `interaction` | Gameplay | Interactable, InteractionSensor, HoldPrompt, HoldableObject, ProceduralObjectHolding, ItemPickup, LootSource | core | IInteractable | 2k | yes |
| `inventory` | Gameplay | Inventory, GridInventory, Equipment, Sockets, ItemDatabase, InventoryOps, Consumables, InventoryUI | interaction | IAmmoSource, IItems | 5k | yes |
| `crafting` | Gameplay | Recipe, Crafting, CraftingMenu, CraftingStation, ChestInventory | inventory | - | 1.5k | yes |
| `weapons` | Gameplay | Weapon, WeaponAttachment, Projectile3D, MeleeAttacker, Crosshair | health_damage, physics3d | IWeapon | 2k | yes |
| `vehicles` | Gameplay | VehicleController(+2D), Seat (vehicle seats), VehicleDamage | physics, interaction, health_damage | IVehicle | 1.5k | yes |
| `survival` | Gameplay | SurvivalStats (hunger/thirst/...), afflictions, zones | health_damage | - | 2k | yes |
| `rpg_stats` | Gameplay | Stats (levels, mana) | health_damage | - | 0.2k | yes |
| `dialogue_quests` | Gameplay | DialogueGraph/Runner, UIDialogueBox, QuestLog, StoryState + editor Dialogue window | interaction, ui_kit | IStory | 3k | yes |
| `building` | Gameplay | BuildMode, BuilderMode, StructureBuilder, BlockBuilder, BuildPiece | inventory (cost) | - | 3k | yes |
| `save_games` | Gameplay | SaveGame, SaveFramework(+Scene/Sync), GameSaves, Saveable, AutoSave | core | ISave | 5k | yes |
| `networking` | Net | NetworkManager, NetworkSync/Object/PlayerSpawner, NetInventory, transports, matchmaking, server browser, DedicatedServer, NetSim | core | INet | 11k | yes |
| `anticheat` | Net | AntiCheat, NetValidator, AntiCheatConfig, ClockCheck | networking | - | 2k | yes |
| `relay_online` | Platform | RelayServer client, accounts, friends, parties, store-platform glue | networking | - | 8k | yes |
| `voice` | Net | not present in the code (searched) | networking | - | 0 | slot reserved |
| `scripting_okayscript` | Scripting | OkayScriptVM, Script/ interpreter, ScriptComponent, ActionList, EngineApi | core | IScripting | 18k | yes (no-code games) |
| `scripting_visual` | Scripting | VisualScript graph, nodes, VisualScriptComponent, editor VS window | core | IVisualScript | 14k | yes |
| `ecs` | ECS | World, Systems, SceneBridge, NetWorld | core | - | 1k | yes |
| `localization_tools` | Core | LocScan/Check/Preview, SceneLocalization, Subtitles | core | - | 1k | yes (tables are core) |
| `editor_tools_*` | Editor | per-module editor panels (Navigation, BehaviorTree, Dialogue, FpHands, Animator, NetSim, Localization windows) | the module they edit | - | editor only | follows its module |

Notes: the `Optimize/` and `Debug/` (frame history, GPU timer, report) code is editor-time or dev-time and goes into
`dev_diagnostics` (strippable from the player); `Testing/` (SceneValidator, PlayTest, golden images) is dev-only. The
renderer backends (GL, D3D11, D3D12, console) are already chosen by platform and stay as build options, not modules.

### 2.3 Dependency graph and the edges to cut
Direction of the final graph: `core <- physics*, audio, animation, ui_kit, particles ... <- ik, ragdoll, controllers,
ai_* <- health_damage <- interaction <- inventory <- crafting, weapons, vehicles, dialogue_quests, building <-
networking-aware features`. `networking`, `scripting_*`, `save_games`, `ui_kit` are consumed by many modules but must
only be reached through an interface registered by the module.

Edges that block independent stripping, and the cut for each:

| Edge (verified in 1.12) | Cut |
|---|---|
| `SceneSerializer.cpp` names 175 components | Each module registers its records through `ComponentSerializerRegistry` (`write/read` lambdas) from its own `.cpp`; the chain shrinks until only core records remain. Move one component at a time with a byte-identical round-trip test on every existing sample scene (golden text). |
| `OkayScriptVM.cpp` and `EngineApi.cpp` name 75/33 components | An `ScriptApiRegistry`: a module registers its functions (`name, signature, doc, callback`) at module registration; VM looks up by name. The existing call sites become registrations in the owning module file. Unknown function in a script = "needs module X" error from the manifest `scriptApi` list. |
| Property tables (`BuiltinProperties*.cpp`) | Already `PropertyRegistry::Register(ComponentTypeInfo)`; split each table into the module's `.cpp`. |
| `SceneValidator`, `ProjectDiagnostics`, `SceneLocalization`, `Tween`, `SaveFrameworkScene` | A `ValidatorRegistry`/`SaverRegistry`/`LocalizableRegistry` of per-component callbacks, registered by the module (same pattern). |
| VisualScript nodes (header-only, one factory chain `NodeGraph.cpp:878`) | `VsNodeRegistry::Register(typeName, factory, pins, help, group)`; each node header moves into the module and registers in its module init. Group id = module name; the palette filters by the enabled set. |
| `Input/GameInput.cpp` includes 10 feature components | `UiBlockers` registry: a module registers `bool WantsInput(const Scene*)` ("inventory open", "dialogue active", "field focused"). `GameInput` iterates the registry. |
| `Core/` -> Components (SaveFramework scene savers, Tween, GameLoop) | Savers via registry (above); Tween's component targets via a `Tweenable` interface in `Scene`. |
| `Render` -> Components (43): renderer reads MeshRenderer, Light, Camera, Decal, ParticleSystem, Water, Terrain ... | Renderer base keeps Camera/MeshRenderer/Light/SpriteRenderer. Others implement `IRenderContribution { void Collect(RenderQueue&) }` registered per module; renderer iterates contributions. |
| `Physics` -> Components (8), components -> Physics (95) | Allowed direction (module requires physics). Cut only Physics -> Components via `IPhysicsListener`. |
| `Inventory/` -> Components (47) | Inventory components move into the `inventory` module (same module, internal edge). Edges from other areas into inventory go through `IAmmoSource`, `IItems`. |
| `Net` -> Components (5) and `Components` -> Net (31: controllers, health) | Controllers use `INet::IsLocallyOwned(go)` from core's `NetOwnership.hpp` (a core interface with a "single player" default); `NetworkManager` calls modules through `INetReplicated` (see 3.8). |
| `Scene` -> Components (373), mostly SceneSerializer + templates | `SceneTemplates.hpp` (shooter player, vehicle ...) splits per module into `<module>/Templates.hpp`, registered as GameObject menu entries via the manifest. |
| Editor `main.cpp` (47k lines) names every component and window | Add Component menu, GameObject menu, Project Settings sections and windows are built from the registries; each module contributes an `EditorModule` (see 2.4). |
| `Okay.hpp` umbrella | Stays for users; the check script (2.4) forbids including it from `engine/`. |

### 2.4 Module anatomy, the manifest, and adding a module later
Folder layout per module (new code goes here; existing code moves here module by module):
```
engine/modules/<name>/
  module.okaymodule         # manifest (text, below)
  include/okay/<Name>/...   # public headers (headers other modules may include)
  src/...                   # sources (private)
  register.cpp              # the only file the build links by name: void Register_<name>(ModuleContext&)
  editor/                   # optional EditorModule: panels, inspectors, menu items
  tests/                    # test_<name>_*.cpp, listed in the manifest
  docs/<name>.md            # doc page required by the manifest
```
Manifest (`module.okaymodule`, key=value like `project.okayproj`, parsed by one engine class `ModuleManifest`):
```
name=weapons          title=Weapons          area=Gameplay        default=on
requires=health_damage,physics3d             provides=IWeapon
components=Weapon,WeaponAttachment,Projectile3D,MeleeAttacker
records=weapon,weaponattachment,projectile3d          # scene records it owns
script_api=fire,reload,ammo,...                       # OkayScript functions it registers
vs_group=weapons                                      # visual-script palette group
editor=WeaponInspector,WeaponsWindow
assets=Assets/Modules/weapons/**                      # extra roots for export (PR #683 walk)
tests=test_gameplay_weapons*.cpp    doc=docs/modules/weapons.md    size_kb_hint=180
```
`register.cpp` does everything the module contributes, in one place: serializer records, property tables, script
API, VS nodes, validators, UI blockers, render contributions, templates, `NetReplicated` types. The generated file
`modules_enabled.cpp` (CMake, from the enabled list) calls `Register_*` for exactly the enabled modules, in
dependency order. That generated table replaces the central lists; no module is referenced anywhere else.

**Check script** `scripts/check_modules.py` (runs in CI, fails the build, prints "module A includes B/x.hpp but B
is not in A.requires"): reads each module's manifest, scans its files' `#include "okay/..."` lines, maps each include to the
owning module (by folder or by `components=`/header lists), and checks: (1) includes only from `requires`
(transitively resolved), core, or itself; (2) core includes no module; (3) no cycles; (4) every manifest key points
at an existing file; (5) doc page exists; (6) no `#ifdef OKAY_MODULE_` inside module code (a module is compiled once
and linked into many variants, see 2.5); (7) `Okay.hpp` is not included from `engine/`; (8) every `provides`
interface is declared in the core-side `okay/Gameplay` or `okay/Modules` interface headers.

**New module later:** copy `engine/modules/_template/` (a minimal module with a component, a record, a property
table, one VS node, one script function, a test and a doc page, all marked TODO), fill the manifest, run
`scripts/new_module.py <name>`, which also adds it to the module list the build reads. CI fails until the doc page
and the test exist.

### 2.5 Stripping code: honest options
Facts that decide it (1.11): users have no compiler; the player is one prebuilt program; release is cross-compiled in
CI; Web and Android are separate toolchains; the serializer/VM/properties currently reference every component, so a
linker cannot drop anything today (dead-code removal only works once registration is data-driven).

| Option | How | What it strips | Who compiles | Cost | Risk |
|---|---|---|---|---|---|
| (a) Per-module static libs (CMake OBJECT/STATIC) + generated `modules_enabled.cpp`; player links only enabled ones | the build target `okay_player(modules=...)` | everything inside modules: components, serializers, script bindings, VS nodes, validators | whoever has a toolchain: CI, developers, dedicated-server operators | Stage 0 + per-module conversion (the big work in 2.3) | Low, mechanical; only ordering of registration and hidden cross-includes. Gives nothing by itself to users without a compiler. |
| (b) Each module a shared library / plugin, loaded by manifest; "strip" = not shipping the DLL | dlopen at start | per-DLL, exact | nobody at export time: copy files | XL: every component header is inline C++ with `std::` types, function-local statics (`HealthDeathHook()`, `ActionList::Vars()`, registries keyed by `std::type_index`) that duplicate per DLL, `dllexport` on ~260 classes, MinGW cross-DLL RTTI and exception rules; not allowed on Web, console, mobile stores | High: ABI and duplicate-singleton bugs, slower start. Rejected. |
| (c) CI-built player variants for presets (for example 2D, 3D action, survival, full); the editor downloads the smallest variant containing the project's modules (same download path as `EnsurePlayer`) | CI matrix on (a) | exactly at preset granularity; extra modules present but unused | CI | S-M once (a) exists; CI links N variants (modules compile once, link many) | Medium: variant naming/versioning must match the editor; a project can get a superset, not exact. |
| (d) Ship per-module static libs per platform + a bundled linker (no compiler), and link the exact player on Build Game | `lld` + the `.a`/`.lib` set + generated `modules_enabled.o` | exact set, no compiler needed | the editor, at export | M-L: ship ~tens of MB of libs and a linker per OS, must match the editor version, SDL2 import libs, plus Web/Android excluded | Medium: toolchain bundling per platform, link errors need clear messages. |

**Recommendation:** (a) as the build system (it is the prerequisite for everything), (c) as the first distribution
step for users (presets, fast, no toolchain), and (d) as a follow-up only if the owner wants exact per-project sets
for desktop. Developers and the dedicated server get exact sets from (a): `cmake -DOKAY_MODULES=...` or
`okay-build --modules ...`. Not planned: hosted per-project cloud builds (needs infrastructure and trust).
Measured data to collect in Stage 0 before choosing between (c) and (d): size of each module in a link map, link
time of the player, and archive size of the module libs. Supplement: `-ffunction-sections -fdata-sections
--gc-sections` and LTO on the player once registration is data-driven (cheap, applies to every option).

Key design rule that makes (c) and (d) cheap: a module's object files must not depend on which other modules are
enabled (no `#ifdef` on module presence; absent contracts are found through registries and `Find<I>()` returning
null). Then each module is compiled once per platform and only the final link differs.

### 2.6 What is strippable at what granularity
| Thing | Granularity | Mechanism |
|---|---|---|
| Components and their scene records | per module | `register.cpp` not linked, record unknown to the loader (scene reports "module disabled", see 2.7) |
| Property tables and Inspector | per module | registered in `register.cpp` |
| Script API (OkayScript) | per function group = module | `ScriptApiRegistry`; the VM reports "function needs module X" |
| Visual-script nodes | per module group | `VsNodeRegistry` |
| Editor panels / menus / Project Settings sections | per module | `EditorModule` list; the editor always contains all modules for authoring, but shows only enabled ones (the editor binary is not stripped) |
| Network replicated types, RPC names | per module | `INetReplicated` registry |
| Assets | per module | `assets=` roots (#683 walk) |
| Renderer-internal effects (post effects, shadows, SSAO in `GLRenderer.cpp` 2.5k lines, D3D) | not separable in the first stages | later: split each backend into per-feature translation units behind `IRenderContribution`; until then they are core |
| Core (2.1), renderer base, serializer framework, component model | no | - |
| Editor binary | no (authoring needs everything) | only the exported player/server is stripped |

### 2.7 Dependencies: enforcement and reporting
- Build time: CMake reads `requires`; enabling `weapons` without `health_damage` fails configure with
  `module 'weapons' requires 'health_damage' (needed for IDamageable); enable it or disable weapons`.
- Editor: enabling a module turns on its transitive requires, listing them with the reason (`weapons needs
  health_damage: Weapon deals damage through IDamageable`) and one Confirm; disabling lists what breaks (modules that
  require it, scenes and prefabs that use its components, scripts and VS graphs that use its API/nodes).
- Before export: the Problems panel and Build gate run `module.disabled_component_in_scene` (scene records
  and prefabs), `module.disabled_script_api` (OkayScript uses a function from a disabled module, found by the same
  scan the walk already does), `module.disabled_vs_node`. Export is blocked until fixed or the user confirms
  "remove these components from the build" (they are dropped from the exported scene text, with a count in the
  report). Enabling/disabling in the editor is reversible until export; the choice never edits scenes.
- Export report (extends #683's `Report`): a "Modules" table: module, shipped yes/no, size contributed (from the
  per-module size table generated at build time, see 2.5), and a line `Modules removed: 7, saved 3.1 MB (-27%) vs full
  player`. Exact for options (a)/(d), the variant's size for (c) with the note "variant X contains: ...".

### 2.8 CI and test time
- `ctest` links `okay::engine` today. After Stage 0 the tests link a `okay::engine_all` interface that pulls every
  module lib; behaviour and time are unchanged (same objects, same link).
- New CI configurations: **core-only** (`OKAY_MODULES=core`, runs only core tests, a compile check for the
  independence rule), **all-on** (today's job), and the **integration matrix** (section 8). Because module libs are
  built once and shared, extra configurations add link time and the tests of that configuration, not recompiles
  (ccache plus the OBJECT libs). Expected: all-on stays the same; core-only adds a few minutes of tests; the matrix
  adds roughly one tiny-scene test per module pair. The per-module test lists also make a module's change run only its
  tests plus those of modules that require it (a `ctest -L module:<name>` label added in Stage 0).

## 3. Gameplay contracts (on top of the module system)

Principles: contracts live in `engine/include/okay/Modules/` (interfaces only, in core), each module implements its
own; a module depends on contracts, never on another module's classes; one `Find<I>(GameObject*)` per contract (self,
then parents); one `GameplayEvents::Fire(go, "on_x", "VsEntry", args)` (generalising `InventoryEvents::Fire`) that
reaches the script event, the visual-script entry, C++ listeners and, where flagged, the network. **Rule: every
contract lists what it replaces, and that code is deleted in the same PR.** No forwarding fields. The only
compatibility code allowed is the scene migration and the multiplayer wire messages (section 7).

### 3.1 Health (module `health_damage`)
```cpp
class IHealth : public IDamageable {
public:
    virtual float Current() const = 0;  virtual float Max() const = 0;  virtual float Armor() const = 0;
    virtual bool  IsDead() const = 0;   virtual bool  IsInvincible() const = 0;
    virtual float Heal(float amount, GameObject* source = nullptr) = 0;
    virtual void  Kill(const DamageEvent* cause = nullptr) = 0;  virtual void Revive() = 0;
    static IHealth* Find(GameObject*);
};
```
Implemented by `HealthStat` (kept name, kept record `stat_health`, the one model). Adds `Resistances`, percent armour,
events `on_damaged`, `on_healed`, `on_health_low`, `on_died`, `on_revived`.
**Replaces and deletes in PR 2:** health fields and `Damage/Heal/Revive/IsDead` of `SurvivalStats`, `Stats`,
`NPCController` (they read and write the sibling `HealthStat`); `DamageHealthOn`/`HealOn`/`DamageGameObject`;
`NPCController::Damage`; `MeleeAttacker`'s `npc->Damage`; the three armour branches in `Equipment::ApplyBonuses`; the
duplicate `{health}` prefs publishing (only `HealthStat` publishes); death signals collapse to one `on_died` event
(`HealthDeathHook`, `died` broadcast, `on_death`, `npc_died` become listeners/aliases only in the migration of
scripts: old script event names are renamed by the script migration, see 3.10). Old scenes: section 6.

### 3.2 Damage and resistances
```cpp
enum class DamageType : uint8_t { Generic, Bullet, Explosion, Melee, Fall, Fire, Cold, Poison, Starvation,
                                  Drowning, Collision, Environment };
struct DamageEvent { float amount; DamageType type; GameObject* source; GameObject* instigator;
                     std::string weapon, zone; Vec3 point, direction; bool hasPoint; uint32_t netTick; };
struct DamageResult { float applied; bool killed, blocked; };
class IDamageable { public: virtual DamageResult ApplyDamage(const DamageEvent&) = 0; };
```
Order: zone multiplier, type multiplier, flat armor + equipArmor, `resistance`, `minDamage`, i-frames/dodge. With empty zone
and neutral type the numbers equal today's (a test proves it first). `HealthStat::Damage(float)` and
`Damage(float, Vec3)` are removed; their callers (VS `DamageNode`, `NativeUIActions`, NoCode `DamageOnTouch`,
afflictions, `NetworkManager::ApplyHit`) build a `DamageEvent`.

### 3.3 HitZones
`HitZones` component: named zones with multiplier and optional limb state, resolved from the nearest humanoid bone to
`DamageEvent.point` (`humanoid::MapHumanoidBones`) or a named child collider. Not Ragdoll colliders (Ragdoll is a Verlet
chain). First release multipliers; limb state later. Replaces nothing (new), module `health_damage`.

### 3.4 Interactable (module `interaction`)
```cpp
class IInteractable { public:
    struct Context { GameObject* actor; bool hold; float heldSeconds; bool inspect; };
    virtual bool CanInteract(const Context&) const = 0;
    virtual std::string Prompt(const Context&) const = 0;     // localized
    virtual void Interact(const Context&) = 0;  virtual void Inspect(const Context&) {}
};
class Interactable : public Behaviour {   // range, holdSeconds, prompt, inspectText, action="interact" (override),
    ...                                   // requiredItem, requiredTag, oneShot; events on_interact, on_inspect,
};                                        // on_hold_progress, on_focus, on_blur
```
`InteractionSensor` (on the player) picks one target per frame and drives the prompt label; input comes from the
`InputActions` action named by `Interactable.action` (default `"interact"`, section 6). 
**Replaces and deletes in PR 3:** `InventoryInteractable` base, `InventoryInteractable::Nearest()`,
`ItemPickup::interactKey/interactRange/InteractBlocked/BlockInteract/ClearInteractBlock`, `DialogueRunner::interactKey/
interactRange/InRange/promptText` (prompt moves to the Interactable), `Seat::useRadius/prompt/playerInRange` and its
`GetKeyDown('e')`, `HoldPrompt`'s own range scans (`HoldPrompt` becomes the view: label only), the per-chest `open`
toggle logic (chest/craft implement `IInteractable`; `on_chest_opened/closed` stay as events), `HoldableObject` pick-up
key, and the interact branches in `GameInput.cpp` (`ItemPickup`/`HoldPrompt` loops, replaced by `UiBlockers`).
`LogicSource` buttons stay (they are the I/O link system); an `Interactable` next to one triggers it.

### 3.5 Weapon (module `weapons`) with ammo link
`WeaponDef` (id, fire mode Single/Burst/Auto, delivery Hitscan/Projectile/Melee, damage, range, fireRate, pellets,
spread + per-shot spread + recovery, aim spread factor, recoil pitch/yaw/recovery, magazineSize, reload times,
`ammoType`, damageType, projectile prefab/speed); `IWeapon { TryFire, TryReload, MagazineAmmo, ReserveAmmo, IsReloading,
SetAiming }`; `Weapon` component; `WeaponAttachment` (on a `Sockets` child: spread/recoil/damage/magazine modifiers).
Ammo: magazine count on the weapon (stored in `ItemInstanceData` when the weapon is an inventory item); reserve ammo
counted from the owner's `Inventory` by `ammoType` through `IAmmoSource`; with no `Inventory`, a plain number on `Weapon`
(decision kept: ammo in Inventory when one exists, else on Weapon).
Fire pipeline, once: spread, ray or projectile, `DamageEvent` per hit (zone from `HitZones`), `IDamageable::ApplyDamage`.
**Replaces and deletes in PR 4/5:** `TopDownShooterController` shooting, spread, pellets, magazine, `ammo`, reload, `on_fire/
on_reload/on_reloaded/on_empty` plumbing and its private `ammo` int (it keeps movement, aiming, dash and calls
`IWeapon`); `ThirdPersonShooterController`'s `on_fire` script hook and `fireRate`; `MeleeAttacker` (melee is
`Delivery::Melee`); the `float` damage path in `Projectile3D` (takes `DamageEvent`). Scripts that implemented `on_fire`
in the third-person shooter now use the weapon event; the script migration (3.10) rewrites the name.

### 3.6 Seat and vehicle seat (module `vehicles`)
`Seat` gains `role {Sit, Driver, Passenger, Gunner}`, `vehicle`, `doorName`; `IVehicle { SetDriver, Seats, Health }` on
`VehicleController`/2D. Enter/exit through `Interactable`; driver controls routed by `SetDriver`; passengers parented
and can use a `Weapon` in a Gunner seat; vehicle door = child `Interactable` + door `LogicTarget`; `VehicleDamage` on the
body's `IHealth` (collision impulse, smoke, wreck). **Replaces/deletes in PR 6:** the key check and range code in
`Seat`/`HumanoidActions::RequestSit` path that duplicates the Interactable, and `VehicleController`'s direct player
input read (driver input comes from `IVehicle::SetDriver`).

### 3.7 Inventory link
`IAmmoSource { Count, Take, Give }` on `Inventory`/`GridInventory`; weapon and armour data stay in `ItemDefinition.custom`
first. `Equipment` applies armour to `HealthStat` only.

### 3.8 Network contract (multiplayer syncs all of it)
`NetworkSync.properties`: list of `{component, path}` (PropertyRegistry paths) synced server-authoritatively on change
(reliable `__prop/<netId>/<path>`). Default synced when an object has `NetworkSync`: `HealthStat.health/dead`,
`Seat.occupied/driver`, `Weapon.magazine` (owner only), `Interactable` open state of doors/chests. Events flagged
`replicate` fire on every peer from the server's message. Hit claims: `ClaimHit` carries target, weapon id, origin,
direction, zone; the server computes damage from the `WeaponDef`. Clients never apply damage. Interact requests are
RPCs `interact/<netId>` run on the server.

### 3.9 Module manifest
Defined in 2.4 (replaces the earlier gameplay-only sketch). Gameplay modules are rows of the same table.

### 3.10 Scripting: one set of nodes and APIs per contract
OkayScript and visual script are generated from the same contract description: each contract has a small
`*.contract` data file (`health.contract`: functions `damage(amount, type?)`, `heal`, `health()`, `max_health()`,
`is_dead()`, `kill()`; events `on_damaged`, `on_died`...). The module's `register.cpp` turns it into (1) OkayScript
functions via `ScriptApiRegistry`, (2) visual-script nodes and events via `VsNodeRegistry` (group = module), (3) the
docs table of the module doc page. So `damage(...)` and the "Damage" node are the same call, with the same argument
names, and both appear only when the module is on. Existing node and function names stay as the public names
(`GlGetHealth`..., `damage`, `heal`); nodes that took a bare float gain optional type/zone pins. Scripts that read old
event names (`on_death`, `npc_died`, `died`, `on_fire` in the shooter) are rewritten by a project migration run by
the editor on open (a text rewrite of `.okayscript` and `.okayvs`, listed in the log, reversible through the existing
backup); wire aliases are not kept in the runtime.

## 4. Everything works together, easily

### 4.1 Auto-wiring (no manual setup)
Each component declares needs in its manifest-backed `ComponentInfo`: `needs` (hard), `suggests` (soft), with a
default creator. The Inspector shows a banner "Needs: Health Stat (not on this object) [Add what this needs]". One
click adds the missing pieces with defaults. Rules that run without clicking (at Add Component and at load):
- `Weapon` on an object with a controller: links to the controller's aim ray and adds nothing unrelated. It looks
  for an `Inventory` on the owner (reserve ammo) and falls back to its own counter.
- Damage targets: any object with `IHealth` is hit. `Weapon` raises a Problems warning if no `IHealth` exists in
  the scene (4.5).
- `Seat` with a `vehicle` finds the `VehicleController` in its parents.
- `Interactable` finds `InputActions` "interact"; if the project has no such action the project defaults are created.
- `NPCController`/`BehaviorTreeRunner` auto-add `HealthStat` (they need `IHealth`), and perception reads weapon fire.

### 4.2 Worked examples across areas (all through contracts, no wiring)
1. **Weapon -> Health -> feedback:** `Weapon.TryFire` hits a crate with `HealthStat`; `DamageEvent{Bullet, zone, point,
   direction}`; `on_damaged` is heard by `audio` (hit sound by DamageType), `particles` (impact at `point`),
   `render3d_effects` (a `Decal` at `point` aligned to the normal), `ui_kit` (`UIDamageNumbers`), `survival`
   (nothing). Each is a listener registered by its module on `on_damaged`; with a module off its listener does not exist.
2. **NPC perception:** `Weapon` fires `on_fire` with the position; `ai_behavior`'s perception listens (hearing range)
   and the behavior tree reacts; `NPCController` receives `on_damaged` from `HealthStat`, becomes hostile, flees by
   `fleeHealthPct`, dies via `on_died`, `LootSource` drops items (interaction/inventory).
3. **Ammo:** `Weapon.TryReload` asks `IAmmoSource` of the owner for `ammoType`; the inventory screen shows the same
   count; picking up ammo (`ItemPickup` -> `Inventory`) raises reserve ammo without touching the weapon.
4. **Vehicle:** pressing "interact" near a door focuses an `Interactable`; the Seat with role Driver claims the actor;
   `IVehicle::SetDriver` gives that actor's input (`Move`, `Jump`=handbrake) to `VehicleController`; a crash applies
   `DamageEvent{Collision}` to the vehicle's `IHealth`; occupants take explosion damage through their `IHealth`.
5. **Multiplayer:** all of the above run on the server; clients see `HealthStat.health`, `Seat.occupied`, door state and
   the weapon events through 3.8, and send only inputs, hit claims and interact RPCs.
6. **Save:** `save_games` saves any component whose module registers a saver; weapons, health, inventories, seats are in
   the same save with no extra wiring.

### 4.3 Easy dependency handling (UX)
Enabling a module enables its requires (list + reason, one Confirm). Disabling shows what breaks. Add Component shows
`needs Health Stat` as a hint on each row and a one-click "Add with what it needs". Each module has a **Setup** button
(Project Settings > Modules, and the module's menu) that creates a working example in the current scene (for Weapons: a
player with the matching controller and `Weapon`, a target with `HealthStat`, ammo item in an `Inventory`, a HUD ammo
text) from a per-module `setup` function listed in the manifest, undoable as one step.

### 4.4 One inspector layout and one naming scheme
Same concept, same field name and unit across modules: `damage`, `range` (meters), `cooldown` (seconds, replaces
`fireRate`/`attackInterval`/`respawnDelay` style names: `rate` stays as shots per second only in `WeaponDef`),
`prompt`, `action` (the `InputActions` name, replaces per-component `key` chars), `radius`, `duration`, `enabledInteract`.
Each module's inspector uses the same section order: Basics, Limits, Feedback (audio/fx), Events, Network. The
`PropertyBuilder` gets `.Group("Basics")` and a style check test fails when a registered property in a gameplay
module uses an alias of a reserved name (`key`, `fireRate`, `interactRange`...). Renames for old scene fields are in
the migration (section 6).

### 4.5 Problems panel: mis-wired combinations
New rules (in `ProjectDiagnostics`, each registered by its module): `weapon.no_health_target` (a Weapon and no
`IHealth` in the scene), `weapon.no_ammo_source` (ammoType set, owner has no Inventory), `interactable.no_action`
(action not defined in `InputActions`), `interactable.no_sensor` (no `InteractionSensor` on any player),
`seat.no_vehicle_controller`, `npc.no_health`, `health.no_ui_listener` (info), `module.*` rules from 2.7.

### 4.6 Scripting parity
See 3.10. The same registration feeds both; an integration test calls `damage` from OkayScript and the Damage node on
the same object and compares results; a coverage test (like `test_vs_node_docs`) fails when a contract function
has no node or no script function.

## 5. Choosing modules per project

### 5.1 New project flow
Launcher/editor New Project dialog gets a second step "What do you want in this project?":
- A checklist grouped by area: **Combat** (health and damage, weapons, hit zones), **Survival**, **Vehicles**,
  **Characters and animation**, **Dialogue and quests**, **Inventory, crafting and building**, **AI**, **World and
  rendering** (terrain, sky, particles, effects), **Audio**, **UI**, **Multiplayer** (networking, anti-cheat, online),
  **Scripting** (OkayScript, visual script), **Tools** (modeling, cutscenes, localization).
- Each row: name, one-line description, `requires` ("also needs: Health and damage"), and size cost ("+0.4 MB in the
  game"), from the manifest `size_kb_hint` and the measured table.
- Presets (2D platformer, 3D action, survival, shooter, vehicle game, story game, multiplayer) are buttons that
  pre-tick the list; the user can change any tick. Ticking a row ticks its requires (shown), unticking a required one is
  blocked with the reason.
- Templates (the existing project templates such as "Third Person Shooter", "Main Menu", "Inventory",
  `editor/main.cpp:17918`) carry `needs_modules`; choosing a template pre-ticks and locks those rows (can be unlocked
  with a warning because the template would not run).
- Stored in `project.okayproj` as `modules=health_damage,weapons,...` (`ProjectSettings` gains `modules`,
  `ParseProjectSettings` ignores it in older editors). An existing project without the key = all modules on until the
  user opens the page. Core modules are not listed.

### 5.2 Project Settings > Modules
One row per module with toggle, doc link, requires/required-by, size, and "used by: N objects in M scenes, K scripts"
(click to see the list). Turning off lists what uses it; the change is reversible until export and never edits
scenes. Components of a disabled module stay in scenes (read-only "module disabled" header) and are dropped only
from the export, with the count in the report. The Add Component popup and GameObject menus, VS palette and script
function help hide disabled modules; a hidden VS node in a saved graph shows a "module disabled" badge and the graph
still loads.

### 5.3 Export (relation to #683)
See 2.7. Manifest `assets=` globs feed `WalkAssets` as extra roots; assets under a disabled module's folder are not
roots and appear under "excluded: module X disabled". `module.*` Problems rules join the Build gate. The size table
comes from the build, not from guesses.

## 6. Scene format v3 -> v4 and old-project migration
- `kSceneFormatVersion` 3 -> 4 (`SceneSerializer.hpp:72`), `SceneMigration::Step{from=3, name="gameplay-contracts",
  needsEngine=true}` (needs component classes, like the character-animator step). It runs on open and on
  `--build-game`; old files are rewritten once, with a log and a backup copy (existing `Backup`).
- Health: objects with `survival`, `stats` or `npc` records get a `stat_health` line holding their old health values
  (max, armor, resistance, godMode, regen from SurvivalStats `regenWhenFed`), the old fields are dropped from those
  records on write. `Stats.defense` becomes `HealthStat` flat armour with `minDamage=1`. `NPCController.fleeHealthPct` stays
  on the NPC (behaviour, not health).
- Interaction: `ItemPickup.interactKey/interactRange`, `DialogueRunner.interactKey/interactRange/promptText`,
  `Seat.useRadius/prompt`, `ChestInventory`/`CraftingStation` key and range become an `interactable` record on the same object
  (`range`, `prompt`, `action`); a non-default key char maps to a per-interactable override `key:<c>`; the default
  `e` maps to action `interact`.
- Weapons: `topdownshooter` shooting fields move to a `weapon` record (field for field); `thirdpersonshooter` gets a
  default `weapon` (damage taken from the shooter's script `on_fire` is not inferable: a Problems warning says
  "no damage set").
- Seats: `seat` record keeps its fields (`useRadius`, `prompt` move to the interactable record); new fields trail.
- Scripts and graphs: the project migration renames old event/function names (3.10) and replaces `GetKeyDown('e')`
  based `interact/use` by the action (section below). Unknown renames are reported, not guessed.
- Prefabs and variants use the same migrator. A scene with newer version than the build loads "what is understood"
  with a warning (existing behaviour at `SceneSerializer.cpp:4072`).
- Tests use checked-in fixtures saved by today's main: a scene with all four health components, chests, pickups,
  dialogue, seats, shooters; migrate, load, compare health/range/prompt values; second migration is a no-op.

### 6.1 Interact key
Default is the action `Interact` (key E, pad X) that `InputActions.cpp:67` already defines, saved bindings under
`input.interact` (`ApplySaved`). Changes: `Interactable.action` defaults to `"interact"` and can name any action
(per-interactable override such as `"open_door"`); the Controls screen (needs `RemapModel` UI) lists it; hold-to-use
uses `InputActions::Held`. Removed in PR 3/6: hardcoded `GetKeyDown('e')` in `HumanoidActions.cpp:2177` (Seat),
`VisualScriptComponent.cpp:240-271` (`interact`/`use` queries and `OnInteract` now ask the focused Interactable /
the action, `OnInteract` fires on `on_interact` of the focused target), every `char interactKey` field listed in 3.4, and
FirstPersonController's "lean right is E" conflict is resolved by giving lean its own action (`LeanRight`). A project
that rebinds `interact` to F changes all of them at once, with no per-component edit.

## 7. Multiplayer compatibility
- Wire messages that must stay, with removal: `__dmg` (`<netId>|<health>|<damage>|<shooter>`) and the `__hit` claim
  keep their current format for the release that ships PR 1 (N) and N+1; N+2 removes them in favour of
  `__prop` (health) and the extended claim. New clients accept both. Old clients connecting to a new host get `__dmg` for
  that period; after removal the lobby shows a version mismatch (the relay already carries
  `kRelayProtocolVersion` with a min version, `RelayProtocol.hpp:40`).
- `NetworkSync` fields (`netId`, `authority`, `owned`, `syncAnimation`, `clientClaimable`) unchanged; the new
  `properties` list is optional.
- Authority: server owns health, ammo validation (via `NetValidator "hit/<weapon>"`), seats, door state, interact
  results; the owning client owns its transform; the driver's client gets the vehicle transform on entering a driver
  seat. Clients never apply damage.
- Disabled-module objects: a client and server built with different module sets refuse to join (module list hash in the
  handshake with the names of the missing modules in the message), because replicated types would not exist.

## 8. Verification: integration matrix and CI
- **Pair/triple matrix** (file `tests/integration/matrix.txt`, one line per combination, generated list checked by a test
  so a new module must add its row): each combination builds a tiny scene with only those modules and asserts they
  cooperate. Minimum set: weapons+health (damage lands, zone multiplier), weapons+inventory (reload uses reserve),
  weapons+audio/particles/decal (hit feedback fires on `on_damaged`), weapons+ai_behavior (NPC hears the shot, turns
  hostile), health+survival (starving damages), interaction+inventory (chest, pickup), interaction+dialogue,
  vehicles+interaction+health (enter, drive, crash damage, exit), vehicles+networking (occupancy replicates),
  networking+health+weapons (server-validated hit), save_games+all gameplay (round trip), scripting_okayscript vs
  scripting_visual on each contract (same result), building+inventory (cost charged), ui_kit+inventory (drag/drop).
- **Configurations in CI:** core-only (build, core tests, check_modules), all-on (today), each preset configuration
  builds and runs its integration rows; the exported-player check (`tests/export/verify.sh`) runs with one module off
  and verifies a scene using it fails the Problems gate, and with it on loads.
- `check_modules.py` (2.4) runs on every PR.
- Problems rules (4.5) are tested with a scene per rule.

## 9. Migration plan (ordered PRs)

This is a large effort: about 34 PRs in five stages. Every PR builds, passes the full existing suite, adds its tests,
leaves nothing hidden or off, and contains no dead code (a registry lands with its first user; a contract lands with its
first consumer and deletes the duplicate). Sizes: S < 300 lines changed, M 300-900, L 900-2500, XL > 2500 (mostly
moves). Because many PRs are file moves, they are reviewed as "moved, not changed" with a golden-text round-trip test.

**Stage 0: build per module, no behaviour change**
1. **Module build skeleton (M).** `engine/modules.cmake` with `okay_add_module(name SOURCES ... REQUIRES ...)`;
   convert the single `add_library(okay_engine ...)` list into module groups in one file (core + one module per group
   of 2.2, still compiled in all builds); `okay::engine` becomes an interface over all module libs; `ctest -L module:`
   labels; no source moves, no code change. Test: whole suite identical; player and editor still link. Risk: link
   order of static libs (circular references between areas, the edges in 2.3): use one library with OBJECT groups
   until cycles are cut; verify the player size is unchanged within 1%.
2. **Registries that already exist become the only path (M).** Introduce `ModuleContext` + the generated
   `modules_enabled.cpp` calling `Register_<module>()` for each group (empty at first) from `Application` init; add the
   manifest parser `ModuleManifest` and `scripts/check_modules.py` in report-only mode that lists every violating edge
   (the "problem list" from 1.12) so later PRs show the list shrinking. Tests: manifest parse, check script on a fixture.
3. **Link-size and module-size report (S).** Link-map script producing `module_sizes.json` for the player and editor;
   shown by Build Game's report ("Modules" table with all `shipped`). Needed to pick (c) vs (d) with data.

**Stage 1: cut the central registries (each PR leaves the engine building, all tests passing)**
4. **Serializer registry completion, part A (L):** move the records of the leaf modules (`audio`, `particles`,
   `splines`, `sky_weather`, `tilemap2d`, `ecs`) from the if/else chain to `ComponentSerializerRegistry`. Test: every
   sample/fixture scene round-trips byte-identical.
5. **Serializer part B (XL):** rendering/physics/animation/ik/cutscenes/ragdoll records.
6. **Serializer part C (XL):** gameplay and UI-kit records (UI kit is 48 components: split in two PRs if > 2500 lines).
7. **Property tables to modules (L)**, one PR per `BuiltinProperties*.cpp` file (3 PRs, M each).
8. **ScriptApiRegistry (L):** OkayScript and `EngineApi.cpp` functions registered by modules; VM uses the registry;
   tests: every documented function still resolves, unknown name reports the module.
9. **VsNodeRegistry (L):** nodes register themselves; generated editor tables (`gplib_*.inc`) get a module column;
   `test_vs_node_docs` still passes.
10. **Validators, savers, localizable, tween targets, UI blockers registries (M each, 2 PRs).** `GameInput` stops including
    features.
11. **Render contributions (L):** `IRenderContribution`, decal/particle/water/terrain/LOD collect through it. Golden
    image tests unchanged.
12. **Net replication interface `INetReplicated` (M):** NetworkManager stops naming components.
13. **Templates and editor menus from registries (L):** `SceneTemplates.hpp` split per module, Add Component built from
    `ComponentTypeInfo.category` + `needs`, hierarchy menu from manifests (this removes the 39679-style hand lists).
14. **check_modules.py becomes blocking (S)** once the list from PR 2 is empty for core; per-module requires filled in.

**Stage 2: convert modules, leaves first (each PR moves one module's files into `engine/modules/<name>/`, writes its
`module.okaymodule`, `register.cpp`, doc page and tests labels; behaviour unchanged)**
15. `audio`, `particles`, `splines`, `sky_weather`, `tilemap2d`, `ecs` (M each; 3 PRs grouped).
16. `physics2d`, `physics3d`, `render3d_effects`, `terrain_foliage`, `modeling`, `model_import` (M-L each).
17. `animation`, `ik`, `cutscenes`, `ragdoll` (L, L, M, S).
18. `ui_kit` (L), `character_controllers` (L), `ai_navigation`, `ai_behavior` (M each).
19. `scripting_okayscript`, `scripting_visual` (L each: they depend on everything via registries, now clean).
20. `networking`, `anticheat`, `relay_online` (L, M, M), `save_games` (M), `localization_tools` (S).
21. Gameplay: `inventory`, `crafting`, `dialogue_quests`, `building`, `survival`, `rpg_stats`, `vehicles` current code
    moved as is (M each). Contracts (stage 4) then change them in place.
22. **Core-only build (M):** CMake option `OKAY_MODULES`, player target with a module list, `core-only` CI config
    green, `module sizes` real (strip actually works at build level). This is the point where stripping exists for
    developers and the dedicated server.

**Stage 3: manifest, Modules page, picker, export**
23. **Manifest enforcement + Modules page + Add Component/VS filter (L):** `modules=` in `project.okayproj`, Project
    Settings > Modules with requires/required-by, "used by", reversible toggles, disabled-component headers; Problems
    `module.*` rules. Depends on 22. Test: project file round trip with and without the key, filter, rule fixtures.
24. **New Project picker (M):** step 2 of New Project (5.1), presets, template `needs_modules`, size column.
25. **Export integration (M):** export report "Modules" table, extra roots from `assets=`, drop disabled components from
    exported scenes; needs #683 merged (if not, the same PR set lists the missing part, no stub).
26. **Distribution of stripped players, option (c) (M):** CI matrix of preset variants published beside the player,
    `FindPlayer`/`EnsurePlayer` pick the smallest variant covering the modules; report line "variant X (contains ...)".
    Optional follow-up PR 27: option (d) link-only export (L), decided after the PR 3 size data.

**Stage 4: gameplay contracts on top (each deletes the duplicate it replaces)**
28. **Health + Damage contracts (M)** (3.1, 3.2) including `Modules/` interface headers, `GameplayEvents`, and
    all damage callers moved to `DamageEvent`; deletes `DamageGameObject/DamageHealthOn/HealOn`, `Damage(float)` forms.
29. **Health merge + scene v3->v4 (M)** (3.1, 6): `SurvivalStats`, `Stats`, `NPCController` lose their health fields and
    classes' health code; migration step with fixtures; Equipment simplified; script migration for death events.
30. **Interactable + InteractionSensor + interact action (M)** (3.4, 6.1): deletes the per-component keys/ranges,
    `InteractBlocked`, `Nearest()`, the `'e'` checks.
31. **Weapon, top-down port + Inventory ammo (L)** (3.5): deletes the shooter's shooting/reload/ammo code and the
    `MeleeAttacker` class; wire claim extended.
32. **Third/first-person on Weapon + recoil + attachments (M + M).**
33. **Vehicle seats, doors, vehicle damage (L + M)**, then **HitZones with limb state (M)**.
34. **Auto-wire + Setup buttons + naming style test + Problems rules + scripting parity (M + M):** `needs/suggests`
    infrastructure (can land as part of 28 for health and extend), per-module `setup` functions, naming test,
    contract-file generator for scripts and nodes (3.10) with parity tests, **network property sync (M)**, and the
    **integration matrix** (8): one matrix row ships with each PR above; this PR adds the harness and the CI configs
    (core-only, presets) if not already in place (S-M).

Counts: Stage 0: 3, Stage 1: 11, Stage 2: about 12 (groups), Stage 3: 4-5, Stage 4: about 9 (some PRs have two
parts). Roughly 40 PRs in total. The first three (concrete):
1. `build: module groups in engine CMake` (PR 1): adds `engine/modules.cmake`, groups the 258 sources, labels tests
   `module:<name>`, keeps one linked result. Acceptance: all tests, same binary size within 1%.
2. `build: module manifest parser + check_modules.py report` (PR 2): `ModuleManifest`, `ModuleContext`, generated
   `modules_enabled.cpp`, report of the include edges from 1.12 (expected: about 25 violating edges, Scene->Components
   first).
3. `build: per-module size report` (PR 3): link-map script, `module_sizes.json`, a Modules table in the Build report,
   decision data for 2.5.
Stage 4 can start (28, 29) in parallel with Stage 1 because it touches different files (health/damage code), except
the serializer part, which must be rebased on Stage 1 PRs 4-6.

### Tests per PR (summary)
Stage 0: unchanged suite plus manifest/check-script fixtures. Stage 1: golden-text round trip of all scenes under
`tests/assets`/examples, registry lookups, node/function coverage, golden-image tests for rendering. Stage 2: module
label runs, `check_modules` clean for the moved module, the core-only build compiles. Stage 3: project file,
filters, Problems rules, export layout (`tests/export/verify.sh`), picker presets. Stage 4: per contract (damage order
matrix equal to today's, interaction single-target and hold, weapon ammo/reload/spread bounds, seats, migration fixtures,
network authority tests with the net sim), plus its matrix row.

### Risks (honest)
- Volume: SceneSerializer (9.7k lines) and OkayScriptVM (10.9k lines) conversions are the bulk; each must be byte-
  and behaviour-identical. Mitigation: golden round trips, small PRs per module, no refactoring while moving.
- `editor/main.cpp` (47k lines) is touched by menu/registry work and by #683; land #683 first, put new UI in new files.
- Hidden cross-includes: the check script's first report will be longer than expected; budget for fixing edges in
  stage 1 rather than hoping.
- Static-library link order and duplicate registrations: tests that assert every registered name is unique.
- Compile time: more targets, more headers parsed once per module; mitigate with ccache and combined-source builds only per module.
- Variant matrix (option c) grows with modules; keep presets few (<= 8).
- Scene v4 migration is one-way: backup and fixture tests; older players refuse or warn on v4.
- Performance: registry lookups (script API, VS nodes) happen at load/first call, not per frame (verify in PR 8, 9).
- Web/Android: only desktop and dedicated server are covered by (c)/(d); Web uses (a) CI variants.

### What to verify on a real run
1. After each Stage 1 and 2 PR: open every example scene and the project templates, play 2 minutes, save, diff scene
   text (should be identical), compare player size and start time with the previous build.
2. After PR 22: build a core-only player and run an empty scene; build all-on and run a shooter sample.
3. After PR 23-26: create a project with the picker (3D action preset minus audio), export, check the report's saved
   size, run the exe; open the same project and enable audio again, export again.
4. After each Stage 4 PR: the sample scenes of that system in single player, and host + one client with the net sim.
5. Scripting: run one OkayScript and one visual graph that damage, heal and interact on the same sample.

## 10. Open questions for the owner (max 6)
Decided by the owner and built in: no duplicate features and no forwarding fields (health merged and old
fields/classes removed with the v3->v4 migration); code stripping, not only menus; a new-project module picker with
presets as shortcuts; interact is the `interact` action, default E, rebindable. Kept at the recommended defaults:
ammo lives in `Inventory` when one exists, otherwise on `Weapon`; scene format version 4 with a migration.

1. Distribution of stripped players: start with CI-built preset variants (c) and decide on the link-only export (d)
   after the size data from PR 3? Recommended: yes.
2. Should the editor itself be strippable (a lighter editor without modeling, dialogue, behaviour-tree windows)?
   Recommended: no, the editor always contains everything; only the exported game is stripped.
3. Voice chat has no code today: reserve a `voice` module slot or leave it out until it exists? Recommended: leave out
   of the table until code exists (the manifest format already supports adding it).
4. Renderer-internal effects (post effects, shadows, SSAO) are not separable without splitting the GL/D3D backends:
   do it as a later stage or never? Recommended: later, only if the size data shows a worthwhile saving.
5. Module list in the join handshake: refuse to join on a mismatch (recommended), or allow with missing types ignored?
6. Version for removing the `__dmg` wire compatibility: release N+2 (recommended), or keep until the next protocol
   bump?
