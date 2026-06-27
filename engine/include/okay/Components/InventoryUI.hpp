#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Render/Color.hpp"
#include <string>

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

    float slotSize = 50.0f;       ///< slot square, pixels
    float slotGap  = 6.0f;
    std::string iconFolder = "textures/items/";   ///< <folder><item>.png icons (optional; falls back to the name)
    Color slotColor     = Color::FromBytes(28, 30, 38, 210);
    Color slotBorder    = Color::FromBytes(70, 72, 84, 255);
    Color selectedColor = Color::FromBytes(255, 255, 255, 255);
    Color textColor     = Color::FromBytes(240, 240, 245, 255);
    Color panelColor    = Color::FromBytes(18, 19, 26, 240);
    bool  darkenWhenOpen = true;

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
