#include "OkayUI_ContextState.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "OkayUIInternal.hpp"
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUICompat.hpp"
#include "okay/UI/OkayUIInternals.hpp"
#include "okay/UI/OkayUIDock.hpp"

namespace OkayUI {
using namespace ui_core;
void ChannelsMergeAll();

// ---- Auto-layout window + ImGui-style overloads --------------------------------

bool ui_core::g_pinNext = false;   // PinNextWindow: the next Begin sits where it is told
// The window being built (for GetWindowPos / IsWindowHovered ...) and the one that was clicked last (IsWindowFocused).
CurWin ui_core::g_curWin;
// What End() needs to finish the window Begin() started: scrolling, the scroll bar, auto-resize, a hidden measuring frame.
WinEnd ui_core::g_we;
int  ui_core::g_wflagsNext = 0;      // WindowFlags for the next core Begin


int ui_core::g_focusWin = 0;
// SetNextWindowPos / SetNextWindowSize, consumed by the next Begin(title, p_open, flags).
NextWin ui_core::g_nextWinPos, ui_core::g_nextWinSize;
void PinNextWindow() { g_pinNext = true; }
bool ui_core::g_bareNext = false;   // BareNextWindow: the next Begin has no title bar (a panel that fills a host's rectangle)
void BareNextWindow() { g_bareNext = true; }
bool  ui_core::g_noTitleNext = false;  // Window_NoTitleBar: the next Begin has no title bar (docked windows)
bool  ui_core::g_resizeNext = false;   // ResizableNextWindow: the next Begin gets a resize grip
static float g_resizeMinW = 100.0f, g_resizeMinH = 60.0f;
void ResizableNextWindow(float minW, float minH) { g_resizeNext = true; g_resizeMinW = minW; g_resizeMinH = minH; }

// SetNextWindowBgAlpha: scale the alpha of the body geometry a window just drew (vertices from `from` on); the title bar and the content are drawn after and are not touched.
static void scaleBodyAlpha(int from, float a) {
    if (!(a >= 0.0f) || a >= 1.0f || g_toOverlay) return;
    for (int i = from; i < g_nv; ++i) g_verts[i].color.a = (Uint8)(g_verts[i].color.a * a + 0.5f);
}

bool Begin(const char* title, float x, float y, float w, float h, bool* p_open) {
    g_posOverride = false;             // a SetCursorScreenPos left over from the previous window
    g_lay.seed = 0;                    // hash the window id from a FIXED seed so it (and
    const int id = hashLabel(title);   // every widget id under it) is stable across frames
    WinSlot* slot = nullptr;
    for (WinSlot& ws : g_wins) if (ws.used && ws.id == id) { slot = &ws; break; }
    if (!slot) for (WinSlot& ws : g_wins) if (!ws.used) { ws.used = true; ws.id = id; ws.x = x; ws.y = y; slot = &ws; break; }
    const int wf = g_wflagsNext;
    g_wflagsNext = 0;
    if (g_we.open) End();               // a Begin without its End: close it so the next window starts clean
    const bool bare = g_bareNext;       // no title bar, never collapsed, never dragged: it is where the host put it
    g_bareNext = false;
    const bool pinned = g_pinNext || bare;
    g_pinNext = false;
    const float bgAlpha = g_bgAlphaCore; g_bgAlphaCore = -1.0f;   // SetNextWindowBgAlpha for this window's body
    const int bodyV0 = g_nv;
    // Opt-in resize grip (ResizableNextWindow): the remembered size replaces the w and h passed in.
    const bool autoSize = (wf & Window_AlwaysAutoResize) != 0 && slot != nullptr;
    const bool resizable = g_resizeNext && slot != nullptr && !autoSize;
    const float minW = g_resizeMinW, minH = g_resizeMinH;
    g_resizeNext = false;
    if (resizable) {
        if (!slot->sized) { slot->w = w; slot->h = h; slot->sized = true; }
        w = slot->w; h = slot->h;
    }
    bool hidden = false;
    if (autoSize) {   // as big as the content was last frame; the first frame only measures and is not shown
        if (slot->measured) { w = slot->autoW; h = slot->autoH; slot->w = w; slot->h = h; slot->sized = true; }
        else hidden = true;
    }
    if (pinned && slot) { slot->x = x; slot->y = y; }
    float wx = slot ? slot->x : x, wy = slot ? slot->y : y;
    if (pinned) { wx = x; wy = y; }
    const bool noTitle = g_noTitleNext;
    g_noTitleNext = false;
    const float titleH = (bare || noTitle) ? 0.0f : rowH();
    static bool s_bareOpen = false;     // a bare window's "collapsed" flag: always false
    bool* collapsed = bare ? &s_bareOpen : openState(id ^ 0x5c01, false);   // per-window collapsed flag
    if (bare) { s_bareOpen = false; p_open = nullptr; }
    const bool noCollapse = (wf & Window_NoCollapse) != 0, noBg = (wf & Window_NoBackground) != 0, noFront = (wf & Window_NoBringToFront) != 0 || bare;   // a bare (host) panel never rises above floating windows
    if (noTitle || noCollapse) *collapsed = false;
    // Stacking: a window that is new (or has not been drawn for a frame) comes to the front and takes focus unless told not to;
    // a click on the topmost window under the pointer raises it. NoBringToFront windows stay at the bottom.
    {
        const float bh = *collapsed ? titleH : h;
        const bool fresh = slot && (slot->lastFrame == 0 || (slot->lastFrame == 0 || slot->lastFrame + 1 < g_frameCount));
        if (slot) {
            if (noFront) slot->z = 0;
            else if (slot->z == 0) slot->z = ++g_zCounter;
            if (fresh && !noFront && !(wf & Window_NoFocusOnAppearing)) { slot->z = ++g_zCounter; g_focusWin = id; }
        }
        if (g_pressed && !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, wx, wy, w, bh)
            && !(slot && coveredByHigher(g_in.mouseX, g_in.mouseY, slot->z, id))) {
            g_focusWin = id;                                    // the topmost window under the pointer
            if (slot && !noFront) slot->z = ++g_zCounter;
        }
        g_hitWinOn = slot != nullptr; g_hitWinId = id; g_hitWinZ = slot ? slot->z : 0;
    }

    // Title-bar sub-rects: a collapse caret on the left, an optional close [x] on the
    // right, and the draggable strip in between.
    const float btn = titleH;
    const float caretX = wx, caretW = btn;
    const float closeX = wx + w - btn, closeW = p_open ? btn : 0.0f;

