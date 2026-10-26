#include "okay/Inventory/InventoryOps.hpp"
#include "okay/Components/NetInventory.hpp"
#include "okay/Components/BuildMode.hpp"
#include "okay/Components/Consumables.hpp"
#include "okay/Components/Equipment.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Inventory/LootTable.hpp"
#include "okay/Inventory/InventoryEvents.hpp"
#include <algorithm>
#include <utility>

namespace okay {
namespace InventoryOps {

namespace {
bool ValidSlot(const Inventory* inv, int i) { return inv && i >= 0 && i < (int)inv->slots.size(); }
void TrimTail(Inventory& inv) {   // drop trailing holes (positions before them stay)
    while (!inv.slots.empty() && Inventory::Empty(inv.slots.back())) inv.slots.pop_back();
}
void Notify(Inventory* a, Inventory* b) { if (a) a->NotifyChanged(); if (b && b != a) b->NotifyChanged(); }
} // namespace

int UsableSlots(const Inventory& inv, int shown) {
    return inv.capacity > 0 ? inv.capacity : shown;
}

namespace {
constexpr int kMaxSlots = 4096;   // no UI or request may grow a bag past this many slots

float UnitWeight(const std::string& item) {
    const ItemDefinition* d = ItemDatabase::Get().Find(item);
    return d ? d->weight : 0.0f;
}
/// Would `inv` still be within its enforced weight limit after losing `out` and gaining `in`?
bool WeightOk(const Inventory& inv, float in, float out) {
    if (!inv.enforceWeight || inv.weightLimit <= 0.0f || in <= out) return true;
    return inv.TotalWeight() - out + in <= inv.weightLimit + 1e-4f;
}

InvResult MoveImpl(Inventory* src, int si, Inventory* dst, int di, int amount, int dstSlots, bool notice) {
    if (!ValidSlot(src, si) || !dst || di < 0) return InvResult::No("bad slot");
    if (amount < 0) return InvResult::No("bad amount");
    if (src == dst && si == di) return InvResult::No("same slot");
    int limit = dstSlots > 0 ? dstSlots : UsableSlots(*dst);
    if (limit <= 0 || limit > kMaxSlots) limit = kMaxSlots;
    if (di >= limit) return InvResult::No("past the last slot");
    if (Inventory::Empty(src->slots[si])) return InvResult::No("empty slot");
    const int have = src->slots[si].count;
    const int n = (amount == 0 || amount >= have) ? have : amount;
    if (n < have && src->slots[si].data.uid != 0) return InvResult::No("unique items move whole");
    while ((int)dst->slots.size() <= di) dst->slots.push_back(Inventory::Slot{});
    Inventory::Slot& a = src->slots[si];   // (after the grow: src may be dst)
    Inventory::Slot& b = dst->slots[di];
    const bool other = src != dst;
    int moved = 0;
    const char* why = "stack full";
    if (Inventory::Empty(b)) {
        int m = (int)std::min<long long>(n, dst->StackCap(a.item));   // the target's own stack limit
        if (other) { const int w = dst->WeightFit(a.item, m); if (w < m) why = "too heavy"; m = w; }
        if (m > 0 && m < have && a.data.uid != 0) m = 0;
        if (m <= 0) { TrimTail(*dst); return InvResult::No(why); }
        b = a; b.count = m; a.count -= m; moved = m;
    } else if (Inventory::Stackable(b, a)) {
        const long long room = dst->StackCap(b.item) - b.count;
        long long m = std::min<long long>(n, room > 0 ? room : 0);
        if (other) { const int w = dst->WeightFit(a.item, (int)m); if (w < m) why = "too heavy"; m = w; }
        if (m <= 0) { TrimTail(*dst); return InvResult::No(why); }
        moved = (int)m;
        b.count += moved; a.count -= moved;
    } else {
        if (n != have) { TrimTail(*dst); return InvResult::No("different item there"); }
        if (have > dst->StackCap(a.item) || b.count > src->StackCap(b.item)) { TrimTail(*dst); return InvResult::No("stack too big for the other bag"); }
        if (other && (!WeightOk(*dst, UnitWeight(a.item) * a.count, UnitWeight(b.item) * b.count) ||
                      !WeightOk(*src, UnitWeight(b.item) * b.count, UnitWeight(a.item) * a.count))) {
            TrimTail(*dst); return InvResult::No("too heavy");
        }
        std::swap(a, b); moved = n;
    }
    if (a.count <= 0) a = Inventory::Slot{};
    TrimTail(*src); TrimTail(*dst);
    Notify(src, dst);
    const InvResult r = moved < n ? InvResult::Part(moved, why) : InvResult::Done(moved);
    if (notice) NetInventory::NoticeMove(src, si, dst, di, amount, r.Ok());
    return r;
}
} // namespace

InvResult Move(Inventory* src, int si, Inventory* dst, int di, int amount, int dstSlots) {
    return MoveImpl(src, si, dst, di, amount, dstSlots, true);
}

InvResult Split(Inventory* inv, int i, int amount, int slots) {
    if (!ValidSlot(inv, i) || Inventory::Empty(inv->slots[i])) return InvResult::No("empty slot");
    if (amount < 1 || amount >= inv->slots[i].count) return InvResult::No("nothing to split");
    if (inv->slots[i].data.uid != 0) return InvResult::No("unique items cannot be split");
    const int limit = UsableSlots(*inv, slots);
    int to = -1;
    for (int k = 0; k < (int)inv->slots.size() && (limit <= 0 || k < limit); ++k)
        if (Inventory::Empty(inv->slots[k])) { to = k; break; }
    if (to < 0) {
        if (limit > 0 && (int)inv->slots.size() >= limit) return InvResult::No("no empty slot");
        to = (int)inv->slots.size();
        inv->slots.push_back(Inventory::Slot{});
    }
    inv->slots[to] = inv->slots[i];   // same item and instance data
    inv->slots[to].count = amount;
    inv->slots[i].count -= amount;
    inv->NotifyChanged();
    NetInventory::NoticeSplit(inv, i, amount, true);
    return InvResult::Done(amount);
}

InvResult QuickMove(Inventory* inv, int from, int hotbar, int slots) {
    if (!ValidSlot(inv, from) || Inventory::Empty(inv->slots[from])) return InvResult::No("empty slot");
    const int n = hotbar < 1 ? 1 : hotbar, total = std::min(UsableSlots(*inv, slots), kMaxSlots);
    const int lo = (from < n) ? n : 0, hi = (from < n) ? total : std::min(n, total);
    while ((int)inv->slots.size() < hi) inv->slots.push_back(Inventory::Slot{});
    Inventory::Slot& s = inv->slots[from];
    const long long cap = inv->StackCap(s.item);
    int moved = 0;
    for (int k = lo; k < hi && !Inventory::Empty(s); ++k) {   // top up matching stacks
        Inventory::Slot& d = inv->slots[k];
        if (Inventory::Empty(d) || !Inventory::Stackable(d, s)) continue;
        const long long room = cap - d.count;
        if (room <= 0) continue;
        const int mv = (long long)s.count < room ? s.count : (int)room;
        d.count += mv; s.count -= mv; moved += mv;
        if (s.count <= 0) s = Inventory::Slot{};
    }
    if (!Inventory::Empty(s))                                   // the rest: first empty slot there
        for (int k = lo; k < hi; ++k)
            if (Inventory::Empty(inv->slots[k])) { moved += s.count; std::swap(inv->slots[k], s); break; }
    const bool rest = !Inventory::Empty(s);
    TrimTail(*inv);
    if (moved <= 0) return InvResult::No("no room on the other side");
    inv->NotifyChanged();
    const InvResult r = rest ? InvResult::Part(moved, "no room for the rest") : InvResult::Done(moved);
    NetInventory::NoticeQuick(inv, from, hotbar, r.Ok());
    return r;
}

InvResult Transfer(Inventory* from, int i, Inventory* to) {
    if (!ValidSlot(from, i) || !to || from == to) return InvResult::No("bad slot");
    if (Inventory::Empty(from->slots[i])) return InvResult::No("empty slot");
    if (!from->slots[i].data.Empty()) {   // a one-off item (durability, uid, values) moves whole into a free slot
        const int limit = UsableSlots(*to);
        int hole = -1;
        for (int k = 0; k < (int)to->slots.size(); ++k) if (Inventory::Empty(to->slots[k])) { hole = k; break; }
        if (hole < 0) {
            if (limit > 0 && (int)to->slots.size() >= limit) return InvResult::No("destination full");
            hole = (int)to->slots.size();
            to->slots.push_back(Inventory::Slot{});
        }
        const int n = from->slots[i].count;
        if (to->WeightFit(from->slots[i].item, n) < n) { TrimTail(*to); return InvResult::No("too heavy"); }
        std::swap(to->slots[hole], from->slots[i]);
        TrimTail(*from);
        Notify(from, to);
        NetInventory::NoticeTransfer(from, i, to, true);
        return InvResult::Done(n);
    }
    const std::string item = from->slots[i].item;
    const int have = from->slots[i].count;
    const int left = to->AddPartial(item, have);
    if (left == have) return InvResult::No("destination full");
    Inventory::Slot& src = from->slots[i];
    src.count = left;
    if (src.count <= 0) src = Inventory::Slot{};
    from->NotifyChanged();
    const InvResult r = left > 0 ? InvResult::Part(have - left, "destination full") : InvResult::Done(have);
    NetInventory::NoticeTransfer(from, i, to, r.Ok());
    return r;
}

int TransferAll(Inventory* from, Inventory* to) {
    if (!from || !to || from == to) return 0;
    int moved = 0;
    for (int i = 0; i < (int)from->slots.size(); ++i) {
        if (Inventory::Empty(from->slots[i])) continue;
        InvResult r = Transfer(from, i, to);
        if (r.Ok()) moved += r.moved;
    }
    return moved;
}

int TransferMatching(Inventory* from, Inventory* to) {
    if (!from || !to || from == to) return 0;
    int moved = 0;
    for (int i = 0; i < (int)from->slots.size(); ++i) {
        if (Inventory::Empty(from->slots[i])) continue;
        if (to->Count(from->slots[i].item) <= 0) continue;
        InvResult r = Transfer(from, i, to);
        if (r.Ok()) moved += r.moved;
    }
    return moved;
}

bool SortByName(Inventory* inv) {
    if (!inv) return false;
    std::vector<Inventory::Slot> packed;
    for (const auto& s : inv->slots) {
        if (Inventory::Empty(s)) continue;
        bool merged = false;
        for (auto& p : packed)
            if (Inventory::Stackable(p, s) && (long long)p.count + s.count <= 2147483647LL) {
                p.count += s.count; merged = true; break;
            }
        if (!merged) packed.push_back(s);
    }
    std::vector<Inventory::Slot> split;
    for (const auto& p : packed) {
        const long long cap = inv->StackCap(p.item);
        for (long long left = p.count; left > 0; left -= cap) {
            Inventory::Slot part = p;
            part.count = (int)(left < cap ? left : cap);
            split.push_back(part);
        }
    }
    const std::size_t cap = inv->capacity > 0 ? (std::size_t)inv->capacity : 0;
    const std::size_t room = inv->slots.size() > cap ? inv->slots.size() : cap;
    if (split.size() > room) return false;
    const ItemDatabase& db = ItemDatabase::Get();
    std::stable_sort(split.begin(), split.end(), [&](const Inventory::Slot& a, const Inventory::Slot& b) {
        const ItemDefinition* da = db.Find(a.item), *dbb = db.Find(b.item);
        const std::string& na = da ? da->DisplayName() : a.item;
        const std::string& nb = dbb ? dbb->DisplayName() : b.item;
        return na < nb;
    });
    const std::size_t keep = std::max(inv->slots.size(), split.size());
    inv->slots.assign(keep, Inventory::Slot{});
    for (std::size_t i = 0; i < split.size(); ++i) inv->slots[i] = split[i];
    inv->NotifyChanged();
    return true;
}

InvResult Drop(Inventory* inv, int i, int n) {
    if (!ValidSlot(inv, i) || Inventory::Empty(inv->slots[i]) || n < 1) return InvResult::No("empty slot");
    Inventory::Slot& s = inv->slots[i];
    const int take = std::min(n, s.count);
    s.count -= take;
    if (s.count <= 0) s = Inventory::Slot{};
    inv->NotifyChanged();
    NetInventory::NoticeDrop(inv, i, take, true);
    return InvResult::Done(take);
}

static InvResult UseItem(Inventory* inv, int i, GameObject* user) {
    if (!ValidSlot(inv, i) || Inventory::Empty(inv->slots[i]) || !user) return InvResult::No("nothing to use");
    const std::string item = inv->slots[i].item;
    GameObject* root = user;
    while (root->transform && root->transform->Parent() && root->transform->Parent()->gameObject) root = root->transform->Parent()->gameObject;
    for (GameObject* g : {user, root}) {
        auto* c = g->GetComponent<Consumables>();
        if (c && c->Inv() == inv && c->Use(item)) return InvResult::Done(1);
    }
    const ItemDefinition* def = ItemDatabase::Get().Find(item);
    for (GameObject* g : {user, root})
        if (auto* bm = g->GetComponent<BuildMode>())
            for (int k = 0; k < (int)bm->palette.size(); ++k) {
                const std::string& n = bm->palette[k].name;
                if (n == item || (def && n == def->name)) { bm->SetActive(true); bm->SelectSlot(k); return InvResult::Done(0); }
            }
    if (def && def->equip != EquipSlot::None)   // wearable / weapon: put it on (main hand for a weapon)
        if (Equipment* eq = Equipment::Of(user)) return eq->EquipFrom(*inv, i);
    return InvResult::No("nothing uses it");
}

InvResult Use(Inventory* inv, int i, GameObject* user) {
    const std::string item = ValidSlot(inv, i) ? inv->slots[i].item : std::string();
    const ItemDefinition* def = item.empty() ? nullptr : ItemDatabase::Get().Find(item);
    const InvResult r = UseItem(inv, i, user);
    if (r.Ok()) {
        InventoryEvents::Fire(inv->gameObject, "on_item_used", "OnItemUsed", {vs::VsValue(item)});
        if (!def || def->equip == EquipSlot::None) NetInventory::NoticeUse(inv, i, true);
    }
    return r;
}

int TransferItem(Inventory* src, const std::string& item, int n, Inventory* dst) {
    if (!src || !dst || src == dst || item.empty() || n <= 0) return 0;
    int moved = 0;
    for (auto& s : src->slots) {
        if (n <= 0) break;
        if (Inventory::Empty(s) || s.item != item) continue;
        Inventory carried;
        carried.slots.push_back(Inventory::Slot{s.item, std::min(n, s.count), s.data});
        const int want = carried.slots[0].count;
        const InvResult r = Transfer(&carried, 0, dst);
        s.count -= r.moved; moved += r.moved; n -= r.moved;
        if (s.count <= 0) s = Inventory::Slot{};
        if (r.moved < want) break;   // the target is full
    }
    if (moved > 0) src->NotifyChanged();
    return moved;
}

int DropItem(Inventory* inv, const std::string& item, int n, Scene& scene, const Vec3& pos) {
    if (!inv || item.empty()) return 0;
    int dropped = 0;
    for (int i = 0; i < (int)inv->slots.size() && n > 0; ++i) {
        if (Inventory::Empty(inv->slots[i]) || inv->slots[i].item != item) continue;
        const ItemInstanceData data = inv->slots[i].data;
        const int k = std::min(n, inv->slots[i].count);
        if (!Drop(inv, i, k).Ok()) break;
        ItemPickup::Spawn(scene, item, k, data, pos);
        dropped += k; n -= k;
    }
    return dropped;
}

bool SetSlot(Inventory* inv, int i, const std::string& item, int n) {
    if (!inv || i < 0 || i > 4096 || (inv->capacity > 0 && i >= inv->capacity)) return false;
    const long long cap = item.empty() ? 0 : std::min<long long>(n, inv->StackCap(item));
    if ((int)inv->slots.size() <= i) inv->slots.resize((std::size_t)i + 1);
    inv->slots[i] = (item.empty() || cap <= 0) ? Inventory::Slot{} : Inventory::Slot{item, (int)cap, {}};
    TrimTail(*inv);
    inv->NotifyChanged();
    return true;
}

int RollLoot(const std::string& table, GameObject* target) {
    if (table.empty() || !target) return 0;
    Inventory* inv = ItemPickup::InventoryOf(target);
    auto* grid = target->GetComponent<GridInventory>();
    int n = 0;
    for (const auto& s : LootTable::Cached(table).Roll(Random::Shared())) {
        n += s.count;
        int left = s.count;
        if (inv) left = inv->AddPartial(s.item, left);
        else if (grid) for (; left > 0 && grid->AddDefined(s.item, 1) >= 0; --left) {}
        if (left > 0 && target->scene() && target->transform)
            ItemPickup::Spawn(*target->scene(), s.item, left, {}, target->transform->Position());
    }
    return n;
}

bool EquipItem(GameObject* who, const std::string& slot, const std::string& item) {
    Equipment* eq = Equipment::Of(who);
    Inventory* inv = ItemPickup::InventoryOf(who);
    const int to = eq ? eq->Find(slot) : -1;
    if (!eq || !inv || to < 0) return false;
    for (int i = 0; i < (int)inv->slots.size(); ++i)
        if (!Inventory::Empty(inv->slots[i]) && inv->slots[i].item == item) return eq->EquipFrom(*inv, i, to).Ok();
    return false;
}

bool UnequipSlot(GameObject* who, const std::string& slot) {
    Equipment* eq = Equipment::Of(who);
    Inventory* inv = ItemPickup::InventoryOf(who);
    return eq && inv && eq->Unequip(eq->Find(slot), *inv).Ok();
}

InvResult Craft(Inventory* inv, const std::vector<Inventory::Stack>& take, const std::vector<Inventory::Stack>& give) {
    if (!inv) return InvResult::No("no inventory");
    for (const auto& t : take) if (inv->Count(t.item) < t.count) return InvResult::No("missing ingredients");
    if (!inv->Exchange(take, give)) return InvResult::No("no room for the result");
    int n = 0;
    for (const auto& g : give) n += g.count;
    return InvResult::Done(n);
}

InvResult Move(GridInventory* src, int index, GridInventory* dst, int x, int y, bool rotated) {
    if (!src || !dst || index < 0 || index >= (int)src->items.size()) return InvResult::No("bad item");
    const int before = src->items[index].count;
    const std::size_t srcCount = src->items.size();
    if (!src->MoveTo(index, *dst, x, y, rotated)) return InvResult::No("doesn't fit there");
    const InvResult r = src->items.size() < srcCount
        ? InvResult::Done(before)   // moved out / merged away
        : (src->items[index].count < before ? InvResult::Part(before - src->items[index].count, "stack full")
                                            : InvResult::Done(before));
    NetInventory::NoticeGridMove(src, index, dst, x, y, rotated, r.Ok());
    return r;
}

InvResult Split(GridInventory* inv, int index, int n) {
    if (!inv || inv->Split(index, n) < 0) return InvResult::No("no room to split");
    NetInventory::NoticeGridSplit(inv, index, n, true);
    return InvResult::Done(n);
}

InvResult ToGrid(Inventory* src, int si, GridInventory* dst, int x, int y, bool rotated, int amount) {
    if (!ValidSlot(src, si) || !dst) return InvResult::No("bad slot");
    if (amount < 0) return InvResult::No("bad amount");
    if (Inventory::Empty(src->slots[si])) return InvResult::No("empty slot");
    const Inventory::Slot slot = src->slots[si];
    const int have = slot.count;
    int n = (amount == 0 || amount >= have) ? have : amount;
    if (n < have && slot.data.uid != 0) return InvResult::No("unique items move whole");
    const ItemDefinition* def = ItemDatabase::Get().Def(slot.item);
    if (!def) return InvResult::No("unknown item");
    const int lim = ItemDatabase::Get().StackLimit(slot.item, dst->maxStack);
    if (lim > 0 && n > lim) { if (slot.data.uid != 0) return InvResult::No("stack too big for the grid"); n = lim; }
    // A scratch grid holds the lifted stack; the destination's own MoveTo decides where it
    // goes (bounds, overlap, merge, weight), so the rules are the grid's, not a copy of them.
    GridInventory tmp;
    const int side = std::max(1, std::max(def->gridW, def->gridH));
    tmp.cols = tmp.rows = side;
    tmp.maxStack = 0; tmp.enforceWeight = false; tmp.weightLimit = 0.0f;
    GridItem it{def->id, std::max(1, def->gridW), std::max(1, def->gridH), 0, 0, n, def->weight, slot.data};
    tmp.items.push_back(it);
    if (!tmp.MoveTo(0, *dst, x, y, rotated)) return InvResult::No("doesn't fit there");
    const int left = tmp.items.empty() ? 0 : tmp.items[0].count;
    const int moved = n - left;
    if (moved <= 0) return InvResult::No("doesn't fit there");
    src->slots[si].count -= moved;
    if (src->slots[si].count <= 0) src->slots[si] = Inventory::Slot{};
    TrimTail(*src);
    src->NotifyChanged();
    const int asked = (amount == 0 || amount >= have) ? have : amount;
    return moved < asked ? InvResult::Part(moved, "stack full") : InvResult::Done(moved);
}

InvResult FromGrid(GridInventory* src, int index, Inventory* dst, int di, int amount, int dstSlots) {
    if (!src || !dst || index < 0 || index >= (int)src->items.size() || di < 0) return InvResult::No("bad item");
    if (amount < 0) return InvResult::No("bad amount");
    const GridItem gi = src->items[index];
    if (gi.count < 1) return InvResult::No("bad item");
    const int n = (amount == 0 || amount >= gi.count) ? gi.count : amount;
    if (n < gi.count && gi.data.uid != 0) return InvResult::No("unique items move whole");
    if (di < (int)dst->slots.size() && !Inventory::Empty(dst->slots[di])) {
        Inventory::Slot probe; probe.item = gi.name; probe.count = 1; probe.data = gi.data;
        if (!Inventory::Stackable(dst->slots[di], probe)) return InvResult::No("different item there");
    }
    Inventory tmp;
    tmp.capacity = 1;
    Inventory::Slot lifted; lifted.item = gi.name; lifted.count = n; lifted.data = gi.data;
    tmp.slots.push_back(lifted);
    const InvResult r = MoveImpl(&tmp, 0, dst, di, 0, dstSlots, false);
    if (!r.Ok()) return r;
    src->items[index].count -= r.moved;
    if (src->items[index].count <= 0) src->RemoveAt(index);
    return r;
}

} // namespace InventoryOps
} // namespace okay
