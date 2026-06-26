#include "okay/UI/OkayUI.hpp"
#include "okay/Graphics/Font.hpp"   // okay::Font8x8 — a no-STL C API over a bitmap font
#include <SDL.h>
#include <cstdio>   // snprintf for numeric widgets (C library, not STL)

// All rendering goes through SDL_RenderGeometry (SDL >= 2.0.18), which is exactly
// the call Dear ImGui's SDL_Renderer backend uses — so OkayUI shares ImGui's GPU
// pipeline (Direct3D 11 on Windows, Metal on macOS, OpenGL on Linux) and can never
// conflict with it. No STL is used here: fixed C arrays, C strings, plain ints.

namespace OkayUI {
namespace {

// Fixed-capacity geometry batch. Text is drawn as one small quad per lit font
// pixel, so size generously for several labelled buttons per frame; on overflow we
// silently drop further geometry rather than grow/allocate.
constexpr int kMaxVerts = 20000;
constexpr int kMaxIdx   = 30000;
SDL_Vertex g_verts[kMaxVerts];
int        g_nv = 0;
int        g_idx[kMaxIdx];
int        g_ni = 0;

// Overlay batch: popups (Combo lists) and tooltips draw here, then it's appended
// AFTER the main batch each frame so it composites on top of later widgets.
SDL_Vertex g_ovVerts[kMaxVerts];
int        g_onv = 0;
int        g_ovIdx[kMaxIdx];
int        g_oni = 0;
bool       g_toOverlay = false;   // route quad()/drawText() to the overlay batch

Input g_in;                      // this frame's input
bool  g_prevDown = false;        // last frame's mouse-down (for edge detection)
bool  g_pressed  = false;        // mouse went down this frame
bool  g_released = false;        // mouse went up this frame
int   g_hot = 0, g_active = 0;   // hovered id / pressed-and-holding id (0 = none)
int   g_focus = 0;               // keyboard-focused widget id (TextField); 0 = none
bool  g_focusClaimed = false;    // a widget took the click this frame (keep focus)
unsigned g_frame = 0;            // frame counter (drives the text caret blink)
float    g_dragAccum = 0.0f;     // sub-unit accumulator for DragInt while dragging
Theme g_theme;

// Length of a C string without pulling in <cstring> (keeps the toolkit STL/libc-light).
inline int cstrlen(const char* s) { int n = 0; if (s) while (s[n]) ++n; return n; }

// ---- Auto-layout state (ImGui-style window + cursor) ---------------------------
struct LayoutState {
    bool  active   = false;
    float ox = 0, oy = 0;            // content origin (top-left of the content area)
    float cx = 0, cy = 0;            // cursor: top-left where the next item goes
    float contentW = 0;             // usable content width
    float spacingX = 8.0f, spacingY = 6.0f;
    float prevX = 0, prevY = 0, prevW = 0, prevH = 0;   // last placed item
    bool  pendingSameLine = false;
    float sameLineSpacing = -1.0f;
    float indent = 0.0f;            // TreeNode indentation of the content's left edge
    int   seed = 0;                 // window id, mixed into label->id hashing
};
LayoutState g_lay;

// Draggable-window position store (no STL): position persists across frames per id.
struct WinSlot { int id = 0; float x = 0, y = 0; bool used = false; };
WinSlot g_wins[16];

// Per-widget open/closed state (CollapsingHeader, Combo), persisted by id.
struct OpenSlot { int id = 0; bool open = false; bool used = false; };
OpenSlot g_opens[64];
bool* openState(int id, bool dflt) {
    for (OpenSlot& s : g_opens) if (s.used && s.id == id) return &s.open;
    for (OpenSlot& s : g_opens) if (!s.used) { s.used = true; s.id = id; s.open = dflt; return &s.open; }
    static bool sink = false; return &sink;   // table full: harmless fallback
}
float g_prevMouseX = 0.0f, g_prevMouseY = 0.0f;   // last frame's cursor
float g_mouseDX = 0.0f, g_mouseDY = 0.0f;         // cursor delta this frame

// FNV-1a hash of a label -> stable nonzero widget id (so callers needn't pass ids).
inline int hashLabel(const char* s) {
    unsigned h = 2166136261u ^ (unsigned)g_lay.seed;
    if (s) for (; *s; ++s) h = (h ^ (unsigned char)*s) * 16777619u;
    int id = (int)(h & 0x7fffffffu);
    return id ? id : 1;
}
inline float rowH() { return okay::Font8x8::Height * g_theme.textScale + 12.0f; }
inline float textH() { return okay::Font8x8::Height * g_theme.textScale; }
inline float labelW(const char* s) { return s && *s ? okay::Font8x8::MeasureWidth(s) * g_theme.textScale : 0.0f; }

// The indented content left edge and full indented content width.
inline float leftEdge() { return g_lay.ox + g_lay.indent; }
inline float fullW()    { return g_lay.contentW - g_lay.indent; }
// Where the next item's left edge will land (honors a pending SameLine).
inline float cursorX() {
    if (g_lay.pendingSameLine) {
        float sp = g_lay.sameLineSpacing >= 0.0f ? g_lay.sameLineSpacing : g_lay.spacingX;
        return g_lay.prevX + g_lay.prevW + sp;
    }
    return leftEdge();
}
// Remaining width from the next item's left edge to the content's right edge.
inline float availW() { return g_lay.ox + g_lay.contentW - cursorX(); }

// Reserve a w*h slot for the next item; returns its top-left and advances the cursor
// to the next line (SameLine() keeps it on the current line instead).
void place(float w, float h, float& x, float& y) {
    if (g_lay.pendingSameLine) {
        float sp = g_lay.sameLineSpacing >= 0.0f ? g_lay.sameLineSpacing : g_lay.spacingX;
        x = g_lay.prevX + g_lay.prevW + sp;
        y = g_lay.prevY;
        g_lay.pendingSameLine = false;
        g_lay.sameLineSpacing = -1.0f;
    } else {
        x = leftEdge();
        y = g_lay.cy;
    }
    g_lay.prevX = x; g_lay.prevY = y; g_lay.prevW = w; g_lay.prevH = h;
    g_lay.cx = leftEdge();
    g_lay.cy = y + h + g_lay.spacingY;
}

inline SDL_Color toColor(const unsigned char c[4]) {
    SDL_Color sc; sc.r = c[0]; sc.g = c[1]; sc.b = c[2]; sc.a = c[3]; return sc;
}

// Append an axis-aligned rectangle (two triangles) in a flat color, to whichever
// batch is active (main, or overlay when g_toOverlay is set).
void quad(float x, float y, float w, float h, const unsigned char c[4]) {
    SDL_Vertex* V = g_toOverlay ? g_ovVerts : g_verts;
    int*        I = g_toOverlay ? g_ovIdx   : g_idx;
    int&        nv = g_toOverlay ? g_onv : g_nv;
    int&        ni = g_toOverlay ? g_oni : g_ni;
    if (nv + 4 > kMaxVerts || ni + 6 > kMaxIdx) return;   // overflow: drop silently
    const SDL_Color sc = toColor(c);
    const int base = nv;
    SDL_FPoint uv; uv.x = 0.0f; uv.y = 0.0f;
    SDL_Vertex v;
    v.color = sc; v.tex_coord = uv;
    v.position.x = x;     v.position.y = y;     V[nv++] = v;
    v.position.x = x + w; v.position.y = y;     V[nv++] = v;
    v.position.x = x + w; v.position.y = y + h; V[nv++] = v;
    v.position.x = x;     v.position.y = y + h; V[nv++] = v;
    I[ni++] = base + 0; I[ni++] = base + 1; I[ni++] = base + 2;
    I[ni++] = base + 0; I[ni++] = base + 2; I[ni++] = base + 3;
}

// Draw a C string from the 8x8 bitmap font, each lit pixel a `s`x`s` quad.
void drawText(float x, float y, const char* str, float s, const unsigned char c[4]) {
    if (!str) return;
    float penX = x;
    for (const char* p = str; *p; ++p) {
        for (int gy = 0; gy < okay::Font8x8::Height; ++gy)
            for (int gx = 0; gx < okay::Font8x8::Width; ++gx)
                if (okay::Font8x8::Pixel(*p, gx, gy))
                    quad(penX + gx * s, y + gy * s, s, s, c);
        penX += okay::Font8x8::Width * s;
    }
}

inline bool pointIn(float px, float py, float x, float y, float w, float h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

} // namespace

void BeginFrame(const Input& in) {
    g_in       = in;
    g_pressed  =  in.mouseDown && !g_prevDown;
    g_released = !in.mouseDown &&  g_prevDown;
    g_prevDown = in.mouseDown;
    g_mouseDX = in.mouseX - g_prevMouseX;
    g_mouseDY = in.mouseY - g_prevMouseY;
    g_prevMouseX = in.mouseX;
    g_prevMouseY = in.mouseY;
    g_hot = 0;
    g_focusClaimed = false;
    g_lay.active = false;            // widgets called outside Begin/End no-op
    g_lay.pendingSameLine = false;
    ++g_frame;
    g_nv = 0; g_ni = 0;
    g_onv = 0; g_oni = 0; g_toOverlay = false;
}

bool Button(int id, float x, float y, float w, float h, const char* label) {
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
    if (inside) g_hot = id;

    // Hot/active (Casey-style) interaction: arm on press-inside, fire on
    // release-inside. A release outside cancels without firing.
    bool clicked = false;
    if (g_active == id) {
        if (g_released) { if (inside) clicked = true; g_active = 0; }
    } else if (inside && g_pressed) {
        g_active = id;
    }

    const unsigned char* bg = g_theme.bg;
    if (g_active == id && inside) bg = g_theme.bgDown;
    else if (inside)             bg = g_theme.bgHover;

    // Border frame, then the inset fill.
    quad(x, y, w, h, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) quad(x + b, y + b, w - 2.0f * b, h - 2.0f * b, bg);

    // Centered label.
    if (label && *label) {
        const float s  = g_theme.textScale;
        const float tw = okay::Font8x8::MeasureWidth(label) * s;
        const float th = okay::Font8x8::Height * s;
        drawText(x + (w - tw) * 0.5f, y + (h - th) * 0.5f, label, s, g_theme.text);
    }
    return clicked;
}

void Label(float x, float y, const char* text) {
    if (text && *text) drawText(x, y, text, g_theme.textScale, g_theme.text);
}

void Panel(float x, float y, float w, float h) {
    quad(x, y, w, h, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) quad(x + b, y + b, w - 2.0f * b, h - 2.0f * b, g_theme.panel);
}

bool Checkbox(int id, float x, float y, float size, const char* label, bool* value) {
    if (!value) return false;
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, size, size);
    if (inside) g_hot = id;

