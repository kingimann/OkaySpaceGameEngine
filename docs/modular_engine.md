# Modular engine

The engine is a list of **modules** (physics, audio, animation, UI, weapons, inventory, networking and so on). A
project picks which modules it uses. The design and the reasons are in
[design/modular_gameplay_contracts.md](design/modular_gameplay_contracts.md) (section 11 lists the decisions taken).
This page says what exists today, how to use it and what is not done.

## What exists today

| Part | Where | State |
|---|---|---|
| Module manifests, one per module (40) | `engine/modules/<name>/module.okaymodule` | Done. Name, version, requires, provides, components, scene records, editor items, source globs. |
| `ModuleManifest` (parser) and `ModuleRegistry` | `engine/include/okay/Modules/ModuleRegistry.hpp`, `engine/src/Modules/` | Done. Which modules exist and are on, what each adds, requirements in dependency order, `Resolve` (add what is needed), `Validate`. |
| `OKAY_MODULES` build option and the generated `modules_enabled.cpp` | `cmake/OkayModules.cmake`, `engine/CMakeLists.txt` | Done. Default `all`: nothing changes. See below. |
| Per-module `register.cpp` (linked only when the module is on) | `engine/modules/<name>/register.cpp` | Done for 7 modules (health, survival, RPG stats, AI behaviour, interaction, dialogue, vehicles). |
| Dependency check | `scripts/check_modules.py`, run in CI by `branch-lint.yml` | Done. Fails on a new forbidden include or a broken manifest; today's 700+ known ones are in `scripts/module_deps_baseline.txt` and can only shrink. |
| Size report | `scripts/module_sizes.py` | Source lines per module, and object-file bytes when given a build folder. Not a link map. |
| Health, damage, hit zones contract | `okay/Modules/Damage.hpp`, `Health.hpp`, `Components/HitZones.hpp` | Done as adapters over the old components. |
| Interaction and seat contracts | `okay/Modules/Interact.hpp`, `ISeat.hpp` | Done as adapters; the interact key is the `Interact` input action. |
| New Project asks which modules; Project Settings > Modules | `editor/ModulesModel.hpp`, `ModulesPanelUI.hpp`, `editor/main.cpp` | Written and unit tested (the model); the drawing is not run in the real editor window. See "Unverified". |

## Using it

**See the modules.** `ModuleRegistry::Get().All()` lists them; each has `manifest` (what it says) and `enabled`.
`scripts/module_sizes.py` prints the size table.

**Choose modules when you build the engine** (developers and dedicated-server operators):

```
cmake -S . -B build                                       # every module (the default)
cmake -S . -B build -DOKAY_MODULES=core                   # only the core
cmake -S . -B build -DOKAY_MODULES=physics3d,health_damage,weapons
```

Every requirement must be in the list. A missing one fails configure and says which
(`module 'weapons' requires 'health_damage'; add it to the list`), as does an unknown name. The configure output ends
its module line with the counts: `OkaySpace: modules all (40 on, 0 off)`.

**Choose modules for a project.** File > New Project has a "Modules" part: four presets (2D game, 3D action,
Survival, Everything), a summary ("27 of 39 modules are on") and "Choose modules one by one". Picking a template
picks the fitting preset until you choose yourself. Edit > Project Settings > Modules shows the same list for the open
project, with how many objects of the open scene use each module, and warns about objects of a module that is off. The
choice is saved in `project.okayproj` as `modules=a,b,c` (core is always on and not listed). A project with no
`modules=` line has every module on, so projects made before modules existed behave as before.

Ticking a module ticks what it needs and says so. Unticking one that another ticked module needs is refused with the
reason (hover the greyed box). Turning a module off never edits a scene.

## The manifest

```
name=weapons
title=Weapons
area=Gameplay
description=Ranged and melee weapons, projectiles, weapon definitions.
version=1                  (optional, default 1)
default=on
core=0
requires=health_damage,physics3d
provides=IWeapon
components=RangedWeapon,Projectile3D,MeleeAttacker,Crosshair
serializers=rangedweapon,projectile3d      (scene record names it reads and writes)
vs_nodes=                  (visual-script node names it adds)
vs_group=
editor=                    (editor items it adds)
sources=include/okay/Components/RangedWeapon.hpp src/Components/RangedWeapon.cpp ...   (globs under engine/)
```

