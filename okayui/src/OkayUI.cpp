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

// Menu bar / open-menu state.
bool  g_inMenuBar = false;
float g_barX = 0.0f, g_barY = 0.0f, g_barH = 0.0f;
struct CurMenu {
    bool  active = false;
    int   menuId = 0;
    float x = 0, w = 0;                          // dropdown left + width
    float bx = 0, by = 0, bw = 0, bh = 0;        // the menu's button on the bar
    float itemStartY = 0, itemY = 0;             // dropdown item cursor
};
CurMenu g_curMenu;
Theme g_theme;

// Scoped color-override stack: each entry remembers a theme color slot and its prior
// value, so PopStyleColor can restore it. Reset every BeginFrame to survive imbalance.
struct ColorSave { unsigned char* field; unsigned char prev[4]; };
ColorSave g_colStack[64];
int       g_colTop = 0;
// Map a Col enum to the theme's 4-byte color field.
inline unsigned char* themeColor(int which) {
    switch (which) {
        case 0: return g_theme.bg;      case 1: return g_theme.bgHover;
        case 2: return g_theme.bgDown;  case 3: return g_theme.border;
        case 4: return g_theme.text;    case 5: return g_theme.panel;
        case 6: return g_theme.track;   case 7: return g_theme.accent;
        default: return g_theme.text;
    }
}

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

// ID stack: an extra seed mixed into label hashing so same-labelled widgets in a
// loop stay distinct. PushID/PopID adjust g_idSeed; a small stack remembers prior
// seeds to restore on pop.
unsigned g_idSeed = 0;
unsigned g_idStack[32];
int      g_idTop = 0;
inline void pushIdSeed(unsigned mix) {
    if (g_idTop < 32) g_idStack[g_idTop++] = g_idSeed;
    g_idSeed = (g_idSeed ^ mix) * 16777619u;
}
inline void popIdSeed() { if (g_idTop > 0) g_idSeed = g_idStack[--g_idTop]; }

// FNV-1a hash of a label -> stable nonzero widget id (so callers needn't pass ids).
inline int hashLabel(const char* s) {
    unsigned h = (2166136261u ^ (unsigned)g_lay.seed ^ g_idSeed);
    if (s) for (; *s; ++s) h = (h ^ (unsigned char)*s) * 16777619u;
    int id = (int)(h & 0x7fffffffu);
    return id ? id : 1;
}
// ---- Active font ---------------------------------------------------------------
bool defGlyph(char c, int x, int y) { return okay::Font8x8::Pixel(c, x, y); }
// Bold = the glyph OR-ed with itself shifted one pixel right (thickens strokes).
bool boldGlyph(char c, int x, int y) { return okay::Font8x8::Pixel(c, x, y) || (x > 0 && okay::Font8x8::Pixel(c, x - 1, y)); }
const Font kFontDefault{8, 8, defGlyph};
const Font kFontBold{8, 8, boldGlyph};
const Font* g_font = &kFontDefault;
inline int  fW() { return g_font->width; }
inline int  fH() { return g_font->height; }
inline bool fPix(char c, int x, int y) { return g_font->pixel && g_font->pixel(c, x, y); }
inline int  fMeasure(const char* s) { return fW() * cstrlen(s); }

inline float rowH() { return fH() * g_theme.textScale + 12.0f; }
inline float textH() { return fH() * g_theme.textScale; }
inline float labelW(const char* s) { return s && *s ? fMeasure(s) * g_theme.textScale : 0.0f; }

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
// Item-width override: SetNextItemWidth (one-shot) / PushItemWidth (scoped). availW()
// returns this instead of the full remaining width when set, so callers can size a
// widget explicitly. Always clamped to what actually remains on the line.
float g_nextItemW = -1.0f;
float g_itemWStack[16];
int   g_itemWTop = 0;
// Remaining width from the next item's left edge to the content's right edge.
inline float availW() {
    float real = g_lay.ox + g_lay.contentW - cursorX();
    float over = g_nextItemW > 0.0f ? g_nextItemW : (g_itemWTop > 0 ? g_itemWStack[g_itemWTop - 1] : -1.0f);
    if (over > 0.0f && over < real) return over;
    return real;
}

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
    g_nextItemW = -1.0f;   // a one-shot SetNextItemWidth applies to just this item
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

