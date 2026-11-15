#pragma once
// Item durability: wearing items down, repairing them, and what a broken item can no longer do.
//
// Which items have durability is set on the item definition (the .okayitems file) through custom fields, so no saved
// format changes:
//   maxDurability   the full value (a number above 0); 0 or missing = the item has no durability
//   breakAction     "keep" (the default: the broken item stays, at 0) or "destroy" (it is removed when it breaks)
// An item's current value is its instance data's `durability`; an item that has never been worn has none stored yet
// and counts as full. Rules: a broken item (durability 0) is refused by InventoryOps::Use ("broken"), and a broken
// worn item gives no armor or defense bonus (Equipment). Scripts get on_item_broken(item) on the inventory's object.
// Game code decides when wear happens (a hit with a weapon, a swing of a tool, damage taken in armor) and calls Wear.
#include "okay/Components/Inventory.hpp"
#include "okay/Inventory/InventoryOps.hpp"
#include <string>
#include <vector>

namespace okay {

class Equipment;
struct ItemDefinition;

namespace durability {

/// The item's full durability from its definition, or 0 when it has none.
float MaxOf(const ItemDefinition* def);
float MaxOf(const std::string& item);
/// True when the item has durability (by definition, or a value already stored on it).
bool Tracked(const std::string& item, const ItemInstanceData& data);
/// The current value: the stored one, else full (the definition's maximum). 0 for an item without durability.
float Current(const std::string& item, const ItemInstanceData& data);
/// True when the item has durability and it is used up (0).
bool Broken(const std::string& item, const ItemInstanceData& data);

/// Wear the item in `slot` down by `amount`. A stack of several is split first so only one wears (refused when there
/// is no free slot for the rest). At 0 the item breaks: on_item_broken fires, and with breakAction "destroy" it is
/// removed. Refused for an empty slot, an item without durability, an amount <= 0, or an item already broken.
InvResult Wear(Inventory* inv, int slot, float amount);
/// Repair the item in `slot` by `amount` (<= 0 = to full), up to its maximum. `cost` (materials) is taken from the
/// same inventory first, all or nothing. Refused for an item without durability, one that is already full, or when
/// the materials are missing. Repairing a broken item makes it usable again.
InvResult Repair(Inventory* inv, int slot, float amount = 0.0f, const std::vector<Inventory::Stack>& cost = {});

/// The same for a worn equipment slot (Equipment::slots index). Breaking or repairing refreshes the bonuses.
InvResult WearEquipped(Equipment* eq, int slot, float amount);
InvResult RepairEquipped(Equipment* eq, int slot, float amount = 0.0f);

} // namespace durability
} // namespace okay
