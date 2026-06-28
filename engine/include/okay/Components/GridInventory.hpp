#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Input/Input.hpp"
#include <string>
#include <vector>
#include <utility>

namespace okay {

/// A DayZ / Unturned style grid inventory: items occupy a w×h footprint of cells
/// and live at a grid position, so a rifle (2×6) and a can (1×1) share one bag.
/// This is the DATA model (placement, fit-testing, auto-add, weight); GridInventoryUI
/// draws it and handles drag-and-drop. Tag several of these for separate containers
/// (backpack / vest / crate) and drag items between them.
struct GridItem {
    std::string name;
    int   w = 1, h = 1;      ///< footprint in cells
    int   x = 0, y = 0;      ///< top-left cell
    int   count = 1;
    float weight = 0.0f;     ///< per-stack weight (informational)
};

class GridInventory : public Behaviour {
public:
    int  cols = 8, rows = 6;
    std::vector<GridItem> items;
    bool open = false;
    char toggleKey = 'i';
    std::string title = "Inventory";
    float weightLimit = 0.0f;     ///< max carry weight (0 = unlimited); shown by the UI

    /// Unturned-style role of this container, shown as the label above its grid in the
    /// multi-container screen ("Shirt", "Pants", "Vest", "Backpack", "Hands", ...). When
    /// empty the title is used. Purely a label — the footprint logic is unchanged.
    std::string category;
    /// A ground / loot container: it appears in the player screen's "Nearby" column when
    /// the player is within range, so you can drag loot straight into your bags. Equipped
    /// containers (the player's own + its child clothes/bags) leave this false.
    bool worldItem = false;

    /// True when a weight limit is set and the contents exceed it (over-encumbered).
    bool OverWeight() const { return weightLimit > 0.0f && TotalWeight() > weightLimit; }

    /// Is the w×h block at (x,y) fully in-bounds and free (ignoring item `ignore`)?
    bool CanPlace(int x, int y, int w, int h, int ignore = -1) const {
        if (x < 0 || y < 0 || w < 1 || h < 1 || x + w > cols || y + h > rows) return false;
        for (int i = 0; i < (int)items.size(); ++i) {
            if (i == ignore) continue;
            const GridItem& it = items[i];
            if (x < it.x + it.w && x + w > it.x && y < it.y + it.h && y + h > it.y) return false;
        }
        return true;
    }

    /// Index of the item covering cell (cx,cy), or -1.
    int ItemAtCell(int cx, int cy) const {
        for (int i = 0; i < (int)items.size(); ++i) {
            const GridItem& it = items[i];
            if (cx >= it.x && cx < it.x + it.w && cy >= it.y && cy < it.y + it.h) return i;
        }
        return -1;
    }

    /// Place a new item at the first free spot (row-major). Returns its index or -1
    /// if it doesn't fit anywhere.
    int AddItem(const std::string& name, int w, int h, int count = 1, float weight = 0.0f) {
        for (int y = 0; y <= rows - h; ++y)
            for (int x = 0; x <= cols - w; ++x)
                if (CanPlace(x, y, w, h)) {
                    items.push_back(GridItem{name, w, h, x, y, count, weight});
                    return (int)items.size() - 1;
                }
        return -1;
    }

    /// Move item `index` to (x,y) if it fits there. Returns false (and leaves it put)
    /// otherwise. Works across instances via MoveTo() below for cross-container drags.
    bool PlaceAt(int index, int x, int y) {
        if (index < 0 || index >= (int)items.size()) return false;
        if (!CanPlace(x, y, items[index].w, items[index].h, index)) return false;
        items[index].x = x; items[index].y = y;
        return true;
    }

    /// Move item `index` from this inventory into `dst` at (x,y). Returns true on
    /// success (item removed from here, added there) — used for cross-bag drags.
    bool MoveTo(int index, GridInventory& dst, int x, int y) {
        if (index < 0 || index >= (int)items.size()) return false;
        GridItem it = items[index];
        if (!dst.CanPlace(x, y, it.w, it.h)) return false;
        it.x = x; it.y = y;
        dst.items.push_back(it);
        items.erase(items.begin() + index);
        return true;
    }

    void RemoveAt(int index) {
        if (index >= 0 && index < (int)items.size()) items.erase(items.begin() + index);
    }

    /// Rotate item `index` 90° (swap its w/h) if the rotated footprint still fits at
    /// its current cell. Returns true if it rotated. Used by the UI while dragging.
    bool Rotate(int index) {
        if (index < 0 || index >= (int)items.size()) return false;
        GridItem& it = items[index];
        if (!CanPlace(it.x, it.y, it.h, it.w, index)) return false;
        std::swap(it.w, it.h);
        return true;
    }

    float TotalWeight() const {
        float w = 0.0f;
        for (const GridItem& it : items) w += it.weight * it.count;
        return w;
    }

    void Update(float) override {
        if (toggleKey && Input::GetKeyDown(toggleKey)) open = !open;
    }
};

} // namespace okay
