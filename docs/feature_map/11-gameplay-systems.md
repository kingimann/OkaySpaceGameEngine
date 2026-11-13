# 11. Gameplay systems

The ready-made game-logic kit: inventory and equipment, items, loot and crafting, survival, player building, spawners, melee
and RPG stats. Code is `engine/include/okay/Inventory/*` (8 headers), `engine/src/Inventory/*` (about 2.6k lines) and about 35
components in `Components/`; editor pieces are in `editor/main.cpp`. Docs: `docs/survival_kit.md`, `docs/npcs_and_crafting.md`,
`docs/build_mode.md`; the inventory, item database, loot and equipment have none. ROADMAP.md lists inventory items 6-9 (gameplay,
scripting, UI polish, networking) as open although `inv_*` scripting, pickups and a host-owned inventory are on main: ask first.
No open PR touches this area.

## 11. Gameplay systems

### 11.1 Inventory: bag, grid, chests, equipment
- **What:** Items live in a slot Inventory (hotbar and backpack), a DayZ-style Grid Inventory, chests and crates, and an Equipment component of worn and held slots; all player actions go through one shared set of operations so the player and the editor Play view behave the same.
- **Parts:**
  - 11.1.1 Slot inventory - `Components/Inventory` - stacks with item id, count and per-instance data; capacity, max stack, weight limit and slow-when-overweight; Add, Remove, Count, dry-run fit; keep across scenes
  - 11.1.2 Hotbar and backpack UI - `Components/InventoryUI`, `Inventory/InventoryInput` - number-key hotbar, wheel select, backpack grid, layout, hit-testing, drag state machine, equipment column
  - 11.1.3 Grid bag - `Components/GridInventory`, `Components/GridInventoryUI` - items with a w by h footprint placed on cells, rotation, fit tests, weight, drag between containers
  - 11.1.4 Chests and "the player" lookup - `Components/ChestInventory`, `Components/InventoryPlayer` - open key, nearest-in-range only, take and stash; `FindInventoryPlayer` and the `InventoryInteractable` base shared by chests and benches
  - 11.1.5 Equipment - `Components/Equipment`, `engine/src/Inventory/Equipment.cpp`, `Components/Sockets` - named slots (Head to Off Hand), shown at sockets, main-hand item goes to the hands, armor bonus; `FindSocket` is shared with object holding
  - 11.1.6 Shared operations and events - `Inventory/InventoryOps`, `Inventory/InventoryEvents` - Move, Split, QuickMove, Transfer, Sort, Drop, Use, Craft with all-or-nothing results; `on_item_added` style events to scripts and visual scripts
  - 11.1.7 Persistence - `Inventory/InventoryPersistence` - carry the bag into the next scene; save and load one object's inventories to a small text file
  - 11.1.8 Tooltips and glyphs - `Inventory/ItemPresentation` - item tooltip with compare-to-equipped, coloured fallback tile with initials, duplicate-hotkey warning
