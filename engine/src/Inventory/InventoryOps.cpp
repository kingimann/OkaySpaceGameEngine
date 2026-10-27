#include "okay/Inventory/InventoryOps.hpp"
#include "okay/Components/BuildMode.hpp"
#include "okay/Components/Consumables.hpp"
#include "okay/Components/Equipment.hpp"
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

InvResult Move(Inventory* src, int si, Inventory* dst, int di, int amount, int dstSlots) {
    if (!ValidSlot(src, si) || !dst || di < 0) return InvResult::No("bad slot");
    if (src == dst && si == di) return InvResult::No("same slot");
    const int limit = dstSlots > 0 ? dstSlots : UsableSlots(*dst);
    if (limit > 0 && di >= limit) return InvResult::No("past the last slot");
    if (Inventory::Empty(src->slots[si])) return InvResult::No("empty slot");
    const int have = src->slots[si].count;
    const int n = (amount <= 0 || amount >= have) ? have : amount;
    while ((int)dst->slots.size() <= di) dst->slots.push_back(Inventory::Slot{});
    Inventory::Slot& a = src->slots[si];   // (after the grow: src may be dst)
    Inventory::Slot& b = dst->slots[di];
    int moved = 0;
    if (Inventory::Empty(b)) {
        b = a; b.count = n; a.count -= n; moved = n;
    } else if (Inventory::Stackable(b, a)) {
        const long long room = dst->StackCap(b.item) - b.count;
        moved = (int)std::min<long long>(n, room > 0 ? room : 0);
        if (moved <= 0) { TrimTail(*dst); return InvResult::No("stack full"); }
        b.count += moved; a.count -= moved;
    } else {
        if (n != have) { TrimTail(*dst); return InvResult::No("different item there"); }
        std::swap(a, b); moved = n;
    }
    if (a.count <= 0) a = Inventory::Slot{};
    TrimTail(*src); TrimTail(*dst);
    Notify(src, dst);
    return moved < n ? InvResult::Part(moved, "stack full") : InvResult::Done(moved);
}

InvResult Split(Inventory* inv, int i, int amount, int slots) {
    if (!ValidSlot(inv, i) || Inventory::Empty(inv->slots[i])) return InvResult::No("empty slot");
    if (amount < 1 || amount >= inv->slots[i].count) return InvResult::No("nothing to split");
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
    return InvResult::Done(amount);
}

InvResult QuickMove(Inventory* inv, int from, int hotbar, int slots) {
    if (!ValidSlot(inv, from) || Inventory::Empty(inv->slots[from])) return InvResult::No("empty slot");
    const int n = hotbar < 1 ? 1 : hotbar, total = UsableSlots(*inv, slots);
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
    return rest ? InvResult::Part(moved, "no room for the rest") : InvResult::Done(moved);
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
        std::swap(to->slots[hole], from->slots[i]);
        TrimTail(*from);
        Notify(from, to);
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
    return left > 0 ? InvResult::Part(have - left, "destination full") : InvResult::Done(have);
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
    return InvResult::Done(take);
}

InvResult Use(Inventory* inv, int i, GameObject* user) {
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
    if (src->items.size() < srcCount) return InvResult::Done(before);   // moved out / merged away
    const int left = src->items[index].count;
    return left < before ? InvResult::Part(before - left, "stack full") : InvResult::Done(before);
}

InvResult Split(GridInventory* inv, int index, int n) {
    if (!inv || inv->Split(index, n) < 0) return InvResult::No("no room to split");
    return InvResult::Done(n);
}

} // namespace InventoryOps
} // namespace okay
