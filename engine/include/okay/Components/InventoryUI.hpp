#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Inventory/InventoryOps.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Render/Color.hpp"
#include <string>
#include <utility>
#include <algorithm>
#include <cstdlib>
#include <vector>

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
    int   backpackRows = 3;       ///< extra rows shown when open, if the Inventory has no capacity (else capacity decides)
    int   selected     = 0;       ///< selected hotbar slot
    bool  open         = false;   ///< backpack grid visible
    char  toggleKey    = 'e';     ///< open / close the backpack
    bool  selectHotkeys = true;   ///< 1–9 select a hotbar slot
    bool  scrollSelect  = true;   ///< mouse wheel cycles the selection
    bool  dragItems     = true;   ///< drag items between slots while the backpack is open
    int   dragIndex     = -1;     ///< slot being dragged (runtime), or -1
    int   dragAmount    = 0;      ///< how many are lifted off the source (0 = whole stack)
    int   navSlot       = -1;     ///< keyboard / gamepad cursor while the bag is open (-1 = none)
    bool  gamepadNav    = true;   ///< d-pad / WASD moves navSlot; A picks up and drops, X splits, B cancels
    int   equipDrag     = -1;     ///< Equipment slot being dragged (runtime), or -1

    float slotSize = 50.0f;       ///< slot square, pixels (at the reference height)
    float slotGap  = 6.0f;
    bool  scaleToScreen  = true;  ///< scale the whole UI by screenHeight / referenceHeight
    float referenceHeight = 720.0f; ///< the height the sizes above are authored for
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

    // ---- More style customization ----
    /// Horizontal anchor of the hotbar row. Center is classic; Left/Right dock it to
    /// a screen edge (still nudged by `marginX`).
    enum class HAlign { Left, Center, Right };
    HAlign hAlign = HAlign::Center;
    /// The selected hotbar slot grows by this factor (Minecraft-style "pop"); 1 = off.
    float selectedScale = 1.0f;
    /// Fill colour for EMPTY slots (defaults to the same look as `slotColor`, so it's
    /// a no-op until you change it — e.g. a darker empty slot to read the grid).
    Color emptySlotColor = Color::FromBytes(28, 30, 38, 210);
    /// Separate fill for hotbar slots (defaults to `slotColor`); lets the hotbar and
    /// backpack use different colours.
    Color hotbarColor = Color::FromBytes(28, 30, 38, 210);
    /// Outline drawn around the hotbar/backpack panel. Alpha 0 (default) = no outline.
    Color panelBorder = Color::FromBytes(70, 72, 84, 0);
    float panelBorderWidth = 2.0f;  ///< panel outline thickness (px); ignored when panelBorder is transparent
    /// Backpack header text (drawn above the open grid when `showTitle` is on).
    bool        showTitle  = false;
    std::string backpackTitle = "Inventory";
    Color       titleColor = Color::FromBytes(235, 236, 245, 255);

    // ---- Features ----
    bool  showTooltips   = true;    ///< hover a slot to see the item's name + count
    Color tooltipColor   = Color::FromBytes(12, 13, 18, 240);   ///< tooltip background
    Color tooltipText    = Color::FromBytes(245, 245, 250, 255); ///< tooltip text
    bool  slotNumbers    = false;   ///< draw 1–9 in the corner of each hotbar slot
    Color numberColor    = Color::FromBytes(180, 184, 200, 255); ///< hotbar slot-number tint
    char  sortKey        = 0;       ///< press to compact + merge stacks (0 = disabled)
    /// How Sort() orders the packed stacks. Compact keeps gather-order (just closes
    /// holes + merges); Name = A→Z by display name; Count = biggest stacks first;
    /// Rarity = `rarities` order then the item's tier (rarest first); Category = by the
    /// item's category, then name; Weight = heaviest stacks first; Value = the item's
    /// "value" custom field × count, highest first.
    enum class SortMode { Compact, Name, Count, Rarity, Category, Weight, Value };
    SortMode sortMode    = SortMode::Compact;
    bool  sortOnClose    = false;   ///< auto-Sort() the bag when the backpack closes
    char  dropKey        = 0;       ///< press to drop one of the selected hotbar item (0 = disabled)
    char  useKey         = 0;       ///< press to use the selected hotbar item (0 = disabled)
    bool  clickToUse     = false;   ///< left-click uses the selected item while the backpack is closed
    bool  splitRightClick = true;   ///< right-click a stack to pick up part of it
    bool  splitHalf       = true;   ///< right-click lifts half the stack (else just one)
    bool  shiftQuickMove  = true;   ///< shift+click moves a stack hotbar↔backpack
    float iconInset       = 2.0f;   ///< padding between a slot's border and its icon (px)

    /// A rarity/tier tint: stacks of `item` get their slot border drawn in `color`
    /// (Minecraft net-style quality colours). Add as many as you like.
    struct RarityRule { std::string item; Color color = Color::FromBytes(255, 215, 0, 255); };
    std::vector<RarityRule> rarities;
    const Color* RarityOf(const std::string& item) const {
        for (const auto& r : rarities) if (!r.item.empty() && r.item == item) return &r.color;   // per-UI override
        const ItemDefinition* d = ItemDatabase::Get().Find(item);
        return (d && d->hasRarityColor) ? &d->rarityColor : nullptr;
    }

    /// Name of the object holding the Inventory this UI shows (e.g. "Player"), for a UI
    /// that lives under a Canvas. Empty = this object, else its root (as before).
    std::string inventoryTarget;

    /// Slots shown and usable: the Inventory's capacity when it has one (hotbar first,
    /// the backpack rows wrap the rest), else hotbar + backpackRows × hotbar.
    int SlotCount() const {
        const int n = hotbarSlots < 1 ? 1 : hotbarSlots;
        Inventory* inv = Inv();
        if (inv && inv->capacity > 0) return inv->capacity;
        return n + (backpackRows > 0 ? backpackRows : 0) * n;
    }
    /// Backpack rows needed to show SlotCount() (the last row may be partly filled).
    int BackpackRowCount() const {
        const int n = hotbarSlots < 1 ? 1 : hotbarSlots, rest = SlotCount() - n;
        return rest > 0 ? (rest + n - 1) / n : 0;
    }

    /// Right-click split: move half of stack `from` into the first empty slot.
    bool SplitSlot(int from) {
        Inventory* inv = Inv();
        if (!inv || from < 0 || from >= (int)inv->slots.size()) return false;
        return InventoryOps::Split(inv, from, inv->slots[from].count / 2, SlotCount()).Ok();
    }

    /// Shift-move a stack between the hotbar and the backpack (whichever it isn't in).
    bool QuickMove(int from) { return InventoryOps::QuickMove(Inv(), from, hotbarSlots, SlotCount()).Ok(); }

    /// Index of `item` in the rarity rules (lower = listed earlier = treated as rarer
    /// for sorting). Returns a large number when the item has no rarity rule, so
    /// unranked items sort after ranked ones.
    int RarityIndex(const std::string& item) const {
        for (std::size_t i = 0; i < rarities.size(); ++i)
            if (rarities[i].item == item) return (int)i;
        const ItemDefinition* d = ItemDatabase::Get().Find(item);   // then the item's tier (higher = rarer)
        return (1 << 20) - (d ? d->rarity : 0);
    }

    /// Compact the bag: merge identical stacks and pull everything to the front, so
    /// holes left by dragging close up. `sortMode` then orders the result (A→Z, by
    /// count, or by rarity). Bound to `sortKey` (e.g. set it to 'r').
    void Sort() {
        Inventory* inv = Inv();
        if (!inv) return;
        std::vector<Inventory::Slot> packed;
        for (auto& s : inv->slots) {
            if (Inventory::Empty(s)) continue;
            bool merged = false;
            for (auto& p : packed)
                if (Inventory::Stackable(p, s) && (long long)p.count + s.count <= 2147483647LL) {   // no int overflow
                    p.count += s.count; merged = true; break;
                }
            if (!merged) packed.push_back(s);
        }
        // Re-split by the stack limit: merging must never make a stack bigger than it.
        {
            std::vector<Inventory::Slot> split;
            for (const auto& p : packed) {
                const long long cap = inv->StackCap(p.item);
                for (long long left = p.count; left > 0; left -= cap) {
                    Inventory::Slot part = p;
                    part.count = (int)(left < cap ? left : cap);
                    split.push_back(part);
                }
            }
            packed.swap(split);
        }
        // If the packed stacks no longer fit, keep the original layout (no loss).
        const std::size_t room = inv->slots.size() > (std::size_t)(inv->capacity > 0 ? inv->capacity : 0)
                                     ? inv->slots.size() : (std::size_t)inv->capacity;
        if (packed.size() > room) return;
        // Order the packed stacks per the chosen mode (Compact keeps gather order).
        const ItemDatabase& db = ItemDatabase::Get();
        auto def = [&](const std::string& id) { return db.Find(id); };
        auto name = [&](const Inventory::Slot& x) { const ItemDefinition* d = def(x.item); return d ? d->DisplayName() : x.item; };
        auto num = [&](const Inventory::Slot& x, bool value) {
            const ItemDefinition* d = def(x.item);
            if (!d) return 0.0;
            return x.count * (value ? std::atof(d->Custom("value", "0").c_str()) : (double)d->weight);
        };
        if (sortMode == SortMode::Name)
            std::stable_sort(packed.begin(), packed.end(),
                             [&](const Inventory::Slot& a, const Inventory::Slot& b){ return name(a) < name(b); });
        else if (sortMode == SortMode::Category)
            std::stable_sort(packed.begin(), packed.end(), [&](const Inventory::Slot& a, const Inventory::Slot& b) {
                const ItemDefinition* da = def(a.item); const ItemDefinition* dbb = def(b.item);
                const std::string ca = da ? da->category : std::string(), cb = dbb ? dbb->category : std::string();
                if (ca != cb) return ca < cb;
                return name(a) < name(b); });
        else if (sortMode == SortMode::Weight || sortMode == SortMode::Value)
            std::stable_sort(packed.begin(), packed.end(), [&](const Inventory::Slot& a, const Inventory::Slot& b) {
                const bool v = sortMode == SortMode::Value;
                return num(a, v) > num(b, v); });
        else if (sortMode == SortMode::Count)
            std::stable_sort(packed.begin(), packed.end(),
                             [](const Inventory::Slot& a, const Inventory::Slot& b){ return a.count > b.count; });
        else if (sortMode == SortMode::Rarity)
            std::stable_sort(packed.begin(), packed.end(),
                             [this](const Inventory::Slot& a, const Inventory::Slot& b){
                                 int ra = RarityIndex(a.item), rb = RarityIndex(b.item);
                                 if (ra != rb) return ra < rb; return a.item < b.item; });
        for (auto& s : inv->slots) s = Inventory::Slot{};     // clear, keeping slot count
        if (inv->slots.size() < packed.size()) inv->slots.resize(packed.size());
        for (std::size_t i = 0; i < packed.size(); ++i) inv->slots[i] = packed[i];
        inv->NotifyChanged();
    }

    /// Drop one of the selected hotbar item: removes it from the inventory and returns
    /// its name (empty if nothing to drop) so gameplay can spawn a world pickup.
    std::string DropSelected() {
        const std::string item = SelectedItem();
        return InventoryOps::Drop(Inv(), selected, 1).Ok() ? item : std::string();
    }

    /// Drop `n` (0 = the whole stack) from slot `i`. A script's on_item_dropped(item, count)
    /// takes it when one is listening (it spawns its own pickup); otherwise it lands in
    /// the world in front of whoever carries this inventory as an ItemPickup, so it can
    /// be picked up again. A dropped item is never just deleted.
    bool DropToWorld(int i, int n = 1) {
        Inventory* inv = Inv();
        if (!inv || i < 0 || i >= (int)inv->slots.size() || Inventory::Empty(inv->slots[i])) return false;
        Inventory::Slot dropped = inv->slots[i];
        if (n > 0 && n < dropped.count) dropped.count = n;
        if (IScriptVM* vm = DropHandler()) {
            if (!InventoryOps::Drop(inv, i, dropped.count).Ok()) return false;
            vm->CallEventArgs("on_item_dropped", {vs::VsValue(dropped.item), vs::VsValue((float)dropped.count)});
            return true;
        }
        GameObject* carrier = inv->gameObject;
        Scene* sc = carrier ? carrier->scene() : nullptr;
        if (!sc || !carrier->transform || !InventoryOps::Drop(inv, i, dropped.count).Ok()) return false;
        const Vec3 pos = carrier->transform->Position() + carrier->transform->Forward() * 1.2f;
        if (GameObject* g = ItemPickup::Spawn(*sc, dropped.item, dropped.count, dropped.data, pos))
            g->GetComponent<ItemPickup>()->collectorTag = carrier->tag.empty() ? carrier->name : carrier->tag;
        return true;
    }
    /// The drop key: drop one of the selected hotbar item (see DropToWorld).
    bool DropSelectedItem() { return DropToWorld(selected, 1); }

    /// Use the selected hotbar item: eat/drink/heal it (Consumables) or pick its build
    /// piece (BuildMode) — see InventoryOps::Use.
    bool UseSelected() {
        Inventory* inv = Inv();
        return inv && InventoryOps::Use(inv, selected, inv->gameObject).Ok();
    }

    /// The Inventory this UI shows: on `inventoryTarget` when set, else on the same
    /// object, else on its root.
    Inventory* Inv() const {
        if (!inventoryTarget.empty() && gameObject && gameObject->scene())
            if (GameObject* t = gameObject->scene()->Find(inventoryTarget))
                if (auto* in = t->GetComponent<Inventory>()) return in;
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

    /// Drag a stack from slot `from` onto slot `to`: swap, or merge identical stacks.
    /// Hotbar (0..hotbar-1) and backpack slots reorder freely.
    void MoveSlot(int from, int to) { Inventory* inv = Inv(); InventoryOps::Move(inv, from, inv, to, 0, SlotCount()); }

    /// Drop the lifted `dragAmount` from slot `from` onto `to`. dragAmount >= the
    /// source count (or 0) means the whole stack (swap/merge); a smaller amount is a
    /// partial split — Minecraft-style: placed into an empty/identical slot, or
    /// cancelled (left on the source) if a different item sits there.
    void ApplyDrag(int from, int to) { Inventory* inv = Inv(); InventoryOps::Move(inv, from, inv, to, dragAmount, SlotCount()); }

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
        if (toggleKey && Input::GetKeyDown(toggleKey)) {
            bool wasOpen = open;
            open = !open;
            if (wasOpen && !open && sortOnClose) Sort();   // auto-tidy on close
        }
        if (sortKey && Input::GetKeyDown(sortKey)) Sort();
        if (dropKey && Input::GetKeyDown(dropKey)) DropSelectedItem();
        if ((useKey && Input::GetKeyDown(useKey)) || (clickToUse && !open && Input::GetMouseButtonDown(0))) UseSelected();
        if (selected < 0) selected = 0;
        if (selected >= hotbarSlots) selected = hotbarSlots - 1;
        if (open && gamepadNav) Navigate();
        else navSlot = -1;
    }

    /// Move the keyboard / gamepad cursor. dy -1 is up the screen (backpack toward the
    /// hotbar when the bar is docked to the bottom).
    void NudgeNav(int dx, int dy) {
        const int cols = hotbarSlots < 1 ? 1 : hotbarSlots;
        const int total = SlotCount();
        const int bp = BackpackRowCount();
        if (total <= 0) return;
        if (navSlot < 0 || navSlot >= total) navSlot = (selected >= 0 && selected < total) ? selected : 0;
        std::vector<int> rowStart, rowCount;
        auto pushRow = [&](int start, int n) {
            if (n > 0 && start < total) { rowStart.push_back(start); rowCount.push_back(n); }
        };
        auto backpack = [&]() {
            for (int r = 0; r < bp; ++r) {
                const int start = cols + r * cols;
                pushRow(start, std::min(cols, total - start));
            }
        };
        if (anchorTop) { pushRow(0, std::min(cols, total)); backpack(); }
        else { backpack(); pushRow(0, std::min(cols, total)); }
        if (rowStart.empty()) return;
        int rr = 0, cc = 0;
        for (int r = 0; r < (int)rowStart.size(); ++r)
            for (int c = 0; c < rowCount[r]; ++c)
                if (rowStart[r] + c == navSlot) { rr = r; cc = c; }
        rr += dy; cc += dx;
        if (rr < 0) rr = 0;
        if (rr >= (int)rowStart.size()) rr = (int)rowStart.size() - 1;
        if (cc < 0) cc = 0;
        if (cc >= rowCount[rr]) cc = rowCount[rr] - 1;
        navSlot = rowStart[rr] + cc;
    }

