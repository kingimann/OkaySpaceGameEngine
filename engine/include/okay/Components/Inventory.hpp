#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include <string>
#include <vector>

namespace okay {

/// A simple stacked item inventory (RPG bags, hotbars, loot). Items are referenced by
/// name and stack; a fixed number of `capacity` slots limits how many distinct stacks
/// fit. Add/Remove fire the sibling ScriptComponent's on_inventory_changed() so UI can
/// refresh. Pair with a Stats component for a basic RPG character.
class Inventory : public Behaviour {
public:
    struct Slot { std::string item; int count = 0; };

    std::vector<Slot> slots;
    int capacity = 20;            // max distinct stacks
    int maxStack = 0;             // max items per stack (0 = unlimited); Add() overflows

    /// Slots may be FIXED-POSITION: an empty stack (item == "" or count <= 0) is a
    /// reusable hole, so the UI can drag an item to any slot and leave a gap behind.
    static bool Empty(const Slot& s) { return s.item.empty() || s.count <= 0; }
    int SlotsUsed() const { int n = 0; for (const auto& s : slots) if (!Empty(s)) ++n; return n; }
    bool IsFull() const { return SlotsUsed() >= capacity; }

    int Count(const std::string& item) const {
        int total = 0;   // sum across stacks (an item may span several once maxStack splits it)
        for (const auto& s : slots) if (!Empty(s) && s.item == item) total += s.count;
        return total;
    }
    bool Has(const std::string& item, int n = 1) const { return Count(item) >= n; }

    /// How many of `n` items would fit right now (a dry run; nothing changes).
    int CanAdd(const std::string& item, int n) const {
        if (n <= 0 || item.empty()) return 0;
        const long long cap = StackCap();
        long long need = n, fit = 0;
        for (const auto& s : slots) {                 // room in existing stacks
            if (need <= 0) break;
            if (Empty(s) || s.item != item) continue;
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
    void NotifyChanged() {
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_inventory_changed");
    }

private:
    /// Per-stack limit: maxStack, or (unlimited) INT_MAX so counts never overflow.
    long long StackCap() const { return maxStack > 0 ? maxStack : 2147483647LL; }

    /// Add up to `n` without notifying; returns the leftover that didn't fit.
    int AddRaw(const std::string& item, int n) {
        if (n <= 0 || item.empty()) return n < 0 ? 0 : n;
        const long long cap = StackCap();
        for (auto& s : slots) {                     // 1) top up existing stacks
            if (n <= 0) break;
            if (Empty(s) || s.item != item) continue;
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
            dst->item = item; dst->count = put; n -= put;
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
            if (s.count <= 0) { s.item.clear(); s.count = 0; }
        }
        return n;
    }
};

} // namespace okay
