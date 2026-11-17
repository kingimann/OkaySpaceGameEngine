# Modular engine (internal notes)

Design and decisions: `design/modular_gameplay_contracts.md` (section 11).

## What exists
- 40 module manifests (`engine/modules/<name>/module.okaymodule`), `ModuleRegistry`, `OKAY_MODULES` (default all), generated `modules_enabled.cpp`.
- A manifest `compile=` list names source files built only when the module is on; `register.cpp` runs only for modules that are on.
- Stripped for real so far: the model readers (`model_import`) and `Interactable` (`interaction`).
- Contracts: `health::`, `interact::`, `seat::`, `weapon::` (adapters in each module's `register.cpp`).
- `scripts/check_modules.py` (CI, baseline of known forbidden includes), `scripts/module_sizes.py`.
- Editor: New Project module choice, Project Settings > Modules, Add Component and Inspector hide components of modules that are off, Build warns about scenes that use them.
- Join handshake carries the module list; a mismatch is refused with a plain message.

## Missing
- Serializer, OkayScript VM, property tables, visual-script factory, validators and renderer still name every component, so most modules cannot be stripped yet.
- Scene v3 to v4 health merge, GameObject menu filtering, exporter drops nothing (warns only), preset player builds.

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


