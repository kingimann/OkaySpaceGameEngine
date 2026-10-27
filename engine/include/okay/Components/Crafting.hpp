#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/CraftingStation.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Inventory/Recipe.hpp"
#include <string>
#include <vector>

namespace okay {

/// Recipe-based crafting on top of a sibling Inventory (the player's crafting menu).
/// Each recipe lists inputs and an output (plus byproducts); it can take craft time
/// (queued — see Queue()), need an open bench with a station tag nearby, or need an
/// unlock flag. Recipes come from `recipes` and from an optional shared `.okayrecipes`
/// book. Drive it from a crafting-menu button (On Click `Craft`, Amount = recipe index)
/// or from code/script.
class Crafting : public Behaviour {
public:
    using Ingredient = okay::Ingredient;
    using Recipe = okay::Recipe;
    std::vector<Recipe> recipes;
    std::string recipeBook;     ///< optional .okayrecipes file whose recipes are added to these

    /// Add a recipe: AddRecipe("torch", 1, {{"wood",1},{"cloth",1}}).
    void AddRecipe(const std::string& output, int outputCount, std::vector<Ingredient> inputs) {
        recipes.push_back({output, outputCount, std::move(inputs)});
    }

    /// Every recipe this knows: its own, then the book's.
    std::vector<Recipe> AllRecipes() const {
        std::vector<Recipe> all = recipes;
        if (!recipeBook.empty())
            for (const auto& r : RecipeBook::Cached(recipeBook).recipes) all.push_back(r);
        return all;
    }

    Inventory* Inv() const { return gameObject ? gameObject->GetComponent<Inventory>() : nullptr; }

    /// Known, at the right bench (when it needs one), and the inventory has the inputs.
    bool CanCraft(const Recipe& r) const {
        Inventory* inv = Inv();
        return inv && r.Unlocked() && AtStation(r) && CraftQueue::Affordable(*inv, r) > 0;
    }
    bool CanCraft(const std::string& output) const { Recipe r; return Find(output, r) && CanCraft(r); }

    /// Craft a recipe `times` times: instantly (atomic — nothing changes if the output
    /// doesn't fit) or, with craft time, queued. Returns false if none was crafted.
    bool Craft(const Recipe& r, int times = 1) {
        Inventory* inv = Inv();
        if (!inv) return false;
        if (!CanCraft(r)) {
            CraftQueue::Failed(*inv, r, !r.Unlocked() ? "not unlocked" : !AtStation(r) ? "needs a station" : "missing ingredients");
            return false;
        }
        return m_queue.Craft(*inv, r, times) > 0;
    }
    bool Craft(const std::string& output, int times = 1) { Recipe r; return Find(output, r) && Craft(r, times); }
    bool CraftIndex(int i) { auto all = AllRecipes(); return i >= 0 && i < (int)all.size() && Craft(all[i]); }
    /// Craft as many as the inventory can afford.
    bool CraftMax(const Recipe& r) { Inventory* inv = Inv(); return inv && Craft(r, CraftQueue::Affordable(*inv, r)); }

    const CraftQueue& Queue() const { return m_queue; }
    /// Refund every unfinished queued craft.
    void CancelQueue() { if (Inventory* inv = Inv()) m_queue.Cancel(*inv, GetScene(), DropPos()); }

    void Update(float dt) override { if (Inventory* inv = Inv()) m_queue.Update(dt, *inv, GetScene(), DropPos()); }

private:
    bool Find(const std::string& output, Recipe& out) const {
        for (const auto& r : AllRecipes()) if (r.output == output) { out = r; return true; }
        return false;
    }
    /// A recipe that needs a bench needs an open CraftingStation with that tag.
    bool AtStation(const Recipe& r) const {
        if (r.station.empty()) return true;
        Scene* s = GetScene();
        if (!s) return false;
        for (const auto& up : s->Objects())
            if (up) if (auto* cs = up->GetComponent<CraftingStation>()) if (cs->open && cs->stationTag == r.station) return true;
        return false;
    }
    Vec3 DropPos() const { return (gameObject && gameObject->transform) ? gameObject->transform->Position() : Vec3{}; }

    CraftQueue m_queue;
};

} // namespace okay
