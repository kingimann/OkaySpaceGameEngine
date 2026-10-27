#include "okay/Inventory/InventoryInput.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Inventory/InventoryOps.hpp"
#include <algorithm>
#include <cmath>

namespace okay {
namespace InventoryInput {

// ---- Hotbar + backpack ----

float HotbarLayout::RowY(int row) const {
    const float y = anchorTop ? hy + sz + 14.0f + row * (sz + gap)
                              : hy - 14.0f - (rows - row) * (sz + gap);
    return y + bagScroll;
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

void HotbarLayout::EquipXY(int i, float& x, float& y) const {
    x = ex;
    y = anchorTop ? hy + i * (sz + gap) : hy + sz - (equipN - i) * (sz + gap) + gap;
}

int HotbarLayout::PickEquip(float px, float py) const {
    for (int i = 0; i < equipN; ++i) {
        float x, y; EquipXY(i, x, y);
        if (px >= x && px < x + sz && py >= y && py < y + sz) return i;
    }
    return -1;
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
    if (ui.open && ui.Inv())
        if ((L.equip = Equipment::Of(ui.Inv()->gameObject))) {
            L.equipN = (int)L.equip->slots.size();
            const float colGap = L.gap * 3.0f + ui.panelPad * 2.0f * L.scale;
            L.ex = L.hx - L.sz - colGap;                              // left of the bag...
            if (L.ex < 0.0f) L.ex = L.hx + L.rowW + colGap;           // ...or right when docked left
        }
    // A backpack taller than the screen scrolls. Content that fits leaves bagScroll at 0,
    // so a closed bag (and every existing layout test) stays put.
    if (ui.open && L.rows > 0) {
        const float step = L.sz + L.gap;
        if (!L.anchorTop) {
            const float far = L.hy - 14.0f - (float)L.rows * step;   // top of the farthest row
            if (far < 8.0f) {
                L.maxScroll = 8.0f - far;
                float s = ui.scroll;
                if (s < 0.0f) s = 0.0f;
                if (s > L.maxScroll) s = L.maxScroll;
                L.bagScroll = s;
            }
        } else {
            const float lastBottom = L.hy + L.sz + 14.0f + (float)(L.rows - 1) * step + L.sz;
            if (lastBottom > H - 8.0f) {
                L.maxScroll = lastBottom - (H - 8.0f);
                float s = ui.scroll;
                if (s < 0.0f) s = 0.0f;
                if (s > L.maxScroll) s = L.maxScroll;
                L.bagScroll = -s;
            }
        }
    }
    return L;
}

HotbarFrame HandleHotbar(InventoryUI& ui, const HotbarLayout& L, Vec2 mouse) {
    HotbarFrame f;
    if (ui.open && L.maxScroll > 0.0f) {
        const float w = Input::MouseWheel();
        if (w > 0.01f || w < -0.01f) {
            float s = ui.scroll - w * 36.0f;
            if (s < 0.0f) s = 0.0f;
            if (s > L.maxScroll) s = L.maxScroll;
            ui.scroll = s;
        }
    }
    Inventory* inv = ui.Inv();
    if (!ui.open || !ui.dragItems || !inv) { ui.dragIndex = -1; ui.dragAmount = 0; ui.equipDrag = -1; return f; }
    const int eq = L.PickEquip(mouse.x, mouse.y);
    const int at = eq >= 0 ? -1 : L.Pick(mouse.x, mouse.y);   // the equipment column wins over a near bag slot
    f.equipHover = eq;
    // A worn item being dragged: drop it in the bag (on a slot, or anywhere off the column).
    if (ui.equipDrag >= 0) {
        if (!L.equip || ui.equipDrag >= L.equipN) { ui.equipDrag = -1; return f; }
        if (at >= 0) f.target = at;
        if (Input::GetMouseButtonUp(0)) {
            if (eq != ui.equipDrag) L.equip->Unequip(ui.equipDrag, *inv, at);
            ui.equipDrag = -1;
        }
        return f;
    }
    if (ui.dragIndex < 0 && eq >= 0 && L.equip && !L.equip->ItemIn(eq).empty() && Input::GetMouseButtonDown(0)) {
        ui.equipDrag = eq;
        return f;
    }
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
        if (eq >= 0 && L.equip) L.equip->EquipFrom(*inv, ui.dragIndex, eq);   // rejected if the slot doesn't take it
        else if (at >= 0) ui.ApplyDrag(ui.dragIndex, at);
        else ui.DropToWorld(ui.dragIndex, ui.dragAmount);   // released away from every slot: drop it in the world
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
    // Same scale as the hotbar: screen height over the player's reference height
    // (720 when that UI doesn't scale). At 720 the pixel sizes match a saved scene.
    const InventoryUI* pui = ch.PlayerUI();
    float ref = 720.0f;
    if (pui && pui->scaleToScreen && pui->referenceHeight > 1.0f) ref = pui->referenceHeight;
    float scale = H > 1.0f ? H / ref : 1.0f;
    if (!(scale > 0.0f)) scale = 1.0f;
    const float cs0 = ch.slotSize, cg0 = ch.gap;
    const float ps0 = pui ? pui->slotSize : cs0, pg0 = pui ? pui->slotGap : cg0;
    L.cc = ch.cols < 1 ? 1 : ch.cols;
    L.pc = pui ? (pui->hotbarSlots < 1 ? 1 : pui->hotbarSlots) : L.cc;
    L.chestN = L.chest ? (L.chest->capacity > 0 ? L.chest->capacity : (int)L.chest->slots.size()) : 0;
    L.playN = L.player ? (L.player->capacity > 0 ? L.player->capacity : (int)L.player->slots.size()) : 0;
    const int chestRows = L.chestN > 0 ? (L.chestN + L.cc - 1) / L.cc : 0;
    const int playRows = L.playN > 0 ? (L.playN + L.pc - 1) / L.pc : 0;
    auto measure = [&](float s) {
        L.scale = s;
        L.cs = cs0 * s; L.cg = cg0 * s; L.ps = ps0 * s; L.pg = pg0 * s;
        const float chestW = L.cc * L.cs + (L.cc - 1) * L.cg;
        const float playW = L.playN > 0 ? L.pc * L.ps + (L.pc - 1) * L.pg : 0.0f;
        L.panelW = std::max(chestW, playW);
        L.chestH = chestRows * (L.cs + L.cg);
        L.totalH = L.chestH + 30.0f * s + playRows * (L.ps + L.pg);
        L.chestOx = (L.panelW - chestW) * 0.5f;   // filled in once panelX is known
        L.playOx = (L.panelW - playW) * 0.5f;
    };
    measure(scale);
    if (L.panelW > W - 24.0f && L.panelW > 1.0f) measure(scale * ((W - 24.0f) / L.panelW));
    L.panelX = (W - L.panelW) * 0.5f;
    L.chestOx += L.panelX;
    L.playOx += L.panelX;
    if (L.totalH > H - 80.0f) {
        L.scrolling = true;
        L.maxScroll = L.totalH - (H - 80.0f);
        float s = ch.scroll;
        if (s < 0.0f) s = 0.0f;
        if (s > L.maxScroll) s = L.maxScroll;
        L.scroll = s;
        L.topY = 40.0f - s;
    } else {
        L.scrolling = false;
        L.maxScroll = 0;
        L.scroll = 0;
        L.topY = (H - L.totalH) * 0.5f + 16.0f * L.scale;
    }
    L.chestGy = L.topY;
    L.playGy = L.topY + L.chestH + 24.0f * L.scale;
    return L;
}

ChestFrame HandleChest(ChestInventory& ch, const ChestLayout& L, Vec2 mouse) {
    ChestFrame f;
    if (L.scrolling) {
        const float w = Input::MouseWheel();
        if (w > 0.01f || w < -0.01f) {
            float s = ch.scroll - w * 36.0f;
            if (s < 0.0f) s = 0.0f;
            if (s > L.maxScroll) s = L.maxScroll;
            ch.scroll = s;
        }
    }
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
    if (ui.multiContainer) ui.CollectContainers(L.equipped, L.nearby);
    else L.equipped.push_back(&primary);
    // Same scale as the chest: authored pixels at 720, then shrink to the safe width.
    float scale = H > 1.0f ? H / 720.0f : 1.0f;
    if (!(scale > 0.0f)) scale = 1.0f;
    const float cs0 = ui.cellSize < 24.0f ? 24.0f : ui.cellSize;
    const float gp0 = ui.gap < 0.0f ? 0.0f : ui.gap;
    const float label0 = ui.headerHeight < 16.0f ? 16.0f : ui.headerHeight;
    const float vspace0 = ui.containerGap < 4.0f ? 4.0f : ui.containerGap;
    const float colGap0 = ui.columnGap < 8.0f ? 8.0f : ui.columnGap;
    const float pad0 = ui.panelPad < 4.0f ? 4.0f : ui.panelPad;
    const float head0 = ui.showMasterTitle ? 30.0f : 0.0f;
    const float headGap0 = ui.showMasterTitle ? 18.0f : 6.0f;
    float vspace = vspace0;
    auto colSize = [&](const std::vector<GridInventory*>& list, float& cw, float& ch) {
        cw = 0; ch = 0;
        for (auto* g : list) {
            cw = std::max(cw, g->cols * L.cs + (g->cols - 1) * L.gp);
            ch += L.labelH + (g->rows * L.cs + (g->rows - 1) * L.gp) + vspace;
        }
        if (!list.empty()) ch -= vspace;
    };
    auto measure = [&](float s) {
        L.scale = s;
        L.cs = cs0 * s; L.gp = gp0 * s; L.labelH = label0 * s;
        L.colGap = colGap0 * s; L.pad = pad0 * s; L.headH = head0 * s; L.headGap = headGap0 * s;
        vspace = vspace0 * s;
        L.panels.clear();
        colSize(L.equipped, L.lw, L.lh);
        colSize(L.nearby, L.rw, L.rh);
    };
    measure(scale);
    L.hasNear = !L.nearby.empty();
    float totalW = L.lw + (L.hasNear ? L.colGap + L.rw : 0.0f);
    if (totalW > W - 24.0f && totalW > 1.0f) {
        measure(scale * ((W - 24.0f) / totalW));
        totalW = L.lw + (L.hasNear ? L.colGap + L.rw : 0.0f);
    }
    const float totalH = std::max(L.lh, L.rh);
    L.startX = (W - totalW) * 0.5f;
    const float block = totalH + L.headH + L.headGap;
    if (block > H - 80.0f) {
        L.scrolling = true;
        L.maxScroll = block - (H - 80.0f);
        float s = ui.scroll;
        if (s < 0.0f) s = 0.0f;
        if (s > L.maxScroll) s = L.maxScroll;
        ui.scroll = s;
        L.scroll = s;
        L.startY = 40.0f - s + L.headH + L.headGap;
    } else {
        L.scrolling = false;
        L.maxScroll = 0;
        L.scroll = 0;
        ui.scroll = 0;
        L.startY = (H - totalH) * 0.5f + 14.0f * L.scale + L.headH + L.headGap;
    }
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
    if (L.maxScroll > 0.0f) {
        const float w = Input::MouseWheel();
        if (w > 0.01f || w < -0.01f) {
            float s = ui.scroll - w * 36.0f;
            if (s < 0.0f) s = 0.0f;
            if (s > L.maxScroll) s = L.maxScroll;
            ui.scroll = s;
        }
    }
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

// ---- Crafting bench ----

int CraftingLayout::Pick(float x, float y) const {
    for (int i = 0; i < rows; ++i) {
        const float ry = RowY(i);
        if (x >= px + 4 && x < px + pw - 4 && y >= ry && y < ry + rowH - 2) return i;
    }
    return -1;
}

CraftingLayout LayoutCrafting(const CraftingStation& cs, float W, float H) {
    CraftingLayout L;
    L.rows = (int)cs.Recipes().size();
    L.rowH = cs.rowHeight;
    L.pw = cs.width;
    L.ph = L.rows * cs.rowHeight + 8.0f;
    L.px = (W - L.pw) * 0.5f + cs.marginX;
    L.py = (H - L.ph) * 0.5f + cs.marginY;
    return L;
}

int HandleCrafting(CraftingStation& cs, const CraftingLayout& L, Vec2 mouse) {
    const int row = L.Pick(mouse.x, mouse.y);
    if (row >= 0 && Input::GetMouseButtonDown(0)) {
        if (Input::GetKey(Input::KeyShift)) cs.CraftMax(row);
        else cs.Craft(row);
    }
    return row;
}

} // namespace InventoryInput
} // namespace okay
