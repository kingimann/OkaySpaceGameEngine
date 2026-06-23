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

    int SlotsUsed() const { return (int)slots.size(); }
    bool IsFull() const { return SlotsUsed() >= capacity; }

    int Count(const std::string& item) const {
        for (const auto& s : slots) if (s.item == item) return s.count;
        return 0;
    }
    bool Has(const std::string& item, int n = 1) const { return Count(item) >= n; }

    /// Add `n` of an item: stacks onto an existing slot, else takes a new slot (if any
    /// remain). Returns false only when there's no room for a brand-new stack.
    bool Add(const std::string& item, int n = 1) {
        if (n <= 0 || item.empty()) return false;
        for (auto& s : slots)
            if (s.item == item) { s.count += n; Changed(); return true; }
        if (IsFull()) return false;
        slots.push_back({item, n});
        Changed();
        return true;
    }

    /// Remove up to `n` of an item; drops the slot when it empties. Returns true if
    /// the full amount was removed.
    bool Remove(const std::string& item, int n = 1) {
        if (n <= 0) return false;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (slots[i].item != item) continue;
            bool enough = slots[i].count >= n;
            slots[i].count -= n;
            if (slots[i].count <= 0) slots.erase(slots.begin() + i);
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
