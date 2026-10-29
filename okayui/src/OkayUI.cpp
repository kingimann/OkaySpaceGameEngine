#include <algorithm>
#include "okay/UI/OkayUI.hpp"
#include "okay/Graphics/Font.hpp"   // okay::Font8x8 — a no-STL C API over a bitmap font
#include "OkayUI_FontAtlas.hpp"     // the launcher font (TrueType) for UseLauncherLook()
#include "okay/Graphics/TtfFont.hpp" // fallback fonts for characters the atlas lacks
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

// The launcher look: text comes from a TrueType atlas, and flat shapes sample its
// solid-white patch, so shapes + text share one texture (and one draw call). Spans
// carry kAtlasTex as a placeholder; EndFrame swaps in the renderer's real texture.
bool g_look = false;
const fontatlas::Atlas* g_ttf = nullptr;            // set while the look is on and the atlas built
SDL_Texture* const kAtlasTex = (SDL_Texture*)(void*)&g_ttf;   // placeholder (never dereferenced)
SDL_Texture* g_defTex = nullptr;                    // texture for flat geometry (nullptr / kAtlasTex)
float g_whiteU = 0.0f, g_whiteV = 0.0f;             // uv of an opaque texel for flat geometry
SDL_Texture*  g_atlasTex = nullptr;                 // the atlas on g_atlasR
SDL_Renderer* g_atlasR = nullptr;
// Engine fonts drawing text the atlas lacks (fontatlas::FallbackFont). Their spans
// carry the font's address as a placeholder; EndFrame binds the font's atlas.
okay::TtfFont* g_fbFonts[8];
int            g_nfb = 0;
// Texture changes inside the overlay batch (fallback-font text in toasts and
// popups): the overlay index each starts at. mergeOverlay turns them into spans.
struct OverlaySpan { int startIdx; SDL_Texture* tex; };
OverlaySpan g_ovSpans[32];
int         g_novSpans = 0;
SDL_Texture* fallbackTex(okay::TtfFont* f) {
    for (int i = 0; i < g_nfb; ++i) if (g_fbFonts[i] == f) return (SDL_Texture*)(void*)f;
    if (g_nfb == 8) return nullptr;
    g_fbFonts[g_nfb++] = f;
    return (SDL_Texture*)(void*)f;
}

// Draw spans: the main batch is split into contiguous index ranges, each carrying a
// clip state and a texture, so scrolling child regions can clip and Image() can bind a
// texture. A span records the index where a state begins; EndFrame draws each range
// with the matching clip rect + texture. The overlay batch is never clipped.
struct DrawSpan { int startIdx; bool on; SDL_Rect rect; SDL_Texture* tex; };
DrawSpan g_spans[128];
int      g_nspans = 0;
SDL_Rect g_clipStack[16];
int      g_clipTop = 0;
// Current draw state; a span is emitted whenever clip or texture changes.
bool         g_curClipOn = false;
SDL_Rect     g_curClipRect = { 0, 0, 0, 0 };
SDL_Texture* g_curTex = nullptr;
// Record a span at the current index position (coalescing if no geometry was added
// since the previous span).
void pushSpan() {
    if (g_nspans > 0 && g_spans[g_nspans - 1].startIdx == g_ni) {   // overwrite empty span
        g_spans[g_nspans - 1].on = g_curClipOn; g_spans[g_nspans - 1].rect = g_curClipRect;
        g_spans[g_nspans - 1].tex = g_curTex; return;
    }
    if (g_nspans < 128) g_spans[g_nspans++] = DrawSpan{ g_ni, g_curClipOn, g_curClipRect, g_curTex };
}
// Backwards-compatible helper used by the clip API.
void pushClipSpan(bool on, SDL_Rect rect) { g_curClipOn = on; g_curClipRect = rect; pushSpan(); }
// Switch the bound texture for subsequent geometry (nullptr = untextured/flat color).
void setTexture(SDL_Texture* t) { if (t != g_curTex) { g_curTex = t; pushSpan(); } }

Input g_in;                      // this frame's input
bool  g_prevDown = false;        // last frame's mouse-down (for edge detection)
bool  g_pressed  = false;        // mouse went down this frame
bool  g_released = false;        // mouse went up this frame
bool  g_rPrevDown = false;       // last frame's right-mouse (edge detection)
bool  g_rPressed  = false;       // right button went down this frame

// Popup/context-menu registry. Each open popup remembers where it opened and its
// measured height (so the background can be drawn before the content next frame).
struct PopupSlot { int id = 0; bool open = false; bool used = false; float x = 0, y = 0; };
PopupSlot g_popups[8];
PopupSlot* popupSlot(int id, bool create) {
    for (PopupSlot& p : g_popups) if (p.used && p.id == id) return &p;
    if (!create) return nullptr;
    for (PopupSlot& p : g_popups) if (!p.used) { p.used = true; p.id = id; p.open = false; return &p; }
    return nullptr;
}
int      g_openPopupThisFrame = 0;   // popup id requested to open this frame
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
    bool  winClip  = false;           // Begin() clipped the content to the window (launcher look)
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

// Saved parent layout + child metadata while inside a BeginChild/EndChild pair.
struct ChildSave {
    LayoutState lay;      // parent layout to restore
    float rx, ry, rw, rh; // child region rect
    float top;            // unscrolled content top (ry + pad)
    float* scroll;        // persisted scroll offset
    float* maxSlot;       // persisted max scroll from last frame (clamps this frame)
    float pad;
    bool  hovered;        // mouse over the region this frame
};
ChildSave g_childStack[8];
int       g_childTop = 0;
constexpr float kScrollbarW = 10.0f;

// Multi-column layout state (Columns/NextColumn/EndColumns).
struct ColumnState {
    bool  active = false;
    int   count = 1, index = 0;
    float x0 = 0, fullW = 0;   // content origin + full width when Columns() began
    float rowTop = 0, rowBottom = 0;
};
ColumnState g_col;

// Table layout state (BeginTable/TableNextRow/TableNextColumn/EndTable).
struct TableState {
    bool  active = false;
    int   id = 0, count = 0, index = -1;
    float x0 = 0, fullW = 0;
    float rowTop = 0, rowBottom = 0, rowStart = 0;
    float* w[16] = {nullptr};   // persisted per-column widths (pixels)
    float headerBottom = 0;     // y below the header row (for the resize handles)
};
TableState g_tbl;

// Popup body layout context (uses LayoutState, so defined after it).
struct PopupCtx { LayoutState lay; int id; float x, y, w; float* hSlot; };
PopupCtx g_popupStack[4];
int      g_popupTop = 0;

// Drag & drop: a single int payload. `armed` records a press on a source before it
// has moved far enough to count as a drag; `active` means a drag is in flight.
struct DragDrop {
    bool  active = false;
    int   payload = 0;
    bool  armed = false;
    int   armPayload = 0;
    float armX = 0, armY = 0;
};
DragDrop g_dd;

// Draggable-window position store (no STL): position persists across frames per id.
struct WinSlot { int id = 0; float x = 0, y = 0; bool used = false; };
WinSlot g_wins[16];

// Per-widget open/closed state (CollapsingHeader, Combo), persisted by id.
struct OpenSlot { int id = 0; bool open = false; bool used = false; };
OpenSlot g_opens[128];
bool* openState(int id, bool dflt) {
    for (OpenSlot& s : g_opens) if (s.used && s.id == id) return &s.open;
    for (OpenSlot& s : g_opens) if (!s.used) { s.used = true; s.id = id; s.open = dflt; return &s.open; }
    static bool sink = false; return &sink;   // table full: harmless fallback
}
// Per-id float state (child-region scroll offsets), persisted across frames.
struct FloatSlot { int id = 0; float v = 0.0f; bool used = false; };
FloatSlot g_floats[256];
float* floatState(int id, float dflt) {
    for (FloatSlot& s : g_floats) if (s.used && s.id == id) return &s.v;
    for (FloatSlot& s : g_floats) if (!s.used) { s.used = true; s.id = id; s.v = dflt; return &s.v; }
    static float sink = 0.0f; return &sink;
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
// Width of `s` in font-cell units (multiply by textScale for pixels). With the
// launcher look it's the TrueType width at the 8-unit cell height (proportional).
inline float fMeasure(const char* s) {
    if (g_ttf && s) {
        if (okay::TtfFont* f = fontatlas::FallbackFont(s)) return f->Measure(s, 8.0f);
        const fontatlas::Size& z = g_ttf->Pick(16.0f);
        float w = 0.0f;
        for (const char* p = s; *p;) {
            const unsigned cp = fontatlas::NextCodepoint(&p);
            const fontatlas::Glyph* g = z.Find(cp); if (!g) g = z.Find('?');
            if (g) w += g->advance;
        }
        return w * 8.0f / z.px;
    }
    return (float)(fW() * cstrlen(s));
}
// Average character width in cell units (spacing glyph-like icons).
inline float fCharW() { return g_ttf ? fMeasure("n") : (float)fW(); }

inline float rowH() { return fH() * g_theme.textScale + (g_look ? 15.0f : 12.0f); }
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
    SDL_FPoint uv; uv.x = g_whiteU; uv.y = g_whiteV;
    SDL_Vertex v;
    v.color = sc; v.tex_coord = uv;
    v.position.x = x;     v.position.y = y;     V[nv++] = v;
    v.position.x = x + w; v.position.y = y;     V[nv++] = v;
    v.position.x = x + w; v.position.y = y + h; V[nv++] = v;
    v.position.x = x;     v.position.y = y + h; V[nv++] = v;
    I[ni++] = base + 0; I[ni++] = base + 1; I[ni++] = base + 2;
    I[ni++] = base + 0; I[ni++] = base + 2; I[ni++] = base + 3;
}

// A textured quad: binds `tex`, samples uv [u0,v0]..[u1,v1], modulated by `tint`
// (use white for the raw image). Reverts to the untextured state afterward so the
// next flat quad starts a new span. Textured geometry always goes to the main batch.
void texQuad(float x, float y, float w, float h, SDL_Texture* tex,
             float u0, float v0, float u1, float v1, const unsigned char tint[4]) {
    setTexture(tex);
    if (g_nv + 4 > kMaxVerts || g_ni + 6 > kMaxIdx) { setTexture(g_defTex); return; }
    const SDL_Color sc = toColor(tint);
    const int base = g_nv;
    SDL_Vertex v; v.color = sc;
    v.position.x = x;     v.position.y = y;     v.tex_coord.x = u0; v.tex_coord.y = v0; g_verts[g_nv++] = v;
    v.position.x = x + w; v.position.y = y;     v.tex_coord.x = u1; v.tex_coord.y = v0; g_verts[g_nv++] = v;
    v.position.x = x + w; v.position.y = y + h; v.tex_coord.x = u1; v.tex_coord.y = v1; g_verts[g_nv++] = v;
    v.position.x = x;     v.position.y = y + h; v.tex_coord.x = u0; v.tex_coord.y = v1; g_verts[g_nv++] = v;
    g_idx[g_ni++] = base + 0; g_idx[g_ni++] = base + 1; g_idx[g_ni++] = base + 2;
    g_idx[g_ni++] = base + 0; g_idx[g_ni++] = base + 2; g_idx[g_ni++] = base + 3;
    setTexture(g_defTex);
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
    SDL_FPoint uv; uv.x = g_whiteU; uv.y = g_whiteV;
    SDL_Vertex v; v.color = sc; v.tex_coord = uv;
    v.position.x = ax; v.position.y = ay; V[nv++] = v;
    v.position.x = bx; v.position.y = by; V[nv++] = v;
    v.position.x = cx; v.position.y = cy; V[nv++] = v;
    v.position.x = dx; v.position.y = dy; V[nv++] = v;
    I[ni++] = base + 0; I[ni++] = base + 1; I[ni++] = base + 2;
    I[ni++] = base + 0; I[ni++] = base + 2; I[ni++] = base + 3;
}

