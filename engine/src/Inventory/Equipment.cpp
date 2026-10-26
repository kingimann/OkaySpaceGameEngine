#include "okay/Components/Equipment.hpp"
#include "okay/Components/GridInventory.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/Sockets.hpp"
#include "okay/Components/Stats.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/SurvivalStats.hpp"
#include "okay/Inventory/InventoryPersistence.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/Transform.hpp"
#include <cstdlib>

namespace okay {

namespace {
const char* kBagKey = "bag";   // instance-data key holding a worn bag's contents

// A bag's contents are multi-line text; scene lines are one line, so escape newlines.
std::string Escape(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '\\') o += "\\\\"; else if (c == '\n') o += "\\n"; else o += c; }
    return o;
}
std::string Unescape(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) { o += s[i + 1] == 'n' ? '\n' : s[i + 1]; ++i; }
        else o += s[i];
    }
    return o;
}
float CustomF(const ItemDefinition* d, const char* key) {
    if (!d) return 0.0f;
    const std::string v = d->Custom(key);
    return v.empty() ? 0.0f : (float)std::atof(v.c_str());
}
void EraseValue(ItemInstanceData& d, const std::string& key) {
    for (std::size_t i = 0; i < d.values.size(); ++i)
        if (d.values[i].first == key) { d.values.erase(d.values.begin() + (long)i); return; }
}
void SetValue(ItemInstanceData& d, const std::string& key, const std::string& v) {
    for (auto& kv : d.values) if (kv.first == key) { kv.second = v; return; }
    d.values.push_back({key, v});
}
// The shown prefab is display only: never saved, no physics, not a pickup.
void MakeDisplayOnly(GameObject* go) {
    if (!go) return;
    go->transient = true;
    if (auto* pk = go->GetComponent<ItemPickup>()) go->RemoveComponent(pk);
    for (auto* rb : go->GetComponents<Rigidbody3D>()) { rb->bodyType = Rigidbody3D::BodyType::Kinematic; rb->enabled = false; }
    for (auto* c : go->GetComponents<Collider3D>()) c->enabled = false;
    if (go->transform)
        for (Transform* ch : go->transform->Children()) if (ch) MakeDisplayOnly(ch->gameObject);
}
} // namespace

std::vector<Equipment::Slot> Equipment::DefaultSlots() {
    std::vector<Slot> s;
    auto add = [&](const char* n, EquipSlot e) { Slot x; x.name = n; x.accepts = e; s.push_back(x); };
    add("Head", EquipSlot::Head);   add("Chest", EquipSlot::Chest); add("Legs", EquipSlot::Legs);
    add("Feet", EquipSlot::Feet);   add("Hands", EquipSlot::Hands); add("Back", EquipSlot::Back);
    add("Main Hand", EquipSlot::MainHand); add("Off Hand", EquipSlot::OffHand);
    return s;
}

const char* Equipment::DefaultSocket(EquipSlot s) {
    switch (s) {
        case EquipSlot::Head:     return "head";
        case EquipSlot::Chest:    return "torso";
        case EquipSlot::Back:     return "torso";
        case EquipSlot::Legs:     return "hips";
        case EquipSlot::Feet:     return "r_foot";
        case EquipSlot::Hands:    return "r_hand";
        case EquipSlot::MainHand: return "r_hand";
        case EquipSlot::OffHand:  return "l_hand";
        default:                  return "";
    }
}

Equipment* Equipment::Of(GameObject* go) {
    if (!go) return nullptr;
    if (auto* e = go->GetComponent<Equipment>()) return e;
    Transform* t = go->transform;
    while (t && t->Parent()) t = t->Parent();
    return (t && t->gameObject) ? t->gameObject->GetComponent<Equipment>() : nullptr;
}

int Equipment::Find(const std::string& slotName) const {
    for (std::size_t i = 0; i < slots.size(); ++i) if (slots[i].name == slotName) return (int)i;
    return -1;
}

bool Equipment::Accepts(int slot, const std::string& item) const {
    if (slot < 0 || slot >= (int)slots.size() || item.empty()) return false;
    const ItemDefinition* d = ItemDatabase::Get().Find(item);
    if (!d || d->equip == EquipSlot::None) return false;
    return slots[slot].accepts == EquipSlot::None || slots[slot].accepts == d->equip;
}

int Equipment::SlotFor(const std::string& item) const {
    int first = -1;
    for (int i = 0; i < (int)slots.size(); ++i) {
        if (!Accepts(i, item)) continue;
        if (slots[i].item.empty()) return i;
        if (first < 0) first = i;
    }
    return first;
}