    // Collapse toggle: click the caret to fold/unfold the window.
    const bool overCaret = !noCollapse && !bare && hit(caretX, wy, caretW, titleH);
    if (overCaret) g_hot = id ^ 0x5c01;
    if (g_active == (id ^ 0x5c01)) { if (g_released) { if (overCaret) *collapsed = !*collapsed; g_active = 0; } }
    else if (overCaret && g_pressed) { g_active = id ^ 0x5c01; g_focusClaimed = true; }

    // Close button: click the [x] to clear *p_open.
    bool overClose = false;
    if (p_open) {
        overClose = hit(closeX, wy, closeW, titleH);
        if (overClose) g_hot = id ^ 0xc105;
        if (g_active == (id ^ 0xc105)) { if (g_released) { if (overClose) *p_open = false; g_active = 0; } }
        else if (overClose && g_pressed) { g_active = id ^ 0xc105; g_focusClaimed = true; }
    }

    // Drag the window by the title bar (the strip between caret and close button).
    const float dragX = noCollapse ? wx : wx + caretW, dragW = w - (noCollapse ? 0.0f : caretW) - closeW;
    const bool overDrag = !bare && hit(dragX, wy, dragW, titleH);
    if (overDrag) g_hot = id;
    if (g_active == id) {
        if (!g_in.mouseDown) g_active = 0;
        else if (slot && !pinned) { slot->x += g_mouseDX; slot->y += g_mouseDY; wx = slot->x; wy = slot->y; }
    } else if (overDrag && g_pressed) {
        g_active = id; g_focusClaimed = true;
    }

    // Resize grip: dragging the bottom-right corner grows or shrinks the window (not while folded).
    if (resizable && !*collapsed) {
        const int gid = id ^ 0x6f1d;
        const float grip = 14.0f;
        const bool overGrip = hit(wx + w - grip, wy + h - grip, grip, grip);
        if (overGrip) g_hot = gid;
        if (g_active == gid) {
            if (!g_in.mouseDown) g_active = 0;
            else {
                slot->w += g_mouseDX; slot->h += g_mouseDY;
                if (slot->w < minW) slot->w = minW;
                if (slot->h < minH) slot->h = minH;
                w = slot->w; h = slot->h;
            }
        } else if (overGrip && g_pressed) { g_active = gid; g_focusClaimed = true; }
    }

    // Frame: panel body (only the title bar when collapsed) + accent underline + title.
    const float bodyH = *collapsed ? titleH : h;
    const unsigned char* titleCol = pick(*collapsed ? Col_TitleBgCollapsed : (g_focusWin == id ? Col_TitleBgActive : Col_TitleBg), g_theme.titleBg);
    g_winBaseScale = g_theme.textScale;
    if (!g_in.blocked && !hidden && pointIn(g_in.mouseX, g_in.mouseY, wx, wy, w, bodyH)) g_ptrOverWindow = true;
    g_curWin = CurWin{true, id, wx, wy, w, bodyH, slot != nullptr && (slot->lastFrame == 0 || slot->lastFrame + 1 < g_frameCount)};   // appearing: never drawn, or not on the frame before
    g_curWin.z = slot ? slot->z : 0;
    if (title) std::snprintf(g_curWin.title, sizeof g_curWin.title, "%s", title);
    if (slot) { slot->lastFrame = g_frameCount; if (title) std::snprintf(slot->title, sizeof slot->title, "%s", title); }
    if (!hidden) registerWindowRect(id, g_curWin.z, wx, wy, w, bodyH);
    wrecBegin(id, g_curWin.z);
    g_we = WinEnd(); g_we.open = true; g_we.hidden = hidden; g_we.id = id; g_we.slot = slot ? (int)(slot - g_wins) : -1;
    g_we.autoSize = autoSize; g_we.x = wx; g_we.y = wy; g_we.w = w; g_we.h = h; g_we.titleH = titleH;
    if (bare) {
        // A bare window draws no frame at all: the host's own window behind it is the background.
    } else {
        // The design's window: a shadow as deep as the design asks, a rounded body inside a hairline, and a clean header in the
        // sunken colour: the title (dimmer when the window does not have focus), a close cross on a round chip that turns the
        // danger colour under the pointer, and the fold chevron only while the pointer is over its corner (or the window is folded).
        const float wr = g_theme.windowRounding;
        if (!noBg) {
            if (g_theme.windowShadow) elevation(wx, wy, w, bodyH, wr, g_theme.shadowDepth);
            rrect(wx, wy, w, bodyH, wr, g_theme.border);
            rrect(wx + 1.0f, wy + 1.0f, w - 2.0f, bodyH - 2.0f, wr > 1.0f ? wr - 1.0f : 0.0f, pick(Col_WindowBg, g_theme.panel));
        }
        scaleBodyAlpha(bodyV0, bgAlpha);
        if (!noTitle) {
            rrect(wx + 1.0f, wy + 1.0f, w - 2.0f, titleH - 1.0f, wr > 1.0f ? wr - 1.0f : 0.0f, titleCol);
            if (!*collapsed && titleH > wr) quad(wx + 1.0f, wy + wr, w - 2.0f, titleH - wr, titleCol);
            if (!*collapsed) quad(wx + 1.0f, wy + titleH - 1.0f, w - 2.0f, 1.0f, g_theme.border);
            const float s0 = g_theme.textScale;
            const bool focusedWin = g_focusWin == id;
            // The title starts at the header's padding (no gap for a fold arrow). The fold chevron shows on the right, before the
            // close cross, while the pointer is over the header or the window is folded; the fold still answers a click on the
            // header's left corner as before (behaviour unchanged).
            const bool overHeader = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, wx, wy, w, titleH);
            if (!noCollapse && (overHeader || *collapsed))
                chevron(wx + w - (p_open ? btn : 0.0f) - btn * 0.5f, wy + titleH * 0.5f, 8.0f, !*collapsed, overHeader ? g_theme.text : pick(Col_TextDisabled, g_theme.textDim));
            if (title && *title)
                drawText(wx + 12.0f, wy + (titleH - textH()) * 0.5f, VisLabel(title).b, s0, focusedWin ? g_theme.text : g_theme.textDim);
            if (p_open) {
                unsigned char tmp[4];
                const float cr = std::min(btn, titleH) * 0.5f - 5.0f, ccx = closeX + btn * 0.5f, ccy = wy + titleH * 0.5f;
                const float over = anim(id ^ 0xc105, 0, overClose);
                if (over > 0.0f && cr > 2.0f) fillCircle(ccx, ccy, cr, withAlpha(g_theme.danger, 0.85f * over, tmp), 24);
                unsigned char xc[4];
                xMark(ccx, ccy, 8.0f, mixColor(pick(Col_TextDisabled, g_theme.textDim), g_theme.onAccent, over, xc));
            }
        }
    }

    if (*collapsed) { g_lay.active = false; return false; }   // folded: skip content

    if (resizable) {   // grip marks: two diagonal strokes in the corner
        const bool gripAct = g_active == (id ^ 0x6f1d), gripHot = g_hot == (id ^ 0x6f1d);
        const unsigned char* gc = gripAct ? pick(Col_ResizeGripActive, g_theme.accent) : (gripHot ? pick(Col_ResizeGripHovered, g_theme.accent) : pick(Col_ResizeGrip, g_theme.textFaint));
        line(wx + w - 4.0f, wy + h - 12.0f, wx + w - 12.0f, wy + h - 4.0f, 1.5f, gc);
        line(wx + w - 4.0f, wy + h - 7.0f, wx + w - 7.0f, wy + h - 4.0f, 1.5f, gc);
    }

    // Establish the content layout region.
    // (A bare window has no padding: the host's own window already has its own.)
    const float pad = bare ? 0.0f : g_theme.windowPadding;
    // A window whose content did not fit last frame scrolls (wheel, and a bar unless NoScrollbar); the content is clipped to it.
    float scrollY = 0.0f;
    bool scrollable = false;
    const bool flagStyle = (wf & kWinFlagStyle) != 0;   // only windows begun with Begin(title, p_open, flags) scroll; the old Begin(title, x, y, w, h) never did
    if (slot && flagStyle && !autoSize && slot->maxScroll > 0.0f) {
        if (slot->scroll > slot->maxScroll) slot->scroll = slot->maxScroll;
        if (slot->scroll < 0.0f) slot->scroll = 0.0f;
        scrollY = slot->scroll; scrollable = true;
    } else if (slot) slot->scroll = 0.0f;
    const bool showBar = scrollable && !(wf & Window_NoScrollbar);
    if (g_look || scrollable) { PushClipRect(wx + 1.0f, wy + titleH, w - 2.0f, h - titleH - 1.0f); g_lay.winClip = true; }
    g_lay.active = true;
    g_lay.seed = id;
    g_lay.ox = wx + pad;
    g_lay.oy = wy + titleH + pad - scrollY;
    g_lay.winX = wx; g_lay.winY = wy; g_lay.winW = w; g_lay.winH = h; g_lay.baseX = wx; g_lay.baseY = wy - scrollY;
    g_lay.cx = g_lay.ox;
    g_lay.cy = g_lay.oy;
    g_lay.maxRight = g_lay.ox;
    g_we.laid = true; g_we.scrolls = flagStyle; g_we.showBar = showBar; g_we.pad = pad; g_we.ox = g_lay.ox; g_we.oy = g_lay.oy;
    g_lay.contentW = w - 2.0f * pad - (showBar ? kScrollbarW : 0.0f);
    g_lay.bottom = wy + h - pad;
    g_lay.spacingX = (g_look ? 9.0f : 8.0f) * g_theme.spacing; g_lay.spacingY = (g_look ? 8.0f : 6.0f) * g_theme.spacing;   // density
    g_lay.indent = 0.0f;
    g_lay.pendingSameLine = false;
    g_lay.prevX = g_lay.ox; g_lay.prevY = g_lay.oy; g_lay.prevW = 0.0f; g_lay.prevH = 0.0f;
    return true;
}

