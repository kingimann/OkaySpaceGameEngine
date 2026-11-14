# Inventory: what exists, what is missing

A short, honest status of survival-game inventory in the engine. The to-do item
"Improve inventory systems (survival-game style)" is **partly done**: the move,
stack, weight and equipment logic is solid and tested; the gaps are listed under
"Not done".

## What exists

| Area | Where | State |
| --- | --- | --- |
| Slot bag with stacks | `Components/Inventory` | Stacks by item id and instance data, capacity, per-bag and per-item stack limits, fixed-position slots (holes), add / remove / dry-run `CanAdd`, `Exchange` (all-or-nothing craft/trade) |
| Grid bag (DayZ / Unturned) | `Components/GridInventory` | w x h footprints, rotation, fit tests, stack merge, `Validate()` repair with `overflow` |
| Moving, splitting, merging, swapping | `Inventory/InventoryOps` | `Move` (one bag or two), `Split`, `QuickMove` (hotbar <-> backpack), `Transfer`, `TransferAll`, `TransferMatching`, `SortByName`, `Drop`, `Use`, `Craft`, grid `Move` / `Split`, and now slot <-> grid `ToGrid` / `FromGrid` |
| Hotbar | `InventoryUI` | The first slots of a slot bag; `QuickMove` shifts between them and the rest |
| Equipment slots | `Components/Equipment` | Head, Chest, Legs, Feet, Hands, Back, Main/Off hand; armor and defense bonuses; worn weight counts toward the bag; worn backpacks |
| Durability, unique items | `ItemInstanceData` | `durability`, `uid` (never stacks, never splits), free key/values; carried by every move |
| Weight limits | `Inventory`, `GridInventory` | Slow-when-overweight on both; **enforced** (refuses or trims adds and moves) on grids always, and on slot bags with `enforceWeight` (new) |
| Drag and drop | `InventoryUI`, `GridInventoryUI`, `InventoryInput` | Mouse and gamepad, split-half, drag between chest / bags / grids; all through `InventoryOps` |
| Loot containers | `ChestInventory`, `LootSource`, `LootTable` | Chest screen, loot tables, refill rules |
| Persistence | `InventoryPersistence`, `SceneSerializer` | Scene files, `.okinv` player-bag files, `keepOnSceneLoad` |
| Multiplayer | `NetInventory` | Host-authoritative; the host validates every `inv/*` request and sends versioned snapshots |

## What changed in this pass

Found by reading `InventoryOps` and attacking it with bad input:

* A negative `amount` meant "the whole stack". Now rejected ("bad amount"). `0` still means the whole stack.
* A huge destination index on a bag with no capacity (`capacity = 0`) grew the slot list to that size (a memory attack through the network `inv/move` request). Every move, quick-move and split is now limited to 4096 slots.
* A move into an empty slot, and a swap, ignored the destination bag's stack size, so a 10-stack could land in a bag that allows 5. The move is now trimmed (Partial) or the swap refused.
* Moving or splitting part of a unique (`uid`) stack copied the `uid` onto both halves. Unique items now only move whole.
* `GridInventory::CanPlace` added `x + w` in `int`; coordinates near `INT_MAX` wrapped. Now computed in 64 bits.
* New: `Inventory::enforceWeight` (default off, saved as one appended number; older saves load with it off). With it on, `Add`, `AddPartial`, `CanAdd`, crafting results, `Transfer` and `Move` into that bag never take it over `weightLimit`: adds fail or are trimmed, swaps compare the net weight change both ways.
* New: `InventoryOps::ToGrid` and `FromGrid` move stacks between a hotbar/slot bag and a grid bag. Footprint and weight come from the item definition, instance data travels with the stack, and the destination grid's own rules decide bounds, overlap, merging and weight. Never a swap.

Tests: `tests/test_inventory_moves.cpp` (hostile numbers, same-slot, stack and weight limits, unique items, slot <-> grid, old-format loading).

## Not done

* **Networked slot <-> grid moves.** `ToGrid` / `FromGrid` are local operations: they send no `inv/*` request. A client must not use them against a host-owned bag until a request for them exists in `NetInventory`.
* **Scripts and visual scripts** cannot call `ToGrid` / `FromGrid`. Scripts already have `inv_move`; a script function and a visual-script node ("Move Item To Grid" with slot, grid, x, y, rotated, amount) are still to be added (the visual-script core is owned by another change, so no node was added here).
* **Worn weight and enforced weight.** `Equipment::Unequip` into a bag that enforces its weight limit is checked while the worn item is still counted in `wornWeight`, so it can be refused a little early near the limit.
* **Durability use.** Durability is stored and carried, but nothing wears it down (tools, weapons, armor) or repairs it; no "broken item" rule.
* **Container-in-container rules, locked and owned containers, item cooldowns, hotbar-bound ability slots, and a "stack count" prompt in the drag UI** are not built.
* **Slot bag weight in the UI** shows a bar only where the screen already did; no per-item "too heavy" message beyond the result reason (`InvResult::reason`).
* **Multiplayer sync** of `enforceWeight` as a setting is just a scene value; nothing syncs it live.