`sources` lists the files a module owns while the code still lives in the shared folders (the files move into
`engine/modules/<name>/` later; the list is what the dependency check and the size report use). A file no module
claims belongs to `core`.

To add a module: make the folder and manifest, run `python3 scripts/check_modules.py`, add a `register.cpp` with
`void okay::modules::Register_<name>(ModuleContext&)` if it has anything to register (run cmake again so the
generated file sees it), and add the new forbidden includes it reveals to nothing: fix them instead.

## The shared contracts

All three are small interfaces in `engine/include/okay/Modules/`. A module registers an **adapter** for each of its
components from its `register.cpp`, so a module that is off simply has no adapter. The components, their settings and
their saved records are unchanged: old scenes load as before.

- **Health and damage** (`health::`): `health::Damage(go, DamageEvent, parents)`, `health::Heal`, `health::Find`.
  `DamageEvent` carries amount, type (`Bullet`, `Melee`, `Fire`, ...), source, instigator, weapon, zone, hit point and
  origin. `HitZones` (new component, Add Component > Hit Zones) scales a hit by zone (`head` x2, `body`, `legs` x0.75;
  a hit with only a point is sorted by height) and by damage type. Adapters: `HealthStat` (first), `SurvivalStats`,
  `Stats`, `NPCController`. **Replaced and deleted:** `DamageGameObject`, `DamageHealthOn`, `HealOn`, the cascade in
  `ApplyCombatDamage`, `CanBeHurt`. Shots and blows now hurt every kind of health; before, a shot hurt only a
  `HealthStat`.
- **Interaction** (`interact::`, `IInteractable`): `interact::Pressed(key)`, `Take(key)` (one press is used once),
  `Find`, `Nearest`, `Use`. Adapters: `ItemPickup`, chests and crafting stations (`InventoryInteractable`),
  `DialogueRunner`, `Seat`, `VehicleSeat`. The key: a component's saved `interactKey` of `'e'` means the `Interact`
  input action (default E and pad X, rebindable), another letter stays a raw key, `0` means "not interactable". **Replaced
  and deleted:** `ItemPickup::BlockInteract/ClearInteractBlock`, the raw `GetKeyDown('e')` in `Seat`, in the visual-script
  `interact` input and in `VehicleSeat` (now the shared key).
- **Seats** (`seat::`, `ISeat`): `seat::Find(go)`, `seat::SeatOf(who)`; adapters for `Seat` and `VehicleSeat`.

## What is not stripped yet

Turning a module off today changes what the registry reports, what the editor lists and warns about, and whether that
module's `register.cpp` (its adapters) is compiled. It does **not** yet remove the module's other code from the
binary, because the scene serializer, the OkayScript VM, the property tables, the visual-script factory and the
renderer still name every component directly (about 740 baseline entries in `scripts/module_deps_baseline.txt` are those
edges). Cutting them is design stage 1; stripping the binary (per-module libraries, CI preset player variants) follows.
Until then there is no smaller player to download.

Also not done: the scene format v3 to v4 health merge (the old health fields still exist next to the contract), the
weapon contract (`IWeapon`) and ammo, the Add Component and GameObject menus hiding modules that are off, the export
gate that drops components of disabled modules, the size column (needs a measured link map), module list in the
multiplayer join handshake, `Interactable` and `InteractionSensor` components, `HoldPrompt` as a pure view.

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


## Tests

`test_modules` (parser, registry, the real list), `test_health_contract`, `test_interact_contract`,
`test_modules_model` (presets, ticking rules, project file). `python3 scripts/check_modules.py` is the dependency check.
Pictures of the two pages (software render, not the real editor window): `docs/images/modular/`, made by
`tools/modules_shots.cpp`.
