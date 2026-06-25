#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/Inventory.hpp"
#include <string>
#include <vector>

namespace okay {

/// Recipe-based crafting on top of a sibling Inventory. Each recipe lists input
/// items + counts and produces an output item. `Craft` checks the inventory has the
/// inputs, removes them, and adds the output. Put it on the Player next to its
/// Inventory; drive it from a crafting-menu button (On Click `Craft`, Amount = recipe
/// index) or from code/script.
class Crafting : public Behaviour {
public:
    struct Ingredient { std::string item; int count = 1; };
    struct Recipe { std::string output; int outputCount = 1; std::vector<Ingredient> inputs; };
    std::vector<Recipe> recipes;

    /// Add a recipe: AddRecipe("torch", 1, {{"wood",1},{"cloth",1}}).
    void AddRecipe(const std::string& output, int outputCount, std::vector<Ingredient> inputs) {
        recipes.push_back({output, outputCount, std::move(inputs)});
    }

    Inventory* Inv() const { return gameObject ? gameObject->GetComponent<Inventory>() : nullptr; }

    bool CanCraft(const Recipe& r) const {
        Inventory* inv = Inv();
        if (!inv) return false;
        for (const auto& in : r.inputs) if (inv->Count(in.item) < in.count) return false;
        return true;
    }
    bool CanCraft(const std::string& output) const {
        const Recipe* r = Find(output); return r && CanCraft(*r);
    }

    /// Craft a recipe: consumes its inputs and adds its output. Returns false if the
    /// inventory is missing ingredients (nothing is consumed) or full.
    bool Craft(const Recipe& r) {
        Inventory* inv = Inv();
        if (!inv || !CanCraft(r)) return false;
        for (const auto& in : r.inputs) inv->Remove(in.item, in.count);
        inv->Add(r.output, r.outputCount);
        return true;
    }
    bool Craft(const std::string& output) { const Recipe* r = Find(output); return r && Craft(*r); }
    bool CraftIndex(int i) { return (i >= 0 && i < (int)recipes.size()) && Craft(recipes[i]); }

private:
    const Recipe* Find(const std::string& output) const {
        for (const auto& r : recipes) if (r.output == output) return &r;
        return nullptr;
    }
};

} // namespace okay