- **Code:** `Components/Inventory`, `Components/InventoryUI`, `Components/GridInventory`, `Components/GridInventoryUI`, `Components/ChestInventory`, `Components/InventoryPlayer`, `Components/Equipment`, `Components/Sockets`, `engine/src/Inventory/Equipment.cpp`, `Inventory/InventoryOps`, `Inventory/InventoryInput`, `Inventory/InventoryEvents`, `Inventory/InventoryPersistence`, `Inventory/ItemPresentation`
- **API:** `Inventory`, `InventoryUI`, `GridInventory`, `ChestInventory`, `Equipment`, `InventoryOps::Move`, `InventoryPersistence::SaveObject`, `FindInventoryPlayer`, script `inv_add`, `inv_remove`, `inv_count`, `inv_has`, `inv_move`, `equip`, `on_item_added`, `on_inventory_full`, visual-script inventory nodes
- **Editor:** Add Component > RPG / Turn-Based (Inventory, Equipment, Chest / Container); Inventory UI and Grid Inventory come from the GameObject > UI inventory templates; inspectors with item name pickers backed by the Item Database
- **Docs:** `docs/survival_kit.md` (items feed survival stats only); no inventory doc
- **Tests:** `inventory_core`, `inventory_wiring`, `inventory_ops`, `inventory_ui`, `grid_inventory`, `grid_inventory_fixes`, `inventory_equipment`, `inventory_holding`, `inventory_script`, `inventory_persist_craft`, `vs_inventory`
- **Status:** Works - Inventory.hpp, InventoryOps::Move (checks first, leaves containers unchanged on rejection) and Equipment::EquipFrom (rolls the bag back when there is no room) read; correctness (no dupes, no item loss, stack caps) and wiring covered by the tests above
- **Open:** ROADMAP "Unturned-style inventory improvements" and inventory plan items 6-9 are listed open there (gameplay features, scripting, UI polish, networking) although parts exist (see 11.8); no doc for the inventory system; the old `Inventory` slot model and `GridInventory` are two separate data models
- **Related:** 11.2, 11.3, 11.8, 5.9, 12.2, 8.5, 1.7
- **Words:** inventory, bag, backpack, hotbar, slot, stack, grid inventory, DayZ, Unturned, chest, crate, storage, equipment, armor slot, weight limit, encumbered, InventoryUI, drag and drop items
- **Seen as:** items duplicate or vanish when moved; the bag will not open or opens with the wrong key; a chest opens for the wrong player; a stack exceeds its limit; equipped gear does not appear on the character; the bag empties when the scene changes
- **Ask me like:** "add a backpack with a grid inventory"; "make a chest the player can loot"

### 11.2 Items and loot
- **What:** One project-wide item database (`.okayitems`) defines every item (name, icon, stack size, weight, grid size, rarity, use action, world prefab, custom fields); loot tables (`.okayloot`) roll weighted drops into containers or onto the ground; pickups are items lying in the world.
- **Parts:**
  - 11.2.1 Item database - `Inventory/ItemDatabase`, `engine/src/Inventory/ItemDatabase.cpp` - definitions by stable id, legacy names, loose definitions for unknown names, per-instance data (durability, uid, values), duplicate-id warnings
  - 11.2.2 Loot tables - `Inventory/LootTable` - weighted entries with min, max and chance, several rolls, cached per path
  - 11.2.3 Item pickups - `Components/ItemPickup` - walk into, auto radius or interact key, magnet, takes what fits, drops spawn a pickup so nothing is lost, "hold in hands" items
  - 11.2.4 Loot source - `Components/LootSource` - rolls a table at start, on first chest open or on death; refill timer; repeatable seed
  - 11.2.5 Items, Loot & Recipes window - `editor/ContentEditors.cpp` - Window menu and View > Editors & Tools; items, loot tables (weights, quantity, roll preview) and the same window's recipe tab
- **Code:** `Inventory/ItemDatabase`, `Inventory/LootTable`, `Components/ItemPickup`, `Components/LootSource`, `editor/ContentEditors.cpp`
- **API:** `ItemDatabase::Get`, `ItemDefinition`, `ItemInstanceData`, `EquipSlot`, `LootTable::Roll`, `LootTable::Shares`, `LootTable::Issues`, `ItemPickup`, `LootSource`, `ItemPickup::Spawn`, script `loot_roll`, visual-script Loot Roll, Spawn Pickup
- **Editor:** Window > Items, Loot & Recipes; View > Editors & Tools > Items, Loot & Recipes; Add Component > RPG / Turn-Based > Item Pickup, Loot Source (Edit Loot Table)
- **Docs:** `docs/asset_database.md` (lists the extensions only); `docs/editor.md`
- **Tests:** `item_database`, `item_loot_recipes`, `inventory_gameplay`, `inventory_build_charge`, `vs_inventory`
- **Status:** Works - ItemDatabase.hpp, LootTable.hpp and ItemPickup.hpp read; item definitions, pickups, drops, loot rolls, file round trip and roll distribution covered by `item_database`, `item_loot_recipes` and `inventory_gameplay`
- **Open:** no separate doc page that spells the `.okayitems` or `.okayloot` text format out line by line; unknown item names become "loose" definitions that are never saved until Define it is used
- **Related:** 11.1, 11.3, 20, 9.5, 5.9
- **Words:** item database, .okayitems, item definition, rarity, stack size, loot table, .okayloot, drop, pickup, ItemPickup, LootSource, loot chest, durability, item uid, legacy item names
- **Seen as:** an item shows as plain text with no icon; a renamed item disappears from old saves; enemies drop nothing; a pickup cannot be picked up; loot rolls the same every time or differs between runs
- **Ask me like:** "create a rarity system with colored borders"; "make wolves drop meat 50 percent of the time"

