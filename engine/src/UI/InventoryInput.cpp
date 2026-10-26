#include "okay/Inventory/InventoryInput.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Inventory/InventoryOps.hpp"
#include <algorithm>
#include <cmath>

namespace okay {
namespace InventoryInput {

// ---- Hotbar + backpack ----

float HotbarLayout::RowY(int row) const {
    return anchorTop ? hy + sz + 14.0f + row * (sz + gap)
                     : hy - 14.0f - (rows - row) * (sz + gap);
}

void HotbarLayout::SlotXY(int idx, float& x, float& y) const {
    if (idx < hotbar) { x = hx + idx * (sz + gap); y = hy; return; }
    const int b = idx - hotbar;
    x = hx + (b % hotbar) * (sz + gap);
    y = RowY(b / hotbar);
}

int HotbarLayout::Pick(float px, float py) const {
    for (int idx = 0; idx < total; ++idx) {   // containment wins: hovering a slot picks THAT slot
        float sx, sy; SlotXY(idx, sx, sy);
        if (px >= sx && px < sx + sz && py >= sy && py < sy + sz) return idx;
    }
    int best = -1;
    float bestD = (sz + gap) * (sz + gap) * 1.1f;   // gaps: nearest centre within reach
    for (int idx = 0; idx < total; ++idx) {
        float sx, sy; SlotXY(idx, sx, sy);
        const float dx = px - (sx + sz * 0.5f), dy = py - (sy + sz * 0.5f), d = dx * dx + dy * dy;
        if (d < bestD) { bestD = d; best = idx; }
    }
    return best;
}

HotbarLayout LayoutHotbar(const InventoryUI& ui, float W, float H) {
    HotbarLayout L;
    L.hotbar = ui.hotbarSlots < 1 ? 1 : ui.hotbarSlots;
    L.total = ui.SlotCount();
    L.rows = ui.BackpackRowCount();
    L.anchorTop = ui.anchorTop;
    // Resolution-independent: scale by the area's height vs the authored reference, so
    // the built game and the editor Game view agree ("Scale With Screen Size").
    L.scale = (ui.scaleToScreen && ui.referenceHeight > 1.0f) ? H / ui.referenceHeight : 1.0f;
    L.sz = ui.slotSize * L.scale;
    L.gap = ui.slotGap * L.scale;
    const float marginX = ui.marginX * L.scale, marginY = ui.marginY * L.scale;
    L.rowW = L.hotbar * L.sz + (L.hotbar - 1) * L.gap;
    L.hx = (ui.hAlign == InventoryUI::HAlign::Left)  ? marginX
         : (ui.hAlign == InventoryUI::HAlign::Right) ? (W - L.rowW - marginX)
         : (W - L.rowW) * 0.5f + marginX;
    L.hy = ui.anchorTop ? marginY : H - L.sz - marginY;
    return L;
}

HotbarFrame HandleHotbar(InventoryUI& ui, const HotbarLayout& L, Vec2 mouse) {
    HotbarFrame f;
    Inventory* inv = ui.Inv();
    if (!ui.open || !ui.dragItems || !inv) { ui.dragIndex = -1; ui.dragAmount = 0; return f; }
    const int at = L.Pick(mouse.x, mouse.y);
    if (ui.dragIndex < 0) f.hover = at;
    else if (at != ui.dragIndex) f.target = at;
    if (ui.dragIndex < 0) {
        const bool onItem = at >= 0 && at < (int)inv->slots.size() && !Inventory::Empty(inv->slots[at]);
        if (onItem && Input::GetMouseButtonDown(0)) {
            if (ui.shiftQuickMove && Input::GetKey(Input::KeyShift)) ui.QuickMove(at);
            else { ui.dragIndex = at; ui.dragAmount = 0; }                 // the whole stack
        } else if (onItem && ui.splitRightClick && Input::GetMouseButtonDown(1) && inv->slots[at].count > 1) {
            ui.dragIndex = at;                                             // half (or one)
            ui.dragAmount = ui.splitHalf ? inv->slots[at].count / 2 : 1;
        }
    }
    // Drop when the held button is released (either button: you held one to drag).
    if (ui.dragIndex >= 0 && (Input::GetMouseButtonUp(0) || Input::GetMouseButtonUp(1))) {
        ui.ApplyDrag(ui.dragIndex, at);
        ui.dragIndex = -1; ui.dragAmount = 0;
    }
    if (ui.dragIndex >= (int)inv->slots.size()) { ui.dragIndex = -1; ui.dragAmount = 0; }
    return f;
}

// ---- Chest screen ----

void ChestLayout::SlotXY(int side, int i, float& x, float& y) const {
    const int cols = side == 1 ? pc : cc;
    const float s = Size(side), g = side == 1 ? pg : cg;
    x = (side == 1 ? playOx : chestOx) + (i % cols) * (s + g);
    y = (side == 1 ? playGy : chestGy) + (i / cols) * (s + g);
}

bool ChestLayout::Pick(float px, float py, int& side, int& idx) const {
    for (int sd = 0; sd < 2; ++sd) {
        if (sd == 1 && !player) continue;
        const float s = Size(sd);
        for (int i = 0; i < Count(sd); ++i) {
            float x, y; SlotXY(sd, i, x, y);
            if (px >= x && px < x + s && py >= y && py < y + s) { side = sd; idx = i; return true; }
        }
    }
    side = -1; idx = -1;
    return false;
}

ChestLayout LayoutChest(const ChestInventory& ch, float W, float H) {
    ChestLayout L;
    L.chest = ch.Inv();
    L.player = ch.PlayerInv();
    // The chest grid uses the chest's size; the player grid uses the player's own
    // inventory UI size/columns, so resizing the chest doesn't resize your inventory.
    L.cs = ch.slotSize; L.cg = ch.gap; L.cc = ch.cols < 1 ? 1 : ch.cols;
    const InventoryUI* pui = ch.PlayerUI();
    L.ps = pui ? pui->slotSize : L.cs;
    L.pg = pui ? pui->slotGap : L.cg;
    L.pc = pui ? (pui->hotbarSlots < 1 ? 1 : pui->hotbarSlots) : L.cc;
    L.chestN = L.chest ? (L.chest->capacity > 0 ? L.chest->capacity : (int)L.chest->slots.size()) : 0;
    L.playN = L.player ? (L.player->capacity > 0 ? L.player->capacity : (int)L.player->slots.size()) : 0;
    const int chestRows = (L.chestN + L.cc - 1) / L.cc, playRows = L.playN > 0 ? (L.playN + L.pc - 1) / L.pc : 0;
    const float chestW = L.cc * L.cs + (L.cc - 1) * L.cg, playW = L.pc * L.ps + (L.pc - 1) * L.pg;
    L.panelW = std::max(chestW, playW);
    L.chestH = chestRows * (L.cs + L.cg);
    L.totalH = L.chestH + 30.0f + playRows * (L.ps + L.pg);
    L.panelX = (W - L.panelW) * 0.5f;
    L.topY = (H - L.totalH) * 0.5f + 16.0f;
    L.chestOx = L.panelX + (L.panelW - chestW) * 0.5f;
    L.playOx = L.panelX + (L.panelW - playW) * 0.5f;
    L.chestGy = L.topY;
    L.playGy = L.topY + L.chestH + 24.0f;
    return L;
}

ChestFrame HandleChest(ChestInventory& ch, const ChestLayout& L, Vec2 mouse) {
    ChestFrame f;
    L.Pick(mouse.x, mouse.y, f.side, f.index);
    if (ch.dragSide < 0 && f.index >= 0 && Input::GetMouseButtonDown(0)) {   // lift a stack
        Inventory* inv = L.Inv(f.side);
        if (inv && f.index < (int)inv->slots.size() && !Inventory::Empty(inv->slots[f.index])) {
            ch.dragSide = f.side; ch.dragIndex = f.index;
        }
    }
    if (ch.dragSide >= 0 && Input::GetMouseButtonUp(0)) {                   // drop: move / swap / merge
        if (f.index >= 0) InventoryOps::Move(L.Inv(ch.dragSide), ch.dragIndex, L.Inv(f.side), f.index);
        ch.dragSide = -1; ch.dragIndex = -1;
    }
    if (ch.dragSide >= 0) {   // the lifted stack must still be there (a script may have changed it)
        Inventory* inv = L.Inv(ch.dragSide);
        if (!inv || ch.dragIndex >= (int)inv->slots.size() || Inventory::Empty(inv->slots[ch.dragIndex])) {
            ch.dragSide = -1; ch.dragIndex = -1;
        }
    }
    return f;
}

// ---- Grid bags ----

bool GridLayout::Pick(float px, float py, int& panel, int& cx, int& cy) const {
    panel = -1;
    for (int pi = 0; pi < (int)panels.size(); ++pi) {
        const GridPanel& P = panels[pi];
        const int x = (int)std::floor((px - P.ox) / (cs + gp)), y = (int)std::floor((py - P.oy) / (cs + gp));
        if (px >= P.ox && py >= P.oy && x >= 0 && y >= 0 && x < P.inv->cols && y < P.inv->rows) {
            panel = pi; cx = x; cy = y;
        }
    }
    return panel >= 0;
}

GridLayout LayoutGrid(GridInventoryUI& ui, GridInventory& primary, float W, float H) {
    GridLayout L;
    // Honour the configured sizes (just a minimal sane floor).
    L.cs = ui.cellSize < 24.0f ? 24.0f : ui.cellSize;
    L.gp = ui.gap < 0.0f ? 0.0f : ui.gap;
    if (ui.multiContainer) ui.CollectContainers(L.equipped, L.nearby);
    else L.equipped.push_back(&primary);
    L.labelH = ui.headerHeight < 16.0f ? 16.0f : ui.headerHeight;
    const float vspace = ui.containerGap < 4.0f ? 4.0f : ui.containerGap;
    L.colGap = ui.columnGap < 8.0f ? 8.0f : ui.columnGap;
    L.pad = ui.panelPad < 4.0f ? 4.0f : ui.panelPad;
    L.headH = ui.showMasterTitle ? 30.0f : 0.0f;
    L.headGap = ui.showMasterTitle ? 18.0f : 6.0f;
    auto colSize = [&](const std::vector<GridInventory*>& list, float& cw, float& ch) {
        cw = 0; ch = 0;
        for (auto* g : list) {
            cw = std::max(cw, g->cols * L.cs + (g->cols - 1) * L.gp);
            ch += L.labelH + (g->rows * L.cs + (g->rows - 1) * L.gp) + vspace;
        }
        if (!list.empty()) ch -= vspace;
    };
    colSize(L.equipped, L.lw, L.lh);
    colSize(L.nearby, L.rw, L.rh);
    L.hasNear = !L.nearby.empty();
    const float totalW = L.lw + (L.hasNear ? L.colGap + L.rw : 0.0f), totalH = std::max(L.lh, L.rh);
    L.startX = (W - totalW) * 0.5f;
    L.startY = (H - totalH) * 0.5f + 14.0f + L.headH + L.headGap;
    auto placeCol = [&](const std::vector<GridInventory*>& list, float colX, float colW, bool ground) {
        float y = L.startY;
        for (auto* g : list) {
            const float gw = g->cols * L.cs + (g->cols - 1) * L.gp, gh = g->rows * L.cs + (g->rows - 1) * L.gp;
            L.panels.push_back({g, colX + (colW - gw) * 0.5f, y + L.labelH, gw, gh,
                                g->category.empty() ? g->title : g->category, ground});
            y += L.labelH + gh + vspace;
        }
    };
    placeCol(L.equipped, L.startX, L.lw, false);
    if (L.hasNear) placeCol(L.nearby, L.startX + L.lw + L.colGap, L.rw, true);
    std::vector<GridInventory*> live(L.equipped);
    live.insert(live.end(), L.nearby.begin(), L.nearby.end());
    ui.CheckDrag(live);   // the bag being dragged from may have gone out of range or been destroyed
    return L;
}

GridFrame HoverGrid(const GridLayout& L, Vec2 mouse) {
    GridFrame f;
    L.Pick(mouse.x, mouse.y, f.panel, f.cx, f.cy);
    return f;
}

void HandleGrid(GridInventoryUI& ui, const GridLayout& L, const GridFrame& h) {
    if (ui.rotateKey && ui.dragInv && Input::GetKeyDown(ui.rotateKey)) ui.RotateDrag();   // applied only on a good drop
    GridInventory* under = h.panel >= 0 ? L.panels[h.panel].inv : nullptr;
    if (!ui.dragInv && under && Input::GetMouseButtonDown(0))
        ui.BeginDrag(under, under->ItemAtCell(h.cx, h.cy), h.cx, h.cy);
    if (!ui.dragInv && under && Input::GetMouseButtonDown(1)) {   // right-click: split half off
        const int idx = under->ItemAtCell(h.cx, h.cy);
        if (idx >= 0) InventoryOps::Split(under, idx, under->items[idx].count / 2);
    }
    // Drop: same container = move, another = transfer (cross-bag / loot), a matching stack = merge.
    if (ui.dragInv && Input::GetMouseButtonUp(0)) ui.Drop(under, h.cx, h.cy);
}

} // namespace InventoryInput
} // namespace okay