// A scroll bar's track (classic look, or when ScrollbarBg is set) and thumb in the Col_Scrollbar* colours.
// With a pushed StyleVar_ScrollbarSize the bar is that wide and the grab sits 2 pixels inside it on every side, fully rounded.
void ui_core::scrollbarThumb(float sbx, float top, float trackH, float thumbY, float thumbH, bool active, bool over, bool track) {
    const float bw = g_sbSize > 0.0f ? g_sbSize : kScrollbarW;
    if (track && colorIsSet(Col_ScrollbarBg)) quad(sbx, top, bw, trackH, pick(Col_ScrollbarBg, g_theme.track));
    // A thin overlay thumb: a faint ink pill that is slim at rest and grows (and brightens) under the pointer; the accent while
    // dragged. It sits at the right of the bar's room, so the layout never moves when it grows.
    unsigned char tmp[4];
    const unsigned char* tc2 = active ? pick(Col_ScrollbarGrabActive, g_theme.accent)
                                      : (over ? pick(Col_ScrollbarGrabHovered, withAlpha(g_theme.ink, 0.34f, tmp)) : pick(Col_ScrollbarGrab, withAlpha(g_theme.ink, 0.18f, tmp)));
    if (g_sbSize > 0.0f) { const float gw = std::max(1.0f, bw - 4.0f); rrect(sbx + 2.0f, thumbY + 2.0f, gw, thumbH - 4.0f, gw * 0.5f, tc2); return; }
    const float gw = (over || active) ? kScrollbarW - 3.0f : 4.0f;
    rrect(sbx + kScrollbarW - 2.0f - gw, thumbY + 2.0f, gw, thumbH - 4.0f, gw * 0.5f, tc2);
}