    bool changed = false;
    if (g_active == id) {
        if (g_released) { if (inside) { *value = !*value; changed = true; } g_active = 0; }
    } else if (inside && g_pressed) {
        g_active = id;
    }

    const unsigned char* bg = g_theme.bg;
    if (g_active == id && inside) bg = g_theme.bgDown;
    else if (inside)             bg = g_theme.bgHover;

    // Box frame + fill.
    quad(x, y, size, size, g_theme.border);
    const float b = g_theme.borderPx;
    if (size > 2.0f * b) quad(x + b, y + b, size - 2.0f * b, size - 2.0f * b, bg);
    // Check mark: a centered accent square when ticked.
    if (*value) {
        const float pad = size * 0.28f;
        quad(x + pad, y + pad, size - 2.0f * pad, size - 2.0f * pad, g_theme.accent);
    }
    // Label to the right, vertically centered against the box.
    if (label && *label) {
        const float s  = g_theme.textScale;
        const float th = okay::Font8x8::Height * s;
        drawText(x + size + 8.0f, y + (size - th) * 0.5f, label, s, g_theme.text);
    }
    return changed;
}

bool Slider(int id, float x, float y, float w, float h, float* value, float minV, float maxV) {
    if (!value || w <= 0.0f || maxV <= minV) return false;
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
    if (inside) g_hot = id;

    bool changed = false;
    auto setFromMouse = [&]() {
        const float t  = clamp01((g_in.mouseX - x) / w);
        const float nv = minV + t * (maxV - minV);
        if (nv != *value) { *value = nv; changed = true; }
    };
    if (g_active == id) {
        if (!g_in.mouseDown) g_active = 0;   // released -> stop dragging
        else setFromMouse();                 // drag tracks the cursor
    } else if (inside && g_pressed) {
        g_active = id;
        setFromMouse();                      // jump to the click position
    }

    // Groove, then the filled portion, then the handle.
    const float t = clamp01((*value - minV) / (maxV - minV));
    const float gy = y + h * 0.5f - 3.0f;     // 6px groove, vertically centered
    quad(x, gy, w, 6.0f, g_theme.track);
    quad(x, gy, w * t, 6.0f, g_theme.accent);
    const float hw = 10.0f;                    // handle width
    float hx = x + w * t - hw * 0.5f;
    if (hx < x) hx = x; if (hx > x + w - hw) hx = x + w - hw;
    const unsigned char* hc = (g_active == id || inside) ? g_theme.text : g_theme.accent;
    quad(hx, y, hw, h, g_theme.border);
    quad(hx + 1.0f, y + 1.0f, hw - 2.0f, h - 2.0f, hc);
    return changed;
}

void ProgressBar(float x, float y, float w, float h, float t) {
    t = clamp01(t);
    quad(x, y, w, h, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) {
        quad(x + b, y + b, w - 2.0f * b, h - 2.0f * b, g_theme.track);
        const float iw = (w - 2.0f * b) * t;
        if (iw > 0.0f) quad(x + b, y + b, iw, h - 2.0f * b, g_theme.accent);
    }
}

bool RadioButton(int id, float x, float y, float size, const char* label, int* value, int option) {
    if (!value) return false;
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, size, size);
    if (inside) g_hot = id;

