# 1. Core & scene

The engine's spine: the main loop and clock, timers and tweens, the scene graph (Scene, GameObject, Transform, Component), scene loading and saving, prefabs, save games and prefs, localization, events and ECS, logging and profiling, the asset manager and asset database, and the runtime utilities, property registry and scene text tools that merged in waves 4 and 5.
Covers `engine/include/okay/Core` (56 headers, 45 sources in `engine/src/Core`), `Scene` (26 headers, 20 sources; `SceneSerializer.cpp` alone is about 9.0k lines), `ECS` (5 headers, header-only) and three editor files. The data layers for undo, debugging, optimization and testing live in `Debug`, `Optimize` and `Testing` (areas 22, 23, 27, 33).
Everything is on main. 2.851.0 is live: it holds 2.850.0 (#574 tools wave 2, #575 scene hierarchy, #578 core wave 5, #579 loop and time pass 1) plus #581 (loop and time pass 2), #582 (scene text tools know `gravity3`), #577 (editor layout redesign) and the feature map (#576). Merged after it: the scene graph wave (#586, 1.3), timers and tweens (#587, 1.2) and the third loop and time pass (#585, 1.1). Release 2.852.0 (draft PR #588) is being cut and will contain #585, #586 and #587. Nothing else is open in this area.

## 1. Core & scene

### 1.1 Loop and time
- **What:** The game loop and the global clock: how a frame starts, how long it lasted, how fast time runs, whether the game is paused or asked to quit, plus the seedable random generator, UTF-8 helpers, timers you poll and the save snapshot of the clocks. Three passes are on main: #579 (one frame driver, `GameLoop`, named clocks, hit stop, pause and quit reasons, random extras), #581 (frame stepping, capture, recording, statistics) and #585 (the player on `GameLoop`, cooldowns, clock ramps, time snapshot, loop console).
- **Parts:**
  - 1.1.1 Main loop - `Core/Application` - `Application::Run` is a thin loop on `GameLoop`; the terminal is closed by a destructor so a throwing component no longer leaves it raw; `Config::maxDelta`, `fixedStep`, `maxFixedSteps`, `catchExceptions`, `app.loop()`, `ExitCode()`; `Config::pacing` is `Pacing::Sleep` (default) or `Pacing::Limiter` (uses `FrameLimiter`, 1.18.7); options set on `loop()` before `Run` (capture, frame budget, statistics window) now stand; defaults to the console renderer (`sandbox/main.cpp`); the player runs its own `GameLoop` (1.1.6), the editor's Play loop does not
  - 1.1.2 Global clock - `Core/Time` - DeltaTime, UnscaledDeltaTime, ElapsedTime (also as double), FrameCount, TimeScale (capped at 100), Frozen, SmoothDelta, SmoothedFPS, `FixedTime`/`InFixedStep`; a NaN, infinite or negative frame time counts as a 0 s frame
  - 1.1.3 Pause and quit flags - `Core/Game` - `Game::Paused/SetPaused/TogglePause` freeze time without touching the scale; `RequestQuit/QuitRequested/Reset` are read by the player and editor Play each frame
  - 1.1.4 Random generator - `Core/Random` - seedable xorshift stream with a shared instance, Range, Value, Gaussian, Shuffle, weighted pick, save/restore of the whole state; `Chance`, `Fork`, `SeedFromString`, `ThreadLocal` (#579) and `Random::LockShared()` to draw from the shared stream on another thread (#585)
  - 1.1.5 UTF-8 helpers - `Core/Utf8` - decode with replacement characters for bad bytes, length, offset, append; `IsValid`, `Sanitize`, `Prev` (backwards walk for a text cursor), `Substr`, `Truncate`, `TruncateBytes` (never cuts a character), `ToUtf16/FromUtf16`, `ToUtf32/FromUtf32`, `ToWide/FromWide` (#585); used by text drawing and localization
  - 1.1.6 Frame driver - `Core/GameLoop` - `GameLoop::BeginFrame(realSeconds)` cleans and clamps the frame time (0.25 s; a clamp only ever shortens a frame), counts hitches, fires OnPause/OnResume, steps Time and the clocks, runs `OnFixedUpdate` (up to 5 steps, at most `kMaxFixedStepsPerFrame` = 10000; the surplus is dropped, not owed), then `OnUpdate`/`OnLateUpdate`; `Alpha()` for smooth drawing; `BeginSession()` is a clean Play; `player/main.cpp` runs on it since #585 (`maxDelta` 0.1, smoothing, `OnUpdate` gated on focus, `loop.BeginSession()` instead of `Game::Reset()`)
  - 1.1.7 Named clocks - `Core/Clock` - `Clock::Get("game")`, `"ui"` (real time, ignores pause), `"cinematic"` (real time, holds on pause) and your own channels, each with its own scale, pause and `Delta()`; `ClockSet::Global()` is ticked once a frame by the loop (and now in a built game, since the player uses `GameLoop`)
  - 1.1.8 Hit stop, time-scale ramps, real time - `Core/Time` - `Time::HitStop(seconds, scale)` freezes or slows on real time and composes with the script's time scale (up to 8 at once); `RampTimeScale(target, seconds)`; `RealtimeSinceStartup`, `UnixSeconds`, `FormatDuration`, `FormatClock`; `SetClockSources` test clock hook; `EffectiveTimeScale`
  - 1.1.9 Pause reasons, focus pause, quit reasons - `Core/Game` - `PushPause("menu")`/`PopPause` so two systems cannot un-pause each other, `SetPauseOnFocusLoss` + `NotifyFocus`, `RequestQuit(reason, code)`, `SetQuitGuard` (a veto, for "unsaved changes?"), `ForceQuit`, `ExitCode`, `BeginSession` (Reset plus time scale 1)
  - 1.1.10 Random extras - `Core/RandomExtras` - `rnd::PointInCircle/PointOnCircle/PointInRing/PointInSphere/PointInBox/Direction2D/Direction3D`, `DirectionInCone/DirectionInCone2D` (weapon spread), `PointInTriangle`, `Sign`, `ShuffleBag`, `WeightedTable` (with and without replacement), dice notation (`RollDice("2d6+3")`, `ParseDice("4d6kh3")`); works with `Random` and with `Rng` (1.1/36.7)
  - 1.1.11 Frame stepping, debug time scale, capture mode - `Core/GameLoop` - (#581) `RequestStep(n, dt)` advances a paused game exactly n frames, `SetDebugTimeScale` for slow motion or fast forward while debugging, `SetCapture(fps, frames)` makes every frame exactly 1/fps for screenshots and golden tests, `frameBudget`
  - 1.1.12 Frame recording and replay - `Core/FrameRecord` - (#581) `FrameRecorder`/`FramePlayer` and the small text file `okayframes 1` (checksummed, strict loader); a replay through a `GameLoop` with the same options reproduces `Time` values bit for bit (per docs and test names)
  - 1.1.13 Frame statistics and server tick pacer - `Core/FrameStats`, `Core/TickPacer` - (#581) `GameLoop::Stats()` keeps a 240-frame window (fps, 1% low, worst frame, hitches) and can sync into `Debug/FrameHistory`; `TickPacer` is the pacing arithmetic `DedicatedServer` uses (`engine/src/Net/DedicatedServer.cpp` includes it)
  - 1.1.14 Poll timers and clock ramps - `Core/Countdown`, `Core/Clock`, `Core/Stopwatch` - (#585) `Countdown` (`Start/Tick/Consume/Extend/Overshoot/Progress`, e.g. a jump buffer or a fuse) and `Cooldown` (`TryUse/Trigger/Reduce/Progress`, held fire is frame-rate independent), both with `Save/Load` through the save framework; `Clock::RampScale/Ramping/RampTarget/RampSecondsLeft/CancelRamp` and `Clock::Restore`; `Stopwatch::OnClock(name)` follows a named clock
  - 1.1.15 Save snapshot of clocks and random - `Core/TimeSnapshot` - (#585) `TimeSnapshot::Capture().AddRandom("loot", rng).Write(writer)` and `Read`, `Apply`, `ApplyRandom` put every clock's scale, pause, elapsed time and ramp, the gameplay time scale and named random streams into a save document; `ElapsedTime`, `UnscaledTime` and `FrameCount` are deliberately not restored
  - 1.1.16 Frame-stat export and loop console - `Core/FrameStats`, `Core/LoopConsole` - (#585) `FrameStats::ToCsv/SaveCsv`; `LoopConsole(loop, prefix)` registers `time.stats`, `time.stats_csv`, `time.scale`, `time.debug_scale`, `time.pause`, `time.step` on the console (`Core/Console`, 1.18.1)
- **Code:** `Core/Application`, `Core/Game`, `Core/Time`, `Core/Random`, `Core/Utf8`, `Core/GameLoop`, `Core/Clock`, `Core/RandomExtras`, `Core/FrameRecord`, `Core/FrameStats`, `Core/TickPacer`, `Core/Countdown`, `Core/TimeSnapshot`, `Core/LoopConsole`, `Core/Stopwatch`
- **API:** `Application::Config`, `Application::Run`, `GameLoop::BeginFrame`, `GameLoop::Alpha`, `GameLoop::RequestStep`, `SetDebugTimeScale`, `SetCapture`, `Clock::Get`, `Clock::RampScale`, `Time::DeltaTime`, `Time::SetTimeScale`, `Time::HitStop`, `Time::RampTimeScale`, `Time::SmoothDelta`, `Game::SetPaused`, `Game::PushPause`, `Game::RequestQuit(reason, code)`, `Game::SetQuitGuard`, `Random::Shared`, `Random::LockShared`, `Random::Range`, `rnd::PointInCircle`, `rnd::DirectionInCone`, `ShuffleBag`, `WeightedTable`, `RollDice`, `utf8::Next`, `utf8::Sanitize`, `FrameRecorder`, `FramePlayer`, `FrameStats::ToCsv`, `TickPacer`, `Countdown`, `Cooldown`, `TimeSnapshot::Capture`, `LoopConsole`
- **Editor:** none (`player/main.cpp` runs on `GameLoop` since #585; the editor's Play loop still clamps and steps by hand, never calls `Time::Step`, and no window shows the frame statistics, step buttons or debug time scale)
- **Docs:** `docs/making_a_game.md`, `docs/core_audit_notes.md`, `docs/loop_and_time.md`
- **Tests:** `timescale`, `time_pause`, `random`, `core_features`, `core_fixes`, `utf8`, `game_loop`, `clock`, `time_extras`, `game_state`, `random_extras`, `application_loop`, `loop_audit2`, `loop_audit3`, `frame_step`, `capture_mode`, `frame_record`, `frame_stats`, `tick_pacer`, `loop_player_style`, `frame_pacing`, `countdown`, `time_snapshot`
- **Status:** Works - three passes merged (#579, #581, #585), covered by deterministic tests with fake frame times and, per the docs, a randomized comparison of `TickPacer` with the old server code; read `docs/loop_and_time.md`, the `GameLoop`, `Clock`, `Countdown`, `TimeSnapshot`, `LoopConsole`, `Application` and `Utf8` headers and `player/main.cpp`'s loop on main (the .cpp files were not read in depth); 11 + 13 + 4 bugs fixed across the passes (NaN time, hit stop, pause un-pausing, raw terminal after an exception, a clamp that lengthened a frame, options on `loop()` lost by `Run`, a tiny `fixedStep` that hung the loop)
- **Open:** the editor Play loop is not on `GameLoop` and never calls `Time::Step`, so `Time.timeScale`, hit stop, ramps and `Clock` channels do nothing in editor Play (`docs/loop_and_time.md` "Adopting GameLoop": one loop with `OnUpdate = TickGame`, `BeginFrame(dt)` replacing the clamp and smooth lines, editor pause as `PushPause("editor")`, Step as `RequestStep()`, `BeginSession` in Play/Stop); `editor/EditorState.cpp` lines 736 and 767 and `engine/src/Net/DedicatedServer.cpp` line 548 still call `Game::Reset()` (it keeps a script's time scale) and should call `BeginSession()`; `Game::TogglePause()` does nothing while another system holds a pause; no overlay shows `Stats()` (the player's F3 overlay still uses its own `dt`); `Random::Shared()` is not thread safe (use `LockShared()` or `ThreadLocal()`); `Application` sleeps for pacing unless `Config::pacing` is `Limiter`; rule: `Okay.hpp` must not expose `okay::Clock` (a program with its own `Clock` and `using namespace okay` broke `test_save_scene` once; `Stopwatch::OnClock` is defined in `Core/Clock.hpp` and `ClockCursor::Take` takes numbers for that reason)
- **Related:** 1.2, 1.7, 1.10, 1.18, 6.4, 8.1, 16.5, 36.7, 15.14
- **Words:** Application, Game, Time, GameLoop, Clock, deltaTime, time scale, slow motion, hit stop, freeze frame, pause, pause reason, quit, quit guard, frame rate, FPS, hitch, fixed timestep, frame step, capture mode, frame recorder, frame statistics, 1% low, TickPacer, Countdown, Cooldown, cooldown, jump buffer, coyote time, Stopwatch, clock ramp, TimeSnapshot, LoopConsole, time.scale, LockShared, Random, seed, shuffle bag, weighted table, dice, cone spread, UTF-8, Sanitize, game loop
- **Seen as:** movement judders although the frame rate looks steady; slow motion or pause does not stop something; slow motion and hit stop do nothing in the editor's Play mode; the game keeps running after Quit; the same "random" result every run; a menu freezes during pause; a pause menu and a cutscene un-pause each other; the game jumps ahead after a window drag; a weapon fires at a different rate at 30 and 144 fps; reloading a save re-rolls a chest
- **Ask me like:** "Pause freezes the timers but my menu animation should keep running, which time do I use?"; "Add a seeded random so the loot rolls are the same on every run"; "Freeze the game for 80 ms when the player is hit"; "Give the weapon a 0.25 s cooldown"; "Switch the editor's Play mode to GameLoop"

### 1.2 Timers and tweens
- **What:** Do something after a delay, every N seconds, or animate a value over time with easing, from C++, scripts or the Inspector; timers can follow a named clock and die with the object they act on, eases can be written as text, any property can be tweened and springs follow a moving target.
- **Parts:**
  - 1.2.1 Scene scheduler - `Core/Scheduler` - `Invoke`, `InvokeRepeating`, `Tween(duration, onUpdate, onComplete)`; options for unscaled time, tag, owner, clock and object; Cancel, CancelByTag/Owner/Object, SetPaused, SetPausedByTag/Owner, SetInterval, Remaining, CountByTag; safe to call from inside a callback; elapsed time kept in double; a backlog runs at most `kMaxCatchUp` (4096) times per update; a throwing callback drops only its own task (the first exception is rethrown after the update)
  - 1.2.2 Global scheduler - `Core/Scheduler` - `Scheduler::Global()` survives scene loads and Play restarts; ticked once per frame by `Scene::Update`; a callback that captures a scene object says so with `Options::object`
  - 1.2.3 Tween engine - `Core/Tween` - DOTween-style `Tweener`, `ValueTween`, `CustomTween`, `TweenSequence`; chainable SetEase/SetDelay/SetLoops (Restart, Yoyo, Incremental), callbacks, Play/Pause/Kill/Goto; NaN, infinite or negative times cannot poison a tween
  - 1.2.4 Tween shortcuts - `Core/Tween` - `TweenManager` Move, Scale, Rotate, Punch*, Shake*, Jump, Path, ColorTo, Fade, AnchorPos, SizeDelta, Text, Counter; killed when their target object is destroyed; a shortcut that cannot make a tween returns an inactive one
  - 1.2.5 No-code tween component - `Components/TweenAnimation` - a tween set up in the Inspector that plays at scene start or by id from a script; has `clock` and `easeCurve` fields (saved as an optional tail, old files unchanged); removing the component ends its tween
  - 1.2.6 Easing curves - `Math/Easing` - the `Ease` enum and `Easing::*` functions (see 36.3)
  - 1.2.7 Task owners - `Core/Scheduler` - `Options::object` (the `GameObject` a task acts on: dropped silently when it dies or its scene unloads) and `Options::alive` (any predicate), `CancelByObject`, `PurgeDeadOwners`; the visual-script `tween_move`, `tween_scale`, `shake`, `punch_scale`, `tween_rotate`, `tween_color`, `fade`, `flash` instructions use it
  - 1.2.8 Clock channels - `Core/Scheduler`, `Core/Tween`, `Core/ClockCursor` - `Options::clock`, `Tweener::SetClock`, `SetClockSet` make a task or tween follow `Clock::Get("ui")` or your own channel; `ClockCursor` reads what a clock advanced; the scheduler self-ticks the global `ClockSet` once per `Time::FrameCount()` when the host does not run `GameLoop`
  - 1.2.9 Debug view - `Core/Scheduler`, `Core/Tween` - `Scheduler::Tasks()`/`Dump()` and `TweenManager::Dump()` give a text list of live tasks for an overlay
  - 1.2.10 Ease curves as data - `Math/Easing` - `Easing::ToName/FromName`, `CubicBezier`, `Steps`, `EaseCurve` (parses and writes `cubic-bezier(...)`, `steps(n)`, `ease-in-out`, preset names); `Tweener::SetEaseBezier/SetEaseSteps/SetEaseCurve/SetEaseSpec`
  - 1.2.11 Tween any property - `Core/Tween` - `DOTween::Property(go, "Light.intensity", PropertyValue, duration)` writes through the property registry (1.19); script `tween_prop(path, to, dur[, ease][, "done"])`, `tween_clock(id, channel)`, and `tween_ease` and the ease argument of `tween_*` accept curve text
  - 1.2.12 Springs - `Math/Spring` - `Spring::Step`, `StepCritical`, `StepVec2/3`, `StepCriticalVec2/3`, `StepAngle`, `StepQuat`, `StepQuatCritical` and a `Spring` state object; closed-form, so one big step equals many small ones
- **Code:** `Core/Scheduler`, `Core/Tween`, `Core/ClockCursor`, `Components/TweenAnimation`, `Math/Easing`, `Math/Spring`
- **API:** `Scene::scheduler()`, `Scene::tweens()`, `Scheduler::Global`, `Scheduler::Options`, `CancelByObject`, `Scheduler::Dump`, `Tweener::SetEase`, `SetEaseSpec`, `SetClock`, `TweenManager::Move`, `DOTween::Property`, `TweenSequence`, `EaseCurve::Parse`, `Spring::StepCritical`, script `tween_play_id`, `tween_prop`, `tween_clock`
- **Editor:** Tween Animation component in the Inspector (editor/main.cpp, with an ease preview for the preset eases); no inspector fields for `clock` or `easeCurve` yet
- **Docs:** `docs/timers_and_tweens.md`
- **Tests:** `scheduler_reentry`, `scheduler_hardening`, `scheduler_owner_clock`, `timers_tweens`, `tween`, `tween_edges`, `tween_features`, `easing`, `spring`
- **Status:** Works - merged in #560 and hardened in #587 (15 bug fixes); read `Scheduler.hpp`, `Tween.hpp`, `ClockCursor.hpp`, `Easing.hpp`, `Spring.hpp`, `TweenAnimation.hpp` and `docs/timers_and_tweens.md` on main (the .cpp files were not read in depth); tests cover re-entrancy, loops, big steps, time scale, lifetimes, owners, clock channels and edge numbers (per test names)
- **Open:** no inspector fields for `clock` and `easeCurve` (editor hook: a Clock combo from `ClockSet::Global().Names()` plus an ease-curve text field previewed with `EaseCurve::Parse/Evaluate`); no visual-script nodes for named or bezier eases or springs; no overlay for `scene.scheduler().Dump()` / `tweens().Dump()`; the editor's script autocomplete list does not know `tween_clock` and `tween_prop`; `Scheduler` handle lookups are a linear scan (cancel big groups by tag or owner); the tween's `float` time loses precision on endless tweens after many hours; the editor's Play loop does not tick the `ClockSet` (the player does since #585, and the scheduler's self-tick covers channel use in the meantime; ticking it belongs to 1.1); scene timers die with the scene by design; behaviour changes: negative, NaN and infinite frame times count as 0, a throwing callback ends its task, `SetEase` clears a custom ease curve
- **Related:** 1.1, 1.3, 1.19, 36.3, 12.4, 13.7
- **Words:** Invoke, InvokeRepeating, timer, delay, cooldown, Scheduler, Tween, DOTween, ease, yoyo, punch, shake, TweenAnimation, easing, coroutine alternative, cubic-bezier, steps, EaseCurve, spring, SmoothDamp, critical damping, tween_prop, tween_clock, DOTween::Property, CancelByObject, owner, ClockCursor
- **Seen as:** a delayed action never fires after a scene change; a tween keeps going after its object was deleted; an animation runs during pause when it should not (or the reverse); a timer fires on a destroyed object; a UI tween freezes with the game pause; a camera follow overshoots or lags
- **Ask me like:** "Make the door slide open over half a second with an ease-out"; "Cancel every timer tagged `wave` when the player dies"; "Tween this light's intensity by name"; "Make the menu slide ignore the pause"

### 1.3 Scene graph
- **What:** The object model: a Scene owns GameObjects, each with a Transform and components; layers and tags classify them and a layer matrix decides which colliders interact. Deep hierarchies (a 100000-deep parent chain), the Transform and spawn API, and the project tag and layer lists were hardened and extended in #586.
- **Parts:**
  - 1.3.1 Scene container - `Scene/Scene` - create, find, instantiate, destroy objects; sibling order; `FindObjectsOfType`; `Instantiate` overloads (position and rotation, parent, `worldPositionStays`), `Duplicate` (same parent and local pose, `UniqueName` "Cube (1)"), `DestroyImmediate`, `WorldBounds(go, includeChildren)`, `FindGameObjectsWithName/OnLayer`, `CountWithTag/OnLayer`, `UnlistedTags`, `ReplaceTag`; per-scene `renderSettings`, physics worlds, `fixedTimeStep`, `mainCamera`, scheduler and tween manager
  - 1.3.2 GameObject - `Scene/GameObject` - name, active (own switch; `ActiveInHierarchy()` is the effective one, 1.17), tag, layer, static flag, persistent `Guid()`, `InstanceId()`, Add/Get/RemoveComponent, `SetLayer(layer, includeChildren)`, `Path()`, `CompareTag`, first-person viewmodel flags, `transient` objects that are never saved (and neither is anything below them)
  - 1.3.3 Transform - `Scene/Transform` - local and world position/rotation/scale, cached world matrix, `SetParent` that keeps world pose, refuses cycles, destroyed and cross-scene parents; `GetChild`, `Find("A/B")`, `FindDeep`, `IsChildOf`, `Root`, `Depth`, `DetachChildren`, `SetAsFirstSibling/LastSibling`, `PlaceBefore/PlaceAfter`; `Space {Local, World, Self}` with `Translate`/`Rotate`, `RotateAround`, `TransformPoint/Direction/Vector`; world walks are loops, not recursion
  - 1.3.4 Component base - `Scene/Component` - Awake, Start, Update, LateUpdate, native `FixedUpdate` (#575), OnEnable/OnDisable, OnDestroy, 2D/3D collision and trigger messages, OnMouse*, `ExecutionOrder`, owned parts
  - 1.3.5 Layers and collision matrix - `Scene/Layers` - 32 layers with editable names (`SetName`, `ClearName`, `NameToLayer`, `ResetNames`, `Mask({...})`, `MaskToList`, `MaskFromList`); the 32x32 collision matrix lives on the Scene (`SetLayerCollision`, saved as `layermatrix`) with `ApplyLayerCollisionPreset(Everything | Nothing | SameLayerOnly)`, `SetLayerCollisionGroups`, `SetLayerCollisionByName`, `DisabledLayerPairs`; a saved layer outside 0-31 is stored modulo 32 with a warning
  - 1.3.6 Tags - `Scene/Tags` - `TagString` with a tag index for fast `FindWithTag` (every mutating string call counts as a change), project tag list (`Tags::Add`, `Remove`, `Rename`, `Strict`, `WhyInvalid`, `IsBuiltin`, `Index`, at most `kMaxCustom` = 1000), `GameObject::SetTag`
  - 1.3.7 Simple behaviours - `Components/Mover`, `Components/Spinner`, `Components/Lifetime` - constant movement (`space` Local by default), constant spin (`space` Self by default; one rotation about the velocity axis, so frame-rate independent), self-destroy timer (`SetElapsed`, `Restart`, saves the seconds already lived) without scripting; NaN velocity is ignored
  - 1.3.8 Project tag and layer lists - `Scene/ProjectLists` - (#586) `ProjectLists::Write/ApplyLine/Apply/Load/Save/Reset` read and write `tag=`, `strict_tags=` and `layer_name_N=` lines of `project.okayproj` and keep every other line; bad lines are skipped; included by `Okay.hpp`; the editor does not call it yet
- **Code:** `Scene/Scene`, `Scene/GameObject`, `Scene/Transform`, `Scene/Component`, `Scene/Layers`, `Scene/Tags`, `Scene/ProjectLists`, `Components/Mover`, `Components/Spinner`, `Components/Lifetime`
- **API:** `Scene::CreateGameObject`, `Scene::Find`, `Scene::FindWithTag`, `Scene::Instantiate`, `Scene::Duplicate`, `Scene::UniqueName`, `Scene::WorldBounds`, `GameObject::AddComponent<T>`, `GameObject::SetLayer`, `Transform::SetParent`, `Transform::GetChild`, `Transform::Find`, `Transform::Translate(delta, Space)`, `Transform::RotateAround`, `Layers::NameToLayer`, `Layers::SetName`, `Scene::SetLayerCollision`, `Scene::ApplyLayerCollisionPreset`, `Tags::Add`, `Tags::Rename`, `Scene::ReplaceTag`, `ProjectLists::ApplyLine`, `Mover::space`
- **Editor:** Hierarchy and Inspector (15.1, 15.2); layer picker on the Camera culling mask and object layer fields (editor/main.cpp)
- **Docs:** `docs/making_a_game.md`, `docs/layers_tags_cameras.md`, `docs/core_audit_notes.md`, `docs/scene_hierarchy_search.md`, `docs/scene_serializer.md`, `docs/scene_management.md`
- **Tests:** `scene`, `structure_version`, `transform_input`, `remove`, `parent_cycles`, `layers`, `layers_tags_cameras`, `subtree`, `scene_pointer`, `scene_defaults`, `gravity`, `gameplay`, `scene_hierarchy_search`, `deep_hierarchy`, `transform_graph`, `scene_graph_audit`, `layers_tags_project`, `scene_spawn`, `mover_spinner_lifetime`
- **Status:** Works - merged in #570, #575 and #586 (16 bug fixes); read `Transform.hpp`, `Scene.hpp`, `GameObject.hpp`, `Layers.hpp`, `Tags.hpp`, `ProjectLists.hpp`, `Mover.hpp`, `Spinner.hpp`, `Lifetime.hpp` and the docs on main (the .cpp files were not read in depth); fixed: a 60000-deep chain crashed, `ActiveInHierarchy` stopped at 4096 parents, `RemoveComponent` held an iterator across `OnDisable`, `tag.clear()` left a stale tag index, root reordering did not bump `StructureVersion()`, a scene saved with a NaN could not be opened
- **Open:** the editor still keeps its own tag list (`g_project.customTags`, a preset list) and does not call `Tags::*` or `ProjectLists` (editor hook in `docs/layers_tags_cameras.md`: `LoadProjectSettings` calls `ProjectLists::ApplyLine`, `SaveProjectSettings` appends `ProjectLists::Write()`, a layer-name box uses `Layers::SetName`/`ClearName`, tag rename uses `Tags::Rename` + `Scene::ReplaceTag`); `EditorState::DuplicateSelected` should use `Scene::Duplicate` (a duplicate of a child becomes a root at the child's local pose today); component order is still not saved (evaluated, reasons in `docs/scene_serializer.md`); `InverseTransformPoint` on a zero-scale object returns the world point (`Mat4::Inverse` returns identity for a singular matrix, per its comment); not verified, from the #586 notes: a 100000-deep chain costs O(n*depth) per frame in Update/Start (`ActiveInHierarchy` per component) and destroying one parent with hundreds of thousands of children is quadratic; calling `Clear()` or `LoadFromFile` from inside a component's `Update` frees the running object (`RequestLoad` is the safe call); behaviour changes: a multi-axis `Spinner` turns differently, `SetParent` refuses destroyed and cross-scene parents, anything under a transient object is no longer saved or cloned, `Lifetime` saves `elapsed`
- **Related:** 1.4, 1.5, 1.17, 2.10, 6.1, 6.2, 15.1
- **Words:** Scene, GameObject, Transform, Component, Behaviour, hierarchy, parent, child, layer, tag, FindWithTag, layer matrix, collision matrix, collision preset, layer name, project tags, strict tags, project.okayproj, ProjectLists, static, Mover, Spinner, Lifetime, Instantiate, Duplicate, UniqueName, DestroyImmediate, WorldBounds, Space.Self, RotateAround, deep hierarchy, scene graph
- **Seen as:** a child jumps when re-parented; two objects pass through each other (or collide) unexpectedly; "find by tag" returns the wrong object after a tag change; a parent cycle is refused; a very deep hierarchy crashed or hung; a duplicated child lands at the scene root; a scene file with a stray NaN would not open; a spinner turns differently at different frame rates; a custom tag or layer name is lost on restart
- **Ask me like:** "Stop layer 3 colliding with layer 4"; "Why does `SetParent` change the child's local position?"; "Name layer 8 Enemy and save it with the project"; "Duplicate the selected child under the same parent"

### 1.4 Scene lifecycle
- **What:** What happens when objects are created, switched on and off, destroyed and when scenes are loaded, replaced, added next to each other or streamed in over several frames.
- **Parts:**
  - 1.4.1 Start and enable events - `Scene/Scene`, `Scene/Component` - Start waits until a component is enabled on an active object; OnEnable/OnDisable alternate and are delivered at the start of the next frame
  - 1.4.2 Destroy - `Scene/Scene` - `Destroy` queues the whole subtree, `Destroy(go, seconds)` delays it (zero, negative and NaN destroy at once), `DestroyImmediate(go)` frees now (#586; from inside an Update or a callback it only queues, because freeing components under a running loop is not safe), `FlushDestroyed` runs at the end of the frame; `RemovalWatch` lets physics skip removed components
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
  - 1.5.7 Canonical text - `Scene/SceneText` - (#578) a tolerant parser of scene and prefab text that never builds a Scene; `SceneText::Canonicalize` gives equal scenes equal text (layout, number spelling, record order, object numbering) and leaves anything it does not understand alone; knows the `gravity3` record since #582; helper sources `engine/src/Scene/SceneTextDetail.*`
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
  - 1.6.10 Editor prefab actions - `editor/PrefabEdit.*` - the editor's use of 1.6.6 to 1.6.8: instance info, per-field names, Revert / Apply with undo and with the other instances updated, Create Variant, Unpack, Update All (21.2 to 21.4)
  - 1.6.9 Prefab text cache - `Scene/SceneSerializer` - (#575) the decoded text of the last 32 prefab files is kept so repeated spawns skip the file read; `SceneSerializer::ClearPrefabCache()`
- **Code:** `Scene/SceneTemplates`, `Scene/UITemplates`, `Scene/UIStyles`, `Scene/UILayoutTools`, `Scene/EnsureCamera`, `Scene/SceneSerializer`, `Scene/PrefabTools`
- **API:** `SceneSerializer::InstantiateFromFile`, `SceneSerializer::ReapplyPrefab`, `GameObject::prefabOverrides`, `Templates::*`, `UITemplates::AddHUD`, `EnsureSceneCamera`, `PrefabTools::Diff`, `PrefabTools::SavePrefab`, `PrefabTools::SaveVariant`, `PrefabTools::Unpack`
- **Editor:** GameObject > Create menu, Hierarchy prefab Apply/Revert/Reload (editor/main.cpp); no override UI yet
- **Docs:** `docs/prefabs.md`, `docs/prefab_system.md`, `docs/making_a_game.md`
- **Tests:** `instantiate`, `templates`, `scene_cameras`, `scene_defaults`, `prefab_nested`, `prefab_variants`, `prefab_apply_revert`
- **Status:** Partial - prefab files, override data, nested prefabs and variants work as library code and the editor shows and edits them (1.6.10; tests `prefab_editor`); prefab mode (editing a prefab in its own scene) is not built
- **Open:** prefab mode (Open isolates the instance in the Scene view only); the old override unit is a whole record, `PrefabTools::Diff` goes per field but falls back to whole records for unknown ones; objects added under an instance are not carried over by `ReapplyPrefab`
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
  - 1.7.5 Save framework - `Core/SaveFramework`, `Core/SaveFrameworkScene`, `Core/SaveFrameworkSync` - (#572) saveable objects and `SaveSystem::CaptureScene/ApplyScene`, `<slot>.okayslot` slots with headers, quick save, autosave ring, checkpoints, migrations, sync hooks and a slot menu model; separate from `SaveGame` (see 35); `Countdown`/`Cooldown` and `TimeSnapshot` (1.1.14, 1.1.15) save through its `SaveWriter`/`SaveReader`
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
  - 1.15.2 Quality preset - `Core/DisplaySettings` - `ApplyQuality` flips shadows, per-pixel lighting, bloom, SSAO and FXAA, and sets shadow resolution, cascade count and shadow distance
- **Code:** `Core/DisplaySettings`
- **API:** `DisplaySettings::SetQuality`, `DisplaySettings::SetFullscreen`, `DisplaySettings::Generation`, `DisplaySettings::ApplyQuality`
- **Editor:** none (the player reads it; Settings screen component, 12.8)
- **Docs:** -
- **Tests:** `visibility_lod`
- **Status:** Basic - Low/Medium/High now also set shadow map size, cascade count and distance (High matches the old 2048 / 80 m / 3 defaults); `FXAAEnabled()` and `PerPixelLighting()` are still read only by the software path
- **Open:** resolution and window mode options; the editor calls `ApplyQuality()` at start (editor/main.cpp), the player reads `DisplaySettings` too
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
  - 1.17.3 Scene validation - `Scene/SceneValidation` - `scene.Validate("Assets")` and `SceneSerializer::ValidateFile` report MissingComponentType, UnknownField, MissingAsset, BrokenParentLink, DuplicateGuid, LoadError and, since #586, BadLayer (a `layer` outside 0-31) and UnknownTag (reported only when `Tags::Strict()` is on) as a `SceneValidationReport`
  - 1.17.4 Scene diff, merge, migration - see 1.5.7 to 1.5.10 (libraries on the scene text) and 28.2, 28.4, 25.3 (what is still missing around them)
  - 1.17.5 Semantic scene validator - `Testing/SceneValidator` (#572, area 33) - missing assets and prefabs, no camera, duplicate names, bad scripts, non-finite transforms, Rigidbody without Collider
  - 1.17.6 Native FixedUpdate, load errors and speed - `Scene/Component`, `Scene/Scene`, `Scene/SceneManager`, `Scene/SceneSerializer` - `Component::FixedUpdate(dt)` once per physics step; `Clear()` runs OnDisable/OnDestroy for awakened components; `Scene::LastLoadError()`; `ActiveIndex` moves only after a successful load; `gravity3` record; floats written in the shortest exact form; a cache of 32 decoded prefab files (`ClearPrefabCache`); linear-time mass `Destroy`
- **Code:** `Scene/SceneValidation`, `Scene/GameObject`, `Scene/Scene`, `Scene/Transform`, `Scene/Component`, `Scene/SceneManager`, `Scene/SceneSerializer`, `Testing/SceneValidator`
- **API:** `scene.Validate("Assets")`, `SceneValidationReport`, `SceneSerializer::ValidateFile`, `Scene::FindByPath`, `Scene::Roots`, `GameObject::ActiveInHierarchy`, `GetComponentInChildren<T>`, `Component::FixedUpdate`, `Scene::LastLoadError`, `Transform::LookAt`, `SceneSerializer::ClearPrefabCache`, `SceneValidator::Validate`
- **Editor:** Problems panel (15.11.8, 33.2) lists these checks in edit mode; Build blocks on errors
- **Docs:** `docs/scene_hierarchy_search.md`, `docs/automated_testing.md`
- **Tests:** `scene_hierarchy_search`, `scene_validator`
- **Status:** Works - merged in #575 with tests; read `scene_hierarchy_search.md`, `Scene.hpp` and where `ActiveInHierarchy` is called (Scene, both physics worlds, GL, D3D11, D3D12 and the software renderer); `SceneValidation.cpp` itself was not read in depth
- **Open:** `Testing/SceneValidator` (semantic checks, area 33) and `Scene/SceneValidation` (format checks) both report missing assets; `CollectProjectDiagnostics` drops the duplicate missing-asset row and also reports `AssetDatabase::FindMissing` as `asset.db.missing`; unknown keys inside a known key=value record are skipped silently by the loader and not reported (docs)
- **Related:** 1.3, 1.4, 1.5, 33, 28
- **Words:** scene validation, validator, missing asset, broken link, FindByPath, hierarchy active, ActiveInHierarchy, FixedUpdate, GetComponentInChildren, LastLoadError, Roots, LookAt
- **Seen as:** a disabled parent does not disable its children's scripts; a scene opens with missing textures and no warning; a child still collides under a switched-off parent; a failed load leaves no reason
- **Ask me like:** "Validate this scene before the build"; "Find the object at Level/Arena/Enemy"

### 1.18 Core runtime utilities
- **What:** Small building blocks the rest of the engine and a game's own code lean on, with no scene or renderer needed: command-line parsing, console variables and commands, paths, build info, stopwatch, state machine, a job system, memory arenas and pools, generational handles, hashed string ids and frame pacing helpers. Merged in #571 (2.849.0) and #578 (2.850.0).
- **Parts:**
  - 1.18.1 CLI, console and cvars - `Core/CommandLine`, `Core/Console`, `Core/Cvar`, `Core/DebugOverlay` - (#571) `CommandLine` flag and value parser (the player uses it, `player/main.cpp`); typed console variables (`Cvars::Int/Bool`, range, persistence, `OnChange`) and a command registry (`Console::Register/Execute`, history, completion); `DebugOverlay` builds the FPS and object-count lines, switched by the `debug.overlay` cvar; no in-game console window calls them yet
  - 1.18.2 Paths, build info, stopwatch, state machine - `Core/Paths`, `Core/BuildInfo`, `Core/Stopwatch`, `Core/StateMachine` - (#571) per-platform data, config, cache and log folders and an asset base folder with listeners; version, compiler and platform strings; a real-clock or game-clock stopwatch (`Stopwatch::OnClock(name)` follows a named `Clock`, 1.1.14); a small enter/update/exit state machine
  - 1.18.3 Job system - `Core/Jobs` - (#578) `JobSystem::Global()`, `Submit` with dependencies and priorities, `ParallelFor`/`ParallelReduce`, main-thread queue (`RunOnMainThread`, `PumpMainThread`), cancel and shutdown, counters; 0 workers runs everything inline (WebGL builds without threads); threads and locks sit behind `engine/src/Core/CoreThreads.hpp`
  - 1.18.4 Memory arenas and pools - `Core/Memory` - (#578) `FrameArena` (per-frame bump allocator with a limit, `Mark/Rewind`), `PoolAllocator<T>` with stable addresses, `AllocStats`; header only
  - 1.18.5 Handles and slot maps - `Core/SlotMap` - (#578) `Handle<T>` (index plus generation) and `SlotMap<T>` so a stale handle gives nullptr instead of a dangling pointer; header only
  - 1.18.6 String ids - `Core/StringId` - (#578) `StringId` 64-bit FNV-1a hashes, the `_sid` literal, `Intern` with collision reporting
  - 1.18.7 Frame pacing - `Core/FramePacing` - (#578) `ClampFrameDelta`, `FrameTimer`, `FrameLimiter` (sleeps out the rest of a frame, a steady grid); `Application::Config::pacing = Pacing::Limiter` uses `FrameLimiter` (#585; the default is still `Pacing::Sleep`); the player and the editor do not call it, and `GameLoop::maxDelta` (1.1) applies the same clamp rule
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
  - 1.19.5 Built-in registrations - `Scene/BuiltinProperties` - 334 properties on 26 types per docs, a few more since `space` was added to Mover and Spinner in #586 (GameObject, Transform, Camera, Light, SpriteRenderer, MeshRenderer, 2D and 3D rigidbodies and colliders, AudioSource, ParticleSystem, Mover, Spinner, Lifetime, CameraFollow, Canvas, UIPanel); `RegisterBuiltinProperties()`
  - 1.19.6 Consumers - first one is `DOTween::Property` (1.2.11, #587: tweens any registered float, int, vector, colour or quaternion property by path, and the script `tween_prop`); planned: Inspector fields, per-property undo (22), keep-changes-from-Play (34.1), per-field prefab overrides (21.4) and visual-script nodes reading the registry instead of hand-written code per component
- **Code:** `Scene/Properties`, `Scene/PropertyRegistry`, `Scene/PropertyBuilder`, `Scene/BuiltinProperties`
- **API:** `PropertyRegistry::GetProperty`, `SetProperty`, `SetPropertyFromText`, `AddChangeHook`, `ScopedHookSuppression`, `SnapshotProperties`, `DiffProperties`, `PropertyBuilder<T>`, `PropertyValue`, `PropertyDesc`, `RegisterBuiltinProperties`
- **Editor:** none (nothing in the editor, the serializer, undo or prefab code calls it; the tween engine is the only caller)
- **Docs:** `docs/properties.md`
- **Tests:** `property_registry`
- **Status:** Partial - the registry, builder and built-in registrations are on main (#578) with a cross-check against the scene serializer (per docs: all 325 stored properties pass); the tween engine (`DOTween::Property`) is the only consumer so far
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
