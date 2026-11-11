// okay::imui - context, frame model, windows, layout and the first widgets. See Imui.hpp.
#include "imui/Imui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace okay {
namespace imui {

namespace {

Context* g_ctx = nullptr;

float Fin(float f, float fallback = 0) { return std::isfinite(f) ? std::max(-1e6f, std::min(1e6f, f)) : fallback; }
Vec2 Fin(Vec2 v) { return Vec2(Fin(v.x), Fin(v.y)); }
float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float FrameH(Context& c) { return c.font.lineHeight + 2 * c.style.framePadding.y; }

const char* LabelEnd(const char* l) {
    for (const char* p = l; p[0]; ++p) if (p[0] == '#' && p[1] == '#') return p;
    return l + std::strlen(l);
}

bool Clicked(const Context& c, int b = 0) { return c.in.mouseDown[b] && !c.prev.mouseDown[b]; }

bool HoverTest(Context& c, Window* w, const Rect& bb) {
    return c.hoveredWin == w && bb.Contains(c.in.mousePos) && w->dl->ClipRect().Contains(c.in.mousePos);
}

struct Beh { bool hovered = false, held = false, pressed = false, started = false; };

Beh ButtonBehavior(Context& c, Window* w, const Rect& bb, std::uint32_t id) {
    Beh b;
    b.hovered = HoverTest(c, w, bb) && (c.activeId == 0 || c.activeId == id);
    if (b.hovered) c.hoveredId = id;
    if (b.hovered && Clicked(c) && c.activeId == 0) { c.activeId = id; b.started = true; }
    if (c.activeId == id) {
        b.held = c.in.mouseDown[0];
        if (!b.held) { b.pressed = HoverTest(c, w, bb); c.activeId = 0; }
    }
    return b;
}

void SetLast(Window* w, std::uint32_t id, const Rect& bb, bool hovered, bool clicked) {
    w->lastItemId = id; w->lastItemRect = bb; w->lastHovered = hovered; w->lastClicked = clicked;
}

Rect Place(Context& c, Window* w, Vec2 size) {
    size = Fin(size);
    if (size.x < 0) size.x = 0;
    if (size.y < 0) size.y = 0;
    if (!w->sameLine) { w->lineY = w->cur.y; w->lineH = 0; if (!w->alignFresh) w->lineAlign = 0; }
    w->alignFresh = false;
    w->sameLine = false;
    const Vec2 pos(w->cur.x, w->lineY);
    w->lineH = std::max(w->lineH, size.y);
    w->lineEndX = pos.x + size.x;
    w->itemsMax.x = std::max(w->itemsMax.x, pos.x + size.x);
    w->itemsMax.y = std::max(w->itemsMax.y, pos.y + size.y);
    w->cur = Vec2(w->leftX, w->lineY + w->lineH + c.style.itemSpacing.y);
    w->lastItemRect = Rect(pos, pos + size);
    return w->lastItemRect;
}

float ItemWidth(Context& c, Window* w) {
    if (w->itemWidth > 0) { float v = w->itemWidth; w->itemWidth = 0; return v; }
    const float avail = w->rightX - w->cur.x;
    (void)c;
    return std::max(60.0f, avail * 0.6f);
}

void PushIdValue(Window* w, std::uint32_t v) { w->idStack.push_back(v); }

std::string VFormat(const char* fmt, va_list ap) {
    char stackBuf[512];
    va_list ap2; va_copy(ap2, ap);
    int n = std::vsnprintf(stackBuf, sizeof stackBuf, fmt ? fmt : "", ap);
    std::string out;
    if (n < 0) out = "";
    else if ((std::size_t)n < sizeof stackBuf) out.assign(stackBuf, (std::size_t)n);
    else { out.resize((std::size_t)n + 1); std::vsnprintf(&out[0], out.size(), fmt, ap2); out.resize((std::size_t)n); }
    va_end(ap2);
    return out;
}

} // namespace

// ---------------------------------------------------------------------------------------------
Style::Style() {
    colors[Col_Text] = MakeCol(232, 234, 240);
    colors[Col_TextDisabled] = MakeCol(128, 134, 150);
    colors[Col_WindowBg] = MakeCol(24, 27, 36);
    colors[Col_TitleBg] = MakeCol(32, 36, 48);
    colors[Col_TitleBgActive] = MakeCol(44, 60, 96);
    colors[Col_Border] = MakeCol(60, 66, 86);
    colors[Col_FrameBg] = MakeCol(40, 44, 58);
    colors[Col_FrameBgHovered] = MakeCol(54, 60, 80);
    colors[Col_FrameBgActive] = MakeCol(64, 72, 98);
    colors[Col_Button] = MakeCol(52, 84, 150);
    colors[Col_ButtonHovered] = MakeCol(70, 110, 190);
    colors[Col_ButtonActive] = MakeCol(40, 66, 120);
    colors[Col_CheckMark] = MakeCol(120, 200, 255);
    colors[Col_SliderGrab] = MakeCol(96, 150, 230);
    colors[Col_Header] = MakeCol(52, 84, 150, 110);
    colors[Col_HeaderHovered] = MakeCol(70, 110, 190, 160);
    colors[Col_HeaderActive] = MakeCol(70, 110, 190, 220);
    colors[Col_Separator] = MakeCol(70, 76, 98);
    colors[Col_ScrollbarBg] = MakeCol(16, 18, 24);
    colors[Col_ScrollbarGrab] = MakeCol(80, 88, 114);
    colors[Col_PopupBg] = MakeCol(30, 33, 44);
    colors[Col_ResizeGrip] = MakeCol(96, 104, 134);
    colors[Col_TextSelectedBg] = MakeCol(60, 110, 200, 140);
    windowRounding = 0;
}

Context::Context(FontAtlas::GlyphSource rows, int fontScale) { font.BuildFrom8x8(rows, fontScale); }

Context* CreateContext(FontAtlas::GlyphSource rows, int fontScale) { return new Context(rows, fontScale); }
void DestroyContext(Context* ctx) { if (g_ctx == ctx) g_ctx = nullptr; delete ctx; }
void SetCurrentContext(Context* ctx) { g_ctx = ctx; }
Context* GetContext() { return g_ctx; }

// ---------------------------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------------------------
void NewFrame(const Input& inRaw, Vec2 displaySize) {
    Context* cp = g_ctx;
    if (!cp) return;
    Context& c = *cp;
    if (c.inFrame) Render();    // forgot Render(): finish the old frame for the caller
    c.prev = c.in;
    c.in = inRaw;
    c.in.mousePos = Vec2(Fin(c.in.mousePos.x, -1e6f), Fin(c.in.mousePos.y, -1e6f));
    c.in.wheel = Fin(c.in.wheel);
    c.in.dt = Clamp(Fin(c.in.dt, 1.0f / 60.0f), 0.0f, 1.0f);
    c.display = Vec2(std::max(1.0f, Fin(displaySize.x, 1)), std::max(1.0f, Fin(displaySize.y, 1)));
    c.mouseDelta = c.in.mousePos - c.prev.mousePos;
    if (c.frame == 0) c.mouseDelta = Vec2(0, 0);
    ++c.frame;
    c.time += c.in.dt;
    c.inFrame = true;
    c.hoveredId = 0;
    c.popupClosedId = 0;
    c.stack.clear();
    c.cur = nullptr;

    for (int k = 0; k < Key_Count; ++k) {
        const bool d = c.in.keyDown[k], pd = c.prev.keyDown[k];
        c.keyPressed[k] = false;
        if (d && !pd) { c.keyPressed[k] = true; c.keyHeld[k] = 0; }
        else if (d) {
            const float before = c.keyHeld[k];
            c.keyHeld[k] += c.in.dt;
            const int a = before < 0.4f ? -1 : (int)((before - 0.4f) / 0.05f);
            const int b = c.keyHeld[k] < 0.4f ? -1 : (int)((c.keyHeld[k] - 0.4f) / 0.05f);
            c.keyPressed[k] = b > a;
        } else c.keyHeld[k] = 0;
    }

    // Who is under the mouse? Decided from last frame's rectangles, front to back.
    c.hoveredRoot = c.hoveredWin = nullptr;
    const bool popupOpen = c.popupId != 0;
    for (std::size_t i = c.order.size(); i-- > 0;) {
        Window* w = c.order[i];
        if (!w->activeThisFrame) continue;
        if (popupOpen && !(w->flags & Win_Popup)) continue;
        if (w->prevRect.Contains(c.in.mousePos)) { c.hoveredRoot = w; break; }
    }
    if (c.hoveredRoot) {
        c.hoveredWin = c.hoveredRoot;
        for (const auto& cr : c.hoveredRoot->prevChildRects)
            if (cr.second.Contains(c.in.mousePos)) {
                auto it = c.windows.find(cr.first);
                if (it != c.windows.end()) c.hoveredWin = it->second.get();
            }
    }
    if (Clicked(c)) {
        if (c.popupId && !(c.hoveredRoot && (c.hoveredRoot->flags & Win_Popup))) { c.popupClosedId = c.popupId; c.popupId = 0; }
        c.focusId = 0;     // a text box that is clicked takes it back this frame
        if (c.hoveredRoot) {
            if (!(c.hoveredRoot->flags & Win_Popup)) {
                auto it = std::find(c.order.begin(), c.order.end(), c.hoveredRoot);
                if (it != c.order.end()) { c.order.erase(it); c.order.push_back(c.hoveredRoot); }
            }
            c.focusedRoot = c.hoveredRoot;
        } else c.focusedRoot = nullptr;
    }
    for (auto& kv : c.windows) kv.second->activeThisFrame = false;
}

const DrawData& Render() {
    Context* cp = g_ctx;
    static DrawData empty;
    if (!cp) return empty;
    Context& c = *cp;
    if (!c.inFrame) { ++c.errors; return c.drawData; }
    c.errors += (int)c.stack.size();
    c.stack.clear();
    c.cur = nullptr;
    while (!c.colorStack.empty()) { c.style.colors[c.colorStack.back().first] = c.colorStack.back().second; c.colorStack.pop_back(); ++c.errors; }
    c.inFrame = false;
    if (!c.in.mouseDown[0]) c.activeId = 0;
    if (c.popupId) {
        bool any = false;
        for (Window* w : c.order) if (w->activeThisFrame && (w->flags & Win_Popup)) any = true;
        if (!any) c.popupId = 0;
    }
    c.order.erase(std::remove_if(c.order.begin(), c.order.end(), [](Window* w) { return !w->activeThisFrame; }), c.order.end());
    std::stable_sort(c.order.begin(), c.order.end(), [](Window* a, Window* b) { return !(a->flags & Win_Popup) && (b->flags & Win_Popup); });
    c.drawData.lists.clear();
    c.drawData.display = c.display;
    for (Window* w : c.order) c.drawData.lists.push_back(&w->own);
    return c.drawData;
}

// ---------------------------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------------------------
void SetNextWindowPos(Vec2 pos, Cond cond) { if (!g_ctx) return; g_ctx->nextPosSet = true; g_ctx->nextPos = Fin(pos); g_ctx->nextPosCond = cond; }
void SetNextWindowSize(Vec2 size, Cond cond) { if (!g_ctx) return; g_ctx->nextSizeSet = true; g_ctx->nextSize = Fin(size); g_ctx->nextSizeCond = cond; }

namespace {

// Shared by top-level windows and scroll regions: reset the cursor and clip to the content area.
void SetupLayout(Context& c, Window* w, const Rect& area, Vec2 pad) {
    const bool wantBar = !(w->flags & Win_NoScrollbar) && w->contentSize.y > area.Height() + 0.5f;
    w->hasScrollbar = wantBar;
    const float sb = wantBar ? c.style.scrollbarSize : 0;
    w->sbTrack = Rect(area.max.x - sb, area.min.y, area.max.x, area.max.y);
    w->contentRect = Rect(area.min, Vec2(area.max.x - sb, area.max.y));
    if (c.hoveredWin == w && c.in.wheel != 0) w->scrollY -= c.in.wheel * c.font.lineHeight * 3;
    const float maxScroll = std::max(0.0f, w->contentSize.y - area.Height());
    w->scrollY = Clamp(Fin(w->scrollY), 0, maxScroll);
    w->contentOrigin = Vec2(area.min.x + pad.x, area.min.y + pad.y - w->scrollY);
    w->cur = w->contentOrigin;
    w->leftX = w->contentOrigin.x;
    w->indent = 0;
    w->rightX = w->contentRect.max.x - pad.x;
    w->lineY = w->contentOrigin.y; w->lineH = 0; w->lineEndX = w->leftX;
    w->lineAlign = w->pendingAlign = 0; w->sameLine = w->alignFresh = false;
    w->itemsMax = w->contentOrigin;
    w->itemWidth = 0;
    w->colCount = 1; w->colIdx = 0;
    w->groups.clear();
    w->lastItemId = 0; w->lastHovered = w->lastClicked = false;
    w->dl->PushClipRect(w->contentRect);
}

void FinishLayout(Context& c, Window* w, Vec2 pad) {
    if (w->colCount > 1) Columns(1);
    if (!w->groups.empty()) { c.errors += (int)w->groups.size(); w->groups.clear(); }
    w->contentSize = Vec2(w->itemsMax.x - w->contentOrigin.x + 2 * pad.x, w->itemsMax.y - w->contentOrigin.y + 2 * pad.y);
    w->dl->PopClipRect();
    // scrollbar
    const float viewH = w->contentRect.Height();
    if (w->hasScrollbar && w->contentSize.y > viewH + 0.5f) {
        const Rect tr = w->sbTrack;
        w->dl->AddRectFilled(tr.min, tr.max, c.style.colors[Col_ScrollbarBg]);
        const float total = w->contentSize.y, maxScroll = total - viewH;
        const float thumbH = std::max(16.0f, viewH * viewH / total);
        const float travel = std::max(1.0f, viewH - thumbH);
        const float ty = tr.min.y + (maxScroll > 0 ? w->scrollY / maxScroll : 0) * travel;
        const Rect thumb(tr.min.x + 1, ty, tr.max.x - 1, ty + thumbH);
        const std::uint32_t sid = HashString("#vscroll", w->idStack.empty() ? w->id : w->idStack.front());
        const bool hov = HoverTest(c, w, tr) && (c.activeId == 0 || c.activeId == sid);
        if (hov && Clicked(c) && c.activeId == 0) {
            c.activeId = sid;
            if (!thumb.Contains(c.in.mousePos))   // click on the track: jump there
                w->scrollY = Clamp((c.in.mousePos.y - tr.min.y - thumbH * 0.5f) / travel * maxScroll, 0, maxScroll);
        }
        if (c.activeId == sid) {
            if (c.in.mouseDown[0]) w->scrollY = Clamp(w->scrollY + c.mouseDelta.y / travel * maxScroll, 0, maxScroll);
            else c.activeId = 0;
        }
        const Rect th2(tr.min.x + 1, tr.min.y + (maxScroll > 0 ? w->scrollY / maxScroll : 0) * travel, tr.max.x - 1, 0);
        w->dl->AddRectFilled(th2.min, Vec2(th2.max.x, th2.min.y + thumbH), c.style.colors[Col_ScrollbarGrab], 2);
    }
}

} // namespace

bool Begin(const char* name, bool* open, int flags) {
    Context* cp = g_ctx;
    if (!cp || !cp->inFrame || !name) return false;
    Context& c = *cp;
    const std::uint32_t id = HashString(name, 0);
    std::unique_ptr<Window>& slot = c.windows[id];
    bool first = false;
    if (!slot) {
        slot.reset(new Window());
        slot->id = id; slot->name = name;
        const float cascade = 40.0f + 26.0f * (float)(c.windows.size() % 10);
        slot->pos = Vec2(cascade, cascade);
        slot->size = Vec2(300, 200);
        first = true;
    }
    Window* w = slot.get();
    const bool appending = (w->lastFrame == c.frame);
    w->lastFrame = c.frame;
    w->activeThisFrame = true;
    w->flags = flags & ~Win_Child;
    w->parent = nullptr; w->root = w;
    w->dl = &w->own;
    if (!appending) w->childRects.clear();
    if (std::find(c.order.begin(), c.order.end(), w) == c.order.end()) c.order.push_back(w);
    if (c.nextPosSet && (c.nextPosCond == Cond_Always || first)) w->pos = c.nextPos;
    const bool sizeSet = c.nextSizeSet && (c.nextSizeCond == Cond_Always || first);
    if (sizeSet) w->size = c.nextSize;
    const Vec2 minSize = c.nextSize;
    const bool minSizeSet = c.nextSizeSet;
    c.nextPosSet = c.nextSizeSet = false;
    w->pos = Fin(w->pos);
    w->size = Fin(w->size);

    const bool noTitle = (flags & Win_NoTitleBar) != 0;
    const float titleH = noTitle ? 0.0f : FrameH(c);
    const Vec2 pad = c.style.windowPadding;
    w->measuring = false;
    if (flags & Win_AlwaysAutoResize) {
        if (w->hasContent) w->size = Vec2(w->contentSize.x, w->contentSize.y + titleH);
        else w->measuring = true;
        if (minSizeSet) { w->size.x = std::max(w->size.x, minSize.x); w->size.y = std::max(w->size.y, minSize.y); }
        w->size.x = Clamp(w->size.x, 40, c.display.x); w->size.y = Clamp(w->size.y, titleH + 8, c.display.y);
    }
    w->size.x = std::max(w->size.x, 40.0f); w->size.y = std::max(w->size.y, titleH + 8.0f);

    // title bar: collapse arrow, close button, dragging
    w->idStack.clear(); w->idStack.push_back(id);
    bool collapsed = w->collapsed && !noTitle;
    Rect titleR(w->pos, Vec2(w->pos.x + w->size.x, w->pos.y + titleH));
    if (!noTitle && !appending) {
        const Rect arrow(titleR.min, Vec2(titleR.min.x + titleH, titleR.max.y));
        const Rect close(Vec2(titleR.max.x - titleH, titleR.min.y), titleR.max);
        const std::uint32_t aid = HashString("#collapse", id), cid = HashString("#close", id), tid = HashString("#title", id);
        if (!(flags & Win_NoCollapse)) {
            Beh b = ButtonBehavior(c, w, arrow, aid);
            if (b.pressed) w->collapsed = !w->collapsed;
        }
        if (open) {
            Beh b = ButtonBehavior(c, w, close, cid);
            if (b.pressed) *open = false;
        }
        Rect drag = titleR;
        if (!(flags & Win_NoCollapse)) drag.min.x = arrow.max.x;
        if (open) drag.max.x = close.min.x;
        const bool hov = HoverTest(c, w, drag) && (c.activeId == 0 || c.activeId == tid);
        if (hov && Clicked(c) && c.activeId == 0 && !(flags & Win_NoMove)) c.activeId = tid;
        if (c.activeId == tid) {
            if (c.in.mouseDown[0]) w->pos = Fin(w->pos + c.mouseDelta);
            else c.activeId = 0;
        }
        titleR = Rect(w->pos, Vec2(w->pos.x + w->size.x, w->pos.y + titleH));
        collapsed = w->collapsed;
    }
    // resize grip (interaction first so this frame already uses the new size)
    const bool canResize = !(flags & (Win_NoResize | Win_AlwaysAutoResize)) && !collapsed;
    const std::uint32_t rid = HashString("#resize", id);
    if (canResize && !appending) {
        const Rect grip(Vec2(w->pos.x + w->size.x - 14, w->pos.y + w->size.y - 14), w->pos + w->size);
        const bool hov = HoverTest(c, w, grip) && (c.activeId == 0 || c.activeId == rid);
        if (hov) c.hoveredId = rid;
        if (hov && Clicked(c) && c.activeId == 0) c.activeId = rid;
        if (c.activeId == rid) {
            if (c.in.mouseDown[0]) {
                w->size = Fin(w->size + c.mouseDelta);
                w->size.x = std::max(w->size.x, 80.0f); w->size.y = std::max(w->size.y, titleH + 30.0f);
            } else c.activeId = 0;
        }
    }
    titleR = Rect(w->pos, Vec2(w->pos.x + w->size.x, w->pos.y + titleH));
    const float h = collapsed ? titleH : w->size.y;
    const Rect full(w->pos, Vec2(w->pos.x + w->size.x, w->pos.y + h));
    w->prevRect = w->measuring ? Rect(0, 0, 0, 0) : full;

    if (!appending) w->own.Init(&c.font, Rect(Vec2(0, 0), c.display));
    w->dl->PushClipRect(full);
    const bool popup = (flags & Win_Popup) != 0;
    if (!(flags & Win_NoBackground) && !collapsed)
        w->dl->AddRectFilled(full.min, full.max, c.style.colors[popup ? Col_PopupBg : Col_WindowBg], c.style.windowRounding);
    if (!noTitle) {
        const bool focused = c.focusedRoot == w;
        w->dl->AddRectFilled(titleR.min, titleR.max, c.style.colors[focused ? Col_TitleBgActive : Col_TitleBg], 0);
        const float ah = titleH;
        const Vec2 ac(titleR.min.x + ah * 0.5f, titleR.min.y + ah * 0.5f);
        const Col tc = c.style.colors[Col_Text];
        if (!(flags & Win_NoCollapse)) {
            if (w->collapsed) w->dl->AddTriangleFilled(Vec2(ac.x - 3, ac.y - 5), Vec2(ac.x - 3, ac.y + 5), Vec2(ac.x + 4, ac.y), tc);
            else w->dl->AddTriangleFilled(Vec2(ac.x - 5, ac.y - 3), Vec2(ac.x + 5, ac.y - 3), Vec2(ac.x, ac.y + 4), tc);
        }
        const char* te = LabelEnd(name);
        const float tx = titleR.min.x + ((flags & Win_NoCollapse) ? c.style.framePadding.x : ah);
        w->dl->PushClipRect(Rect(titleR.min, Vec2(titleR.max.x - (open ? ah : 0), titleR.max.y)));
        w->dl->AddText(Vec2(tx, titleR.min.y + c.style.framePadding.y), tc, name, te);
        w->dl->PopClipRect();
        if (open) {
            const Vec2 cc(titleR.max.x - ah * 0.5f, titleR.min.y + ah * 0.5f);
            w->dl->AddLine(Vec2(cc.x - 4, cc.y - 4), Vec2(cc.x + 4, cc.y + 4), tc, 1.5f);
            w->dl->AddLine(Vec2(cc.x - 4, cc.y + 4), Vec2(cc.x + 4, cc.y - 4), tc, 1.5f);
        }
    }
    if (!(flags & Win_NoBackground) && !collapsed) w->dl->AddRect(full.min, full.max, c.style.colors[Col_Border], c.style.windowRounding, 1);

    c.stack.push_back(w);
    c.cur = w;
    if (collapsed) {
        w->skipItems = true;
        w->contentRect = Rect(full.min, full.min);
        w->itemsMax = w->contentOrigin = full.min;
        w->colCount = 1; w->groups.clear();
        w->dl->PushClipRect(Rect(0, 0, 0, 0));
        return false;
    }
    w->skipItems = false;
    SetupLayout(c, w, Rect(Vec2(w->pos.x + 1, w->pos.y + titleH), Vec2(w->pos.x + w->size.x - 1, w->pos.y + h - 1)), pad);
    return true;
}

void End() {
    Context* cp = g_ctx;
    if (!cp || !cp->inFrame) return;
    Context& c = *cp;
    if (c.stack.empty() || c.cur->parent) { ++c.errors; return; }
    Window* w = c.cur;
    const Vec2 pad = c.style.windowPadding;
    if (w->skipItems) {
        w->dl->PopClipRect();
    } else {
        FinishLayout(c, w, pad);
        // resize grip, drawn over the content
        if (!(w->flags & (Win_NoResize | Win_AlwaysAutoResize))) {
            const Vec2 br = w->pos + w->size;
            const std::uint32_t rid = HashString("#resize", w->id);
            const Col gc = (c.activeId == rid || c.hoveredId == rid) ? c.style.colors[Col_Text] : c.style.colors[Col_ResizeGrip];
            w->dl->AddTriangleFilled(Vec2(br.x - 12, br.y - 2), Vec2(br.x - 2, br.y - 2), Vec2(br.x - 2, br.y - 12), gc);
        }
    }
    w->dl->PopClipRect();
    if (w->measuring) { w->own.Clear(); }
    if (!w->collapsed || (w->flags & Win_NoTitleBar)) w->hasContent = true;
    w->prevChildRects = w->childRects;
    c.stack.pop_back();
    c.cur = c.stack.empty() ? nullptr : c.stack.back();
}

bool BeginScrollRegion(const char* idName, Vec2 size) {
    Context* cp = g_ctx;
    if (!cp || !cp->inFrame || !cp->cur || !idName) return false;
    Context& c = *cp;
    Window* parent = c.cur;
    const std::uint32_t cid = HashString(idName, parent->idStack.empty() ? parent->id : parent->idStack.back());
    std::unique_ptr<Window>& slot = c.windows[cid];
    if (!slot) { slot.reset(new Window()); slot->id = cid; slot->name = idName; }
    Window* ch = slot.get();
    ch->lastFrame = c.frame; ch->activeThisFrame = true;
    ch->flags = Win_Child; ch->parent = parent; ch->root = parent->root ? parent->root : parent;
    ch->dl = parent->dl;
    ch->idStack.clear(); ch->idStack.push_back(cid);
    size = Fin(size);
    const Vec2 avail(parent->rightX - parent->cur.x, parent->contentRect.max.y - parent->cur.y);
    if (size.x <= 0) size.x = std::max(20.0f, avail.x + size.x);
    if (size.y <= 0) size.y = std::max(40.0f, avail.y + size.y);
    ch->skipItems = parent->skipItems;
    Rect bb = Place(c, parent, size);
    ch->pos = bb.min; ch->size = size;
    if (!ch->skipItems) {
        Rect vis = bb.Intersect(parent->dl->ClipRect());
        ch->root->childRects.push_back({cid, vis});
        parent->dl->AddRectFilled(bb.min, bb.max, c.style.colors[Col_FrameBg], 0);
        parent->dl->AddRect(bb.min, bb.max, c.style.colors[Col_Border], 0, 1);
        SetupLayout(c, ch, bb, Vec2(4, 4));
    } else {
        ch->dl->PushClipRect(Rect(0, 0, 0, 0));
        ch->contentRect = Rect(bb.min, bb.min);
        ch->itemsMax = ch->contentOrigin = bb.min;
        ch->colCount = 1; ch->groups.clear();
    }
    c.stack.push_back(ch);
    c.cur = ch;
    return true;
}

void EndScrollRegion() {
    Context* cp = g_ctx;
    if (!cp || !cp->inFrame) return;
    Context& c = *cp;
    if (c.stack.empty() || !c.cur->parent) { ++c.errors; return; }
    Window* ch = c.cur;
    if (ch->skipItems) ch->dl->PopClipRect();
    else FinishLayout(c, ch, Vec2(4, 4));
    c.stack.pop_back();
    c.cur = c.stack.empty() ? nullptr : c.stack.back();
}

// ---------------------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------------------
void SameLine(float offset, float spacing) {
    if (!g_ctx || !g_ctx->cur) return;
    Window* w = g_ctx->cur;
    if (w->skipItems) return;
    const float sp = spacing < 0 ? g_ctx->style.itemSpacing.x : spacing;
    w->cur.x = Fin(offset != 0 ? w->leftX + offset : w->lineEndX + sp);
    w->cur.y = w->lineY;
    w->sameLine = true;
    w->lineAlign = std::max(w->lineAlign, w->pendingAlign);
}
void Spacing() { if (g_ctx && g_ctx->cur && !g_ctx->cur->skipItems) Place(*g_ctx, g_ctx->cur, Vec2(0, g_ctx->style.itemSpacing.y)); }
void Dummy(Vec2 size) { if (g_ctx && g_ctx->cur && !g_ctx->cur->skipItems) Place(*g_ctx, g_ctx->cur, size); }
void Indent(float v) {
    if (!g_ctx || !g_ctx->cur) return;
    Window* w = g_ctx->cur;
    if (v == 0) v = g_ctx->style.indentSpacing;
    v = Fin(v);
    w->leftX += v; w->indent += v; w->cur.x = w->leftX;
}
void Unindent(float v) { if (g_ctx) Indent(-(v == 0 ? g_ctx->style.indentSpacing : v)); }
void Separator() {
    if (!g_ctx || !g_ctx->cur || g_ctx->cur->skipItems) return;
    Context& c = *g_ctx; Window* w = c.cur;
    const float width = std::max(1.0f, w->rightX - w->cur.x);
    Rect bb = Place(c, w, Vec2(width, 1));
    w->dl->AddRectFilled(bb.min, bb.max, c.style.colors[Col_Separator]);
}
void BeginGroup() {
    if (!g_ctx || !g_ctx->cur) return;
    Window* w = g_ctx->cur;
    Window::Group g{w->cur, w->lineY, w->lineH, w->lineEndX, w->lineAlign, w->sameLine, w->alignFresh, w->itemsMax};
    w->groups.push_back(g);
    w->itemsMax = w->cur;
}
void EndGroup() {
    if (g_ctx && !g_ctx->cur) { ++g_ctx->errors; return; }
    if (!g_ctx) return;
    Context& c = *g_ctx; Window* w = c.cur;
    if (w->groups.empty()) { ++c.errors; return; }
    const Window::Group g = w->groups.back(); w->groups.pop_back();
    const Vec2 ext(std::max(0.0f, w->itemsMax.x - g.cur.x), std::max(0.0f, w->itemsMax.y - g.cur.y));
    const Vec2 outer = g.itemsMax;
    w->cur = g.cur; w->lineY = g.lineY; w->lineH = g.lineH; w->lineEndX = g.lineEndX;
    w->lineAlign = g.lineAlign; w->sameLine = g.sameLine; w->alignFresh = g.alignFresh;
    w->itemsMax = outer;
    Place(c, w, ext);
}
void Columns(int count) {
    if (!g_ctx || !g_ctx->cur) return;
    Context& c = *g_ctx; Window* w = c.cur;
    if (w->colCount > 1) {
        const float y = std::max(w->colMaxY, w->cur.y);
        w->leftX = w->colLeft0; w->rightX = w->colRight0;
        w->cur = Vec2(w->leftX, y); w->lineY = y; w->lineH = 0; w->sameLine = false;
        w->colCount = 1; w->colIdx = 0;
    }
    if (count > 1) {
        count = std::min(count, 32);
        w->colCount = count; w->colIdx = 0;
        w->colLeft0 = w->leftX; w->colRight0 = w->rightX;
        w->colWidth = std::max(1.0f, (w->rightX - w->leftX) / count);
        w->colRowY = w->colMaxY = w->cur.y;
        w->rightX = w->leftX + w->colWidth - c.style.itemSpacing.x;
        w->sameLine = false;
    }
}
void NextColumn() {
    if (!g_ctx || !g_ctx->cur) return;
    Context& c = *g_ctx; Window* w = c.cur;
    if (w->colCount <= 1) return;
    w->colMaxY = std::max(w->colMaxY, w->cur.y);
    if (++w->colIdx >= w->colCount) { w->colIdx = 0; w->colRowY = w->colMaxY; }
    w->leftX = w->colLeft0 + w->colIdx * w->colWidth;
    w->rightX = w->leftX + w->colWidth - c.style.itemSpacing.x;
    w->cur = Vec2(w->leftX, w->colRowY); w->lineY = w->colRowY; w->lineH = 0; w->sameLine = false;
}
void SetNextItemWidth(float v) { if (g_ctx && g_ctx->cur) g_ctx->cur->itemWidth = Fin(v); }
void AlignTextToFramePadding() {
    if (!g_ctx || !g_ctx->cur) return;
    Window* w = g_ctx->cur;
    w->lineAlign = std::max(w->lineAlign, g_ctx->style.framePadding.y);
    w->alignFresh = true;
}
Vec2 GetCursorScreenPos() { return g_ctx && g_ctx->cur ? g_ctx->cur->cur : Vec2(); }
void SetCursorScreenPos(Vec2 p) { if (g_ctx && g_ctx->cur) { g_ctx->cur->cur = Fin(p); g_ctx->cur->sameLine = false; } }
Vec2 GetContentRegionAvail() {
    if (!g_ctx || !g_ctx->cur) return Vec2();
    Window* w = g_ctx->cur;
    return Vec2(std::max(0.0f, w->rightX - w->cur.x), std::max(0.0f, w->contentRect.max.y - w->cur.y));
}
Vec2 CalcTextSize(const char* t, const char* e) { return g_ctx ? g_ctx->font.CalcTextSize(t, e) : Vec2(); }
float GetTextLineHeight() { return g_ctx ? g_ctx->font.lineHeight : 0; }
float GetFrameHeight() { return g_ctx ? FrameH(*g_ctx) : 0; }
Rect GetItemRect() { return g_ctx && g_ctx->cur ? g_ctx->cur->lastItemRect : Rect(); }
Vec2 GetWindowPos() { return g_ctx && g_ctx->cur ? g_ctx->cur->pos : Vec2(); }
Vec2 GetWindowSize() { return g_ctx && g_ctx->cur ? g_ctx->cur->size : Vec2(); }
DrawList* GetWindowDrawList() { return g_ctx && g_ctx->cur ? g_ctx->cur->dl : nullptr; }

// ---------------------------------------------------------------------------------------------
// IDs and style stacks
// ---------------------------------------------------------------------------------------------
void PushID(const char* s) { if (g_ctx && g_ctx->cur) PushIdValue(g_ctx->cur, GetID(s ? s : "")); }
void PushID(int i) {
    if (g_ctx && g_ctx->cur) PushIdValue(g_ctx->cur, HashBytes(&i, sizeof i, g_ctx->cur->idStack.empty() ? 0 : g_ctx->cur->idStack.back()));
}
void PopID() {
    if (g_ctx && !g_ctx->cur) { ++g_ctx->errors; return; }
    if (!g_ctx) return;
    if (g_ctx->cur->idStack.size() <= 1) { ++g_ctx->errors; return; }
    g_ctx->cur->idStack.pop_back();
}
std::uint32_t GetID(const char* s) {
    const std::uint32_t seed = (g_ctx && g_ctx->cur && !g_ctx->cur->idStack.empty()) ? g_ctx->cur->idStack.back() : 0;
    return HashString(s, seed);
}
std::uint32_t GetWindowID(const char* name) { return HashString(name, 0); }
void PushStyleColor(ColIdx idx, Col c) {
    if (!g_ctx || idx < 0 || idx >= Col_Count) return;
    g_ctx->colorStack.push_back({(int)idx, g_ctx->style.colors[idx]});
    g_ctx->style.colors[idx] = c;
}
void PopStyleColor(int count) {
    if (!g_ctx) return;
    while (count-- > 0) {
        if (g_ctx->colorStack.empty()) { ++g_ctx->errors; return; }
        g_ctx->style.colors[g_ctx->colorStack.back().first] = g_ctx->colorStack.back().second;
        g_ctx->colorStack.pop_back();
    }
}
Col GetColor(ColIdx idx) { return g_ctx && idx >= 0 && idx < Col_Count ? g_ctx->style.colors[idx] : 0; }

// ---------------------------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------------------------
void TextUnformatted(const char* text, const char* end) {
    if (!g_ctx || !g_ctx->cur || g_ctx->cur->skipItems || !text) return;
    Context& c = *g_ctx; Window* w = c.cur;
    const Vec2 ts = c.font.CalcTextSize(text, end);
    const float al = (w->sameLine || w->alignFresh) ? w->lineAlign : 0.0f;
    Rect bb = Place(c, w, Vec2(ts.x, ts.y + 2 * al));
    w->dl->AddText(Vec2(bb.min.x, bb.min.y + al), c.style.colors[Col_Text], text, end);
    SetLast(w, 0, bb, false, false);
}
void Text(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::string s = VFormat(fmt, ap);
    va_end(ap);
    TextUnformatted(s.c_str(), s.c_str() + s.size());
}
void TextColored(Col col, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::string s = VFormat(fmt, ap);
    va_end(ap);
    PushStyleColor(Col_Text, col);
    TextUnformatted(s.c_str(), s.c_str() + s.size());
    PopStyleColor();
}
void TextDisabled(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::string s = VFormat(fmt, ap);
    va_end(ap);
    PushStyleColor(Col_Text, GetColor(Col_TextDisabled));
    TextUnformatted(s.c_str(), s.c_str() + s.size());
    PopStyleColor();
}

static bool ButtonImpl(const char* label, Vec2 size, bool small) {
    if (!g_ctx || !g_ctx->cur || g_ctx->cur->skipItems || !label) return false;
    Context& c = *g_ctx; Window* w = c.cur;
    const std::uint32_t id = GetID(label);
    const char* le = LabelEnd(label);
    const Vec2 ts = c.font.CalcTextSize(label, le);
    const Vec2 fp(c.style.framePadding.x, small ? 0.0f : c.style.framePadding.y);
    const Vec2 sz(size.x > 0 ? size.x : ts.x + 2 * fp.x, size.y > 0 ? size.y : ts.y + 2 * fp.y);
    Rect bb = Place(c, w, sz);
    Beh b = ButtonBehavior(c, w, bb, id);
    const Col col = c.style.colors[(b.held && b.hovered) ? Col_ButtonActive : b.hovered ? Col_ButtonHovered : Col_Button];
    w->dl->AddRectFilled(bb.min, bb.max, col, c.style.frameRounding);
    w->dl->PushClipRect(bb);
    w->dl->AddText(Vec2(bb.min.x + (bb.Width() - ts.x) * 0.5f, bb.min.y + (bb.Height() - ts.y) * 0.5f), c.style.colors[Col_Text], label, le);
    w->dl->PopClipRect();
    SetLast(w, id, bb, b.hovered, b.hovered && Clicked(c));
    return b.pressed;
}
bool Button(const char* label, Vec2 size) { return ButtonImpl(label, size, false); }
bool SmallButton(const char* label) { return ButtonImpl(label, Vec2(0, 0), true); }

bool Checkbox(const char* label, bool* v) {
    if (!g_ctx || !g_ctx->cur || g_ctx->cur->skipItems || !label || !v) return false;
    Context& c = *g_ctx; Window* w = c.cur;
    const std::uint32_t id = GetID(label);
    const char* le = LabelEnd(label);
    const Vec2 ts = c.font.CalcTextSize(label, le);
    const float box = FrameH(c);
    Rect bb = Place(c, w, Vec2(box + (ts.x > 0 ? c.style.itemInnerSpacing + ts.x : 0), box));
    Beh b = ButtonBehavior(c, w, bb, id);
    if (b.pressed) *v = !*v;
    const Rect br(bb.min, Vec2(bb.min.x + box, bb.min.y + box));
    w->dl->AddRectFilled(br.min, br.max, c.style.colors[(b.held && b.hovered) ? Col_FrameBgActive : b.hovered ? Col_FrameBgHovered : Col_FrameBg], c.style.frameRounding);
    if (*v) {
        const float p = box * 0.25f;
        const Col cc = c.style.colors[Col_CheckMark];
        w->dl->AddLine(Vec2(br.min.x + p, br.min.y + box * 0.5f), Vec2(br.min.x + box * 0.43f, br.max.y - p), cc, 2);
        w->dl->AddLine(Vec2(br.min.x + box * 0.43f, br.max.y - p), Vec2(br.max.x - p, br.min.y + p), cc, 2);
    }
    if (ts.x > 0) w->dl->AddText(Vec2(br.max.x + c.style.itemInnerSpacing, bb.min.y + c.style.framePadding.y), c.style.colors[Col_Text], label, le);
    SetLast(w, id, bb, b.hovered, b.hovered && Clicked(c));
    return b.pressed;
}

namespace {
// Shared by sliders and drags: draws the frame and the label, returns the frame rect and behaviour.
struct NumBox { Rect frame; Beh b; std::uint32_t id = 0; bool ok = false; };
NumBox NumberFrame(Context& c, Window* w, const char* label) {
    NumBox n;
    n.id = GetID(label);
    const char* le = LabelEnd(label);
    const Vec2 ts = c.font.CalcTextSize(label, le);
    const float width = ItemWidth(c, w), h = FrameH(c);
    Rect bb = Place(c, w, Vec2(width + (ts.x > 0 ? c.style.itemInnerSpacing + ts.x : 0), h));
    n.frame = Rect(bb.min, Vec2(bb.min.x + width, bb.max.y));
    n.b = ButtonBehavior(c, w, n.frame, n.id);
    w->dl->AddRectFilled(n.frame.min, n.frame.max, c.style.colors[(n.b.held) ? Col_FrameBgActive : n.b.hovered ? Col_FrameBgHovered : Col_FrameBg], c.style.frameRounding);
    if (ts.x > 0) w->dl->AddText(Vec2(n.frame.max.x + c.style.itemInnerSpacing, bb.min.y + c.style.framePadding.y), c.style.colors[Col_Text], label, le);
    SetLast(w, n.id, bb, n.b.hovered, n.b.hovered && Clicked(c));
    n.ok = true;
    return n;
}
void CenterText(Context& c, Window* w, const Rect& r, const std::string& s) {
    const Vec2 ts = c.font.CalcTextSize(s.c_str(), s.c_str() + s.size());
    w->dl->PushClipRect(r);
    w->dl->AddText(Vec2(r.min.x + (r.Width() - ts.x) * 0.5f, r.min.y + (r.Height() - ts.y) * 0.5f), c.style.colors[Col_Text], s.c_str(), s.c_str() + s.size());
    w->dl->PopClipRect();
}
std::string FormatNumber(const char* fmt, double v, bool isInt) {
    // Accept exactly one conversion of the right kind, with flags / width / precision only (no length
    // modifiers, no %n or %s). Anything else falls back to a safe default, so a bad format cannot misread
    // the argument.
    const char* fallback = isInt ? "%d" : "%.3f";
    int convs = 0; bool ok = true;
    for (const char* p = fmt ? fmt : ""; *p && ok; ++p) {
        if (*p != '%') continue;
        ++p;
        if (*p == '%') continue;
        ++convs;
        while (*p && std::strchr("-+ #0123456789.", *p)) ++p;
        if (isInt ? !(*p == 'd' || *p == 'i') : !(*p == 'f' || *p == 'F' || *p == 'e' || *p == 'E' || *p == 'g' || *p == 'G')) ok = false;
        if (!*p) break;
    }
    if (!ok || convs != 1) fmt = fallback;
    char buf[96];
    if (isInt) std::snprintf(buf, sizeof buf, fmt, (int)v);
    else std::snprintf(buf, sizeof buf, fmt, v);
    return buf;
}
} // namespace

bool SliderFloat(const char* label, float* v, float lo, float hi, const char* fmt) {
    if (!g_ctx || !g_ctx->cur || g_ctx->cur->skipItems || !label || !v) return false;
    Context& c = *g_ctx; Window* w = c.cur;
    NumBox n = NumberFrame(c, w, label);
    bool changed = false;
    if (!std::isfinite(lo) || !std::isfinite(hi) || hi <= lo) { CenterText(c, w, n.frame, "?"); return false; }
    const float grabW = std::max(c.style.grabMinSize, 12.0f);
    const float travel = std::max(1.0f, n.frame.Width() - grabW);
    if (n.b.held) {
        const float t = Clamp((c.in.mousePos.x - (n.frame.min.x + grabW * 0.5f)) / travel, 0, 1);
        const float nv = lo + t * (hi - lo);
        if (nv != *v) { *v = nv; changed = true; }
    }
    if (!std::isfinite(*v)) *v = lo;
    const float t = Clamp((*v - lo) / (hi - lo), 0, 1);
    const float gx = n.frame.min.x + t * travel;
    w->dl->AddRectFilled(Vec2(gx + 1, n.frame.min.y + 2), Vec2(gx + grabW - 1, n.frame.max.y - 2), c.style.colors[Col_SliderGrab], c.style.frameRounding);
    CenterText(c, w, n.frame, FormatNumber(fmt, *v, false));
    return changed;
}
bool SliderInt(const char* label, int* v, int lo, int hi, const char* fmt) {
    if (!v) return false;
    float f = (float)*v;
    // Reuse the float slider with integer snapping applied through the widget's own value.
    Context* cp = g_ctx;
    if (!cp || !cp->cur || cp->cur->skipItems || !label) return false;
    Context& c = *cp; Window* w = c.cur;
    NumBox n = NumberFrame(c, w, label);
    bool changed = false;
    if (hi <= lo) { CenterText(c, w, n.frame, "?"); return false; }
    const float grabW = std::max(c.style.grabMinSize, 12.0f);
    const float travel = std::max(1.0f, n.frame.Width() - grabW);
    if (n.b.held) {
        const float t = Clamp((c.in.mousePos.x - (n.frame.min.x + grabW * 0.5f)) / travel, 0, 1);
        const int nv = (int)std::lround((double)lo + (double)t * ((double)hi - lo));
        if (nv != *v) { *v = nv; changed = true; }
    }
    f = (float)Clamp((float)*v, (float)lo, (float)hi);
    const float t = (f - lo) / (float)(hi - lo);
    const float gx = n.frame.min.x + t * travel;
    w->dl->AddRectFilled(Vec2(gx + 1, n.frame.min.y + 2), Vec2(gx + grabW - 1, n.frame.max.y - 2), c.style.colors[Col_SliderGrab], c.style.frameRounding);
    CenterText(c, w, n.frame, FormatNumber(fmt, *v, true));
    return changed;
}
bool DragFloat(const char* label, float* v, float speed, float lo, float hi, const char* fmt) {
    if (!g_ctx || !g_ctx->cur || g_ctx->cur->skipItems || !label || !v) return false;
    Context& c = *g_ctx; Window* w = c.cur;
    NumBox n = NumberFrame(c, w, label);
    bool changed = false;
    if (n.b.held && !n.b.started && std::isfinite(speed)) {
        float nv = *v + c.mouseDelta.x * speed;
        if (hi > lo) nv = Clamp(nv, lo, hi);
        if (nv != *v && std::isfinite(nv)) { *v = nv; changed = true; }
    }
    CenterText(c, w, n.frame, FormatNumber(fmt, *v, false));
    return changed;
}

// ---- InputText -------------------------------------------------------------------------------
namespace {
int SnapBoundary(const std::string& s, int i) {
    if (i < 0) i = 0;
    if (i > (int)s.size()) i = (int)s.size();
    while (i > 0 && i < (int)s.size() && (((unsigned char)s[(std::size_t)i]) & 0xC0) == 0x80) --i;
    return i;
}
int PrevBoundary(const std::string& s, int i) {
    if (i <= 0) return 0;
    --i;
    while (i > 0 && (((unsigned char)s[(std::size_t)i]) & 0xC0) == 0x80) --i;
    return i;
}
int NextBoundary(const std::string& s, int i) {
    const int n = (int)s.size();
    if (i >= n) return n;
    ++i;
    while (i < n && (((unsigned char)s[(std::size_t)i]) & 0xC0) == 0x80) ++i;
    return i;
}
int CursorFromX(Context& c, const std::string& s, float x) {
    float acc = 0;
    int i = 0; const int n = (int)s.size();
    while (i < n) {
        std::uint32_t cp; int len = Utf8Decode(s.data() + i, s.data() + n, &cp);
        if (len <= 0) break;
        const float adv = cp == '\n' ? 0.0f : c.font.GetGlyph(cp).advance;
        if (x < acc + adv * 0.5f) return i;
        acc += adv; i += len;
    }
    return n;
}
} // namespace

bool InputText(const char* label, std::string* buf, int flags, int maxBytes) {
    if (!g_ctx || !g_ctx->cur || g_ctx->cur->skipItems || !label || !buf) return false;
    Context& c = *g_ctx; Window* w = c.cur;
    const std::uint32_t id = GetID(label);
    const char* le = LabelEnd(label);
    const Vec2 ts = c.font.CalcTextSize(label, le);
    const float width = ItemWidth(c, w), h = FrameH(c);
    Rect bb = Place(c, w, Vec2(width + (ts.x > 0 ? c.style.itemInnerSpacing + ts.x : 0), h));
    const Rect fr(bb.min, Vec2(bb.min.x + width, bb.max.y));
    const Vec2 fp = c.style.framePadding;
    std::string& s = *buf;
    InputTextState& st = c.inputState[id];
    const bool hovered = HoverTest(c, w, fr) && (c.activeId == 0 || c.activeId == id);
    if (hovered) c.hoveredId = id;
    const bool wasFocused = c.focusId == id;
    bool justClicked = false;
    if (hovered && Clicked(c) && c.activeId == 0) {
        c.activeId = id; c.focusId = id; justClicked = true;
        const int idx = CursorFromX(c, s, c.in.mousePos.x - (fr.min.x + fp.x) + st.scrollX);
        if (!wasFocused && (flags & Input_AutoSelectAll)) { st.anchor = 0; st.cursor = (int)s.size(); }
        else { st.cursor = idx; if (!(c.in.shift && wasFocused)) st.anchor = idx; }
    }
    if (c.activeId == id && !justClicked) {
        if (c.in.mouseDown[0]) st.cursor = CursorFromX(c, s, c.in.mousePos.x - (fr.min.x + fp.x) + st.scrollX);
        else c.activeId = 0;
    }
    bool changed = false, entered = false;
    const bool focused = c.focusId == id;
    if (focused) {
        st.cursor = SnapBoundary(s, st.cursor); st.anchor = SnapBoundary(s, st.anchor);
        const bool ro = (flags & Input_ReadOnly) != 0;
        auto deleteSel = [&]() {
            const int a = std::min(st.cursor, st.anchor), b = std::max(st.cursor, st.anchor);
            if (a == b) return false;
            s.erase((std::size_t)a, (std::size_t)(b - a)); st.cursor = st.anchor = a; return true;
        };
        if (c.in.ctrl && c.keyPressed[Key_A]) { st.anchor = 0; st.cursor = (int)s.size(); }
        if (!ro && !c.in.ctrl && !c.in.text.empty()) {
            const char* p = c.in.text.data(); const char* e = p + c.in.text.size();
            while (p < e) {
                std::uint32_t cp; int len = Utf8Decode(p, e, &cp);
                if (len <= 0) break;
                const char* q = p; p += len;
                if (cp < 32 || cp == 127 || cp == 0xFFFD) continue;
                std::string ins(q, (std::size_t)len);
                const int a = std::min(st.cursor, st.anchor), b = std::max(st.cursor, st.anchor);
                if ((int)s.size() - (b - a) + len > maxBytes) continue;
                if (deleteSel()) changed = true;
                s.insert((std::size_t)st.cursor, ins); st.cursor += len; st.anchor = st.cursor; changed = true;
            }
        }
        auto moveTo = [&](int pos) { st.cursor = pos; if (!c.in.shift) st.anchor = pos; };
        if (c.keyPressed[Key_Left]) { if (!c.in.shift && st.cursor != st.anchor) moveTo(std::min(st.cursor, st.anchor)); else moveTo(PrevBoundary(s, st.cursor)); }
        if (c.keyPressed[Key_Right]) { if (!c.in.shift && st.cursor != st.anchor) moveTo(std::max(st.cursor, st.anchor)); else moveTo(NextBoundary(s, st.cursor)); }
        if (c.keyPressed[Key_Home]) moveTo(0);
        if (c.keyPressed[Key_End]) moveTo((int)s.size());
        if (!ro && c.keyPressed[Key_Backspace]) {
            if (deleteSel()) changed = true;
            else if (st.cursor > 0) { const int p = PrevBoundary(s, st.cursor); s.erase((std::size_t)p, (std::size_t)(st.cursor - p)); st.cursor = st.anchor = p; changed = true; }
        }
        if (!ro && c.keyPressed[Key_Delete]) {
            if (deleteSel()) changed = true;
            else if (st.cursor < (int)s.size()) { const int n2 = NextBoundary(s, st.cursor); s.erase((std::size_t)st.cursor, (std::size_t)(n2 - st.cursor)); changed = true; }
        }
        if (c.keyPressed[Key_Enter]) entered = true;
        if (c.keyPressed[Key_Escape]) { c.focusId = 0; if (c.activeId == id) c.activeId = 0; }
    }
    const bool nowFocused = c.focusId == id;
    w->dl->AddRectFilled(fr.min, fr.max, c.style.colors[nowFocused ? Col_FrameBgActive : hovered ? Col_FrameBgHovered : Col_FrameBg], c.style.frameRounding);
    const float innerW = std::max(1.0f, width - 2 * fp.x);
    if (nowFocused) {
        st.cursor = SnapBoundary(s, st.cursor);
        const float cx = c.font.CalcTextSize(s.c_str(), s.c_str() + st.cursor).x;
        if (cx - st.scrollX > innerW - 2) st.scrollX = cx - innerW + 2;
        if (cx < st.scrollX) st.scrollX = cx;
        if (st.scrollX < 0) st.scrollX = 0;
    } else st.scrollX = 0;
    w->dl->PushClipRect(Rect(fr.min.x + fp.x - 1, fr.min.y, fr.max.x - fp.x + 1, fr.max.y));
    const float tx = fr.min.x + fp.x - st.scrollX;
    if (nowFocused && st.cursor != st.anchor) {
        const int a = std::min(st.cursor, st.anchor), b = std::max(st.cursor, st.anchor);
        const float x0 = c.font.CalcTextSize(s.c_str(), s.c_str() + a).x, x1 = c.font.CalcTextSize(s.c_str(), s.c_str() + b).x;
        w->dl->AddRectFilled(Vec2(tx + x0, fr.min.y + 2), Vec2(tx + x1, fr.max.y - 2), c.style.colors[Col_TextSelectedBg]);
    }
    w->dl->AddText(Vec2(tx, fr.min.y + fp.y), c.style.colors[Col_Text], s.c_str(), s.c_str() + s.size());
    if (nowFocused && (!c.style.cursorBlink || std::fmod(c.time, 1.0) < 0.5)) {
        const float cx = tx + c.font.CalcTextSize(s.c_str(), s.c_str() + st.cursor).x;
        w->dl->AddRectFilled(Vec2(cx, fr.min.y + 2), Vec2(cx + 1, fr.max.y - 2), c.style.colors[Col_Text]);
    }
    w->dl->PopClipRect();
    if (ts.x > 0) w->dl->AddText(Vec2(fr.max.x + c.style.itemInnerSpacing, bb.min.y + fp.y), c.style.colors[Col_Text], label, le);
    SetLast(w, id, bb, hovered, hovered && Clicked(c));
    return (flags & Input_EnterReturnsTrue) ? entered : changed;
}

// ---- Combo / popups --------------------------------------------------------------------------
bool BeginCombo(const char* label, const char* preview) {
    if (!g_ctx || !g_ctx->cur || g_ctx->cur->skipItems || !label) return false;
    Context& c = *g_ctx; Window* w = c.cur;
    const std::uint32_t id = GetID(label);
    const char* le = LabelEnd(label);
    const Vec2 ts = c.font.CalcTextSize(label, le);
    const float width = ItemWidth(c, w), h = FrameH(c);
    Rect bb = Place(c, w, Vec2(width + (ts.x > 0 ? c.style.itemInnerSpacing + ts.x : 0), h));
    const Rect fr(bb.min, Vec2(bb.min.x + width, bb.max.y));
    const bool hovered = HoverTest(c, w, fr) && (c.activeId == 0);
    if (hovered) c.hoveredId = id;
    const bool isOpen = c.popupId == id;
    if (hovered && Clicked(c)) {
        if (c.popupClosedId == id) { /* this click just closed it */ }
        else if (c.popupId == 0) c.popupId = id;
    }
    w->dl->AddRectFilled(fr.min, fr.max, c.style.colors[hovered ? Col_FrameBgHovered : Col_FrameBg], c.style.frameRounding);
    const Rect arrow(Vec2(fr.max.x - fr.Height(), fr.min.y), fr.max);
    w->dl->AddRectFilled(arrow.min, arrow.max, c.style.colors[hovered ? Col_ButtonHovered : Col_Button], c.style.frameRounding);
    const Vec2 ac(arrow.min.x + arrow.Width() * 0.5f, arrow.min.y + arrow.Height() * 0.5f);
    w->dl->AddTriangleFilled(Vec2(ac.x - 4, ac.y - 2), Vec2(ac.x + 4, ac.y - 2), Vec2(ac.x, ac.y + 3), c.style.colors[Col_Text]);
    w->dl->PushClipRect(Rect(fr.min, Vec2(arrow.min.x, fr.max.y)));
    if (preview) w->dl->AddText(Vec2(fr.min.x + c.style.framePadding.x, fr.min.y + c.style.framePadding.y), c.style.colors[Col_Text], preview);
    w->dl->PopClipRect();
    if (ts.x > 0) w->dl->AddText(Vec2(fr.max.x + c.style.itemInnerSpacing, bb.min.y + c.style.framePadding.y), c.style.colors[Col_Text], label, le);
    SetLast(w, id, bb, hovered, hovered && Clicked(c));
    if (!isOpen && c.popupId != id) return false;
    char name[32]; std::snprintf(name, sizeof name, "##combo%08x", (unsigned)id);
    SetNextWindowPos(Vec2(fr.min.x, fr.max.y));
    SetNextWindowSize(Vec2(width, 0));
    const bool ok = Begin(name, nullptr, Win_Popup | Win_NoTitleBar | Win_AlwaysAutoResize | Win_NoMove | Win_NoResize | Win_NoCollapse | Win_NoScrollbar);
    if (!ok) { End(); return false; }
    return true;
}
void EndCombo() { End(); }
void CloseCurrentPopup() { if (g_ctx) { g_ctx->popupId = 0; } }

bool Combo(const char* label, int* current, const char* const* items, int count) {
    if (!current || !items || count < 0) return false;
    const char* preview = (*current >= 0 && *current < count && items[*current]) ? items[*current] : "";
    bool changed = false;
    if (BeginCombo(label, preview)) {
        for (int i = 0; i < count; ++i) {
            PushID(i);
            if (Selectable(items[i] ? items[i] : "", i == *current)) { *current = i; changed = true; }
            PopID();
        }
        EndCombo();
    }
    return changed;
}

bool Selectable(const char* label, bool selected, Vec2 size) {
    if (!g_ctx || !g_ctx->cur || g_ctx->cur->skipItems || !label) return false;
    Context& c = *g_ctx; Window* w = c.cur;
    const std::uint32_t id = GetID(label);
    const char* le = LabelEnd(label);
    const Vec2 ts = c.font.CalcTextSize(label, le);
    const float avail = w->rightX - w->cur.x;
    Rect bb = Place(c, w, Vec2(size.x > 0 ? size.x : std::max(avail, ts.x), size.y > 0 ? size.y : ts.y));
    Beh b = ButtonBehavior(c, w, bb, id);
    if (b.hovered || b.held || selected)
        w->dl->AddRectFilled(bb.min, bb.max, c.style.colors[(b.held && b.hovered) ? Col_HeaderActive : b.hovered ? Col_HeaderHovered : Col_Header]);
    w->dl->PushClipRect(bb);
    w->dl->AddText(bb.min, c.style.colors[Col_Text], label, le);
    w->dl->PopClipRect();
    SetLast(w, id, bb, b.hovered, b.hovered && Clicked(c));
    if (b.pressed && (w->flags & Win_Popup)) CloseCurrentPopup();
    return b.pressed;
}

bool TreeNode(const char* label) {
    if (!g_ctx || !g_ctx->cur || g_ctx->cur->skipItems || !label) return false;
    Context& c = *g_ctx; Window* w = c.cur;
    const std::uint32_t id = GetID(label);
    const char* le = LabelEnd(label);
    const Vec2 ts = c.font.CalcTextSize(label, le);
    const float h = std::max(ts.y, GetFrameHeight() - 4);
    Rect bb = Place(c, w, Vec2(std::max(1.0f, w->rightX - w->cur.x), h));
    Beh b = ButtonBehavior(c, w, bb, id);
    bool& open = c.treeOpen[id];
    if (b.pressed) open = !open;
    if (b.hovered || b.held) w->dl->AddRectFilled(bb.min, bb.max, c.style.colors[b.held ? Col_HeaderActive : Col_HeaderHovered]);
    const Vec2 ac(bb.min.x + 8, bb.min.y + h * 0.5f);
    const Col tc = c.style.colors[Col_Text];
    if (open) w->dl->AddTriangleFilled(Vec2(ac.x - 4, ac.y - 2), Vec2(ac.x + 4, ac.y - 2), Vec2(ac.x, ac.y + 3), tc);
    else w->dl->AddTriangleFilled(Vec2(ac.x - 2, ac.y - 4), Vec2(ac.x - 2, ac.y + 4), Vec2(ac.x + 3, ac.y), tc);
    w->dl->PushClipRect(bb);
    w->dl->AddText(Vec2(bb.min.x + 20, bb.min.y + (h - ts.y) * 0.5f), tc, label, le);
    w->dl->PopClipRect();
    SetLast(w, id, bb, b.hovered, b.hovered && Clicked(c));
    if (open) { PushIdValue(w, id); Indent(); }
    return open;
}
void TreePop() {
    if (g_ctx && !g_ctx->cur) { ++g_ctx->errors; return; }
    if (!g_ctx) return;
    if (g_ctx->cur->idStack.size() <= 1) { ++g_ctx->errors; return; }
    Unindent();
    PopID();
}

// ---- queries ---------------------------------------------------------------------------------
bool IsItemHovered() { return g_ctx && g_ctx->cur && g_ctx->cur->lastHovered; }
bool IsItemActive() { return g_ctx && g_ctx->cur && g_ctx->cur->lastItemId != 0 && g_ctx->activeId == g_ctx->cur->lastItemId; }
bool IsItemClicked() { return g_ctx && g_ctx->cur && g_ctx->cur->lastClicked; }
std::uint32_t GetHoveredId() { return g_ctx ? g_ctx->hoveredId : 0; }
std::uint32_t GetActiveId() { return g_ctx ? g_ctx->activeId : 0; }
std::uint32_t GetFocusId() { return g_ctx ? g_ctx->focusId : 0; }
bool IsKeyPressed(Key k) { return g_ctx && k >= 0 && k < Key_Count && g_ctx->keyPressed[k]; }
bool IsMouseClicked(int b) { return g_ctx && b >= 0 && b < 3 && Clicked(*g_ctx, b); }
bool IsMouseDown(int b) { return g_ctx && b >= 0 && b < 3 && g_ctx->in.mouseDown[b]; }

} // namespace imui
} // namespace okay