    bool changed = false;
    if (g_active == id) {
        if (g_released) { if (inside && *value != option) { *value = option; changed = true; } g_active = 0; }
    } else if (inside && g_pressed) {
        g_active = id;
    }

    const unsigned char* bg = g_theme.bg;
    if (g_active == id && inside) bg = g_theme.bgDown;
    else if (inside)             bg = g_theme.bgHover;

    quad(x, y, size, size, g_theme.border);
    const float b = g_theme.borderPx;
    if (size > 2.0f * b) quad(x + b, y + b, size - 2.0f * b, size - 2.0f * b, bg);
    // Selected indicator: a smaller centered accent dot (distinct from the checkbox).
    if (*value == option) {
        const float pad = size * 0.32f;
        quad(x + pad, y + pad, size - 2.0f * pad, size - 2.0f * pad, g_theme.accent);
    }
    if (label && *label) {
        const float s  = g_theme.textScale;
        const float th = okay::Font8x8::Height * s;
        drawText(x + size + 8.0f, y + (size - th) * 0.5f, label, s, g_theme.text);
    }
    return changed;
}

bool Tab(int id, float x, float y, float w, float h, const char* label, int* current, int index) {
    if (!current) return false;
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
    if (inside) g_hot = id;

    if (g_active == id) {
        if (g_released) { if (inside) *current = index; g_active = 0; }
    } else if (inside && g_pressed) {
        g_active = id;
    }

    const bool selected = (*current == index);
    const unsigned char* bg = selected ? g_theme.bg : g_theme.track;
    if (!selected && inside) bg = g_theme.bgHover;

    quad(x, y, w, h, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) quad(x + b, y + b, w - 2.0f * b, h - 2.0f * b, bg);
    if (selected) quad(x, y, w, 3.0f, g_theme.accent);   // accent strip on the active tab
    if (label && *label) {
        const float s  = g_theme.textScale;
        const float tw = okay::Font8x8::MeasureWidth(label) * s;
        const float th = okay::Font8x8::Height * s;
        drawText(x + (w - tw) * 0.5f, y + (h - th) * 0.5f, label, s, g_theme.text);
    }
    return *current == index;
}

