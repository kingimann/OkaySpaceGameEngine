# Modular engine: state of each stage

Plan: `modular_gameplay_contracts.md` section 9 (PR numbers below are from there). "Branch" is `claude2/modular-2`
(the coordinator's `claude/modular-1` merged with main on 2026-11-22, overlaps resolved as in section 11.1). Stage 0 is
being landed first, in small branches made from main; everything after it waits until those merge.

| Stage | PR (section 9) | What | State on the branch | Landing branch |
|---|---|---|---|---|
| 0 | 1 | Module groups in the engine build, `module:<name>` test labels, one linked result | Branch has `cmake/OkayModules.cmake` that also strips `compile=` files | `claude2/modular-s0-build`: grouping and labels only, no behaviour change |
| 0 | 2 | `ModuleManifest`, `ModuleRegistry`/`ModuleContext`, generated `modules_enabled.cpp`, `check_modules.py` report with a shrink-only baseline | Done on the branch, with `OKAY_MODULES`, register hooks and about 740 baseline entries | `claude2/modular-s0-manifest`: parser, registry (only what stage 0 uses), generated list, check in CI report mode, about 800 baseline entries |
| 0 | 3 | Per-module size report (`module_sizes.json`, Build report table) | `scripts/module_sizes.py` counts object sizes per module; no link map, no Build report table | `claude2/modular-s0-sizes`: every player build writes a link map and `module_sizes.txt`; Build Game's report has a Modules tab |
| 1 | 4-6 | Scene records through `ComponentSerializerRegistry`, module by module | Started: `daynight`, `ragdoll`, `nav*`, `hitzones`, `interactable` records live in their modules | - |
| 1 | 7 | Property tables per module | Started: DayNightCycle's table is registered by sky_weather (`engine/modules/sky_weather/register.cpp`). The other gameplay tables are still in `BuiltinPropertiesGameplay.cpp` | - |
| 1 | 8 | `ScriptApiRegistry` | Started: `time_of_day`, `ragdoll`, `ragdoll_push`, `is_ragdoll` | - |
| 1 | 9 | Visual-script node registry | Started: `VsNodeRegistry`. Swing, Fire, Reload, WeaponAmmo and SetWeapon register as the weapons module. `MakeNode` and the add-node list hide them when weapons is off. Every other node is still the hard-coded switch | - |
| 1 | 10 | Validator, saver, localizable, tween, UI-blocker registries | Started: singleton rule registry (DayNightCycle) | - |
| 1 | 11 | Render contributions | Not started | - |
| 1 | 12 | `INetReplicated`; module list in the join handshake | Handshake carries the module list and refuses a mismatch; `INetReplicated` not started | - |
| 1 | 13 | Templates and editor menus from registries | Started for the node list only: the add-node palette skips a node whose module is off (`VsNodeSuppressed`). Add Component and the template lists are still hand-written. `editor/main.cpp` was not edited | - |
| 1 | 14 | `check_modules.py` blocking | Blocking for new edges only (baseline ratchet) | - |
| 2 | 15-21 | Move each module's files into `engine/modules/<name>/` | Not started (manifests point at today's paths) | - |
| 2 | 22 | Core-only build, `OKAY_MODULES`, player with a module list | `OKAY_MODULES` works for the registry and the `compile=` files (model readers, Interactable, Ragdoll); core-only library about 10% smaller (measured 2026-11-17, -O0) | - |
| 3 | 23 | Project Settings > Modules, Add Component filter, Problems rules | Model and drawing code exist (`editor/ModulesModel.hpp`, `ModulesPanelUI.hpp`, test `modules_model`); not wired into `editor/main.cpp` on this branch | `claude2/modular-s3-settings`: Project Settings > Modules page, saved in project.okayproj, Build report marks off modules; no stripping, no Add Component filter yet |
| 3 | 24 | New Project module picker with presets | Same as 23 | `claude2/modular-s3-newproject`: a folded Modules line in New Project with presets; no size column yet |
| 3 | 25 | Export: Modules table, `assets=` roots, drop disabled components | Not started | - |
| 3 | 26 | Preset player variants | `.github/workflows/preset-players.yml` (manual only, never run), `scripts/preset_modules.py` | - |
| 4 | 28 | Health and damage contracts | Main's one damage path (#1091) is the contract; the four other health lookups now use `HealthOf` | - |
| 4 | 29 | Health merge, scene v3 to v4 | Not started (afflictions still use `DamageHealthOn`, SurvivalStats first) | - |
| 4 | 30 | Interactable, InteractionSensor, interact action | Done on main's Interact action (#1101) and main's one-press token | - |
| 4 | 31-32 | Weapon contract, ammo, recoil, attachments | `IWeapon` adapters and `weapon::Find`; no merge of shooter code | - |
| 4 | 33 | Seats, doors, vehicle damage, hit zones with limb state | `ISeat` adapters; `HitZones` (no limb state) | - |
| 4 | 34 | Auto-wire, Setup buttons, naming test, scripting parity, integration matrix | Not started | - |

Files from other branches that a module will own when they merge are listed under `planned=` in its manifest
(physics3d: claude2/own-physics-engine; ragdoll: claude2/active-ragdoll). `save_games` claims `SaveGame*` and `GameSaves*`
by pattern, so it wraps whichever store is left after claude2/dedup-saves (#1132).
