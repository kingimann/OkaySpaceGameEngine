# NPCs & Crafting

Two native components that round out the gameplay kit.

## NPC Controller

*Add Component ▸ Gameplay ▸ NPC Controller.* A simple steering AI that moves a sibling
`Rigidbody3D` (or the Transform) toward/away from a named target and turns to face its
heading. Pick a **Behavior**:

| Behavior | Does |
|----------|------|
| Idle   | stands still (a sentry if `aggressive`) |
| Wander | roams randomly within `wanderRadius` of its spawn |
| Patrol | walks its list of `waypoints` (loop or ping-pong), waiting at each |
| Guard  | holds the spawn point and watches; chases intruders if `aggressive` |
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
and/or left mouse) it hits whatever is inside `range` and the `arc`-degree cone in
front: a Health component, survival health, RPG stats, or an NPC. A weapon from
**View > Editors & Tools > Weapons** replaces those numbers and gives the swing a hit
window, a combo and knockback. See `docs/weapons.md`. Broadcasts `player_attack`.
`Swing()` is exposed for custom inputs/scripts.

## Guns (Ranged Weapon)

*Add Component ▸ Gameplay ▸ Ranged Weapon (gun).* Fires on a rate, spends the rounds
in the gun, and reloads from the spare rounds. The same weapon list supplies the
numbers. An instant shot hurts the first health it reaches. See `docs/weapons.md`.

## Spawner (enemy waves / loot)

*Add Component ▸ Gameplay ▸ Spawner.* Names a `templateName` object to clone (hidden as
a blueprint at play) and spawns copies near itself every `interval` seconds, keeping
at most `maxAlive` of its own spawns alive and stopping after `totalToSpawn`
(0 = endless). Spawns land within `spawnRadius`. `SpawnOne()` and `AliveCount()` are
exposed. Make a "den" that drips out wolves, a chest that refills loot, etc.

## Items, loot tables and recipes

**Window > Items, Loot & Recipes** (also **View > Editors & Tools > Items, Loot & Recipes**) is one window with three tabs. You can list, search, add, duplicate, delete and edit:

- **Items**, saved as the project's `items.okayitems`.
- **Loot tables** (`.okayloot`): each entry has a weight and a quantity range. **Roll** previews a drop with the same roll the game uses.
- **Recipes** (`.okayrecipes`): ingredients, outputs, extra outputs and crafting time, plus the bench tag and the unlock flag the file already stores.

An item name is a dropdown of items you have defined. A name that is not one of them is called out in plain English. Ctrl+Z and Ctrl+Y undo these edits while the window has focus. Double-click a `.okayitems`, `.okayloot` or `.okayrecipes` file in the Project panel to open it here. A Loot Source or a Crafting component can jump to its file.

## Crafting menu (auto UI)

*Add Component ▸ Gameplay ▸ Crafting Menu.* Builds one button per recipe of a sibling
`Crafting` at play (labelled with the output, wired to craft it), and toggles the
whole panel with `toggleKey` (default K). Zero UI wiring — add it next to a `Crafting`
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

## NPC pathfinding

Tick **Pathfinding** on the NPC Controller and it routes around walls,
props and holes with grid A* instead of walking straight lines into them —
follow, chase, patrol, wander and return-home all use it. The route
recomputes every **Repath** seconds (default 0.6) and whenever the target
moves; if no route exists the NPC falls back to straight-line steering.
Works on box/sphere colliders and heightmap terrain (max step 0.45, holes
and cliffs are avoided). A stuck watchdog forces a fresh route whenever the
NPC stops making progress for ~0.8 s (wedged on a corner or another NPC).

Selecting an NPC in the Scene view draws its **perception** too: a
yellow sight-cone arc at eye height (Sight Range + Field Of View) and a
blue circle for Hearing Range, so you can tune a guard's senses visually.
That cone, the optional wall check and the hearing radius are the same
check a behavior tree's Perception service uses, so the two agree when
the numbers match. Each one keeps its own numbers: a new NPC starts with
sight 8, a wide cone, line of sight off and hearing off, and a new
Perception node starts with sight 12, a 120 degree cone, line of sight on
and hearing 4.

### Ordering NPCs around from scripts

`npc_goto("Guard", x, y, z)` sends a named NPC Controller to a world point —
it pathfinds there (when Pathfinding is on), then broadcasts `npc_arrived`
for Action Lists and resumes its base behavior. Combat still wins: a
commanded NPC that spots a threat will chase or flee first and finish the
errand after. Pass `""` as the name to command a sibling NPC Controller.
`npc_stop(name)` cancels the order, `npc_busy(name)` is true while walking,
and `npc_state(name)` returns the live AI state (`Patrol`, `Chase`, …).

## Spawner — enemy and pickup waves

Add Component ▸ Gameplay ▸ **Spawner** turns any object into a wave
spawner with zero scripting. Point **Template** at an enemy or pickup you
built in the scene (it's hidden at Play start and cloned from then on),
or set **Prefab File** to a `.okayprefab`. Copies appear on a flat disc
of **Spawn Radius** around the spawner — shown as a green ring in the
Scene view while selected.

Waves work like you'd expect: **Count Per Wave** objects, one every
**Interval** seconds, then a **Wave Delay** pause; **Waves** limits the
run (0 = endless), **Total** caps lifetime spawns, and **Max Alive**
pauses spawning while that many spawned objects are still alive, so an
endless spawner can't flood the scene. The defaults (count 1, no delay)
keep the classic steady drip. It broadcasts `spawner_spawn`, `spawner_wave` and `spawner_done`
for Action Lists, and scripts can drive it with `spawner_start(name)` /
`spawner_stop(name)` and read `spawner_alive(name)` for a
"3 enemies left" HUD. Turn **Auto Start** off to arm it from a script or
a Trigger Zone instead of at scene start.

Pair it with an NPC Controller template (aggressive + pathfinding) and a
Trigger Zone at the arena door, and you have a wave-defense encounter
without writing a line of code.
