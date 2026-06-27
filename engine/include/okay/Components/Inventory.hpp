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

    /// Slots may be FIXED-POSITION: an empty stack (item == "" or count <= 0) is a
    /// reusable hole, so the UI can drag an item to any slot and leave a gap behind.
    static bool Empty(const Slot& s) { return s.item.empty() || s.count <= 0; }
    int SlotsUsed() const { int n = 0; for (const auto& s : slots) if (!Empty(s)) ++n; return n; }
    bool IsFull() const { return SlotsUsed() >= capacity; }

    int Count(const std::string& item) const {
        for (const auto& s : slots) if (!Empty(s) && s.item == item) return s.count;
        return 0;
    }
    bool Has(const std::string& item, int n = 1) const { return Count(item) >= n; }

    /// Add `n` of an item: stacks onto an existing stack, else fills the first empty
    /// slot, else takes a new one. Returns false only when there's no room.
    bool Add(const std::string& item, int n = 1) {
        if (n <= 0 || item.empty()) return false;
        for (auto& s : slots)
            if (!Empty(s) && s.item == item) { s.count += n; Changed(); return true; }
        for (auto& s : slots)
            if (Empty(s)) { s.item = item; s.count = n; Changed(); return true; }   // reuse a hole
        if (IsFull()) return false;
        slots.push_back({item, n});
        Changed();
        return true;
    }

    /// Remove up to `n` of an item; the slot becomes an empty hole when it drains
    /// (positions stay stable). Returns true if the full amount was removed.
    bool Remove(const std::string& item, int n = 1) {
        if (n <= 0) return false;
        for (auto& s : slots) {
            if (Empty(s) || s.item != item) continue;
            bool enough = s.count >= n;
            s.count -= n;
            if (s.count <= 0) { s.item.clear(); s.count = 0; }
            Changed();
            return enough;
        }
        return false;
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
