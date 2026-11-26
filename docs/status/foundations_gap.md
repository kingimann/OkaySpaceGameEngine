# Foundations: what we have, what runs, what is missing

> **What this is:** Internal audit (owner request 2026-11-21: "start from foundations, look at other engines, see what we are lacking and build our systems up").
>
> **Status:** Internal record. Read it as a dated record; the newest facts are in the code.
>
> **Last checked:** 2026-11-21 against main 5085afc45 by `git grep` and reading files. Nothing was built or run for this page. Unity, Unreal and Godot were compared from their documented features only; none of their code was read.

## The main finding

Many foundation libraries are written and tested but **nothing in the engine, editor or player calls them**: the job system, the allocators and slot map, the ECS (removed 2026-11-22), the asset manager, the memory tracker, the console and cvars, the event bus, the GPU timer and render stats, and the property-level undo commands. The code that actually runs uses hand-written paths instead:

- `engine/src/Scene/SceneSerializer.cpp` is about 10k lines with about 400 `else if` branches, one per component field family.
- `editor/main.cpp` is about 50k lines with the Inspector, Add Component menu and many panels written by hand.
- `OkayScriptVM.cpp` has about 1,235 hand-written builtins; NextScript and visual script each have their own binding surface.
- Four renderers (OpenGL, Direct3D 11, Direct3D 12, software) each write every shading model again, and the GPU ones read every frame back to the CPU.

Unity, Unreal and Godot all drive serialization, the Inspector, undo, scripting bindings and replication from **one** reflection system, render through **one** hardware interface with a pass or graph system, load assets through **one** asset manager with handles, and run heavy work on **one** job system. That is the gap.

## Foundation table

| # | Foundation | What runs today | Missing vs Unity / Unreal / Godot | Severity |
|---|---|---|---|---|
| 1 | Memory | `Core/Memory.hpp` (frame arena, pool), `Core/SlotMap.hpp`, `Debug/MemoryTracker`: tests only | Per-frame arenas in use, per-system budgets, GPU memory accounting, leak report | Important |
| 2 | Job system | `Core/Jobs.hpp`: tests only. Raw `std::thread` in 8 places, one OS thread per OkayScript coroutine | Physics, skinning, culling, rasteriser and asset loading on worker threads; render thread | Blocks others (speed) |
| 3 | Object model | GameObject/Component runs everything; lifecycle order is tested. The separate `ECS/*` store (tests only) was removed on 2026-11-22 | Generational handles instead of raw pointers; per-type component index (118 `FindObjectsOfType` scans) | Important |
| 4 | Reflection | `PropertyRegistry` (112 of about 240 component types), used by the fallback Inspector, tweens, visual script and one OkayScript builtin | One registry driving serializer, Inspector, undo, scripting and networking | **Blocks others** |
| 5 | Serialization | Text `okayscene 3`, positional records, migrations | Named fields (reorder-safe), GUID asset references instead of paths, binary/cooked form | Important (needs 4) |
| 6 | Asset pipeline | `AssetDatabase` (GUID meta, dependencies) used by the Project panel and build; `AssetManager` (handles, async, hot reload): textures use it (`Render/TextureAsset`, branch `claude2/texture-system`); nothing else does. Real loads are global maps, never unloaded | Import cache, cooking, file watcher, async load and unload on the real path | **Blocks others** |
| 7 | GPU resources | Textures: one shared `TextureAsset` per picture through `AssetManager` (`TextureHandle`, reference counted, hot reload, texture budget with idle trim); each renderer keeps only its GPU object per name and re-uploads on a new asset serial (`docs/rendering/textures.md`, branch `claude2/texture-system`). Meshes and shaders: still per-renderer caches; `MeshRenderer` holds its `Mesh` by value | Mesh and shader handles; one GPU resource manager with budgets across all resource kinds | Important |
| 8 | Rendering | Four separate renderer classes chosen by an if-chain written twice (player, editor); `IRenderer` covers 2D quads only | Hardware interface (RHI), pass/graph system, one shader source, variants, no per-frame readback (2026-11-22: a first step to one shader source: user surface shaders, `.okayshader`, compile from one source to GLSL, HLSL and a CPU program and are spliced into each renderer's main shader as a variant, see `docs/rendering/shaders.md`; the renderers' own shaders are still written four times; 2026-11-26: RHI round 1, `docs/rendering/rhi.md`: the core, OpenGL and Software backends, and the OpenGL Bloom moved onto it; Direct3D and the other passes follow) | **Blocks others** |
| 9 | Math | Scalar math, used everywhere; world transform cache compares copies on every read | SIMD, double precision or floating origin, dirty flags | Nice to have |
| 10 | Input | The built-in gameplay controllers now read named actions through `InputActions::ControlKey` / `ControlMouse` / `ControlMove` (first / third person, Character Controller 2D / 3D, top-down 3D and 2D, both shooters, 2.5D platformer, stance, ranged and melee weapons, item pickup, vehicle seat): same keys by default, a rebind reaches them (test `controller_actions`). Part 2: Vehicle Controller driving keys ("Vehicle" context actions), Pause Menu (a rebound "Pause" also closes it past its Block context), Free Roam, 2.5D movement / layer / grab keys, Top Down Shooter dash, lean and prone (new actions). Still raw: vehicle recover / camera / look-back / reverse keys and pad fields, dodge, throw, builders and diggers other than VoxelDigger, toggle keys of flashlight / view / inventory / crafting, boat, UI widgets, visual-script and OkayScript nodes | Move the rest; per-player devices and contexts for these controllers (they read player 0 and the base context) | Important |
| 11 | Physics | Own 2D and 3D, joints, queries, layers; tested. One 3D path since 2026-11-23: our own engine core (`okay_physcore`): dynamic AABB tree, GJK / EPA / SAT manifolds, islands solved in parallel with bit-identical results, speculative contacts, ball / cone-twist joints with drives; Classic, Improved 3D and 2D Classic deleted (`docs/physics/physics_core.md`) | Scene queries on the core tree; voxel terrain inside the core; 2D on the core | Important |
| 12 | Audio | Static mixer with 4 fixed groups, own decoders, spatial effects; mixes on the main thread | Mixer graph with user buses and effect chains, snapshots, audio thread | Nice to have |
| 13 | Animation | Works; three clip representations | One clip/pose format, compression, threaded skinning | Important (later) |
| 14 | Scripting | Three languages, hand-written bindings | Bindings generated from the registry, file-watch hot reload, text-script debugger, coroutines without OS threads | Important |
| 15 | Events, loop | `GameLoop` shared by player and editor Play; `EventBus`: tests only; physics keeps its own fixed-step accumulator | Engine events on the bus; one fixed step | Important |
| 16 | Editor | Undo rebuilds the whole scene from text; `UndoCommands` (property level): tests only; Inspector mostly hand-written | Property-level undo that keeps object pointers; Inspector from reflection; play-mode isolation of globals; editor extension API | **Blocks others** |
| 17 | Platform, files | Platform layer used, but direct SDL calls remain (player 116, editor 310, launcher 710); 69 direct file opens | Virtual file system with mounted packs; async file I/O | Important |
| 18 | Debug, profiling | Profiler markers only in `Scene.cpp` and the editor; console, cvars, GPU timer, render stats: no host | In-game console and cvars, GPU timings, frame debugger | Important |
| 19 | Build, tests | Build copies source files plus a zip; 688 test registrations | Cooking step; GPU backend tests; performance budgets in CI | Important |
| 20 | Networking | Transforms plus named string vars and RPCs (the unused `ECS/NetWorld` was removed on 2026-11-22) | Property replication driven by the registry | Nice to have (needs 4) |