### 11.3 Crafting and recipes
- **What:** Recipes turn input items into outputs (with byproducts, craft time, bench tags and unlock flags); a Crafting menu on the player, a Crafting Menu that builds its own buttons, and Crafting Station benches all share one recipe system and a craft queue.
- **Parts:**
  - 11.3.1 Recipes and books - `Inventory/Recipe` - `Recipe`, `RecipeBook` (`.okayrecipes` text file), unlock by game variable
  - 11.3.2 Craft queue - `Inventory/Recipe` - `CraftQueue`: inputs taken when queued, outputs when done, overflow dropped next to the crafter, cancel refunds
  - 11.3.3 Crafting component and auto menu - `Components/Crafting`, `Components/CraftingMenu` - recipes over a sibling Inventory, one button per recipe, toggle key
  - 11.3.4 Crafting station - `Components/CraftingStation` - bench with its own panel, station tag, shift-click to craft many, nearest-in-range only
- **Code:** `Inventory/Recipe`, `Components/Crafting`, `Components/CraftingMenu`, `Components/CraftingStation`, `editor/main.cpp` (crafting inspectors)
- **API:** `Recipe`, `Ingredient`, `RecipeBook`, `CraftQueue`, `Crafting::Craft`, `CraftingStation`, script `craft`, visual-script Craft, `on_craft`, `on_craft_failed`
- **Editor:** Window > Items, Loot & Recipes (Recipes tab) edits `.okayrecipes` (inputs, outputs, crafting time, bench, unlock); Add Component > Gameplay > Crafting, Crafting Menu; RPG / Turn-Based > Crafting Station (Edit Recipe Book); recipe rows in the inspector; optional `.okayrecipes` picker
- **Docs:** `docs/npcs_and_crafting.md`, `docs/asset_database.md` (lists the `.okayrecipes` extension only), `docs/editor.md`
- **Tests:** `inventory_persist_craft`, `item_loot_recipes`, `npc_crafting`, `vs_inventory`
- **Status:** Works - Recipe.hpp and the CraftQueue functions in Recipe.cpp read; queue, bench tags, unlocks, craft counts and the recipe file round trip covered by `inventory_persist_craft` and `item_loot_recipes`
- **Open:** `docs/npcs_and_crafting.md` predates the shared recipe book and craft queue and does not describe them
- **Related:** 11.1, 11.2, 11.5, 9.5
- **Words:** crafting, recipe, .okayrecipes, workbench, crafting station, craft time, byproduct, craft queue, ingredients, CraftingMenu, unlock recipe
- **Seen as:** crafting takes items but gives nothing; the same button appears twice after loading; a bench opens for the wrong player; craft time never finishes
- **Ask me like:** "add a workbench that needs 4 planks"; "unlock a recipe after a quest"

### 11.4 Survival: stats, afflictions, zones
- **What:** Native drop-in stat components (health, hunger, thirst, stamina, oxygen, temperature, sleep, sanity), damage-over-time afflictions, carry weight, timed status effects, zones that drive them from the world, and consumable items that feed them.
- **Parts:**
  - 11.4.1 All-in-one Survival Stats - `Components/SurvivalStats` - drains stats, damages health when hunger, thirst, oxygen or warmth hit zero, armor, regen, death
  - 11.4.2 Individual stat components - `Components/SurvivalComponents` - `HealthStat` (armor, resistance, i-frames, overheal, respawn, lives) and Hunger, Thirst, Stamina, Oxygen, Temperature, Sleep, Sanity stats on a shared `StatComponent`
  - 11.4.3 Afflictions - `Components/SurvivalAfflictions` - Radiation, Bleeding, Poison, Wetness that fill toward harm and damage the health source
  - 11.4.4 Systems - `Components/SurvivalSystems` - Carry Weight stat, generic timed Status Effects (`Apply`, `Remove`), Survival Save and Load
  - 11.4.5 Zones - `Components/SurvivalZone` - trigger volumes that drive radiation, water, cold, fire and similar inputs
  - 11.4.6 Consumables - `Components/Consumables` - maps item names to Eat, Drink, Heal and similar; works from `Use(item)` or by dropping a world item on the player
  - 11.4.7 Death lock and speed scale - `Components/DeathLock`, `Components/MoveSpeed` - a dead body does not read move input; one multiplier for load, holding and overweight that controllers apply to their speed
