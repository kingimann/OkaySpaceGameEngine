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
    return L;
}

HotbarFrame HandleHotbar(InventoryUI& ui, const HotbarLayout& L, Vec2 mouse) {
    HotbarFrame f;
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
    if (scrolling && (py < viewY || py >= viewY + viewH)) { side = -1; idx = -1; return false; }
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

int ChestLayout::HitButton(float px, float py) const {
    auto in = [&](const ClickRect& b) { return b.w > 0 && px >= b.x && px < b.x + b.w && py >= b.y && py < b.y + b.h; };
    if (in(takeAll)) return 0;
    if (in(stash)) return 1;
    if (in(sort)) return 2;
    return -1;
}

ChestLayout LayoutChest(ChestInventory& ch, float W, float H) {
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
    const float chrome = 50.0f;
    const float avail = std::max(120.0f, H - 36.0f);
    if (L.totalH + chrome > avail && avail > chrome) {
        L.scrolling = true;
        L.viewH = avail - chrome;
        L.viewY = 40.0f;
        L.topY = L.viewY;
        L.chestGy = L.topY;
        L.playGy = L.topY + L.chestH + 24.0f;
        const float maxScroll = std::max(0.0f, L.totalH - L.viewH);
        const float wheel = Input::MouseWheel();
        if (wheel > 0.5f) ch.scroll -= 48.0f;
        else if (wheel < -0.5f) ch.scroll += 48.0f;
        if (ch.scroll < 0.0f) ch.scroll = 0.0f;
        if (ch.scroll > maxScroll) ch.scroll = maxScroll;
        L.chestGy -= ch.scroll;
        L.playGy -= ch.scroll;
        L.drawH = L.viewH + chrome;
    } else {
        ch.scroll = 0.0f;
        L.drawH = L.totalH + 46.0f;
    }
    const float bw = 62.0f, bh = 16.0f, by = L.topY - 30.0f;
    const float right = L.panelX + L.panelW - 8.0f;
    L.sort    = {right - bw, by, bw, bh};
    L.stash   = {right - bw * 2 - 4, by, bw, bh};
    L.takeAll = {right - bw * 3 - 8, by, bw, bh};
    return L;
}

ChestFrame HandleChest(ChestInventory& ch, const ChestLayout& L, Vec2 mouse) {
    ChestFrame f;
    if (Input::GetMouseButtonDown(0)) {
        const int b = L.HitButton(mouse.x, mouse.y);
        if (b >= 0) {
            if (b == 0) ch.TakeAll();
            else if (b == 1) ch.StashMatching();
            else ch.SortContents();
            ch.dragSide = -1; ch.dragIndex = -1;
            return f;
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
            GridPanel p{g, colX + (colW - gw) * 0.5f, y + L.labelH, gw, gh,
                        g->category.empty() ? g->title : g->category, ground};
            if (ground) {   // "Take all" sits in the container's header, right side
                p.actW = 66.0f; p.actH = L.labelH - 2.0f;
                p.actX = p.ox + gw - p.actW; p.actY = p.oy - L.labelH;
            }
            L.panels.push_back(p);
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

namespace {
/// Drop grid item `index` into the first container in `dests` that can hold it
/// (a matching stack, else the first free spot, rotated if that's the only way).
bool AutoPlace(GridInventory& src, int index, const std::vector<GridInventory*>& dests) {
    if (index < 0 || index >= (int)src.items.size()) return false;
    const GridItem& it = src.items[index];
    for (GridInventory* d : dests) {
        if (!d || d == &src) continue;
        for (const GridItem& o : d->items)
            if (o.name == it.name && src.MoveTo(index, *d, o.x, o.y, false)) return true;
        int x = 0, y = 0;
        if (d->FindSpot(it.w, it.h, x, y) && src.MoveTo(index, *d, x, y, false)) return true;
        if (it.w != it.h && d->FindSpot(it.h, it.w, x, y) && src.MoveTo(index, *d, x, y, true)) return true;
    }
    return false;
}
void TakeAllInto(GridInventory& src, const std::vector<GridInventory*>& dests) {
    for (int guard = 0; guard < 512 && !src.items.empty(); ++guard) {
        const int before = (int)src.items.size();
        if (!AutoPlace(src, (int)src.items.size() - 1, dests)) {
            bool any = false;
            for (int i = (int)src.items.size() - 2; i >= 0 && !any; --i) any = AutoPlace(src, i, dests);
            if (!any || (int)src.items.size() == before) break;
        }
    }
}
} // namespace

void HandleGrid(GridInventoryUI& ui, const GridLayout& L, const GridFrame& h) {
    if (ui.rotateKey && ui.dragInv && Input::GetKeyDown(ui.rotateKey)) ui.RotateDrag();   // applied only on a good drop
    GridInventory* under = h.panel >= 0 ? L.panels[h.panel].inv : nullptr;
    if (!ui.dragInv && Input::GetMouseButtonDown(0)) {
        for (const GridPanel& P : L.panels) {
            if (!P.ground || P.actW <= 0 || !P.inv) continue;
            if (Input::MousePosition().x >= P.actX && Input::MousePosition().x < P.actX + P.actW &&
                Input::MousePosition().y >= P.actY && Input::MousePosition().y < P.actY + P.actH) {
                TakeAllInto(*P.inv, L.equipped);
                return;
            }
        }
    }
    // Shift-click sends the item to the other column (bag <-> nearby) without a drag.
    if (!ui.dragInv && under && Input::GetMouseButtonDown(0) && Input::GetKey(Input::KeyShift)) {
        const int idx = under->ItemAtCell(h.cx, h.cy);
        if (idx >= 0) {
            const bool fromGround = h.panel >= 0 && L.panels[h.panel].ground;
            AutoPlace(*under, idx, fromGround ? L.equipped : L.nearby);
            if (!fromGround && L.nearby.empty()) {
                std::vector<GridInventory*> others;
                for (GridInventory* g : L.equipped) if (g != under) others.push_back(g);
                AutoPlace(*under, idx, others);
            }
        }
        return;
    }
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
