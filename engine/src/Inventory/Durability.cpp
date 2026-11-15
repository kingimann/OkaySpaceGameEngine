// Item durability (okay/Inventory/Durability.hpp). Written for this engine from its own inventory model.
#include "okay/Inventory/Durability.hpp"
#include "okay/Components/Equipment.hpp"
#include "okay/Components/NetInventory.hpp"
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
// Every change: the new value goes to scripts (on_durability_changed(item, value)) and, on a host, to the clients.
void Changed(GameObject* owner, const std::string& item, float value) {
    InventoryEvents::Fire(owner, "on_durability_changed", nullptr, {vs::VsValue(item), vs::VsValue(value)});
    NetInventory::NoticeDurability(owner);
}
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

// Only the definition decides. A value stored on an item whose definition has no maxDurability (an older scene or
// save, or a definition that dropped it) is ignored, so a stored 0 never makes such an item "broken".
bool Tracked(const std::string& item, const ItemInstanceData&) { return MaxOf(item) > 0.0f; }

float Current(const std::string& item, const ItemInstanceData& data) {
    const float full = MaxOf(item);
    if (full <= 0.0f) return 0.0f;
    if (data.durability >= 0.0f) return std::min(data.durability, full);
    return full;
}

bool Broken(const std::string& item, const ItemInstanceData& data) { return Tracked(item, data) && Current(item, data) <= 0.0f; }

InvResult Wear(Inventory* inv, int slot, float amount) {
    if (!ValidSlot(inv, slot) || Inventory::Empty(inv->slots[slot])) return InvResult::No("empty slot");
    if (!(amount > 0.0f) || !std::isfinite(amount)) return InvResult::No("nothing to wear");
    const std::string item = inv->slots[slot].item;
    if (!Tracked(item, inv->slots[slot].data)) return InvResult::No("no durability");
    if (NetInventory::HostDecides(inv->gameObject)) return InvResult::No("the host decides");
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
        Changed(owner, item, 0.0f);
        InventoryEvents::Fire(owner, "on_item_broken", nullptr, {vs::VsValue(item)});
        return InvResult::Done(1);
    }
    inv->NotifyChanged();
    Changed(inv->gameObject, item, now);
    return InvResult::Done(1);
}

InvResult Repair(Inventory* inv, int slot, float amount, const std::vector<Inventory::Stack>& cost) {
    if (!ValidSlot(inv, slot) || Inventory::Empty(inv->slots[slot])) return InvResult::No("empty slot");
    const std::string item = inv->slots[slot].item;
    const float full = MaxOf(item);
    if (full <= 0.0f) return InvResult::No("no durability");
    if (NetInventory::HostDecides(inv->gameObject)) return InvResult::No("the host decides");
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
    Changed(inv->gameObject, item, target);
    return InvResult::Done(1);
}

InvResult WearEquipped(Equipment* eq, int slot, float amount) {
    if (!eq || slot < 0 || slot >= (int)eq->slots.size() || eq->slots[slot].item.empty()) return InvResult::No("nothing worn");
    if (!(amount > 0.0f) || !std::isfinite(amount)) return InvResult::No("nothing to wear");
    Equipment::Slot& s = eq->slots[slot];
    if (!Tracked(s.item, s.data)) return InvResult::No("no durability");
    if (Broken(s.item, s.data)) return InvResult::No("broken");
    if (NetInventory::HostDecides(eq->gameObject)) return InvResult::No("the host decides");
    const std::string item = s.item;
    s.data.durability = std::max(0.0f, Current(s.item, s.data) - amount);
    const float now = s.data.durability;
    if (now <= 0.0f) {
        if (DestroyOnBreak(item)) eq->Clear(slot);
        eq->Refresh();   // a broken piece gives no armor or defense
    }
    if (auto* bag = eq->gameObject ? eq->gameObject->GetComponent<Inventory>() : nullptr) bag->NotifyChanged();   // UIs redraw
    Changed(eq->gameObject, item, now);
    if (now <= 0.0f) InventoryEvents::Fire(eq->gameObject, "on_item_broken", nullptr, {vs::VsValue(item)});
    return InvResult::Done(1);
}

InvResult RepairEquipped(Equipment* eq, int slot, float amount) {
    if (!eq || slot < 0 || slot >= (int)eq->slots.size() || eq->slots[slot].item.empty()) return InvResult::No("nothing worn");
    Equipment::Slot& s = eq->slots[slot];
    const float full = MaxOf(s.item);
    if (full <= 0.0f) return InvResult::No("no durability");
    if (NetInventory::HostDecides(eq->gameObject)) return InvResult::No("the host decides");
    const float now = Current(s.item, s.data);
    if (now >= full) return InvResult::No("not damaged");
    const bool wasBroken = now <= 0.0f;
    s.data.durability = (amount > 0.0f && std::isfinite(amount)) ? std::min(full, now + amount) : full;
    if (wasBroken) eq->Refresh();
    if (auto* bag = eq->gameObject ? eq->gameObject->GetComponent<Inventory>() : nullptr) bag->NotifyChanged();   // UIs redraw
    Changed(eq->gameObject, s.item, s.data.durability);
    return InvResult::Done(1);
}

void WearArmorOnHit(GameObject* who) {
    Equipment* eq = who ? Equipment::Of(who) : nullptr;
    if (!eq) return;
    for (int i = 0; i < (int)eq->slots.size(); ++i) {
        const Equipment::Slot& s = eq->slots[i];
        if (s.item.empty() || Broken(s.item, s.data)) continue;
        const ItemDefinition* d = ItemDatabase::Get().Find(s.item);
        const std::string v = d ? d->Custom("wearPerHit") : std::string();
        const float per = v.empty() ? 0.0f : (float)std::atof(v.c_str());
        if (std::isfinite(per) && per > 0.0f) WearEquipped(eq, i, per);
    }
}

namespace {
void WearMainHand(GameObject* who, const char* key) {
    Equipment* eq = who ? Equipment::Of(who) : nullptr;
    if (!eq) return;
    const int i = eq->Find("Main Hand");
    if (i < 0 || eq->slots[i].item.empty() || Broken(eq->slots[i].item, eq->slots[i].data)) return;
    const ItemDefinition* d = ItemDatabase::Get().Find(eq->slots[i].item);
    const std::string v = d ? d->Custom(key) : std::string();
    const float per = v.empty() ? 0.0f : (float)std::atof(v.c_str());
    if (std::isfinite(per) && per > 0.0f) WearEquipped(eq, i, per);
}
} // namespace

void WearWeaponOnStrike(GameObject* who) { WearMainHand(who, "wearPerStrike"); }
void WearWeaponOnShot(GameObject* who) { WearMainHand(who, "wearPerShot"); }

} // namespace durability
} // namespace okay