bool ui_core::g_dockAware = false, ui_core::g_dockRouted = false;   // SetDockAwareBegin: Begin runs a window of the dock layout as DockBegin; the End that closes it as DockEnd
void End() {
    if (g_dockRouted) { g_dockRouted = false; DockEnd(); return; }   // closes a Begin that was run as DockBegin (SetDockAwareBegin); DockEnd calls End again, now plain
    ChannelsMergeAll();   // a channel split left open ends with its window
    if (g_winItemWDepth > 0) { if (g_itemWTop >= g_winItemWDepth) g_itemWTop = g_winItemWDepth - 1; g_winItemWDepth = 0; }
    if (g_curWin.valid && g_winBaseScale > 0.0f) g_theme.textScale = g_winBaseScale;   // SetWindowFontScale ends with its window
    // Forget it once the window is closed: the next BeginFrame restores a window LEFT OPEN to it, and a window that was opened inside
    // the host's PushTextScale would otherwise hand that pushed scale to the next frame as the base (the text grew a step every frame).
    if (g_curWin.valid) g_winBaseScale = 0.0f;
    g_curWin.valid = false;
    g_posOverride = false;
    if (g_lay.winClip) { PopClipRect(); g_lay.winClip = false; }
    if (g_we.open) {
        WinSlot* slot = g_we.slot >= 0 ? &g_wins[g_we.slot] : nullptr;
        if (slot && g_we.laid) {
            const float contentH = std::max(0.0f, (g_lay.cy - g_lay.spacingY) - g_we.oy);
            const float view = g_we.h - g_we.titleH - 2.0f * g_we.pad;
            if (g_we.autoSize) {
                slot->autoW = std::max(g_lay.maxRight - g_we.ox, 1.0f) + 2.0f * g_we.pad;
                slot->autoH = g_we.titleH + contentH + 2.0f * g_we.pad;
                slot->measured = std::isfinite(slot->autoW) && std::isfinite(slot->autoH);
                slot->maxScroll = 0.0f;
            } else if (g_we.scrolls) {
                slot->maxScroll = contentH > view + g_we.pad ? contentH - view : 0.0f;   // a window scrolls once content passes its bottom edge (the padding may be eaten)
                if (slot->maxScroll > 0.0f && g_we.laid) {
                    const bool over = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, g_we.x, g_we.y, g_we.w, g_we.h)
                                      && !coveredByHigher(g_in.mouseX, g_in.mouseY, slot->z, g_we.id);
                    if (over && g_in.wheel != 0.0f && !g_wheelUsed && g_disCount == 0) slot->scroll -= g_in.wheel * rowH();
                    if (g_we.showBar) {   // the scroll bar along the right edge, below the title bar
                        const float sbx = g_we.x + g_we.w - kScrollbarW - 1.0f, sby = g_we.y + g_we.titleH, trackH = g_we.h - g_we.titleH - 1.0f;
                        float frac = view / (view + slot->maxScroll); if (frac > 1.0f) frac = 1.0f;
                        float thumbH = frac * trackH; if (thumbH < 16.0f) thumbH = 16.0f; if (thumbH > trackH) thumbH = trackH;
                        const float t = slot->scroll / slot->maxScroll;
                        const float thumbY = sby + std::max(0.0f, std::min(1.0f, t)) * (trackH - thumbH);
                        const int sbId = g_we.id ^ 0x5c0b;
                        const bool overThumb = hit(sbx, thumbY, kScrollbarW, thumbH);
                        if (overThumb) g_hot = sbId;
                        if (g_active == sbId) {
                            if (!g_in.mouseDown) g_active = 0;
                            else if (trackH - thumbH > 0.0f) slot->scroll += g_mouseDY * (slot->maxScroll / (trackH - thumbH));
                        } else if (overThumb && g_pressed) { g_active = sbId; g_focusClaimed = true; }
                        scrollbarThumb(sbx, sby, trackH, thumbY, thumbH, g_active == sbId, overThumb);
                    }
                    if (slot->scroll < 0.0f) slot->scroll = 0.0f;
                    if (slot->scroll > slot->maxScroll) slot->scroll = slot->maxScroll;
                }
            }
        }
        wrecEnd();
        if (g_we.hidden && g_nwrec > 0 && g_wrec[g_nwrec - 1].id == g_we.id) {   // the measuring frame leaves no trace
            const WinRec& r = g_wrec[--g_nwrec];
            g_nv = r.v0; g_ni = r.i0; g_nspans = r.ns0; g_curClipOn = r.clipOn; g_curClipRect = r.clip; g_curTex = r.tex;
        }
        g_we = WinEnd();
    }
    g_hitWinOn = false;
    g_lay.active = false;
}

void SetWindowFontScale(float scale) {
    if (!g_curWin.valid || g_winBaseScale <= 0.0f || !std::isfinite(scale)) return;
    g_theme.textScale = g_winBaseScale * (scale < 0.25f ? 0.25f : (scale > 8.0f ? 8.0f : scale));
}
void SetNextWindowPos(Vec2 pos, Cond cond) { g_nextWinPos = NextWin{true, pos.x, pos.y, (int)cond}; }
void SetNextWindowPos(Vec2 pos, Cond cond, Vec2 pivot) {
    const float px = std::isfinite(pivot.x) ? pivot.x : 0.0f, py = std::isfinite(pivot.y) ? pivot.y : 0.0f;
    g_nextWinPos = NextWin{true, pos.x, pos.y, (int)cond, px, py};
}
void SetNextWindowSize(Vec2 size, Cond cond) { g_nextWinSize = NextWin{true, size.x, size.y, (int)cond}; }

// A window's id is hashed from a fixed seed (as Begin does), whatever widget ids were being built before.
static int windowId(const char* title) { const auto saved = g_lay.seed; g_lay.seed = 0; const int id = hashLabel(title); g_lay.seed = saved; return id; }

void SetDockAwareBegin(bool on) { g_dockAware = on; }
bool DockAwareBegin() { return g_dockAware; }
bool Begin(const char* title, bool* p_open, int flags) {
    if (g_dockAware && title && *title && !g_dockRouted && !DockInsideBegin() && (GetDockLayout().Contains(title) || DockClosedPending(title))) { g_dockRouted = true; return DockBegin(title, p_open, flags); }
    if (!title) { g_nextWinPos.set = g_nextWinSize.set = false; g_nextWinFocus = false; g_nextCollapse = NextCollapse(); g_nextBgAlpha = -1.0f; return false; }
    const int id = windowId(title);
    WinSlot* slot = nullptr; bool created = false;
    for (WinSlot& ws : g_wins) if (ws.used && ws.id == id) { slot = &ws; break; }
    if (!slot) for (WinSlot& ws : g_wins) if (!ws.used) { created = true; ws.used = true; ws.id = id; ws.x = 60.0f; ws.y = 60.0f; ws.w = 320.0f; ws.h = 240.0f; ws.sized = true; slot = &ws; break; }
    NextWin np = g_nextWinPos, ns = g_nextWinSize;
    g_nextWinPos.set = g_nextWinSize.set = false;
    if (np.set && (np.pivX != 0.0f || np.pivY != 0.0f)) {   // SetNextWindowPos with a pivot: move the point of the window that sits at the position
        const float pw = ns.set ? ns.x : (slot && slot->sized ? slot->w : 320.0f), ph = ns.set ? ns.y : (slot && slot->sized ? slot->h : 240.0f);
        np.x -= np.pivX * pw; np.y -= np.pivY * ph;
    }
    bool pin = false;
    if (slot) {
        const bool appearing = (slot->lastFrame == 0 || slot->lastFrame + 1 < g_frameCount);      // not drawn on the frame before
        auto apply = [&](const NextWin& n, bool& init) {
            if (!n.set) return false;
            const bool take = n.cond == Cond_Always || ((n.cond == Cond_Once || n.cond == Cond_FirstUseEver) && !init) || (n.cond == Cond_Appearing && appearing);
            if (take) init = true;
            return take;
        };
        if (apply(np, slot->posInit)) { slot->x = np.x; slot->y = np.y; pin = (np.cond == Cond_Always); }
        if (apply(ns, slot->sizeInit)) { slot->w = ns.x; slot->h = ns.y; slot->sized = true; }
        if (created) slot->posInit = slot->sizeInit = true;            // a window that exists has had its first use (FirstUseEver / Once are spent)
        if (flags & Window_NoMove) pin = true;
        if (!slot->sized) { slot->w = 320.0f; slot->h = 240.0f; slot->sized = true; }
    }
    const float x = slot ? slot->x : (np.set ? np.x : 60.0f), y = slot ? slot->y : (np.set ? np.y : 60.0f);
    const float w = slot ? slot->w : (ns.set ? ns.x : 320.0f), h = slot ? slot->h : (ns.set ? ns.y : 240.0f);
    if (g_nextCollapse.set) {   // SetNextWindowCollapsed, with the same conditions as position and size
        const NextCollapse nc = g_nextCollapse; g_nextCollapse = NextCollapse();
        if (slot) {
            const bool appearing = (slot->lastFrame == 0 || slot->lastFrame + 1 < g_frameCount);
            const bool take = nc.cond == Cond_Always || ((nc.cond == Cond_Once || nc.cond == Cond_FirstUseEver) && !slot->collInit) || (nc.cond == Cond_Appearing && appearing);
            if (take) { *openState(id ^ 0x5c01, false) = nc.value; slot->collInit = true; }
        }
    }
    g_bgAlphaCore = g_nextBgAlpha; g_nextBgAlpha = -1.0f;
    if (pin) PinNextWindow();
    if (g_nextWinFocus) { g_nextWinFocus = false; SetWindowFocus(title); }
    if (!(flags & (Window_NoResize | Window_AlwaysAutoResize))) ResizableNextWindow(96.0f, 64.0f);
    g_noTitleNext = (flags & Window_NoTitleBar) != 0;
    g_wflagsNext = flags | kWinFlagStyle;
    return Begin(title, x, y, w, h, (flags & Window_NoTitleBar) ? nullptr : p_open);
}