// A quad from four arbitrary corners (for rotated/skewed shapes and thick lines).
void quadPts(float ax, float ay, float bx, float by, float cx, float cy,
             float dx, float dy, const unsigned char c[4]) {
    SDL_Vertex* V = g_toOverlay ? g_ovVerts : g_verts;
    int*        I = g_toOverlay ? g_ovIdx   : g_idx;
    int&        nv = g_toOverlay ? g_onv : g_nv;
    int&        ni = g_toOverlay ? g_oni : g_ni;
    if (nv + 4 > kMaxVerts || ni + 6 > kMaxIdx) return;
    const SDL_Color sc = toColor(c);
    const int base = nv;
    SDL_FPoint uv; uv.x = 0.0f; uv.y = 0.0f;
    SDL_Vertex v; v.color = sc; v.tex_coord = uv;
    v.position.x = ax; v.position.y = ay; V[nv++] = v;
    v.position.x = bx; v.position.y = by; V[nv++] = v;
    v.position.x = cx; v.position.y = cy; V[nv++] = v;
    v.position.x = dx; v.position.y = dy; V[nv++] = v;
    I[ni++] = base + 0; I[ni++] = base + 1; I[ni++] = base + 2;
    I[ni++] = base + 0; I[ni++] = base + 2; I[ni++] = base + 3;
}

// A straight line of thickness `th` between two points, as a thin rotated quad.
void line(float x0, float y0, float x1, float y1, float th, const unsigned char c[4]) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = dx * dx + dy * dy;
    if (len <= 0.0001f) { quad(x0 - th * 0.5f, y0 - th * 0.5f, th, th, c); return; }
    len = 1.0f / SDL_sqrtf(len);
    // Perpendicular unit vector * half thickness.
    float px = -dy * len * th * 0.5f, py = dx * len * th * 0.5f;
    quadPts(x0 + px, y0 + py, x1 + px, y1 + py, x1 - px, y1 - py, x0 - px, y0 - py, c);
}

