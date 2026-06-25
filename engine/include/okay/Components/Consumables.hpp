#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/NativeUIActions.hpp"
#include <string>
#include <vector>

namespace okay {

/// Bridges items to the survival components: maps item names to a survival action +
/// amount (e.g. "apple" → Eat 30, "water" → Drink 25, "medkit" → Heal 50). Put it on
/// the Player alongside the survival stats (and optionally an Inventory).
///
/// Two ways to use a recipe:
///  - `Use(item)` — consumes one from a sibling Inventory (when `requireInventory`)
///    and applies the effect. Wire it to a hotbar button or call from a script.
///  - Drag & drop — drop a world item (Draggable) onto this object and, if its name
///    or tag matches a recipe, the effect is applied and (optionally) the dropped
///    object is destroyed. Lets inventory/drag-drop feed the survival stats with no
///    scripting.
class Consumables : public Behaviour {
public:
    struct Recipe { std::string item; std::string action = "Eat"; float amount = 25.0f; };
    std::vector<Recipe> recipes;

    bool requireInventory   = true;   // Use() must find + remove the item from a sibling Inventory
    bool consumeOnDrop      = true;    // a matching world item dropped here is consumed
    bool destroyDroppedItem = true;    // remove the dropped world object after consuming it

    void AddRecipe(const std::string& item, const std::string& action, float amount) {
        recipes.push_back({item, action, amount});
    }

    /// Use one `item`: removes it from a sibling Inventory (if required), then applies
    /// its survival effect to this object. Returns true if it was consumed.
    bool Use(const std::string& item) {
        const Recipe* r = Find(item);
        if (!r) return false;
        if (requireInventory) {
            auto* inv = gameObject ? gameObject->GetComponent<Inventory>() : nullptr;
            if (!inv || !inv->Remove(item, 1)) return false;
        }
        InvokeNativeUIAction(gameObject, r->action, r->amount);
        return true;
    }

    /// Drag-drop hook: a world item `dragged` was dropped on this object. If its name
    /// or tag matches a recipe, apply the effect (and destroy the item if configured).
    bool ConsumeDropped(GameObject* dragged) {
        if (!consumeOnDrop || !dragged) return false;
        const Recipe* r = Find(dragged->name);
        if (!r) r = Find(dragged->tag);
        if (!r) return false;
        InvokeNativeUIAction(gameObject, r->action, r->amount);
        if (destroyDroppedItem && dragged->scene()) dragged->scene()->Destroy(dragged);
        return true;
    }

private:
    const Recipe* Find(const std::string& item) const {
        if (item.empty()) return nullptr;
        for (const auto& r : recipes) if (r.item == item) return &r;
        return nullptr;
    }
};

} // namespace okay
