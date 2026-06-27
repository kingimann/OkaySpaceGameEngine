#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/InventoryUI.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Render/Color.hpp"
#include <string>

namespace okay {

/// A lootable container — chest, crate, barrel. Its contents live in a sibling
/// Inventory (fill it in the editor with the items it should hold). Walk up to it
/// and press the open key to show a panel: the chest's items on top, your own
/// inventory below. Click a stack to move it to the other side — take loot out, or
/// stash items in. The player inventory is found automatically (the scene's
/// InventoryUI). Pair it with an Inventory and you have a working chest, no script.
class ChestInventory : public Behaviour {
public:
    bool        open    = false;
    char        openKey = 'f';          ///< open/close when in range (default F to avoid the E backpack)
    float       range   = 4.0f;         ///< how close the player must be (0 = open from anywhere)
    std::string title   = "Chest";
    int         cols     = 6;           ///< chest grid columns

    // ---- Style ----
    float slotSize = 46.0f, gap = 4.0f, cornerRadius = 4.0f;
    std::string iconFolder = "textures/items/";
    Color panelColor = Color::FromBytes(18, 19, 26, 240);
    Color titleBar   = Color::FromBytes(46, 50, 64, 255);
    Color slotColor  = Color::FromBytes(34, 36, 46, 230);
    Color slotBorder = Color::FromBytes(70, 72, 84, 255);
    Color textColor  = Color::FromBytes(240, 240, 245, 255);
    Color hoverColor = Color::FromBytes(255, 255, 255, 36);
    bool  darkenWhenOpen = true;

    /// The chest's own contents (a sibling Inventory).
    Inventory* Inv() const {
        if (gameObject) if (auto* in = gameObject->GetComponent<Inventory>()) return in;
        return nullptr;
    }
    /// The player's inventory: the first InventoryUI's Inventory in the scene.
    Inventory* PlayerInv() const {
        Scene* s = GetScene();
        if (!s) return nullptr;
        for (const auto& o : s->Objects())
            if (o) if (auto* iu = o->GetComponent<InventoryUI>()) { if (auto* in = iu->Inv()) return in; }
        return nullptr;
    }

    /// Move the whole stack in chest slot `i` into the player's inventory.
    bool TakeToPlayer(int i) {
        Inventory* c = Inv(); Inventory* p = PlayerInv();
        if (!c || !p || i < 0 || i >= (int)c->slots.size()) return false;
        Inventory::Slot& s = c->slots[i];
        if (Inventory::Empty(s)) return false;
        if (p->Add(s.item, s.count)) { s.item.clear(); s.count = 0; return true; }
        return false;       // player inventory full
    }
    /// Move the whole stack in player slot `i` into the chest.
    bool StashFromPlayer(int i) {
        Inventory* c = Inv(); Inventory* p = PlayerInv();
        if (!c || !p || i < 0 || i >= (int)p->slots.size()) return false;
        Inventory::Slot& s = p->slots[i];
        if (Inventory::Empty(s)) return false;
        if (c->Add(s.item, s.count)) { s.item.clear(); s.count = 0; return true; }
        return false;       // chest full
    }

    /// Is the player (the InventoryUI owner's root) within `range` of this chest?
    bool PlayerInRange() const {
        if (range <= 0.0f) return true;
        Scene* s = GetScene();
        if (!s || !gameObject || !gameObject->transform) return true;
        GameObject* player = nullptr;
        for (const auto& o : s->Objects())
            if (o && o->GetComponent<InventoryUI>()) {
                Transform* t = o->transform;
                while (t && t->Parent()) t = t->Parent();
                player = (t && t->gameObject) ? t->gameObject : o.get();
                break;
            }
        if (!player || !player->transform) return true;
        Vec3 a = gameObject->transform->Position(), b = player->transform->Position();
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return (dx * dx + dy * dy + dz * dz) <= range * range;
    }

    void Update(float) override {
        if (openKey && Input::GetKeyDown(openKey) && PlayerInRange()) open = !open;
        if (open && !PlayerInRange()) open = false;   // auto-close when you walk away
    }
};

} // namespace okay
