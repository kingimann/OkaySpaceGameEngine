#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Render/Color.hpp"
#include <string>
#include <utility>

namespace okay {

/// A Minecraft-style inventory UI: a hotbar along the bottom with a highlighted
/// selected slot, plus a backpack grid you open with a key. It draws itself (player
/// + editor Play view) from a sibling/owner Inventory component, so you just drop it
/// in — pair it with an Inventory and you have a working bag + hotbar. Number keys
/// 1–9 and the mouse wheel pick the hotbar slot; SelectedItem() tells gameplay what's
/// in hand (e.g. feed it to a BlockBuilder/StructureBuilder).
class InventoryUI : public Behaviour {
public:
    int   hotbarSlots  = 9;       ///< slots on the bottom bar
    int   backpackRows = 3;       ///< extra rows shown when open (backpack = rows × hotbarSlots)
    int   selected     = 0;       ///< selected hotbar slot
    bool  open         = false;   ///< backpack grid visible
    char  toggleKey    = 'e';     ///< open / close the backpack
    bool  selectHotkeys = true;   ///< 1–9 select a hotbar slot
    bool  scrollSelect  = true;   ///< mouse wheel cycles the selection
    bool  dragItems     = true;   ///< drag items between slots while the backpack is open
    int   dragIndex     = -1;     ///< slot being dragged (runtime), or -1

    float slotSize = 50.0f;       ///< slot square, pixels
    float slotGap  = 6.0f;
    std::string iconFolder = "textures/items/";   ///< <folder><item>.png icons (optional; falls back to the name)
    Color slotColor     = Color::FromBytes(28, 30, 38, 210);
    Color slotBorder    = Color::FromBytes(70, 72, 84, 255);
    Color selectedColor = Color::FromBytes(255, 255, 255, 255);
    Color textColor     = Color::FromBytes(240, 240, 245, 255);
    Color countColor    = Color::FromBytes(255, 255, 255, 255);
    Color panelColor    = Color::FromBytes(18, 19, 26, 240);
    bool  darkenWhenOpen = true;

    // ---- Layout / style customization ----
    float marginX        = 0.0f;    ///< horizontal nudge from centre (px)
    float marginY        = 16.0f;   ///< distance from the screen edge (px)
    bool  anchorTop      = false;   ///< dock the hotbar to the top instead of the bottom
    float cornerRadius   = 4.0f;    ///< rounded slot corners (0 = square)
    float borderWidth    = 2.0f;    ///< slot border thickness (px); selected adds 1
    float labelScale     = 1.0f;    ///< multiplier on item-name / count text size
    bool  showCounts     = true;    ///< draw stack counts
    bool  showNames      = true;    ///< draw the item name when there's no icon
    bool  showPanel      = false;   ///< a rounded background panel behind the slots
    float panelPad       = 8.0f;    ///< panel padding around the slots (px)
    Color hoverColor     = Color::FromBytes(255, 255, 255, 36);  ///< tint on the slot under the cursor
    bool  showSelectedName = false; ///< Minecraft-style: held item's name above the hotbar
    int   nameChars      = 6;       ///< how many characters of the item name to show in a slot

    // ---- Features ----
    bool  showTooltips   = true;    ///< hover a slot to see the item's name + count
    Color tooltipColor   = Color::FromBytes(12, 13, 18, 240);   ///< tooltip background
    Color tooltipText    = Color::FromBytes(245, 245, 250, 255); ///< tooltip text
    bool  slotNumbers    = false;   ///< draw 1–9 in the corner of each hotbar slot
    Color numberColor    = Color::FromBytes(180, 184, 200, 255); ///< hotbar slot-number tint
    char  sortKey        = 0;       ///< press to compact + merge stacks (0 = disabled)
    bool  splitRightClick = true;   ///< right-click a stack to split half into an empty slot
    bool  shiftQuickMove  = true;   ///< shift+click moves a stack hotbar↔backpack

    /// A rarity/tier tint: stacks of `item` get their slot border drawn in `color`
    /// (Minecraft net-style quality colours). Add as many as you like.
    struct RarityRule { std::string item; Color color = Color::FromBytes(255, 215, 0, 255); };
    std::vector<RarityRule> rarities;
    const Color* RarityOf(const std::string& item) const {
        for (const auto& r : rarities) if (!r.item.empty() && r.item == item) return &r.color;
        return nullptr;
    }

    /// Right-click split: move half of stack `from` into the first empty slot.
    bool SplitSlot(int from) {
        Inventory* inv = Inv();
        if (!inv || from < 0 || from >= (int)inv->slots.size()) return false;
        Inventory::Slot& s = inv->slots[from];
        if (Inventory::Empty(s) || s.count < 2) return false;
        int move = s.count / 2;
        int cap = inv->capacity > 0 ? inv->capacity : (hotbarSlots + backpackRows * hotbarSlots);
        for (auto& d : inv->slots)
            if (Inventory::Empty(d)) { d.item = s.item; d.count = move; s.count -= move; return true; }
        if ((int)inv->slots.size() < cap) { inv->slots.push_back({s.item, move}); s.count -= move; return true; }
        return false;
    }