// An axis-aligned quad whose four corners each have their own color; the GPU
// interpolates between them (for gradients: hue bars, SV squares, fades).
void gradQuad(float x, float y, float w, float h,
              const unsigned char tl[4], const unsigned char tr[4],
              const unsigned char br[4], const unsigned char bl[4]) {
    SDL_Vertex* V = g_toOverlay ? g_ovVerts : g_verts;
    int*        I = g_toOverlay ? g_ovIdx   : g_idx;
    int&        nv = g_toOverlay ? g_onv : g_nv;
    int&        ni = g_toOverlay ? g_oni : g_ni;
    if (nv + 4 > kMaxVerts || ni + 6 > kMaxIdx) return;
    const int base = nv;
    SDL_FPoint uv; uv.x = g_whiteU; uv.y = g_whiteV;
    SDL_Vertex v; v.tex_coord = uv;
    v.position.x = x;     v.position.y = y;     v.color = toColor(tl); V[nv++] = v;
    v.position.x = x + w; v.position.y = y;     v.color = toColor(tr); V[nv++] = v;
    v.position.x = x + w; v.position.y = y + h; v.color = toColor(br); V[nv++] = v;
    v.position.x = x;     v.position.y = y + h; v.color = toColor(bl); V[nv++] = v;
    I[ni++] = base + 0; I[ni++] = base + 1; I[ni++] = base + 2;
    I[ni++] = base + 0; I[ni++] = base + 2; I[ni++] = base + 3;
}

// HSV (h in [0,1), s,v in [0,1]) -> RGB bytes.
void hsvToRgb(float h, float s, float v, unsigned char out[3]) {
    h -= SDL_floorf(h); if (h < 0) h += 1.0f;
    float r = v, g = v, b = v;
    float hh = h * 6.0f;
    int   i = (int)hh;
    float f = hh - i;
    float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default:r = v; g = p; b = q; break;
    }
    out[0] = (unsigned char)(r * 255.0f + 0.5f);
    out[1] = (unsigned char)(g * 255.0f + 0.5f);
    out[2] = (unsigned char)(b * 255.0f + 0.5f);
}

