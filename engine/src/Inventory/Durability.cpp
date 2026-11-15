// Item durability (okay/Inventory/Durability.hpp). Written for this engine from its own inventory model.
#include "okay/Inventory/Durability.hpp"
#include "okay/Components/Equipment.hpp"
#include "okay/Inventory/InventoryEvents.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/VisualScript/VsValue.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace okay {
namespace durability {

namespace {
bool ValidSlot(const Inventory* inv, int i) { return inv && i >= 0 && i < (int)inv->slots.size(); }
bool DestroyOnBreak(const std::string& item) {
    const ItemDefinition* d = ItemDatabase::Get().Find(item);
    return d && d->Custom("breakAction") == "destroy";
}
} // namespace

float MaxOf(const ItemDefinition* def) {
    if (!def) return 0.0f;
    const std::string v = def->Custom("maxDurability");
    if (v.empty()) return 0.0f;
    const float f = (float)std::atof(v.c_str());
    return std::isfinite(f) && f > 0.0f ? f : 0.0f;
}
float MaxOf(const std::string& item) { return MaxOf(ItemDatabase::Get().Find(item)); }

bool Tracked(const std::string& item, const ItemInstanceData& data) { return data.durability >= 0.0f || MaxOf(item) > 0.0f; }

float Current(const std::string& item, const ItemInstanceData& data) {
    if (data.durability >= 0.0f) return data.durability;
    return MaxOf(item);
}

bool Broken(const std::string& item, const ItemInstanceData& data) { return Tracked(item, data) && Current(item, data) <= 0.0f; }

InvResult Wear(Inventory* inv, int slot, float amount) {
    if (!ValidSlot(inv, slot) || Inventory::Empty(inv->slots[slot])) return InvResult::No("empty slot");
    if (!(amount > 0.0f) || !std::isfinite(amount)) return InvResult::No("nothing to wear");
    const std::string item = inv->slots[slot].item;
    if (!Tracked(item, inv->slots[slot].data)) return InvResult::No("no durability");
    if (Broken(item, inv->slots[slot].data)) return InvResult::No("broken");
    if (inv->slots[slot].count > 1) {   // only one item of the stack wears: the rest moves to a free slot
        const InvResult s = InventoryOps::Split(inv, slot, inv->slots[slot].count - 1);
        if (!s.Ok()) return InvResult::No("stacked, and no free slot to split it");
    }
    Inventory::Slot& s = inv->slots[slot];
    const float now = std::max(0.0f, Current(item, s.data) - amount);
    s.data.durability = now;
    if (now <= 0.0f) {
        GameObject* owner = inv->gameObject;
        if (DestroyOnBreak(item)) { s = Inventory::Slot{}; }
        inv->NotifyChanged();
        InventoryEvents::Fire(owner, "on_item_broken", nullptr, {vs::VsValue(item)});
        return InvResult::Done(1);
    }
    inv->NotifyChanged();
    return InvResult::Done(1);
}

InvResult Repair(Inventory* inv, int slot, float amount, const std::vector<Inventory::Stack>& cost) {
    if (!ValidSlot(inv, slot) || Inventory::Empty(inv->slots[slot])) return InvResult::No("empty slot");
    const std::string item = inv->slots[slot].item;
    const float full = MaxOf(item);
    if (full <= 0.0f) return InvResult::No("no durability");
    const float now = Current(item, inv->slots[slot].data);
    if (now >= full) return InvResult::No("not damaged");
    if (!cost.empty()) {
        // The item being repaired is set aside while the materials are taken, so it can never pay for itself (a
        // sword repaired with another sword takes the other one). Slots never move, so it goes back where it was.
        const Inventory::Slot keep = inv->slots[slot];
        inv->slots[slot] = Inventory::Slot{};
        const bool paid = inv->Exchange(cost, {});
        inv->slots[slot] = keep;
        if (!paid) { inv->NotifyChanged(); return InvResult::No("missing materials"); }
    }
    Inventory::Slot& s = inv->slots[slot];
    const float target = (amount > 0.0f && std::isfinite(amount)) ? std::min(full, now + amount) : full;
    s.data.durability = target;
    inv->NotifyChanged();
    return InvResult::Done(1);
}

InvResult WearEquipped(Equipment* eq, int slot, float amount) {
    if (!eq || slot < 0 || slot >= (int)eq->slots.size() || eq->slots[slot].item.empty()) return InvResult::No("nothing worn");
    if (!(amount > 0.0f) || !std::isfinite(amount)) return InvResult::No("nothing to wear");
    Equipment::Slot& s = eq->slots[slot];
    if (!Tracked(s.item, s.data)) return InvResult::No("no durability");
    if (Broken(s.item, s.data)) return InvResult::No("broken");
    s.data.durability = std::max(0.0f, Current(s.item, s.data) - amount);
    if (s.data.durability <= 0.0f) {
        const std::string item = s.item;
        if (DestroyOnBreak(item)) eq->Clear(slot);
        eq->Refresh();   // a broken piece gives no armor or defense
        InventoryEvents::Fire(eq->gameObject, "on_item_broken", nullptr, {vs::VsValue(item)});
    }
    return InvResult::Done(1);
}

InvResult RepairEquipped(Equipment* eq, int slot, float amount) {
    if (!eq || slot < 0 || slot >= (int)eq->slots.size() || eq->slots[slot].item.empty()) return InvResult::No("nothing worn");
    Equipment::Slot& s = eq->slots[slot];
    const float full = MaxOf(s.item);
    if (full <= 0.0f) return InvResult::No("no durability");
    const float now = Current(s.item, s.data);
    if (now >= full) return InvResult::No("not damaged");
    const bool wasBroken = now <= 0.0f;
    s.data.durability = (amount > 0.0f && std::isfinite(amount)) ? std::min(full, now + amount) : full;
    if (wasBroken) eq->Refresh();
    return InvResult::Done(1);
}

} // namespace durability
} // namespace okay