bool TextField(int id, float x, float y, float w, float h, char* buf, int cap) {
    if (!buf || cap < 1) return false;
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
    if (inside) g_hot = id;
    if (inside && g_pressed) { g_focus = id; g_focusClaimed = true; }
    const bool focused = (g_focus == id);

    // Apply keystrokes when focused (the host only forwards text/backspace it wants
    // OkayUI to consume, e.g. when ImGui isn't capturing the keyboard).
    bool changed = false;
    if (focused) {
        if (g_in.text && g_in.text[0]) {
            int len = cstrlen(buf);
            for (const char* p = g_in.text; *p && len < cap - 1; ++p) buf[len++] = *p;
            buf[len] = '\0';
            changed = true;
        }
        if (g_in.backspace) {
            int len = cstrlen(buf);
            if (len > 0) { buf[len - 1] = '\0'; changed = true; }
        }
    }

    // Box (focused gets a subtly lighter fill + accent border hint).
    quad(x, y, w, h, focused ? g_theme.accent : g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) quad(x + b, y + b, w - 2.0f * b, h - 2.0f * b, g_theme.track);

    // Show the trailing characters that fit (so the caret stays visible) — this
    // avoids needing a scissor rect across the batched geometry.
    const float s     = g_theme.textScale;
    const float charW = okay::Font8x8::Width * s;
    const float th    = okay::Font8x8::Height * s;
    const float pad   = 6.0f;
    const float innerW = w - 2.0f * pad;
    const int   maxCh  = charW > 0.0f ? (int)(innerW / charW) : 0;
    const int   len    = cstrlen(buf);
    const char* shown  = (maxCh > 0 && len > maxCh) ? buf + (len - maxCh) : buf;
    const float tx = x + pad, ty = y + (h - th) * 0.5f;
    drawText(tx, ty, shown, s, g_theme.text);

    // Blinking caret at the end of the shown text when focused.
    if (focused && (g_frame / 30u) % 2u == 0u) {
        const int shownLen = cstrlen(shown);
        const float cx = tx + shownLen * charW;
        if (cx < x + w - 2.0f) quad(cx, ty, 2.0f, th, g_theme.text);
    }
    return changed;
}