// With UseImGuiCoordinates, inside a child or a popup these are the child's or popup's rectangle, as in Dear ImGui.
static bool innerWin() { return g_imguiCoords && g_lay.active && g_lay.winW > 0.0f; }
Vec2 GetWindowPos() { if (innerWin()) return Vec2{g_lay.winX, g_lay.winY}; return g_curWin.valid ? Vec2{g_curWin.x, g_curWin.y} : Vec2{0, 0}; }
Vec2 GetWindowSize() { if (innerWin()) return Vec2{g_lay.winW, g_lay.winH}; return g_curWin.valid ? Vec2{g_curWin.w, g_curWin.h} : Vec2{0, 0}; }
float GetWindowWidth() { return GetWindowSize().x; }
float GetWindowHeight() { return GetWindowSize().y; }
bool IsWindowHovered() {
    return g_curWin.valid && !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, g_curWin.x, g_curWin.y, g_curWin.w, g_curWin.h)
           && !coveredByHigher(g_in.mouseX, g_in.mouseY, g_curWin.z, g_curWin.id);
}
bool IsWindowFocused() { return g_curWin.valid && g_focusWin == g_curWin.id; }
bool IsWindowAppearing() { return g_curWin.valid && g_curWin.appearing; }
bool FindWindowSlot(const char* title, Vec2& pos, Vec2& size, bool& focused) {
    if (!title || !*title) return false;
    const int id = windowId(title);
    for (const WinSlot& ws : g_wins) if (ws.used && ws.id == id) {
        pos = Vec2{ws.x, ws.y}; size = Vec2{ws.w, ws.h}; focused = (g_focusWin == id);
        return true;
    }
    return false;
}
void SetNextWindowFocus() { g_nextWinFocus = true; }
// ---- small calls and internals added in step 6.9 (declared in OkayUICompat.hpp / OkayUIInternals.hpp) ----
void SetWindowSize(Vec2 size, Cond cond) { if (g_curWin.valid) SetWindowSize(g_curWin.title, size, cond); }
bool SetWindowSize(const char* title, Vec2 size, Cond cond) {
    if (!title || !*title || !std::isfinite(size.x) || !std::isfinite(size.y)) return false;
    const int id = windowId(title);
    for (WinSlot& ws : g_wins) if (ws.used && ws.id == id) {
        const bool appearing = (ws.lastFrame == 0 || ws.lastFrame + 1 < g_frameCount);
        const bool take = cond == Cond_Always || (cond == Cond_Appearing && appearing) || ((cond == Cond_Once || cond == Cond_FirstUseEver) && !ws.sizeInit);
        if (!take) return true;   // found, the condition said no
        ws.w = size.x < 1.0f ? 1.0f : (size.x > 1.0e6f ? 1.0e6f : size.x);
        ws.h = size.y < 1.0f ? 1.0f : (size.y > 1.0e6f ? 1.0e6f : size.y);
        ws.sized = true; ws.sizeInit = true;
        return true;
    }
    return false;
}
Vec2 GetWindowContentRegionMin() {
    if (!g_curWin.valid || !g_lay.active) return Vec2{0, 0};
    return Vec2{g_lay.ox - g_curWin.x, g_lay.oy - g_curWin.y};
}
CurrentWindow GetCurrentWindow() {
    CurrentWindow w;
    if (!g_curWin.valid) return w;
    w.valid = true; w.id = (unsigned)g_curWin.id; w.pos = Vec2{g_curWin.x, g_curWin.y}; w.size = Vec2{g_curWin.w, g_curWin.h};
    w.focused = g_focusWin == g_curWin.id; w.hovered = IsWindowHovered(); w.appearing = g_curWin.appearing; w.z = g_curWin.z;
    w.ID = w.id; w.Pos = w.pos; w.Size = w.size;
    std::snprintf(w.title, sizeof w.title, "%s", g_curWin.title);
    return w;
}
void SetWindowFocus(const char* title) {
    if (!title) return;
    g_focusWin = windowId(title);
    for (WinSlot& ws : g_wins) if (ws.used && ws.id == g_focusWin && ws.z != 0) ws.z = ++g_zCounter;   // and raise it
}

