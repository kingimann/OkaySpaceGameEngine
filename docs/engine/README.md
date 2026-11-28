# engine/: The engine core

> **What this is:** the list of pages in `docs/engine/`, one line each. The engine core: runtime, time, maths, scenes, prefabs, assets, saving, input, localization, settings, debug and speed tools.
>
> **Status:** index, made 2026-11-20 when the docs were sorted into folders. Back to the [start page](../README.md).
>
> **Last checked:** 2026-11-20.

- [Accessibility](accessibility.md): The engine side of accessibility. What a player can change today, and what really happens (each row is on the Settings screen, tested, and saved to Prefs when Done is pressed):
- [Architecture](architecture.md): the layers, the nine subsystems (rendering, physics, audio, animation, scripting, networking, world streaming, UI, platform), which folder holds each and what may include what.
- [Asset database](asset_database.md): `okay::AssetDatabase` (`okay/Core/AssetDatabase.hpp`) is the data layer an asset browser sits on: persistent GUIDs in a sidecar meta file per asset, move and rename tracking, a dependency graph, typed import settings, a thumbnail cache and...
- [Asset manager](asset_manager.md): `okay::AssetManager` (`okay/Core/AssetManager.hpp`) is an opt-in central registry for files loaded from disk.
- [Logging and profiling](core_logging_profiling.md): Two small core systems, both header-only: `okay/Core/Log.hpp` and `okay/Core/Profiler.hpp`.
- [Core runtime](core_runtime.md): The core runtime foundations and the core audit notes.
- [Core tools](core_tools.md): Small, dependency-free helpers in `okay/Core/`.
- [Shared code: one place for each job](shared_code.md): the one file to use for hashing, CRC, Base64, JSON, noise, easing, tweens, timers, pools, saves, input, logging and more. Look here before writing a helper.
- [Data assets and data packs](data_assets.md): Data assets and data packs: typed data files a game reads (headers `okay/Core/DataAsset.hpp`, `okay/Core/DataPack.hpp`).
- [Profiling and debugging tools (data layer)](debug_tools.md): The profiling and debugging tools data layer (headers in `engine/include/okay/Debug/`, sources in `engine/src/Debug/`).
- [Display settings](display_settings.md): `okay::DisplaySettings` (`okay/Core/DisplaySettings.hpp`) holds what the Settings screen changes about the window and the picture: the **window mode**, the **resolution**, **vsync** and the **quality preset**.
- [Input](input.md): Where the input code is, what is missing, and the input audit.
- [Localization](localization.md): The string table, the localization tools, and the formats / typed Prefs / fixed timestep record.
- [Loop and time](loop_and_time.md): How a frame runs and how the engine keeps time.
- [Math, geometry, noise and RNG](math_geometry.md): Header-only additions to the core math library.
- [Modular engine](modular_engine.md): Engine modules: the `module.okaymodule` manifests, the module groups in the build and the `module:<name>` test labels (design: `docs/design/modular_gameplay_contracts.md`).
- [Modularization plan](modularization_plan.md): the audit of the largest files, include cycles, platform code and names, and the ranked plan for splitting files, fixing the layers and cleaning up names (includes the `editor/main.cpp` split and the `launcher/main.cpp` map).
- [Optimization tools](optimization_tools.md): Engine-side algorithms for feature map area 27: LOD generation, occlusion culling, batching, streaming and a scene analysis report.
- [Performance log](perf_log.md): Owner request (2026-11-15): "work on optimization for the engine, improve FPS." This page is the record: what was measured, what was changed, and the numbers before and after.
- [Prefabs](prefabs.md): Prefabs, overrides, nested prefabs and variants, and Prefab mode in the editor.
- [Automation (design note)](automation.md): the one table of commands that scripts, tests and AI agents (through `okay mcp`) run to work the engine and the editor, and how to add one.
- [Properties (reflection)](properties.md): The property registry lets code read, write and watch a component's fields **by name**, without knowing the component's C++ type.
- [Save games](save_games.md): Save slots and the save game framework underneath them.
- [Scene diff, merge, canonical form and migration](scene_diff_merge.md): Engine-side libraries that let version control work with scene (`.okayscene`) and prefab (`.okayprefab`) files.
- [Scene hierarchy, search and validation](scene_hierarchy_search.md): Feature map 1.3 - 1.5. Everything here is additive: old scene files load unchanged.
- [Scene management](scene_management.md): What the engine gives you for loading scenes, spawning objects and declaring what a component needs.
- [Scene serializer notes](scene_serializer.md): Format and loading rules are in `engine/include/okay/Scene/SceneSerializer.hpp`.
- [Splines, lines and trails](splines.md): A **Spline** is a curve you draw in the Scene view: points with handles, open or closed.
- [Timers and tweens](timers_and_tweens.md): Two systems schedule work over time. The `Scheduler` runs callbacks after a delay or on an interval.
- [Volumes and zones](volumes.md): A **Volume** is a region of the level (a box, a sphere or a cylinder) that does one thing to what is inside it.
