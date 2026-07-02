#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Render/Color.hpp"
#include <string>
#include <vector>

namespace okay {

/// A simple crafting bench: a list of recipes (inputs -> output) that consume items
/// from a sibling/owner Inventory and produce a new one. It draws its own panel (player
/// + editor Play view) and crafts on click; open it with a key. Pair it with an
/// Inventory and you have Minecraft/survival-style crafting with no scripting.
class CraftingStation : public Behaviour {
public:
    struct Ingredient { std::string item; int count = 1; };
    struct Recipe { std::vector<Ingredient> inputs; std::string output; int outputCount = 1; };

    std::vector<Recipe> recipes;
    bool  open       = false;
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

    /// The Inventory this station pulls from: same object, else the player root.
    Inventory* Inv() const {
        if (gameObject)
            if (auto* in = gameObject->GetComponent<Inventory>()) return in;
        GameObject* o = Owner();
        return o ? o->GetComponent<Inventory>() : nullptr;
    }

    /// Does `inv` hold every ingredient of recipe `r` (and is it non-empty)?
    bool CanCraft(const Recipe& r, const Inventory& inv) const {
        if (r.inputs.empty() || r.output.empty()) return false;
        for (const auto& in : r.inputs)
            if (in.count > 0 && inv.Count(in.item) < in.count) return false;
        return true;
    }
    bool CanCraft(int idx) const {
        Inventory* inv = Inv();
        if (!inv || idx < 0 || idx >= (int)recipes.size()) return false;
        return CanCraft(recipes[idx], *inv);
    }

    /// Consume the inputs and add the output. Returns false if it isn't affordable or
    /// the output doesn't fit. Inputs are only consumed on success.
    bool Craft(int idx) {
        Inventory* inv = Inv();
        if (!inv || idx < 0 || idx >= (int)recipes.size()) return false;
        const Recipe& r = recipes[idx];
        if (!CanCraft(r, *inv)) return false;
        for (const auto& in : r.inputs) inv->Remove(in.item, in.count);
        bool ok = inv->Add(r.output, r.outputCount < 1 ? 1 : r.outputCount);
        if (ok && closeWhenCrafted) open = false;
        return ok;
    }

    void Update(float) override {
        if (toggleKey && Input::GetKeyDown(toggleKey)) open = !open;
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