namespace {
// Append the overlay batch after the main batch so popups/tooltips draw on top.
void mergeOverlay() {
    const int base = g_nv;
    for (int i = 0; i < g_onv && g_nv < kMaxVerts; ++i) g_verts[g_nv++] = g_ovVerts[i];
    for (int j = 0; j < g_oni && g_ni < kMaxIdx; ++j)   g_idx[g_ni++] = base + g_ovIdx[j];
    g_onv = 0; g_oni = 0;
}
// End-of-frame interaction bookkeeping, shared by every backend.
void finalizeFrame() {
    if (g_released) g_active = 0;                    // clear if the active item wasn't drawn
    if (g_pressed && !g_focusClaimed) g_focus = 0;   // click on empty space drops keyboard focus
    mergeOverlay();
}
} // namespace

DrawData GetDrawData() {
    DrawData d;
    d.vertices = g_verts; d.vertexCount = g_nv;
    d.indices = g_idx;    d.indexCount = g_ni;
    d.vertexStride = (int)sizeof(SDL_Vertex);
    return d;
}

void EndFrameData() { finalizeFrame(); }

void EndFrame(SDL_Renderer* r) {
    finalizeFrame();
    if (!r || g_nv == 0 || g_ni == 0) return;
    // Save/restore the renderer's blend mode so we never disturb ImGui or anything
    // else that draws through the same SDL_Renderer.
    SDL_BlendMode prev = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(r, &prev);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r, nullptr, g_verts, g_nv, g_idx, g_ni);
    SDL_SetRenderDrawBlendMode(r, prev);
}

Theme& Style() { return g_theme; }

// ---- Auto-layout window + ImGui-style overloads --------------------------------

bool Begin(const char* title, float x, float y, float w, float h) {
    g_lay.seed = 0;                    // hash the window id from a FIXED seed so it (and
    const int id = hashLabel(title);   // every widget id under it) is stable across frames
    WinSlot* slot = nullptr;
    for (WinSlot& ws : g_wins) if (ws.used && ws.id == id) { slot = &ws; break; }
    if (!slot) for (WinSlot& ws : g_wins) if (!ws.used) { ws.used = true; ws.id = id; ws.x = x; ws.y = y; slot = &ws; break; }
    float wx = slot ? slot->x : x, wy = slot ? slot->y : y;
    const float titleH = rowH();

    // Drag the window by its title bar (reuses the active-item mechanism).
    const bool overTitle = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, wx, wy, w, titleH);
    if (overTitle) g_hot = id;
    if (g_active == id) {
        if (!g_in.mouseDown) g_active = 0;
        else if (slot) { slot->x += g_mouseDX; slot->y += g_mouseDY; wx = slot->x; wy = slot->y; }
    } else if (overTitle && g_pressed) {
        g_active = id; g_focusClaimed = true;
    }

    // Frame: panel body + title bar + accent underline + title text.
    Panel(wx, wy, w, h);
    quad(wx, wy, w, titleH, g_theme.bgDown);
    quad(wx, wy + titleH - 2.0f, w, 2.0f, g_theme.accent);
    if (title && *title) drawText(wx + 10.0f, wy + (titleH - textH()) * 0.5f, title, g_theme.textScale, g_theme.text);

    // Establish the content layout region.
    const float pad = 10.0f;
    g_lay.active = true;
    g_lay.seed = id;
    g_lay.ox = wx + pad;
    g_lay.oy = wy + titleH + pad;
    g_lay.cx = g_lay.ox;
    g_lay.cy = g_lay.oy;
    g_lay.contentW = w - 2.0f * pad;
    g_lay.spacingX = 8.0f; g_lay.spacingY = 6.0f;
    g_lay.indent = 0.0f;
    g_lay.pendingSameLine = false;
    g_lay.prevX = g_lay.ox; g_lay.prevY = g_lay.oy; g_lay.prevW = 0.0f; g_lay.prevH = 0.0f;
    return true;
}

void End() { g_lay.active = false; }

void SameLine(float spacing) {
    if (!g_lay.active) return;
    g_lay.pendingSameLine = true;
    g_lay.sameLineSpacing = spacing;
}

void Spacing(float h) {
    if (!g_lay.active) return;
    float x, y; place(0.0f, h, x, y);
}