    /// Shift-move a stack between the hotbar and the backpack (whichever it isn't in).
    bool QuickMove(int from) {
        Inventory* inv = Inv();
        if (!inv || from < 0 || from >= (int)inv->slots.size() || Inventory::Empty(inv->slots[from])) return false;
        int n = hotbarSlots < 1 ? 1 : hotbarSlots, total = n + backpackRows * n;
        int lo = (from < n) ? n : 0, hi = (from < n) ? total : n;
        while ((int)inv->slots.size() < hi) inv->slots.push_back(Inventory::Slot{});
        for (int i = lo; i < hi; ++i)            // merge onto a matching stack first
            if (!Inventory::Empty(inv->slots[i]) && inv->slots[i].item == inv->slots[from].item) {
                inv->slots[i].count += inv->slots[from].count; inv->slots[from] = Inventory::Slot{}; return true;
            }
        for (int i = lo; i < hi; ++i)            // else first empty slot in the other region
            if (Inventory::Empty(inv->slots[i])) { std::swap(inv->slots[i], inv->slots[from]); return true; }
        return false;
    }

    /// Compact the bag: merge identical stacks and pull everything to the front, so
    /// holes left by dragging close up. Bound to `sortKey` (e.g. set it to 'r').
    void Sort() {
        Inventory* inv = Inv();
        if (!inv) return;
        std::vector<Inventory::Slot> packed;
        for (auto& s : inv->slots) {
            if (Inventory::Empty(s)) continue;
            bool merged = false;
            for (auto& p : packed)
                if (p.item == s.item) { p.count += s.count; merged = true; break; }
            if (!merged) packed.push_back(Inventory::Slot{s.item, s.count});
        }
        for (auto& s : inv->slots) s = Inventory::Slot{};     // clear, keeping slot count
        for (std::size_t i = 0; i < packed.size() && i < inv->slots.size(); ++i)
            inv->slots[i] = packed[i];
    }

    /// The Inventory this UI shows: on the same object, else on the player root.
    Inventory* Inv() const {
        if (gameObject)
            if (auto* in = gameObject->GetComponent<Inventory>()) return in;
        GameObject* o = Owner();
        return o ? o->GetComponent<Inventory>() : nullptr;
    }
    /// Item name in the selected hotbar slot ("" if empty) — what's "in hand".
    std::string SelectedItem() const {
        Inventory* inv = Inv();
        if (!inv || selected < 0 || selected >= (int)inv->slots.size()) return "";
        return inv->slots[selected].item;
    }
    int SelectedCount() const {
        Inventory* inv = Inv();
        if (!inv || selected < 0 || selected >= (int)inv->slots.size()) return 0;
        return inv->slots[selected].count;
    }

    /// Drag a stack from slot `from` onto slot `to`: swap, merge identical stacks, or
    /// (dropping past the used slots) send it to the end. Operates on the Inventory's
    /// stack list, so hotbar (0..hotbar-1) and backpack slots reorder freely.
    void MoveSlot(int from, int to) {
        Inventory* inv = Inv();
        if (!inv || from < 0 || to < 0 || from == to) return;
        if (from >= (int)inv->slots.size()) return;
        // Grow the list so the (possibly empty) target slot exists, so you can drop an
        // item into any open slot and leave a gap behind.
        while ((int)inv->slots.size() <= to) inv->slots.push_back(Inventory::Slot{});
        Inventory::Slot& a = inv->slots[from];
        Inventory::Slot& b = inv->slots[to];
        if (!Inventory::Empty(a) && !Inventory::Empty(b) && a.item == b.item) {
            b.count += a.count; a.item.clear(); a.count = 0;      // merge identical stacks
        } else {
            std::swap(a, b);                                      // swap (target may be empty)
        }
        while (!inv->slots.empty() && Inventory::Empty(inv->slots.back())) inv->slots.pop_back();
    }

    void Update(float) override {
        if (selectHotkeys) {
            for (int k = 0; k < hotbarSlots && k < 9; ++k)
                if (Input::GetKeyDown((char)('1' + k))) selected = k;
        }
        if (scrollSelect && hotbarSlots > 0) {
            float w = Input::MouseWheel();
            if (w > 0.5f)      selected = (selected - 1 + hotbarSlots) % hotbarSlots;
            else if (w < -0.5f) selected = (selected + 1) % hotbarSlots;
        }
        if (toggleKey && Input::GetKeyDown(toggleKey)) open = !open;
        if (sortKey && Input::GetKeyDown(sortKey)) Sort();
        if (selected < 0) selected = 0;
        if (selected >= hotbarSlots) selected = hotbarSlots - 1;
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