const std::string& Equipment::ItemIn(int slot) const {
    static const std::string none;
    return (slot >= 0 && slot < (int)slots.size()) ? slots[slot].item : none;
}

bool Equipment::PutBack(Inventory& inv, int pref, const std::string& item, const ItemInstanceData& data, int& placed) {
    const int usable = InventoryOps::UsableSlots(inv);
    Inventory::Slot one{item, 1, data};
    auto fits = [&](int i) { return usable <= 0 || i < usable; };
    if (pref >= 0 && fits(pref)) {
        if (pref >= (int)inv.slots.size()) inv.slots.resize(pref + 1);
        if (Inventory::Empty(inv.slots[pref])) { inv.slots[pref] = one; placed = pref; return true; }
    }
    for (int i = 0; i < (int)inv.slots.size(); ++i)   // top up a matching stack
        if (!Inventory::Empty(inv.slots[i]) && Inventory::Stackable(inv.slots[i], one) &&
            inv.slots[i].count < inv.StackCap(item)) { ++inv.slots[i].count; placed = i; return true; }
    for (int i = 0; i < (int)inv.slots.size(); ++i)
        if (fits(i) && Inventory::Empty(inv.slots[i])) { inv.slots[i] = one; placed = i; return true; }
    if (fits((int)inv.slots.size())) { inv.slots.push_back(one); placed = (int)inv.slots.size() - 1; return true; }
    return false;
}

InvResult Equipment::EquipFrom(Inventory& inv, int from, int to) {
    if (from < 0 || from >= (int)inv.slots.size() || Inventory::Empty(inv.slots[from])) return InvResult::No("nothing to equip");
    const std::string item = inv.slots[from].item;
    if (to < 0) to = SlotFor(item);
    if (!Accepts(to, item)) return InvResult::No("this slot doesn't take that item");
    const std::vector<Inventory::Slot> before = inv.slots;
    const ItemInstanceData data = inv.slots[from].data;
    if (--inv.slots[from].count <= 0) inv.slots[from] = Inventory::Slot{};
    Slot& s = slots[to];
    const std::string old = s.item;
    if (!old.empty()) {
        int placed = -1;
        if (!PutBack(inv, from, old, CurrentData(to), placed)) { inv.slots = before; return InvResult::No("no room for what's worn there"); }
        Hide(to);
    }
    s.item = item; s.data = data;
    m_dirty = true;
    Refresh();
    inv.NotifyChanged();
    if (!old.empty()) Fire("on_unequip", to, old);
    Fire("on_equip", to, item);
    return InvResult::Done(1);
}

InvResult Equipment::Unequip(int slot, Inventory& inv, int into) {
    if (slot < 0 || slot >= (int)slots.size() || slots[slot].item.empty()) return InvResult::No("nothing worn there");
    const std::string item = slots[slot].item;
    int placed = -1;
    if (!PutBack(inv, into, item, CurrentData(slot), placed)) return InvResult::No("no room in the bag");
    Hide(slot);
    slots[slot].item.clear(); slots[slot].data = ItemInstanceData{};
    m_dirty = true;
    Refresh();
    inv.NotifyChanged();
    Fire("on_unequip", slot, item);
    return InvResult::Done(1);
}

bool Equipment::Set(int slot, const std::string& item, const ItemInstanceData& data) {
    if (item.empty()) { Clear(slot); return true; }
    if (!Accepts(slot, item)) return false;
    const std::string old = slots[slot].item;
    Hide(slot);
    slots[slot].item = item; slots[slot].data = data;
    m_dirty = true;
    if (!old.empty()) Fire("on_unequip", slot, old);
    Fire("on_equip", slot, item);
    return true;
}

void Equipment::Clear(int slot) {
    if (slot < 0 || slot >= (int)slots.size() || slots[slot].item.empty()) return;
    const std::string old = slots[slot].item;
    Hide(slot);
    slots[slot].item.clear(); slots[slot].data = ItemInstanceData{};
    m_dirty = true;
    Fire("on_unequip", slot, old);
}

void Equipment::Restore(int slot, const std::string& item, const ItemInstanceData& data) {
    if (slot < 0 || slot >= (int)slots.size()) return;
    slots[slot].shownItem.clear();   // so Hide doesn't fold the old bag into the new data
    Hide(slot);
    slots[slot].item = item; slots[slot].data = data;
    m_dirty = true;
}

ItemInstanceData Equipment::CurrentData(int slot) const {
    if (slot < 0 || slot >= (int)slots.size()) return {};
    ItemInstanceData d = slots[slot].data;
    if (GameObject* go = Shown(slot))
        if (go->GetComponent<GridInventory>()) SetValue(d, kBagKey, Escape(InventoryPersistence::SerializeObject(*go)));
    return d;
}