void Separator() {
    if (!g_lay.active) return;
    const float gap = g_lay.spacingY;
    float x, y; place(fullW(), gap, x, y);
    quad(leftEdge(), y + gap * 0.5f, fullW(), 1.0f, g_theme.border);
}

void Text(const char* s) {
    if (!g_lay.active) return;
    const float h = rowH();
    float x, y; place(s && *s ? labelW(s) : 1.0f, h, x, y);
    if (s && *s) drawText(x, y + (h - textH()) * 0.5f, s, g_theme.textScale, g_theme.text);
}

bool Button(const char* label) {
    if (!g_lay.active) return false;
    const float w = labelW(label) + 24.0f, h = rowH();
    float x, y; place(w, h, x, y);
    return Button(hashLabel(label), x, y, w, h, label);
}

bool Checkbox(const char* label, bool* value) {
    if (!g_lay.active || !value) return false;
    const float h = rowH(), sz = textH() + 6.0f;
    const float w = sz + 8.0f + labelW(label);
    float x, y; place(w, h, x, y);
    return Checkbox(hashLabel(label), x, y + (h - sz) * 0.5f, sz, label, value);
}

bool RadioButton(const char* label, int* value, int option) {
    if (!g_lay.active || !value) return false;
    const float h = rowH(), sz = textH() + 6.0f;
    const float w = sz + 8.0f + labelW(label);
    float x, y; place(w, h, x, y);
    return RadioButton(hashLabel(label), x, y + (h - sz) * 0.5f, sz, label, value, option);
}

bool SliderFloat(const char* label, float* value, float minV, float maxV) {
    if (!g_lay.active || !value) return false;
    const float h = rowH();
    const float lw = labelW(label) > 0.0f ? labelW(label) + 8.0f : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    float ctrlW = w - lw; if (ctrlW < 24.0f) ctrlW = 24.0f;
    const bool ch = Slider(hashLabel(label), x, y, ctrlW, h, value, minV, maxV);
    if (lw > 0.0f) drawText(x + ctrlW + 8.0f, y + (h - textH()) * 0.5f, label, g_theme.textScale, g_theme.text);
    return ch;
}

void ProgressBar(float fraction, const char* overlay) {
    if (!g_lay.active) return;
    const float h = rowH(), w = availW();
    float x, y; place(w, h, x, y);
    ProgressBar(x, y, w, h, fraction);
    if (overlay && *overlay) {
        const float tw = labelW(overlay);
        drawText(x + (w - tw) * 0.5f, y + (h - textH()) * 0.5f, overlay, g_theme.textScale, g_theme.text);
    }
}

bool InputText(const char* label, char* buf, int cap) {
    if (!g_lay.active || !buf) return false;
    const float h = rowH();
    const float lw = labelW(label) > 0.0f ? labelW(label) + 8.0f : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    float ctrlW = w - lw; if (ctrlW < 24.0f) ctrlW = 24.0f;
    const bool ch = TextField(hashLabel(label), x, y, ctrlW, h, buf, cap);
    if (lw > 0.0f) drawText(x + ctrlW + 8.0f, y + (h - textH()) * 0.5f, label, g_theme.textScale, g_theme.text);
    return ch;
}

bool CollapsingHeader(const char* label) {
    if (!g_lay.active) return false;
    const int id = hashLabel(label);
    bool* open = openState(id, false);
    const float h = rowH(), w = fullW();
    float x, y; place(w, h, x, y);
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
    if (inside) g_hot = id;
    if (g_active == id) { if (g_released) { if (inside) *open = !*open; g_active = 0; } }
    else if (inside && g_pressed) g_active = id;
    const unsigned char* bg = (g_active == id && inside) ? g_theme.bgDown : (inside ? g_theme.bgHover : g_theme.track);
    quad(x, y, w, h, bg);
    const float s = g_theme.textScale;
    drawText(x + 8.0f, y + (h - textH()) * 0.5f, *open ? "-" : "+", s, g_theme.text);   // expander glyph
    drawText(x + 8.0f + okay::Font8x8::Width * s * 2.0f, y + (h - textH()) * 0.5f, label, s, g_theme.text);
    return *open;
}

