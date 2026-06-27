#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Render/Color.hpp"
#include <string>
#include <utility>

namespace okay {

/// A Minecraft-style inventory UI: a hotbar along the bottom with a highlighted
/// selected slot, plus a backpack grid you open with a key. It draws itself (player
/// + editor Play view) from a sibling/owner Inventory component, so you just drop it
/// in — pair it with an Inventory and you have a working bag + hotbar. Number keys
/// 1–9 and the mouse wheel pick the hotbar slot; SelectedItem() tells gameplay what's
/// in hand (e.g. feed it to a BlockBuilder/StructureBuilder).
class InventoryUI : public Behaviour {
public:
    int   hotbarSlots  = 9;       ///< slots on the bottom bar
    int   backpackRows = 3;       ///< extra rows shown when open (backpack = rows × hotbarSlots)
    int   selected     = 0;       ///< selected hotbar slot
    bool  open         = false;   ///< backpack grid visible
    char  toggleKey    = 'e';     ///< open / close the backpack
    bool  selectHotkeys = true;   ///< 1–9 select a hotbar slot
    bool  scrollSelect  = true;   ///< mouse wheel cycles the selection
    bool  dragItems     = true;   ///< drag items between slots while the backpack is open
    int   dragIndex     = -1;     ///< slot being dragged (runtime), or -1

    float slotSize = 50.0f;       ///< slot square, pixels
    float slotGap  = 6.0f;
    std::string iconFolder = "textures/items/";   ///< <folder><item>.png icons (optional; falls back to the name)
    Color slotColor     = Color::FromBytes(28, 30, 38, 210);
    Color slotBorder    = Color::FromBytes(70, 72, 84, 255);
    Color selectedColor = Color::FromBytes(255, 255, 255, 255);
    Color textColor     = Color::FromBytes(240, 240, 245, 255);
    Color countColor    = Color::FromBytes(255, 255, 255, 255);
    Color panelColor    = Color::FromBytes(18, 19, 26, 240);
    bool  darkenWhenOpen = true;

    // ---- Layout / style customization ----
    float marginX        = 0.0f;    ///< horizontal nudge from centre (px)
    float marginY        = 16.0f;   ///< distance from the screen edge (px)
    bool  anchorTop      = false;   ///< dock the hotbar to the top instead of the bottom
    float cornerRadius   = 4.0f;    ///< rounded slot corners (0 = square)
    float borderWidth    = 2.0f;    ///< slot border thickness (px); selected adds 1
    float labelScale     = 1.0f;    ///< multiplier on item-name / count text size
    bool  showCounts     = true;    ///< draw stack counts
    bool  showNames      = true;    ///< draw the item name when there's no icon
    bool  showPanel      = false;   ///< a rounded background panel behind the slots
    float panelPad       = 8.0f;    ///< panel padding around the slots (px)
    Color hoverColor     = Color::FromBytes(255, 255, 255, 36);  ///< tint on the slot under the cursor
    bool  showSelectedName = false; ///< Minecraft-style: held item's name above the hotbar
    int   nameChars      = 6;       ///< how many characters of the item name to show in a slot

    /// The Inventory this UI shows: on the same object, else on the player root.
    Inventory* Inv() const {
        if (gameObject)
            if (auto* in = gameObject->GetComponent<Inventory>()) return in;
        GameObject* o = Owner();
        return o ? o->GetComponent<Inventory>() : nullptr;
    }
    /// Item name in the selected hotbar slot ("" if empty) — what's "in hand".
    std::string SelectedItem() const {
        Inventory* inv = Inv();
        if (!inv || selected < 0 || selected >= (int)inv->slots.size()) return "";
        return inv->slots[selected].item;
    }
    int SelectedCount() const {
        Inventory* inv = Inv();
        if (!inv || selected < 0 || selected >= (int)inv->slots.size()) return 0;
        return inv->slots[selected].count;
    }

    /// Drag a stack from slot `from` onto slot `to`: swap, merge identical stacks, or
    /// (dropping past the used slots) send it to the end. Operates on the Inventory's
    /// stack list, so hotbar (0..hotbar-1) and backpack slots reorder freely.
    void MoveSlot(int from, int to) {
        Inventory* inv = Inv();
        if (!inv || from < 0 || to < 0 || from == to) return;
        if (from >= (int)inv->slots.size()) return;
        // Grow the list so the (possibly empty) target slot exists, so you can drop an
        // item into any open slot and leave a gap behind.
        while ((int)inv->slots.size() <= to) inv->slots.push_back(Inventory::Slot{});
        Inventory::Slot& a = inv->slots[from];
        Inventory::Slot& b = inv->slots[to];
        if (!Inventory::Empty(a) && !Inventory::Empty(b) && a.item == b.item) {
            b.count += a.count; a.item.clear(); a.count = 0;      // merge identical stacks
        } else {
            std::swap(a, b);                                      // swap (target may be empty)
        }
        while (!inv->slots.empty() && Inventory::Empty(inv->slots.back())) inv->slots.pop_back();
    }

    void Update(float) override {
        if (selectHotkeys) {
            for (int k = 0; k < hotbarSlots && k < 9; ++k)
                if (Input::GetKeyDown((char)('1' + k))) selected = k;
        }
        if (scrollSelect && hotbarSlots > 0) {
            float w = Input::MouseWheel();
            if (w > 0.5f)      selected = (selected - 1 + hotbarSlots) % hotbarSlots;
            else if (w < -0.5f) selected = (selected + 1) % hotbarSlots;
        }
        if (toggleKey && Input::GetKeyDown(toggleKey)) open = !open;
        if (selected < 0) selected = 0;
        if (selected >= hotbarSlots) selected = hotbarSlots - 1;
    }

private:
    GameObject* Owner() const {
        if (!gameObject || !gameObject->transform) return gameObject;
        Transform* t = gameObject->transform;
        while (t->Parent()) t = t->Parent();
        return t->gameObject ? t->gameObject : gameObject;
    }
};

} // namespace okay
