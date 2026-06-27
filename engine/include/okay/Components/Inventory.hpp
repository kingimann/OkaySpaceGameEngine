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

    /// Add `n` of an item: tops up existing stacks (respecting maxStack), then fills
    /// empty holes / new slots, splitting into multiple stacks when maxStack is set.
    /// Returns false if some couldn't fit (no room left).
    bool Add(const std::string& item, int n = 1) {
        if (n <= 0 || item.empty()) return false;
        const int cap = maxStack > 0 ? maxStack : 0;   // 0 = unlimited
        bool changed = false;
        // 1) Top up existing stacks of this item.
        for (auto& s : slots) {
            if (n <= 0) break;
            if (Empty(s) || s.item != item) continue;
            int room = cap > 0 ? cap - s.count : n;
            if (room <= 0) continue;
            int add = n < room ? n : room;
            s.count += add; n -= add; changed = true;
        }
        // 2) Put the remainder into empty holes / new slots, splitting by cap.
        while (n > 0) {
            int put = (cap > 0 && n > cap) ? cap : n;
            Slot* dst = nullptr;
            for (auto& s : slots) if (Empty(s)) { dst = &s; break; }   // reuse a hole
            if (!dst) {
                if (IsFull()) { if (changed) Changed(); return false; }
                slots.push_back(Slot{}); dst = &slots.back();
            }
            dst->item = item; dst->count = put; n -= put; changed = true;
        }
        if (changed) Changed();
        return n <= 0;
    }

    /// Remove up to `n` of an item; the slot becomes an empty hole when it drains
    /// (positions stay stable). Returns true if the full amount was removed.
    bool Remove(const std::string& item, int n = 1) {
        if (n <= 0) return false;
        int need = n; bool changed = false;
        for (auto& s : slots) {                 // drain across stacks (holes stay put)
            if (need <= 0) break;
            if (Empty(s) || s.item != item) continue;
            int take = s.count < need ? s.count : need;
            s.count -= take; need -= take; changed = true;
            if (s.count <= 0) { s.item.clear(); s.count = 0; }
        }
        if (changed) Changed();
        return need <= 0;
    }

    void Clear() { slots.clear(); Changed(); }

private:
    void Changed() {
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_inventory_changed");
    }
};

} // namespace okay