// Draw a C string from the 8x8 bitmap font, each lit pixel a `s`x`s` quad.
void drawText(float x, float y, const char* str, float s, const unsigned char c[4]) {
    if (!str) return;
    float penX = x;
    for (const char* p = str; *p; ++p) {
        for (int gy = 0; gy < fH(); ++gy)
            for (int gx = 0; gx < fW(); ++gx)
                if (fPix(*p, gx, gy))
                    quad(penX + gx * s, y + gy * s, s, s, c);
        penX += fW() * s;
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
    g_inMenuBar = false; g_curMenu.active = false;
    g_idSeed = 0; g_idTop = 0;        // reset the ID stack each frame
    // Restore any colors left pushed by an imbalanced previous frame, then clear.
    PopStyleColor(g_colTop);
    g_colTop = 0;
    g_nextItemW = -1.0f; g_itemWTop = 0;   // reset item-width overrides
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
        const float tw = fMeasure(label) * s;
        const float th = fH() * s;
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
        const float th = fH() * s;
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
        const float th = fH() * s;
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
        const float tw = fMeasure(label) * s;
        const float th = fH() * s;
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
    const float charW = fW() * s;
    const float th    = fH() * s;
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

void PushStyleColor(Col which, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    if (g_colTop >= 64) return;
    unsigned char* f = themeColor((int)which);
    ColorSave& s = g_colStack[g_colTop++];
    s.field = f;
    s.prev[0] = f[0]; s.prev[1] = f[1]; s.prev[2] = f[2]; s.prev[3] = f[3];
    f[0] = r; f[1] = g; f[2] = b; f[3] = a;
}

void PopStyleColor(int count) {
    while (count-- > 0 && g_colTop > 0) {
        ColorSave& s = g_colStack[--g_colTop];
        s.field[0] = s.prev[0]; s.field[1] = s.prev[1]; s.field[2] = s.prev[2]; s.field[3] = s.prev[3];
    }
}

void SetFont(const Font* f) { g_font = f ? f : &kFontDefault; }
const Font* GetFont()       { return g_font; }
const Font* FontDefault()   { return &kFontDefault; }
const Font* FontBold()      { return &kFontBold; }

// ---- Auto-layout window + ImGui-style overloads --------------------------------

bool Begin(const char* title, float x, float y, float w, float h, bool* p_open) {
    g_lay.seed = 0;                    // hash the window id from a FIXED seed so it (and
    const int id = hashLabel(title);   // every widget id under it) is stable across frames
    WinSlot* slot = nullptr;
    for (WinSlot& ws : g_wins) if (ws.used && ws.id == id) { slot = &ws; break; }
    if (!slot) for (WinSlot& ws : g_wins) if (!ws.used) { ws.used = true; ws.id = id; ws.x = x; ws.y = y; slot = &ws; break; }
    float wx = slot ? slot->x : x, wy = slot ? slot->y : y;
    const float titleH = rowH();
    bool* collapsed = openState(id ^ 0x5c01, false);   // per-window collapsed flag

    // Title-bar sub-rects: a collapse caret on the left, an optional close [x] on the
    // right, and the draggable strip in between.
    const float btn = titleH;
    const float caretX = wx, caretW = btn;
    const float closeX = wx + w - btn, closeW = p_open ? btn : 0.0f;

    // Collapse toggle: click the caret to fold/unfold the window.
    const bool overCaret = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, caretX, wy, caretW, titleH);
    if (overCaret) g_hot = id ^ 0x5c01;
    if (g_active == (id ^ 0x5c01)) { if (g_released) { if (overCaret) *collapsed = !*collapsed; g_active = 0; } }
    else if (overCaret && g_pressed) { g_active = id ^ 0x5c01; g_focusClaimed = true; }

    // Close button: click the [x] to clear *p_open.
    bool overClose = false;
    if (p_open) {
        overClose = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, closeX, wy, closeW, titleH);
        if (overClose) g_hot = id ^ 0xc105;
        if (g_active == (id ^ 0xc105)) { if (g_released) { if (overClose) *p_open = false; g_active = 0; } }
        else if (overClose && g_pressed) { g_active = id ^ 0xc105; g_focusClaimed = true; }
    }

    // Drag the window by the title bar (the strip between caret and close button).
    const float dragX = wx + caretW, dragW = w - caretW - closeW;
    const bool overDrag = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, dragX, wy, dragW, titleH);
    if (overDrag) g_hot = id;
    if (g_active == id) {
        if (!g_in.mouseDown) g_active = 0;
        else if (slot) { slot->x += g_mouseDX; slot->y += g_mouseDY; wx = slot->x; wy = slot->y; }
    } else if (overDrag && g_pressed) {
        g_active = id; g_focusClaimed = true;
    }

    // Frame: panel body (only the title bar when collapsed) + accent underline + title.
    const float bodyH = *collapsed ? titleH : h;
    Panel(wx, wy, w, bodyH);
    quad(wx, wy, w, titleH, g_theme.bgDown);
    quad(wx, wy + titleH - 2.0f, w, 2.0f, g_theme.accent);
    const float s = g_theme.textScale;
    drawText(wx + 8.0f, wy + (titleH - textH()) * 0.5f, *collapsed ? "+" : "-", s, g_theme.text);  // caret
    if (title && *title) drawText(wx + 8.0f + fW() * s * 2.0f, wy + (titleH - textH()) * 0.5f, title, s, g_theme.text);
    if (p_open) {
        const unsigned char* xcol = overClose ? g_theme.accent : g_theme.text;
        drawText(closeX + (btn - fW() * s) * 0.5f, wy + (titleH - textH()) * 0.5f, "x", s, xcol);
    }

    if (*collapsed) { g_lay.active = false; return false; }   // folded: skip content

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

void SetNextItemWidth(float w) { g_nextItemW = w; }
void PushItemWidth(float w)    { if (g_itemWTop < 16) g_itemWStack[g_itemWTop++] = w; }
void PopItemWidth()            { if (g_itemWTop > 0) --g_itemWTop; }

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

void TextColored(unsigned char r, unsigned char g, unsigned char b, const char* s) {
    if (!g_lay.active) return;
    const float h = rowH();
    float x, y; place(s && *s ? labelW(s) : 1.0f, h, x, y);
    const unsigned char col[4] = { r, g, b, 255 };
    if (s && *s) drawText(x, y + (h - textH()) * 0.5f, s, g_theme.textScale, col);
}

void TextDisabled(const char* s) {
    // Blend the theme text toward the panel color for a muted look.
    const unsigned char* t = g_theme.text; const unsigned char* p = g_theme.panel;
    TextColored((unsigned char)((t[0] + p[0]) / 2), (unsigned char)((t[1] + p[1]) / 2),
                (unsigned char)((t[2] + p[2]) / 2), s);
}