- **Code:** `Components/SurvivalStats`, `Components/SurvivalComponents`, `Components/SurvivalAfflictions`, `Components/SurvivalSystems`, `Components/SurvivalZone`, `Components/Consumables`, `Components/DeathLock`, `Components/MoveSpeed`, `editor/main.cpp` (Stat inspectors and Add Component rows)
- **API:** `SurvivalStats`, `HealthStat`, `HungerStat`, `StaminaStat`, `StatusEffectStat`, `CarryWeightStat`, `SurvivalSave`, `SurvivalZone`, `Consumables`, `MoveSpeedScale`, `DeathLocksInput`, messages `died`, `starving`, `dehydrated`, script `Stats` and `SurvivalStats` access
- **Editor:** Add Component > Gameplay > Survival Stats and Stat rows; UI buttons can call Eat, Drink and Heal through On Click (native UI actions)
- **Docs:** `docs/survival_kit.md`
- **Tests:** `survival`
- **Status:** Works - SurvivalComponents.hpp (HealthStat read in detail), SurvivalSystems.hpp and the docs read; drain, damage, armor, death and save round trip covered by the 728-line `survival` test
- **Open:** three separate health models exist: HealthStat, SurvivalStats' own health and the older RPG `Stats` (11.9) plus `NPCController`'s own `health` (9.5) (possible duplication, not unified)
- **Related:** 11.1, 11.7, 9.5, 6.6, 12.2, 5.8
- **Words:** survival, health, HealthStat, hunger, thirst, stamina, oxygen, temperature, sanity, radiation, bleeding, poison, wetness, status effect, buff, debuff, damage over time, respawn, lives, consumable
- **Seen as:** the player never gets hungry or never dies; health does not regenerate; the stamina bar stays full; a dead player can still move; eating an item does nothing; the bar does not update
- **Ask me like:** "add hunger and thirst that hurt you at zero"; "poison that ticks for 10 seconds"

### 11.5 Building: build mode, structure builder, snapping
- **What:** Several ways to build: a Build Mode players use in your game (palette, snapping, undo, logic links, saved creations), a Rust-style tiered Structure Builder, a Minecraft-style Block Builder, and a developer Builder Mode for modelling while walking.
- **Parts:**
  - 11.5.1 Build Mode - `Components/BuildMode`, `Components/Buildable` - palette hotbar, grid snap, rotate, move, copy, delete, undo and redo, reach and object caps, `BuildCreation` save and load to a creations file
  - 11.5.2 Logic links - `Components/LogicLinks` - Logic Source (button, pressure plate, trigger zone, timer) wired to Logic Targets (door, mover, light, spawner, event) by Buildable id; I/O links on a source (named output to target object, input, parameter, delay, fire-once, enabled; Relay kind; Fire Output action and script call)
  - 11.5.3 Structure Builder - `Components/StructureBuilder`, `Components/BuildPiece` - foundations, walls, floors, pillars and ramps that snap to each other, wood-stone-metal upgrades, repair, cost from the inventory
  - 11.5.4 Block Builder - `Components/BlockBuilder` - place and remove grid cubes by aiming at surfaces
  - 11.5.5 Builder Mode - `Components/BuilderMode` - developer tool: drop primitives or stamp prefabs, rotate and scale, save the assembly as a `.okayprefab`