void Tooltip(const char* text) {
    if (!g_lay.active || !text || !*text || g_in.blocked) return;
    // Only when the most recent item is hovered.
    if (!pointIn(g_in.mouseX, g_in.mouseY, g_lay.prevX, g_lay.prevY, g_lay.prevW, g_lay.prevH)) return;
    const float s = g_theme.textScale, pad = 6.0f;
    const float tw = labelW(text), th = textH();
    const float bx = g_in.mouseX + 14.0f, by = g_in.mouseY + 14.0f;
    g_toOverlay = true;
    quad(bx, by, tw + pad * 2.0f, th + pad * 2.0f, g_theme.border);
    quad(bx + 1.0f, by + 1.0f, tw + pad * 2.0f - 2.0f, th + pad * 2.0f - 2.0f, g_theme.panel);
    drawText(bx + pad, by + pad, text, s, g_theme.text);
    g_toOverlay = false;
}

bool Combo(const char* label, const char* const* items, int count, int* current) {
    if (!g_lay.active || !items || !current || count <= 0) return false;
    const int id = hashLabel(label);
    bool* open = openState(id, false);
    const float h = rowH(), s = g_theme.textScale, b = g_theme.borderPx;
    const float lw = labelW(label) > 0.0f ? labelW(label) + 8.0f : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    float boxW = w - lw; if (boxW < 48.0f) boxW = 48.0f;

    const bool insideBox = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, boxW, h);
    if (insideBox) g_hot = id;
    if (g_active == id) { if (g_released) { if (insideBox) *open = !*open; g_active = 0; } }
    else if (insideBox && g_pressed) g_active = id;

    quad(x, y, boxW, h, g_theme.border);
    if (boxW > 2*b && h > 2*b) quad(x + b, y + b, boxW - 2*b, h - 2*b, insideBox ? g_theme.bgHover : g_theme.bg);
    const char* cur = (*current >= 0 && *current < count) ? items[*current] : "";
    drawText(x + 6.0f, y + (h - textH()) * 0.5f, cur, s, g_theme.text);
    drawText(x + boxW - okay::Font8x8::Width * s - 6.0f, y + (h - textH()) * 0.5f, *open ? "-" : "+", s, g_theme.text);
    if (lw > 0.0f) drawText(x + boxW + 8.0f, y + (h - textH()) * 0.5f, label, s, g_theme.text);

    bool changed = false;
    if (*open) {
        const float listY = y + h;
        g_toOverlay = true;   // the dropdown list draws on top of later widgets
        for (int k = 0; k < count; ++k) {
            const float ry = listY + (float)k * h;
            const int iid = id ^ (int)(0x9e3779b9u * (unsigned)(k + 1));
            const bool ih = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, ry, boxW, h);
            if (ih) g_hot = iid;
            if (g_active == iid) { if (g_released) { if (ih) { *current = k; changed = true; *open = false; } g_active = 0; } }
            else if (ih && g_pressed) g_active = iid;
            quad(x, ry, boxW, h, ih ? g_theme.bgHover : g_theme.panel);
            drawText(x + 6.0f, ry + (h - textH()) * 0.5f, items[k], s, g_theme.text);
        }
        g_toOverlay = false;
        // A press outside both the box and the list closes the dropdown.
        const bool inList = pointIn(g_in.mouseX, g_in.mouseY, x, listY, boxW, (float)count * h);
        if (g_pressed && !insideBox && !inList) *open = false;
    }
    return changed;
}

// Shared body for the drag widgets: returns the pixels dragged this frame (0 unless
// this id is the active drag). Draws the box; the caller formats/places the value.
namespace {
bool dragBox(int id, float x, float y, float w, float h, bool inside, float& outDelta) {
    outDelta = 0.0f;
    if (inside) g_hot = id;
    bool dragging = false;
    if (g_active == id) {
        if (!g_in.mouseDown) g_active = 0;
        else { outDelta = g_mouseDX; dragging = true; }
    } else if (inside && g_pressed) {
        g_active = id; g_dragAccum = 0.0f;
    }
    const float b = g_theme.borderPx;
    quad(x, y, w, h, g_theme.border);
    if (w > 2*b && h > 2*b) quad(x + b, y + b, w - 2*b, h - 2*b, (g_active == id || inside) ? g_theme.bgHover : g_theme.track);
    return dragging;
}
} // namespace

bool DragFloat(const char* label, float* v, float speed, float minV, float maxV) {
    if (!g_lay.active || !v) return false;
    const int id = hashLabel(label);
    const float h = rowH(), s = g_theme.textScale;
    const float lw = labelW(label) > 0.0f ? labelW(label) + 8.0f : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    float ctrlW = w - lw; if (ctrlW < 32.0f) ctrlW = 32.0f;
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, ctrlW, h);
    float delta; dragBox(id, x, y, ctrlW, h, inside, delta);
    bool changed = false;
    if (delta != 0.0f) {
        float nv = *v + delta * speed;
        if (minV < maxV) nv = nv < minV ? minV : (nv > maxV ? maxV : nv);
        if (nv != *v) { *v = nv; changed = true; }
    }
    char buf[32]; std::snprintf(buf, sizeof(buf), "%.3f", *v);
    drawText(x + 6.0f, y + (h - textH()) * 0.5f, buf, s, g_theme.text);
    if (lw > 0.0f) drawText(x + ctrlW + 8.0f, y + (h - textH()) * 0.5f, label, s, g_theme.text);
    return changed;
}