void SeparatorText(const char* label) {
    if (!g_lay.active) return;
    const float h = rowH();
    float x, y; place(fullW(), h, x, y);
    const float ty = y + (h - textH()) * 0.5f;
    const float lw = label && *label ? labelW(label) : 0.0f;
    if (lw > 0.0f) drawText(x, ty, label, g_theme.textScale, g_theme.text);
    // A line filling the space to the right of the label.
    const float lineX = x + (lw > 0.0f ? lw + 8.0f : 0.0f);
    const float lineW = (x + fullW()) - lineX;
    if (lineW > 2.0f) quad(lineX, y + h * 0.5f, lineW, 1.0f, g_theme.border);
}

void Dummy(float w, float h) {
    if (!g_lay.active) return;
    float x, y; place(w, h, x, y);
}

void Indent(float w) {
    if (!g_lay.active) return;
    g_lay.indent += (w > 0.0f ? w : textH());   // default step = one text line height
}

void Unindent(float w) {
    if (!g_lay.active) return;
    g_lay.indent -= (w > 0.0f ? w : textH());
    if (g_lay.indent < 0.0f) g_lay.indent = 0.0f;
}

void Bullet() {
    if (!g_lay.active) return;
    const float d = textH() * 0.35f, h = rowH();
    float x, y; place(d, h, x, y);
    quad(x, y + (h - d) * 0.5f, d, d, g_theme.text);
}

void BulletText(const char* s) {
    if (!g_lay.active) return;
    const float d = textH() * 0.35f, h = rowH();
    const float w = d + 8.0f + (s && *s ? labelW(s) : 0.0f);
    float x, y; place(w, h, x, y);
    quad(x, y + (h - d) * 0.5f, d, d, g_theme.text);
    if (s && *s) drawText(x + d + 8.0f, y + (h - textH()) * 0.5f, s, g_theme.textScale, g_theme.text);
}

void PushID(int id)         { pushIdSeed((unsigned)id * 2654435761u + 1u); }
void PushID(const char* id) {
    unsigned h = 2166136261u;
    if (id) for (const char* p = id; *p; ++p) h = (h ^ (unsigned char)*p) * 16777619u;
    pushIdSeed(h);
}
void PopID() { popIdSeed(); }

bool Button(const char* label) {
    if (!g_lay.active) return false;
    const float w = labelW(label) + 24.0f, h = rowH();
    float x, y; place(w, h, x, y);
    return Button(hashLabel(label), x, y, w, h, label);
}

bool SmallButton(const char* label) {
    if (!g_lay.active) return false;
    const float w = labelW(label) + 12.0f, h = textH() + 6.0f;   // tight padding
    float x, y; place(w, h, x, y);
    return Button(hashLabel(label), x, y, w, h, label);
}

