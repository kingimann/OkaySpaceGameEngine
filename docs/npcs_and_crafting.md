# NPCs & Crafting

Two native components that round out the gameplay kit.

## NPC Controller

*Add Component ▸ Gameplay ▸ NPC Controller.* A simple steering AI that moves a sibling
`Rigidbody3D` (or the Transform) toward/away from a named target and turns to face its
heading. Pick a **Behavior**:

| Behavior | Does |
|----------|------|
| Idle   | stands still |
| Wander | roams randomly within `wanderRadius` of its spawn |
| Follow | approaches the target, stopping at `attackRange` (a pet/companion) |
| Flee   | runs from the target while it's within `sightRange` (prey) |
| Chase  | hunts the target within `sightRange`; within `attackRange` it bites for `attackDamage` every `attackInterval` (a predator) — out of sight it wanders. Broadcasts `npc_attack` per hit. |

Fields: `moveSpeed`, `targetName` (default `Player`), `sightRange`, `wanderRadius`,
`attackRange`, `attackDamage`, `attackInterval`, `faceMovement`. Chase damages the
target's health source (`SurvivalStats` or `HealthStat`), so it drops the player's
health directly. NPCs also have **`health`/`maxHealth`** and `Damage()` — at 0 HP they
broadcast `npc_died`, play a sibling AudioSource, and despawn, so the player can fight
them.

## Player attacks (Melee Attacker)

*Add Component ▸ Gameplay ▸ Melee Attacker.* On attack input (`attackKey`, default F,
and/or left mouse) it hits every NPC within `range` inside a `arc`-degree cone in
front for `damage`, on a `cooldown`. Broadcasts `player_attack`. `Swing()` is exposed
for custom inputs/scripts.

## Spawner (enemy waves / loot)

*Add Component ▸ Gameplay ▸ Spawner.* Names a `templateName` object to clone (hidden as
a blueprint at play) and spawns copies near itself every `interval` seconds, keeping
at most `maxAlive` of its own spawns alive and stopping after `totalToSpawn`
(0 = endless). Spawns land within `spawnRadius`. `SpawnOne()` and `AliveCount()` are
exposed. Make a "den" that drips out wolves, a chest that refills loot, etc.

## Crafting menu (auto UI)

*Add Component ▸ Gameplay ▸ Crafting Menu.* Builds one button per recipe of a sibling
`Crafting` at play (labelled with the output, wired to craft it), and toggles the
whole panel with `toggleKey` (default C). Zero UI wiring — add it next to a `Crafting`
+ `Inventory` and you have a working craft menu.

## Crafting

*Add Component ▸ Gameplay ▸ Crafting.* Recipe-based crafting over a sibling
`Inventory`. Each recipe lists input items + counts and an output item; `Craft` checks
the inventory has the inputs, removes them, and adds the output.

- `AddRecipe("torch", 1, {{"wood",1},{"cloth",2}})` (or the Inspector recipe editor).
- `Craft(name)` / `CraftIndex(i)` / `CanCraft(name)`.
- Drive it from a crafting-menu **button**: On Click ▸ Function `Craft`, **Amount** =
  the recipe index.

Pair with **Consumables** to make crafted items do something: craft a `bandage`, then
a button (Function `UseItem`, Amount = the consumable's index) consumes it and applies
its effect (e.g. Heal). That's the full **gather → craft → use** loop, all native and
no-code — exactly how OkaySurvival's bandage works.
