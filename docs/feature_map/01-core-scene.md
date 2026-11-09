# 1. Core & scene

The engine's spine: the main loop and clock, timers and tweens, the scene graph (Scene, GameObject, Transform, Component), scene loading and saving, prefabs, save games and prefs, localization, events and ECS, logging and profiling, the asset manager and asset database, and the runtime utilities, property registry and scene text tools that merged in waves 4 and 5.
Covers `engine/include/okay/Core` (46 headers, 37 sources in `engine/src/Core`), `Scene` (25 headers, 19 sources; `SceneSerializer.cpp` alone is about 8.8k lines), `ECS` (5 headers, header-only) and three editor files. The data layers for undo, debugging, optimization and testing live in `Debug`, `Optimize` and `Testing` (areas 22, 23, 27, 33).
Everything up to #578 is on main: 2.849.0 is published (it holds #571 and #572) and 2.850.0 is being cut with #574, #575, #578 and #579. #579 (loop and time 1.1) merges next; a second 1.1 pass waits on branch `claude/loop-time-wave2` (no PR yet). Nothing else is open in this area.

## 1. Core & scene

### 1.1 Loop and time
- **What:** The game loop and the global clock: how a frame starts, how long it lasted, how fast time runs, whether the game is paused or asked to quit, plus the seedable random generator and UTF-8 helpers every other system leans on. PR #579 (loop and time 1.1, merging with 2.850.0) put one frame driver, `GameLoop`, under `Application` and added named clocks, hit stop, pause and quit reasons and random extras.
- **Parts:**
  - 1.1.1 Main loop - `Core/Application` - `Application::Run` is now a thin loop on `GameLoop` (same defaults); the terminal is closed by a destructor so a throwing component no longer leaves it raw; `Config::maxDelta`, `fixedStep`, `maxFixedSteps`, `catchExceptions`, `app.loop()`, `ExitCode()`; defaults to the console renderer (the editor and player still run their own loops)
  - 1.1.2 Global clock - `Core/Time` - DeltaTime, UnscaledDeltaTime, ElapsedTime (also as double), FrameCount, TimeScale (capped at 100), Frozen, SmoothDelta, SmoothedFPS, `FixedTime`/`InFixedStep`; a NaN, infinite or negative frame time counts as a 0 s frame
  - 1.1.3 Pause and quit flags - `Core/Game` - `Game::Paused/SetPaused/TogglePause` freeze time without touching the scale; `RequestQuit/QuitRequested/Reset` are read by the player and editor Play each frame
  - 1.1.4 Random generator - `Core/Random` - seedable xorshift stream with a shared instance, Range, Value, Gaussian, Shuffle, weighted pick, save/restore of the whole state; #579 adds `Chance`, `Fork`, `SeedFromString`, `ThreadLocal`
  - 1.1.5 UTF-8 helpers - `Core/Utf8` - decode with replacement characters for bad bytes, length, offset, append (surrogates and reads past the end fixed in #579); used by text drawing and localization
  - 1.1.6 Frame driver - `Core/GameLoop` - `GameLoop::BeginFrame(realSeconds)` cleans and clamps the frame time (0.25 s), counts hitches, fires OnPause/OnResume, steps Time and the clocks, runs `OnFixedUpdate` (up to 5 steps, the surplus is dropped, not owed), then `OnUpdate`/`OnLateUpdate`; `Alpha()` for smooth drawing; `BeginSession()` is a clean Play
  - 1.1.7 Named clocks - `Core/Clock` - `Clock::Get("game")`, `"ui"` (real time, ignores pause), `"cinematic"` (real time, holds on pause) and your own channels, each with its own scale, pause and `Delta()`; `ClockSet::Global()` is ticked once a frame by the loop
  - 1.1.8 Hit stop, time-scale ramps, real time - `Core/Time` - `Time::HitStop(seconds, scale)` freezes or slows on real time and composes with the script's time scale (up to 8 at once); `RampTimeScale(target, seconds)`; `RealtimeSinceStartup`, `UnixSeconds`, `FormatDuration`, `FormatClock`; `SetClockSources` test clock hook; `EffectiveTimeScale`
  - 1.1.9 Pause reasons, focus pause, quit reasons - `Core/Game` - `PushPause("menu")`/`PopPause` so two systems cannot un-pause each other, `SetPauseOnFocusLoss` + `NotifyFocus`, `RequestQuit(reason, code)`, `SetQuitGuard` (a veto, for "unsaved changes?"), `ForceQuit`, `ExitCode`, `BeginSession` (Reset plus time scale 1)
  - 1.1.10 Random extras - `Core/RandomExtras` - `rnd::PointInCircle/PointOnCircle/PointInRing/PointInSphere/PointInBox/Direction2D/Direction3D`, `ShuffleBag`, `WeightedTable` (with and without replacement), dice notation (`RollDice("2d6+3")`, `ParseDice("4d6kh3")`); works with `Random` and with `Rng` (1.1/36.7)
  - 1.1.11 Frame stepping, debug time scale, capture mode - In flight (branch `claude/loop-time-wave2`, no PR yet) - `Core/GameLoop` - `RequestStep(n, dt)` advances a paused game exactly n frames, `SetDebugTimeScale` for slow motion or fast forward while debugging, `SetCapture(fps, frames)` makes every frame exactly 1/fps for screenshots and golden tests, `frameBudget`
  - 1.1.12 Frame recording and replay - In flight (`claude/loop-time-wave2`) - `Core/FrameRecord` - `FrameRecorder`/`FramePlayer` and the small text file `okayframes 1` (checksummed, strict loader); a replay through a `GameLoop` with the same options reproduces `Time` values bit for bit (per docs and test names; not on main)
  - 1.1.13 Frame statistics and server tick pacer - In flight (`claude/loop-time-wave2`) - `Core/FrameStats`, `Core/TickPacer` - `GameLoop::Stats()` keeps a 240-frame window (fps, 1% low, worst frame, hitches) and can sync into `Debug/FrameHistory`; `TickPacer` is the pacing arithmetic `DedicatedServer` now uses (`engine/src/Net/DedicatedServer.cpp` changes by 18 lines there)
- **Code:** `Core/Application`, `Core/Game`, `Core/Time`, `Core/Random`, `Core/Utf8`, `Core/GameLoop`, `Core/Clock`, `Core/RandomExtras`; In flight: `Core/FrameRecord`, `Core/FrameStats`, `Core/TickPacer`
- **API:** `Application::Config`, `Application::Run`, `GameLoop::BeginFrame`, `GameLoop::Alpha`, `Clock::Get`, `Time::DeltaTime`, `Time::SetTimeScale`, `Time::HitStop`, `Time::RampTimeScale`, `Time::SmoothDelta`, `Game::SetPaused`, `Game::PushPause`, `Game::RequestQuit(reason, code)`, `Game::SetQuitGuard`, `Random::Shared`, `Random::Range`, `rnd::PointInCircle`, `ShuffleBag`, `WeightedTable`, `RollDice`, `utf8::Next`; In flight: `GameLoop::RequestStep`, `SetDebugTimeScale`, `SetCapture`, `FrameRecorder`, `FramePlayer`, `TickPacer`
- **Editor:** none (Play mode in the editor and `player/main.cpp` drive Time and Game themselves; per `docs/loop_and_time.md` neither adopted `GameLoop` yet, and no window shows the frame statistics, step buttons or debug time scale)
- **Docs:** `docs/making_a_game.md`, `docs/core_audit_notes.md`, `docs/loop_and_time.md`
- **Tests:** `timescale`, `time_pause`, `random`, `core_features`, `core_fixes`, `utf8`, `game_loop`, `clock`, `time_extras`, `game_state`, `random_extras`, `application_loop`; In flight: `loop_audit2`, `frame_step`, `capture_mode`, `frame_record`, `frame_stats`, `tick_pacer`, `loop_player_style`
- **Status:** Works - #579 (treated as merged) is covered by six deterministic tests with fake frame times (read `docs/loop_and_time.md` and `GameLoop.hpp` on the branch; the .cpp files were not read); 11 bugs fixed there (NaN time, hit-stop, pause un-pausing, raw terminal after an exception, `SmoothedFPS`, UTF-8 surrogates); the second pass (1.1.11-1.1.13) is In flight
- **Open:** `player/main.cpp` and the editor Play loop still clamp, smooth and step time by hand, so `GameLoop` is not adopted there (docs have the sketch; the second pass adds `loop_player_style` to pin the differences); `Game::Reset()` keeps a script's time scale (use `BeginSession`); `Random::Shared()` is not thread safe (use `ThreadLocal()`); `Application` still sleeps for pacing (`Core/FramePacing`, 1.18.7, is not wired); In flight on `claude/loop-time-wave2`: frame stepping, recording, capture, stats and 13 more bug fixes
- **Related:** 1.2, 1.7, 1.10, 1.18, 6.4, 8.1, 16.5, 36.7
- **Words:** Application, Game, Time, GameLoop, Clock, deltaTime, time scale, slow motion, hit stop, freeze frame, pause, pause reason, quit, quit guard, frame rate, FPS, hitch, fixed timestep, frame step, capture mode, frame recorder, Random, seed, shuffle bag, weighted table, dice, UTF-8, game loop
- **Seen as:** movement judders although the frame rate looks steady; slow motion or pause does not stop something; the game keeps running after Quit; the same "random" result every run; a menu freezes during pause; a pause menu and a cutscene un-pause each other; the game jumps ahead after a window drag
- **Ask me like:** "Pause freezes the timers but my menu animation should keep running, which time do I use?"; "Add a seeded random so the loot rolls are the same on every run"; "Freeze the game for 80 ms when the player is hit"

### 1.2 Timers and tweens
- **What:** Do something after a delay, every N seconds, or animate a value over time with easing, from C++, scripts or the Inspector.
- **Parts:**
  - 1.2.1 Scene scheduler - `Core/Scheduler` - `Invoke`, `InvokeRepeating`, `Tween(duration, onUpdate, onComplete)`; options for unscaled time, tag and owner; Cancel, CancelByTag/Owner, SetPaused, SetInterval, Remaining; safe to call from inside a callback
  - 1.2.2 Global scheduler - `Core/Scheduler` - `Scheduler::Global()` survives scene loads and Play restarts; ticked once per frame by `Scene::Update`
  - 1.2.3 Tween engine - `Core/Tween` - DOTween-style `Tweener`, `ValueTween`, `CustomTween`, `TweenSequence`; chainable SetEase/SetDelay/SetLoops (Restart, Yoyo, Incremental), callbacks, Play/Pause/Kill/Goto
  - 1.2.4 Tween shortcuts - `Core/Tween` - `TweenManager` Move, Scale, Rotate, Punch*, Shake*, Jump, Path, ColorTo, Fade, AnchorPos, SizeDelta, Text, Counter; killed when their target object is destroyed
  - 1.2.5 No-code tween component - `Components/TweenAnimation` - a tween set up in the Inspector that plays at scene start or by id from a script
  - 1.2.6 Easing curves - `Math/Easing` - the `Ease` enum and `Easing::*` functions (see 36.3)
- **Code:** `Core/Scheduler`, `Core/Tween`, `Components/TweenAnimation`, `Math/Easing`
- **API:** `Scene::scheduler()`, `Scene::tweens()`, `Scheduler::Global`, `Tweener::SetEase`, `TweenManager::Move`, `TweenSequence`, script `tween_play_id`
- **Editor:** Tween Animation component in the Inspector (editor/main.cpp, with an ease curve preview)
- **Docs:** `docs/timers_and_tweens.md`
- **Tests:** `scheduler_reentry`, `timers_tweens`, `tween`, `easing`
- **Status:** Works - merged in #560; read `Scheduler.hpp` and `Tween.hpp`; tests cover re-entrancy, loops, big steps, time scale and lifetimes (per test descriptions)
- **Open:** callbacks on the Global scheduler must not capture scene object pointers (they dangle after a load; documented rule, not enforced); scene timers die with the scene by design
- **Related:** 1.1, 1.3, 36.3, 12.4
- **Words:** Invoke, InvokeRepeating, timer, delay, cooldown, Scheduler, Tween, DOTween, ease, yoyo, punch, shake, TweenAnimation, easing, coroutine alternative
- **Seen as:** a delayed action never fires after a scene change; a tween keeps going after its object was deleted; an animation runs during pause when it should not (or the reverse)
- **Ask me like:** "Make the door slide open over half a second with an ease-out"; "Cancel every timer tagged `wave` when the player dies"

### 1.3 Scene graph
- **What:** The object model: a Scene owns GameObjects, each with a Transform and components; layers and tags classify them and a layer matrix decides which colliders interact.
- **Parts:**
  - 1.3.1 Scene container - `Scene/Scene` - create, find, instantiate, destroy objects; sibling order; `FindObjectsOfType`; per-scene `renderSettings`, physics worlds, `fixedTimeStep`, `mainCamera`, scheduler and tween manager
  - 1.3.2 GameObject - `Scene/GameObject` - name, active (own switch; `ActiveInHierarchy()` is the effective one, 1.17), tag, layer, static flag, persistent `Guid()`, `InstanceId()`, Add/Get/RemoveComponent, first-person viewmodel flags, `transient` objects that are never saved
  - 1.3.3 Transform - `Scene/Transform` - local and world position/rotation/scale, cached world matrix, `SetParent` that keeps world pose and refuses cycles, child ordering, TransformPoint/Direction
  - 1.3.4 Component base - `Scene/Component` - Awake, Start, Update, LateUpdate, native `FixedUpdate` (#575), OnEnable/OnDisable, OnDestroy, 2D/3D collision and trigger messages, OnMouse*, `ExecutionOrder`, owned parts
  - 1.3.5 Layers and collision matrix - `Scene/Layers` - 32 named layers, `MaskFromList`; the 32x32 collision matrix lives on the Scene (`SetLayerCollision`, saved as `layermatrix`)
  - 1.3.6 Tags - `Scene/Tags` - `TagString` with a tag index for fast `FindWithTag`, project tag list (`Tags::Add`, `Strict`), `GameObject::SetTag`
  - 1.3.7 Simple behaviours - `Components/Mover`, `Components/Spinner`, `Components/Lifetime` - constant movement, constant spin, self-destroy timer without scripting
- **Code:** `Scene/Scene`, `Scene/GameObject`, `Scene/Transform`, `Scene/Component`, `Scene/Layers`, `Scene/Tags`, `Components/Mover`, `Components/Spinner`, `Components/Lifetime`
- **API:** `Scene::CreateGameObject`, `Scene::Find`, `Scene::FindWithTag`, `GameObject::AddComponent<T>`, `Transform::SetParent`, `Layers::MaskFromList`, `Scene::SetLayerCollision`, `Tags::Add`
- **Editor:** Hierarchy and Inspector (15.1, 15.2); layer picker on the Camera culling mask and object layer fields (editor/main.cpp)
- **Docs:** `docs/making_a_game.md`, `docs/layers_tags_cameras.md`, `docs/core_audit_notes.md`
- **Tests:** `scene`, `structure_version`, `transform_input`, `remove`, `parent_cycles`, `layers`, `layers_tags_cameras`, `subtree`, `scene_pointer`, `scene_defaults`, `gravity`, `gameplay`, `scene_hierarchy_search`
- **Status:** Works - read `Scene.hpp`, `GameObject.hpp`, `Tags.hpp`, `Layers.hpp`; layer matrix and tags merged in #570
- **Open:** the editor keeps its own tag list and does not call `Tags::*` or the layer matrix (grep finds neither in editor or player); component order is not saved (docs/scene_serializer.md); hierarchy-aware `active`, `FindByPath`, GetComponentInChildren/Parent, Transform helpers (LookAt, sibling index) and native `FixedUpdate` merged in #575 (1.17)
- **Related:** 1.4, 1.5, 2.10, 6.1, 6.2, 15.1
- **Words:** Scene, GameObject, Transform, Component, Behaviour, hierarchy, parent, child, layer, tag, FindWithTag, layer matrix, collision matrix, static, Mover, Spinner, Lifetime, layers, layers (physics), tags, scene graph
- **Seen as:** a child jumps when re-parented; two objects pass through each other (or collide) unexpectedly; "find by tag" returns the wrong object; a parent cycle is refused
- **Ask me like:** "Stop layer 3 colliding with layer 4"; "Why does `SetParent` change the child's local position?"

### 1.4 Scene lifecycle
- **What:** What happens when objects are created, switched on and off, destroyed and when scenes are loaded, replaced, added next to each other or streamed in over several frames.
- **Parts:**
  - 1.4.1 Start and enable events - `Scene/Scene`, `Scene/Component` - Start waits until a component is enabled on an active object; OnEnable/OnDisable alternate and are delivered at the start of the next frame
  - 1.4.2 Destroy - `Scene/Scene` - `Destroy` queues the whole subtree, `Destroy(go, seconds)` delays it, `FlushDestroyed` runs at the end of the frame; `RemovalWatch` lets physics skip removed components
  - 1.4.3 Scene manager and build list - `Scene/SceneManager` - `AddScene`, `LoadScene`, `LoadSceneByName`, `LoadNextScene`, `ReloadScene`, `ActiveIndex`; loads are deferred to the end of the frame via `Scene::RequestLoad`
  - 1.4.4 Async and additive loads - `Scene/Scene`, `Scene/SceneSerializer` - `BeginAsyncLoad` builds N objects per frame (`SceneLoadJob`), progress callbacks, additive merge with offset, `UnloadAdditive`
  - 1.4.5 DontDestroyOnLoad - `Scene/Scene` - kept objects and children are taken out before a replacing load and put back with the same ids
  - 1.4.6 Component rules - `Scene/GameObject` - `RequireComponent<T, Req>()` and `DisallowMultipleComponent<T>()` applied inside `AddComponent`, the loader and the editor's Add Component
  - 1.4.7 Scene Manager window - `editor/SceneManagerPanel.*` - project scenes with Create, Open, Duplicate, Rename and Delete, build order and on/off, saved per project and published to the engine SceneManager
- **Code:** `Scene/SceneManager`, `Scene/Scene`, `Scene/Component`, `Scene/GameObject`, `editor/SceneManagerPanel.*`
- **API:** `SceneManager::LoadSceneAsyncByName`, `Scene::AsyncLoadOptions`, `Scene::RequestLoad`, `Scene::DontDestroyOnLoad`, `RequireComponent`, `Component::OnEnable`
- **Editor:** View > Panels > Scene Manager (`editor/SceneManagerPanel.*`); Build Settings edits the same list (15.7)
- **Docs:** `docs/scene_management.md`, `docs/making_a_game.md`
- **Tests:** `scene_lifecycle`, `scenemanager`, `scene_load`, `scene_load_pool`
- **Status:** Works - merged in #558 and #570; read `Scene.cpp` (Update order) and `SceneManager.hpp`; but no loading-screen component, editor or player code calls `LoadSceneAsync`, `ObjectPool` or `RequireComponent` yet (grep finds no caller)
- **Open:** wire async load into the Loading Screen component (12.8); since #575 unload runs OnDisable/OnDestroy for every awakened component, `Scene::LastLoadError()` keeps the reason a load failed and `ActiveIndex` moves only after a successful load (1.17)
- **Related:** 1.5, 1.13, 12.8, 15.7
- **Words:** SceneManager, LoadScene, build list, scenes in build, async load, additive, unload, DontDestroyOnLoad, Awake, Start, OnEnable, OnDisable, RequireComponent, loading screen, scene manager, object pool, object pooling, Scene Manager panel
- **Seen as:** loading a scene crashes or loses things; Start runs before the object is ready; the loading bar jumps from 0 to 100; an object survives (or does not survive) a scene change
- **Ask me like:** "Load Level2 in the background and show progress, then switch"; "Keep the music player alive across scenes"

### 1.5 Scene serializer
- **What:** Reads and writes the human-readable `okayscene` text format (version 3), clones objects, merges scenes and records what a round trip keeps.
- **Parts:**
  - 1.5.1 Save and load - `Scene/SceneSerializer` - `Serialize`, `SaveToFile`, `Deserialize`, `LoadFromFile`; `kSceneFormatVersion = 3`; packed (obfuscated) files are unpacked on read
  - 1.5.2 Safe loading - `Scene/SceneSerializer` - all-or-nothing: a bad or cut-short file leaves the live scene untouched; counts are bounded; records from a newer build are skipped with warnings
  - 1.5.3 Merge and chunks - `Scene/SceneSerializer` - `MergeFromText/File` adds a scene next to the host, roots offset, objects tagged with `sourceScene`
  - 1.5.4 Stable ids and flags - `Scene/SceneSerializer` - `guid`, `sibling` order, `active`, per-component `compoff` records (see docs/scene_serializer.md)
  - 1.5.5 Component serializer registry - `Scene/ComponentSerializer` - register a record name with a writer and reader instead of growing the long if/else chain; `trailrenderer` is the first user
  - 1.5.6 Asset path collection - `Scene/SceneSerializer` - `CollectAssetPaths` lists the textures, sounds and files a scene needs so Build Game can copy them
  - 1.5.7 Canonical text - `Scene/SceneText` - (#578) a tolerant parser of scene and prefab text that never builds a Scene; `SceneText::Canonicalize` gives equal scenes equal text (layout, number spelling, record order, object numbering) and leaves anything it does not understand alone; helper sources `engine/src/Scene/SceneTextDetail.*`
  - 1.5.8 Structural diff - `Scene/SceneDiff` - (#578) `SceneDiff::Compute/Apply`: added, removed, modified objects, renames, reparents, per-record and per-field changes, as text or JSON (see 28.2)
  - 1.5.9 Three-way merge - `Scene/SceneMerge` - (#578) `SceneMerge::Merge(base, ours, theirs)` with policies Mark, PreferOurs, PreferTheirs and a conflict report; the merged text always loads (see 28.4)
  - 1.5.10 Format migration - `Scene/SceneMigration` - (#578) `MigrateToCurrent`, a registry of `N -> N+1` steps, `MigrateThroughEngine` for upgrades that need component classes (see 25.3)
- **Code:** `Scene/SceneSerializer`, `Scene/ComponentSerializer`, `Scene/SceneText`, `engine/src/Scene/SceneTextDetail.*`, `Scene/SceneDiff`, `Scene/SceneMerge`, `Scene/SceneMigration`
- **API:** `SceneSerializer::Serialize`, `SceneSerializer::LoadFromFile`, `SceneSerializer::MergeFromFile`, `ComponentSerializerRegistry::Register`, `SceneLoadJob`, `SceneText::Canonicalize`, `SceneDiff::Compute`, `SceneMerge::Merge`, `SceneMigration::MigrateToCurrent`
- **Editor:** File menu Save/Open (15.9); the same serializer feeds undo snapshots, copy/paste and prefab files; no window uses the text tools (1.5.7-1.5.10) yet
- **Docs:** `docs/scene_serializer.md`, `docs/prefabs.md`, `docs/scene_diff_merge.md`
- **Tests:** `serialize`, `scene_safe_load`, `scene_records`, `scene_ids_prefabs`, `parent_cycles`, `assets`, `scene_canonical`, `scene_diff`, `scene_merge`, `scene_migration` (they share `tests/scene_text_testkit.hpp`, a seeded random scene generator)
- **Status:** Works - merged in #553 and #570; read `SceneSerializer.hpp` and `ComponentSerializer.*`; the 8.6k-line `SceneSerializer.cpp` was only skimmed, most components still go through one if/else chain; covered by the tests below; the text tools of #578 are library code with tests that include random-scene and fuzz cases (per docs and test names)
- **Open:** file splitting (19.4); duplicate components of one type save only the first, except scripts, visual scripts, action lists, tweens, sync vars and state machines; #575 added `ValidateFile`, shortest-round-trip floats and `gravity3` (1.17); no editor or command-line use of the diff, merge, canonical and migration tools (28)
- **Related:** 1.4, 1.6, 1.14, 15.9, 19.4, 28
- **Words:** okayscene, serializer, SceneSerializer, save scene, load scene, merge, text format, guid, ComponentSerializerRegistry, CollectAssetPaths, gravity, scene file, canonical form, Canonicalize, scene diff, three-way merge, merge conflict, scene migration, SceneText
- **Seen as:** a scene file will not open or opens half-empty; a component's settings are lost after save and load; two scenes will not merge cleanly
- **Ask me like:** "Add my new component to the scene file without touching the big chain"; "Why did loading drop my second collider?"

### 1.6 Prefabs and templates
- **What:** Reusable objects saved as `.okayprefab` files and the starter worlds, players and UI screens the editor creates from templates.
- **Parts:**
  - 1.6.1 Prefab files - `Scene/SceneSerializer` - `SaveObjectToFile`, `InstantiateFromFile`, `Instantiate`; an instance remembers `prefabSource`
  - 1.6.2 Prefab overrides - `Scene/SceneSerializer`, `Scene/GameObject` - `CapturePrefabOverrides`, `InstantiatePrefab`, `ReapplyPrefab`; overrides are whole records kept as data
  - 1.6.3 Starter worlds and players - `Scene/SceneTemplates` - `Templates::Platformer`, `ThirdPerson`, `FPS`, `TopDown`, `Vehicle3D`, `CoinCollector`, `Snake`, `BrickBreaker`, `TerrainSandbox`, `VoxelSandbox` and more, with player builders such as `AddThirdPersonPlayer`
  - 1.6.4 UI screens and styles - `Scene/UITemplates`, `Scene/UIStyles`, `Scene/UILayoutTools` - prebuilt menus, HUD, loading and shop screens in the launcher's Slate look; style presets and copy/paste style; wrap, fit, stack tools (12.7)
  - 1.6.5 Camera guarantee - `Scene/EnsureCamera` - every scene gets a main camera, 2D or 3D (see 2.10)
  - 1.6.6 Nested prefabs - `Scene/PrefabTools` - (#578) a prefab file may hold `nestedprefab "<file>"` objects with override, remove and unset lines and added objects; cycle detection, depth limit 16, a damaged nested file skips only that part; `SavePrefab` writes the structure back (21.2)
  - 1.6.7 Prefab variants - `Scene/PrefabTools` - (#578) a file with `variant "<base>"` stores only differences; a variant of a variant works; `SaveVariant(instance, path)` (21.3)
  - 1.6.8 Per-field overrides and unpack - `Scene/PrefabTools` - (#578) `Diff`, `RevertOverride`, `RevertAll`, `ApplyToSource`, `ApplyAllToSource`, `Unpack`, `ValidateAsset`, `ValidateInstance` (21.4)
  - 1.6.9 Prefab text cache - `Scene/SceneSerializer` - (#575) the decoded text of the last 32 prefab files is kept so repeated spawns skip the file read; `SceneSerializer::ClearPrefabCache()`
- **Code:** `Scene/SceneTemplates`, `Scene/UITemplates`, `Scene/UIStyles`, `Scene/UILayoutTools`, `Scene/EnsureCamera`, `Scene/SceneSerializer`, `Scene/PrefabTools`
- **API:** `SceneSerializer::InstantiateFromFile`, `SceneSerializer::ReapplyPrefab`, `GameObject::prefabOverrides`, `Templates::*`, `UITemplates::AddHUD`, `EnsureSceneCamera`, `PrefabTools::Diff`, `PrefabTools::SavePrefab`, `PrefabTools::SaveVariant`, `PrefabTools::Unpack`
- **Editor:** GameObject > Create menu, Hierarchy prefab Apply/Revert/Reload (editor/main.cpp); no override UI yet
- **Docs:** `docs/prefabs.md`, `docs/prefab_system.md`, `docs/making_a_game.md`
- **Tests:** `instantiate`, `templates`, `scene_cameras`, `scene_defaults`, `prefab_nested`, `prefab_variants`, `prefab_apply_revert`
- **Status:** Partial - prefab files, override data, nested prefabs and variants work as library code (tests; `InstantiateFromFile` routes nested files to `PrefabTools`, read `SceneSerializer.cpp`); the editor calls neither the override API nor `PrefabTools` (docs/prefab_system.md: "nothing in the editor calls it yet")
- **Open:** prefab overrides in the editor (apply, revert, per-field, prefab mode, variant creation, nested roots in the Hierarchy); the old override unit is a whole record, `PrefabTools::Diff` goes per field but falls back to whole records for unknown ones; objects added under an instance are not carried over by `ReapplyPrefab`
- **Related:** 1.5, 12.7, 15.2, 21
- **Words:** prefab, okayprefab, instantiate, override, template, starter, platformer template, player template, UI template, Slate, EnsureSceneCamera, nested prefab, prefab variant, PrefabTools, unpack, apply to prefab, revert override
- **Seen as:** prefab changes don't apply to instances; a template scene has no camera or starts dark; an instance lost its colour change after re-apply
- **Ask me like:** "Re-apply the prefab but keep this instance's colour"; "Add a Wave Shooter template next to Platformer"

### 1.7 Saves and prefs
- **What:** Player progress and settings on disk: whole-game save slots, an Easy-Save-style typed key/value store, and a simple preferences bag.
- **Parts:**
  - 1.7.1 Save slots - `Core/SaveGame` - `SaveGame::Save/Load/LoadNow/List/Info/Delete`, versioned and checksummed `<slot>.okaygame`, `.bak` fallback, migration hook, size caps
  - 1.7.2 Typed save files - `Core/SaveData` - `SaveFile` and the `Save` facade: float, int, bool, string, Vec3 keys per file, `BaseDir`, safe relative paths, escaped on disk
  - 1.7.3 Prefs - `Core/Prefs` - global string-backed bag (`SetInt/GetInt`, floats, strings), typed `SetBool`, `SetVec3`, `SetColor` and change listeners (`AddListener`, #571), `Save(path)/Load(path)`; also holds audio, video and accessibility settings
  - 1.7.4 Autosave component - `Components/AutoSave` - saves a slot every N seconds and/or when the scene starts
  - 1.7.5 Save framework - `Core/SaveFramework`, `Core/SaveFrameworkScene`, `Core/SaveFrameworkSync` - (#572) saveable objects and `SaveSystem::CaptureScene/ApplyScene`, `<slot>.okayslot` slots with headers, quick save, autosave ring, checkpoints, migrations, sync hooks and a slot menu model; separate from `SaveGame` (see 35)
- **Code:** `Core/SaveGame`, `Core/SaveData`, `Core/Prefs`, `Components/AutoSave`, `Core/SaveFramework`, `Core/SaveFrameworkScene`, `Core/SaveFrameworkSync`
- **API:** `SaveGame::Save`, `SaveGame::List`, `Save::SetInt`, `Save::GetString`, `Prefs::SetInt`, `AutoSave`, script `save()` and `save_game`
- **Editor:** AutoSave component in the Inspector (editor/main.cpp); no save-slot UI components yet (12.8)
- **Docs:** `docs/save_games.md`, `docs/save_framework.md`, `docs/localization_prefs_time.md`
- **Tests:** `save_game`, `durable_saves`, `savedata`, `prefs`, `prefs_misc`, `save_framework`, `save_scene`, `loc_prefs_time`
- **Status:** Works - atomic writes, escaping and checksums merged in #552 (read `SaveGame.hpp`); `Prefs` is a thin string bag read by the player at start and saved at exit (`player/main.cpp`)
- **Open:** the save framework (#572, area 35) has no scene-file record, component or editor window yet, so no game uses it, and it keeps its own slot format next to `SaveGame` (nothing converts between them); loading a slot replaces the default Save file's keys (documented); no cloud backend behind the sync hooks
- **Related:** 1.14, 12.8, 35, 7.4
- **Words:** SaveGame, save slot, okaygame, SaveData, Save, Easy Save, Prefs, PlayerPrefs, autosave, checkpoint, load menu, backup, migration, save game, save file, SaveManager, checkpoint, quick save, Saveable, slot header, okayslot
- **Seen as:** progress lost; corrupt save refuses to load; a save from a newer version is rejected; settings reset on restart
- **Ask me like:** "Show a load menu listing every slot with its play time"; "Keep unlocks across slots"

### 1.8 Localization
- **What:** Player-facing text in several languages: a string table, language choice, fallbacks, `{0}`/`{name}` and plural formatting, and scene text that follows the language.
- **Parts:**
  - 1.8.1 String table - `Core/Localization` - `StringTable` keys by languages with translator notes, `.okayloc` and CSV formats, completion per language
  - 1.8.2 Language service - `Core/Localization` - `Localization::Get/TryGet/SetLanguage/PickLanguage/LoadSettings`, fallback to default language then the key, each miss reported once, `Version()` bump
  - 1.8.3 Formatting and plurals - `Core/Localization` - `Localize(key, args...)`, `LocArg`, `PluralForm`, `SetPluralRule`, `Pseudo` pseudo-localization
  - 1.8.4 Localized scene text - `Scene/SceneLocalization` - `LocalizeScene`, `LocalizeObject`, `CollectLocKeys`; `LocalizedText` option on text fields; scripts told on language change
  - 1.8.5 Font fallback - `Graphics/TtfFont` - per-language font and installed-font fallback when the pixel font lacks glyphs (see 2.9)
  - 1.8.6 Plural rules and formats - `Core/Localization` - (#571) CLDR plural categories for about 30 languages (`Plural(lang, n)`, `PluralText(key, count)` reading `key.one/few/other`), `FormatNumber/Currency/Percent/Date/Time`, region to base to default language fallback (`pt-BR` to `pt`)
- **Code:** `Core/Localization`, `Scene/SceneLocalization`
- **API:** `Localization::Get`, `Localize`, `Localization::SetLanguage`, `StringTable`, `LocalizedText`, `LocalizeScene`, script `set_language`
- **Editor:** Localization window (1.16, `editor/LocalizationWindow.*`)
- **Docs:** `docs/localization.md`, `docs/localization_prefs_time.md`
- **Tests:** `localization`, `loc_prefs_time`
- **Status:** Works - read `Localization.hpp` and `SceneLocalization.hpp`; the test is the largest in the area (678 lines, per test file)
- **Open:** right-to-left scripts draw left to right unshaped (docs); dialogue box and journal atlas covers ASCII and Latin-1 only (docs)
- **Related:** 1.16, 2.9, 10.2, 31
- **Words:** Localization, string table, okayloc, language, translation, plural, pseudo-localization, CSV, font fallback, LocalizedText, set_language, plural rules, FormatNumber, FormatCurrency, FormatDate, CLDR, region fallback
- **Seen as:** "1 item / 2 items" reads wrong; some letters show as boxes; text stays in the old language after switching; a key name shows instead of text
- **Ask me like:** "Add Polish plural rules"; "Why does the Japanese menu show squares?"

### 1.9 Events and ECS
- **What:** A typed publish/subscribe event bus and a small data-oriented ECS that lives beside the GameObject scene, with a bridge that draws ECS entities as GameObjects and a networked world.
- **Parts:**
  - 1.9.1 Event bus - `Core/EventBus` - `Subscribe<E>`, `Publish`, scoped subscription handle; safe when handlers subscribe, unsubscribe or publish during a publish
  - 1.9.2 ECS world - `ECS/World` - entities as ids, sparse-set component pools, `each<A, B>` queries, add/get/remove
  - 1.9.3 System scheduler - `ECS/Systems` - named systems run in order each tick, can be toggled
  - 1.9.4 Scene bridge - `ECS/SceneBridge` - `EcsTransform` entities mirrored to GameObjects created by your factory, with interpolation
  - 1.9.5 Networked ECS - `ECS/NetWorld`, `ECS/NetWorldSync` - register replicated component types, `snapshot()/apply()`, `WorldReplicator` broadcast/listen over the NetworkManager RPC channel (see 16.3); delta snapshots with acknowledgements and a linear-time apply (#571)
  - 1.9.6 Command buffer and safe iteration - `ECS/World` - (#571) `world.commands()` records create, destroy, add, set and remove during `each` and `flush()` applies them in order; destroying inside `each` is safe
  - 1.9.7 Event bus extras - `Core/EventBus` - (#571) priorities, `SubscribeOnce`, scoped `Listen`, `Queue` and `Flush`
- **Code:** `Core/EventBus`, `ECS/World`, `ECS/Systems`, `ECS/SceneBridge`, `ECS/NetWorld`, `ECS/NetWorldSync`
- **API:** `EventBus::Subscribe`, `EventBus::Publish`, `ecs::World`, `ecs::SystemScheduler`, `ecs::SceneBridge`, `ecs::NetWorld`, `ecs::WorldReplicator`
- **Editor:** none
- **Docs:** -
- **Tests:** `eventbus`, `ecs`, `core_audit`, `ecs_commands`
- **Status:** Works - read the headers; `eventbus` and `ecs` tests are thorough, but it is opt-in library code: nothing in the engine, editor or player uses `EventBus` or the ECS (grep finds no consumer)
- **Open:** possible bug: `SceneBridge` keeps raw `GameObject*` and dereferences them on the next sync if something else destroyed the object (core_audit_notes); #571 added the command buffer, safe `World::each`, delta snapshots with ack and the `EventBus` extras (docs `docs/ecs.md`, `docs/core_tools.md`)
- **Related:** 16.3, 1.3
- **Words:** EventBus, event, publish, subscribe, ECS, entity, component pool, World, system, SceneBridge, NetWorld, snapshot, replication, event bus, command buffer, delta snapshot, ack, SubscribeOnce, event priority
- **Seen as:** a handler still runs after it unsubscribed; an ECS unit does not show on screen; the client's ECS world differs from the server's
- **Ask me like:** "Use the ECS for 5000 bullets and draw them through the bridge"; "Send only changed components to clients"

### 1.10 Logging and profiling
- **What:** Messages for developers (levels, categories, file output) and a frame profiler with a scrubbable history and trace export.
- **Parts:**
  - 1.10.1 Log - `Core/Log` - Trace/Info/Warning/Error, per-category levels, `Log::Configure("info,net=trace")` and `$OKAY_LOG`, timestamps, thread-safe lines, `Log::sink` hook
  - 1.10.2 Log file sink - `Core/Log` - `Log::OpenFile(path, maxBytes, keep)` with rotation, flushed on warnings and errors
  - 1.10.3 Frame profiler - `Core/Profiler` - `OKAY_PROFILE("name")` scopes, 240-frame ring, per-section min/avg/max, draw/triangle/particle counters, `Gpu()` time proxy
  - 1.10.4 Profiler export - `Core/Profiler` - Chrome trace JSON and CSV export of a frame range
  - 1.10.5 Editor Console - `editor/ConsolePanel.*` - `ConsoleLog` with info/warning/error levels, repeat counts, filters with counts, search, collapse, details
  - 1.10.6 Debug data layers - `Debug/FrameHistory`, `Debug/GpuTimer`, `Debug/RenderStats`, `Debug/MemoryTracker`, `Debug/DrawCallRecorder`, `Debug/ConsoleModel`, `Debug/DebugStatsOverlay` - (#574) long frame history with percentiles and spikes, GPU scopes, per-frame render counters, named memory categories, a one-frame draw list, a console view model and an overlay text builder; shared helpers in `Debug/DebugUtil`, umbrella `Debug/Debug`, opt-in heap counters in `Debug/MemoryHooks` (see 23)
- **Code:** `Core/Log`, `Core/Profiler`, `editor/ConsolePanel.*`, `Debug/FrameHistory`, `Debug/GpuTimer`, `Debug/RenderStats`, `Debug/MemoryTracker`, `Debug/MemoryHooks`, `Debug/DrawCallRecorder`, `Debug/ConsoleModel`, `Debug/DebugStatsOverlay`, `Debug/DebugUtil`, `Debug/Debug`
- **API:** `Log::Info`, `Log::Category`, `Log::Configure`, `Log::OpenFile`, `OKAY_PROFILE`, `Prof()`, `Profiler::ExportChromeTraceToFile`, `ConsoleLog`
- **Editor:** Console panel (`editor/ConsolePanel.*`); Profiler and Stats windows (editor/main.cpp, 23)
- **Docs:** `docs/core_logging_profiling.md`, `docs/debug_tools.md`
- **Tests:** `log`, `profiler`, `profiler_export`, `debug_frame_history`, `debug_gpu_stats`, `debug_memory`, `debug_memory_hooks`, `debug_draw_calls`, `debug_console`, `debug_overlay`
- **Status:** Works - merged in #559; read `Log.hpp` and `Profiler.hpp`; the profiler is off until the editor window turns it on
- **Open:** the "GPU" time is the synchronous render plus read-back time, not a GPU timer; #574 added frame history, GPU timer, render stats, memory tracking, a frame debugger, console model and a stats overlay as library code under `Debug/*` (docs `debug_tools.md`; nothing feeds them from the renderers yet, 23) and #571 added Cvar, Console and DebugOverlay (1.18)
- **Related:** 23, 38.11, 15.9
- **Words:** Log, logging, Console, Profiler, OKAY_PROFILE, trace, Chrome trace, CSV, frame time, category, log file, sink, frame history, GpuTimer, MemoryTracker, DrawCallRecorder, ConsoleModel, stats overlay
- **Seen as:** log spam from one system; no log file after a crash; the Console shows the same line many times; a hitch with no clue which system
- **Ask me like:** "Turn on trace logging for net only"; "Export the last 240 frames to chrome://tracing"

### 1.11 Asset manager
- **What:** A central registry for files loaded from disk: typed handles with reference counts, one cache key per file, background loading and hot reload.
- **Parts:**
  - 1.11.1 Handles and caching - `Core/AssetManager` - `AssetHandle<T>`, `Get<T>(path)`, weak cache freed with the last handle, failed loads never cached
  - 1.11.2 Path normalization - `Core/AssetManager` - `AssetManager::Normalize` makes one key per file (slashes, `.` and `..`, case folded on Windows only)
  - 1.11.3 Loaders - `Core/AssetManager` - `RegisterLoader<T>`; defaults for Image, AudioClip, Mesh, DataAsset and Material on `Instance()`
  - 1.11.4 Async loading - `Core/AssetManager` - `LoadAsync` on one worker thread, callbacks from `Pump()` on the main thread, `Progress()`, duplicate requests share one decode
  - 1.11.5 Hot reload - `Core/AssetManager` - `SetHotReload` compares mtime and size in `Pump()` and re-decodes in place so handles stay valid
  - 1.11.6 Report - `Core/AssetManager` - `Report()` lists every cached asset with type, bytes, refcount and reload count
- **Code:** `Core/AssetManager`
- **API:** `AssetManager::Instance`, `AssetHandle<T>`, `AssetManager::LoadAsync`, `AssetManager::Pump`, `SetHotReload`, `Report`
- **Editor:** none yet (the editor's "Asset Manager" panel is the marketplace download tool, 17.6)
- **Docs:** `docs/asset_manager.md`
- **Tests:** `assetmanager`
- **Status:** Partial - merged in #568 and tested, but no engine loader, editor or player path calls it (docs and grep agree); GPU textures are not tracked
- **Open:** move existing loaders (`DataAsset::Cached`, `AudioClip::LoadCached`, the renderers' own loads) onto it; track GPU resources; an asset browser window on top (area 20)
- **Related:** 1.12, 20, 2.9, 7.2
- **Words:** AssetManager, asset handle, AssetHandle, hot reload, async load, resource cache, reference count, Pump
- **Seen as:** the same texture loaded twice; an edited file does not show until restart; a hitch when a big file loads
- **Ask me like:** "Load the boss music in the background and tell me when it is ready"; "Reload textures when I save them"

### 1.12 Asset database
- **What:** The data layer an asset browser sits on: stable GUIDs in `.okaymeta` files, move tracking, a dependency graph, typed import settings, thumbnails and a search query. No UI yet.
- **Parts:**
  - 1.12.1 GUIDs and meta files - `Core/AssetDatabase` - `Scan`, `Guid`, `.okaymeta` sidecars, `MoveAsset`, rename and duplicate handling, orphan reports, labels
  - 1.12.2 Dependency graph - `Core/AssetDatabaseDeps` - extractors for scenes, prefabs, materials and text assets; `DependenciesOf`, `UsedBy`, `BuildClosure`, `FindUnused`, `FindMissing`, `FixStaleReferences`
  - 1.12.3 Import settings - `Core/ImportSettings` - typed texture, model and audio settings with versions and migration; reimport signal
  - 1.12.4 Thumbnails - see 1.20 (`Core/ThumbnailCache` is its own sub-area)
  - 1.12.5 Search and paging - `Core/AssetDatabaseSearch` - filter, sort and page query with per-type counts
  - 1.12.6 Shared helpers - `Core/AssetDatabaseInternal` - private utilities used by the three database sources
- **Code:** `Core/AssetDatabase`, `Core/AssetDatabaseDeps`, `Core/AssetDatabaseSearch`, `Core/AssetDatabaseInternal`, `Core/ImportSettings`
- **API:** `AssetDatabase::Scan`, `AssetDatabase::BuildClosurePaths`, `AssetDatabase::Search`, `Guid`, `ImportSettings`
- **Editor:** none yet (the Project panel does not call it)
- **Docs:** `docs/asset_database.md`
- **Tests:** `asset_database`
- **Status:** Partial - merged in #568 with a 1467-line test, but nothing consumes it: no importer reads the import settings, scenes still store paths, the editor does not use it (docs "Not covered")
- **Open:** asset browser window and thumbnail grid (an old branch claude/asset-browser exists but shares no history with main); importers reading the settings; GUID references inside scene files; build/export closure wiring; binary formats such as `.fbx` have no dependency edges
- **Related:** 1.11, 1.20, 20, 15.4, 18.1
- **Words:** AssetDatabase, GUID, meta file, okaymeta, dependency, import settings, unused assets, missing assets, asset browser, reference, closure
- **Seen as:** moving a file in the project loses its texture in scenes; the build ships unused files or misses one; no thumbnails in the Project panel
- **Ask me like:** "List every asset nothing references"; "Which scenes use `hero.png`?"

### 1.13 Object pooling
- **What:** Reuse spawned objects (bullets, sparks, pickups) instead of creating and destroying them each time.
- **Parts:**
  - 1.13.1 Pool source - `Scene/ObjectPool` - `SetPrefabFile` or `SetTemplate` captured once; the template can be destroyed afterwards
  - 1.13.2 Get and Release - `Scene/ObjectPool` - `Get`, `Get(position)`, `Release`, `Prewarm`, `maxSize`; an object released this frame is reused from the next frame because OnEnable/OnDisable are the reset points
  - 1.13.3 Pool statistics - `Scene/ObjectPool` - `IdleCount`, `ActiveCount`, `CreatedCount`, `ReuseCount` for tuning the pre-warm size
- **Code:** `Scene/ObjectPool`
- **API:** `ObjectPool`, `ObjectPool::Get`, `ObjectPool::Release`, `ObjectPool::Prewarm`
- **Editor:** none
- **Docs:** `docs/scene_management.md`
- **Tests:** `scene_load_pool`
- **Status:** Partial - merged in #570 and tested; no component, script builtin or editor option uses it yet (grep)
- **Open:** a script builtin and a no-code Spawner option; resetting Rigidbody velocities is each component's job
- **Related:** 1.4, 11.6
- **Words:** ObjectPool, pool, pooling, Get, Release, Prewarm, bullet pool, reuse, object pool
- **Seen as:** a frame hitch when many bullets spawn; a reused bullet keeps its old speed or state
- **Ask me like:** "Pool 64 bullets and reset their velocity on reuse"

### 1.14 Data assets and data packs
- **What:** Small data files for game content (`.okaydata` fields) and light obfuscation of shipped data so players cannot casually read or edit it.
- **Parts:**
  - 1.14.1 Data asset - `Core/DataAsset` - ordered key/value fields saved as `.okaydata`, cached load with `Reload/Invalidate`, read from OkayScript (`data_num`, `data_str`)
  - 1.14.2 Data pack - `Core/DataPack` - `OKP1` header, keystream XOR and a checksum; scenes and builds are unpacked transparently on load
- **Code:** `Core/DataAsset`, `Core/DataPack`
- **API:** `DataAsset::Cached`, `DataAsset::Load`, `DataPack::Pack`, `DataPack::Unpack`, `DataPack::IsPacked`
- **Editor:** Data asset editing in editor/main.cpp; Build Game packs files
- **Docs:** -
- **Tests:** `dataasset`, `datapack`
- **Status:** Works - read `DataPack.hpp`; the header itself says this is obfuscation, not DRM (the key ships in the game)
- **Open:** `DataAsset::Cached` keeps a failed load (a file created later is not picked up) and `Save` writes multi-line values that `Load` reads back as extra fields (core_audit_notes, not re-checked)
- **Related:** 1.5, 1.7, 1.11, 18.1
- **Words:** DataAsset, okaydata, ScriptableObject, data pack, DataPack, obfuscation, tamper, checksum
- **Seen as:** "tampered or unreadable" on load; a data file edit not showing in game
- **Ask me like:** "Store enemy stats in a data asset"; "Pack the scene so players can't edit it"

### 1.15 Display settings
- **What:** Fullscreen, vsync and the Low/Medium/High quality preset the Settings screen drives.
- **Parts:**
  - 1.15.1 Settings values - `Core/DisplaySettings` - Fullscreen, VSync, Quality (0-2), `Generation()` bumps on every Set so hosts know to apply; stored in Prefs as `video.*`
  - 1.15.2 Quality preset - `Core/DisplaySettings` - `ApplyQuality` flips the shared shadow, per-pixel lighting, bloom, SSAO and FXAA switches (declared in `SoftwareRenderer.hpp`, some read by the GPU backends too)
- **Code:** `Core/DisplaySettings`
- **API:** `DisplaySettings::SetQuality`, `DisplaySettings::SetFullscreen`, `DisplaySettings::Generation`, `DisplaySettings::ApplyQuality`
- **Editor:** none (the player reads it; Settings screen component, 12.8)
- **Docs:** -
- **Tests:** -
- **Status:** Basic - read `DisplaySettings.cpp` (70 lines); no test of its own; the OpenGL and Direct3D 11 renderers also read `ShadowsEnabled()` and `BloomEnabled()` (OpenGL also `SSAOEnabled()`), so the presets reach them; `FXAAEnabled()` and `PerPixelLighting()` are read only by the software path (grep)
- **Open:** a test; the preset does not change shadow resolution or distance; resolution and window mode options; the editor calls `ApplyQuality()` at start (editor/main.cpp), the player reads `DisplaySettings` too
- **Related:** 12.8, 38.1, 2.3, 2.5
- **Words:** DisplaySettings, fullscreen, vsync, quality, graphics settings, Low Medium High, video settings
- **Seen as:** the quality setting changes nothing on a GPU machine; fullscreen is not remembered
- **Ask me like:** "Add a Ultra quality level with soft shadows"

### 1.16 Localization tools
- **What:** The editor side of localization: the string-table grid, CSV import and export, language preview, pseudo-localization and finding where a key is used.
- **Parts:**
  - 1.16.1 Localization window - `editor/LocalizationWindow.*` - grid of keys by languages, inline edit with undo, completion bars, search and missing/unused filters, Find in Scene
  - 1.16.2 CSV and build copy - `editor/LocalizationWindow.*` - Import/Export CSV for translators; Build Game copies the table and the fonts the languages name
  - 1.16.3 Preview language - `editor/LocalizationWindow.*` - toolbar language menu and pseudo-localize switch; the scene follows in Scene and Game views
  - 1.16.4 Inspector helpers - `editor/LocalizationWindow.*` - `LocalizedTextField`, `LocKeyInput`, `LocalizedSwitch` for Pause Menu, Hold Prompt and Loading Screen
  - 1.16.5 Table model, checker, scanner and previews - `Core/LocTable`, `Core/LocCheck`, `Core/LocScan`, `Core/LocPreview` - (#574) cell states, CSV and JSON, placeholder and plural checks, a scan for used, unused and undefined keys, pseudo-localization and mirrored-text previews; not wired to the window (31.1, 31.3)
  - 1.16.6 Captions and accessibility settings - `Core/Subtitles`, `Core/AccessibilitySettings` - (#574) caption queue and settings; see 32.2 and 32.3
- **Code:** `editor/LocalizationWindow.*`, `Scene/SceneLocalization`, `Core/LocTable`, `Core/LocCheck`, `Core/LocScan`, `Core/LocPreview`, `Core/Subtitles`, `Core/AccessibilitySettings`
- **API:** `DrawLocalizationWindow`, `LocalizedTextField`, `LocKeyInput`, `CollectLocKeys`
- **Editor:** View > Editors & Tools > Localization; toolbar language menu next to Build
- **Docs:** `docs/localization.md`, `docs/localization_tools.md`
- **Tests:** `localization`, `loc_tools`
- **Status:** Partial - the window, CSV and pseudo-localization exist (per docs/localization.md; read the header); the grid model, checker, scanner and previews are on main (#574) as engine code but `docs/localization_tools.md` says the window is not wired to them yet
- **Open:** wire the window to `LocTable`, the checker and the scanner (31.1), a language preview that mirrors right-to-left text (31.3), accessibility text scaling (area 32); tests for the window itself
- **Related:** 1.8, 31, 32, 15.6
- **Words:** Localization window, translation table, CSV import, pseudo-localize, language preview, Find in Scene, missing key, translators, LocTable, LocCheck, LocScan, LocPreview, placeholder check, caption manager
- **Seen as:** a translated text is cut off; unused keys pile up; a language shows empty cells
- **Ask me like:** "Export the table to CSV for the translator"; "Show me every key missing in French"

### 1.17 Scene hierarchy, search and validation
- **What:** Hierarchy-aware active state, finding objects by path or by component in children, a native fixed-step hook, load errors, and a validator that reports missing assets, unknown components and broken links. Merged in #575 (2.850.0). The scene diff, merge and migration libraries behind it are in 1.5.
- **Parts:**
  - 1.17.1 Hierarchy-aware active - `Scene/GameObject`, `Scene/Scene` - `GameObject::ActiveInHierarchy()` is true only when the object and every parent are active; Update, LateUpdate, Render, collider auto-fit, 2D picking, all four renderers and both physics worlds skip a child of an inactive parent, and OnDisable/OnEnable follow
  - 1.17.2 Search API and Transform helpers - `Scene/Scene`, `Scene/GameObject`, `Scene/Transform` - `FindByPath("A/B/C")`, `Roots()`, `FindGameObjectsWithTag`, `GetComponentInChildren/Parent`; Transform `LookAt`, `SetLossyScale`, Euler angle getters and setters, `GetSiblingIndex/SetSiblingIndex`
  - 1.17.3 Scene validation - `Scene/SceneValidation` - `scene.Validate("Assets")` and `SceneSerializer::ValidateFile` report MissingComponentType, UnknownField, MissingAsset, BrokenParentLink, DuplicateGuid and LoadError as a `SceneValidationReport`
  - 1.17.4 Scene diff, merge, migration - see 1.5.7 to 1.5.10 (libraries on the scene text) and 28.2, 28.4, 25.3 (what is still missing around them)
  - 1.17.5 Semantic scene validator - `Testing/SceneValidator` (#572, area 33) - missing assets and prefabs, no camera, duplicate names, bad scripts, non-finite transforms, Rigidbody without Collider
  - 1.17.6 Native FixedUpdate, load errors and speed - `Scene/Component`, `Scene/Scene`, `Scene/SceneManager`, `Scene/SceneSerializer` - `Component::FixedUpdate(dt)` once per physics step; `Clear()` runs OnDisable/OnDestroy for awakened components; `Scene::LastLoadError()`; `ActiveIndex` moves only after a successful load; `gravity3` record; floats written in the shortest exact form; a cache of 32 decoded prefab files (`ClearPrefabCache`); linear-time mass `Destroy`
- **Code:** `Scene/SceneValidation`, `Scene/GameObject`, `Scene/Scene`, `Scene/Transform`, `Scene/Component`, `Scene/SceneManager`, `Scene/SceneSerializer`, `Testing/SceneValidator`
- **API:** `scene.Validate("Assets")`, `SceneValidationReport`, `SceneSerializer::ValidateFile`, `Scene::FindByPath`, `Scene::Roots`, `GameObject::ActiveInHierarchy`, `GetComponentInChildren<T>`, `Component::FixedUpdate`, `Scene::LastLoadError`, `Transform::LookAt`, `SceneSerializer::ClearPrefabCache`, `SceneValidator::Validate`
- **Editor:** none yet (the editor's only scene check at Play is the visual-script validator, 33.2; no Validate button or window)
- **Docs:** `docs/scene_hierarchy_search.md`, `docs/automated_testing.md`
- **Tests:** `scene_hierarchy_search`, `scene_validator`
- **Status:** Works - merged in #575 with tests; read `scene_hierarchy_search.md`, `Scene.hpp` and where `ActiveInHierarchy` is called (Scene, both physics worlds, GL, D3D11, D3D12 and the software renderer); `SceneValidation.cpp` itself was not read in depth
- **Open:** possible duplicate: `Testing/SceneValidator` (semantic checks, area 33) and `Scene/SceneValidation` (format checks) both report missing assets, and `AssetDatabase::FindMissing` is a third (33.2); unknown keys inside a known key=value record are skipped silently by the loader and not reported (docs); no editor use of `Validate` or `FindByPath`
- **Related:** 1.3, 1.4, 1.5, 33, 28
- **Words:** scene validation, validator, missing asset, broken link, FindByPath, hierarchy active, ActiveInHierarchy, FixedUpdate, GetComponentInChildren, LastLoadError, Roots, LookAt
- **Seen as:** a disabled parent does not disable its children's scripts; a scene opens with missing textures and no warning; a child still collides under a switched-off parent; a failed load leaves no reason
- **Ask me like:** "Validate this scene before the build"; "Find the object at Level/Arena/Enemy"

### 1.18 Core runtime utilities
- **What:** Small building blocks the rest of the engine and a game's own code lean on, with no scene or renderer needed: command-line parsing, console variables and commands, paths, build info, stopwatch, state machine, a job system, memory arenas and pools, generational handles, hashed string ids and frame pacing helpers. Merged in #571 (2.849.0) and #578 (2.850.0).
- **Parts:**
  - 1.18.1 CLI, console and cvars - `Core/CommandLine`, `Core/Console`, `Core/Cvar`, `Core/DebugOverlay` - (#571) `CommandLine` flag and value parser (the player uses it, `player/main.cpp`); typed console variables (`Cvars::Int/Bool`, range, persistence, `OnChange`) and a command registry (`Console::Register/Execute`, history, completion); `DebugOverlay` builds the FPS and object-count lines, switched by the `debug.overlay` cvar; no in-game console window calls them yet
  - 1.18.2 Paths, build info, stopwatch, state machine - `Core/Paths`, `Core/BuildInfo`, `Core/Stopwatch`, `Core/StateMachine` - (#571) per-platform data, config, cache and log folders and an asset base folder with listeners; version, compiler and platform strings; a real-clock or game-clock stopwatch; a small enter/update/exit state machine
  - 1.18.3 Job system - `Core/Jobs` - (#578) `JobSystem::Global()`, `Submit` with dependencies and priorities, `ParallelFor`/`ParallelReduce`, main-thread queue (`RunOnMainThread`, `PumpMainThread`), cancel and shutdown, counters; 0 workers runs everything inline (WebGL builds without threads); threads and locks sit behind `engine/src/Core/CoreThreads.hpp`
  - 1.18.4 Memory arenas and pools - `Core/Memory` - (#578) `FrameArena` (per-frame bump allocator with a limit, `Mark/Rewind`), `PoolAllocator<T>` with stable addresses, `AllocStats`; header only
  - 1.18.5 Handles and slot maps - `Core/SlotMap` - (#578) `Handle<T>` (index plus generation) and `SlotMap<T>` so a stale handle gives nullptr instead of a dangling pointer; header only
  - 1.18.6 String ids - `Core/StringId` - (#578) `StringId` 64-bit FNV-1a hashes, the `_sid` literal, `Intern` with collision reporting
  - 1.18.7 Frame pacing - `Core/FramePacing` - (#578) `ClampFrameDelta`, `FrameTimer`, `FrameLimiter` (sleeps out the rest of a frame, a steady grid); nothing calls it yet, and `GameLoop::maxDelta` (1.1) applies the same clamp rule
- **Code:** `Core/CommandLine`, `Core/Console`, `Core/Cvar`, `Core/DebugOverlay`, `Core/Paths`, `Core/BuildInfo`, `Core/Stopwatch`, `Core/StateMachine`, `Core/Jobs`, `Core/Memory`, `Core/SlotMap`, `Core/StringId`, `Core/FramePacing`, `engine/src/Core/CoreThreads.hpp`
- **API:** `CommandLine`, `Cvars::Int`, `Console::Register`, `Console::Execute`, `DebugOverlay::BuildLines`, `Paths::UserDataDir`, `BuildInfo::Version`, `Stopwatch`, `StateMachine<S>`, `JobSystem::Global`, `JobSystem::Submit`, `ParallelFor`, `FrameArena`, `PoolAllocator<T>`, `SlotMap<T>`, `Handle<T>`, `StringId`, `_sid`, `FrameLimiter`
- **Editor:** none (the player parses its command line with `CommandLine`; `editor/main.cpp` has no console window on these)
- **Docs:** `docs/core_tools.md`, `docs/core_runtime.md`
- **Tests:** `core_cli`, `core_console`, `jobs`, `arena_handles`, `frame_pacing`, `core_audit`
- **Status:** Works - merged and tested (`jobs` repeats its threaded suites; read `docs/core_runtime.md` and `docs/core_tools.md`, the .cpp files were not read in depth); but only `CommandLine` has a caller outside tests (`player/main.cpp`); `Console`, `Cvars`, the job system, arenas, handles and `StringId` are opt-in library code
- **Open:** the runtime branch's own second console-variable registry was deliberately dropped: `Core/Cvar` is the only one (read-only, cheat and restart-required flags are a follow-up for `Cvar`); no hook of `JobSystem` stats into the Profiler window; no work stealing (one shared queue, per docs); `SlotMap` and `FrameArena` are not thread safe by design; `Application` does not use `FrameLimiter` yet
- **Related:** 1.10, 1.1, 8, 18, 23
- **Words:** CommandLine, console command, cvar, console variable, Paths, BuildInfo, Stopwatch, StateMachine, job system, JobSystem, ParallelFor, thread pool, arena, FrameArena, PoolAllocator, SlotMap, Handle, generational handle, StringId, string hash, FrameLimiter, frame limiter, FPS cap
- **Seen as:** no way to pass options to the player; no in-game console; a stale handle crashes; a loop over thousands of items is slow on one thread; save folder differs per platform
- **Ask me like:** "Add a `--windowed` flag to the player"; "Run the particle update on all cores with ParallelFor"; "Cap the frame rate with the FrameLimiter"

### 1.19 Property reflection
- **What:** A property registry so tools, undo and live editing can read, write and watch a component's fields by name (`Light.intensity`) without knowing its C++ type. Merged in #578 (2.850.0).
- **Parts:**
  - 1.19.1 Values and descriptors - `Scene/Properties` - `PropertyValue` (Bool, Int, Float, String, Vec2, Vec3, Vec4, Quat, Color, Enum, AssetPath, ObjectRef) with an exact text round trip; `PropertyDesc` with range, enum options, tooltip, group and flags (ReadOnly, RuntimeOnly, Derived, Structural); a refused value leaves the component untouched
  - 1.19.2 Registry, paths and change hook - `Scene/PropertyRegistry` - `GetProperty/SetProperty` by path (`Camera#1.depth`, `Transform.position.x`), `SetPropertyFromText`, a hook that fires once per real change with old and new value, `ScopedHookSuppression` for undo
  - 1.19.3 Snapshot, diff, apply, copy - `Scene/PropertyRegistry` - `SnapshotProperties`, `DiffProperties`, `ApplyPropertyDiff` (revert or redo), `CopyProperties`
  - 1.19.4 Builder for your own components - `Scene/PropertyBuilder` - `PropertyBuilder<Torch>("Torch", "torch", "Gameplay").AddFloat(...).Commit()`
  - 1.19.5 Built-in registrations - `Scene/BuiltinProperties` - 334 properties on 26 types (GameObject, Transform, Camera, Light, SpriteRenderer, MeshRenderer, 2D and 3D rigidbodies and colliders, AudioSource, ParticleSystem, Mover, Spinner, Lifetime, CameraFollow, Canvas, UIPanel); `RegisterBuiltinProperties()`
  - 1.19.6 Consumers - planned - Inspector fields, per-property undo (22), keep-changes-from-Play (34.1), per-field prefab overrides (21.4) and visual-script nodes reading the registry instead of hand-written code per component
- **Code:** `Scene/Properties`, `Scene/PropertyRegistry`, `Scene/PropertyBuilder`, `Scene/BuiltinProperties`
- **API:** `PropertyRegistry::GetProperty`, `SetProperty`, `SetPropertyFromText`, `AddChangeHook`, `ScopedHookSuppression`, `SnapshotProperties`, `DiffProperties`, `PropertyBuilder<T>`, `PropertyValue`, `PropertyDesc`, `RegisterBuiltinProperties`
- **Editor:** none (nothing in the editor, the serializer, undo or prefab code calls it yet)
- **Docs:** `docs/properties.md`
- **Tests:** `property_registry`
- **Status:** Partial - the registry, builder and built-in registrations are on main (#578) with a cross-check against the scene serializer (per docs: all 325 stored properties pass); no consumer uses it yet
- **Open:** not registered yet: controllers, scripts, the other UI widgets, navigation, networking; animation and IK components are left to Grok's work; mesh geometry, particle curves and collider point lists are deliberately not properties; possible doc inconsistency: `docs/properties.md` says the scene writer prints six significant digits but #575 writes the shortest exact float (`SceneSerializer.cpp` uses `std::to_chars`); subclass components need their own registration (matching is by exact type)
- **Related:** 15.2, 22, 34, 21.4, 1.5, 14
- **Words:** reflection, property registry, PropertyRegistry, PropertyBuilder, PropertyValue, property path, change hook, serialize field, inspector fields, snapshot, diff
- **Seen as:** each component needs hand-written Inspector and serializer code; a value edited in Play cannot be kept; undo cannot say what changed
- **Ask me like:** "Register my component's fields once and get an Inspector for free"; "Set Light.intensity by name from a script"

### 1.20 Asset thumbnails
- **What:** A cache that returns a small picture for any asset, so an asset browser can show a grid; edited files never show a stale picture.
- **Parts:**
  - 1.20.1 Cache layers - `Core/ThumbnailCache` - least-recently-used memory layer (`maxEntries`, `maxBytes`) and an optional folder of PNGs keyed by GUID, content hash, size and a tag; `Invalidate`, `Clear`, `ClearDisk`, `GetStats`
  - 1.20.2 Generators - `Core/ThumbnailCache` - `ThumbnailGenerator` plug-ins (asked newest first); built-in `ImageThumbnailGenerator` for png, jpg, bmp, tga, gif, psd, hdr through `Image::Load`
  - 1.20.3 Scaling and placeholders - `Core/ThumbnailCache` - `BoxDownscale` (area-weighted, alpha-aware, never enlarges) and `MakePlaceholderThumbnail` tiles with the extension; a failing generator gets a red tile and is not retried every frame
  - 1.20.4 Editor thumbnails today - `editor/SceneManagerPanel.*`, `editor/main.cpp` - the Project panel's `GetThumb` (images and `proc:` textures) and the Scene Manager's `GetSceneThumb` are separate caches that do not use this one
- **Code:** `Core/ThumbnailCache`
- **API:** `ThumbnailCache::Get`, `ThumbnailCache::AddGenerator`, `ThumbnailGenerator`, `BoxDownscale`, `MakePlaceholderThumbnail`
- **Editor:** none yet (the Project panel and Scene Manager draw their own thumbnails)
- **Docs:** `docs/asset_database.md`
- **Tests:** `asset_database`
- **Status:** Partial - merged in #568 and covered by the `asset_database` test, but no editor window uses it; only image files get real pictures
- **Open:** generators for meshes, materials, prefabs, models and scenes (the docs show a mesh example); move the Project panel's `GetThumb` onto this cache; an asset browser grid (area 20)
- **Related:** 1.12, 20, 15.4, 2.9
- **Words:** thumbnail, ThumbnailCache, preview, icon, asset preview, placeholder, BoxDownscale, asset browser
- **Seen as:** no preview pictures for models or scenes in the Project panel; a thumbnail shows an old version of a texture; a red tile for a broken image
- **Ask me like:** "Render a thumbnail for each `.okaymesh` in the asset browser"
