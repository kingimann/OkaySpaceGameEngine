#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>

namespace okay {

/// A DayZ / Unturned style grid inventory: items occupy a w×h footprint of cells
/// and live at a grid position, so a rifle (2×6) and a can (1×1) share one bag.
/// This is the DATA model (placement, fit-testing, auto-add, weight); GridInventoryUI
/// draws it and handles drag-and-drop. Tag several of these for separate containers
/// (backpack / vest / crate) and drag items between them.
struct GridItem {
    std::string name;        ///< ItemDatabase id
    int   w = 1, h = 1;      ///< footprint in cells
    int   x = 0, y = 0;      ///< top-left cell
    int   count = 1;
    float weight = 0.0f;     ///< per-item weight
    ItemInstanceData data;   ///< durability / uid / custom values (empty for plain items)
};

class GridInventory : public Behaviour {
public:
    int  cols = 8, rows = 6;
    std::vector<GridItem> items;
    bool open = false;
    char toggleKey = 'i';
    std::string title = "Inventory";
    float weightLimit = 0.0f;     ///< max carry weight (0 = unlimited); shown by the UI
    /// When set (and weightLimit > 0), adds and drags that would go over the limit are
    /// refused instead of only turning the UI red. Scenes saved before this existed load
    /// with it off, so they behave as they did.
    bool enforceWeight = true;
    /// Same-item, same-size items merge when one is dropped on the other, up to this
    /// count per stack and the item's own stack size (0 = items never merge).
    int maxStack = 0;
    /// Items Validate() couldn't fit in the grid (after the grid shrank, or from a bad
    /// file). Kept and saved — never deleted — and put back as soon as there's room.
    std::vector<GridItem> overflow;
    /// Carry the contents into the next scene loaded with load_scene: the object with
    /// the same name there (usually the player) gets them.
    bool keepOnSceneLoad = false;

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

    /// Would adding `w` more weight still be allowed? Always true unless enforceWeight.
    bool WeightAllows(float w) const {
        return !enforceWeight || weightLimit <= 0.0f || w <= 0.0f || TotalWeight() + w <= weightLimit + 1e-4f;
    }

    /// First free w×h spot (row-major) in (x,y); false if none.
    bool FindSpot(int w, int h, int& x, int& y, int ignore = -1) const {
        for (int yy = 0; yy <= rows - h; ++yy)
            for (int xx = 0; xx <= cols - w; ++xx)
                if (CanPlace(xx, yy, w, h, ignore)) { x = xx; y = yy; return true; }
        return false;
    }

    /// Add `count` of an item: tops up same-name stacks first (when maxStack is set), then
    /// takes free spots — rotated if that's the only way it fits — in stacks of at most
    /// maxStack. All or nothing: returns the index of the last item touched, or -1 (and
    /// nothing changes) if it doesn't all fit or would break an enforced weight limit.
    int AddItem(const std::string& name, int w, int h, int count = 1, float weight = 0.0f) {
        if (count < 1 || w < 1 || h < 1 || !WeightAllows(weight * count)) return -1;
        std::vector<GridItem> before = items;
        int left = count, last = -1;
        for (int i = 0; i < (int)items.size() && left > 0; ++i)
            if (CanStack(items[i], name, w, h, weight, ItemInstanceData{})) {
                const int n = std::min(StackRoom(items[i]), left);
                if (n > 0) { items[i].count += n; left -= n; last = i; }
            }
        while (left > 0) {
            const int n = maxStack > 0 ? std::min(left, maxStack) : left;
            GridItem it{name, w, h, 0, 0, n, weight};
            if (!FindSpot(w, h, it.x, it.y)) {
                if (w == h || !FindSpot(h, w, it.x, it.y)) { items.swap(before); return -1; }
                std::swap(it.w, it.h);
            }
            items.push_back(it);
            last = (int)items.size() - 1;
            left -= n;
        }
        return last;
    }

    /// Add `count` of a defined item: its size and weight come from the ItemDatabase.
    int AddDefined(const std::string& id, int count = 1) {
        const ItemDefinition* d = ItemDatabase::Get().Def(id);
        if (!d) return -1;
        return AddItem(d->id, d->gridW, d->gridH, count, d->weight);
    }

    /// Move item `index` to (x,y) — turned 90° when `rotated` — if it fits there; dropping
    /// it on a matching stack merges as much as that stack holds. Returns false (and leaves
    /// the item exactly as it was) otherwise.
    bool PlaceAt(int index, int x, int y, bool rotated = false) { return Move(index, *this, x, y, rotated, true); }

    /// Move item `index` from this inventory into `dst` at (x,y) (turned 90° when `rotated`).
    /// Dropping on a matching stack merges as much as fits; anything left stays here.
    /// Respects dst's enforced weight limit. Returns true if anything moved.
    bool MoveTo(int index, GridInventory& dst, int x, int y, bool rotated = false) { return Move(index, dst, x, y, rotated, true); }

    /// Would MoveTo (or PlaceAt, when dst is this) succeed? Changes nothing.
    bool CanMoveTo(int index, const GridInventory& dst, int x, int y, bool rotated = false) const {
        return const_cast<GridInventory*>(this)->Move(index, const_cast<GridInventory&>(dst), x, y, rotated, false);
    }

