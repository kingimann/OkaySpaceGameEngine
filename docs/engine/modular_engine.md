# Modular engine (internal notes)

Design and decisions: `../design/modular_gameplay_contracts.md` (section 11; 11.1 is the merge with main). Stages: `../design/modular_status.md`.

## What exists
- 40 module manifests (`engine/modules/<name>/module.okaymodule`), `ModuleRegistry`, `OKAY_MODULES` (default all), generated `modules_enabled.cpp`.
- A manifest `compile=` list names source files built only when the module is on; `register.cpp` runs only for modules that are on.
- Stripped for real so far: the model readers (`model_import`) and `Interactable` (`interaction`).
- Contracts: main's one damage path (`okay/Components/Damage.hpp`: `DealDamage`, `HealthOf`, `HasHealth`; Hit Zones scale a hit inside it), `interact::Find/Nearest/Use`, `seat::`, `weapon::` (adapters in the modules' `register.cpp`). The interact key is main's shared Interact action (`InteractPressed`, `UseKeyDown`, `UseKeyName`) and one press is used once through `ItemPickup::BlockInteract`.
- `scripts/check_modules.py` (CI, baseline of known forbidden includes), `scripts/module_sizes.py`.
- `planned=` in a manifest lists files another branch is adding (physics3d, ragdoll); nothing reads it yet.
- Editor: `editor/ModulesModel.hpp` and `ModulesPanelUI.hpp` (New Project module choice, Project Settings > Modules) exist and are tested as a model, but on `claude2/modular-2` they are not wired into `editor/main.cpp` (section 11.1: the Add Component hand edits were dropped; the page comes back in stage 3 from new files).
- Join handshake carries the module list; a mismatch is refused with a plain message.

## Missing
- Serializer, OkayScript VM, property tables, visual-script factory, validators and renderer still name every component, so most modules cannot be stripped yet.
- Scene v3 to v4 health merge, GameObject menu filtering, exporter drops nothing (warns only), preset player builds.

## Stripping proof (2026-11-17, Linux, -O0 with debug info, same build tree)
`libokay_engine.a`: 471,149,566 bytes with every module, 425,180,764 with `-DOKAY_MODULES=core` (-46 MB, -9.8%): the 35 model reader files, `Interactable.cpp` and the register.cpp of 8 modules are not in the library. `ReadColladaFile` is absent from the core library (nm). `test_modules` (112 checks) and `test_obj_import` pass in the core build; tests that call a stripped reader do not link there.

## Step: scene records through the registry (2026-11-17)
sky_weather (`daynight`), ragdoll (`ragdoll`) and ai_navigation (`navsurface`, `navagent`, `navmod`, `navlink`, `navobstacle`) write and read their records from their `register.cpp` (code moved unchanged; helpers Quote, KvOut, KvIn are now `okay/Scene/RecordIO.hpp`). A record of a module that is off is kept on the object and written back, with the warning "kept the 'x' record ... module is off in this build". Registered records are written after the built-in ones, so the line order of an object that has both can differ from before; the text loads the same and writing it again is stable (`test_module_records`).
Sizes (`libokay_engine.a`, -O0 with debug info): all modules 472,029,618; `OKAY_MODULES=core` 424,718,384 (-47.3 MB, -10.0%). Compared with the previous step the core build shrank only 0.46 MB: the component classes are header-only and still used by the validator, asset walker, property tables and editor, so most of the code stays. Bigger wins need those registries cut first.
Build Game now refuses (not only warns) a scene that uses a module that is off, unless "Build anyway" is ticked (editor code compiles; not run).
Still open from the owner's list: VM bindings and visual-script nodes per module (vs_nodes), property tables per module, renderer contributions, GameObject menu and templates filtering.

