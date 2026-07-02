#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/InventoryUI.hpp"
#include "okay/Components/Camera.hpp"
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
    float       range   = 0.0f;         ///< how close the player must be (0 = open from anywhere; set >0 for proximity)
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

    // ---- Runtime drag state (driven by the renderer's mouse handling) ----
    int   dragSide  = -1;   ///< 0 = chest grid, 1 = player grid, -1 = not dragging
    int   dragIndex = -1;   ///< slot being dragged

    /// Move (or swap/merge) the stack in src slot `si` into dst slot `di`. Works within
    /// one grid or across the chest<->player grids (drag-and-drop, place where you want).
    static void MoveStack(Inventory* src, int si, Inventory* dst, int di) {
        if (!src || !dst || si < 0 || di < 0 || si >= (int)src->slots.size()) return;
        if (src == dst && si == di) return;
        int cap = dst->capacity > 0 ? dst->capacity : di + 1;
        if (di >= cap) return;
        while ((int)dst->slots.size() <= di) dst->slots.push_back(Inventory::Slot{});
        Inventory::Slot& a = src->slots[si];
        Inventory::Slot& b = dst->slots[di];
        if (Inventory::Empty(a)) return;
        if (Inventory::Empty(b)) { b = a; a = Inventory::Slot{}; }
        else if (b.item == a.item) { b.count += a.count; a = Inventory::Slot{}; }   // merge
        else { std::swap(a, b); }                                                   // swap
    }

    /// The chest's own contents (a sibling Inventory).
    Inventory* Inv() const {
        if (gameObject) if (auto* in = gameObject->GetComponent<Inventory>()) return in;
        return nullptr;
    }
    /// The player's inventory UI (for its own slot size / columns when drawn in the chest).
    InventoryUI* PlayerUI() const {
        Scene* s = GetScene();
        if (!s) return nullptr;
        for (const auto& o : s->Objects())
            if (o) if (auto* iu = o->GetComponent<InventoryUI>()) return iu;
        return nullptr;
    }
    /// The player's inventory: the first InventoryUI's Inventory in the scene.
    Inventory* PlayerInv() const {
        InventoryUI* iu = PlayerUI();
        return iu ? iu->Inv() : nullptr;
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

    /// Is the player within `range` of this chest? Uses the main camera's world position
    /// as the player's location (works across the FPS/third-person templates); falls back
    /// to "in range" if there's no camera, so the key always works.
    bool PlayerInRange() const {
        if (range <= 0.0f) return true;
        Scene* s = GetScene();
        if (!s || !gameObject || !gameObject->transform) return true;
        Transform* camT = nullptr;
        for (const auto& o : s->Objects())
            if (o && o->GetComponent<Camera>() && o->transform) { camT = o->transform; break; }
        if (!camT) return true;
        Vec3 a = gameObject->transform->Position(), b = camT->Position();
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return (dx * dx + dy * dy + dz * dz) <= range * range;
    }

    void Update(float) override {
        if (openKey && Input::GetKeyDown(openKey) && PlayerInRange()) open = !open;
        if (open && !PlayerInRange()) open = false;   // auto-close when you walk away
    }
};

} // namespace okay
