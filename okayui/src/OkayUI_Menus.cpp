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
        o.size = Vec2{p.w > 0.0f ? p.w : 200.0f, *floatState(p.id ^ 0x9090, rowH() * 3.0f) + 12.0f};
    }
    return n;
}
WindowRef* GetTopMostPopupModal() {
    static WindowRef ref;
    for (const PopupSlot& p : g_popups) if (p.used && p.open && p.modal) { ref = WindowRef(); ref.ID = (unsigned)p.id; ref.Pos = Vec2{p.x, p.y}; ref.Size = Vec2{p.w, 0.0f}; ref.Active = ref.WasActive = true; ref.RootWindow = &ref; return &ref; }
    return nullptr;
}
bool BeginMenuBar() {
    if (!g_lay.active) return false;
    const float h = rowH();
    float x, y; place(fullW(), h, x, y);
    if (colorIsSet(Col_MenuBarBg)) quad(x, y, fullW(), h, pick(Col_MenuBarBg, g_theme.track));
    quad(x, y + h - 1.0f, fullW(), 1.0f, g_theme.border);   // a hairline under the bar
    g_inMenuBar = true; g_barX = x; g_barY = y; g_barH = h;
    return true;
}

void EndMenuBar() { g_inMenuBar = false; }


namespace {
// The height of the open list's rows.
float menuRowH() { return g_curMenu.rowH > 0.0f ? g_curMenu.rowH : rowH(); }
// The open list starts at `y` (level with its row, or under its bar button). Moved up when last frame's height would run past the
// display's bottom (Input::displayH), as Dear ImGui keeps a menu inside its viewport; taller than the display, it starts at the top
// and scrolls. Sets itemStartY (the visible top), virtTop and itemY (row 0, scroll included).
void menuListBegin(int id, float y) {
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
        g_curMenu.x = x + w; g_curMenu.w = g_popupW > 0.0f ? g_popupW : 180.0f;
        g_curMenu.bx = x; g_curMenu.by = y; g_curMenu.bw = w; g_curMenu.bh = h;
        g_curMenu.fromPopup = true;
        g_curMenu.rowH = h;   // the popup's own row height (a text line and the item spacing)
        g_curMenu.textInset = (g_popupPadX >= 0.0f ? g_popupPadX : g_lay.spacingX * 0.5f + 4.0f);
        if (g_dispW > 0.0f && g_curMenu.x + g_curMenu.w > g_dispW) g_curMenu.x = std::max(0.0f, x - g_curMenu.w);   // no room on the right: the left
        menuListBegin(id, y);
        menuKeysBegin(id, true);
        return true;
    }
    if (g_lay.active && g_curMenu.active && label) {   // a sub-menu: a row of the open menu that opens a list to its right
        if (g_menuDepth >= 4) return false;
        const int id = hashLabel(label) ^ g_curMenu.menuId ^ 0x5bu;
        bool* open = openState(id, false);
        const float h = menuRowH(), s = g_theme.textScale;
        const float x = g_curMenu.x, w = g_curMenu.w, y = g_curMenu.itemY;
        g_curMenu.itemY += h;
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
        g_curMenu.x = x + w; g_curMenu.w = (g_curMenu.fromPopup && g_popupW > 0.0f) ? g_popupW : 180.0f;
        if (g_dispW > 0.0f && g_curMenu.x + g_curMenu.w > g_dispW) g_curMenu.x = std::max(0.0f, x - g_curMenu.w);
        g_curMenu.bx = x; g_curMenu.by = y; g_curMenu.bw = w; g_curMenu.bh = h;
        g_curMenu.subX = g_curMenu.subY = g_curMenu.subW = g_curMenu.subH = 0.0f;
        menuListBegin(id, y);
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

    const bool inside = hit(x, y, bw, h);
    g_lastId = id; if (inside) g_hot = id;
    if (g_active == id) { if (g_released) { if (inside) *open = !*open; g_active = 0; } }
    else if (inside && g_pressed) g_active = id;
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
        menuListBegin(id, y + h);
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
    if (ih || lit) rrect(x, y, w, h, g_framedWidgets ? 0.0f : g_theme.radiusSmall, pick(Col_HeaderHovered, g_theme.bgHover));
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
    const float h = menuRowH(), s = g_theme.textScale;
    const float x = g_curMenu.x, w = g_curMenu.w, y = g_curMenu.itemY;
    g_curMenu.itemY += h;
    const int iid = hashLabel(label) ^ g_curMenu.menuId;
    const bool shown = menuRowShown(y, h);
    const bool ih = shown && enabled && hitOverlay(x, y, w, h);
    if (ih) g_hot = iid;
    const int ord = g_curMenu.n++;
    if (enabled && ord < 64) g_curMenu.mask |= 1ull << ord;
    MenuKb* kb = g_curMenu.kb;
    if (ih && kb) kb->idx = ord;
    const bool ksel = kb && kb->idx == ord && enabled && !kb->childOpen;
    if (ksel && !shown && kb->keyMoved) menuRowReveal(y, h);
    bool clicked = false;
    if (ksel && g_curMenu.activate) {   // Enter on the highlighted item
        clicked = true; *openState(g_curMenu.menuId, false) = false;
        for (int i = 0; i < g_menuDepth; ++i) *openState(g_menuStack[i].menuId, false) = false;
    }
    if (g_active == iid) {
        if (g_released) { if (ih) { clicked = true; *openState(g_curMenu.menuId, false) = false; for (int i = 0; i < g_menuDepth; ++i) *openState(g_menuStack[i].menuId, false) = false; } g_active = 0; }
    } else if (ih && g_pressed) g_active = iid;
    if (clicked)   // a chain of sub-menus opened from a popup's row: the popup closes as well, as a context menu does
        for (int i = 0; i < g_menuDepth; ++i)
            if (g_menuStack[i].popupId != 0) { if (PopupSlot* ps = popupSlot(g_menuStack[i].popupId, false)) ps->open = false; }
    if (!shown) return clicked;
    const unsigned char* col = enabled ? g_theme.text : pick(Col_TextDisabled, g_theme.textDim);
    const bool wasOverlay = g_toOverlay;   // a sub-menu of a popup: the popup's rows after it must stay on top
    g_toOverlay = true;
    const float in = g_curMenu.textInset;
    menuRowBg(x, y, w, h, ih || ksel);
    if (selected) { const float tk = std::min(h - 6.0f, in + 2.0f); tickMark(x + (in - tk) * 0.5f + 1.0f, y + (h - tk) * 0.5f, tk, g_theme.accent); }   // a tick marks a checked item
    drawText(x + in, y + (h - textH()) * 0.5f, VisLabel(label).b, s, col);
    if (shortcut && shortcut[0]) {   // right-aligned, faint; skipped when it would run into the label
        const float sw = labelW(shortcut), sx = x + w - sw - in;
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
    const float x = g_curMenu.x, w = g_curMenu.w, y = g_curMenu.itemY, h = 8.0f;
    g_curMenu.itemY += h;
    if (!menuRowShown(y, h)) return;
    const bool wasOverlay = g_toOverlay;
    g_toOverlay = true;
    if (!g_curMenu.boxed) quad(x, y, w, h, pick(Col_PopupBg, g_theme.overlay));
    quad(x + 8.0f, y + h * 0.5f, w - 16.0f, 1.0f, pick(Col_Separator, g_theme.border));
    g_toOverlay = wasOverlay;
}

void EndMenu() {
    if (!g_curMenu.active) { if (g_popupSubOpen > 0) { --g_popupSubOpen; TreePop(); } return; }
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
    g_curMenu.active = false;
}

static bool selectableImpl(const char* label, bool selected, unsigned flags, Vec2 size, bool* p_sel) {
    if (!g_lay.active) return false;
    if (p_sel) selected = *p_sel;
    const int id = hashLabel(label);
    const bool disabled = (flags & Selectable_Disabled) != 0;
    const float s = g_theme.textScale;
    const float h = (size.y > 0.0f && std::isfinite(size.y)) ? size.y : rowH();
    float w = (size.x > 0.0f && std::isfinite(size.x)) ? size.x : fullW();
    float x, y; place(w, h, x, y);
    const float textX = x;                        // the label starts at the cell, even when the highlight spans the table
    if ((flags & Selectable_SpanAllColumns) && g_tbl.active) {
        x = g_tbl.x0 - (g_tbl.scrollX ? g_tbl.sx : 0.0f); w = g_tbl.fullW;
        g_lay.prevX = x; g_lay.prevW = w;
    }
    const bool inside = !disabled && hit(x, y, w, h);
    g_lastId = id; if (inside) g_hot = id;
    bool clicked = false;
    const bool dblOnly = (flags & Selectable_DoubleClickOnly) != 0;
    if (g_active == id) { if (g_released) { if (inside && !dblOnly) clicked = true; g_active = 0; } }
    else if (inside && g_pressed) { g_active = id; if (dblOnly && g_dblClick) clicked = true; }
    const unsigned char* tcol = disabled ? pick(Col_TextDisabled, g_theme.textDim) : g_theme.text;
    const float ty = y + (h - textH()) * 0.5f;
    if (flags & Selectable_Header) {   // Dear ImGui's rows: flat, square, the header colours; the label half the item spacing in
        unsigned char tmp[4];
        const unsigned char* bg = nullptr;
        if (inside && g_active == id) bg = pick(Col_HeaderActive, g_theme.accent);
        else if (inside)              bg = pick(Col_HeaderHovered, g_theme.bgHover);
        else if (selected)            bg = pick(Col_Header, withAlpha(g_theme.accent, 0.28f, tmp));
        if (bg) quad(x, y, w, h, bg);
        drawText(textX + g_lay.spacingX * 0.5f, ty, label, s, tcol);
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
    }
    return clicked;
}
bool Selectable(const char* label, bool selected) { return selectableImpl(label, selected, 0, Vec2{}, nullptr); }
bool Selectable(const char* label) { return selectableImpl(label, false, 0, Vec2{}, nullptr); }
bool Selectable(const char* label, bool selected, unsigned flags, Vec2 size) { return selectableImpl(label, selected, flags, size, nullptr); }
bool Selectable(const char* label, bool* p_selected, unsigned flags, Vec2 size) { return selectableImpl(label, false, flags, size, p_selected); }

void OpenPopup(const char* id) {
    PopupSlot* p = popupSlot(hashLabel(id), true);
    if (p) { p->open = true; p->x = g_in.mouseX; p->y = g_in.mouseY; p->w = 0.0f; p->modal = false; p->fixedW = false; g_openPopupThisFrame = p->id; }
}

void CloseCurrentPopup() {
    if (g_popupTop > 0) { PopupSlot* p = popupSlot(g_popupStack[g_popupTop - 1].id, false); if (p) p->open = false; }
}

// Shared popup body setup: redirect layout into a floating overlay box at (px,py),
// drawing the background using last frame's measured height. Returns true if open.
bool ui_core::beginPopupBody(int id) {
    // Depth guard first: past the popup-stack capacity the parent layout cannot
    // be saved, so the popup cannot begin -- otherwise EndPopup would restore a
    // state that was never saved and stomp another popup's measured height.
    if (g_popupTop >= 4) return false;
    PopupSlot* p = popupSlot(id, false);
    if (!p || !p->open) return false;
    // StyleVar_PopupWidth / StyleVar_PopupPadding (when pushed) give the size; with a pushed padding the height is the content's
    // exactly (no item spacing after the last item), as in Dear ImGui.
    const float padX = g_popupPadX >= 0.0f ? g_popupPadX : 6.0f, padY = g_popupPadY >= 0.0f ? g_popupPadY : 6.0f;
    float* hSlot = floatState(id ^ 0x9090, rowH() * 3.0f);
    float* wSlot = floatState(id ^ 0x9091, 0.0f);   // the widest line of last frame's content (plus padding): the popup grows to fit it
    float w = p->w > 0.0f ? p->w : (g_popupW > 0.0f ? g_popupW : 160.0f);
    if (!p->fixedW && !(g_popupW > 0.0f) && *wSlot > w) w = *wSlot;   // a pushed StyleVar_PopupWidth is the width (the Dear ImGui-looking menus); a popup with a width asked for (SetNextWindowSize before a modal) keeps it: items that fill the width would make it grow every frame
    if (g_dispW > 32.0f && w > g_dispW - 16.0f) w = g_dispW - 16.0f;
    if (p->modal && g_dispW > 0.0f) p->x = (g_dispW - w) * 0.5f;   // a modal stays centred when it grew
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
    c.padX = padX; c.padY = padY; c.exact = g_popupPadY >= 0.0f;
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
    return true;
}

void EndPopup() {
    if (g_popupTop <= 0) return;
    PopupCtx& c = g_popupStack[--g_popupTop];
    *c.hSlot = g_lay.cy - (c.y + c.padY) - (c.exact ? g_lay.spacingY : 0.0f);   // measured content height for next frame
    if (*c.hSlot < 0.0f) *c.hSlot = 0.0f;
    *c.wSlot = g_lay.maxRight > c.x ? (g_lay.maxRight - c.x) + c.padX : 0.0f;   // and the width it needs, plus padding
    g_toOverlay = g_popupTop > 0;   // back in the popup this one was opened from: still on top
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

bool BeginPopup(const char* id) { return beginPopupBody(hashLabel(id)); }
// By id (what GetID(name) gives, which is also how a named popup is keyed): the popup opened with the same name or id.
void OpenPopup(unsigned id, int) {
    PopupSlot* p = popupSlot((int)id, true);
    if (p) { p->open = true; p->x = g_in.mouseX; p->y = g_in.mouseY; p->w = 0.0f; p->modal = false; p->fixedW = false; g_openPopupThisFrame = p->id; }
}
bool BeginPopupEx(unsigned id, int flags) { (void)flags; return beginPopupBody((int)id); }

bool IsPopupOpen(const char* name) {
    if (!name) return false;
    const PopupSlot* p = popupSlot(hashLabel(name), false);
    return p && p->open;
}

static bool g_modalNoTitle = false;   // BeginPopupModal(.., Window_NoTitleBar): no title line
bool BeginPopupModal(const char* name, bool* open, int flags) { g_modalNoTitle = (flags & Window_NoTitleBar) != 0; const bool r = BeginPopupModal(name, open); g_modalNoTitle = false; return r; }
bool BeginPopupModal(const char* name, bool* open) {
    if (!g_lay.active || !name) return false;
    const int id = hashLabel(name);
    PopupSlot* p = popupSlot(id, false);
    if (!p || !p->open) { if (open) *open = false; return false; }
    p->modal = true;
    if (g_nextWinSize.set) {   // SetNextWindowSize before a modal sets its width (the height follows the content); the position is always the centre
        if (g_nextWinSize.x > 0.0f && std::isfinite(g_nextWinSize.x)) { p->w = g_nextWinSize.x < 120.0f ? 120.0f : (g_dispW > 0.0f && g_nextWinSize.x > g_dispW - 16.0f ? g_dispW - 16.0f : g_nextWinSize.x); p->fixedW = true; }
        g_nextWinSize.set = false;
    }
    g_nextWinPos.set = false;   // consumed: it must not move the next window
    if (p->w <= 0.0f) p->w = 320.0f;
    if (g_in.escape) { p->open = false; if (open) *open = false; return false; }
    const float pad = 6.0f;
    float* hSlot = floatState(id ^ 0x9090, rowH() * 3.0f);
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
        if (p) { p->open = true; p->x = g_in.mouseX; p->y = g_in.mouseY; p->w = 0.0f; g_openPopupThisFrame = pid; }
    }
    return beginPopupBody(pid);
}

} // namespace OkayUI

// ---- What belongs to one context (OkayUI_ContextState.hpp: each context keeps its own copy of these) ----
namespace OkayUI {
OKAYUI_CONTEXT_STATE(
    OKAYUI_CTX_VAR(g_comboCloseId),
    OKAYUI_CTX_VAR(g_modalNoTitle))
} // namespace OkayUI
