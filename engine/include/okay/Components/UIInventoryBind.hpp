#pragma once
// Shows an Inventory (the engine's item bag) in a UI Slot Grid: each slot's label is the item in the
// same slot of the bag ("Axe", or "Potion 3" for a stack), empty slots are blank, and the grid grows to
// the bag's capacity. Click a slot (the grid selects it) and ask SelectedItem() what is in it.
//
// Put it on the object that has the UI Slot Grid. `inventoryName` names the object that holds the
// Inventory; empty means the object tagged "Player", else the first Inventory in the scene.
#include "okay/Components/Inventory.hpp"
#include "okay/Components/UISlotGrid.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <string>

namespace okay {

class UIInventoryBind final : public Behaviour {
public:
    std::string inventoryName;
    bool fitRows = true;           ///< the grid gets as many rows as the bag has room for
    float slotFontScale = 1.0f;    ///< label size on the slots (0 = leave the grid's own); smaller text fits longer item names

    /// The bag this grid shows, or nullptr when the scene has none.
    Inventory* FindInventory() const {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc) return nullptr;
        if (!inventoryName.empty()) {
            GameObject* g = sc->Find(inventoryName);
            return g ? g->GetComponent<Inventory>() : nullptr;
        }
        Inventory* first = nullptr;
        for (const auto& o : sc->Objects()) {
            auto* inv = o->GetComponent<Inventory>();
            if (!inv) continue;
            if (o->tag == "Player") return inv;
            if (!first) first = inv;
        }
        return first;
    }

    /// The label for one slot: the item, and the count after it for a stack. Cut to `maxChars` characters.
    static std::string SlotText(const Inventory::Slot& s, int maxChars) {
        if (Inventory::Empty(s)) return std::string();
        std::string count = s.count > 1 ? " " + std::to_string(s.count) : std::string();
        std::string name = s.item;
        const int room = maxChars - (int)count.size();
        if (room < 1) return name.substr(0, (std::size_t)(maxChars < 1 ? 1 : maxChars));
        if ((int)name.size() > room) name = name.substr(0, (std::size_t)(room > 1 ? room - 1 : 1)) + (room > 1 ? "." : "");
        return name + count;
    }

    /// The item in the selected slot ("" when nothing is selected or the slot is empty).
    std::string SelectedItem() const {
        auto* grid = gameObject ? gameObject->GetComponent<UISlotGrid>() : nullptr;
        Inventory* inv = FindInventory();
        if (!grid || !inv || grid->value < 0 || grid->value >= (int)inv->slots.size()) return std::string();
        const auto& s = inv->slots[(std::size_t)grid->value];
        return Inventory::Empty(s) ? std::string() : s.item;
    }
    int SelectedCount() const {
        auto* grid = gameObject ? gameObject->GetComponent<UISlotGrid>() : nullptr;
        Inventory* inv = FindInventory();
        if (!grid || !inv || grid->value < 0 || grid->value >= (int)inv->slots.size()) return 0;
        const auto& s = inv->slots[(std::size_t)grid->value];
        return Inventory::Empty(s) ? 0 : s.count;
    }

    void Update(float) override {
        auto* grid = gameObject ? gameObject->GetComponent<UISlotGrid>() : nullptr;
        Inventory* inv = FindInventory();
        if (!grid || !inv) return;
        const int cols = grid->columns < 1 ? 1 : grid->columns;
        if (fitRows && inv->capacity > 0) {
            const int rows = (inv->capacity + cols - 1) / cols;
            if (rows != grid->rows) grid->SetRows(rows);
        }
        if (slotFontScale > 0.0f) grid->fontScale = slotFontScale;
        grid->numberEmptySlots = false;                  // an empty place in the bag is blank, not a number
        int chars = (int)(grid->cell.x / (9.0f * grid->fontScale));   // 9 px per character at font scale 1
        if (chars < 3) chars = 3;
        for (int i = 0; i < grid->Count(); ++i) {
            const std::string t = i < (int)inv->slots.size() ? SlotText(inv->slots[(std::size_t)i], chars) : std::string();
            if (grid->Label(i) != t) grid->SetLabel(i, t);
        }
    }
};

} // namespace okay