void TabBar(const char* const* labels, int count, int* current) {
    if (!g_lay.active || !labels || !current || count <= 0) return;
    const float h = rowH();
    // Reserve the full row; lay tabs left-to-right within it.
    float x, y; place(fullW(), h, x, y);
    float tx = x;
    for (int i = 0; i < count; ++i) {
        const char* lbl = labels[i] ? labels[i] : "";
        const float tw = labelW(lbl) + 20.0f;
        const unsigned uid = ((unsigned)hashLabel(lbl) ^ ((unsigned)i * 2654435761u)) & 0x7fffffffu;
        Tab((int)uid, tx, y, tw, h, lbl, current, i);
        tx += tw + 2.0f;
    }
    // A baseline under the whole tab row.
    quad(x, y + h - 2.0f, fullW(), 2.0f, g_theme.border);
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

bool SliderInt(const char* label, int* value, int minV, int maxV) {
    if (!g_lay.active || !value) return false;
    float f = (float)*value;
    // Reuse the float slider, then snap to the nearest integer.
    bool ch = SliderFloat(label, &f, (float)minV, (float)maxV);
    int iv = (int)(f + (f >= 0.0f ? 0.5f : -0.5f));
    if (iv < minV) iv = minV;
    if (iv > maxV) iv = maxV;
    if (iv != *value) { *value = iv; return true; }
    return ch && false;   // value unchanged after snapping
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

// Shared framing + scaling for the two plot widgets. Returns the plot rect and the
// resolved min/max; reserves layout space (leaving room for a label to the right).
static bool plotFrame(const char* label, const float* values, int count,
                      float& scaleMin, float& scaleMax, float height,
                      float& px, float& py, float& pw, float& ph) {
    if (!g_lay.active || !values || count <= 0) return false;
    const float lw = labelW(label) > 0.0f ? labelW(label) + 8.0f : 0.0f;
    const float h = height > 0.0f ? height : rowH() * 2.4f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    pw = w - lw; if (pw < 24.0f) pw = 24.0f;
    px = x; py = y; ph = h;
    if (lw > 0.0f) drawText(x + pw + 8.0f, y + (h - textH()) * 0.5f, label, g_theme.textScale, g_theme.text);
    if (scaleMin >= scaleMax) {              // auto-range from the data
        scaleMin = values[0]; scaleMax = values[0];
        for (int i = 1; i < count; ++i) { if (values[i] < scaleMin) scaleMin = values[i];
                                          if (values[i] > scaleMax) scaleMax = values[i]; }
        if (scaleMax - scaleMin < 0.0001f) scaleMax = scaleMin + 1.0f;
    }
    quad(px, py, pw, ph, g_theme.track);      // framed background
    return true;
}

void PlotLines(const char* label, const float* values, int count,
               float scaleMin, float scaleMax, float height) {
    float px, py, pw, ph;
    if (!plotFrame(label, values, count, scaleMin, scaleMax, height, px, py, pw, ph)) return;
    const float span = scaleMax - scaleMin;
    auto sy = [&](float v) { return py + ph - 2.0f - ((v - scaleMin) / span) * (ph - 4.0f); };
    const float step = count > 1 ? (pw - 4.0f) / (count - 1) : 0.0f;
    for (int i = 0; i + 1 < count; ++i) {
        float x0 = px + 2.0f + step * i,     y0 = sy(values[i]);
        float x1 = px + 2.0f + step * (i + 1), y1 = sy(values[i + 1]);
        line(x0, y0, x1, y1, 1.5f, g_theme.accent);
    }
}

void PlotHistogram(const char* label, const float* values, int count,
                   float scaleMin, float scaleMax, float height) {
    float px, py, pw, ph;
    if (!plotFrame(label, values, count, scaleMin, scaleMax, height, px, py, pw, ph)) return;
    const float span = scaleMax - scaleMin;
    const float bw = (pw - 4.0f) / count;
    for (int i = 0; i < count; ++i) {
        float t = (values[i] - scaleMin) / span; if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
        float bh = t * (ph - 4.0f);
        quad(px + 2.0f + bw * i + 1.0f, py + ph - 2.0f - bh, bw - 2.0f, bh, g_theme.accent);
    }
}

void LabelText(const char* label, const char* value) {
    if (!g_lay.active) return;
    const float h = rowH(), w = availW();
    float x, y; place(w, h, x, y);
    if (value && *value) drawText(x, y + (h - textH()) * 0.5f, value, g_theme.textScale, g_theme.text);
    if (label && *label) {
        const float lw = labelW(label);
        drawText(x + w - lw, y + (h - textH()) * 0.5f, label, g_theme.textScale, g_theme.text);
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

bool InputInt(const char* label, int* value, int step) {
    if (!g_lay.active || !value) return false;
    const int id = hashLabel(label);
    const float h = rowH();
    const float lw = labelW(label) > 0.0f ? labelW(label) + 8.0f : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    const float btnW = h;                 // square [-]/[+] steppers
    float fieldW = w - lw - btnW * 2.0f - 8.0f;
    if (fieldW < 24.0f) fieldW = 24.0f;
    // Value box.
    quad(x, y, fieldW, h, g_theme.track);
    char buf[32]; std::snprintf(buf, sizeof(buf), "%d", *value);
    drawText(x + 6.0f, y + (h - textH()) * 0.5f, buf, g_theme.textScale, g_theme.text);
    // Steppers (unique ids via the field id so they don't collide).
    const float bx = x + fieldW + 4.0f;
    bool ch = false;
    if (Button(id ^ 0x2d, bx, y, btnW, h, "-"))              { *value -= step; ch = true; }
    if (Button(id ^ 0x2b, bx + btnW + 4.0f, y, btnW, h, "+")) { *value += step; ch = true; }
    if (lw > 0.0f) drawText(x + fieldW + btnW * 2.0f + 12.0f, y + (h - textH()) * 0.5f,
                            label, g_theme.textScale, g_theme.text);
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
    drawText(x + 8.0f + fW() * s * 2.0f, y + (h - textH()) * 0.5f, label, s, g_theme.text);
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
    drawText(x + boxW - fW() * s - 6.0f, y + (h - textH()) * 0.5f, *open ? "-" : "+", s, g_theme.text);
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
    drawText(x + 4.0f + fW() * s * 1.5f, y + (h - textH()) * 0.5f, label, s, g_theme.text);
    if (*open) g_lay.indent += 16.0f;   // children indent until TreePop()
    return *open;
}

void TreePop() {
    if (!g_lay.active) return;
    g_lay.indent -= 16.0f;
    if (g_lay.indent < 0.0f) g_lay.indent = 0.0f;
}

void BeginMenuBar() {
    if (!g_lay.active) return;
    const float h = rowH();
    float x, y; place(fullW(), h, x, y);
    quad(x, y, fullW(), h, g_theme.bgDown);
    g_inMenuBar = true; g_barX = x; g_barY = y; g_barH = h;
}

void EndMenuBar() { g_inMenuBar = false; }

bool BeginMenu(const char* label) {
    if (!g_lay.active || !g_inMenuBar) return false;
    const int id = hashLabel(label);
    bool* open = openState(id, false);
    const float h = g_barH, s = g_theme.textScale;
    const float bw = labelW(label) + 16.0f;
    const float x = g_barX, y = g_barY;
    g_barX += bw;   // advance the bar cursor for the next menu

    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, bw, h);
    if (inside) g_hot = id;
    if (g_active == id) { if (g_released) { if (inside) *open = !*open; g_active = 0; } }
    else if (inside && g_pressed) g_active = id;
    if (*open || inside) quad(x, y, bw, h, g_theme.bgHover);
    drawText(x + 8.0f, y + (h - textH()) * 0.5f, label, s, g_theme.text);

    if (*open) {
        g_curMenu.active = true; g_curMenu.menuId = id;
        g_curMenu.x = x; g_curMenu.w = 180.0f;
        g_curMenu.bx = x; g_curMenu.by = y; g_curMenu.bw = bw; g_curMenu.bh = h;
        g_curMenu.itemStartY = y + h; g_curMenu.itemY = y + h;
    }
    return *open;
}

bool MenuItem(const char* label) {
    if (!g_lay.active || !g_curMenu.active) return false;
    const float h = rowH(), s = g_theme.textScale;
    const float x = g_curMenu.x, w = g_curMenu.w, y = g_curMenu.itemY;
    g_curMenu.itemY += h;
    const int iid = hashLabel(label) ^ g_curMenu.menuId;
    const bool ih = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
    if (ih) g_hot = iid;
    bool clicked = false;
    if (g_active == iid) {
        if (g_released) { if (ih) { clicked = true; *openState(g_curMenu.menuId, false) = false; } g_active = 0; }
    } else if (ih && g_pressed) g_active = iid;
    g_toOverlay = true;
    quad(x, y, w, h, ih ? g_theme.bgHover : g_theme.panel);
    drawText(x + 10.0f, y + (h - textH()) * 0.5f, label, s, g_theme.text);
    g_toOverlay = false;
    return clicked;
}

void EndMenu() {
    if (!g_curMenu.active) return;
    // A press outside both the menu button and its item list closes the menu.
    const bool inBtn  = pointIn(g_in.mouseX, g_in.mouseY, g_curMenu.bx, g_curMenu.by, g_curMenu.bw, g_curMenu.bh);
    const bool inList = pointIn(g_in.mouseX, g_in.mouseY, g_curMenu.x, g_curMenu.itemStartY,
                               g_curMenu.w, g_curMenu.itemY - g_curMenu.itemStartY);
    if (g_pressed && !inBtn && !inList) *openState(g_curMenu.menuId, false) = false;
    g_curMenu.active = false;
}

bool Selectable(const char* label, bool selected) {
    if (!g_lay.active) return false;
    const int id = hashLabel(label);
    const float h = rowH(), w = fullW(), s = g_theme.textScale;
    float x, y; place(w, h, x, y);
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
    if (inside) g_hot = id;
    bool clicked = false;
    if (g_active == id) { if (g_released) { if (inside) clicked = true; g_active = 0; } }
    else if (inside && g_pressed) g_active = id;
    if (selected)      quad(x, y, w, h, g_theme.accent);
    else if (inside)   quad(x, y, w, h, g_theme.bgHover);
    drawText(x + 6.0f, y + (h - textH()) * 0.5f, label, s, g_theme.text);
    return clicked;
}

} // namespace OkayUI