## Built more than once

Object model (GameObject and ECS; the ECS was deleted on 2026-11-22, so it is one now), component description (about seven separate paths), undo (three), asset caches (about seven), threads (job system plus raw threads), fixed step (two), shading (four copies), debug overlays and stats (several), random and time helpers, two script VMs plus visual script, three animation clip formats, three character-collision paths, and direct SDL use beside the platform layer.

## Build order

Wiring existing code comes before writing new code. Each task ends with a test that proves it.

1. **One scene-renderer interface.** `ISceneRenderer` with a factory; the player and editor stop choosing backends with if-chains. Test: a small scene through the interface matches the existing software golden image.
2. **Console and cvars live** in the editor console panel and a player console; `--cvar` on the command line; profiler markers around render and audio in the player. Test: run a command and a `--cvar` against a game-loop host.
3. **Textures through `AssetManager`** with hot reload. Test: rewrite a PNG on disk and see the new version; clearing handles frees it. *Done on branch `claude2/texture-system` (`texture_asset` test; `docs/rendering/textures.md`).*
4. **Memory tracker producers** (texture, mesh, audio caches, object counts) shown in F3 and the Profiler. Test: load and free, counts return to zero. *Textures done on `claude2/texture-system` (MemoryTracker `Textures` and `Textures (GPU)`, F3 line, Profiler "Textures" section, `texture_asset` checks counts back to zero); mesh, audio and object counts not started.*
5. **Job system adoption**, deterministic: asset worker, navmesh bake, physics bounding-box pass, skinning. Test: physics with 0 and 4 workers gives bit-identical results.
6. **Registry coverage gate and generic serializer path**, so a new component never touches the `else if` chain. Test: every registered type round-trips through scene text; a known-gaps list only shrinks.
7. **Property-level undo from the registry** for Inspector edits. Test: undo and redo keep the same object pointers.
8. **Built-in controllers read input actions**, so rebinding reaches them. Test: rebind jump to a pad button and the controller jumps; old scenes keep their keys.

After those: per-type component index, a physics broad phase, one fixed step, GUID asset references in scene files, a cooked format with a mounted pack, presenting GPU output without readback, and a decision on the ECS (decided 2026-11-22: deleted, nothing used it).

## Stale docs found

`docs/FEATURE_MAP.md` and `docs/feature_map/01-core-scene.md` section 1.9 marked "Events and ECS" as Works, but nothing at run time used either. The ECS has since been removed and 1.9 is now "Events".

## Not verified

Whether the tests pass; how the physics solver works inside; how many globals leak across Play and Stop; exact feature parity of Direct3D 11 and 12; the launcher's use of these foundations; exact component-type counts; the quaternion convention; audio thread safety.