GameObject* Equipment::Shown(int slot) const {
    if (slot < 0 || slot >= (int)slots.size() || !slots[slot].shown || !gameObject) return nullptr;
    GameObject* go = GameObject::FromInstanceId(slots[slot].shown);
    return (go && go->scene() == gameObject->scene()) ? go : nullptr;
}

float Equipment::ArmorBonus() const {
    float a = 0.0f;
    for (const auto& s : slots) if (!s.item.empty()) a += CustomF(ItemDatabase::Get().Find(s.item), "armor");
    return a;
}
int Equipment::DefenseBonus() const {
    float d = 0.0f;
    for (const auto& s : slots) if (!s.item.empty()) d += CustomF(ItemDatabase::Get().Find(s.item), "defense");
    return (int)d;
}
float Equipment::WeightWorn() const {
    float w = 0.0f;
    for (const auto& s : slots)
        if (!s.item.empty()) if (const ItemDefinition* d = ItemDatabase::Get().Find(s.item)) w += d->weight;
    return w;
}

void Equipment::Show(int i) {
    Slot& s = slots[i];
    Scene* scene = gameObject ? gameObject->scene() : nullptr;
    const ItemDefinition* d = ItemDatabase::Get().Find(s.item);
    if (!scene || !d) return;
    const std::string prefab = d->Custom("equipPrefab", d->worldPrefab);
    if (prefab.empty()) return;   // nothing to show (stats still apply)
    GameObject* go = SceneSerializer::InstantiateFromFile(*scene, prefab);
    if (!go || !go->transform) return;
    MakeDisplayOnly(go);
    GameObject* at = FindSocket(gameObject, s.socket.empty() ? DefaultSocket(s.accepts) : s.socket);
    go->transform->SetParent((at ? at : gameObject)->transform, false);
    go->transform->localPosition = Vec3::Zero;
    go->transform->localRotation = Quat::Identity;
    // A worn bag: it's the player's container now, filled from the item's saved contents.
    if (auto* g = go->GetComponent<GridInventory>()) {
        g->worldItem = false;
        for (const auto& kv : s.data.values)
            if (kv.first == kBagKey) { InventoryPersistence::LoadObjectText(*go, Unescape(kv.second)); break; }
        EraseValue(s.data, kBagKey);   // the live bag holds them until it comes off
    }
    s.shown = go->InstanceId();
    s.shownItem = s.item;
}

void Equipment::Hide(int i) {
    if (i < 0 || i >= (int)slots.size()) return;
    Slot& s = slots[i];
    if (GameObject* go = Shown(i)) {
        if (go->GetComponent<GridInventory>() && !s.item.empty() && s.shownItem == s.item)
            s.data = CurrentData(i);   // keep a worn bag's contents with the item
        if (Scene* sc = go->scene()) sc->Destroy(go);
    }
    s.shown = 0;
    s.shownItem.clear();
}

void Equipment::Refresh() {
    m_dirty = false;
    for (int i = 0; i < (int)slots.size(); ++i) {
        Slot& s = slots[i];
        const bool want = showOnCharacter && !s.item.empty();
        GameObject* go = Shown(i);
        if (go && (!want || s.shownItem != s.item)) { Hide(i); go = nullptr; }
        if (!go && s.shown) { s.shown = 0; s.shownItem.clear(); }   // destroyed elsewhere
        if (want && !go) Show(i);
    }
    ApplyBonuses(ArmorBonus(), DefenseBonus(), WeightWorn());
}

void Equipment::OnDestroy() {
    for (int i = 0; i < (int)slots.size(); ++i) Hide(i);
    ApplyBonuses(0.0f, 0, 0.0f);
}

void Equipment::ApplyBonuses(float armor, int defense, float weight) {
    if (!gameObject) return;
    if (auto* h = gameObject->GetComponent<HealthStat>()) h->equipArmor = armor;
    if (auto* ss = gameObject->GetComponent<SurvivalStats>()) ss->equipArmor = armor;
    if (auto* st = gameObject->GetComponent<Stats>()) st->equipDefense = defense;
    if (auto* inv = gameObject->GetComponent<Inventory>())
        if (inv->wornWeight != weight) { inv->wornWeight = weight; inv->NotifyChanged(); }
}

void Equipment::Fire(const char* ev, int slot, const std::string& item) {
    if (!gameObject || slot < 0 || slot >= (int)slots.size()) return;
    if (auto* sc = gameObject->GetComponent<ScriptComponent>())
        if (sc->VM()) sc->VM()->CallEventArgs(ev, {vs::VsValue(slots[slot].name), vs::VsValue(item)});
}

} // namespace okay
