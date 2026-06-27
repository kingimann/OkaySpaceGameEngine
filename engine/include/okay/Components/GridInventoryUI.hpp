#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/GridInventory.hpp"
#include "okay/Render/Color.hpp"
#include <string>
#include <vector>

namespace okay {

/// Draws a DayZ / Unturned grid inventory and handles drag-and-drop (the player
/// performs the actual mouse picking, using this component's layout + drag state).
/// Drop it next to a GridInventory and open with that inventory's key.
class GridInventoryUI : public Behaviour {
public:
    float cellSize = 46.0f;
    float gap      = 2.0f;
    std::string iconFolder = "textures/items/";   ///< <folder><item>.png icons (else the name)
    Color panelColor  = Color::FromBytes(18, 19, 26, 240);
    Color cellColor   = Color::FromBytes(34, 36, 46, 230);
    Color itemColor   = Color::FromBytes(70, 90, 120, 240);
    Color itemBorder  = Color::FromBytes(150, 170, 200, 255);
    Color textColor   = Color::FromBytes(240, 240, 245, 255);
    Color titleBar    = Color::FromBytes(46, 50, 64, 255);   ///< header strip behind the title
    Color hoverColor  = Color::FromBytes(255, 255, 255, 40); ///< tint over the hovered item
    Color dropOk      = Color::FromBytes(90, 220, 120, 90);  ///< target highlight when it fits
    Color dropBad     = Color::FromBytes(230, 80, 80, 90);   ///< target highlight when blocked
    float cornerRadius = 4.0f;
    bool  darkenWhenOpen = true;
    bool  showWeight  = true;

    // ---- Features ----
    bool  showTooltips = true;   ///< hover an item to see its name, size and weight
    Color tooltipColor = Color::FromBytes(12, 13, 18, 240);   ///< tooltip background
    Color tooltipText  = Color::FromBytes(245, 245, 250, 255); ///< tooltip text
    char  rotateKey    = 'r';    ///< while dragging, rotate the item 90° (0 = disabled)
    Color overweightColor = Color::FromBytes(235, 90, 90, 255); ///< weight text when over the limit

    /// A rarity/tier tint: items named `item` get their border drawn in `color`.
    struct RarityRule { std::string item; Color color = Color::FromBytes(255, 215, 0, 255); };
    std::vector<RarityRule> rarities;
    const Color* RarityOf(const std::string& item) const {
        for (const auto& r : rarities) if (!r.item.empty() && r.item == item) return &r.color;
        return nullptr;
    }

    // ---- Runtime drag state (driven by the renderer's mouse handling) ----
    int   dragIndex = -1;     ///< item being dragged, or -1
    int   grabX = 0, grabY = 0;   ///< grabbed cell offset within the item

    GridInventory* Inv() const {
        if (gameObject)
            if (auto* g = gameObject->GetComponent<GridInventory>()) return g;
        GameObject* o = Owner();
        return o ? o->GetComponent<GridInventory>() : nullptr;
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
