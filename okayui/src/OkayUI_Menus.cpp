#include "OkayUI_ContextState.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "OkayUIInternal.hpp"
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUICompat.hpp"
#include "okay/UI/OkayUIInternals.hpp"

namespace OkayUI {
using namespace ui_core;

Vec2 GetMousePosOnOpeningCurrentPopup() {
    if (g_popupTop > 0) { const PopupSlot* p = popupSlot(g_popupStack[g_popupTop - 1].id, false); if (p) return Vec2{p->x, p->y}; }
    return Vec2{g_in.mouseX, g_in.mouseY};
}
bool AnyPopupOpen() { for (const PopupSlot& p : g_popups) if (p.used && p.open) return true; return false; }
void ClosePopupToLevel(int level, bool) { if (level <= 0) for (PopupSlot& p : g_popups) p.open = false; else if (level == 1) for (PopupSlot& p : g_popups) if (p.open && !p.modal) p.open = false; }
int ListOpenPopups(PopupSlotInfo* out, int cap) {
    int n = 0;
    for (const PopupSlot& p : g_popups) {
        if (!p.used || !p.open || n >= cap) continue;
        PopupSlotInfo& o = out[n++];
        o.id = (unsigned)p.id; o.pos = Vec2{p.x, p.y}; o.modal = p.modal;
        std::snprintf(o.name, sizeof o.name, "%s", p.name); o.openedFrame = p.opened; o.drawnFrame = p.drawn;
        o.size = Vec2{p.w > 0.0f ? p.w : 200.0f, *floatState(p.id ^ 0x9090, rowH() * 3.0f) + 12.0f};
    }
    return n;
}
bool ClosePopup(const char* name) {
    if (!name) return false;
    PopupSlot* p = popupSlot(hashLabel(name), false);
    if (!p || !p->open) return false;
    p->open = false;
    return true;
}
bool ClosePopupById(unsigned id) {
    PopupSlot* p = popupSlot((int)id, false);
    if (!p || !p->open) return false;
    p->open = false;
    return true;
}
int CurrentPopupDepth() { return g_popupTop; }
WindowRef* GetTopMostPopupModal() {
    static WindowRef ref;
    for (const PopupSlot& p : g_popups) if (p.used && p.open && p.modal) { ref = WindowRef(); ref.ID = (unsigned)p.id; ref.Pos = Vec2{p.x, p.y}; ref.Size = Vec2{p.w, 0.0f}; ref.Active = ref.WasActive = true; ref.RootWindow = &ref; return &ref; }
    return nullptr;
}
namespace { bool g_barReserved = false; LayoutState g_barSavedLay; }   // a Window_MenuBar row in use: the layout to give back at EndMenuBar
void ui_core::menuBarReset() { g_barReserved = false; }

// The menu bars' buttons, as drawn this frame and last frame, so a bar behaves as one control (standard menu-bar behaviour): while one
// of its menus is open, moving the pointer onto another of its buttons switches to that menu without a click; Left / Right in an open menu
// (with no sub-menu taking the key) go to the neighbouring menu; a tap of Alt alone opens the first menu of the first bar, keyboard-ready.
namespace {
struct BarBtn { unsigned bar; int id; float x, y, w, h; bool open; };
constexpr int kBarBtnMax = 48;
struct BarMem { BarBtn b[kBarBtnMax]; int n = 0; };
BarMem g_barPrev, g_barNow;
unsigned long long g_barFrame = ~0ull;
unsigned g_curBar = 0;                 // the bar being drawn (from its place on screen)
bool g_altAlone = false;               // Alt went down and nothing else happened since
int g_barOpenReq = 0;                  // a menu to open when its button is drawn (Alt, Left / Right)
int g_barKbOpen = 0;                   // that menu was opened from the keyboard: highlight its first row
unsigned long long g_barKeyFrame = ~0ull;   // the frame Left / Right last switched menus (one switch per key press)
void barFrame() {
    if (g_barFrame == g_frameCount) return;
    g_barFrame = g_frameCount;
    g_barPrev = g_barNow; g_barNow.n = 0;
}
const BarBtn* barPrevOpen(unsigned bar) {
    for (int i = 0; i < g_barPrev.n; ++i) if (g_barPrev.b[i].bar == bar && g_barPrev.b[i].open) return &g_barPrev.b[i];
    return nullptr;
}
// The neighbour of menu `id` on its bar (last frame's order), `dir` -1 or +1, wrapping round.
int barNeighbour(unsigned bar, int id, int dir) {
    int idx[kBarBtnMax]; int n = 0, at = -1;
    for (int i = 0; i < g_barPrev.n; ++i) if (g_barPrev.b[i].bar == bar) { if (g_barPrev.b[i].id == id) at = n; idx[n++] = i; }
    if (at < 0 || n < 2) return 0;
    return g_barPrev.b[idx[(at + dir + n) % n]].id;
}
}
bool BeginMenuBar() {
    if (!g_lay.active) return false;
    const float h = rowH();
    float x, y, bw = fullW();
    if (g_we.open && g_we.menuBarH > 0.0f && g_childTop == 0 && g_popupTop == 0 && !g_barReserved) {
        // the window kept a row for its menu bar: draw it there, wherever the window's cursor is, and lay the bar's items out on it
        g_barSavedLay = g_lay; g_barReserved = true;
        x = g_we.x; y = g_we.y + g_we.titleH; bw = g_we.w;
        g_lay.cx = x; g_lay.cy = y; g_lay.pendingSameLine = false; g_lay.hasLineTop = false;
        g_lay.prevX = x; g_lay.prevY = y; g_lay.prevW = 0.0f; g_lay.prevH = h;
        g_lay.ox = x; g_lay.bottom = y + h;
    } else {
        place(bw, h, x, y);
    }
    barFrame();
    {   // the bar's id from where it is drawn; a far-away place (a hidden window lays out at 1e30) is clamped first, because
        // turning a float larger than any int into an int is undefined (UBSan: "1e+30 is outside the range of ... 'int'")
        const auto cell = [](float v) { return (unsigned)(int)std::floor(std::fmax(-1.0e9f, std::fmin(1.0e9f, v))); };
        g_curBar = (cell(x) * 73856093u) ^ (cell(y) * 19349663u) ^ 0x6ba5u;
    }
    {   // Alt tapped alone: open the first menu of the first bar (Windows' menu key); Alt again or Escape closes it
        bool other = g_pressed || g_rPressed || g_mPressed || g_in.wheel != 0.0f || g_in.mouseDown || g_in.ctrl || g_in.shift
                  || g_in.enter || g_in.escape || g_in.up || g_in.down || g_in.left || g_in.right || g_in.backspace || g_in.del || (g_in.text && *g_in.text);
        for (int k = 1; k < KeyId_COUNT && !other; ++k) if (k != KeyId_Alt && k != KeyId_RightAlt && k != KeyId_ModAlt && IsKeyDown((KeyId)k)) other = true;   // any other key held with it: a chord
        if (IsKeyPressed(KeyId_Alt, false) || IsKeyPressed(KeyId_RightAlt, false)) g_altAlone = !other;
        else if (other) g_altAlone = false;
        if (g_altAlone && !IsKeyDown(KeyId_Alt) && !IsKeyDown(KeyId_RightAlt) && (IsKeyReleased(KeyId_Alt) || IsKeyReleased(KeyId_RightAlt))) {
            g_altAlone = false;
            const bool firstBar = g_barNow.n == 0;
            if (firstBar && g_popupTop == 0) {
                if (const BarBtn* o = barPrevOpen(g_curBar)) *openState(o->id, false) = false;   // a bar menu is open: Alt closes it
                else for (int i = 0; i < g_barPrev.n; ++i) if (g_barPrev.b[i].bar == g_curBar) { g_barOpenReq = g_barKbOpen = g_barPrev.b[i].id; break; }
            }
        }
    }
    if (colorIsSet(Col_MenuBarBg)) quad(x, y, bw, h, pick(Col_MenuBarBg, g_theme.track));
    quad(x, y + h - 1.0f, bw, 1.0f, g_theme.border);   // a hairline under the bar
    g_inMenuBar = true; g_barX = x; g_barY = y; g_barH = h;
    return true;
}

void EndMenuBar() {
    g_inMenuBar = false;
    if (g_barReserved) { g_barReserved = false; g_lay = g_barSavedLay; }
}


namespace {
// The height of the open list's rows.
float menuRowH() { return g_curMenu.rowH > 0.0f ? g_curMenu.rowH : rowH(); }
// The open list starts at `y` (level with its row, or under its bar button). Moved up when last frame's height would run past the
// display's bottom (Input::displayH), as Dear ImGui keeps a menu inside its viewport; taller than the display, it starts at the top
// and scrolls. Sets itemStartY (the visible top), virtTop and itemY (row 0, scroll included).
void menuBodyBegin();
void menuListBegin(int id, float y, bool nested) {
    // As wide as last frame's widest row or widget (Dear ImGui fits a menu to its content); kept inside the display.
    float* wide = floatState(id ^ 0x6d1c0d, 0.0f);
    g_curMenu.widthSlot = wide; g_curMenu.wantW = 0.0f;
    if (*wide > g_curMenu.w) {
        g_curMenu.w = *wide;
        if (nested && g_curMenu.leftward) g_curMenu.x = std::max(0.0f, g_curMenu.anchorR - g_curMenu.w);   // a list on the left keeps its right edge
        else if (g_dispW > 0.0f && g_curMenu.x + g_curMenu.w > g_dispW)
            g_curMenu.x = nested ? std::max(0.0f, g_curMenu.bx - g_curMenu.w) : std::max(0.0f, g_dispW - g_curMenu.w);
    }
    float* full = floatState(id ^ 0x6d1a0b, 0.0f);
    float* scroll = floatState(id ^ 0x6d1b0c, 0.0f);
    g_curMenu.heightSlot = full; g_curMenu.scrollSlot = scroll;
    g_curMenu.clipY0 = g_curMenu.clipY1 = 0.0f;
    float top = y, off = 0.0f;
    if (g_dispH > 0.0f && *full > 0.0f) {
        if (*full > g_dispH) {   // does not fit at all: the whole display, scrolled
            top = 0.0f;
            g_curMenu.clipY0 = 0.0f; g_curMenu.clipY1 = g_dispH;
            const float maxOff = *full - g_dispH, rh = menuRowH();
            *scroll = std::clamp(*scroll, 0.0f, maxOff);
            off = *scroll >= maxOff ? maxOff : std::floor(*scroll / rh + 0.5f) * rh;   // whole rows (the last step shows the end)
            off = std::min(off, maxOff);
        } else {
            if (top + *full > g_dispH) top = g_dispH - *full;
            *scroll = 0.0f;
        }
    } else *scroll = 0.0f;
    g_curMenu.itemStartY = top; g_curMenu.virtTop = top - off; g_curMenu.itemY = top - off;
    // The list's popover box, from last frame's height (the first frame of a new list has none: its rows paint their own
    // background, as before).
    g_curMenu.boxed = *full > 0.0f;
    if (g_curMenu.boxed) {
        const bool wasOverlay = g_toOverlay;
        g_toOverlay = true;
        const float bh = g_curMenu.clipY1 > g_curMenu.clipY0 ? g_curMenu.clipY1 - g_curMenu.clipY0 : *full;
        popoverBox(g_curMenu.x, top - 4.0f, g_curMenu.w, bh + 8.0f, pick(Col_PopupBg, g_theme.overlay));
        g_toOverlay = wasOverlay;
    }
    menuBodyBegin();
}
// The open list's body: a popup-window layout at the list (Dear ImGui's menus are popup windows), so any widget drawn between BeginMenu and
// EndMenu is laid out in the list, below the rows before it, drawn on top, unclipped by the window under it, and takes the pointer by the
// popup rules. Its rows keep their own cursor (itemY); menuRowSync keeps the two in step.
void menuBodyBegin() {
    g_curMenu.body = false;
    if (g_popupTop >= kPopupStackMax) return;
    PopupCtx& c = g_popupStack[g_popupTop++];
    c = PopupCtx{};
    c.lay = g_lay; c.id = g_curMenu.menuId; c.x = g_curMenu.x; c.y = g_curMenu.itemStartY; c.w = g_curMenu.w;
    c.hSlot = g_curMenu.heightSlot; c.wSlot = g_curMenu.widthSlot; c.padX = g_curMenu.textInset; c.padY = 0.0f; c.exact = false;
    c.menu = true; c.wasOverlay = g_toOverlay; c.childTop = g_childTop;
    c.clipBase = g_clipBase; g_clipBase = g_clipTop; overlayClipOff();
    g_toOverlay = true;
    g_curMenu.body = true;
    const float ox = g_curMenu.x + g_curMenu.textInset;
    g_lay.active = true;
    g_lay.ox = ox; g_lay.oy = g_curMenu.itemY;
    g_lay.winX = g_curMenu.x; g_lay.winY = g_curMenu.itemStartY; g_lay.winW = g_curMenu.w;
    g_lay.winH = g_curMenu.heightSlot ? *g_curMenu.heightSlot : 0.0f; g_lay.baseX = g_curMenu.x; g_lay.baseY = g_curMenu.itemStartY;
    g_lay.cx = ox; g_lay.cy = g_curMenu.itemY;
    g_lay.contentW = g_curMenu.w - 2.0f * g_curMenu.textInset;
    g_lay.indent = 0.0f; g_lay.seed = g_curMenu.menuId;
    g_lay.pendingSameLine = false; g_lay.hasLineTop = false;
    // GetItemRect right after BeginMenu is the menu's row (or bar button), as in Dear ImGui
    g_lay.prevX = g_curMenu.bx; g_lay.prevY = g_curMenu.by; g_lay.prevW = g_curMenu.bw; g_lay.prevH = g_curMenu.bh;
    g_lay.maxRight = g_curMenu.x;
    if (g_dispH > 0.0f) g_lay.bottom = g_dispH;   // a menu reaches down to the display's bottom at most
}
// The body ends (EndMenu): the list's height and width for next frame take in the widgets, the layout of what the menu was opened from
// comes back.
void menuBodyEnd() {
    if (!g_curMenu.body || g_popupTop <= 0 || !g_popupStack[g_popupTop - 1].menu) return;
    g_curMenu.itemY = std::max(g_curMenu.itemY, g_lay.cy - g_lay.spacingY * 0.5f);   // widgets after the last row
    const float wide = std::max(g_curMenu.wantW, g_lay.maxRight > g_curMenu.x ? g_lay.maxRight - g_curMenu.x + g_curMenu.textInset : 0.0f);
    if (g_curMenu.widthSlot) *g_curMenu.widthSlot = wide;
    PopupCtx& c = g_popupStack[--g_popupTop];
    while (g_clipTop > g_clipBase) PopClipRect();   // a child left open inside the list
    g_clipBase = c.clipBase;
    g_toOverlay = c.wasOverlay;
    if (g_popupTop > 0) overlayClipToStack(); else overlayClipOff();
    g_lay = c.lay;
    g_curMenu.body = false;
}
// A row of the list (MenuItem, a sub-menu's row, a menu separator) is about to take the row cursor: it goes under any widget drawn since
// the last row. `after` (once the row is placed): the widgets that follow go under it.
void menuRowSync(bool after) {
    if (!g_curMenu.body) return;
    if (!after) { g_curMenu.itemY = std::max(g_curMenu.itemY, g_lay.cy - g_lay.spacingY * 0.5f); return; }
    g_lay.cy = g_curMenu.itemY; g_lay.cx = g_lay.ox; g_lay.pendingSameLine = false; g_lay.hasLineTop = false;
}
// The width a row of the list needs: its label, a gap and its shortcut, the arrow of a sub-menu, the insets.
void menuRowWant(const char* label, const char* shortcut, bool arrow) {
    float w = g_curMenu.textInset * 2.0f + (label ? visW(label) : 0.0f);
    if (shortcut && shortcut[0]) w += labelW(shortcut) + 24.0f;
    if (arrow) w += 20.0f;
    if (w > g_curMenu.wantW) g_curMenu.wantW = w;
}
// Close every menu of the open chain (and the popup a chain opened from a popup's row started from), as picking an item does.
void closeMenuChain() {
    *openState(g_curMenu.menuId, false) = false;
    for (int i = 0; i < g_menuDepth; ++i) *openState(g_menuStack[i].menuId, false) = false;
    for (int i = 0; i < g_menuDepth; ++i)
        if (g_menuStack[i].popupId != 0) { if (PopupSlot* ps = popupSlot(g_menuStack[i].popupId, false)) ps->open = false; }
}
// A row of the open list: a soft rounded highlight inset from the box's edges (or, in an unboxed first frame, the row's own
// background with the highlight on it).
void menuRowBg(float x, float y, float w, float h, bool lit) {
    if (!g_curMenu.boxed) quad(x, y, w, h, pick(Col_PopupBg, g_theme.overlay));
    if (lit) rrect(x + 4.0f, y + 1.0f, w - 8.0f, h - 2.0f, g_theme.radiusSmall, pick(Col_HeaderHovered, g_theme.bgHover));
}
// Is a row at (y, h) of the open list inside its visible band? (always, unless the list scrolls)
bool menuRowShown(float y, float h) {
    if (g_curMenu.clipY1 <= g_curMenu.clipY0) return true;
    return y >= g_curMenu.clipY0 - 0.5f && y + h <= g_curMenu.clipY1 + 0.5f;
}
// The keyboard highlight is on a row outside the band: scroll it into view next frame.
void menuRowReveal(float y, float h) {
    if (g_curMenu.clipY1 <= g_curMenu.clipY0 || !g_curMenu.scrollSlot) return;
    const float rowTop = y - g_curMenu.virtTop;
    if (y < g_curMenu.clipY0) *g_curMenu.scrollSlot = rowTop;
    else if (y + h > g_curMenu.clipY1) *g_curMenu.scrollSlot = rowTop + h - (g_curMenu.clipY1 - g_curMenu.clipY0);
}
// Per-menu keyboard handling at the start of its item list. Only the innermost open menu reacts (the one with no open child).
void menuKeysBegin(int menuId, bool nestedMenu) {
    MenuKb* kb = kbSlot(menuId);
    kb->childOpen = kb->childOpenNext; kb->childOpenNext = false;
    g_curMenu.kb = kb; g_curMenu.n = 0; g_curMenu.mask = 0; g_curMenu.activate = false;
    kb->keyMoved = false;
    if (kb->childOpen) return;
    const int cnt = kb->count;
    auto enabledAt = [&](int i) { return i >= 0 && i < cnt && i < 64 && ((kb->mask >> i) & 1ull); };
    if ((g_in.down || g_in.up) && cnt > 0) {
        kb->keyMoved = true;
        int i = kb->idx;
        for (int step = 0; step < cnt; ++step) {
            i = g_in.down ? (i < 0 ? 0 : (i + 1) % cnt) : (i <= 0 ? cnt - 1 : i - 1);
            if (enabledAt(i)) { kb->idx = i; break; }
        }
    }
    if (g_in.enter && enabledAt(kb->idx)) g_curMenu.activate = true;
    // Escape (as an edge, or as the held key a host passes while no field types) closes the innermost list.
    if (((g_in.escape || IsKeyPressed(KeyId_Escape, false)) && !g_escapeUsed) || (nestedMenu && g_in.left)) {
        *openState(menuId, false) = false;
        if (!(nestedMenu && g_in.left)) g_escapeUsed = true;
    }
}
}

namespace { bool popupRow(const char* label, const char* shortcut, bool selected, bool enabled, bool lit, bool arrow, float& x, float& y, float& w, float& h); }
namespace {
// Where a sub-menu's list goes (its width in g_curMenu.w; the chain so far in chainX0/chainX1, which include the row's own list): beside
// the whole chain, first on the side the chain is going, then on the other; with no room on either side, against its row as before.
void placeSubList(float rowX, float rowW) {
    CurMenu& m = g_curMenu;
    const float W = m.w;
    const float rx = std::max(rowX + rowW, m.chainX1), lx = std::min(rowX, m.chainX0) - W;
    const bool rightFits = !(g_dispW > 0.0f && rx + W > g_dispW), leftFits = lx >= 0.0f;
    if (m.leftward ? leftFits : !rightFits && leftFits) { m.leftward = true; m.x = lx; }
    else if (rightFits) { m.leftward = false; m.x = rx; }
    else { m.leftward = true; m.x = std::max(0.0f, rowX - W); }   // no room beside the chain: on its row's left, as before
    m.anchorR = m.x + W;
}
// The list is placed (and widened by menuListBegin): the chain now takes its width too.
void chainAdd() { g_curMenu.chainX0 = std::min(g_curMenu.chainX0, g_curMenu.x); g_curMenu.chainX1 = std::max(g_curMenu.chainX1, g_curMenu.x + g_curMenu.w); }
}
bool BeginMenu(const char* label) {
    if (g_lay.active && !g_curMenu.active && !g_inMenuBar && label && g_popupTop > 0 && !g_framedWidgets) {   // a submenu of a plain popup: expands in place, indented
        if (!TreeNode(label)) return false;
        ++g_popupSubOpen;
        return true;
    }
    // With StyleVar_FramedWidgets (Dear ImGui's look): a sub-menu row inside a popup (a context menu) that opens a list to its right.
    if (g_lay.active && !g_curMenu.active && g_popupTop > 0 && label) {
        if (g_menuDepth >= 4) return false;
        const int id = hashLabel(label) ^ 0x7e11;
        bool* open = openState(id, false);
        float x, y, w, h;
        const bool ih = popupRow(label, nullptr, false, true, *open, true, x, y, w, h);
        g_lastId = id;
        if (ih) { g_hot = id; *open = true; }   // hovering the row opens its list
        if (!*open) return false;
        CurMenu from;                           // the popup's row: an inactive menu the sub-menu returns to
        from.popupId = g_popupStack[g_popupTop - 1].id;
        from.bx = x; from.by = y; from.bw = w; from.bh = h;
        g_menuStack[g_menuDepth++] = from;
        g_curMenu = CurMenu();
        g_curMenu.active = true; g_curMenu.menuId = id;
        g_curMenu.w = g_popupW > 0.0f ? g_popupW : 180.0f;
        g_curMenu.chainX0 = x; g_curMenu.chainX1 = x + w;   // the popup (its row is as wide as the popup)
        placeSubList(x, w);
        g_curMenu.bx = x; g_curMenu.by = y; g_curMenu.bw = w; g_curMenu.bh = h;
        g_curMenu.fromPopup = true;
        g_curMenu.rowH = h;   // the popup's own row height (a text line and the item spacing)
        g_curMenu.textInset = (g_popupPadX >= 0.0f ? g_popupPadX : g_lay.spacingX * 0.5f + 4.0f);
        menuListBegin(id, y, true);
        chainAdd();
        menuKeysBegin(id, true);
        return true;
    }
    if (g_lay.active && g_curMenu.active && label) {   // a sub-menu: a row of the open menu that opens a list to its right
        if (g_menuDepth >= 4) return false;
        const int id = hashLabel(label) ^ g_curMenu.menuId ^ 0x5bu;
        bool* open = openState(id, false);
        menuRowSync(false);
        menuRowWant(label, nullptr, true);
        const float h = menuRowH(), s = g_theme.textScale;
        const float x = g_curMenu.x, w = g_curMenu.w, y = g_curMenu.itemY;
        g_curMenu.itemY += h;
        menuRowSync(true);
        g_lay.prevX = x; g_lay.prevY = y; g_lay.prevW = w; g_lay.prevH = h; g_lastId = id; g_prevInOverlay = true;
        const bool shown = menuRowShown(y, h);
        const bool ih = shown && hitOverlay(x, y, w, h);
        const int ord = g_curMenu.n++;
        if (ord < 64) g_curMenu.mask |= 1ull << ord;
        MenuKb* pk = g_curMenu.kb;
        if (ih) { g_hot = id; *open = true; if (pk) pk->idx = ord; }          // hovering the row opens it (and highlights it)
        const bool ksel = pk && pk->idx == ord && !pk->childOpen;
        if (ksel && !shown && pk->keyMoved) menuRowReveal(y, h);
        if (ksel && (g_in.right || g_curMenu.activate)) *open = true;          // Right or Enter on the highlighted row opens it
        if (shown) {
            const bool wasOverlay = g_toOverlay;   // inside a popup (a context menu's sub-menu) the popup's later rows stay on top too
            g_toOverlay = true;
            const float ty = y + (h - textH()) * 0.5f;
            menuRowBg(x, y, w, h, ih || *open || ksel);
            drawText(x + g_curMenu.textInset, ty, label, s, g_theme.text);
            if (g_curMenu.fromPopup) {   // Dear ImGui's arrow, as on the popup's own sub-menu rows
                const float th = textH(), cx = x + w - g_curMenu.textInset - th * 0.5f, cy = ty + th * 0.5f;
                chevronDir(cx, cy, std::min(8.0f, th * 0.5f), 0, pick(Col_TextDisabled, g_theme.textDim));
            } else chevron(x + w - 16.0f, y + h * 0.5f, 7.0f, false, pick(Col_TextDisabled, g_theme.textDim));
            g_toOverlay = wasOverlay;
        }
        if (!*open) { kbSlot(id)->idx = -1; return false; }
        if (pk) pk->childOpenNext = true;
        g_menuStack[g_menuDepth++] = g_curMenu;
        g_curMenu.menuId = id;
        g_curMenu.w = (g_curMenu.fromPopup && g_popupW > 0.0f) ? g_popupW : 180.0f;
        placeSubList(x, w);   // beside every list of the chain
        g_curMenu.bx = x; g_curMenu.by = y; g_curMenu.bw = w; g_curMenu.bh = h;
        g_curMenu.subX = g_curMenu.subY = g_curMenu.subW = g_curMenu.subH = 0.0f;
        g_curMenu.body = false; g_curMenu.widthSlot = nullptr; g_curMenu.wantW = 0.0f;
        menuListBegin(id, y, true);
        chainAdd();
        menuKeysBegin(id, true);
        return true;
    }
    if (!g_lay.active || !g_inMenuBar) return false;
    const int id = hashLabel(label);
    bool* open = openState(id, false);
    const float h = g_barH, s = g_theme.textScale;
    const float bw = visW(label) + 16.0f;
    const float x = g_barX, y = g_barY;
    g_barX += bw;   // advance the bar cursor for the next menu
    g_lay.prevX = x; g_lay.prevY = y; g_lay.prevW = bw; g_lay.prevH = h; g_lay.pendingSameLine = false;   // SameLine after a menu continues after it

    const bool inside = hit(x, y, bw, h);
    g_lastId = id; if (inside) g_hot = id;
    barFrame();
    {   // the bar as one control: hovering another button while a menu of this bar is open switches to it
        const BarBtn* was = barPrevOpen(g_curBar);
        if (inside && !*open && was && was->id != id) *open = true;
        if (*open && !inside) {   // this menu is open and the pointer moved onto another button of the bar: it closes
            for (int i = 0; i < g_barPrev.n; ++i) {
                const BarBtn& o = g_barPrev.b[i];
                if (o.bar == g_curBar && o.id != id && pointIn(g_in.mouseX, g_in.mouseY, o.x, o.y, o.w, o.h) && (g_mouseDX != 0.0f || g_mouseDY != 0.0f)) { *open = false; break; }
            }
        }
        if (g_barOpenReq == id) { g_barOpenReq = 0; *open = true; }
    }
    if (g_active == id) { if (g_released) { if (inside) *open = !*open; g_active = 0; } }
    else if (inside && g_pressed) g_active = id;
    if (g_barNow.n < kBarBtnMax) g_barNow.b[g_barNow.n++] = BarBtn{g_curBar, id, x, y, bw, h, *open};
    {   // a soft rounded chip under the pointer or while its menu is open
        unsigned char tmp[4];
        const float over = anim(id, 0, *open || inside);
        if (over > 0.0f) rrect(x + 2.0f, y + 3.0f, bw - 4.0f, h - 6.0f, g_theme.radiusSmall, withAlpha(g_theme.bgHover, over, tmp));
    }
    drawText(x + 8.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);

    if (*open) {
        g_curMenu = CurMenu();
        g_curMenu.active = true; g_curMenu.menuId = id;
        g_curMenu.x = x; g_curMenu.w = 180.0f;
        g_curMenu.bx = x; g_curMenu.by = y; g_curMenu.bw = bw; g_curMenu.bh = h;
        if (g_barKbOpen == id) { g_barKbOpen = 0; kbSlot(id)->idx = 0; }   // opened from the keyboard: its first row is highlighted
        menuListBegin(id, y + h, false);
        g_curMenu.chainX0 = g_curMenu.x; g_curMenu.chainX1 = g_curMenu.x + g_curMenu.w;   // the bar's list starts the chain
        menuKeysBegin(id, false);
    } else kbSlot(id)->idx = -1;
    return *open;
}

namespace {
// A menu row inside a popup body (a context menu), Dear ImGui's way: the item is one text line in the popup's layout and its highlight
// reaches half the item spacing past it on each side, so consecutive rows touch. The label starts at the row; a shortcut is right-aligned
// and dim; a checked item has a tick at the right end; a disabled one is dim and takes no clicks. `arrow` draws a sub-menu's arrow.
// Returns whether the row is under the pointer; (x, y, w, h) is the highlight rectangle.
bool popupRow(const char* label, const char* shortcut, bool selected, bool enabled, bool lit, bool arrow, float& x, float& y, float& w, float& h) {
    const float s = g_theme.textScale, th = textH();
    float ix, iy; w = fullW(); place(w, th, ix, iy);
    x = ix - g_lay.spacingX * 0.5f; y = iy - g_lay.spacingY * 0.5f; w += g_lay.spacingX; h = th + g_lay.spacingY;
    const bool ih = enabled && hit(x, y, w, h);
    g_lay.prevX = x; g_lay.prevY = y; g_lay.prevW = w; g_lay.prevH = h;
    {   // the row's real need (label, a gap and the shortcut, the tick or arrow): the popup grows to it, so a long row keeps a margin
        // on the right instead of running into the edge ("Find References in Scene")
        const float need = labelW(label) + (shortcut && shortcut[0] ? labelW(shortcut) + 24.0f : 0.0f) + (selected || arrow ? th + 4.0f : 0.0f) + g_lay.spacingX * 0.5f;
        if (ix + need > g_lay.maxRight) g_lay.maxRight = ix + need;
    }
    if (ih || lit) rrect(x + 2.0f, y, w - 4.0f, h, std::min(g_theme.radiusSmall, h * 0.3f), pick(Col_HeaderHovered, g_theme.bgHover));   // a rounded highlight inset from the popup's sides
    const unsigned char* col = enabled ? g_theme.text : pick(Col_TextDisabled, g_theme.textDim);
    drawText(ix, iy, label, s, col);
    const float right = ix + w - g_lay.spacingX;
    if (shortcut && shortcut[0]) {
        const float sw = labelW(shortcut), sx = right - sw - (selected || arrow ? th + 4.0f : 0.0f);
        if (sx > ix + labelW(label) + 12.0f) drawText(sx, iy, shortcut, s, pick(Col_TextDisabled, g_theme.textDim));
    }
    if (selected) {   // a tick in the text colour, a text line wide
        const float a = th * 0.25f, cx = right - th * 0.5f, cy = iy + th * 0.5f;
        line(cx - a * 1.2f, cy, cx - a * 0.3f, cy + a * 0.9f, 2.0f, col);
        line(cx - a * 0.3f, cy + a * 0.9f, cx + a * 1.3f, cy - a * 0.9f, 2.0f, col);
    }
    if (arrow) {      // a right-pointing triangle a text line tall at the right end, as Dear ImGui's sub-menu rows have
        const float cx = right - th * 0.5f, cy = iy + th * 0.5f;
        chevronDir(cx, cy, std::min(8.0f, th * 0.5f), 0, col);
    }
    return ih;
}
bool popupMenuItem(const char* label, const char* shortcut, bool selected, bool enabled) {
    float x, y, w, h;
    const int iid = hashLabel(label) ^ 0x31a7;
    const bool ih = popupRow(label, shortcut, selected, enabled, false, false, x, y, w, h);
    g_lastId = iid; if (ih) g_hot = iid;
    bool clicked = false;
    if (g_active == iid) { if (g_released) { if (ih) clicked = true; g_active = 0; } }
    else if (ih && g_pressed) g_active = iid;
    if (clicked) CloseCurrentPopup();
    return clicked;
}
bool menuItemImpl(const char* label, const char* shortcut, bool selected, bool enabled) {
    if (g_lay.active && !g_curMenu.active && !g_inMenuBar && label && g_popupTop > 0 && !g_framedWidgets) {   // inside a plain popup (a context menu): a selectable row
        const float wantW = labelW(label) + (shortcut && *shortcut ? labelW(shortcut) + 40.0f : 0.0f) + 24.0f;   // room for the label, a gap and the shortcut
        const bool clicked = Selectable(label, selected, (unsigned)(Selectable_ClosePopup | (enabled ? 0 : Selectable_Disabled)), Vec2{ wantW, 0.0f });
        if (shortcut && *shortcut) {
            const Rect r = GetItemRect();
            const float tw = labelW(shortcut);
            if (tw < r.w - 24.0f) GetWindowDrawList().AddText(Vec2{ r.x + r.w - tw - 8.0f, r.y + (r.h - textH()) * 0.5f }, 0xff000000u | ((unsigned)g_theme.textDim[2] << 16) | ((unsigned)g_theme.textDim[1] << 8) | g_theme.textDim[0], shortcut);
        }
        return clicked;
    }
    // With StyleVar_FramedWidgets: Dear ImGui's menu row (one text line, the highlight reaching half the item spacing past it).
    if (g_lay.active && !g_curMenu.active && g_popupTop > 0 && label) return popupMenuItem(label, shortcut, selected, enabled);
    if (!g_lay.active || !g_curMenu.active || !label) return false;
    menuRowSync(false);
    menuRowWant(label, shortcut, selected && g_framedWidgets);   // (a tick at the right end needs the room an arrow does)
    const float h = menuRowH(), s = g_theme.textScale;
    const float x = g_curMenu.x, w = g_curMenu.w, y = g_curMenu.itemY;
    g_curMenu.itemY += h;
    menuRowSync(true);
    const int iid = hashLabel(label) ^ g_curMenu.menuId;
    const bool shown = menuRowShown(y, h);
    const bool ih = shown && enabled && hitOverlay(x, y, w, h);
    // the row is the last item: IsItemHovered / GetItemRect after MenuItem ask about it (a tooltip on a menu row), as in Dear ImGui
    g_lay.prevX = x; g_lay.prevY = y; g_lay.prevW = w; g_lay.prevH = h; g_lastId = iid; g_prevInOverlay = true;
    if (ih) g_hot = iid;
    const int ord = g_curMenu.n++;
    if (enabled && ord < 64) g_curMenu.mask |= 1ull << ord;
    MenuKb* kb = g_curMenu.kb;
    if (ih && kb) kb->idx = ord;
    const bool ksel = kb && kb->idx == ord && enabled && !kb->childOpen;
    if (ksel && !shown && kb->keyMoved) menuRowReveal(y, h);
    bool clicked = false;
    if (ksel && g_curMenu.activate) clicked = true;   // Enter on the highlighted item
    if (g_active == iid) {
        if (g_released) { if (ih) clicked = true; g_active = 0; }
    } else if (ih && g_pressed) g_active = iid;
    if (clicked) closeMenuChain();   // the whole chain closes (and a popup it was opened from, as a context menu does)
    if (!shown) return clicked;
    const unsigned char* col = enabled ? g_theme.text : pick(Col_TextDisabled, g_theme.textDim);
    const bool wasOverlay = g_toOverlay;   // a sub-menu of a popup: the popup's rows after it must stay on top
    g_toOverlay = true;
    const float in = g_curMenu.textInset;
    menuRowBg(x, y, w, h, ih || ksel);
    if (selected) {   // a tick marks a checked item: in the left inset, or at the right end in Dear ImGui's look (the inset there is too narrow: it covered the label)
        const float tk = g_framedWidgets ? std::min(h - 6.0f, textH()) : std::min(h - 6.0f, in + 2.0f);
        tickMark(g_framedWidgets ? x + w - in - tk : x + (in - tk) * 0.5f + 1.0f, y + (h - tk) * 0.5f, tk, g_theme.accent);
    }
    drawText(x + in, y + (h - textH()) * 0.5f, VisLabel(label).b, s, col);
    if (shortcut && shortcut[0]) {   // right-aligned, faint; skipped when it would run into the label
        const float sw = labelW(shortcut), sx = x + w - sw - in - (selected && g_framedWidgets ? textH() + 6.0f : 0.0f);
        if (sx > x + in + visW(label) + 12.0f) drawText(sx, y + (h - textH()) * 0.5f, shortcut, s, pick(Col_TextDisabled, g_theme.textFaint));
    }
    g_toOverlay = wasOverlay;
    return clicked;
}
} // namespace

bool MenuItem(const char* label) { return menuItemImpl(label, nullptr, false, true); }
bool MenuItem(const char* label, const char* shortcut, bool selected, bool enabled) { return menuItemImpl(label, shortcut, selected, enabled); }

void MenuSeparator() {
    if (!g_lay.active || !g_curMenu.active) return;
    menuRowSync(false);
    const float x = g_curMenu.x, w = g_curMenu.w, y = g_curMenu.itemY, h = 8.0f;
    g_curMenu.itemY += h;
    menuRowSync(true);
    if (!menuRowShown(y, h)) return;
    const bool wasOverlay = g_toOverlay;
    g_toOverlay = true;
    if (!g_curMenu.boxed) quad(x, y, w, h, pick(Col_PopupBg, g_theme.overlay));
    quad(x + 8.0f, y + h * 0.5f, w - 16.0f, 1.0f, pick(Col_Separator, g_theme.border));
    g_toOverlay = wasOverlay;
}

void EndMenu() {
    if (!g_curMenu.active) { if (g_popupSubOpen > 0) { --g_popupSubOpen; TreePop(); } return; }
    menuBodyEnd();
    const bool nested = g_menuDepth > 0;
    // A press outside both the menu button (or the sub-menu's row) and its item list closes the menu.
    // The list as drawn: from its visible top to its last row, or the band it scrolls in.
    const bool scrolls = g_curMenu.clipY1 > g_curMenu.clipY0;
    const float fullH = g_curMenu.itemY - g_curMenu.virtTop;
    if (g_curMenu.heightSlot) *g_curMenu.heightSlot = fullH;   // where next frame's list starts depends on it
    const float listY0 = g_curMenu.itemStartY;
    const float listY1 = scrolls ? std::min(g_curMenu.itemY, g_curMenu.clipY1) : g_curMenu.itemY;
    const bool inBtn  = pointIn(g_in.mouseX, g_in.mouseY, g_curMenu.bx, g_curMenu.by, g_curMenu.bw, g_curMenu.bh);
    const bool inList = pointIn(g_in.mouseX, g_in.mouseY, g_curMenu.x, listY0, g_curMenu.w, listY1 - listY0);
    if (scrolls && g_curMenu.scrollSlot && g_in.wheel != 0.0f && !g_wheelUsed && hitOverlay(g_curMenu.x, listY0, g_curMenu.w, listY1 - listY0)) {
        *g_curMenu.scrollSlot -= g_in.wheel * menuRowH() * 3.0f;   // three rows a notch
        g_wheelUsed = true;
    }
    const bool inSub  = g_curMenu.subW > 0.0f && pointIn(g_in.mouseX, g_in.mouseY, g_curMenu.subX, g_curMenu.subY, g_curMenu.subW, g_curMenu.subH);
    if (g_pressed && !inBtn && !inList && !inSub) *openState(g_curMenu.menuId, false) = false;
    // A sub-menu also closes when the pointer leaves its row, its list and any list opened from it.
    // (only while the mouse is moving: a sub-menu opened from the keyboard stays open under a parked pointer)
    if (nested && !inBtn && !inList && !inSub && (g_mouseDX != 0.0f || g_mouseDY != 0.0f)) *openState(g_curMenu.menuId, false) = false;
    if (g_curMenu.kb) { g_curMenu.kb->count = g_curMenu.n; g_curMenu.kb->mask = g_curMenu.mask; }
    const bool hasItems = listY1 > listY0;
    if (hasItems)
        registerOverlay(g_curMenu.x, listY0, g_curMenu.w, listY1 - listY0, false);
    if (nested) {   // back to the menu this one was opened from; it needs to know where this list is
        const CurMenu child = g_curMenu;
        g_curMenu = g_menuStack[--g_menuDepth];
        // the sub-menu's row in the list it was opened from is the last item again (not the last row of its own list)
        g_lay.prevX = child.bx; g_lay.prevY = child.by; g_lay.prevW = child.bw; g_lay.prevH = child.bh; g_prevInOverlay = true;
        if (hasItems) {   // the bounding box of its list and of any list opened from it (a press in either is "inside" for the outer menus)
            float x0 = child.x, y0 = listY0, x1 = child.x + child.w, y1 = listY1;
            if (child.subW > 0.0f) { x0 = std::min(x0, child.subX); y0 = std::min(y0, child.subY); x1 = std::max(x1, child.subX + child.subW); y1 = std::max(y1, child.subY + child.subH); }
            g_curMenu.subX = x0; g_curMenu.subY = y0; g_curMenu.subW = x1 - x0; g_curMenu.subH = y1 - y0;
            if (!g_curMenu.active && g_popupTop > 0) {   // back in the popup the chain started from: a press in these lists is inside it
                PopupCtx& pc = g_popupStack[g_popupTop - 1];
                if (pc.subX1 <= pc.subX0) { pc.subX0 = x0; pc.subY0 = y0; pc.subX1 = x1; pc.subY1 = y1; }
                else { pc.subX0 = std::min(pc.subX0, x0); pc.subY0 = std::min(pc.subY0, y0); pc.subX1 = std::max(pc.subX1, x1); pc.subY1 = std::max(pc.subY1, y1); }
            }
        }
        return;
    }
    // Left / Right in an open bar menu that no sub-menu took: the neighbouring menu of the bar opens instead (Escape closes, as before)
    if (g_inMenuBar && g_curMenu.kb && !g_curMenu.kb->childOpen && !g_curMenu.kb->childOpenNext && (g_in.left || g_in.right) && *openState(g_curMenu.menuId, false)
        && g_barKeyFrame != g_frameCount) {
        const int nb = barNeighbour(g_curBar, g_curMenu.menuId, g_in.right ? 1 : -1);
        if (nb != 0) { *openState(g_curMenu.menuId, false) = false; g_barOpenReq = g_barKbOpen = nb; *openState(nb, false) = true; g_barKeyFrame = g_frameCount; }
    }
    g_curMenu.active = false;
    // a bar menu ends: its button is the last item again (SameLine in the bar continues after it, not after the list's last row)
    g_lay.prevX = g_curMenu.bx; g_lay.prevY = g_curMenu.by; g_lay.prevW = g_curMenu.bw; g_lay.prevH = g_curMenu.bh; g_prevInOverlay = !g_inMenuBar && g_popupTop > 0;
}

static bool selectableImpl(const char* label, bool selected, unsigned flags, Vec2 size, bool* p_sel) {
    if (!g_lay.active) return false;
    if (p_sel) selected = *p_sel;
    const int id = hashLabel(label);
    const bool disabled = (flags & Selectable_Disabled) != 0;
    const float s = g_theme.textScale;
    // Dear ImGui's Selectable (StyleVar_FramedWidgets): a text line tall unless a height is given, the label at the item, and its highlight
    // and hit box reach half the item spacing past it on every side, so consecutive rows touch (Selectable_Header keeps its own box).
    const bool imguiRow = g_framedWidgets && !(flags & Selectable_Header);
    const float h = (size.y > 0.0f && std::isfinite(size.y)) ? size.y : (imguiRow ? textH() : rowH());
    float w = (size.x > 0.0f && std::isfinite(size.x)) ? size.x : fullW();
    float x, y; place(w, h, x, y);
    const float textX = x;                        // the label starts at the cell, even when the highlight spans the table
    const float itemY = y, itemH = h;
    if ((flags & Selectable_SpanAllColumns) && g_tbl.active) {
        x = g_tbl.x0 - (g_tbl.scrollX ? g_tbl.sx : 0.0f); w = g_tbl.fullW;
        g_lay.prevX = x; g_lay.prevW = w;
    }
    float bx = x, by = y, bw = w, bh = h;   // the highlight and hit box
    if (imguiRow) { bx -= g_lay.spacingX * 0.5f; by -= g_lay.spacingY * 0.5f; bw += g_lay.spacingX; bh += g_lay.spacingY; }
    x = bx; y = by; w = bw;
    const float hh = bh;
    const bool inside = !disabled && hit(x, y, w, hh);
    g_lastId = id; if (inside) g_hot = id;
    bool clicked = false;
    const bool dblOnly = (flags & Selectable_DoubleClickOnly) != 0;
    if (g_active == id) { if (g_released) { if (inside && !dblOnly) clicked = true; g_active = 0; } }
    else if (inside && g_pressed) { g_active = id; if (dblOnly && g_dblClick) clicked = true; }
    const unsigned char* tcol = disabled ? pick(Col_TextDisabled, g_theme.textDim) : g_theme.text;
    const float ty = itemY + (itemH - textH()) * 0.5f;
    if ((flags & Selectable_Header) || imguiRow) {   // Dear ImGui's rows in their places, drawn OkayUI's way: the header colours on a rounded
                                                     // highlight that eases in under the pointer; a selected row has an accent bar at its left end
        unsigned char tmp[4], hv[4];
        const unsigned char* bg = nullptr;
        const float over = anim(id, 0, inside && g_active != id);
        if (inside && g_active == id) bg = pick(Col_HeaderActive, g_theme.accent);
        else if (selected)            bg = pick(Col_Header, withAlpha(g_theme.accent, 0.28f, tmp));
        else if (over > 0.0f)         bg = withAlpha(pick(Col_HeaderHovered, g_theme.bgHover), over, hv);
        const float rd = std::min(g_theme.radiusSmall, hh * 0.3f);
        if (bg) rrect(x, y, w, hh, rd, bg);
        if (selected && inside && g_active != id && over > 0.0f) rrect(x, y, w, hh, rd, withAlpha(g_theme.ink, 0.06f * over, hv));   // a selected row still reacts
        if (selected && imguiRow && hh >= 10.0f) rrect(x + 1.0f, y + 3.0f, 2.0f, hh - 6.0f, 1.0f, g_theme.accent);
        drawText(imguiRow ? textX : textX + g_lay.spacingX * 0.5f, ty, imguiRow ? VisLabel(label).b : label, s, tcol);
    } else {   // the design's rows: a soft accent tint and a bar on the left when selected, a faint ink wash under the pointer
        unsigned char tmp[4];
        const float over = anim(id, 0, inside && !selected);
        const float rd = std::min(g_theme.radiusSmall + 1.0f, h * 0.5f);
        if (selected) { rrect(x, y, w, h, rd, pick(Col_HeaderActive, g_theme.selection)); rrect(x, y + 5.0f, 3.0f, h - 10.0f, 1.5f, g_theme.accent); }
        else if (over > 0.0f) rrect(x, y, w, h, rd, pick(Col_HeaderHovered, withAlpha(g_theme.ink, 0.07f * over, tmp)));
        drawText(textX + (g_look ? 10.0f : 6.0f), ty, VisLabel(label).b, s, tcol);
    }
    if (clicked) {
        if (p_sel) *p_sel = !*p_sel;
        if (flags & Selectable_ClosePopup) CloseCurrentPopup();
        else if (imguiRow && !(flags & Selectable_DontClosePopups) && g_popupTop > 0 && g_childTop == g_popupStack[g_popupTop - 1].childTop)
            CloseCurrentPopup();   // Dear ImGui: a Selectable right in a popup (a combo's list, a context menu) closes it; one in a child of it does not
    }
    return clicked;
}
bool Selectable(const char* label, bool selected) { return selectableImpl(label, selected, 0, Vec2{}, nullptr); }
bool Selectable(const char* label) { return selectableImpl(label, false, 0, Vec2{}, nullptr); }
bool Selectable(const char* label, bool selected, unsigned flags, Vec2 size) { return selectableImpl(label, selected, flags, size, nullptr); }
bool Selectable(const char* label, bool* p_selected, unsigned flags, Vec2 size) { return selectableImpl(label, false, flags, size, p_selected); }

void OpenPopup(const char* id) {
    PopupSlot* p = popupSlot(hashLabel(id), true);
    if (p && id && !p->open) { std::snprintf(p->name, sizeof p->name, "%s", id); p->opened = g_frameCount; }
    if (p) { p->open = true; p->x = g_in.mouseX; p->y = g_in.mouseY; p->w = 0.0f; p->modal = false; p->fixedW = false; p->fixedH = 0.0f; p->shownW = 0.0f; g_openPopupThisFrame = p->id; }
}

void CloseCurrentPopup() {
    if (g_popupTop <= 0) return;
    if (g_popupStack[g_popupTop - 1].menu && g_curMenu.active) { closeMenuChain(); return; }   // inside a menu's list: the menus close, as in Dear ImGui
    PopupSlot* p = popupSlot(g_popupStack[g_popupTop - 1].id, false); if (p) p->open = false;
}

// Shared popup body setup: redirect layout into a floating overlay box at (px,py),
// drawing the background using last frame's measured height. Returns true if open.
bool ui_core::beginPopupBody(int id) {
    clearNextWindowData();   // a popup uses up what SetNextWindow* set before it, open or not: it must not move or size the next window
    // Depth guard first: past the popup-stack capacity the parent layout cannot
    // be saved, so the popup cannot begin -- otherwise EndPopup would restore a
    // state that was never saved and stomp another popup's measured height.
    if (g_popupTop >= kPopupStackMax) return false;
    PopupSlot* p = popupSlot(id, false);
    if (!p || !p->open) return false;
    p->drawn = g_frameCount;
    // StyleVar_PopupWidth / StyleVar_PopupPadding (when pushed) give the size; with a pushed padding the height is the content's
    // exactly (no item spacing after the last item), as in Dear ImGui.
    // A modal is a window: its padding is the window padding in force when it begins (Dear ImGui's WindowPadding, which the launcher's
    // dialogs push; without it a dialog's text sat 6 pixels from its edge and the Software Update dialog's notes touched it).
    const float modalPadX = std::clamp(std::isfinite(g_theme.windowPadding) ? g_theme.windowPadding : 14.0f, 6.0f, 40.0f);
    const float modalPadY = std::clamp(g_childPadY >= 0.0f && std::isfinite(g_childPadY) ? g_childPadY : modalPadX, 6.0f, 40.0f);
    const float padX = g_popupPadX >= 0.0f ? g_popupPadX : (p->modal ? modalPadX : 6.0f), padY = g_popupPadY >= 0.0f ? g_popupPadY : (p->modal ? modalPadY : 6.0f);
    float* hSlot = floatState(id ^ 0x9090, rowH() * 3.0f);
    float* wSlot = floatState(id ^ 0x9091, 0.0f);   // the widest line of last frame's content (plus padding): the popup grows to fit it
    if (p->fixedH > 0.0f) *hSlot = std::max(0.0f, p->fixedH - 2.0f * padY);   // a height asked for: the box keeps it
    float w = p->w > 0.0f ? p->w : (g_popupW > 0.0f ? g_popupW : 160.0f);
    // A modal with a least width grows to its content in the frames after it opens; later only for a line that is clearly longer (more than
    // 24 px over): a control sized from the space left (a field filling the row, a suffix after it) would otherwise widen it a little every frame.
    const bool mayGrow = !p->leastW || g_frameCount <= p->opened + 3 || *wSlot > w + 24.0f;
    if (!p->fixedW && !(g_popupW > 0.0f) && *wSlot > w && mayGrow) w = *wSlot;   // a pushed StyleVar_PopupWidth is the width (the Dear ImGui-looking menus); a popup with a width asked for (SetNextWindowSize before a modal) keeps it: items that fill the width would make it grow every frame
    if (g_dispW > 32.0f && w > g_dispW - 16.0f) w = g_dispW - 16.0f;
    if (p->modal && g_dispW > 0.0f) { p->x = (g_dispW - w) * 0.5f; if (!p->fixedW) p->w = w; }
    p->shownW = w;   // a modal stays centred when it grew (and reports the width it has)
    float px = p->x, py = p->y;
    // A combo's list goes where Dear ImGui's documented combo placement puts it: under the box toward the right; if that does not fit
    // in the display, above the box toward the right; then under it toward the left (its right edge on the box's right edge); then
    // above it toward the left. Only when none fits is it pushed inside like any popup.
    bool placed = false;
    if (p->combo && g_dispW > 0.0f && g_dispH > 0.0f) {
        const float ph = *hSlot + padY * 2.0f;
        const float cand[4][2] = {{p->x, p->y}, {p->x, p->boxTop - ph}, {p->boxRight - w, p->y}, {p->boxRight - w, p->boxTop - ph}};
        for (const auto& c : cand)
            if (c[0] >= 0.0f && c[1] >= 0.0f && c[0] + w <= g_dispW && c[1] + ph <= g_dispH) { px = c[0]; py = c[1]; placed = true; break; }
    }
    // Kept inside the display (Input::displayW/H), as Dear ImGui keeps a popup inside its viewport: moved left and up as needed.
    if (!placed && g_framedWidgets && g_dispW > 0.0f && px + w > g_dispW) px = std::max(0.0f, g_dispW - w);
    if (!placed && g_framedWidgets && g_dispH > 0.0f && py + *hSlot + padY * 2.0f > g_dispH) py = std::max(0.0f, g_dispH - (*hSlot + padY * 2.0f));
    // Frame (overlay so it sits above everything). Height is last frame's content.
    registerOverlay(px, py, w, *hSlot + padY * 2.0f, true);
    g_toOverlay = true;
    popoverBox(px, py, w, *hSlot + padY * 2.0f, pick(Col_PopupBg, pick(Col_WindowBg, g_theme.overlay)));
    // Save parent layout; set up the popup's inner layout.
    PopupCtx& c = g_popupStack[g_popupTop++];
    c.lay = g_lay; c.id = id; c.x = px; c.y = py; c.w = w; c.hSlot = hSlot; c.wSlot = wSlot;
    // A popup is a top-level window: the clips of what it was opened over do not cut it; a child region inside it clips in the overlay.
    c.clipBase = g_clipBase; g_clipBase = g_clipTop; overlayClipOff();
    c.padX = padX; c.padY = padY; c.exact = g_popupPadY >= 0.0f; c.childTop = g_childTop;
    c.subX0 = c.subY0 = c.subX1 = c.subY1 = 0.0f;
    g_lay.active = true;
    g_lay.ox = px + padX; g_lay.oy = py + padY;
    g_lay.winX = px; g_lay.winY = py; g_lay.winW = w; g_lay.winH = *hSlot + padY * 2.0f; g_lay.baseX = px; g_lay.baseY = py;
    g_lay.cx = g_lay.ox; g_lay.cy = g_lay.oy;
    g_lay.contentW = w - 2.0f * padX;
    g_lay.indent = 0.0f; g_lay.seed = id;
    g_lay.pendingSameLine = false;
    g_lay.prevX = g_lay.ox; g_lay.prevY = g_lay.oy; g_lay.prevW = 0.0f; g_lay.prevH = 0.0f;
    g_lay.maxRight = px;   // measure how far the content reaches
    if (p->fixedH > 0.0f) g_lay.bottom = py + *hSlot + padY;   // GetContentRegionAvail ends at the box's inner bottom
    return true;
}

void EndPopup() {
    if (g_popupTop <= 0) return;
    PopupCtx& c = g_popupStack[--g_popupTop];
    PopupSlot* fixedP = popupSlot(c.id, false);
    if (!(fixedP && fixedP->fixedH > 0.0f)) {
        *c.hSlot = g_lay.cy - (c.y + c.padY) - (c.exact ? g_lay.spacingY : 0.0f);   // measured content height for next frame
        if (*c.hSlot < 0.0f) *c.hSlot = 0.0f;
    }
    *c.wSlot = g_lay.maxRight > c.x ? (g_lay.maxRight - c.x) + c.padX : 0.0f;   // and the width it needs, plus padding
    while (g_clipTop > g_clipBase) PopClipRect();   // a child left open inside the popup
    g_clipBase = c.clipBase;
    g_toOverlay = g_popupTop > 0;   // back in the popup this one was opened from: still on top
    if (g_toOverlay) overlayClipToStack();
    else overlayClipOff();
    // Dismiss on a left-click outside the popup box (but not the click that opened it).
    PopupSlot* p = popupSlot(c.id, false);
    // Escape closes the innermost popup, as in Dear ImGui: not while a text field in it has the keyboard (Escape gives the field up),
    // nor while a sub-menu list is open from one of its rows (that list closes first).
    if (p && p->open && !p->modal && !g_escapeUsed && (g_in.escape || IsKeyPressed(KeyId_Escape, false)) &&
        g_focusAtFrameStart == 0 && g_editId == 0 && !(c.subX1 > c.subX0)) {
        p->open = false;
        g_escapeUsed = true;
    }
    if (p && p->open && !p->modal && g_pressed && g_openPopupThisFrame != c.id) {
        const bool inside = pointIn(g_in.mouseX, g_in.mouseY, c.x, c.y, c.w, *c.hSlot + c.padY * 2.0f);
        const bool inSub = c.subX1 > c.subX0 && pointIn(g_in.mouseX, g_in.mouseY, c.subX0, c.subY0, c.subX1 - c.subX0, c.subY1 - c.subY0);
        if (!inside && !inSub) p->open = false;
    }
    if (p && p->open && g_popCurN < 8) g_popCur[g_popCurN++] = PopRect{c.x, c.y, c.w, *c.hSlot + c.padY * 2.0f, g_popupTop + 1};
    // A popup opened from inside another one (a right-click menu on a row of an open list): a press in it is inside its parent too,
    // so the parent stays open while it is used, as in Dear ImGui.
    if (p && p->open && g_popupTop > 0) {
        PopupCtx& pc = g_popupStack[g_popupTop - 1];
        const float x0 = c.x, y0 = c.y, x1 = c.x + c.w, y1 = c.y + *c.hSlot + c.padY * 2.0f;
        if (pc.subX1 <= pc.subX0) { pc.subX0 = x0; pc.subY0 = y0; pc.subX1 = x1; pc.subY1 = y1; }
        else { pc.subX0 = std::min(pc.subX0, x0); pc.subY0 = std::min(pc.subY0, y0); pc.subX1 = std::max(pc.subX1, x1); pc.subY1 = std::max(pc.subY1, y1); }
    }
    g_lay = c.lay;   // restore parent layout
}

bool BeginPopup(const char* id) {
    const int pid = hashLabel(id);
    if (id) if (PopupSlot* p = popupSlot(pid, false); p && p->open && !p->name[0]) std::snprintf(p->name, sizeof p->name, "%s", id);   // (opened by id: the name for IsPopupOpen)
    return beginPopupBody(pid);   // (which uses up the SetNextWindow* calls made for it, open or not: Dear ImGui's rule)
}
// By id (what GetID(name) gives, which is also how a named popup is keyed): the popup opened with the same name or id.
void OpenPopup(unsigned id, int) {
    PopupSlot* p = popupSlot((int)id, true);
    if (p && !p->open) p->opened = g_frameCount;
    if (p) { p->open = true; p->x = g_in.mouseX; p->y = g_in.mouseY; p->w = 0.0f; p->modal = false; p->fixedW = false; p->fixedH = 0.0f; p->shownW = 0.0f; g_openPopupThisFrame = p->id; }
}
bool BeginPopupEx(unsigned id, int flags) { (void)flags; return beginPopupBody((int)id); }

bool IsPopupOpen(const char* name) {
    if (!name) return false;
    const PopupSlot* p = popupSlot(hashLabel(name), false);
    if (p && p->open) return true;
    // Asked outside every window (after the frame, as tests and hosts do) about a popup opened inside one: its id took in that window, so it
    // is found by the name it was opened or begun with.
    if (!g_lay.active) for (const PopupSlot& q : g_popups) if (q.used && q.open && q.name[0] && std::strcmp(q.name, name) == 0) return true;
    return false;
}

static bool g_modalNoTitle = false;   // BeginPopupModal(.., Window_NoTitleBar): no title line
static bool g_modalAutoFit = false;   // BeginPopupModal(.., Window_AlwaysAutoResize): the box is its content's size
bool BeginPopupModal(const char* name, bool* open, int flags) {
    g_modalNoTitle = (flags & Window_NoTitleBar) != 0; g_modalAutoFit = (flags & Window_AlwaysAutoResize) != 0;
    const bool r = BeginPopupModal(name, open);
    g_modalNoTitle = false; g_modalAutoFit = false;
    return r;
}
bool BeginPopupModal(const char* name, bool* open) {
    if (!name) return false;   // (outside any window too: a modal is a top-level window in Dear ImGui, begun wherever the code is)
    const int id = hashLabel(name);
    PopupSlot* p = popupSlot(id, false);
    // Not open: the SetNextWindow* calls made for it are used up all the same, as Dear ImGui's BeginPopupModal does; they reached the
    // next modal instead (the launcher's closed New Project dialog left 820 x 680 for "Close OkaySpace?", and for the corner toast).
    if (!p || !p->open) { clearNextWindowData(); if (open) *open = false; return false; }
    p->modal = true;
    if (!p->name[0]) std::snprintf(p->name, sizeof p->name, "%s", name);
    if (g_modalAutoFit) {
        // AlwaysAutoResize: the box is as big as last frame's content in both directions (no 320-pixel floor, no size left over from a
        // SetNextWindowSize meant for something else); only a size given to it in this frame's SetNextWindowSize counts, axis by axis,
        // as in Dear ImGui. (The launcher's "Close OkaySpace?" box was 820 x 680 around one line and three buttons.)
        p->fixedW = false; p->fixedH = 0.0f; p->leastW = false;
        const float* wSlot = floatState(id ^ 0x9091, 0.0f);
        p->w = *wSlot > 0.0f ? *wSlot : 320.0f;
    }
    if (g_nextWinSize.set) {   // SetNextWindowSize before a modal sets its width (the height follows the content); the position is always the centre
        // An AlwaysAutoResize modal takes the width as its least: a longer line makes it grow to hold the line (beginPopupBody), as Dear
        // ImGui's auto-fit does after the frame the size is given on (the editor's Project panel Delete dialog ran its text past the edge).
        if (g_nextWinSize.x > 0.0f && std::isfinite(g_nextWinSize.x)) { p->w = g_nextWinSize.x < 120.0f ? 120.0f : (g_dispW > 0.0f && g_nextWinSize.x > g_dispW - 16.0f ? g_dispW - 16.0f : g_nextWinSize.x); p->fixedW = !g_modalAutoFit; p->leastW = g_modalAutoFit; if (p->leastW && p->shownW > p->w) p->w = std::min(p->shownW, g_dispW > 32.0f ? g_dispW - 16.0f : p->shownW); }
        // a height too (Dear ImGui sizes the modal to it; the editor's file browser lays its list out from GetContentRegionAvail().y)
        if (g_nextWinSize.y > 0.0f && std::isfinite(g_nextWinSize.y)) p->fixedH = g_dispH > 0.0f && g_nextWinSize.y > g_dispH - 16.0f ? g_dispH - 16.0f : g_nextWinSize.y;
        g_nextWinSize.set = false;
    }
    g_nextWinPos.set = false;   // consumed: it must not move the next window
    if (p->w <= 0.0f) p->w = 320.0f;
    if (g_in.escape) { p->open = false; if (open) *open = false; return false; }
    const float pad = g_popupPadY >= 0.0f ? g_popupPadY : std::clamp(g_childPadY >= 0.0f && std::isfinite(g_childPadY) ? g_childPadY : (std::isfinite(g_theme.windowPadding) ? g_theme.windowPadding : 14.0f), 6.0f, 40.0f);   // as beginPopupBody pads it
    float* hSlot = floatState(id ^ 0x9090, rowH() * 3.0f);
    if (p->fixedH > 0.0f) *hSlot = std::max(0.0f, p->fixedH - 2.0f * pad);   // centred by the height it was given
    const float titleH = rowH() + 4.0f;
    if (g_dispW > 0.0f && g_dispH > 0.0f) {   // centre it, using last frame's measured height
        p->x = (g_dispW - p->w) * 0.5f;
        p->y = (g_dispH - (*hSlot + pad * 2.0f)) * 0.5f;
        if (p->y < 0.0f) p->y = 0.0f;
    }
    // Dim and block everything behind it.
    if (g_dispW > 0.0f && g_dispH > 0.0f) {
        registerOverlay(0.0f, 0.0f, g_dispW, g_dispH, true);
        g_toOverlay = true;
        const unsigned char dim[4] = {0, 0, 0, 140};
        quad(0.0f, 0.0f, g_dispW, g_dispH, pick(Col_ModalWindowDimBg, dim));
        g_toOverlay = false;
    }
    if (!beginPopupBody(id)) return false;
    (void)titleH;
    const VisLabel shown(name);
    const bool showTitle = !g_modalNoTitle && shown.b[0] != 0;   // an empty title ("##id") or NoTitleBar: no title line and no rule under it
    if (showTitle) Text(static_cast<const char*>(shown.b));
    if (open && showTitle) {   // a close button at the top-right of the title line
        const float bx = p->x + p->w - pad - rowH(), by = p->y + pad;
        if (Button(id ^ 0x58, bx, by, rowH(), rowH(), "x")) { p->open = false; *open = false; }
    }
    if (showTitle) Separator();
    return true;
}


namespace { int g_comboCloseId = 0; }   // a press on an open combo's box: the release closes it instead of reopening it

// A combo whose list is whatever the caller draws: the box shows `preview`; while it is open the content between
// BeginCombo and EndCombo (usually Selectable rows) fills a popup under the box, as wide as the box.
bool BeginCombo(const char* label, const char* preview) {
    if (!g_lay.active) return false;
    const int id = hashLabel(label);
    const VisLabel vis(label);   // the "##id" part is not shown
    const float h = rowH(), s = g_theme.textScale, b = g_theme.borderPx;
    const float lw = visW(label) > 0.0f ? visW(label) + 8.0f : 0.0f;
    float w = availW();
    // an explicit width is the box's; the label is extra (with StyleVar_FramedWidgets the views pass the whole row's width, label included)
    if (lw > 0.0f && widthOverridden() && !g_framedWidgets) { const float room = realAvailW(); w = w + lw < room ? w + lw : room; }
    float x, y; place(w, h, x, y);
    float boxW = w - lw; if (boxW < 48.0f) boxW = 48.0f;
    PopupSlot* ps = popupSlot(id ^ 0x77ca, true);
    // A press counts only when nothing lies over the box (hit()): a popup over it (an Inspector field's prefab menu over the Axis list
    // below the field) takes the click; the box used to open its list under the menu and the menu's row never got it (seen in the real
    // editor). The hover look stays the pointer in the box, as before.
    const bool insideBox = hitOverlay(x, y, boxW, h) && inClip();
    const bool takesPress = insideBox && ((ps && ps->open) || hit(x, y, boxW, h));   // its own open list may lie over it: it still closes it
    g_lastId = id; if (insideBox) g_hot = id;
    if (g_active == id) {
        if (g_released) {
            if (insideBox && ps) {
                if (g_comboCloseId == id) ps->open = false;
                else if (!ps->open) {
                    ps->open = true; ps->x = x; ps->y = y + h; ps->w = std::max(boxW, g_popupW); g_openPopupThisFrame = ps->id;
                    ps->combo = true; ps->boxTop = y; ps->boxRight = x + boxW;
                }   // a pushed PopupWidth wider than the box widens the list (Dear ImGui fits it to its content)
            }
            g_comboCloseId = 0; g_active = 0;
        }
    } else if (takesPress && g_pressed) { g_active = id; g_comboCloseId = (ps && ps->open) ? id : 0; }
    if (navItem(id, x, y, boxW, h, rnd()) && ps) {   // Tab reaches it; Space or Enter opens (or closes) its list
        if (ps->open) ps->open = false;
        else { ps->open = true; ps->x = x; ps->y = y + h; ps->w = std::max(boxW, g_popupW); g_openPopupThisFrame = ps->id; ps->combo = true; ps->boxTop = y; ps->boxRight = x + boxW; }
    }
    const bool open = ps && ps->open;
    if (g_framedWidgets) framedComboBox(x, y, boxW, h, insideBox, preview);
    else comboBox(id, x, y, boxW, h, insideBox, open, preview);
    (void)b;
    if (lw > 0.0f) drawText(x + boxW + 8.0f, y + (h - textH()) * 0.5f, vis.b, s, g_theme.text);
    if (!ps) return false;
    const bool shown = beginPopupBody(ps->id);
    if (shown) { g_lay.prevX = x; g_lay.prevY = y; g_lay.prevW = boxW; g_lay.prevH = h; }   // GetItemRect right after BeginCombo is the box
    return shown;
}

void EndCombo() { EndPopup(); }

bool BeginPopupContextItem(const char* id) {
    const int pid = id ? hashLabel(id) : (hashLabel("##ctxitem") ^ g_lastId);
    // Right-click on the last-issued widget opens the popup at the cursor.
    // hit() is the test a left click on that widget passes: inside it, inside the clip (not scrolled out of its child), not
    // under a window in front, not in a disabled block, not under an open overlay.
    if (g_lay.active && g_rPressed && hit(g_lay.prevX, g_lay.prevY, g_lay.prevW, g_lay.prevH)) {
        PopupSlot* p = popupSlot(pid, true);
        if (p) { p->open = true; p->x = g_in.mouseX; p->y = g_in.mouseY; p->w = 0.0f; g_openPopupThisFrame = pid; std::snprintf(p->name, sizeof p->name, "%s", id ? id : ""); }
    }
    return beginPopupBody(pid);
}

} // namespace OkayUI

// ---- What belongs to one context (OkayUI_ContextState.hpp: each context keeps its own copy of these) ----
namespace OkayUI {
OKAYUI_CONTEXT_STATE(
    OKAYUI_CTX_VAR(g_barReserved),
    OKAYUI_CTX_VAR(g_barPrev),
    OKAYUI_CTX_VAR(g_barNow),
    OKAYUI_CTX_VAR(g_barFrame),
    OKAYUI_CTX_VAR(g_curBar),
    OKAYUI_CTX_VAR(g_altAlone),
    OKAYUI_CTX_VAR(g_barOpenReq),
    OKAYUI_CTX_VAR(g_barKbOpen),
    OKAYUI_CTX_VAR(g_barKeyFrame),
    OKAYUI_CTX_VAR(g_barSavedLay),
    OKAYUI_CTX_VAR(g_comboCloseId),
    OKAYUI_CTX_VAR(g_modalNoTitle),
    OKAYUI_CTX_VAR(g_modalAutoFit))
} // namespace OkayUI