- **Code:** `Components/BuildMode`, `Components/Buildable`, `Components/LogicLinks`, `Components/StructureBuilder`, `Components/BuildPiece`, `Components/BlockBuilder`, `Components/BuilderMode`, `editor/main.cpp` (build inspectors)
- **API:** `BuildMode`, `Buildable`, `BuildPaletteItem`, `BuildCreation`, `LogicSource`, `LogicTarget`, `StructureBuilder`, `BuildPiece`, `BlockBuilder`, `BuilderMode`, script `build_enter`, `build_exit`, `build_toggle`, `on_object_placed`, `fire_output`, Actions step `fire_output`, `LogicSource::IoLink`, `FireOutput`
- **Editor:** Add Component > Gameplay rows "Build Mode (players build + wire)", "Buildable (players may edit)", Logic Source (with an I/O Links list: Add, Test, Remove, broken-target warning), Logic Target, "Block Builder (voxel)", "Structure Builder (Rust-style)"; Builder Mode is toggled in Play from the editor
- **Docs:** `docs/build_mode.md`
- **Tests:** `build_mode`, `io_links`, `structure_builder`, `block_builder`, `builder_mode`, `inventory_build_charge`
- **Status:** Works - BuildMode.hpp, Buildable.hpp, LogicLinks.hpp and the other builder headers read; placement, undo, links and charging for pieces covered by `build_mode`, `structure_builder` and `inventory_build_charge`
- **Open:** per `docs/build_mode.md` "Not covered yet": placements and links are not synced between players, building is 3D only, pressure plates need a collider; Build Mode and Builder Mode are different features with confusingly similar names
- **Related:** 6.6, 11.1, 11.3, 12.8, 16.8, 2.6
- **Words:** build mode, building, place objects, Buildable, logic link, button door, pressure plate, creation, structure builder, Rust building, foundation, wall, block builder, Minecraft, Builder Mode, F2, snap to grid
- **Seen as:** a piece will not place (red ghost); players can delete the level floor; a link stops working after loading; a built structure floats; pieces do not snap together
- **Ask me like:** "let players build walls and wire a button to a door"; "add upgradeable wood to stone walls"

### 11.6 Spawners, turn manager, minimap
- **What:** A Spawner drips copies of a template or prefab (enemy waves, loot); a Turn Manager holds turn order for turn-based games; a Minimap panel plots tagged objects around a target.
- **Parts:**
  - 11.6.1 Spawner - `Components/Spawner` - interval, max alive, total cap, spawn radius, waves, start and stop, live count, `spawner_wave` message
  - 11.6.2 Turn Manager - `Components/TurnManager` - ordered participant names, `EndTurn`, rounds, calls `on_turn_start` and `on_round_start` on scripts
  - 11.6.3 Minimap - `Components/Minimap` - bordered box or circle, zoom, heading-up rotation, player arrow, off-map edge pointers, grid; `MinimapBlip` marks plotted objects
- **Code:** `Components/Spawner`, `Components/TurnManager`, `Components/Minimap`, `editor/main.cpp` (Spawner, Turn Manager, Minimap rows)
- **API:** `Spawner`, `SpawnOne`, `AliveCount`, `StartWaves`, `TurnManager`, `EndTurn`, `Minimap`, `MinimapBlip`, script `spawner_start`, `spawner_stop`, `spawner_alive`
- **Editor:** Add Component > Gameplay > Spawner; RPG / Turn-Based > Turn Manager; UI > Minimap and Minimap Blip
- **Docs:** `docs/npcs_and_crafting.md` (Spawner only)
- **Tests:** `rpg`, `minimap`, `npc_crafting`
- **Status:** Works - Spawner.hpp, TurnManager.hpp and Minimap.hpp read; covered by `rpg` (turn order), `minimap` and `npc_crafting`
- **Open:** no docs for Turn Manager or Minimap; Spawner has no object pooling hook (see pooling in area 1.4); Minimap is 2D top-down only
- **Related:** 9.5, 12.8, 1.4, 16.8
- **Words:** spawner, enemy waves, wave spawner, spawn point, turn manager, turn-based, initiative, round, minimap, radar, blip, MinimapBlip
- **Seen as:** nothing spawns; too many enemies pile up; the template object is visible in the scene; turns skip a unit; minimap shows nothing or the wrong area
- **Ask me like:** "spawn 5 zombies every 10 seconds near the gate"; "add a circular minimap that rotates with the player"