// RGB bytes -> HSV (h,s,v in [0,1]).
void rgbToHsv(unsigned char R, unsigned char G, unsigned char B, float& h, float& s, float& v) {
    float r = R / 255.0f, g = G / 255.0f, b = B / 255.0f;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    v = mx; float d = mx - mn;
    s = mx <= 0.0f ? 0.0f : d / mx;
    if (d <= 0.0f) { h = 0.0f; return; }
    if (mx == r)      h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (mx == g) h = (b - r) / d + 2.0f;
    else              h = (r - g) / d + 4.0f;
    h /= 6.0f;
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

// A single filled triangle.
void tri(float ax, float ay, float bx, float by, float cx, float cy, const unsigned char c[4]) {
    SDL_Vertex* V = g_toOverlay ? g_ovVerts : g_verts;
    int*        I = g_toOverlay ? g_ovIdx   : g_idx;
    int&        nv = g_toOverlay ? g_onv : g_nv;
    int&        ni = g_toOverlay ? g_oni : g_ni;
    if (nv + 3 > kMaxVerts || ni + 3 > kMaxIdx) return;
    const SDL_Color sc = toColor(c);
    const int base = nv;
    SDL_FPoint uv; uv.x = g_whiteU; uv.y = g_whiteV;
    SDL_Vertex v; v.color = sc; v.tex_coord = uv;
    v.position.x = ax; v.position.y = ay; V[nv++] = v;
    v.position.x = bx; v.position.y = by; V[nv++] = v;
    v.position.x = cx; v.position.y = cy; V[nv++] = v;
    I[ni++] = base + 0; I[ni++] = base + 1; I[ni++] = base + 2;
}

// A filled circle as a triangle fan of `segments` wedges.
void fillCircle(float cx, float cy, float radius, const unsigned char c[4], int segments = 24) {
    if (segments < 3) segments = 3;
    const float step = 6.2831853f / segments;
    float px = cx + radius, py = cy;
    for (int i = 1; i <= segments; ++i) {
        float a = step * i;
        float nx = cx + radius * SDL_cosf(a), ny = cy + radius * SDL_sinf(a);
        tri(cx, cy, px, py, nx, ny, c);
        px = nx; py = ny;
    }
}

// A filled circular arc/fan from angle a0 to a1 (radians), centered at (cx,cy).
void arcFan(float cx, float cy, float r, float a0, float a1, const unsigned char c[4], int seg = 5) {
    if (seg < 1) seg = 1;
    const float step = (a1 - a0) / seg;
    float px = cx + r * SDL_cosf(a0), py = cy + r * SDL_sinf(a0);
    for (int i = 1; i <= seg; ++i) {
        float a = a0 + step * i;
        float nx = cx + r * SDL_cosf(a), ny = cy + r * SDL_sinf(a);
        tri(cx, cy, px, py, nx, ny, c);
        px = nx; py = ny;
    }
}

// A filled rounded rectangle (falls back to a plain quad when rad <= 0). Decomposed
// into three bars + four quarter-circle corners so translucent fills never overlap.
void rrect(float x, float y, float w, float h, float rad, const unsigned char c[4]) {
    if (rad <= 0.5f || w <= 0.0f || h <= 0.0f) { quad(x, y, w, h, c); return; }
    if (rad > w * 0.5f) rad = w * 0.5f;
    if (rad > h * 0.5f) rad = h * 0.5f;
    const float PI = 3.14159265f;
    quad(x, y + rad, w, h - 2 * rad, c);            // middle band (full width)
    quad(x + rad, y, w - 2 * rad, rad, c);          // top strip
    quad(x + rad, y + h - rad, w - 2 * rad, rad, c);// bottom strip
    arcFan(x + rad,     y + rad,     rad, PI,          1.5f * PI, c);  // top-left
    arcFan(x + w - rad, y + rad,     rad, 1.5f * PI,   2.0f * PI, c);  // top-right
    arcFan(x + w - rad, y + h - rad, rad, 0.0f,        0.5f * PI, c);  // bottom-right
    arcFan(x + rad,     y + h - rad, rad, 0.5f * PI,   PI,        c);  // bottom-left
}
inline float rnd() { return g_theme.rounding; }

// Text with characters the atlas lacks: every glyph from the engine font `f`, bound
// to its atlas (a span of its own; in the overlay, an overlay span).
void bindTexture(SDL_Texture* t) {
    if (!g_toOverlay) { setTexture(t); return; }
    if (g_novSpans > 0 && g_ovSpans[g_novSpans - 1].startIdx == g_oni) g_ovSpans[g_novSpans - 1].tex = t;
    else if (g_novSpans < 32) g_ovSpans[g_novSpans++] = OverlaySpan{g_oni, t};
}
void drawTextWith(okay::TtfFont* f, float x, float y, const char* str, float s, const unsigned char c[4]) {
    SDL_Texture* tex = fallbackTex(f);
    if (!tex) return;
    f->Prepare(str);   // bake its new characters before reading the atlas size
    const float px = fH() * s, k = px / f->BakeHeight();
    const float aw = (float)f->Atlas().Width(), ah = (float)f->Atlas().Height();
    const float base = y + f->Ascent(px);
    const SDL_Color sc = toColor(c);
    SDL_Vertex* V = g_toOverlay ? g_ovVerts : g_verts;
    int*        I = g_toOverlay ? g_ovIdx   : g_idx;
    int&        nv = g_toOverlay ? g_onv : g_nv;
    int&        ni = g_toOverlay ? g_oni : g_ni;
    bindTexture(tex);
    float pen = x;
    for (const char* p = str; *p;) {
        const okay::TtfFont::Glyph* g = f->GlyphFor(fontatlas::NextCodepoint(&p));
        if (!g) continue;
        if (g->x1 > g->x0 && g->y1 > g->y0 && nv + 4 <= kMaxVerts && ni + 6 <= kMaxIdx) {
            const float x0 = pen + g->xoff * k, y0 = base + g->yoff * k;
            const float x1 = x0 + (g->x1 - g->x0) * k, y1 = y0 + (g->y1 - g->y0) * k;
            const float u0 = g->x0 / aw, v0 = g->y0 / ah, u1 = g->x1 / aw, v1 = g->y1 / ah;
            const int b0 = nv;
            SDL_Vertex v; v.color = sc;
            v.position.x = x0; v.position.y = y0; v.tex_coord.x = u0; v.tex_coord.y = v0; V[nv++] = v;
            v.position.x = x1; v.position.y = y0; v.tex_coord.x = u1; v.tex_coord.y = v0; V[nv++] = v;
            v.position.x = x1; v.position.y = y1; v.tex_coord.x = u1; v.tex_coord.y = v1; V[nv++] = v;
            v.position.x = x0; v.position.y = y1; v.tex_coord.x = u0; v.tex_coord.y = v1; V[nv++] = v;
            I[ni++] = b0; I[ni++] = b0 + 1; I[ni++] = b0 + 2;
            I[ni++] = b0; I[ni++] = b0 + 2; I[ni++] = b0 + 3;
        }
        pen += g->xadvance * k;
    }
    bindTexture(g_defTex);
}

// Draw a C string from the 8x8 bitmap font, each lit pixel a `s`x`s` quad.
void drawText(float x, float y, const char* str, float s, const unsigned char c[4]) {
    if (!str) return;
    if (g_ttf && (!g_toOverlay || g_novSpans < 30))
        if (okay::TtfFont* f = fontatlas::FallbackFont(str)) { drawTextWith(f, x, y, str, s, c); return; }
    if (g_ttf) {   // TrueType: one textured quad per glyph from the atlas (same texture as shapes)
        const float px = fH() * s;
        const fontatlas::Size& z = g_ttf->Pick(px);
        const float k = px / z.px;
        const bool exact = k > 0.97f && k < 1.03f;
        float pen = exact ? SDL_floorf(x + 0.5f) : x;
        const float base = exact ? SDL_floorf(y + z.ascent * k + 0.5f) : y + z.ascent * k;
        SDL_Vertex* V = g_toOverlay ? g_ovVerts : g_verts;
        int*        I = g_toOverlay ? g_ovIdx   : g_idx;
        int&        nv = g_toOverlay ? g_onv : g_nv;
        int&        ni = g_toOverlay ? g_oni : g_ni;
        const SDL_Color sc = toColor(c);
        for (const char* p = str; *p;) {
            const unsigned cp = fontatlas::NextCodepoint(&p);
            const fontatlas::Glyph* g = z.Find(cp); if (!g) g = z.Find('?');
            if (!g) continue;
            if (g->x1 > g->x0 && nv + 4 <= kMaxVerts && ni + 6 <= kMaxIdx) {
                const float x0 = pen + g->x0 * k, y0 = base + g->y0 * k, x1 = pen + g->x1 * k, y1 = base + g->y1 * k;
                const int b0 = nv;
                SDL_Vertex v; v.color = sc;
                v.position.x = x0; v.position.y = y0; v.tex_coord.x = g->u0; v.tex_coord.y = g->v0; V[nv++] = v;
                v.position.x = x1; v.position.y = y0; v.tex_coord.x = g->u1; v.tex_coord.y = g->v0; V[nv++] = v;
                v.position.x = x1; v.position.y = y1; v.tex_coord.x = g->u1; v.tex_coord.y = g->v1; V[nv++] = v;
                v.position.x = x0; v.position.y = y1; v.tex_coord.x = g->u0; v.tex_coord.y = g->v1; V[nv++] = v;
                I[ni++] = b0; I[ni++] = b0 + 1; I[ni++] = b0 + 2; I[ni++] = b0; I[ni++] = b0 + 2; I[ni++] = b0 + 3;
            }
            pen += g->advance * k;
        }
        return;
    }
    float penX = x;
    for (const char* p = str; *p; ++p) {
        for (int gy = 0; gy < fH(); ++gy)
            for (int gx = 0; gx < fW(); ++gx)
                if (fPix(*p, gx, gy))
                    quad(penX + gx * s, y + gy * s, s, s, c);
        penX += fW() * s;
    }
}

// ---- Launcher-look shapes (drawn instead of text glyphs for icons) ----
// A small chevron pointing right (open=false) or down (open=true), centred on (cx, cy).
void chevron(float cx, float cy, float sz, bool down, const unsigned char c[4]) {
    const float h = sz * 0.5f, t = sz < 12.0f ? 1.6f : 2.0f;
    if (down) { line(cx - h, cy - h * 0.5f, cx, cy + h * 0.5f, t, c); line(cx, cy + h * 0.5f, cx + h, cy - h * 0.5f, t, c); }
    else      { line(cx - h * 0.5f, cy - h, cx + h * 0.5f, cy, t, c); line(cx + h * 0.5f, cy, cx - h * 0.5f, cy + h, t, c); }
}
// An "x" mark centred on (cx, cy).
void xMark(float cx, float cy, float sz, const unsigned char c[4]) {
    const float h = sz * 0.5f;
    line(cx - h, cy - h, cx + h, cy + h, 1.8f, c);
    line(cx - h, cy + h, cx + h, cy - h, 1.8f, c);
}
// A tick inside a box at (x, y) of side `sz`.
void tickMark(float x, float y, float sz, const unsigned char c[4]) {
    const float t = sz < 18.0f ? 2.0f : 2.4f;
    line(x + sz * 0.24f, y + sz * 0.52f, x + sz * 0.43f, y + sz * 0.70f, t, c);
    line(x + sz * 0.43f, y + sz * 0.70f, x + sz * 0.77f, y + sz * 0.32f, t, c);
}
// Soft drop shadow under a rounded rect (a few widening translucent layers).
void softShadow(float x, float y, float w, float h, float rad) {
    for (int i = 4; i >= 1; --i) {
        const float g = 3.0f * i;
        const unsigned char c[4] = {0, 0, 0, (unsigned char)(26 - i * 4)};
        rrect(x - g, y - g + 5.0f, w + 2.0f * g, h + 2.0f * g, rad + g, c);
    }
}
// `c` with its alpha scaled (0..1), into `out`.
const unsigned char* withAlpha(const unsigned char c[4], float a, unsigned char out[4]) {
    out[0] = c[0]; out[1] = c[1]; out[2] = c[2]; out[3] = (unsigned char)(c[3] * a);
    return out;
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
    g_rPressed  = in.rightDown && !g_rPrevDown;
    g_rPrevDown = in.rightDown;
    g_openPopupThisFrame = 0;
    g_popupTop = 0;
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
    g_onv = 0; g_oni = 0; g_toOverlay = false; g_novSpans = 0;
    g_inMenuBar = false; g_curMenu.active = false;
    g_idSeed = 0; g_idTop = 0;        // reset the ID stack each frame
    // Restore any colors left pushed by an imbalanced previous frame, then clear.
    PopStyleColor(g_colTop);
    g_colTop = 0;
    g_nextItemW = -1.0f; g_itemWTop = 0;   // reset item-width overrides
    g_nspans = 0; g_clipTop = 0;           // reset draw spans/stack
    g_curClipOn = false; g_curTex = g_defTex;
    g_childTop = 0;                        // reset child-region stack
    g_col.active = false;                  // reset column layout
    g_tbl.active = false;                  // reset table layout
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
    if (g_active == id && inside) bg = g_look ? g_theme.accent : g_theme.bgDown;
    else if (inside)             bg = g_theme.bgHover;

    // Rounded border frame, then the inset fill.
    const float rd = rnd();
    rrect(x, y, w, h, rd, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) rrect(x + b, y + b, w - 2.0f * b, h - 2.0f * b, rd > b ? rd - b : 0.0f, bg);

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
    const float rd = rnd();
    rrect(x, y, w, h, rd, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) rrect(x + b, y + b, w - 2.0f * b, h - 2.0f * b, rd > b ? rd - b : 0.0f, g_theme.panel);
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

    // Rounded box frame + fill.
    const float rd = rnd() * 0.6f;
    rrect(x, y, size, size, rd, g_theme.border);
    const float b = g_theme.borderPx;
    if (size > 2.0f * b) rrect(x + b, y + b, size - 2.0f * b, size - 2.0f * b, rd > b ? rd - b : 0.0f, bg);
    // Check mark: a centered accent square when ticked (launcher look: an accent box with a tick).
    if (*value && g_look) {
        rrect(x, y, size, size, rd, g_theme.accent);
        const unsigned char white[4] = {255, 255, 255, 255};
        tickMark(x, y, size, white);
    } else if (*value) {
        const float pad = size * 0.28f;
        rrect(x + pad, y + pad, size - 2.0f * pad, size - 2.0f * pad, rd * 0.5f, g_theme.accent);
    }
    // Label to the right, vertically centered against the box.
    if (label && *label) {
        const float s  = g_theme.textScale;
        const float th = fH() * s;
        drawText(x + size + 8.0f, y + (size - th) * 0.5f, label, s, g_theme.text);
    }
    return changed;
}

bool ToggleSwitch(int id, float x, float y, float w, float h, const char* label, bool* value) {
    if (!value || w <= 0.0f || h <= 0.0f) return false;
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
    if (inside) g_hot = id;

    bool changed = false;
    if (g_active == id) {
        if (g_released) { if (inside) { *value = !*value; changed = true; } g_active = 0; }
    } else if (inside && g_pressed) {
        g_active = id;
    }

    // Pill track: dim when off, accent when on; knob slides between the two ends.
    const float rd = h * 0.5f;
    const unsigned char* track = *value ? g_theme.accent : g_theme.bg;
    rrect(x, y, w, h, rd, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) rrect(x + b, y + b, w - 2.0f * b, h - 2.0f * b, rd > b ? rd - b : 0.0f, track);
    const float pad = h * 0.14f;
    const float kd  = h - 2.0f * pad;                          // knob diameter
    const float kx  = *value ? (x + w - pad - kd) : (x + pad); // slid to the on/off end
    const unsigned char* knob = (inside || g_active == id) ? g_theme.text : g_theme.bgHover;
    rrect(kx, y + pad, kd, kd, kd * 0.5f, knob);
    // Label to the right of the switch, vertically centered.
    if (label && *label) {
        const float s = g_theme.textScale, th = fH() * s;
        drawText(x + w + 8.0f, y + (h - th) * 0.5f, label, s, g_theme.text);
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

    // Rounded groove, then the filled portion, then a circular handle.
    const float t = clamp01((*value - minV) / (maxV - minV));
    if (g_look) {   // thin field-coloured groove, accent fill, white knob
        const float gy2 = y + h * 0.5f - 2.5f;
        rrect(x, gy2, w, 5.0f, 2.5f, g_theme.field);
        if (w * t > 0.0f) rrect(x, gy2, w * t, 5.0f, 2.5f, g_theme.accent);
        const float kr = h * 0.32f;
        float kcx = x + w * t;
        if (kcx < x + kr) kcx = x + kr;
        if (kcx > x + w - kr) kcx = x + w - kr;
        const unsigned char ring[4] = {0, 0, 0, 60};
        fillCircle(kcx, y + h * 0.5f + 1.0f, kr + 1.0f, ring);
        const unsigned char knob[4] = {245, 247, 252, 255};
        fillCircle(kcx, y + h * 0.5f, kr, (g_active == id || inside) ? g_theme.text : knob);
        return changed;
    }
    const float gy = y + h * 0.5f - 3.0f;     // 6px groove, vertically centered
    rrect(x, gy, w, 6.0f, 3.0f, g_theme.track);
    if (w * t > 0.0f) rrect(x, gy, w * t, 6.0f, 3.0f, g_theme.accent);
    const float hr = h * 0.42f;                // handle radius
    float hcx = x + w * t;
    if (hcx < x + hr) hcx = x + hr;
    if (hcx > x + w - hr) hcx = x + w - hr;
    const unsigned char* hc = (g_active == id || inside) ? g_theme.text : g_theme.accent;
    fillCircle(hcx, y + h * 0.5f, hr, g_theme.border);
    fillCircle(hcx, y + h * 0.5f, hr - 1.5f, hc);
    return changed;
}

void ProgressBar(float x, float y, float w, float h, float t) {
    t = clamp01(t);
    if (g_look) {   // a pill: field track, accent fill
        const float bh = h > 10.0f ? 10.0f : h, by = y + (h - bh) * 0.5f;
        rrect(x, by, w, bh, bh * 0.5f, g_theme.field);
        if (w * t >= bh * 0.5f) rrect(x, by, w * t, bh, bh * 0.5f, g_theme.accent);
        return;
    }
    const float rd = rnd();
    rrect(x, y, w, h, rd, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) {
        const float ir = rd > b ? rd - b : 0.0f;
        rrect(x + b, y + b, w - 2.0f * b, h - 2.0f * b, ir, g_theme.track);
        const float iw = (w - 2.0f * b) * t;
        if (iw > 0.0f) rrect(x + b, y + b, iw, h - 2.0f * b, ir, g_theme.accent);
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

    // Circular radio (distinct from the square checkbox).
    const float cx = x + size * 0.5f, cy = y + size * 0.5f, rr = size * 0.5f;
    fillCircle(cx, cy, rr, g_theme.border);
    fillCircle(cx, cy, rr - g_theme.borderPx, bg);
    // Selected indicator: a smaller centered accent dot.
    if (*value == option) fillCircle(cx, cy, size * 0.22f, g_theme.accent);
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
    if (g_look) {   // launcher pill tabs: accent when selected, faint white otherwise
        unsigned char tmp[4];
        const unsigned char white[4] = {255, 255, 255, 255};
        const unsigned char* fill = selected ? g_theme.accent : withAlpha(white, inside ? 0.10f : 0.05f, tmp);
        rrect(x, y, w, h, h * 0.5f, fill);
        if (label && *label) {
            const float s = g_theme.textScale, tw = fMeasure(label) * s, th = fH() * s;
            drawText(x + (w - tw) * 0.5f, y + (h - th) * 0.5f, label, s, g_theme.text);
        }
        return *current == index;
    }
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

    // Rounded box (focused gets an accent border hint).
    const float rd = rnd();
    rrect(x, y, w, h, rd, focused ? g_theme.accent : g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) rrect(x + b, y + b, w - 2.0f * b, h - 2.0f * b, rd > b ? rd - b : 0.0f, g_theme.field);

    // Show the trailing characters that fit (so the caret stays visible) — this
    // avoids needing a scissor rect across the batched geometry.
    const float s     = g_theme.textScale;
    const float th    = fH() * s;
    const float pad   = g_look ? 9.0f : 6.0f;
    const float innerW = w - 2.0f * pad;
    const char* shown  = buf;   // drop leading characters until the rest fits
    while (*shown && fMeasure(shown) * s > innerW) {
        ++shown;
        while ((*shown & 0xC0) == 0x80) ++shown;   // whole UTF-8 characters only
    }
    const float tx = x + pad, ty = y + (h - th) * 0.5f;
    drawText(tx, ty, shown, s, g_theme.text);

    // Blinking caret at the end of the shown text when focused.
    if (focused && (g_frame / 30u) % 2u == 0u) {
        const float cx = tx + fMeasure(shown) * s;
        if (cx < x + w - 2.0f) quad(cx, ty, 2.0f, th, g_theme.text);
    }
    return changed;
}

namespace {
// Append the overlay batch after the main batch so popups/tooltips draw on top.
void mergeOverlay() {
    if (g_oni > 0) { setTexture(g_defTex); pushClipSpan(false, SDL_Rect{ 0, 0, 0, 0 }); }  // overlay: no clip/tex
    const int base = g_nv;
    for (int i = 0; i < g_onv && g_nv < kMaxVerts; ++i) g_verts[g_nv++] = g_ovVerts[i];
    int sp = 0;
    for (int j = 0; j < g_oni && g_ni < kMaxIdx; ++j) {
        while (sp < g_novSpans && g_ovSpans[sp].startIdx == j) setTexture(g_ovSpans[sp++].tex);
        g_idx[g_ni++] = base + g_ovIdx[j];
    }
    g_onv = 0; g_oni = 0; g_novSpans = 0;
}
// End-of-frame interaction bookkeeping, shared by every backend.
void finalizeFrame() {
    if (g_released) g_active = 0;                    // clear if the active item wasn't drawn
    if (g_pressed && !g_focusClaimed) g_focus = 0;   // click on empty space drops keyboard focus
    // A floating chip follows the cursor while dragging (drawn in the overlay so it's
    // on top). Any release ends the drag (a DropTarget already consumed it if hit).
    if (g_dd.active) {
        g_toOverlay = true;
        const unsigned char* ac = g_theme.accent;
        fillCircle(g_in.mouseX + 10.0f, g_in.mouseY + 10.0f, 7.0f, ac);
        g_toOverlay = false;
    }
    if (!g_in.mouseDown) { g_dd.active = false; g_dd.armed = false; }
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

// The real texture behind a span's texture (kAtlasTex -> this renderer's atlas).
SDL_Texture* resolveTex(SDL_Renderer* r, SDL_Texture* t) {
    for (int i = 0; i < g_nfb; ++i)
        if (t == (SDL_Texture*)(void*)g_fbFonts[i]) return fontatlas::FallbackTexture(r, g_fbFonts[i]);
    if (t != kAtlasTex) return t;
    if (!g_ttf || !g_ttf->ok) return nullptr;
    if (!g_atlasTex || g_atlasR != r) {
        g_atlasTex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, g_ttf->w, g_ttf->h);
        g_atlasR = r;
        if (g_atlasTex) {
            SDL_UpdateTexture(g_atlasTex, nullptr, g_ttf->rgba, g_ttf->w * 4);
            SDL_SetTextureBlendMode(g_atlasTex, SDL_BLENDMODE_BLEND);
#if SDL_VERSION_ATLEAST(2, 0, 12)
            SDL_SetTextureScaleMode(g_atlasTex, SDL_ScaleModeLinear);
#endif
        }
    }
    return g_atlasTex;
}

// Toasts: a few short messages stacked in the bottom-right corner (see Toast()).
struct ToastSlot { char text[160]; int kind; Uint32 until; Uint32 born; };
ToastSlot g_toasts[4];
int       g_ntoasts = 0;
void drawToasts(float screenW, float screenH) {
    const Uint32 now = SDL_GetTicks();
    int k = 0;
    for (int i = 0; i < g_ntoasts; ++i) if (g_toasts[i].until > now) g_toasts[k++] = g_toasts[i];
    g_ntoasts = k;
    if (!g_ntoasts) return;
    const float s = g_theme.textScale, pad = 14.0f, gap = 10.0f;
    float y = screenH - 24.0f;
    g_toOverlay = true;
    for (int i = g_ntoasts - 1; i >= 0; --i) {
        const ToastSlot& t = g_toasts[i];
        const float tw = labelW(t.text), w = tw + pad * 2.0f + 22.0f, h = textH() + pad * 1.6f;
        const float age = (float)(now - t.born) / 180.0f, left = (float)(t.until - now) / 250.0f;
        const float a = clamp01(age < left ? age : left);   // fade in / out
        const float x = screenW - 24.0f - w + (1.0f - a) * 30.0f;
        y -= h;
        const unsigned char bgc[4] = {38, 42, 51, (unsigned char)(245 * a)};
        const unsigned char rim[4] = {255, 255, 255, (unsigned char)(28 * a)};
        unsigned char dot[4] = {66, 150, 250, (unsigned char)(255 * a)};
        if (t.kind == 1) { dot[0] = 67; dot[1] = 181; dot[2] = 129; }
        if (t.kind == 2) { dot[0] = 237; dot[1] = 85; dot[2] = 85; }
        const unsigned char txt[4] = {g_theme.text[0], g_theme.text[1], g_theme.text[2], (unsigned char)(255 * a)};
        rrect(x, y, w, h, 10.0f, rim);
        rrect(x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, 9.0f, bgc);
        fillCircle(x + pad + 4.0f, y + h * 0.5f, 4.0f, dot, 16);
        drawText(x + pad + 18.0f, y + (h - textH()) * 0.5f, t.text, s, txt);
        y -= gap;
    }
    g_toOverlay = false;
}

void EndFrame(SDL_Renderer* r) {
    if (r && g_ntoasts) { int ow = 0, oh = 0; SDL_GetRendererOutputSize(r, &ow, &oh); drawToasts((float)ow, (float)oh); }
    finalizeFrame();
    if (!r || g_nv == 0 || g_ni == 0) return;
    SDL_Texture* const defTex = resolveTex(r, g_defTex);
    // Save/restore the renderer's blend mode so we never disturb ImGui or anything
    // else that draws through the same SDL_Renderer.
    SDL_BlendMode prev = SDL_BLENDMODE_NONE;
    SDL_GetRenderDrawBlendMode(r, &prev);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    if (g_nspans == 0) {
        SDL_RenderGeometry(r, defTex, g_verts, g_nv, g_idx, g_ni);   // fast path: no clipping
    } else {
        // Draw each contiguous index range under its clip rect. Ranges run from one
        // span's startIdx to the next span's (last extends to g_ni).
        SDL_bool hadClip = SDL_RenderIsClipEnabled(r);
        SDL_Rect savedClip; SDL_RenderGetClipRect(r, &savedClip);
        // Geometry before the first span (window frames drawn pre-clip) is unclipped.
        if (g_spans[0].startIdx > 0) {
            SDL_RenderSetClipRect(r, nullptr);
            SDL_RenderGeometry(r, defTex, g_verts, g_nv, &g_idx[0], g_spans[0].startIdx);
        }
        for (int s = 0; s < g_nspans; ++s) {
            int from = g_spans[s].startIdx;
            int to   = (s + 1 < g_nspans) ? g_spans[s + 1].startIdx : g_ni;
            if (to <= from) continue;
            if (g_spans[s].on) SDL_RenderSetClipRect(r, &g_spans[s].rect);
            else               SDL_RenderSetClipRect(r, nullptr);
            SDL_RenderGeometry(r, resolveTex(r, g_spans[s].tex), g_verts, g_nv, &g_idx[from], to - from);
        }
        // Restore the renderer's prior clip state.
        SDL_RenderSetClipRect(r, hadClip ? &savedClip : nullptr);
    }
    SDL_SetRenderDrawBlendMode(r, prev);
}

Theme& Style() { return g_theme; }

Theme SlateTheme() {
    // The launcher's "Slate" palette (shared/OkayTheme.hpp) in bytes.
    Theme t;
    auto set = [](unsigned char* c, int r, int g, int b, int a) { c[0] = (unsigned char)r; c[1] = (unsigned char)g; c[2] = (unsigned char)b; c[3] = (unsigned char)a; };
    set(t.bg, 51, 56, 71, 255);        // kControl
    set(t.bgHover, 67, 72, 86, 255);
    set(t.bgDown, 45, 50, 64, 255);
    set(t.border, 255, 255, 255, 22);  // hairline
    set(t.text, 230, 235, 245, 255);
    set(t.panel, 30, 33, 41, 250);     // kBase
    set(t.track, 41, 46, 59, 255);     // kField
    set(t.accent, 66, 150, 250, 255);
    set(t.textDim, 140, 150, 171, 255);
    set(t.titleBg, 23, 25, 31, 255);
    set(t.field, 41, 46, 59, 255);
    t.borderPx = 1.0f; t.textScale = 2.0f; t.rounding = 6.0f;
    t.windowRounding = 10.0f; t.windowPadding = 14.0f; t.windowShadow = true;
    return t;
}

void UseLauncherLook(bool on) {
    static Theme classic;
    if (on == g_look) return;
    if (on) {
        classic = g_theme;
        g_theme = SlateTheme();
        const fontatlas::Atlas& a = fontatlas::Get();
        g_ttf = a.ok ? &a : nullptr;
        if (g_ttf) { g_defTex = kAtlasTex; g_whiteU = a.whiteU; g_whiteV = a.whiteV; }
    } else {
        g_theme = classic;
        g_ttf = nullptr; g_defTex = nullptr; g_whiteU = g_whiteV = 0.0f;
    }
    g_look = on;
    g_curTex = g_defTex;
}
bool LauncherLook() { return g_look; }

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

static bool g_pinNext = false;   // PinNextWindow: the next Begin sits where it is told
void PinNextWindow() { g_pinNext = true; }

bool Begin(const char* title, float x, float y, float w, float h, bool* p_open) {
    g_lay.seed = 0;                    // hash the window id from a FIXED seed so it (and
    const int id = hashLabel(title);   // every widget id under it) is stable across frames
    WinSlot* slot = nullptr;
    for (WinSlot& ws : g_wins) if (ws.used && ws.id == id) { slot = &ws; break; }
    if (!slot) for (WinSlot& ws : g_wins) if (!ws.used) { ws.used = true; ws.id = id; ws.x = x; ws.y = y; slot = &ws; break; }
    const bool pinned = g_pinNext;
    g_pinNext = false;
    if (pinned && slot) { slot->x = x; slot->y = y; }
    float wx = slot ? slot->x : x, wy = slot ? slot->y : y;
    if (pinned) { wx = x; wy = y; }
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
        else if (slot && !pinned) { slot->x += g_mouseDX; slot->y += g_mouseDY; wx = slot->x; wy = slot->y; }
    } else if (overDrag && g_pressed) {
        g_active = id; g_focusClaimed = true;
    }

    // Frame: panel body (only the title bar when collapsed) + accent underline + title.
    const float bodyH = *collapsed ? titleH : h;
    if (g_look) {   // launcher window: shadow, rounded body with a hairline, slim title bar
        const float wr = g_theme.windowRounding;
        if (g_theme.windowShadow) softShadow(wx, wy, w, bodyH, wr);
        rrect(wx, wy, w, bodyH, wr, g_theme.border);
        rrect(wx + 1.0f, wy + 1.0f, w - 2.0f, bodyH - 2.0f, wr > 1.0f ? wr - 1.0f : 0.0f, g_theme.panel);
        rrect(wx + 1.0f, wy + 1.0f, w - 2.0f, titleH - 1.0f, wr > 1.0f ? wr - 1.0f : 0.0f, g_theme.titleBg);
        if (!*collapsed && titleH > wr) quad(wx + 1.0f, wy + wr, w - 2.0f, titleH - wr, g_theme.titleBg);
        if (!*collapsed) quad(wx + 1.0f, wy + titleH - 1.0f, w - 2.0f, 1.0f, g_theme.border);
        const float s0 = g_theme.textScale;
        chevron(wx + btn * 0.5f, wy + titleH * 0.5f, 9.0f, !*collapsed, overCaret ? g_theme.text : g_theme.textDim);
        if (title && *title) drawText(wx + btn, wy + (titleH - textH()) * 0.5f, title, s0, g_theme.text);
        if (p_open) {
            unsigned char tmp[4];
            const unsigned char white[4] = {255, 255, 255, 255};
            if (overClose) rrect(closeX + 5.0f, wy + 5.0f, btn - 10.0f, titleH - 10.0f, 6.0f, withAlpha(white, 0.10f, tmp));
            xMark(closeX + btn * 0.5f, wy + titleH * 0.5f, 9.0f, overClose ? g_theme.text : g_theme.textDim);
        }
    } else
    Panel(wx, wy, w, bodyH);
    if (!g_look) {
    // Title bar: round the TOP corners to match the panel, square at the bottom edge
    // (drawn as a rounded rect, then a square strip fills its lower half flush to body).
    const float rd = rnd();
    rrect(wx, wy, w, titleH, rd, g_theme.titleBg);
    if (titleH > rd) quad(wx, wy + rd, w, titleH - rd, g_theme.titleBg);
    quad(wx, wy + titleH - 2.0f, w, 2.0f, g_theme.accent);
    const float s = g_theme.textScale;
    drawText(wx + 8.0f, wy + (titleH - textH()) * 0.5f, *collapsed ? "+" : "-", s, g_theme.text);  // caret
    if (title && *title) drawText(wx + 8.0f + fCharW() * s * 2.0f, wy + (titleH - textH()) * 0.5f, title, s, g_theme.text);
    if (p_open) {
        const unsigned char* xcol = overClose ? g_theme.accent : g_theme.text;
        drawText(closeX + (btn - fCharW() * s) * 0.5f, wy + (titleH - textH()) * 0.5f, "x", s, xcol);
    }
    }

    if (*collapsed) { g_lay.active = false; return false; }   // folded: skip content

    // Establish the content layout region.
    const float pad = g_theme.windowPadding;
    if (g_look) { PushClipRect(wx + 1.0f, wy + titleH, w - 2.0f, h - titleH - 1.0f); g_lay.winClip = true; }
    g_lay.active = true;
    g_lay.seed = id;
    g_lay.ox = wx + pad;
    g_lay.oy = wy + titleH + pad;
    g_lay.cx = g_lay.ox;
    g_lay.cy = g_lay.oy;
    g_lay.contentW = w - 2.0f * pad;
    g_lay.spacingX = g_look ? 9.0f : 8.0f; g_lay.spacingY = g_look ? 8.0f : 6.0f;
    g_lay.indent = 0.0f;
    g_lay.pendingSameLine = false;
    g_lay.prevX = g_lay.ox; g_lay.prevY = g_lay.oy; g_lay.prevW = 0.0f; g_lay.prevH = 0.0f;
    return true;
}

void End() {
    if (g_lay.winClip) { PopClipRect(); g_lay.winClip = false; }
    g_lay.active = false;
}

bool BeginChild(const char* id, float w, float h, bool border) {
    if (!g_lay.active) return false;
    const int cid = hashLabel(id);
    const float pad = 6.0f;
    const float rw = w > 0.0f ? w : availW();
    const float rh = h > 0.0f ? h : rowH() * 4.0f;
    float rx, ry; place(rw, rh, rx, ry);

    float* scroll  = floatState(cid, 0.0f);
    float* maxSlot = floatState(cid ^ 0x4d61, 0.0f);   // last frame's max scroll
    const bool hovered = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, rx, ry, rw, rh);
    if (hovered && g_in.wheel != 0.0f) *scroll -= g_in.wheel * rowH();   // wheel scrolls
    // Clamp with the previous frame's content extent so layout uses a sane offset.
    if (*scroll < 0.0f) *scroll = 0.0f;
    if (*scroll > *maxSlot) *scroll = *maxSlot;

    // Frame + clip to the region.
    if (border) { quad(rx, ry, rw, rh, g_theme.panel); }
    PushClipRect(rx, ry, rw, rh);

    // Save the parent layout, then set up the child's inner layout (scrolled).
    if (g_childTop < 8) {
        ChildSave& c = g_childStack[g_childTop++];
        c.lay = g_lay; c.rx = rx; c.ry = ry; c.rw = rw; c.rh = rh;
        c.top = ry + pad; c.scroll = scroll; c.maxSlot = maxSlot; c.pad = pad; c.hovered = hovered;
    }
    g_lay.ox = rx + pad;
    g_lay.contentW = rw - 2.0f * pad - kScrollbarW;
    g_lay.oy = ry + pad - *scroll;
    g_lay.cx = g_lay.ox;
    g_lay.cy = g_lay.oy;
    g_lay.indent = 0.0f;
    g_lay.seed = cid;                   // child-local widget id namespace
    g_lay.pendingSameLine = false;
    g_lay.prevX = g_lay.ox; g_lay.prevY = g_lay.oy; g_lay.prevW = 0.0f; g_lay.prevH = 0.0f;
    return true;
}

void EndChild() {
    if (g_childTop <= 0) return;
    ChildSave& c = g_childStack[--g_childTop];
    // Content height = how far the cursor advanced from the (scrolled) top, in
    // unscrolled space. cy currently sits at top - scroll + contentHeight.
    const float contentH = g_lay.cy - (c.top - *c.scroll);
    const float viewH = c.rh - 2.0f * c.pad;
    float maxScroll = contentH - viewH; if (maxScroll < 0.0f) maxScroll = 0.0f;
    *c.maxSlot = maxScroll;                     // remember for next frame's clamp
    if (*c.scroll < 0.0f) *c.scroll = 0.0f;
    if (*c.scroll > maxScroll) *c.scroll = maxScroll;

    PopClipRect();   // stop clipping before drawing the scrollbar

    // Scrollbar (only when content overflows).
    if (maxScroll > 0.0f) {
        const float sbx = c.rx + c.rw - kScrollbarW;
        if (!g_look) quad(sbx, c.ry, kScrollbarW, c.rh, g_theme.track);
        float frac = viewH / contentH; if (frac > 1.0f) frac = 1.0f;
        float thumbH = frac * c.rh; if (thumbH < 16.0f) thumbH = 16.0f;
        float t = maxScroll > 0.0f ? (*c.scroll / maxScroll) : 0.0f;
        float thumbY = c.ry + t * (c.rh - thumbH);
        // Drag the thumb.
        const int sbId = (int)((unsigned)c.lay.seed ^ 0x5cb0u ^ (unsigned)(sbx * 7.0f)) & 0x7fffffff;
        const bool overThumb = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, sbx, thumbY, kScrollbarW, thumbH);
        if (overThumb) g_hot = sbId;
        if (g_active == sbId) {
            if (!g_in.mouseDown) g_active = 0;
            else if (c.rh - thumbH > 0.0f) *c.scroll += g_mouseDY * (maxScroll / (c.rh - thumbH));
        } else if (overThumb && g_pressed) { g_active = sbId; g_focusClaimed = true; }
        if (*c.scroll < 0.0f) *c.scroll = 0.0f;
        if (*c.scroll > maxScroll) *c.scroll = maxScroll;
        if (g_look) {   // thin rounded thumb, faint until hovered
            unsigned char tmp[4];
            const unsigned char white[4] = {255, 255, 255, 255};
            const unsigned char* tc2 = g_active == sbId ? g_theme.accent : withAlpha(white, overThumb ? 0.26f : 0.14f, tmp);
            rrect(sbx + 3.0f, thumbY + 2.0f, kScrollbarW - 5.0f, thumbH - 4.0f, (kScrollbarW - 5.0f) * 0.5f, tc2);
        } else {
            const unsigned char* tc = (g_active == sbId || overThumb) ? g_theme.text : g_theme.accent;
            quad(sbx + 1.0f, thumbY, kScrollbarW - 2.0f, thumbH, tc);
        }
    }

    g_lay = c.lay;   // restore the parent layout (cursor already past the region)
}

void SameLine(float spacing) {
    if (!g_lay.active) return;
    g_lay.pendingSameLine = true;
    g_lay.sameLineSpacing = spacing;
}

void SetNextItemWidth(float w) { g_nextItemW = w; }
void PushItemWidth(float w)    { if (g_itemWTop < 16) g_itemWStack[g_itemWTop++] = w; }
void PopItemWidth()            { if (g_itemWTop > 0) --g_itemWTop; }

void PushClipRect(float x, float y, float w, float h) {
    SDL_Rect r; r.x = (int)x; r.y = (int)y; r.w = (int)(w < 0 ? 0 : w); r.h = (int)(h < 0 ? 0 : h);
    // Intersect with the enclosing clip so nested regions never draw outside a parent.
    if (g_clipTop > 0) {
        SDL_Rect& p = g_clipStack[g_clipTop - 1];
        int x0 = r.x > p.x ? r.x : p.x, y0 = r.y > p.y ? r.y : p.y;
        int x1 = (r.x + r.w) < (p.x + p.w) ? (r.x + r.w) : (p.x + p.w);
        int y1 = (r.y + r.h) < (p.y + p.h) ? (r.y + r.h) : (p.y + p.h);
        r.x = x0; r.y = y0; r.w = x1 > x0 ? x1 - x0 : 0; r.h = y1 > y0 ? y1 - y0 : 0;
    }
    if (g_clipTop < 16) g_clipStack[g_clipTop++] = r;
    pushClipSpan(true, r);
}

void PopClipRect() {
    if (g_clipTop > 0) --g_clipTop;
    if (g_clipTop > 0) pushClipSpan(true, g_clipStack[g_clipTop - 1]);
    else               pushClipSpan(false, SDL_Rect{ 0, 0, 0, 0 });
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

void TextColored(unsigned char r, unsigned char g, unsigned char b, const char* s) {
    if (!g_lay.active) return;
    const float h = rowH();
    float x, y; place(s && *s ? labelW(s) : 1.0f, h, x, y);
    const unsigned char col[4] = { r, g, b, 255 };
    if (s && *s) drawText(x, y + (h - textH()) * 0.5f, s, g_theme.textScale, col);
}

void TextDisabled(const char* s) {
    if (g_look) { TextColored(g_theme.textDim[0], g_theme.textDim[1], g_theme.textDim[2], s); return; }
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
        const float tw = labelW(lbl) + (g_look ? 28.0f : 20.0f);
        const unsigned uid = ((unsigned)hashLabel(lbl) ^ ((unsigned)i * 2654435761u)) & 0x7fffffffu;
        Tab((int)uid, tx, y, tw, h, lbl, current, i);
        tx += tw + (g_look ? 6.0f : 2.0f);
    }
    // A baseline under the whole tab row (classic look).
    if (!g_look) quad(x, y + h - 2.0f, fullW(), 2.0f, g_theme.border);
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

bool ToggleSwitch(const char* label, bool* value) {
    if (!g_lay.active || !value) return false;
    const float h = rowH();
    const float sh = textH() + 4.0f, sw = sh * 1.9f;   // switch pill size (track ~1.9:1)
    const float w = sw + 8.0f + labelW(label);
    float x, y; place(w, h, x, y);
    return ToggleSwitch(hashLabel(label), x, y + (h - sh) * 0.5f, sw, sh, label, value);
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

bool VSliderFloat(const char* label, float w, float h, float* value, float minV, float maxV) {
    if (!g_lay.active || !value) return false;
    const int id = hashLabel(label);
    if (w <= 0.0f) w = rowH();
    if (h <= 0.0f) h = rowH() * 3.0f;
    float x, y; place(w, h, x, y);
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
    if (inside) g_hot = id;
    bool changed = false;
    if (g_active == id) {
        if (!g_in.mouseDown) g_active = 0;
        else {
            // Top = maxV, bottom = minV (natural for a level fader).
            float t = 1.0f - clamp01((g_in.mouseY - y) / (h > 0 ? h : 1));
            float nv = minV + t * (maxV - minV);
            if (nv != *value) { *value = nv; changed = true; }
        }
    } else if (inside && g_pressed) { g_active = id; g_focusClaimed = true; }
    // Groove + fill from the bottom up to the value.
    quad(x, y, w, h, g_theme.track);
    float span = maxV - minV; float t = span != 0.0f ? clamp01((*value - minV) / span) : 0.0f;
    float fillH = t * h;
    quad(x, y + h - fillH, w, fillH, g_theme.accent);
    // Handle.
    float hy = y + h - fillH - 3.0f;
    quad(x, hy < y ? y : hy, w, 6.0f, (g_active == id || inside) ? g_theme.text : g_theme.bgHover);
    return changed;
}

bool Knob(const char* label, float* value, float minV, float maxV, float size) {
    if (!g_lay.active || !value) return false;
    const int id = hashLabel(label);
    const float d = size > 0.0f ? size : rowH() * 2.0f;
    const float lw = labelW(label) > 0.0f ? labelW(label) + 8.0f : 0.0f;
    float x, y; place(d + lw, d, x, y);
    const float cx = x + d * 0.5f, cy = y + d * 0.5f, rad = d * 0.5f - 2.0f;
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, d, d);
    if (inside) g_hot = id;
    bool changed = false;
    if (g_active == id) {
        if (!g_in.mouseDown) g_active = 0;
        else {
            // Drag up to increase (vertical drag is the common knob gesture).
            float span = maxV - minV;
            float nv = *value - g_mouseDY * span / 120.0f;   // 120px = full sweep
            nv = nv < minV ? minV : (nv > maxV ? maxV : nv);
            if (nv != *value) { *value = nv; changed = true; }
        }
    } else if (inside && g_pressed) { g_active = id; g_focusClaimed = true; }
    // Dial + indicator sweeping ~270 degrees (from 135 to 405 deg).
    fillCircle(cx, cy, rad, (g_active == id || inside) ? g_theme.bgHover : g_theme.track);
    fillCircle(cx, cy, rad * 0.82f, g_theme.panel);
    float span = maxV - minV; float t = span != 0.0f ? clamp01((*value - minV) / span) : 0.0f;
    const float a = 2.35619f + t * 4.71239f;   // 135deg + t*270deg (radians)
    line(cx, cy, cx + SDL_cosf(a) * rad, cy + SDL_sinf(a) * rad, 2.5f, g_theme.accent);
    if (lw > 0.0f) drawText(x + d + 8.0f, y + (d - textH()) * 0.5f, label, g_theme.textScale, g_theme.text);
    return changed;
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

void DrawTexture(SDL_Texture* tex, float x, float y, float w, float h,
                 unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    if (!tex) return;
    const unsigned char tint[4] = { r, g, b, a };
    texQuad(x, y, w, h, tex, 0.0f, 0.0f, 1.0f, 1.0f, tint);
}

void Image(SDL_Texture* tex, float w, float h,
           unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    if (!g_lay.active) return;
    if (w <= 0.0f) w = rowH() * 2.0f;
    if (h <= 0.0f) h = w;
    float x, y; place(w, h, x, y);
    DrawTexture(tex, x, y, w, h, r, g, b, a);
}

bool ImageButton(const char* id, SDL_Texture* tex, float w, float h) {
    if (!g_lay.active) return false;
    const int hid = hashLabel(id);
    if (w <= 0.0f) w = rowH() * 2.0f;
    if (h <= 0.0f) h = w;
    const float pad = 4.0f;
    float x, y; place(w + pad * 2.0f, h + pad * 2.0f, x, y);
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w + pad * 2, h + pad * 2);
    if (inside) g_hot = hid;
    bool clicked = false;
    if (g_active == hid) { if (g_released) { if (inside) clicked = true; g_active = 0; } }
    else if (inside && g_pressed) g_active = hid;
    const unsigned char* bg = (g_active == hid && inside) ? g_theme.bgDown : (inside ? g_theme.bgHover : g_theme.bg);
    quad(x, y, w + pad * 2, h + pad * 2, bg);
    DrawTexture(tex, x + pad, y + pad, w, h);
    return clicked;
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
    const float s = g_theme.textScale;
    if (g_look) {
        rrect(x, y, w, h, rnd(), inside ? g_theme.bgHover : g_theme.bg);
        chevron(x + 14.0f, y + h * 0.5f, 9.0f, *open, g_theme.textDim);
        drawText(x + 28.0f, y + (h - textH()) * 0.5f, label, s, g_theme.text);
        return *open;
    }
    quad(x, y, w, h, bg);
    drawText(x + 8.0f, y + (h - textH()) * 0.5f, *open ? "-" : "+", s, g_theme.text);   // expander glyph
    drawText(x + 8.0f + fCharW() * s * 2.0f, y + (h - textH()) * 0.5f, label, s, g_theme.text);
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
    if (g_look) {
        const float p2 = 8.0f;
        const unsigned char tip[4] = {38, 42, 51, 250};
        rrect(bx, by, tw + p2 * 2.0f, th + p2 * 2.0f, 7.0f, g_theme.border);
        rrect(bx + 1.0f, by + 1.0f, tw + p2 * 2.0f - 2.0f, th + p2 * 2.0f - 2.0f, 6.0f, tip);
        drawText(bx + p2, by + p2, text, s, g_theme.text);
        g_toOverlay = false;
        return;
    }
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
    if (g_look) chevron(x + boxW - 14.0f, y + h * 0.5f, 8.0f, true, g_theme.textDim);
    else drawText(x + boxW - fCharW() * s - 6.0f, y + (h - textH()) * 0.5f, *open ? "-" : "+", s, g_theme.text);
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

bool ColorPicker3(const char* label, float rgb[3]) {
    if (!g_lay.active || !rgb) return false;
    const int id = hashLabel(label);
    const float w = availW();
    const float barW = 18.0f, gap = 8.0f;
    float sq = w - barW - gap;                 // SV square is square-ish
    const float maxSq = rowH() * 6.0f;
    if (sq > maxSq) sq = maxSq;
    if (sq < 40.0f) sq = 40.0f;
    const float total = sq;
    float x, y; place(w, total, x, y);

    // Persist HSV across frames so hue survives S=0/V=0. Re-sync from rgb if it was
    // changed externally (differs from what we last wrote).
    float* H = floatState(id ^ 0x101, -1.0f);
    float* S = floatState(id ^ 0x102, 0.0f);
    float* V = floatState(id ^ 0x103, 0.0f);
    float* lr = floatState(id ^ 0x104, -1.0f);
    float* lg = floatState(id ^ 0x105, -1.0f);
    float* lb = floatState(id ^ 0x106, -1.0f);
    auto approxEq = [](float a, float b) { float d = a - b; return (d < 0 ? -d : d) < 0.004f; };
    if (*H < 0.0f || !approxEq(*lr, rgb[0]) || !approxEq(*lg, rgb[1]) || !approxEq(*lb, rgb[2])) {
        rgbToHsv((unsigned char)(clamp01(rgb[0]) * 255.0f), (unsigned char)(clamp01(rgb[1]) * 255.0f),
                 (unsigned char)(clamp01(rgb[2]) * 255.0f), *H, *S, *V);
    }

    bool changed = false;
    // --- Saturation/Value square (x = saturation, y = value inverted) ---
    unsigned char hueCol[3]; hsvToRgb(*H, 1.0f, 1.0f, hueCol);
    const unsigned char white[4] = {255,255,255,255}, black[4] = {0,0,0,255};
    const unsigned char hue4[4] = {hueCol[0], hueCol[1], hueCol[2], 255};
    gradQuad(x, y, sq, sq, white, hue4, black, black);   // white->hue across, ->black down
    const bool inSq = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, sq, sq);
    if (inSq && g_pressed) g_active = id;
    if (g_active == id) {
        if (!g_in.mouseDown) g_active = 0;
        else {
            *S = clamp01((g_in.mouseX - x) / sq);
            *V = 1.0f - clamp01((g_in.mouseY - y) / sq);
            changed = true;
        }
    }
    // SV cursor ring.
    float cxp = x + (*S) * sq, cyp = y + (1.0f - *V) * sq;
    fillCircle(cxp, cyp, 4.0f, (*V > 0.5f && *S < 0.5f) ? black : white);

    // --- Hue bar (vertical rainbow) ---
    const float hx = x + sq + gap;
    const int   segs = 6;
    for (int i = 0; i < segs; ++i) {
        unsigned char c0[3], c1[3];
        hsvToRgb((float)i / segs, 1.0f, 1.0f, c0);
        hsvToRgb((float)(i + 1) / segs, 1.0f, 1.0f, c1);
        const unsigned char t0[4] = {c0[0],c0[1],c0[2],255}, t1[4] = {c1[0],c1[1],c1[2],255};
        gradQuad(hx, y + sq * i / segs, barW, sq / segs, t0, t0, t1, t1);
    }
    const int hueId = id ^ 0x2a2a;
    const bool inBar = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, hx, y, barW, sq);
    if (inBar && g_pressed) g_active = hueId;
    if (g_active == hueId) {
        if (!g_in.mouseDown) g_active = 0;
        else { *H = clamp01((g_in.mouseY - y) / sq); changed = true; }
    }
    // Hue marker.
    float hy = y + (*H) * sq;
    quad(hx - 2.0f, hy - 1.5f, barW + 4.0f, 3.0f, g_theme.text);

    // Write the resolved color back and remember it for external-change detection.
    unsigned char outc[3]; hsvToRgb(*H, *S, *V, outc);
    rgb[0] = outc[0] / 255.0f; rgb[1] = outc[1] / 255.0f; rgb[2] = outc[2] / 255.0f;
    *lr = rgb[0]; *lg = rgb[1]; *lb = rgb[2];
    (void)label;
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
    if (g_look) {
        unsigned char tmp[4]; const unsigned char white[4] = {255, 255, 255, 255};
        if (inside) rrect(x, y, w, h, 6.0f, withAlpha(white, 0.06f, tmp));
        chevron(x + 11.0f, y + h * 0.5f, 8.0f, *open, g_theme.textDim);
        drawText(x + 24.0f, y + (h - textH()) * 0.5f, label, s, g_theme.text);
    } else {
    if (inside) quad(x, y, w, h, g_theme.bgHover);
    drawText(x + 4.0f, y + (h - textH()) * 0.5f, *open ? "v" : ">", s, g_theme.text);
    drawText(x + 4.0f + fCharW() * s * 1.5f, y + (h - textH()) * 0.5f, label, s, g_theme.text);
    }
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
    if (!g_look) quad(x, y, fullW(), h, g_theme.bgDown);
    else quad(x, y + h - 1.0f, fullW(), 1.0f, g_theme.border);
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
    if (g_look) {   // launcher rows: soft accent tint + bar when selected, faint white on hover
        unsigned char tmp[4];
        const unsigned char white[4] = {255, 255, 255, 255};
        if (selected) { rrect(x, y, w, h, 6.0f, withAlpha(g_theme.accent, 0.28f, tmp)); rrect(x, y + 5.0f, 3.0f, h - 10.0f, 1.5f, g_theme.accent); }
        else if (inside) rrect(x, y, w, h, 6.0f, withAlpha(white, 0.06f, tmp));
        drawText(x + 10.0f, y + (h - textH()) * 0.5f, label, s, g_theme.text);
        return clicked;
    }
    if (selected)      quad(x, y, w, h, g_theme.accent);
    else if (inside)   quad(x, y, w, h, g_theme.bgHover);
    drawText(x + 6.0f, y + (h - textH()) * 0.5f, label, s, g_theme.text);
    return clicked;
}

void OpenPopup(const char* id) {
    PopupSlot* p = popupSlot(hashLabel(id), true);
    if (p) { p->open = true; p->x = g_in.mouseX; p->y = g_in.mouseY; g_openPopupThisFrame = p->id; }
}

void CloseCurrentPopup() {
    if (g_popupTop > 0) { PopupSlot* p = popupSlot(g_popupStack[g_popupTop - 1].id, false); if (p) p->open = false; }
}

// Shared popup body setup: redirect layout into a floating overlay box at (px,py),
// drawing the background using last frame's measured height. Returns true if open.
static bool beginPopupBody(int id) {
    PopupSlot* p = popupSlot(id, false);
    if (!p || !p->open) return false;
    const float w = 160.0f, pad = 6.0f;
    float* hSlot = floatState(id ^ 0x9090, rowH() * 3.0f);
    float px = p->x, py = p->y;
    // Frame (overlay so it sits above everything). Height is last frame's content.
    g_toOverlay = true;
    Panel(px, py, w, *hSlot + pad * 2.0f);
    // Save parent layout; set up the popup's inner layout.
    if (g_popupTop < 4) {
        PopupCtx& c = g_popupStack[g_popupTop++];
        c.lay = g_lay; c.id = id; c.x = px; c.y = py; c.w = w; c.hSlot = hSlot;
    }
    g_lay.active = true;
    g_lay.ox = px + pad; g_lay.oy = py + pad;
    g_lay.cx = g_lay.ox; g_lay.cy = g_lay.oy;
    g_lay.contentW = w - 2.0f * pad;
    g_lay.indent = 0.0f; g_lay.seed = id;
    g_lay.pendingSameLine = false;
    g_lay.prevX = g_lay.ox; g_lay.prevY = g_lay.oy; g_lay.prevW = 0.0f; g_lay.prevH = 0.0f;
    return true;
}

void EndPopup() {
    if (g_popupTop <= 0) return;
    PopupCtx& c = g_popupStack[--g_popupTop];
    *c.hSlot = g_lay.cy - (c.y + 6.0f);   // measured content height for next frame
    g_toOverlay = false;
    // Dismiss on a left-click outside the popup box (but not the click that opened it).
    PopupSlot* p = popupSlot(c.id, false);
    if (p && p->open && g_pressed && g_openPopupThisFrame != c.id) {
        const bool inside = pointIn(g_in.mouseX, g_in.mouseY, c.x, c.y, c.w, *c.hSlot + 12.0f);
        if (!inside) p->open = false;
    }
    g_lay = c.lay;   // restore parent layout
}

bool BeginPopup(const char* id) { return beginPopupBody(hashLabel(id)); }

bool BeginPopupContextItem(const char* id) {
    const int pid = hashLabel(id);
    // Right-click on the last-issued widget opens the popup at the cursor.
    if (g_lay.active && g_rPressed && !g_in.blocked &&
        pointIn(g_in.mouseX, g_in.mouseY, g_lay.prevX, g_lay.prevY, g_lay.prevW, g_lay.prevH)) {
        PopupSlot* p = popupSlot(pid, true);
        if (p) { p->open = true; p->x = g_in.mouseX; p->y = g_in.mouseY; g_openPopupThisFrame = pid; }
    }
    return beginPopupBody(pid);
}

bool IsDragging() { return g_dd.active; }

bool DragSource(int payload) {
    if (!g_lay.active) return false;
    const float px = g_lay.prevX, py = g_lay.prevY, pw = g_lay.prevW, ph = g_lay.prevH;
    const bool overPrev = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, px, py, pw, ph);
    // Arm on a press inside the source widget.
    if (!g_dd.active && !g_dd.armed && g_pressed && overPrev) {
        g_dd.armed = true; g_dd.armPayload = payload;
        g_dd.armX = g_in.mouseX; g_dd.armY = g_in.mouseY;
    }
    // Promote to an active drag once the cursor has moved far enough.
    if (g_dd.armed && g_dd.armPayload == payload && g_in.mouseDown) {
        float dx = g_in.mouseX - g_dd.armX, dy = g_in.mouseY - g_dd.armY;
        if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) > 6.0f) {
            g_dd.active = true; g_dd.payload = payload; g_dd.armed = false;
        }
    }
    return g_dd.active && g_dd.payload == payload;
}