private:
    /// D-pad (or WASD, when the pad isn't also steering) moves the cursor. A picks up or
    /// places, X lifts a split, B puts a held stack back. The d-pad is also fed as WASD,
    /// so a pad press is handled once, from the button, not again as a key.
    void Navigate() {
        if (navSlot < 0) navSlot = selected;
        const bool pad = Input::GetGamepadButton(8) || Input::GetGamepadButton(9)
                      || Input::GetGamepadButton(10) || Input::GetGamepadButton(11);
        if (Input::GetGamepadButtonDown(8)) NudgeNav(0, -1);
        else if (Input::GetGamepadButtonDown(9)) NudgeNav(0, 1);
        else if (Input::GetGamepadButtonDown(10)) NudgeNav(-1, 0);
        else if (Input::GetGamepadButtonDown(11)) NudgeNav(1, 0);
        else if (!pad) {
            if (Input::GetKeyDown('w')) NudgeNav(0, -1);
            else if (Input::GetKeyDown('s')) NudgeNav(0, 1);
            else if (Input::GetKeyDown('a')) NudgeNav(-1, 0);
            else if (Input::GetKeyDown('d')) NudgeNav(1, 0);
        }
        Inventory* inv = Inv();
        if (!inv) return;
        if (Input::GetGamepadButtonDown(1) && dragIndex >= 0) { dragIndex = -1; dragAmount = 0; return; }
        if (Input::GetGamepadButtonDown(2) && dragIndex < 0 && splitRightClick &&
            navSlot >= 0 && navSlot < (int)inv->slots.size() && inv->slots[navSlot].count > 1) {
            dragIndex = navSlot;
            dragAmount = splitHalf ? inv->slots[navSlot].count / 2 : 1;
        }
        if (Input::GetGamepadButtonDown(0)) {
            if (dragIndex < 0) {
                if (navSlot >= 0 && navSlot < (int)inv->slots.size() && !Inventory::Empty(inv->slots[navSlot])) {
                    dragIndex = navSlot; dragAmount = 0;
                }
            } else if (navSlot >= 0) {
                ApplyDrag(dragIndex, navSlot);
                dragIndex = -1; dragAmount = 0;
            }
        }
    }
    /// A script that handles on_item_dropped: on this object, the inventory's, or the root.
    IScriptVM* DropHandler() const {
        GameObject* cands[3] = {gameObject, nullptr, Owner()};
        if (Inventory* inv = Inv()) cands[1] = inv->gameObject;
        for (GameObject* g : cands)
            if (g) if (auto* sc = g->GetComponent<ScriptComponent>())
                if (sc->VM() && sc->VM()->HasEvent("on_item_dropped")) return sc->VM();
        return nullptr;
    }
    GameObject* Owner() const {
        if (!gameObject || !gameObject->transform) return gameObject;
        Transform* t = gameObject->transform;
        while (t->Parent()) t = t->Parent();
        return t->gameObject ? t->gameObject : gameObject;
    }
};

} // namespace okay