### 11.7 Combat actions: melee, dodge, dive
- **What:** Player melee swings that hit NPCs in an arc, plus a dodge roll and a dolphin dive that the first and third person controllers can use.
- **Parts:**
  - 11.7.1 Melee attacker - `Components/MeleeAttacker` - attack key or left mouse, range, arc, damage, cooldown, `Swing()`, `player_attack` message
  - 11.7.2 Dodge - `Components/DodgeAction` - opt-in dodge impulse with its own key and stamina cost (`canDodge` off by default)
  - 11.7.3 Dive - `Components/DiveAction` - sprint then prone dive with a landing slide; called from a controller's Update
- **Code:** `Components/MeleeAttacker`, `Components/DodgeAction`, `Components/DiveAction`, `editor/main.cpp` (Melee Attacker inspector)
- **API:** `MeleeAttacker`, `MeleeAttacker::Swing`, `DodgeMove`, `DiveMotion`, `DiveTune`, script `dodge()`, Action List message `player_attack`
- **Editor:** Add Component > Gameplay > Melee Attacker
- **Docs:** `docs/npcs_and_crafting.md` (Melee Attacker)
- **Tests:** `npc_crafting`
- **Status:** Works for melee (MeleeAttacker.hpp read; covered by `npc_crafting`); Basic for dodge and dive (headers read; used by the player controllers in area 4, no dedicated test file found)
- **Open:** melee hits NPCController targets only per its header; no hit detection on arbitrary HealthStat objects, no weapon definitions tied to items, no ranged weapon component besides Projectile3D (6.6) and the shooter controllers (4.4)
- **Related:** 9.5, 6.6, 4.2, 4.4, 11.4, 8.6
- **Words:** melee, attack, sword swing, MeleeAttacker, hit arc, cooldown, dodge, roll, dive, dolphin dive, combat, weapon, player_attack
- **Seen as:** swinging hits nothing; hits through walls; dodge does nothing because it is off by default; the player dives when it should slide
- **Ask me like:** "add a sword that damages enemies in front of me"; "turn on a dodge roll with stamina cost"

### 11.8 Server-authoritative inventory
- **What:** For multiplayer, the host owns inventory contents: clients send requests, the host validates them, applies them through the shared operations and answers with a versioned snapshot, so a client cannot grant itself items.
- **Parts:**
  - 11.8.1 Request handling - `Components/NetInventory`, `engine/src/Inventory/NetInventory.cpp` - notice calls for move, split, quick move, transfer, drop, use, grid move, equip, craft; rate limit and size limit per request
  - 11.8.2 Visibility and persistence - `Components/NetInventory` - world containers hide their contents from clients that are not looking; host saves a player bag per user id; optional backend save hook
  - 11.8.3 Cheat reporting - `Components/NetInventory` - invalid requests count toward an On Cheat Detected event with detail "inventory"
- **Code:** `Components/NetInventory`, `engine/src/Inventory/NetInventory.cpp`
- **API:** `NetInventory`, `NetInventory::NoticeMove`, `NetInventory::DeferPickup`, `NetInventory::SetBackendSave`, `NetInventory::GrantLog`
- **Editor:** Add Component > RPG / Turn-Based > "Net Inventory (host-owned)"
- **Docs:** -
- **Tests:** -
- **Status:** Basic - header and the file structure read; no test file references NetInventory (checked by search), so behaviour is unverified
- **Open:** no tests or docs; depends on the anti-cheat RPC checks (16.6); interaction with ROADMAP inventory item 9 (networking) status unknown: ask before assuming
- **Related:** 11.1, 16.6, 16.2, 16.3
- **Words:** NetInventory, multiplayer inventory, host-owned inventory, inventory sync, cheat detected, server inventory, inv/ requests, okinv
- **Seen as:** a client duplicates items online; the chest shows items to players who are not near; the bag is not saved per user on the server
- **Ask me like:** "make the inventory server-authoritative"; "add a test for NetInventory"

### 11.9 RPG stats and XP
- **What:** A simple RPG character sheet: health, mana, level and XP with attributes, damage mitigated by defense, healing and levelling that fire script events.
- **Parts:**
  - 11.9.1 Health, mana and attributes - `Components/Stats` - health and mana pools, strength, defense, intelligence, agility; `TakeDamage` mitigated by defense, `Heal`
  - 11.9.2 Levelling and events - `Components/Stats` - `AddXP` raises level, max health, max mana and attributes; calls `on_death` and `on_level_up` on the sibling script
