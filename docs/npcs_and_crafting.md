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
health directly.

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