bool DropTarget(int* outPayload) {
    if (!g_lay.active || !g_dd.active) return false;
    const float px = g_lay.prevX, py = g_lay.prevY, pw = g_lay.prevW, ph = g_lay.prevH;
    const bool over = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, px, py, pw, ph);
    if (over) {
        // Highlight the drop location with an accent outline.
        quad(px, py, pw, 2.0f, g_theme.accent);
        quad(px, py + ph - 2.0f, pw, 2.0f, g_theme.accent);
        if (g_released) {
            if (outPayload) *outPayload = g_dd.payload;
            g_dd.active = false;
            return true;
        }
    }
    return false;
}

bool ListBox(const char* label, int* current, const char* const* items, int count, int visibleRows) {
    if (!g_lay.active || !current || !items) return false;
    if (visibleRows < 1) visibleRows = 1;
    bool changed = false;
    const float h = rowH() * visibleRows;
    if (BeginChild(label, 0.0f, h, true)) {
        for (int i = 0; i < count; ++i) {
            PushID(i);
            if (Selectable(items[i] ? items[i] : "", *current == i)) { *current = i; changed = true; }
            PopID();
        }
    }
    EndChild();
    // Draw the visible part of the label (before any "##") to the right of the box.
    if (label && *label && !(label[0] == '#' && label[1] == '#')) {
        SameLine();
        Text(label);
    }
    return changed;
}