// ---- step 6.10: window and frame calls (declared in OkayUICompat.hpp / OkayUIInternals.hpp) ----
void SetNextWindowCollapsed(bool collapsed, Cond cond) { g_nextCollapse = NextCollapse{true, collapsed, (int)cond}; }
void SetNextWindowBgAlpha(float alpha) { g_nextBgAlpha = std::isfinite(alpha) ? (alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha)) : -1.0f; }
bool SetWindowPos(const char* title, Vec2 pos, Cond cond) {
    if (!title || !*title || !std::isfinite(pos.x) || !std::isfinite(pos.y)) return false;
    const int id = windowId(title);
    for (WinSlot& ws : g_wins) if (ws.used && ws.id == id) {
        const bool appearing = (ws.lastFrame == 0 || ws.lastFrame + 1 < g_frameCount);
        const bool take = cond == Cond_Always || (cond == Cond_Appearing && appearing) || ((cond == Cond_Once || cond == Cond_FirstUseEver) && !ws.posInit);
        if (!take) return true;
        ws.x = pos.x < -1.0e6f ? -1.0e6f : (pos.x > 1.0e6f ? 1.0e6f : pos.x);
        ws.y = pos.y < -1.0e6f ? -1.0e6f : (pos.y > 1.0e6f ? 1.0e6f : pos.y);
        ws.posInit = true;
        return true;
    }
    return false;
}
void SetWindowPos(Vec2 pos, Cond cond) { if (g_curWin.valid) SetWindowPos(g_curWin.title, pos, cond); }
bool BringWindowToDisplayFront(const char* title) {
    if (!title || !*title) return false;
    const int id = windowId(title);
    for (WinSlot& ws : g_wins) if (ws.used && ws.id == id) { if (ws.z != 0) ws.z = ++g_zCounter; return true; }
    return false;
}
bool BringWindowToDisplayFront(const CurrentWindow& w) { return w.valid && BringWindowToDisplayFront(w.title); }
WindowInfo FindWindowByID(unsigned id) {
    WindowInfo wi;
    if (id == 0) return wi;
    for (const WinSlot& ws : g_wins) if (ws.used && (unsigned)ws.id == id) {
        wi.found = true; wi.pos = Vec2{ws.x, ws.y}; wi.size = Vec2{ws.w, ws.h}; wi.focused = g_focusWin == ws.id;
        std::snprintf(wi.Name, sizeof wi.Name, "%s", ws.title); wi.Pos = wi.pos; wi.Size = wi.size; wi.ID = (unsigned)ws.id;
        break;
    }
    return wi;
}
bool AnyWindowFocused() { return g_focusWin != 0; }
unsigned WindowIdOfTitle(const char* title) { return title ? (unsigned)windowId(title) : 0u; }
int ListWindowSlots(WindowSlotInfo* out, int cap) {
    int n = 0;
    for (const WinSlot& ws : g_wins) {
        if (!ws.used || n >= cap) continue;
        WindowSlotInfo& o = out[n++];
        std::snprintf(o.title, sizeof o.title, "%s", ws.title); o.id = (unsigned)ws.id; o.pos = Vec2{ws.x, ws.y}; o.size = Vec2{ws.w, ws.h};
        o.lastFrame = ws.lastFrame; o.z = ws.z; o.focused = g_focusWin == ws.id;
    }
    return n;
}
void SetWindowItemWidthDefault(float width) {
    if (!g_curWin.valid || !(width > 0.0f) || !std::isfinite(width)) return;
    if (g_winItemWDepth > 0 && g_itemWTop >= g_winItemWDepth) { g_itemWStack[g_winItemWDepth - 1] = width; return; }
    if (g_itemWTop >= 16) return;
    g_itemWStack[g_itemWTop++] = width; g_winItemWDepth = g_itemWTop;
}
static bool beginChildImpl(const char* id, float w, float h, bool border, float pad, bool reserveBar, float* autoH = nullptr, float padX = -1.0f, bool outline = false, bool trim = false);
static float* g_childAutoSlot = nullptr;   // set by main's flags form of BeginChild (Child_AutoResizeY) for the plain one to pick up
bool BeginChild(const char* id, float w, float h, bool border) {
    float* const autoSlot = g_childAutoSlot; g_childAutoSlot = nullptr;
    const int depth = g_childTop;
    const bool ok = beginChildImpl(id, w, h, border, 6.0f, true);
    if (autoSlot && g_childTop == depth + 1) g_childStack[depth].autoSlot = autoSlot;   // EndChild stores the measured height there
    return ok;
}

// OkayUI's flags form (the default): Child_Borders is the plain form's filled frame, the padding is always there, Child_AutoResizeY follows
// the content one frame behind (a positive size.y is the smallest height), the other flags are accepted. The Dear ImGui-faithful form below
// is used while StyleVar_FramedWidgets is pushed.
static bool beginChildFlagsImGui(const char* id, Vec2 size, int flags);
bool BeginChild(const char* id, Vec2 size, int childFlags, int /*windowFlags*/) {
    if (g_framedWidgets) return beginChildFlagsImGui(id, size, childFlags);
    if (!id || !g_lay.active) return false;
    float h = std::isfinite(size.y) ? size.y : 0.0f;
    float w = std::isfinite(size.x) ? size.x : 0.0f;
    // Dear ImGui sizes: 0 is the space left, a negative size is the space left minus that much (-40 leaves 40 px below), on each axis.
    // (Not for an auto-resized height: that one follows the content.)
    if (w <= 0.0f) { const float room = availW(); w = w < 0.0f ? (room + w > 8.0f ? room + w : 8.0f) : 0.0f; }
    if (h <= 0.0f && !(childFlags & Child_AutoResizeY)) {
        const Vec2 room = GetContentRegionAvail();
        h = room.y + h;                           // h is 0 or negative here
        if (h < rowH() * 2.0f) h = rowH() * 2.0f; // never smaller than two rows
    }
    float* slot = nullptr;
    if (childFlags & Child_AutoResizeY) {
        slot = floatState(hashLabel(id) ^ 0x7a31, 0.0f);
        const float measured = *slot > 0.0f ? *slot : rowH();   // one frame behind: the first frame shows a single row
        h = h > measured ? h : measured;                          // a positive size is the smallest height
    }
    g_childAutoSlot = slot;
    const bool ok = BeginChild(id, w, h, (childFlags & Child_Borders) != 0);
    g_childAutoSlot = nullptr;
    return ok;
}

