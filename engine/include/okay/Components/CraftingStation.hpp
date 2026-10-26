#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/InventoryPlayer.hpp"
#include "okay/Inventory/InventoryOps.hpp"
#include "okay/Inventory/Recipe.hpp"
#include "okay/Render/Color.hpp"
#include <string>
#include <vector>

namespace okay {

/// A crafting bench: the same recipes as Crafting (okay::Recipe), crafted from a
/// sibling Inventory, else the player's. It draws its own panel (player + editor Play
/// view) with a progress bar for recipes that take time; click a row to craft one,
/// shift-click to craft as many as you can afford. Walk up and press the key — only the
/// nearest bench in `range` opens. With a `stationTag`, it also offers every recipe in
/// its `recipeBook` that needs that bench.
class CraftingStation : public InventoryInteractable {
public:
    using Ingredient = okay::Ingredient;
    using Recipe = okay::Recipe;

    std::vector<Recipe> recipes;
    std::string recipeBook;     ///< optional .okayrecipes file
    std::string stationTag;     ///< the bench kind ("workbench", "forge"...): book recipes needing it show here
    char  toggleKey  = 'c';
    std::string title = "Crafting";
    bool  closeWhenCrafted = false;   ///< auto-close after a successful craft

    // ---- Style ----
    float rowHeight   = 26.0f;
    float width       = 240.0f;
    float marginX     = 0.0f;         ///< nudge from screen centre
    float marginY     = 0.0f;
    float cornerRadius = 4.0f;
    std::string iconFolder = "textures/items/";
    Color panelColor   = Color::FromBytes(18, 19, 26, 240);
    Color titleBar     = Color::FromBytes(46, 50, 64, 255);
    Color rowColor     = Color::FromBytes(34, 36, 46, 230);
    Color canColor     = Color::FromBytes(70, 120, 80, 235);   ///< row tint when craftable
    Color cantColor    = Color::FromBytes(60, 46, 46, 220);    ///< row tint when missing items
    Color textColor    = Color::FromBytes(240, 240, 245, 255);
    Color hoverColor   = Color::FromBytes(255, 255, 255, 32);

    char InteractKey() const override { return toggleKey; }

    /// The Inventory this station pulls from: same object, else the player's
    /// (FindInventoryPlayer), else this object's root.
    Inventory* Inv() const {
        if (gameObject)
            if (auto* in = gameObject->GetComponent<Inventory>()) return in;
        if (GameObject* p = FindInventoryPlayer(gameObject ? gameObject->scene() : nullptr, playerName))
            if (auto* in = p->GetComponent<Inventory>()) return in;
        GameObject* o = Owner();
        return o ? o->GetComponent<Inventory>() : nullptr;
    }

    /// What this bench offers: its own recipes, then the book's recipes for its tag (or,
    /// with no tag, the book's recipes that need no bench). Locked ones are left out.
    std::vector<Recipe> Recipes() const {
        std::vector<Recipe> out;
        for (const auto& r : recipes) if (r.Unlocked()) out.push_back(r);
        if (!recipeBook.empty())
            for (const auto& r : RecipeBook::Cached(recipeBook).recipes)
                if (r.Unlocked() && r.station == stationTag) out.push_back(r);
        return out;
    }

    /// Does `inv` hold every ingredient of recipe `r` (and is it non-empty)?
    bool CanCraft(const Recipe& r, const Inventory& inv) const {
        return !r.inputs.empty() && r.Unlocked() && CraftQueue::Affordable(inv, r) > 0;
    }
    bool CanCraft(int idx) const {
        Inventory* inv = Inv();
        const auto all = Recipes();
        return inv && idx >= 0 && idx < (int)all.size() && CanCraft(all[idx], *inv);
    }

    /// Craft recipe `idx` (of Recipes()) `times` times — instantly and atomically, or
    /// queued when it takes time. Returns false if none was crafted.
    bool Craft(int idx, int times = 1) {
        Inventory* inv = Inv();
        const auto all = Recipes();
        if (!inv || idx < 0 || idx >= (int)all.size() || !CanCraft(all[idx], *inv)) return false;
        const bool ok = m_queue.Craft(*inv, all[idx], times) > 0;
        if (ok && closeWhenCrafted) open = false;
        return ok;
    }
    /// Craft as many of recipe `idx` as the inventory can afford.
    bool CraftMax(int idx) {
        Inventory* inv = Inv();
        const auto all = Recipes();
        return inv && idx >= 0 && idx < (int)all.size() && Craft(idx, CraftQueue::Affordable(*inv, all[idx]));
    }

    const CraftQueue& Queue() const { return m_queue; }

    void Update(float dt) override {
        InventoryInteractable::Update(dt);
        if (Inventory* inv = Inv())
            m_queue.Update(dt, *inv, gameObject ? gameObject->scene() : nullptr,
                           (gameObject && gameObject->transform) ? gameObject->transform->Position() : Vec3{});
    }

private:
    CraftQueue m_queue;

    GameObject* Owner() const {
        if (!gameObject || !gameObject->transform) return gameObject;
        Transform* t = gameObject->transform;
        while (t->Parent()) t = t->Parent();
        return t->gameObject ? t->gameObject : gameObject;
    }
};

} // namespace okay