    /// Split `n` off item `index` into a new stack at the first free spot. Returns the new
    /// item's index, or -1 (nothing changes) if n is out of range or there's no room.
    int Split(int index, int n) {
        if (index < 0 || index >= (int)items.size() || n < 1 || n >= items[index].count) return -1;
        GridItem part = items[index];
        if (!FindSpot(part.w, part.h, part.x, part.y)) return -1;
        part.count = n;
        items[index].count -= n;
        items.push_back(part);
        return (int)items.size() - 1;
    }

    void RemoveAt(int index) {
        if (index >= 0 && index < (int)items.size()) items.erase(items.begin() + index);
    }

    /// Rotate item `index` 90° (swap its w/h) if the rotated footprint still fits at
    /// its current cell. Returns true if it rotated.
    bool Rotate(int index) {
        if (index < 0 || index >= (int)items.size()) return false;
        GridItem& it = items[index];
        if (!CanPlace(it.x, it.y, it.h, it.w, index)) return false;
        std::swap(it.w, it.h);
        return true;
    }

    /// Repair loaded or hand-edited contents: sizes/counts at least 1, finite non-negative
    /// weights, and every item in bounds with no overlaps. An item that is out of place
    /// moves to the first free spot (rotated if that's the only way it fits); one with no
    /// room at all goes to `overflow`, and overflow items come back once there's room.
    /// Returns how many items it moved or repaired.
    int Validate() {
        const int kMaxSide = 64;   // keeps a corrupt file from asking for a billion cells
        cols = std::max(1, std::min(cols, kMaxSide));
        rows = std::max(1, std::min(rows, kMaxSide));
        if (maxStack < 0) maxStack = 0;
        if (!std::isfinite(weightLimit) || weightLimit < 0.0f) weightLimit = 0.0f;
        int touched = 0;
        std::vector<GridItem> in;
        in.swap(items);
        const std::size_t placedBefore = in.size();
        in.insert(in.end(), overflow.begin(), overflow.end());
        overflow.clear();
        for (std::size_t k = 0; k < in.size(); ++k) {
            GridItem it = in[k];
            bool fixedIt = false;
            if (it.w < 1 || it.w > kMaxSide) { it.w = std::max(1, std::min(it.w, kMaxSide)); fixedIt = true; }
            if (it.h < 1 || it.h > kMaxSide) { it.h = std::max(1, std::min(it.h, kMaxSide)); fixedIt = true; }
            if (it.count < 1) { it.count = 1; fixedIt = true; }
            if (!std::isfinite(it.weight) || it.weight < 0.0f) { it.weight = 0.0f; fixedIt = true; }
            if (k < placedBefore && CanPlace(it.x, it.y, it.w, it.h)) {
                items.push_back(it);
            } else if (FindSpot(it.w, it.h, it.x, it.y)) {
                items.push_back(it); fixedIt = true;
            } else if (it.w != it.h && FindSpot(it.h, it.w, it.x, it.y)) {
                std::swap(it.w, it.h); items.push_back(it); fixedIt = true;
            } else {
                overflow.push_back(it); fixedIt = fixedIt || k < placedBefore;
            }
            if (fixedIt) ++touched;
        }
        return touched;
    }

    float TotalWeight() const {
        float w = 0.0f;
        for (const GridItem& it : items) w += it.weight * it.count;
        return w;
    }

    void Update(float) override {
        if (toggleKey && Input::GetKeyDown(toggleKey)) open = !open;
    }

private:
    bool Move(int index, GridInventory& dst, int x, int y, bool rotated, bool apply) {
        if (index < 0 || index >= (int)items.size()) return false;
        GridItem& it = items[index];
        const bool same = (&dst == this);
        const int on = dst.ItemAtCell(x, y);
        if (on >= 0 && !(same && on == index) && dst.CanStack(dst.items[on], it.name, it.w, it.h, it.weight, it.data)) {
            int n = std::min(dst.StackRoom(dst.items[on]), it.count);
            if (!same) n = std::min(n, dst.WeightRoom(it.weight, n));
            if (n <= 0) return false;
            if (!apply) return true;
            dst.items[on].count += n;
            it.count -= n;
            if (it.count <= 0) items.erase(items.begin() + index);
            return true;
        }
        const int w = rotated ? it.h : it.w, h = rotated ? it.w : it.h;
        if (!dst.CanPlace(x, y, w, h, same ? index : -1)) return false;
        if (!same && !dst.WeightAllows(it.weight * it.count)) return false;
        if (!apply) return true;
        GridItem moved = it;
        moved.x = x; moved.y = y; moved.w = w; moved.h = h;
        if (same) { it = moved; return true; }
        dst.items.push_back(moved);
        items.erase(items.begin() + index);
        return true;
    }
    bool CanStack(const GridItem& on, const std::string& name, int w, int h, float weight, const ItemInstanceData& data) const {
        return maxStack > 0 && on.name == name && on.weight == weight && on.data.StacksWith(data) &&
               ((on.w == w && on.h == h) || (on.w == h && on.h == w));
    }
    int StackRoom(const GridItem& on) const {
        const int cap = ItemDatabase::Get().StackLimit(on.name, maxStack);
        return cap > on.count ? cap - on.count : 0;
    }
    /// How many of `n` items weighing `each` the enforced weight limit still lets in.
    int WeightRoom(float each, int n) const {
        if (!enforceWeight || weightLimit <= 0.0f || each <= 0.0f) return n;
        const float room = weightLimit + 1e-4f - TotalWeight();
        if (room <= 0.0f) return 0;
        const float fit = std::floor(room / each);
        return fit < (float)n ? (int)fit : n;
    }
};

} // namespace okay