// Dear ImGui's form (StyleVar_FramedWidgets pushed): Child_FrameStyle draws the same frame as Child_Borders (a one-pixel outline, filled only
// when Col_ChildBg is set); padding only for a bordered or padded child; the scroll bar only when the content overflowed; Child_AutoResizeY
// as tall as last frame's content; the window flags and the other child flags are accepted and ignored.
static bool beginChildFlagsImGui(const char* id, Vec2 size, int flags) {
    if (!g_lay.active || !id) return false;
    const bool border = (flags & (Child_Borders | Child_FrameStyle)) != 0;
    const bool padded = border || (flags & Child_AlwaysUseWindowPadding);
    const float pad = padded ? (g_childPadY >= 0.0f ? g_childPadY : 6.0f) : 0.0f;                 // above and below
    const float padX = padded ? (g_childPadY >= 0.0f ? g_theme.windowPadding : 6.0f) : 0.0f;      // left and right
    const Vec2 avail = GetContentRegionAvail();
    // Dear ImGui's sizes: 0 fills what is left, a negative value leaves that much; a size that cannot be used falls back to filling.
    float w = std::isfinite(size.x) ? size.x : 0.0f, h = std::isfinite(size.y) ? size.y : 0.0f;
    w = w > 0.0f ? w : avail.x + w; h = h > 0.0f ? h : avail.y + h;
    if (w < 1.0f) w = std::max(avail.x, 1.0f);
    if (h < 1.0f) h = std::max(avail.y, rowH());
    float* autoH = nullptr;
    if (flags & Child_AutoResizeY) {   // as tall as its content was on the last frame (one row on the first); it never scrolls
        autoH = floatState(hashLabel(id) ^ 0x41755a, rowH());
        h = std::max(*autoH + 2.0f * pad, 1.0f);
    }
    return beginChildImpl(id, std::min(w, 1.0e6f), std::min(h, 1.0e6f), false, pad, false, autoH, padX, border, true);
}

// `border`: the old form's filled frame. `outline`: Dear ImGui's bordered child (Child_Borders): a one-pixel rounded outline in
// Col_Border, filled only when Col_ChildBg is set. `pad` is the padding above and below, `padX` left and right (< 0: the same as pad).
// `trim`: the gap after the last item is not content (the flags form, as in Dear ImGui: no scroll bar for a few pixels).
static bool beginChildImpl(const char* id, float w, float h, bool border, float pad, bool reserveBar, float* autoH, float padX, bool outline, bool trim) {
    if (padX < 0.0f) padX = pad;
    // Depth guard first: past the child-stack capacity the parent layout cannot
    // be saved, so the child cannot begin at all -- otherwise EndChild would pop
    // a slot that was never pushed and corrupt the parent's scroll/clip state.
    // Returning before place()/wheel handling also keeps the parent layout
    // cursor and scroll slots untouched on the skip path.
    if (!g_lay.active || g_childTop >= 8) return false;
    const int cid = hashLabel(id);
    const float rw = w > 0.0f ? w : availW();
    const float rh = h > 0.0f ? h : rowH() * 4.0f;   // (an auto-height child's height comes from its slot, in BeginChild)
    float rx, ry; place(rw, rh, rx, ry);

    float* scroll  = floatState(cid, 0.0f);
    float* maxSlot = floatState(cid ^ 0x4d61, 0.0f);   // last frame's max scroll
    const bool hovered = hit(rx, ry, rw, rh);
    if (hovered && g_in.wheel != 0.0f) { *scroll -= g_in.wheel * rowH(); if (*maxSlot > 0.0f) g_wheelUsed = true; }   // wheel scrolls
    // Clamp with the previous frame's content extent so layout uses a sane offset.
    if (*scroll < 0.0f) *scroll = 0.0f;
    if (*scroll > *maxSlot) *scroll = *maxSlot;

    // Frame + clip to the region.
    if (border) { quad(rx, ry, rw, rh, pick(Col_ChildBg, g_theme.panel)); }
    else if (g_imguiCoords && trim && !outline && colorIsSet(Col_ChildBg) && g_theme.colors[Col_ChildBg][3] > 0)   // Dear ImGui's rules (UseImGuiCoordinates): a child without a border still fills Col_ChildBg (rounded by ChildRounding)
        rrect(rx, ry, rw, rh, childRnd(), g_theme.colors[Col_ChildBg]);
    if (outline) {   // a stroked outline (filling under it would cost a whole child's area twice on a CPU rasteriser)
        const float rd = childRnd();
        if (colorIsSet(Col_ChildBg)) rrect(rx, ry, rw, rh, rd, g_theme.colors[Col_ChildBg]);
        const unsigned char* bc = pick(Col_Border, g_theme.border);
        // one-pixel edges between the corners, and each corner as three short segments of its arc (a fixed table: no sine per frame)
        const float r = std::min(rd, std::min(rw, rh) * 0.5f);
        quad(rx + r, ry, rw - 2.0f * r, 1.0f, bc); quad(rx + r, ry + rh - 1.0f, rw - 2.0f * r, 1.0f, bc);
        quad(rx, ry + r, 1.0f, rh - 2.0f * r, bc); quad(rx + rw - 1.0f, ry + r, 1.0f, rh - 2.0f * r, bc);
        if (r > 0.5f) {
            static const float k[4][2] = {{1.0f, 0.0f}, {0.8660254f, 0.5f}, {0.5f, 0.8660254f}, {0.0f, 1.0f}};   // cos, sin of 0, 30, 60, 90 degrees
            const float a = r - 0.5f;
            const float cx[4] = {rx + r, rx + rw - r, rx + rw - r, rx + r}, cy[4] = {ry + r, ry + r, ry + rh - r, ry + rh - r};
            const float sx[4] = {-1.0f, 1.0f, 1.0f, -1.0f}, sy[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
            for (int q = 0; q < 4; ++q)
                for (int i = 0; i < 3; ++i)
                    line(cx[q] + sx[q] * a * k[i][0], cy[q] + sy[q] * a * k[i][1], cx[q] + sx[q] * a * k[i + 1][0], cy[q] + sy[q] * a * k[i + 1][1], 1.0f, bc);
        }
    }
    // With a pushed ScrollbarSize the content is clipped where the bar starts while there is one (Dear ImGui's: a row running past the
    // content region is cut at the bar, not drawn under it); otherwise the whole child.
    const float sbClip = g_sbSize > 0.0f && (reserveBar || *maxSlot > 0.0f) ? g_sbSize + (outline ? 1.0f : 0.0f) : 0.0f;
    PushClipRect(rx, ry, std::max(0.0f, rw - sbClip), rh);

    // Save the parent layout, then set up the child's inner layout (scrolled).
    if (g_childTop < 8) {
        ChildSave& c = g_childStack[g_childTop++];
        c.lay = g_lay; c.rx = rx; c.ry = ry; c.rw = rw; c.rh = rh;
        c.top = ry + pad; c.scroll = scroll; c.maxSlot = maxSlot; c.pad = pad; c.padX = padX; c.hovered = hovered; c.setScroll = -1.0f; c.autoH = autoH; c.trim = trim; c.autoSlot = nullptr;
        c.sbW = g_sbSize > 0.0f ? g_sbSize : kScrollbarW; c.sbPushed = g_sbSize > 0.0f; c.sbInside = c.sbPushed && outline;
    }
    g_lay.ox = rx + padX;
    // The old form always leaves room for the scrollbar; the flags form only when the content overflowed last frame (Dear ImGui's way).
    g_lay.contentW = rw - 2.0f * padX - (reserveBar || *maxSlot > 0.0f ? (g_sbSize > 0.0f ? g_sbSize : kScrollbarW) : 0.0f);
    g_lay.bottom = ry + rh - pad;
    g_lay.oy = ry + pad - *scroll;
    g_lay.winX = rx; g_lay.winY = ry; g_lay.winW = rw; g_lay.winH = rh; g_lay.baseX = rx; g_lay.baseY = ry - *scroll;
    g_lay.cx = g_lay.ox;
    g_lay.cy = g_lay.oy;
    g_lay.indent = 0.0f;
    g_lay.seed = cid;                   // child-local widget id namespace
    g_lay.pendingSameLine = false;
    g_lay.prevX = g_lay.ox; g_lay.prevY = g_lay.oy; g_lay.prevW = 0.0f; g_lay.prevH = 0.0f;
    return true;
}

float GetScrollY()    { return g_childTop > 0 ? *g_childStack[g_childTop - 1].scroll : 0.0f; }
float GetScrollMaxY() { return g_childTop > 0 ? *g_childStack[g_childTop - 1].maxSlot : 0.0f; }
void SetScrollY(float y) {
    if (g_childTop <= 0) return;
    if (!(y > 0.0f)) y = 0.0f;                  // also catches NaN
    g_childStack[g_childTop - 1].setScroll = y;   // applied by EndChild, after it measured the content, and clamped to it
}

float GetChildViewHeight() { return g_childTop > 0 ? g_childStack[g_childTop - 1].rh - 2.0f * g_childStack[g_childTop - 1].pad : 0.0f; }   // hook for OkayUICompat's SetScrollHereY

void EndChild() {
    if (g_childTop <= 0) return;
    ChildSave& c = g_childStack[--g_childTop];
    // Content height = how far the cursor advanced from the (scrolled) top, in
    // unscrolled space. cy currently sits at top - scroll + contentHeight.
    float contentH = g_lay.cy - (c.top - *c.scroll);
    if (c.autoSlot) *c.autoSlot = contentH + 2.0f * c.pad;   // main's flags form with Child_AutoResizeY: next frame's height
    if (c.trim) contentH = std::max(0.0f, contentH - g_lay.spacingY);   // the last item's spacing is not content
    if (c.autoH) *c.autoH = std::max(0.0f, std::min(contentH, 1.0e6f));
    const float viewH = c.rh - 2.0f * c.pad;
    float maxScroll = contentH - viewH; if (maxScroll < 0.0f || c.autoH) maxScroll = 0.0f;   // an auto-height child never scrolls
    if (c.setScroll >= 0.0f) *c.scroll = c.setScroll;   // SetScrollY, now that the content is measured
    *c.maxSlot = maxScroll;                     // remember for next frame's clamp
    if (*c.scroll < 0.0f) *c.scroll = 0.0f;
    if (*c.scroll > maxScroll) *c.scroll = maxScroll;

    PopClipRect();   // stop clipping before drawing the scrollbar

    // Scrollbar (only when content overflows).
    if (maxScroll > 0.0f) {
        const float in = c.sbInside ? 1.0f : 0.0f;   // inside the outline: the bar stops one pixel short of each edge
        const float sbx = c.rx + c.rw - c.sbW - in, trackY = c.ry + in, trackH = c.rh - 2.0f * in;
        if (colorIsSet(Col_ScrollbarBg)) quad(sbx, trackY, c.sbW, trackH, pick(Col_ScrollbarBg, g_theme.track));
        float frac = viewH / contentH; if (frac > 1.0f) frac = 1.0f;
        float thumbH = frac * trackH; if (thumbH < 16.0f) thumbH = 16.0f;
        float t = maxScroll > 0.0f ? (*c.scroll / maxScroll) : 0.0f;
        float thumbY = trackY + t * (trackH - thumbH);
        // Drag the thumb.
        const int sbId = (int)((unsigned)c.lay.seed ^ 0x5cb0u ^ (unsigned)clampPx(sbx * 7.0f, 0)) & 0x7fffffff;
        const bool overThumb = hit(sbx, thumbY, c.sbW, thumbH);
        if (overThumb) g_hot = sbId;
        if (g_active == sbId) {
            if (!g_in.mouseDown) g_active = 0;
            else if (trackH - thumbH > 0.0f) *c.scroll += g_mouseDY * (maxScroll / (trackH - thumbH));
        } else if (overThumb && g_pressed) { g_active = sbId; g_focusClaimed = true; }
        if (*c.scroll < 0.0f) *c.scroll = 0.0f;
        if (*c.scroll > maxScroll) *c.scroll = maxScroll;
        const float pushed = g_sbSize; g_sbSize = c.sbPushed ? c.sbW : -1.0f;   // the bar as it was begun
        scrollbarThumb(sbx, trackY, trackH, thumbY, thumbH, g_active == sbId, overThumb, false);
        g_sbSize = pushed;
    }

    g_lay = c.lay;   // restore the parent layout (cursor already past the region)
    g_posOverride = false;
}

} // namespace OkayUI