// ---- Columns -------------------------------------------------------------------
void Columns(int count) {
    if (!g_lay.active || count < 1) return;
    if (count > 16) count = 16;
    g_col.active = true; g_col.count = count; g_col.index = 0;
    g_col.x0 = g_lay.ox; g_col.fullW = g_lay.contentW;
    g_col.rowTop = g_lay.cy; g_col.rowBottom = g_lay.cy;
    g_lay.ox = g_col.x0;
    g_lay.contentW = g_col.fullW / count - 6.0f;   // small gutter between columns
    g_lay.cy = g_col.rowTop;
}

void NextColumn() {
    if (!g_lay.active || !g_col.active) return;
    if (g_lay.cy > g_col.rowBottom) g_col.rowBottom = g_lay.cy;   // track the tallest column
    ++g_col.index;
    if (g_col.index >= g_col.count) {          // past the last column: start a new row
        g_col.index = 0;
        g_col.rowTop = g_col.rowBottom;
    }
    const float colW = g_col.fullW / g_col.count;
    g_lay.ox = g_col.x0 + g_col.index * colW;
    g_lay.contentW = colW - 6.0f;
    g_lay.cy = g_col.rowTop;
    g_lay.pendingSameLine = false;
}

void EndColumns() {
    if (!g_lay.active || !g_col.active) return;
    if (g_lay.cy > g_col.rowBottom) g_col.rowBottom = g_lay.cy;
    g_lay.ox = g_col.x0;
    g_lay.contentW = g_col.fullW;
    g_lay.cy = g_col.rowBottom;
    g_col.active = false;
}