bool DragInt(const char* label, int* v, float speed, int minV, int maxV) {
    if (!g_lay.active || !v) return false;
    const int id = hashLabel(label);
    const float h = rowH(), s = g_theme.textScale;
    const float lw = labelW(label) > 0.0f ? labelW(label) + 8.0f : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    float ctrlW = w - lw; if (ctrlW < 32.0f) ctrlW = 32.0f;
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, ctrlW, h);
    float delta; dragBox(id, x, y, ctrlW, h, inside, delta);
    bool changed = false;
    if (delta != 0.0f) {
        g_dragAccum += delta * speed;          // accumulate sub-unit motion
        int step = (int)g_dragAccum;
        if (step != 0) {
            g_dragAccum -= (float)step;
            int nv = *v + step;
            if (minV < maxV) nv = nv < minV ? minV : (nv > maxV ? maxV : nv);
            if (nv != *v) { *v = nv; changed = true; }
        }
    }
    char buf[32]; std::snprintf(buf, sizeof(buf), "%d", *v);
    drawText(x + 6.0f, y + (h - textH()) * 0.5f, buf, s, g_theme.text);
    if (lw > 0.0f) drawText(x + ctrlW + 8.0f, y + (h - textH()) * 0.5f, label, s, g_theme.text);
    return changed;
}

bool ColorEdit3(const char* label, float rgb[3]) {
    if (!g_lay.active || !rgb) return false;
    const int id = hashLabel(label);
    const float h = rowH(), s = g_theme.textScale, b = g_theme.borderPx;
    const float lw = labelW(label) > 0.0f ? labelW(label) + 8.0f : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    const float row = w - lw;
    const float swatch = h;
    // Swatch preview.
    unsigned char col[4] = {
        (unsigned char)(clamp01(rgb[0]) * 255.0f), (unsigned char)(clamp01(rgb[1]) * 255.0f),
        (unsigned char)(clamp01(rgb[2]) * 255.0f), 255};
    quad(x, y, swatch, h, g_theme.border);
    if (swatch > 2*b && h > 2*b) quad(x + b, y + b, swatch - 2*b, h - 2*b, col);
    // Three channel sliders.
    float slidersW = row - swatch - 6.0f; if (slidersW < 30.0f) slidersW = 30.0f;
    float each = (slidersW - 8.0f) / 3.0f;
    bool changed = false;
    for (int ch = 0; ch < 3; ++ch) {
        int cid = id ^ (int)(0x9e3779b9u * (unsigned)(ch + 1));
        float cxp = x + swatch + 6.0f + (float)ch * (each + 4.0f);
        if (Slider(cid, cxp, y, each, h, &rgb[ch], 0.0f, 1.0f)) changed = true;
    }
    if (lw > 0.0f) drawText(x + row + 8.0f, y + (h - textH()) * 0.5f, label, s, g_theme.text);
    return changed;
}

bool TreeNode(const char* label) {
    if (!g_lay.active) return false;
    const int id = hashLabel(label);
    bool* open = openState(id, false);
    const float h = rowH(), w = fullW(), s = g_theme.textScale;
    float x, y; place(w, h, x, y);
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
    if (inside) g_hot = id;
    if (g_active == id) { if (g_released) { if (inside) *open = !*open; g_active = 0; } }
    else if (inside && g_pressed) g_active = id;
    if (inside) quad(x, y, w, h, g_theme.bgHover);
    drawText(x + 4.0f, y + (h - textH()) * 0.5f, *open ? "v" : ">", s, g_theme.text);
    drawText(x + 4.0f + okay::Font8x8::Width * s * 1.5f, y + (h - textH()) * 0.5f, label, s, g_theme.text);
    if (*open) g_lay.indent += 16.0f;   // children indent until TreePop()
    return *open;
}

void TreePop() {
    if (!g_lay.active) return;
    g_lay.indent -= 16.0f;
    if (g_lay.indent < 0.0f) g_lay.indent = 0.0f;
}

} // namespace OkayUI
