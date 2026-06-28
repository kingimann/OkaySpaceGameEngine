#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/GridInventory.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec3.hpp"
#include <string>
#include <vector>
#include <cmath>

namespace okay {

/// Draws a DayZ / Unturned grid inventory and handles drag-and-drop (the player
/// performs the actual mouse picking, using this component's layout + drag state).
/// Drop it next to a GridInventory and open with that inventory's key.
class GridInventoryUI : public Behaviour {
public:
    float cellSize = 56.0f;
    float gap      = 6.0f;
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

    // ---- Layout (all in pixels; the renderer enforces a small comfortable floor) ----
    float columnGap     = 60.0f;   ///< gap between the equipped column and the Nearby column
    float panelPad      = 20.0f;   ///< padding inside each column's backing panel
    float containerGap  = 26.0f;   ///< vertical gap between stacked containers
    float headerHeight  = 26.0f;   ///< height of each container's name header
    bool  showMasterTitle = true;  ///< the big title above the bags
    std::string masterTitle = "INVENTORY";

    // ---- Style toggles + extra colours ----
    bool  useGradients = true;     ///< soft top->bottom gradients on panels/tiles
    bool  dropShadows  = true;     ///< drop shadows behind panels + the carried item
    bool  accentBars   = true;     ///< the coloured accent stripe on headers + tiles
    bool  autoAccent   = true;     ///< derive each container/item accent from its name (else use accentColor)
    Color accentColor  = Color::FromBytes(120, 140, 180, 255);  ///< fixed accent when autoAccent is off
    Color headerColor  = Color::FromBytes(46, 50, 64, 255);     ///< container header strip (defaults to titleBar)
    Color weightGood   = Color::FromBytes(110, 200, 130, 255);  ///< weight bar when comfortably under
    Color weightWarn   = Color::FromBytes(230, 190, 90, 255);   ///< weight bar when near the limit

    /// The accent colour for a container/item: a stable per-name hue, or the fixed
    /// accentColor when autoAccent is off. Used by both the player and editor renderers.
    Color Accent(const std::string& key) const { return autoAccent ? AccentFor(key) : accentColor; }

    /// A rarity/tier tint: items named `item` get their border drawn in `color`.
    struct RarityRule { std::string item; Color color = Color::FromBytes(255, 215, 0, 255); };
    std::vector<RarityRule> rarities;
    const Color* RarityOf(const std::string& item) const {
        for (const auto& r : rarities) if (!r.item.empty() && r.item == item) return &r.color;
        return nullptr;
    }

    // ---- Unturned-style multi-container screen ----
    /// When true, opening the screen shows EVERY container the player has — its own grid
    /// plus each child clothing/bag's grid — stacked in a left column, and every nearby
    /// ground container (GridInventory.worldItem within `nearbyRange`) in a right column.
    /// Drag-and-drop works across all of them. When false, only Inv() is drawn (classic).
    bool  multiContainer = true;
    float nearbyRange = 4.0f;                ///< world units to gather nearby ground containers
    std::string nearbyTitle = "Nearby";      ///< header above the ground-loot column

    // ---- Runtime drag state (driven by the renderer's mouse handling) ----
    GridInventory* dragInv = nullptr;  ///< container the dragged item came from (cross-bag drags)
    int   dragIndex = -1;     ///< item being dragged within dragInv, or -1
    int   grabX = 0, grabY = 0;   ///< grabbed cell offset within the item

    GridInventory* Inv() const {
        if (gameObject)
            if (auto* g = gameObject->GetComponent<GridInventory>()) return g;
        GameObject* o = Owner();
        return o ? o->GetComponent<GridInventory>() : nullptr;
    }

    /// A stable, distinct accent colour derived from a container's category/title, so
    /// each worn bag / clothing reads with its own colour in the multi-container screen
    /// without any per-game configuration (FNV-1a hash -> hue at a muted S/V).
    static Color AccentFor(const std::string& key) {
        if (key.empty()) return Color::FromBytes(120, 140, 180, 255);
        unsigned h = 2166136261u;
        for (char c : key) { h ^= (unsigned char)c; h *= 16777619u; }
        float H = (float)(h % 360), S = 0.45f, V = 0.95f;
        float C = V * S, X = C * (1.0f - std::fabs(std::fmod(H / 60.0f, 2.0f) - 1.0f)), m = V - C;
        float r = 0, g = 0, b = 0;
        if (H < 60)       { r = C; g = X; }
        else if (H < 120) { r = X; g = C; }
        else if (H < 180) { g = C; b = X; }
        else if (H < 240) { g = X; b = C; }
        else if (H < 300) { r = X; b = C; }
        else              { r = C; b = X; }
        return Color{r + m, g + m, b + m, 1.0f};
    }

    /// Collect the containers the multi-container screen should show. `equipped` is the
    /// player's own grid followed by every GridInventory on a descendant object (clothes /
    /// bags); `nearby` is every ground container (worldItem) within `nearbyRange`. Both the
    /// player and editor renderers call this so the layout stays identical.
    void CollectContainers(std::vector<GridInventory*>& equipped,
                           std::vector<GridInventory*>& nearby) const {
        equipped.clear(); nearby.clear();
        GridInventory* primary = Inv();
        // The player is the object this screen sits on (NOT the topmost ancestor), so
        // the whole rig can be nested under a tidy group without changing who counts as
        // "worn": worn bags are this object's children; ground loot is anything else
        // flagged worldItem within range.
        GameObject* root = gameObject ? gameObject : Owner();
        if (primary) equipped.push_back(primary);
        Scene* sc = root ? root->scene() : nullptr;
        if (!sc || !root || !root->transform) return;
        Vec3 pp = root->transform->Position();
        for (const auto& up : sc->Objects()) {
            GameObject* go = up.get();
            if (!go || go == root) continue;
            auto* gi = go->GetComponent<GridInventory>();
            if (!gi || gi == primary) continue;
            bool worn = IsDescendant(go->transform, root->transform);
            if (worn && !gi->worldItem) {
                equipped.push_back(gi);                 // a worn/held container (clothes, bag)
            } else if (gi->worldItem) {
                Vec3 d = go->transform ? go->transform->Position() - pp : Vec3{1e9f, 1e9f, 1e9f};
                if (d.Magnitude() <= nearbyRange) nearby.push_back(gi);   // ground loot in range
            }
        }
    }

private:
    GameObject* Owner() const {
        if (!gameObject || !gameObject->transform) return gameObject;
        Transform* t = gameObject->transform;
        while (t->Parent()) t = t->Parent();
        return t->gameObject ? t->gameObject : gameObject;
    }
    static bool IsDescendant(Transform* t, Transform* root) {
        for (Transform* p = t ? t->Parent() : nullptr; p; p = p->Parent())
            if (p == root) return true;
        return false;
    }
};

} // namespace okay