// ---- Tables --------------------------------------------------------------------
namespace {
// Left edge of column `i` (sum of prior column widths from the table origin).
float tblColX(int i) {
    float cx = g_tbl.x0;
    for (int k = 0; k < i && k < g_tbl.count; ++k) cx += *g_tbl.w[k];
    return cx;
}
// Point the layout at the current cell (column g_tbl.index at row g_tbl.rowTop).
void tblEnterCell() {
    const float pad = 4.0f;
    const float cx = tblColX(g_tbl.index);
    g_lay.ox = cx + pad;
    g_lay.contentW = *g_tbl.w[g_tbl.index] - 2.0f * pad;
    if (g_lay.contentW < 8.0f) g_lay.contentW = 8.0f;
    g_lay.cy = g_tbl.rowTop;
    g_lay.indent = 0.0f;
    g_lay.pendingSameLine = false;
    g_lay.prevX = g_lay.ox; g_lay.prevY = g_lay.cy; g_lay.prevW = 0.0f; g_lay.prevH = 0.0f;
}
} // namespace

bool BeginTable(const char* id, int columns, const char* const* headers) {
    return BeginTable(id, columns, headers, nullptr);
}

float CalcTextWidth(const char* text) { return labelW(text); }

bool BeginTable(const char* id, int columns, const char* const* headers, const float* weights) {
    if (!g_lay.active || columns < 1) return false;
    if (columns > 16) columns = 16;
    const int tid = hashLabel(id);
    g_tbl.active = true; g_tbl.id = tid; g_tbl.count = columns; g_tbl.index = -1;
    g_tbl.x0 = g_lay.ox; g_tbl.fullW = g_lay.contentW;
    g_tbl.rowTop = g_lay.cy; g_tbl.rowBottom = g_lay.cy; g_tbl.rowStart = g_lay.cy;

    // Fetch/seed persisted column widths, then rescale so they exactly fill fullW
    // (keeps the table snug when the window is resized).
    float sum = 0.0f, wsum = 0.0f;
    if (weights) for (int i = 0; i < columns; ++i) wsum += weights[i] > 0.0f ? weights[i] : 0.0f;
    for (int i = 0; i < columns; ++i) {
        const float seed = (weights && wsum > 0.0f) ? g_tbl.fullW * std::max(weights[i], 0.0f) / wsum : g_tbl.fullW / columns;
        g_tbl.w[i] = floatState(tid ^ (0x7100 + i), seed);
        if (*g_tbl.w[i] < 12.0f) *g_tbl.w[i] = 12.0f;
        sum += *g_tbl.w[i];
    }
    if (sum > 0.0f) { float f = g_tbl.fullW / sum; for (int i = 0; i < columns; ++i) *g_tbl.w[i] *= f; }

    // Optional header row: a bar with bold column labels.
    if (headers) {
        const float hh = rowH();
        if (g_look) quad(g_tbl.x0, g_tbl.rowTop + hh - 1.0f, g_tbl.fullW, 1.0f, g_theme.border);   // hairline under dim headers
        else quad(g_tbl.x0, g_tbl.rowTop, g_tbl.fullW, hh, g_theme.bgDown);
        const Font* prevF = g_font; g_font = &kFontBold;
        for (int i = 0; i < columns; ++i) {
            float cx = tblColX(i);
            if (headers[i] && headers[i][0])
                drawText(cx + 4.0f, g_tbl.rowTop + (hh - textH()) * 0.5f, headers[i], g_theme.textScale, g_look ? g_theme.textDim : g_theme.text);
        }
        g_font = prevF;
        g_tbl.rowTop += hh;
        g_tbl.rowBottom = g_tbl.rowTop;
        g_tbl.rowStart = g_tbl.rowTop;
    }
    g_tbl.headerBottom = g_tbl.rowTop;
    return true;
}