- **Code:** `Components/Stats`, `editor/main.cpp` (Stats inspector)
- **API:** `Stats`, `TakeDamage`, `Heal`, `AddXP`, script `on_death`, `on_level_up`
- **Editor:** Add Component > RPG / Turn-Based > Stats
- **Docs:** -
- **Tests:** `rpg`
- **Status:** Works - Stats.hpp read in full; covered by `rpg` (damage, heal, XP levelling)
- **Open:** a fourth health model next to HealthStat, SurvivalStats and NPCController (possible duplication, see 11.4); no skill tree, buff stacking or save wiring
- **Related:** 11.4, 11.6, 9.5
- **Words:** RPG, stats, health, mana, level up, XP, experience, attributes, strength, defense, Stats component
- **Seen as:** damage ignores armor; the character never levels; on_level_up never fires
- **Ask me like:** "give the player XP for kills and level up"; "unify Stats and HealthStat"

### 11.10 No-code behaviours: move, spin, lifetime
- **What:** Three tiny components that need no script: Mover (constant velocity), Spinner (constant rotation) and Lifetime (destroy after N seconds).
- **Parts:**
  - 11.10.1 Mover - `Components/Mover` - world velocity per second
  - 11.10.2 Spinner - `Components/Spinner` - degrees per second on each axis
  - 11.10.3 Lifetime - `Components/Lifetime` - destroy the object after `seconds`; `Elapsed`, `Remaining`
- **Code:** `Components/Mover`, `Components/Spinner`, `Components/Lifetime`
- **API:** `Mover`, `Spinner`, `Lifetime`, `velocity`, `angularVelocity`, `seconds`
- **Editor:** Add Component > Gameplay > Mover, Spinner, Lifetime
- **Docs:** `docs/making_a_game.md` (one-line mention)
- **Tests:** `gameplay`
- **Status:** Works - all three headers read in full (each is under 30 lines); covered by `gameplay` including serialization
- **Open:** the Lifetime header says it counts from when the scene starts running (ambiguous for objects spawned later; not checked); no easing or path motion (see area 4.8 dolly paths)
- **Related:** 6.6, 1.4, 14.6
- **Words:** Mover, Spinner, Lifetime, rotate forever, move constantly, auto destroy, no-code motion, spin pickup
- **Seen as:** an object spins or slides forever; a bullet never disappears; an effect object lingers
- **Ask me like:** "make coins spin"; "destroy the explosion after 2 seconds"

### 11.11 World drag and drop
- **What:** Lets players drag world-space sprites with the mouse and drop them on drop zones, with snap and tag filtering and script callbacks.
- **Parts:**
  - 11.11.1 Draggable - `Components/Draggable` - press and drag a sprite in orthographic space, `on_drag_start`, `on_drag`, `on_drop` callbacks, optional snap to zone and grid
  - 11.11.2 Drop zone - `Components/Draggable` - `DropZone` receives drops, `on_receive`, tag filter
- **Code:** `Components/Draggable`
- **API:** `Draggable`, `DropZone`, `snapToZone`, `anyTarget`, script `on_drag_start`, `on_drop`, `on_receive`
- **Editor:** Add Component rows "Draggable (item)" and "Drop Zone (item)"
- **Docs:** -
- **Tests:** `item_dragdrop`
- **Status:** Works - Draggable.hpp read; covered by `item_dragdrop` (drop on zone, snap, tag filter, grid snap)
- **Open:** no doc; world-space only (the UI version, `UIDraggable`, is area 12); 2D orthographic camera only
- **Related:** 12.4, 11.1, 8.4
- **Words:** drag and drop, Draggable, DropZone, drag items, puzzle, snap to zone, on_drop
- **Seen as:** an item cannot be dragged; it drops but nothing receives it; the item does not snap to the zone
- **Ask me like:** "drag ingredients onto a cauldron"; "snap puzzle pieces to a grid"

