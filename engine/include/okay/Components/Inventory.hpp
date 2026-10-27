#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include <string>
#include <vector>

namespace okay {

/// A simple stacked item inventory (RPG bags, hotbars, loot). Items are referenced by
/// their ItemDatabase id and stack; a fixed number of `capacity` slots limits how many
/// distinct stacks fit. Add/Remove fire the sibling ScriptComponent's on_inventory_changed() so UI can
/// refresh. Pair with a Stats component for a basic RPG character.
class Inventory : public Behaviour {
public:
    struct Slot {
        std::string item;          ///< ItemDatabase id
        int count = 0;
        ItemInstanceData data;     ///< durability / uid / custom values (empty for plain items)
    };

    std::vector<Slot> slots;
    int capacity = 20;            // max distinct stacks
    int maxStack = 0;             // container-wide cap per stack (0 = none; the item definition's stack size still applies)
    float weightLimit = 0.0f;     // carry limit from the item definitions' weights (0 = none)
    bool  slowWhenOverWeight = false;   // controllers move at overWeightSpeed while over the limit
    float overWeightSpeed = 0.5f;
    /// Carry the contents into the next scene loaded with load_scene: the object with
    /// the same name there (usually the player) gets them.
    bool keepOnSceneLoad = false;
    /// Runtime: weight worn in a sibling Equipment, counted toward weightLimit (not saved).
    float wornWeight = 0.0f;

    /// Slots may be FIXED-POSITION: an empty stack (item == "" or count <= 0) is a
    /// reusable hole, so the UI can drag an item to any slot and leave a gap behind.
    static bool Empty(const Slot& s) { return s.item.empty() || s.count <= 0; }
    /// Can stack `b` merge into `a`? Same item and the same instance data.
    static bool Stackable(const Slot& a, const Slot& b) { return a.item == b.item && a.data.StacksWith(b.data); }
    /// Per-stack limit for `item` here: the smaller of maxStack and the item's own
    /// stack size (INT_MAX when neither is set, so counts never overflow).
    long long StackCap(const std::string& item) const {
        const int lim = ItemDatabase::Get().StackLimit(item, maxStack);
        return lim > 0 ? lim : 2147483647LL;
    }
    int SlotsUsed() const { int n = 0; for (const auto& s : slots) if (!Empty(s)) ++n; return n; }
    bool IsFull() const { return SlotsUsed() >= capacity; }

    int Count(const std::string& item) const {
        int total = 0;   // sum across stacks (an item may span several once maxStack splits it)
        for (const auto& s : slots) if (!Empty(s) && s.item == item) total += s.count;
        return total;
    }
    bool Has(const std::string& item, int n = 1) const { return Count(item) >= n; }

    /// Weight carried: each stack's count × its item definition's weight.
    float TotalWeight() const {
        float w = wornWeight;
        for (const auto& s : slots)
            if (!Empty(s)) if (const ItemDefinition* d = ItemDatabase::Get().Find(s.item)) w += d->weight * s.count;
        return w;
    }
    bool OverWeight() const { return weightLimit > 0.0f && TotalWeight() > weightLimit + 1e-4f; }
    /// What a controller multiplies its speed by (1 unless slowWhenOverWeight and over).
    float SpeedFactor() const { return slowWhenOverWeight && OverWeight() ? overWeightSpeed : 1.0f; }
    /// SpeedFactor of the Inventory on `go` (1 when it has none) — the controllers' hook.
    static float SpeedFactorOf(const GameObject* go) {
        const Inventory* inv = go ? go->GetComponent<Inventory>() : nullptr;
        return inv ? inv->SpeedFactor() : 1.0f;
    }

    /// How many of `n` items would fit right now (a dry run; nothing changes).
    int CanAdd(const std::string& item, int n) const {
        if (n <= 0 || item.empty()) return 0;
        const long long cap = StackCap(item);
        long long need = n, fit = 0;
        for (const auto& s : slots) {                 // room in existing plain stacks
            if (need <= 0) break;
            if (Empty(s) || s.item != item || !s.data.Empty()) continue;
            long long room = cap - s.count; if (room <= 0) continue;
            long long t = need < room ? need : room; fit += t; need -= t;
        }
        int used = SlotsUsed(), holes = 0;
        for (const auto& s : slots) if (Empty(s)) ++holes;
        while (need > 0) {                            // then holes, then new slots up to capacity
            if (holes > 0) --holes;
            else if (used >= capacity) break;
            ++used;
            long long put = need < cap ? need : cap; fit += put; need -= put;
        }
        return (int)fit;
    }