void TableNextRow() {
    if (!g_lay.active || !g_tbl.active) return;
    if (g_lay.cy > g_tbl.rowBottom) g_tbl.rowBottom = g_lay.cy;
    g_tbl.rowTop = g_tbl.rowBottom;   // next row starts below the tallest cell so far
    g_tbl.index = -1;
}

bool TableNextColumn() {
    if (!g_lay.active || !g_tbl.active) return false;
    if (g_tbl.index >= 0) {           // remember how tall the cell we're leaving got
        if (g_lay.cy > g_tbl.rowBottom) g_tbl.rowBottom = g_lay.cy;
    }
    ++g_tbl.index;
    if (g_tbl.index >= g_tbl.count) return false;
    tblEnterCell();
    return true;
}

void EndTable() {
    if (!g_lay.active || !g_tbl.active) return;
    if (g_lay.cy > g_tbl.rowBottom) g_tbl.rowBottom = g_lay.cy;
    const float top = g_tbl.rowStart, bottom = g_tbl.rowBottom;

    // Column borders + draggable resize handles (applied next frame).
    for (int i = 1; i < g_tbl.count; ++i) {
        float bx = tblColX(i);
        quad(bx - 0.5f, g_tbl.headerBottom - rowH(), 1.0f, bottom - (g_tbl.headerBottom - rowH()), g_theme.border);
        const int hid = g_tbl.id ^ (0x7b00 + i);
        const float grab = 4.0f;
        const bool over = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, bx - grab, top - rowH(), grab * 2.0f, bottom - top + rowH());
        if (over) g_hot = hid;
        if (g_active == hid) {
            if (!g_in.mouseDown) g_active = 0;
            else {
                float dx = g_mouseDX;
                float a = *g_tbl.w[i - 1] + dx, b = *g_tbl.w[i] - dx;
                if (a >= 20.0f && b >= 20.0f) { *g_tbl.w[i - 1] = a; *g_tbl.w[i] = b; }
            }
        } else if (over && g_pressed) { g_active = hid; g_focusClaimed = true; }
    }

    g_lay.ox = g_tbl.x0;
    g_lay.contentW = g_tbl.fullW;
    g_lay.cy = bottom + g_lay.spacingY;
    g_lay.indent = 0.0f;
    g_lay.pendingSameLine = false;
    g_tbl.active = false;
}

