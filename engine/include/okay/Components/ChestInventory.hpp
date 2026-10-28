#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/InventoryUI.hpp"
#include "okay/Inventory/InventoryOps.hpp"
#include "okay/Components/InventoryPlayer.hpp"
#include "okay/Render/Color.hpp"
#include <string>

namespace okay {

/// A lootable container — chest, crate, barrel. Its contents live in a sibling
/// Inventory (fill it in the editor with the items it should hold). Walk up to it
/// and press the open key to show a panel: the chest's items on top, your own
/// inventory below. Click a stack to move it to the other side — take loot out, or
/// stash items in. The player is found with FindInventoryPlayer (playerName, the
/// "Player" tag, the main camera's subject). Pair it with an Inventory and you have a
/// working chest, no script. Only the nearest chest in range opens (`range`, `open`).
class ChestInventory : public InventoryInteractable {
public:
    char        openKey = 'f';          ///< open/close when in range (default F to avoid the E backpack)
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
    float scroll = 0;       ///< pixels the grids are scrolled, when they don't fit the screen

    /// Move (or swap/merge) the stack in src slot `si` into dst slot `di`. Works within
    /// one grid or across the chest<->player grids (drag-and-drop, place where you want).
    static void MoveStack(Inventory* src, int si, Inventory* dst, int di) { InventoryOps::Move(src, si, dst, di); }

    /// The chest's own contents (a sibling Inventory).
    Inventory* Inv() const {
        if (gameObject) if (auto* in = gameObject->GetComponent<Inventory>()) return in;
        return nullptr;
    }
    char InteractKey() const override { return openKey; }

    /// The player's inventory: the Inventory on the player (FindInventoryPlayer), else
    /// the one the scene's first InventoryUI shows.
    Inventory* PlayerInv() const {
        if (GameObject* p = FindInventoryPlayer(GetScene(), playerName))
            if (auto* in = p->GetComponent<Inventory>()) return in;
        Scene* s = GetScene();
        if (!s) return nullptr;
        for (const auto& o : s->Objects())
            if (o) if (auto* iu = o->GetComponent<InventoryUI>()) if (Inventory* in = iu->Inv()) return in;
        return nullptr;
    }
    /// The inventory UI showing PlayerInv() (for its slot size / columns in the chest).
    InventoryUI* PlayerUI() const {
        Scene* s = GetScene();
        Inventory* pinv = PlayerInv();
        if (!s || !pinv) return nullptr;
        for (const auto& o : s->Objects())
            if (o) if (auto* iu = o->GetComponent<InventoryUI>()) if (iu->Inv() == pinv) return iu;
        return nullptr;
    }

    /// Move the stack in chest slot `i` into the player's inventory (as much as fits).
    bool TakeToPlayer(int i) {
        Inventory* c = Inv(); Inventory* p = PlayerInv();
        if (!c || !p || i < 0 || i >= (int)c->slots.size()) return false;
        return Transfer(c, i, p);
    }
    /// Move the stack in player slot `i` into the chest (as much as fits).
    bool StashFromPlayer(int i) {
        Inventory* c = Inv(); Inventory* p = PlayerInv();
        if (!c || !p || i < 0 || i >= (int)p->slots.size()) return false;
        return Transfer(p, i, c);
    }

    /// Move as much of `from` slot `i` into `to` as fits; only the leftover stays in
    /// the source slot (never duplicated, never lost). False if nothing fit.
    static bool Transfer(Inventory* from, int i, Inventory* to) { return InventoryOps::Transfer(from, i, to).Ok(); }

    /// Move every chest stack into the player's bag, as much as fits.
    int TakeAll() { return InventoryOps::TransferAll(Inv(), PlayerInv()); }
    /// Move player stacks that this chest already holds into it (quick-stack).
    int StashMatching() { return InventoryOps::TransferMatching(PlayerInv(), Inv()); }
    /// Compact the chest and order it by display name. Never drops items.
    bool SortContents() { return InventoryOps::SortByName(Inv()); }

    void Update(float dt) override {
        const bool was = open;
        InventoryInteractable::Update(dt);
        if (was && !open) { dragSide = -1; dragIndex = -1; scroll = 0; }   // a close mid-drag must not linger
    }
};

} // namespace okay
