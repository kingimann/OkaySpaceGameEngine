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

## Ported from Grok's closed inventory PRs 10-12

Grok's three PRs were closed unmerged and conflicted with main. A review found most of them already in main (builds charged in full, name case folding, the `GameInputBlocked` modal guard, the use-key claim, chest scroll, hotbar scaling, icon fallback and atlas icons). What was missing was re-applied against current code:

* **Top-down fire** no longer shoots through an open bag, a focused text field, a menu or a click on a button (`TopDownController2D` now asks `GameInputBlocked`).
* **Lean steps aside.** In the first-person, third-person and third-person-shooter controllers, a lean key is ignored when a chest, bench or pickup wants it as its use key (`InteractClaimsKey`), or on the press that opens or closes a bag whose toggle key is the same (`LeanKeyTaken`). Q/E clash by default (bag key `E`, lean right `E`).
* **One scale for chest and grid bags.** They scale with the screen height like the hotbar (720 = authored size, or the player UI's reference height) and shrink to fit the width. A grid bag taller than the screen scrolls with the wheel (`GridInventoryUI::scroll`). An open backpack taller than the screen scrolls too (`InventoryUI::scroll`). While the backpack is open the wheel scrolls it instead of changing the hotbar slot.

Left out on purpose: the `InputArbiter` class and `Interactor` component (built on the old branch's own action system, which main does not have; main's `GameInput` already answers who owns a key; a new saved component for it was not worth the file-format risk), the cached modal query (main reads the scene directly), and the player/editor drawing changes (main's drawing was reworked since).

Tests: `tests/test_ui_input_guard.cpp` (lean, top-down fire), `tests/test_inventory_ops.cpp` (layout scale, fit, scroll).

## Not done

* **Networked slot <-> grid moves.** `ToGrid` / `FromGrid` are local operations: they send no `inv/*` request. A client must not use them against a host-owned bag until a request for them exists in `NetInventory`.
* ~~**Scripts and visual scripts** cannot call `ToGrid` / `FromGrid`.~~ Done: `inv_to_grid(slot, x, y, [rotated], [amount = all], [grid object])` and `inv_from_grid(index, [slot = a matching stack, else the first empty one], [amount = all], [grid object])` move between this object's slot bag and its own or another object's grid bag; each returns how many moved (0 when refused). Visual scripts get them as `Fn.inv_to_grid` / `Fn.inv_from_grid`. Like `ToGrid` / `FromGrid` they are local moves: a client must not use them on a host-owned bag (see the networked bullet above). Test `inv_grid_script`.
* ~~**Worn weight and enforced weight.**~~ Not a bug: checking showed `Equipment::Unequip` never tests the weight (it places the item directly), which is right, because a worn item already counts in the bag's weight and taking it off leaves the carried total the same. Test `unequip_weight` holds this at 9, 10 and 11 of 10.
* **Durability: the rules exist; wiring them to gameplay does not.** `okay/Inventory/Durability.hpp` (test `item_durability`):
  - **Settings**, on the item definition's custom fields (no format change): `maxDurability` (a number above 0; missing or 0 = the item has no durability) and `breakAction` (`keep`, the default, or `destroy`). An item never worn has no stored value and counts as full.
  - **Only the definition decides.** An item whose definition has no `maxDurability` is never tracked or broken, even when an older scene or save stores a durability (also 0) on it.
  - **Events:** every change (wear, break, repair; in a bag or worn) fires `on_durability_changed(item, value)`; a break also fires `on_item_broken(item)`.
  - **Multiplayer (NetInventory):** the host decides. On a connected client, wear and repair are refused with "the host decides"; the host's change reaches the clients with the next inventory snapshot, which carries the durability. Wear from combat therefore happens where the hit is decided (the host).
  - **`durability::Wear(inv, slot, amount)`** wears one item down (a stack is split first so only one wears; refused when there is no free slot). At 0 the item is broken: scripts get `on_item_broken(item)`, and with `breakAction destroy` it is removed.
  - **`durability::Repair(inv, slot, amount, cost)`** repairs by an amount or to full (`amount` 0). The `cost` materials are taken from the same bag all or nothing, and never from the item being repaired (another of the same item can pay).
  - **Worn pieces:** `WearEquipped` / `RepairEquipped` on an `Equipment` slot.
  - **Armor wears when hit:** a worn piece whose definition has a custom `wearPerHit` loses that much each time a hit lands on the wearer's Health Stat while worn armor counts (`durability::WearArmorOnHit`). Without `wearPerHit` a piece never wears on its own (the default, so existing games are unchanged). Only Health Stat hits do this, not Stats or Survival Stats damage.
  - **Weapons wear when they strike:** the worn Main Hand piece loses its custom `wearPerStrike` once for each melee swing that lands (MeleeAttacker; a miss does not wear it, and one swing hitting three enemies wears it once). Each ranged shot wears it by `wearPerShot` (RangedWeapon). A broken Main Hand weapon is not used as a weapon (`WeaponForHolder` returns none, for melee and ranged) until it is repaired.
  - **Broken items:** `InventoryOps::Use` refuses them (reason "broken"), and a broken worn piece gives no armor or defense bonus until repaired.
  - **Scripts:** `inv_durability(slot, [bag])` (the current value, -1 when the item has none), `inv_broken(slot, [bag])`, `inv_wear(slot, amount, [bag])` and `inv_repair(slot, [amount, 0 = full], [bag])`; visual scripts get the same as `Fn.inv_*` nodes. `bag` is another object's name (a chest); without it, this object's inventory.
  - **Not done:** no sample scene yet (it needs an `.okayitems` file with the custom fields) and no screenshot, because there is no durability bar to show; tools (harvesting, building) do not wear on their own yet (game code or a script calls `Wear`); script repair takes no materials (call `inv_remove` first); the UI shows the number (`Durability 40`) but no bar or "broken" tint; a repair station component.
* **Container-in-container rules, locked and owned containers, hotbar-bound ability slots, and a "stack count" prompt in the drag UI** are not built. (Locked containers need a saved field on the chest.)
* **Item cooldowns: done.** An item definition's custom `cooldown` (seconds of game time) makes `InventoryOps::Use` refuse that item from the same inventory (reason "cooldown") until the time has passed. Each bag keeps its own timers, keyed by the bag object's id; they are runtime only (not saved, so a reload resets them) and pause with the game clock. `InventoryOps::CooldownLeft(inv, item)`, `ResetCooldowns()`, script `inv_cooldown(item, [bag])` (seconds left, also a visual-script node). Not networked: each machine times its own uses. Test `item_cooldowns`.
* **Slot bag weight in the UI** shows a bar only where the screen already did; no per-item "too heavy" message beyond the result reason (`InvResult::reason`).
* **Multiplayer sync** of `enforceWeight` as a setting is just a scene value; nothing syncs it live.