### 11.12 Volumes and zones
- **What:** One region shape (box, sphere or cylinder, no collider needed) with six zone kinds: checkpoint and kill (a respawn point per body), force (wind, updraft, current), atmosphere (fog and vignette that fade in around the camera), ambience (a sound that fades in around the camera) and reverb (a room, hall, cave or outdoor echo on 3D sounds, blended at the edges). Drawn as a coloured translucent shape in the Scene view, drawn and resized by dragging.
- **Parts:**
  - 11.12.1 Shape - `Components/Volume` - `VolumeGeometry`: box, sphere (ellipsoid) and cylinder from one `size` and `offset`; `Contains`, `DepthAt`, `DragFace` (move one face, the opposite stays), `FromGroundDrag`
  - 11.12.2 Checkpoint and Kill - `Components/Volume` - a body inside a Checkpoint takes the floor middle as its respawn point; a body entering a Kill takes `killDamage` and goes back to its checkpoint (or where it was first seen), or is destroyed; optional On Logic Signal `eventName`
  - 11.12.3 Force - `Components/Volume` - pushes dynamic Rigidbody 3D bodies each physics step: one direction (turning with the object or fixed to the world) or radial, in m/s squared
  - 11.12.4 Atmosphere and Ambience - `Components/Volume` - while the camera is inside, the scene fog and vignette blend to the volume's (several volumes blend, the scene's own values come back on exit); an Audio Source on the object fades in and out
  - 11.12.7 Reverb - `Components/Volume`, `Audio/SpatialFx` - while the listener is inside, 3D sources are sent to the Schroeder reverb at the zone's preset (room, hall, cave, outdoor) and strength; `blend` fades that from the edge; overlapping zones mix; 2D sources are not sent
  - 11.12.5 Scene view tooling - `editor/VolumeTools.*` - translucent shape and label per volume in a colour per kind, six face handles on the selection, GameObject > Volume (draw by dragging on the ground, or create at the view), Scene Overlays > Volumes (show or hide by kind), Inspector, Add Component rows; Logic Source plates and trigger zones get the same shape, handles and switch
  - 11.12.6 Save and checks - `Scene/SceneSerializer`, `Testing/SceneValidator` - `volume` scene and prefab record (several per object), property registry entries, validator codes `volume.zero_size`, `volume.force_zero`, `volume.no_audio`
- **Code:** `Components/Volume`, `editor/VolumeTools.*`, `Scene/SceneSerializer`, `Scene/BuiltinPropertiesGameplay`, `Testing/SceneValidator`
- **API:** `Volume`, `Volume::Kind`, `VolumeGeometry`, `Volume::RespawnPoint`, `Volume::SetRespawnPoint`, `Volume::Occupants`, `Volume::ViewerWeight`
- **Editor:** GameObject > Volume; Scene Overlays > Volumes; Add Component > Gameplay > Volume rows; Inspector "Volume (zone)"
- **Docs:** `docs/volumes.md`
- **Tests:** `volumes`, `spatial_audio`, `save_coverage`, `property_registry`
- **Status:** Works - all six kinds act in Play and are covered by the `volumes` test (shapes, handle math, respawn, force, fog and sound blend, reverb weight and tail, scene and prefab round trip, validator) and `spatial_audio` (presets, edge blend, the demo scene); the Scene view tools were driven in the real editor
- **Open:** no per-scene post settings beyond fog and vignette, no navigation-area volume, no convex region shape; Force reaches Rigidbody 3D bodies only (not 2D, and a character controller that sets its own velocity each frame overrides it); Atmosphere overrides weather fog while the camera is inside; a Kill needs the body to have been seen outside the volume to know where to send it; reverb follows the listener only (a sound does not carry the room it is standing in once you have left)
- **Related:** 6.2, 11.4, 11.6, 11.5, 2.4, 7.1, 15.3, 44.4
- **Words:** volume, zone, trigger volume, checkpoint, respawn, kill zone, kill plane, out of bounds, wind zone, force field, updraft, current, fog volume, ambient sound zone, reverb zone, room echo, region, box, sphere, cylinder, draw volume
- **Seen as:** cannot see where my zones are; the player falls into lava and stays dead; no checkpoint between rooms; need a wind tunnel or a fan; fog should change in a cave; a room needs its own background sound
- **Ask me like:** "add a kill zone under the bridge that sends the player to the last checkpoint"; "a wind tunnel that blows crates along"; "fog that thickens when I enter the swamp"