// ---- What belongs to one context (OkayUI_ContextState.hpp: each context keeps its own copy of these) ----
namespace OkayUI {
OKAYUI_CONTEXT_STATE(
    OKAYUI_CTX_VAR(ui_core::g_bareNext),
    OKAYUI_CTX_VAR(g_childAutoSlot),
    OKAYUI_CTX_VAR(ui_core::g_curWin),
    OKAYUI_CTX_VAR(ui_core::g_dockAware),
    OKAYUI_CTX_VAR(ui_core::g_dockRouted),
    OKAYUI_CTX_VAR(ui_core::g_focusWin),
    OKAYUI_CTX_VAR(ui_core::g_nextWinPos),
    OKAYUI_CTX_VAR(ui_core::g_nextWinSize),
    OKAYUI_CTX_VAR(ui_core::g_noTitleNext),
    OKAYUI_CTX_VAR(ui_core::g_pinNext),
    OKAYUI_CTX_VAR(g_resizeMinH),
    OKAYUI_CTX_VAR(g_resizeMinW),
    OKAYUI_CTX_VAR(ui_core::g_resizeNext),
    OKAYUI_CTX_VAR(ui_core::g_we),
    OKAYUI_CTX_VAR(ui_core::g_wflagsNext))
} // namespace OkayUI
