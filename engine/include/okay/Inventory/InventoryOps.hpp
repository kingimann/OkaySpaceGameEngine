#pragma once
#include "okay/Components/Inventory.hpp"
#include "okay/Components/GridInventory.hpp"
#include "okay/Math/Vec3.hpp"
#include <string>
#include <vector>

namespace okay {

class GameObject;
class Scene;

/// What an inventory operation did. `moved` counts items that changed place.
struct InvResult {
    enum class Status { Ok, Partial, Rejected };
    Status status = Status::Rejected;
    int moved = 0;
    std::string reason;   ///< why it was rejected or partial (empty on Ok)

    bool Ok() const { return status != Status::Rejected; }
    static InvResult Done(int n) { InvResult r; r.status = Status::Ok; r.moved = n; return r; }
    static InvResult Part(int n, const char* why) { InvResult r; r.status = Status::Partial; r.moved = n; r.reason = why; return r; }
    static InvResult No(const char* why) { InvResult r; r.reason = why; return r; }
};

/// Every change a player makes to an inventory through the UI goes through these —
/// the hotbar/backpack, chest screen, grid bags and crafting bench all call the same
/// code in the player and the editor Play view, and a networked game can route the
/// same requests to the host instead. Each op leaves the containers unchanged when
/// it's rejected, and fires on_inventory_changed on every slot inventory it changes.
namespace InventoryOps {

/// Slots usable in `inv` when a UI shows `shown` slots: the capacity when it has one,
/// else `shown` (0 = no limit).
int UsableSlots(const Inventory& inv, int shown = 0);

/// Move `amount` (0 = the whole stack) from src slot `si` to dst slot `di`, within one
/// inventory or across two. Empty target: placed. Same item: merged up to dst's
/// maxStack (Partial if some stays behind). Different item: swapped (whole stack only;
/// a part-stack onto a different item is rejected). `dstSlots` caps the target index
/// (UsableSlots of the destination's UI).
InvResult Move(Inventory* src, int si, Inventory* dst, int di, int amount = 0, int dstSlots = 0);

/// Split `amount` off slot `i` into the first empty slot below `slots`.
InvResult Split(Inventory* inv, int i, int amount, int slots = 0);

/// Shift-click: send slot `from` between the hotbar (the first `hotbar` slots) and the
/// rest — topping up matching stacks first, then the first empty slot there.
InvResult QuickMove(Inventory* inv, int from, int hotbar, int slots = 0);

/// Move as much of `from` slot `i` into `to` as fits (chest take / stash).
InvResult Transfer(Inventory* from, int i, Inventory* to);

/// Take `n` out of slot `i` (dropped into the world by the caller).
InvResult Drop(Inventory* inv, int i, int n = 1);

/// Use the item in slot `i` as `user` (who carries the inventory): eat/drink/heal it
/// through a Consumables on the user (its recipe, else the item definition's use
/// action), pick the matching piece in the user's BuildMode palette, or equip it
/// through the user's Equipment when its definition has an equip slot.
InvResult Use(Inventory* inv, int i, GameObject* user);

/// Atomically remove `take` and add `give` (crafting).
InvResult Craft(Inventory* inv, const std::vector<Inventory::Stack>& take, const std::vector<Inventory::Stack>& give);

// ---- By item (scripts, visual scripts, ActionList) ----

/// Move up to `n` of `item` from `src` to `dst`, stack by stack (each keeps its instance
/// data) until `dst` is full. Returns how many moved.
int TransferItem(Inventory* src, const std::string& item, int n, Inventory* dst);
/// Take up to `n` of `item` out of `inv` and drop them at `pos` as world pickups.
/// Returns how many were dropped.
int DropItem(Inventory* inv, const std::string& item, int n, Scene& scene, const Vec3& pos);
/// Put `n` of `item` into slot `i` (`n` <= 0 or no item empties it). Refused past the
/// capacity; `n` is capped to the stack limit.
bool SetSlot(Inventory* inv, int i, const std::string& item, int n);
/// Roll a loot table (`.okayloot`) into `target`'s Inventory (else its grid bag); what
/// doesn't fit lands next to it as pickups. Returns how many items were rolled.
int RollLoot(const std::string& table, GameObject* target);
/// Equip `item` from `who`'s bag into the Equipment slot named `slot`.
bool EquipItem(GameObject* who, const std::string& slot, const std::string& item);
/// Put what's worn in the slot named `slot` back into `who`'s bag.
bool UnequipSlot(GameObject* who, const std::string& slot);

/// Move grid item `index` from `src` to (x,y) in `dst` (turned 90° when `rotated`),
/// merging onto a matching stack (see GridInventory::MoveTo).
InvResult Move(GridInventory* src, int index, GridInventory* dst, int x, int y, bool rotated = false);

/// Split `n` off grid item `index` into the first free spot.
InvResult Split(GridInventory* inv, int index, int n);

} // namespace InventoryOps
} // namespace okay