## Step: validator rules, property tables, script functions (2026-11-17)
New registries (all reached through `ModuleRegistry::Get()`): `ScriptApiRegistry` (OkayScript functions, copied into the VM's builtin table), `rules::AddSingletonRule` (validator), module property tables via `PropertyBuilder` from `register.cpp`, `ModuleRegistry::Get()` no longer deadlocks when a hook asks for it again. Moved: sky_weather (DayNightCycle property table, singleton rule, `time_of_day`), ragdoll (`ragdoll`, `ragdoll_push`, `is_ragdoll`; `Ragdoll.cpp` is now `compile=`). Manifests have `script_api=`.
| Library `libokay_engine.a` (-O0, debug info) | bytes |
|---|---|
| all modules | 472,505,974 |
| `OKAY_MODULES=core` | 423,838,648 (-48.7 MB, -10.3%; the step before: 424,718,384) |
nm symbol count in the core library: Ragdoll 0, DayNightCycle 0 (all build: 366 and 627). NavMeshSurface still 1444: the navigation module is only partly moved (records), its classes are still named by the validator, ProjectDiagnostics, property tables, VM, EngineApi, visual-script builtins and BehaviorTreeRunner. A linked test (`test_module_script_api`, stripped, -O0): 39,731,792 bytes all, 36,504,144 core (-8.1%).
Not done: nav and the other modules' property tables/validators/VM functions, visual-script nodes (`vs_nodes`), renderer contributions, GameObject menu/templates filtering.

## Measured sizes (stage 0)

From `python3 scripts/module_sizes.py <build folder>` on 2026-11-17: source lines and the code+data bytes of each
module's object files in an unoptimised Linux build (`-O0`, so only the proportions mean anything; header-only
components count in the .cpp that includes them). "core" is everything no module claims, which today includes the
scene serializer, the renderer backends and the base components. A real per-module size needs the link map of the
player, which needs the per-module libraries (not built yet).

| Module | Files | Lines | % of lines | Object code+data (KB) |
|---|---|---|---|---|
| core | 304 | 92560 | 29.5% | 19857 |
| model_import | 48 | 23711 | 7.5% | 10155 |
| ui_kit | 109 | 21744 | 6.9% | 1730 |
| scripting_okayscript | 19 | 21703 | 6.9% | 10083 |
| scripting_visual | 42 | 16429 | 5.2% | 4119 |
| animation | 34 | 14897 | 4.7% | 2378 |
| networking | 48 | 14144 | 4.5% | 2567 |
| modeling | 40 | 12735 | 4.1% | 5627 |
| character_controllers | 29 | 8292 | 2.6% | 875 |
| dev_diagnostics | 33 | 8052 | 2.6% | 1859 |
| audio | 25 | 6764 | 2.2% | 623 |
| relay_online | 22 | 6387 | 2.0% | 381 |
| render3d_effects | 29 | 5301 | 1.7% | 1265 |
| terrain_foliage | 11 | 5280 | 1.7% | 1188 |
| save_games | 11 | 4995 | 1.6% | 1188 |
| physics3d | 22 | 4932 | 1.6% | 659 |
| inventory | 22 | 4497 | 1.4% | 1414 |
| localization_tools | 13 | 4392 | 1.4% | 889 |
| ai_navigation | 20 | 4218 | 1.3% | 828 |
| anticheat | 14 | 4015 | 1.3% | 1116 |
| ai_behavior | 8 | 3583 | 1.1% | 700 |
| building | 7 | 3313 | 1.1% | 650 |
| dialogue_quests | 12 | 3236 | 1.0% | 1238 |
| ik | 13 | 2655 | 0.8% | 19 |
| cutscenes | 8 | 2336 | 0.7% | 814 |
| interaction | 8 | 1989 | 0.6% | 141 |
| physics2d | 7 | 1964 | 0.6% | 206 |
| splines | 5 | 1527 | 0.5% | 523 |
| weapons | 8 | 1112 | 0.4% | 196 |
| vehicles | 5 | 1043 | 0.3% | 71 |
| health_damage | 5 | 1003 | 0.3% | 91 |
| ecs | 5 | 963 | 0.3% | 0 |
| crafting | 6 | 748 | 0.2% | 282 |
| survival | 5 | 674 | 0.2% | 40 |
| particles | 1 | 665 | 0.2% | 0 |
| sky_weather | 5 | 652 | 0.2% | 0 |
| controllers_2d | 3 | 621 | 0.2% | 0 |
| ragdoll | 2 | 512 | 0.2% | 108 |
| rpg_stats | 3 | 215 | 0.1% | 11 |
| tilemap2d | 2 | 215 | 0.1% | 0 |
| **total** | 1013 | 314074 | | 73905 |