// ---- Launcher-style building blocks ---------------------------------------------
namespace {
float g_textScaleStack[8];
int   g_textScaleTop = 0;
struct CardSave { float x, y, w; float* height; float ox, contentW; };
CardSave g_cards[8];
int      g_ncards = 0;
// A pleasant, stable colour per name (avatar backgrounds).
void nameColor(const char* name, unsigned char out[4]) {
    static const unsigned char pal[8][3] = {{66, 133, 244}, {67, 181, 129}, {235, 137, 52}, {171, 92, 230},
                                            {230, 82, 120}, {38, 166, 191}, {214, 170, 44}, {108, 120, 230}};
    unsigned h = 2166136261u;
    if (name) for (const char* p = name; *p; ++p) h = (h ^ (unsigned char)*p) * 16777619u;
    const unsigned char* c = pal[h % 8];
    out[0] = c[0]; out[1] = c[1]; out[2] = c[2]; out[3] = 255;
}
// The first character of `name` (upper-cased ASCII), as a C string.
void initialOf(const char* name, char out[8]) {
    out[0] = '?'; out[1] = 0;
    if (!name) return;
    while (*name == ' ' || *name == '@') ++name;
    if (!*name) return;
    const unsigned char c = (unsigned char)*name;
    if (c < 0x80) { out[0] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c); out[1] = 0; return; }
    int n = 1; while (n < 4 && (((unsigned char)name[n]) & 0xC0) == 0x80) ++n;   // a whole UTF-8 character
    for (int i = 0; i < n; ++i) out[i] = name[i];
    out[n] = 0;
}
void drawAvatar(float x, float y, float d, const char* name) {
    unsigned char c[4]; nameColor(name, c);
    fillCircle(x + d * 0.5f, y + d * 0.5f, d * 0.5f, c, 28);
    char ini[8]; initialOf(name, ini);
    const float prev = g_theme.textScale;
    g_theme.textScale = d * 0.5f / 8.0f;
    const unsigned char white[4] = {255, 255, 255, 255};
    drawText(x + (d - labelW(ini)) * 0.5f, y + (d - textH()) * 0.5f, ini, g_theme.textScale, white);
    g_theme.textScale = prev;
}
} // namespace

void PushTextScale(float scale) {
    if (g_textScaleTop < 8) g_textScaleStack[g_textScaleTop++] = g_theme.textScale;
    g_theme.textScale = g_theme.textScale * (scale > 0.1f ? scale : 0.1f);
}
void PopTextScale() { if (g_textScaleTop > 0) g_theme.textScale = g_textScaleStack[--g_textScaleTop]; }

void Heading(const char* text) {
    if (!g_lay.active || !text) return;
    PushTextScale(1.625f);   // 16px -> 26px (a baked size: crisp)
    const float tw = labelW(text), th = textH();
    float x, y; place(tw, th + 12.0f, x, y);
    drawText(x, y, text, g_theme.textScale, g_theme.text);
    rrect(x, y + th + 5.0f, tw, 3.0f, 1.5f, g_theme.accent);
    PopTextScale();
}

void Caption(const char* text, unsigned char r, unsigned char g, unsigned char b) {
    if (!g_lay.active || !text) return;
    PushTextScale(0.8125f);  // 13px
    const float tw = labelW(text), th = textH();
    float x, y; place(tw, th + 2.0f, x, y);
    const unsigned char c[4] = {r, g, b, 255};
    drawText(x, y + 1.0f, text, g_theme.textScale, c);
    PopTextScale();
}

void TextWrapped(const char* text) {
    if (!g_lay.active || !text) return;
    const float maxW = availW();
    char line[512]; int n = 0;
    const char* p = text;
    while (*p) {
        // Take words while the line fits.
        const char* wordEnd = p;
        while (*wordEnd && *wordEnd != ' ' && *wordEnd != '\n') ++wordEnd;
        const int wl = (int)(wordEnd - p);
        char trial[512]; int tn = 0;
        for (int i = 0; i < n && tn < 500; ++i) trial[tn++] = line[i];
        if (n > 0 && tn < 500) trial[tn++] = ' ';
        for (int i = 0; i < wl && tn < 500; ++i) trial[tn++] = p[i];
        trial[tn] = 0;
        if (n > 0 && labelW(trial) > maxW) { line[n] = 0; Text(line); n = 0; continue; }
        for (int i = 0; i < tn && i < 500; ++i) line[i] = trial[i];
        n = tn < 500 ? tn : 500;
        p = wordEnd;
        if (*p == '\n') { line[n] = 0; Text(line); n = 0; ++p; }
        else if (*p == ' ') ++p;
    }
    if (n > 0) { line[n] = 0; Text(line); }
}

bool PrimaryButton(const char* label) {
    if (!g_lay.active || !label) return false;
    const int id = hashLabel(label);
    const float w = labelW(label) + 40.0f, h = rowH();
    float x, y; place(w, h, x, y);
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
    if (inside) g_hot = id;
    bool clicked = false;
    if (g_active == id) { if (g_released) { if (inside) clicked = true; g_active = 0; } }
    else if (inside && g_pressed) g_active = id;
    unsigned char c[4] = {g_theme.accent[0], g_theme.accent[1], g_theme.accent[2], 255};
    if (g_active == id && inside) { c[0] = (unsigned char)(c[0] * 0.85f); c[1] = (unsigned char)(c[1] * 0.85f); c[2] = (unsigned char)(c[2] * 0.85f); }
    else if (inside) { c[0] = (unsigned char)(c[0] + (255 - c[0]) * 0.12f); c[1] = (unsigned char)(c[1] + (255 - c[1]) * 0.12f); c[2] = (unsigned char)(c[2] + (255 - c[2]) * 0.12f); }
    rrect(x, y, w, h, rnd(), c);
    const unsigned char white[4] = {255, 255, 255, 255};
    drawText(x + (w - labelW(label)) * 0.5f, y + (h - textH()) * 0.5f, label, g_theme.textScale, white);
    return clicked;
}

void Badge(const char* text, unsigned char r, unsigned char g, unsigned char b) {
    if (!g_lay.active || !text) return;
    PushTextScale(0.8125f);
    const float tw = labelW(text), h = textH() + 8.0f, w = tw + 16.0f;
    g_theme.textScale = g_textScaleStack[g_textScaleTop - 1];   // the row height of normal text...
    const float row = rowH();
    g_theme.textScale *= 0.8125f;
    float x, y; place(w, row, x, y);
    y += (row - h) * 0.5f;                                      // ...with the pill centred on it
    unsigned char c[4] = {r, g, b, 255};
    if (!r && !g && !b) { c[0] = g_theme.accent[0]; c[1] = g_theme.accent[1]; c[2] = g_theme.accent[2]; }
    rrect(x, y, w, h, h * 0.5f, c);
    const unsigned char white[4] = {255, 255, 255, 255};
    drawText(x + 8.0f, y + 3.5f, text, g_theme.textScale, white);
    PopTextScale();
}

void Avatar(const char* name, float size) {
    if (!g_lay.active) return;
    const float d = size > 0.0f ? size : rowH();
    float x, y; place(d, d, x, y);
    drawAvatar(x, y, d, name);
}

void BeginCard(const char* id) {
    if (!g_lay.active || g_ncards >= 8) return;
    const int cid = hashLabel(id ? id : "card");
    float* height = floatState(cid ^ 0xca7d, 0.0f);
    const float x = cursorX(), w = availW();
    float px, py; place(w, 0.0f, px, py);   // reserve the line; content follows inside
    g_lay.cy = py;                           // (no spacing yet: the card grows to fit)
    const float pad = 14.0f, rad = g_look ? 10.0f : rnd();
    if (*height > 0.0f) {
        const unsigned char raised[4] = {38, 42, 51, 255};
        rrect(px, py, w, *height, rad, g_theme.border);
        rrect(px + 1.0f, py + 1.0f, w - 2.0f, *height - 2.0f, rad > 1.0f ? rad - 1.0f : 0.0f, g_look ? raised : g_theme.bg);
    }
    CardSave& c = g_cards[g_ncards++];
    c.x = px; c.y = py; c.w = w; c.height = height; c.ox = g_lay.ox; c.contentW = g_lay.contentW;
    (void)x;
    g_lay.ox = px + pad - g_lay.indent; g_lay.contentW = w - 2.0f * pad + g_lay.indent;
    g_lay.cy = py + pad; g_lay.cx = g_lay.ox;
    g_lay.prevX = g_lay.ox; g_lay.prevY = g_lay.cy; g_lay.prevW = 0.0f; g_lay.prevH = 0.0f;
}

void EndCard() {
    if (!g_lay.active || g_ncards <= 0) return;
    CardSave& c = g_cards[--g_ncards];
    const float pad = 14.0f;
    const float bottom = g_lay.cy - g_lay.spacingY + pad;   // last item's bottom + padding
    *c.height = bottom - c.y;
    g_lay.ox = c.ox; g_lay.contentW = c.contentW;
    g_lay.cx = leftEdge();
    g_lay.cy = bottom + g_lay.spacingY;
    g_lay.prevX = c.x; g_lay.prevY = c.y; g_lay.prevW = c.w; g_lay.prevH = *c.height;
    g_lay.pendingSameLine = false;
}

bool ListItem(const char* title, const char* subtitle, const char* right, bool selected) {
    if (!g_lay.active) return false;
    const int id = hashLabel(title ? title : "item");
    const bool two = subtitle && *subtitle;
    const float h = two ? rowH() + textH() * 0.85f : rowH() + 4.0f, w = fullW();
    float x, y; place(w, h, x, y);
    const bool inside = !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
    if (inside) g_hot = id;
    bool clicked = false;
    if (g_active == id) { if (g_released) { if (inside) clicked = true; g_active = 0; } }
    else if (inside && g_pressed) g_active = id;
    unsigned char tmp[4]; const unsigned char white[4] = {255, 255, 255, 255};
    if (selected) rrect(x, y, w, h, 8.0f, withAlpha(g_theme.accent, 0.25f, tmp));
    else if (inside) rrect(x, y, w, h, 8.0f, withAlpha(white, 0.06f, tmp));
    const float d = h - 12.0f;
    drawAvatar(x + 6.0f, y + 6.0f, d, title);
    const float tx = x + d + 18.0f, s = g_theme.textScale;
    float rw = 0.0f;
    if (right && *right) { rw = labelW(right); drawText(x + w - rw - 10.0f, y + (h - textH()) * 0.5f, right, s, g_theme.textDim); }
    if (two) {
        drawText(tx, y + 6.0f, title ? title : "", s, g_theme.text);
        PushTextScale(0.8125f);
        drawText(tx, y + h - 6.0f - textH() - 1.0f, subtitle, g_theme.textScale, g_theme.textDim);
        PopTextScale();
    } else drawText(tx, y + (h - textH()) * 0.5f, title ? title : "", s, g_theme.text);
    return clicked;
}

void KeyValue(const char* label, const char* value) {
    if (!g_lay.active) return;
    const float h = rowH(), w = fullW();
    float x, y; place(w, h, x, y);
    const float ty = y + (h - textH()) * 0.5f;
    if (label) drawText(x, ty, label, g_theme.textScale, g_theme.textDim);
    if (value) drawText(x + w - labelW(value), ty, value, g_theme.textScale, g_theme.text);
}

void Spinner(float size) {
    if (!g_lay.active) return;
    const float d = size > 0.0f ? size : rowH();
    float x, y; place(d, d, x, y);
    const float cx = x + d * 0.5f, cy = y + d * 0.5f, r = d * 0.38f;
    const float a0 = (float)(SDL_GetTicks() % 1000u) / 1000.0f * 6.2831853f;
    unsigned char tmp[4];
    for (int i = 0; i < 20; ++i) {   // a 270-degree arc fading toward its tail
        const float t0 = a0 + i * 0.2356f, t1 = t0 + 0.2356f;
        line(cx + r * SDL_cosf(t0), cy + r * SDL_sinf(t0), cx + r * SDL_cosf(t1), cy + r * SDL_sinf(t1),
             d * 0.1f, withAlpha(g_theme.accent, 0.15f + 0.85f * (float)i / 19.0f, tmp));
    }
}

void Toast(const char* text, int kind, float seconds) {
    if (!text || !*text) return;
    if (g_ntoasts == 4) { for (int i = 1; i < 4; ++i) g_toasts[i - 1] = g_toasts[i]; g_ntoasts = 3; }
    ToastSlot& t = g_toasts[g_ntoasts++];
    int i = 0; for (; text[i] && i < 159; ++i) t.text[i] = text[i];
    t.text[i] = 0;
    t.kind = kind;
    t.born = SDL_GetTicks();
    t.until = t.born + (Uint32)((seconds > 0.5f ? seconds : 0.5f) * 1000.0f);
}

} // namespace OkayUI
