#pragma once
#include "okay/Components/ChestInventory.hpp"
#include "okay/Components/CraftingStation.hpp"
#include "okay/Components/Equipment.hpp"
#include "okay/Components/GridInventory.hpp"
#include "okay/Components/GridInventoryUI.hpp"
#include "okay/Components/InventoryUI.hpp"
#include "okay/Math/Vec2.hpp"
#include <string>
#include <vector>

namespace okay {

/// Layout, hit-testing and the mouse/drag state machine for the inventory screens,
/// computed once for both the built player (SDL) and the editor Play view (ImGui).
/// The renderers only draw: they ask for the layout of a W×H area (in the same pixel
/// space the mouse is fed in), draw at those positions, then hand the frame's mouse to
/// the Handle* call, which turns clicks and drags into InventoryOps requests.
namespace InventoryInput {

// ---- Hotbar + backpack (InventoryUI) ----

struct HotbarLayout {
    float scale = 1.0f;          ///< resolution scale (screen height / reference height)
    float sz = 0, gap = 0;       ///< slot size and gap, scaled
    float hx = 0, hy = 0;        ///< hotbar row top-left
    float rowW = 0;              ///< width of one row of slots
    int   hotbar = 1;            ///< slots per row (the hotbar)
    int   total = 0;             ///< slots shown (InventoryUI::SlotCount)
    int   rows = 0;              ///< backpack rows
    bool  anchorTop = false;

    /// Top of backpack row `row` (rows grow away from the docked edge).
    float RowY(int row) const;
    /// Top-left of slot `idx` (hotbar first, then the backpack rows).
    void SlotXY(int idx, float& x, float& y) const;
    /// The slot a point picks: the one it's inside, else (for gaps / imperfect aim) the
    /// nearest centre within a slot's reach, else -1.
    int Pick(float px, float py) const;

    /// Equipment column beside the backpack (while it's open and the carrier has an
    /// Equipment): one slot per equipment slot, aligned with the hotbar's edge.
    Equipment* equip = nullptr;
    int   equipN = 0;
    float ex = 0;                ///< column left
    void EquipXY(int i, float& x, float& y) const;
    /// Equipment slot under a point (exact hit only), or -1.
    int PickEquip(float px, float py) const;
};
HotbarLayout LayoutHotbar(const InventoryUI& ui, float W, float H);

struct HotbarFrame { int hover = -1; int target = -1; int equipHover = -1; };
/// Pick up (left = whole stack, right = half/one), shift-click quick move, drop on
/// release. Clears the drag when the backpack is closed. `hover` is the slot under the
/// mouse while not dragging; `target` the slot a held stack would drop into.
/// With an equipment column: releasing a bag stack on an equipment slot equips it,
/// and dragging a worn item onto the bag (or anywhere off the column) unequips it.
/// `equipHover` is the equipment slot under the mouse.
HotbarFrame HandleHotbar(InventoryUI& ui, const HotbarLayout& L, Vec2 mouse);

// ---- Chest screen (ChestInventory: chest grid on top, the player's below) ----

struct ChestLayout {
    float panelX = 0, topY = 0, panelW = 0, totalH = 0, chestH = 0;
    float chestOx = 0, chestGy = 0, playOx = 0, playGy = 0;
    float cs = 0, cg = 0, ps = 0, pg = 0;   ///< chest / player slot size and gap
    int   cc = 1, pc = 1;                   ///< columns
    int   chestN = 0, playN = 0;            ///< slots shown
    Inventory* chest = nullptr;
    Inventory* player = nullptr;

    float Size(int side) const { return side == 1 ? ps : cs; }
    int   Count(int side) const { return side == 1 ? playN : chestN; }
    Inventory* Inv(int side) const { return side == 1 ? player : chest; }
    void SlotXY(int side, int i, float& x, float& y) const;
    bool Pick(float px, float py, int& side, int& idx) const;
};
ChestLayout LayoutChest(const ChestInventory& ch, float W, float H);

struct ChestFrame { int side = -1; int index = -1; };
/// Press on a stack to lift it, release on a slot to move / swap / merge it.
ChestFrame HandleChest(ChestInventory& ch, const ChestLayout& L, Vec2 mouse);

// ---- Grid bags (GridInventoryUI: equipped column + Nearby column) ----

struct GridPanel { GridInventory* inv = nullptr; float ox = 0, oy = 0, gw = 0, gh = 0; std::string label; bool ground = false; };

struct GridLayout {
    std::vector<GridInventory*> equipped, nearby;
    std::vector<GridPanel> panels;
    float cs = 0, gp = 0;                       ///< cell size and gap
    float labelH = 0, colGap = 0, pad = 0, headH = 0, headGap = 0;
    float lw = 0, lh = 0, rw = 0, rh = 0;       ///< equipped / nearby column sizes
    float startX = 0, startY = 0;
    bool  hasNear = false;

    /// Panel + cell under a point; false if none.
    bool Pick(float px, float py, int& panel, int& cx, int& cy) const;
};
/// Gathers the containers, lays them out and cancels a drag whose container is gone.
GridLayout LayoutGrid(GridInventoryUI& ui, GridInventory& primary, float W, float H);

struct GridFrame { int panel = -1, cx = 0, cy = 0; };
GridFrame HoverGrid(const GridLayout& L, Vec2 mouse);
/// Rotate key, pick up, right-click split, drop (move / transfer / merge).
void HandleGrid(GridInventoryUI& ui, const GridLayout& L, const GridFrame& hover);

// ---- Crafting bench (CraftingStation) ----

struct CraftingLayout {
    float px = 0, py = 0, pw = 0, ph = 0, rowH = 0;
    int   rows = 0;
    float RowY(int i) const { return py + i * rowH + 4.0f; }
    /// Recipe row under a point, or -1.
    int Pick(float x, float y) const;
};
CraftingLayout LayoutCrafting(const CraftingStation& cs, float W, float H);
/// Click a row to craft one, shift-click to craft as many as you can afford. Returns
/// the hovered row (-1 none).
int HandleCrafting(CraftingStation& cs, const CraftingLayout& L, Vec2 mouse);

} // namespace InventoryInput
} // namespace okay