    /// Add as many of `n` as fit (tops up existing stacks, then holes / new slots,
    /// splitting by maxStack). Returns the LEFTOVER that didn't fit (0 = all added).
    int AddPartial(const std::string& item, int n) {
        int leftover = AddRaw(item, n);
        if (leftover != n) NotifyChanged();
        return leftover < 0 ? 0 : leftover;
    }

    /// Add `n` of an item — all or nothing: if they don't all fit, nothing changes
    /// and it returns false. (AddPartial adds what fits and reports the rest.)
    bool Add(const std::string& item, int n = 1) {
        if (n <= 0 || item.empty() || CanAdd(item, n) < n) return false;
        AddRaw(item, n);
        NotifyChanged();
        return true;
    }

    /// Remove up to `n` of an item; the slot becomes an empty hole when it drains
    /// (positions stay stable). Returns true if the full amount was removed.
    bool Remove(const std::string& item, int n = 1) {
        if (n <= 0) return false;
        int need = RemoveRaw(item, n);
        if (need != n) NotifyChanged();
        return need <= 0;
    }

    struct Stack { std::string item; int count = 0; };
    /// A recipe's inputs as Stacks, with repeated items summed ({wood,1},{wood,1}
    /// needs 2 wood). Works for any {item, count} ingredient type.
    template <class Ing>
    static std::vector<Stack> SumStacks(const std::vector<Ing>& inputs) {
        std::vector<Stack> t;
        for (const auto& in : inputs) {
            if (in.item.empty() || in.count <= 0) continue;
            bool merged = false;
            for (auto& x : t) if (x.item == in.item) { x.count += in.count; merged = true; break; }
            if (!merged) t.push_back({in.item, in.count});
        }
        return t;
    }
    /// Atomically remove `take` and add `give` (crafting, trades): either every
    /// removal and addition succeeds, or nothing changes and it returns false.
    /// Repeated items in a list are summed (two "wood x1" lines need 2 wood).
    bool Exchange(const std::vector<Stack>& take, const std::vector<Stack>& give) {
        const std::vector<Slot> saved = slots;
        bool ok = true;
        for (const auto& t : take) if (t.count > 0 && RemoveRaw(t.item, t.count) > 0) { ok = false; break; }
        if (ok) for (const auto& g : give) if (g.count > 0 && AddRaw(g.item, g.count) > 0) { ok = false; break; }
        if (!ok) { slots = saved; return false; }
        NotifyChanged();
        return true;
    }

    void Clear() { slots.clear(); NotifyChanged(); }

    /// Fire the sibling script's on_inventory_changed(). Every mutator calls this,
    /// including the UI's drag / split / sort / chest moves.
    /// Fires on_encumbered(true/false) too when going over / back under weightLimit.
    void NotifyChanged() {
        const bool over = OverWeight();
        const bool flipped = over != m_wasOver;
        m_wasOver = over;
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) {
                    sc->VM()->CallEvent("on_inventory_changed");
                    if (flipped) sc->VM()->CallEventWith("on_encumbered", vs::VsValue(over));
                }
    }

private:
    bool m_wasOver = false;

    /// Add up to `n` without notifying; returns the leftover that didn't fit.
    int AddRaw(const std::string& item, int n) {
        if (n <= 0 || item.empty()) return n < 0 ? 0 : n;
        const long long cap = StackCap(item);
        for (auto& s : slots) {                     // 1) top up existing plain stacks
            if (n <= 0) break;
            if (Empty(s) || s.item != item || !s.data.Empty()) continue;
            long long room = cap - s.count; if (room <= 0) continue;
            int add = (long long)n < room ? n : (int)room;
            s.count += add; n -= add;
        }
        while (n > 0) {                             // 2) holes, then new slots, split by cap
            int put = (long long)n > cap ? (int)cap : n;
            Slot* dst = nullptr;
            for (auto& s : slots) if (Empty(s)) { dst = &s; break; }   // reuse a hole
            if (!dst) {
                if (IsFull()) break;
                slots.push_back(Slot{}); dst = &slots.back();
            }
            dst->item = item; dst->count = put; dst->data = ItemInstanceData{}; n -= put;
        }
        return n;
    }
    /// Remove up to `n` without notifying; returns how many were still missing.
    int RemoveRaw(const std::string& item, int n) {
        for (auto& s : slots) {                     // drain across stacks (holes stay put)
            if (n <= 0) break;
            if (Empty(s) || s.item != item) continue;
            int take = s.count < n ? s.count : n;
            s.count -= take; n -= take;
            if (s.count <= 0) s = Slot{};
        }
        return n;
    }
};

} // namespace okay
