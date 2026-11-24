#pragma once
// Private shared state and helpers of the OkayUI toolkit (okayui/src/OkayUI*.cpp). The toolkit keeps its state in file-scope
// variables; the families that were split out of OkayUI.cpp (tooltips, tables, draw list; O1 in docs/engine/modularization_plan.md) reach it through these
// declarations. Not part of the public API (that is okay/UI/OkayUI.hpp). Definitions stay in OkayUI.cpp.
#include <SDL.h>
#include "okay/UI/OkayUI.hpp"
#include "okay/Graphics/Font.hpp"
#include "OkayUI_FontAtlas.hpp"
#include "okay/Graphics/TtfFont.hpp"   // okay::TtfFont: the fallback fonts g_fbFonts

namespace OkayUI {
namespace ui_core {

// Fixed-capacity geometry batch. Text is drawn as one small quad per lit font
// pixel, so size generously for several labelled buttons per frame; on overflow we
// silently drop further geometry rather than grow/allocate.
constexpr int kMaxVerts = 262144;   // per batch (main and overlay): about 5 MB of vertices each
constexpr int kMaxIdx   = 393216;
// Geometry that did not fit is counted, not hidden: DrawData::dropped, DroppedGeometry() and a message on stderr.
extern int g_dropped;
extern bool g_rowClipper;
extern bool g_cullRow;
inline void noteDrop() { ++g_dropped; }
extern SDL_Vertex g_verts[kMaxVerts];
extern int g_nv;
extern int g_idx[kMaxIdx];
extern int g_ni;
// Overlay batch: popups (Combo lists) and tooltips draw here, then it's appended
// AFTER the main batch each frame so it composites on top of later widgets.
extern SDL_Vertex g_ovVerts[kMaxVerts];
extern int g_onv;
extern int g_ovIdx[kMaxIdx];
extern int g_oni;
extern bool g_toOverlay;
// The launcher look: text comes from a TrueType atlas, and flat shapes sample its
// solid-white patch, so shapes + text share one texture (and one draw call). Spans
// carry kAtlasTex as a placeholder; EndFrame swaps in the renderer's real texture.
extern bool g_look;
extern SDL_Texture* g_defTex;
extern float g_whiteU, g_whiteV;
// Texture changes inside the overlay batch (fallback-font text in toasts and
// popups): the overlay index each starts at. mergeOverlay turns them into spans.
struct OverlaySpan { int startIdx; SDL_Texture* tex; };
extern OverlaySpan g_ovSpans[32];
extern int g_novSpans;
// Draw spans: the main batch is split into contiguous index ranges, each carrying a
// clip state and a texture, so scrolling child regions can clip and Image() can bind a
// texture. A span records the index where a state begins; EndFrame draws each range
// with the matching clip rect + texture. The overlay batch is never clipped.
struct DrawSpan { int startIdx; bool on; SDL_Rect rect; SDL_Texture* tex; };
constexpr int kMaxSpans = 256;   // 128 before window z-order: reordering adds a span per window segment
extern DrawSpan g_spans[kMaxSpans];
extern int g_nspans;
extern SDL_Rect g_clipStack[16];
extern int g_clipTop;
// Current draw state; a span is emitted whenever clip or texture changes.
extern bool g_curClipOn;
extern SDL_Rect g_curClipRect;
extern SDL_Texture* g_curTex;
// Record a span at the current index position (coalescing if no geometry was added
// since the previous span).
void pushSpan();
// Switch the bound texture for subsequent geometry (nullptr = untextured/flat color).
void setTexture(SDL_Texture* t);
extern Input g_in;
// Item state for the IsItem* queries: the id of the last widget that registered itself, and which id was
// active when the frame began (so "activated" and "deactivated" are edges). place() clears g_lastId.
extern int g_lastId;
extern int g_disCount;
extern bool g_rawDraw;
extern bool g_pressed;
extern bool g_released;
extern bool g_rPressed;
// Popup/context-menu registry. Each open popup remembers where it opened and its
// measured height (so the background can be drawn before the content next frame).
struct PopupSlot { int id = 0; bool open = false; bool used = false; float x = 0, y = 0; float w = 0; bool modal = false; bool fixedW = false;   // w <= 0: the default width; modal: a click outside does not close it
                   unsigned long long touched = 0;   // the last frame it was asked for (a closed slot not asked for longest is reused)
                   bool combo = false; float boxTop = 0, boxRight = 0; };   // a combo's list: placed around its box as Dear ImGui places one
// Every combo box and menu keeps a slot. When all are taken (an editor with many hosted panels has more combos than slots), the closed
// slot that was asked for longest ago is given to the new id: a closed popup has nothing worth keeping but its last place.
PopupSlot* popupSlot(int id, bool create);
extern int g_openPopupThisFrame;
extern int g_hot, g_active;
extern float g_dispW, g_dispH;
extern bool g_focusClaimed;
extern Theme g_theme;
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
// The colours beyond the original eight: set (Theme::colorSet) or following the base theme.
inline bool colorIsSet(int id) { return id >= 8 && id < 64 && ((g_theme.colorSet >> id) & 1ull) != 0; }
// `dflt` unless the colour was set: how a widget honours an override while keeping its own default look.
inline const unsigned char* pick(int id, const unsigned char* dflt) { return colorIsSet(id) ? g_theme.colors[id] : dflt; }
// The base-theme value of a colour that was never set (what GetStyleColorVec4 answers; the classic look's default).
void deriveColor(int id, unsigned char out[4]);
// Any colour as bytes: the original eight, a set one, or the derived default (valid until the next call with `tmp`).
inline const unsigned char* styleColor(int id, unsigned char tmp[4]) {
    if (id >= 0 && id < 8) return themeColor(id);
    if (id < 8 || id >= Col_COUNT) { tmp[0] = tmp[1] = tmp[2] = tmp[3] = 0; return tmp; }
    if (colorIsSet(id)) return g_theme.colors[id];
    deriveColor(id, tmp);
    return tmp;
}
// ---- Auto-layout state (ImGui-style window + cursor) ---------------------------
struct LayoutState {
    bool  active   = false;
    bool  winClip  = false;           // Begin() clipped the content to the window (launcher look)
    float ox = 0, oy = 0;            // content origin (top-left of the content area)
    float cx = 0, cy = 0;            // cursor: top-left where the next item goes
    float contentW = 0;             // usable content width
    float spacingX = 8.0f, spacingY = 6.0f;
    float prevX = 0, prevY = 0, prevW = 0, prevH = 0;   // last placed item
    // a text centred on the taller item before it on its line (textLine) keeps the line's top here: the next SameLine item starts at
    // the line's top as in Dear ImGui, not at the text's lowered top (a drag after a labelled drag sat 8 px low, section 19)
    bool hasLineTop = false; float lineTopY = 0;
    bool  pendingSameLine = false;
    float sameLineSpacing = -1.0f;
    float indent = 0.0f;            // TreeNode indentation of the content's left edge
    float bottom = 0.0f;            // bottom of the visible content area (GetContentRegionAvail)
    float maxRight = 0.0f;          // right edge of the furthest item placed (window auto-resize measures it)
    int   seed = 0;                 // window id, mixed into label->id hashing
};
extern LayoutState g_lay;
// BeginGroup / EndGroup: the bounding box of the items placed since BeginGroup (nested up to 8 deep).
struct GroupBox { float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f; bool any = false; };
extern GroupBox g_grp[8];
extern int g_grpTop;
extern bool g_posOverride;
// Multi-column layout state (Columns/NextColumn/EndColumns).
struct ColumnState {
    bool  active = false;
    int   count = 1, index = 0;
    float x0 = 0, fullW = 0;   // content origin + full width when Columns() began
    float rowTop = 0, rowBottom = 0;
};
extern ColumnState g_col;
// Table layout state (BeginTable/TableNextRow/TableNextColumn/EndTable).
// Per-table memory that outlives a frame: the sort keys and the row heights of the last frame (for row backgrounds).
constexpr int kTblRows = 256, kTblSortKeys = 4;
struct TblStore {
    int id = 0; bool used = false; unsigned long long lastFrame = 0;
    float rowH[kTblRows] = {};
    int sortN = 0; int sortCol[kTblSortKeys] = {}; bool sortDesc[kTblSortKeys] = {};
    bool dirty = false, inited = false;
};
TblStore* tblStoreFor(int id);
struct TableState {
    bool  active = false;
    int   id = 0, count = 0, index = -1;
    float x0 = 0, fullW = 0;
    float rowTop = 0, rowBottom = 0, rowStart = 0;
    float minRowH = 0;          // TableNextRow(flags, minRowHeight): the row being built is at least this tall
    float* w[16] = {nullptr};   // persisted per-column widths (pixels)
    float headerBottom = 0;     // y below the header row (for the resize handles)
    float topY = 0;             // y of the table's top edge (borders and resize handles start here)
    // The flag-style API (BeginTable(id, n, flags) + TableSetupColumn): columns are described after Begin and sized on first use.
    int   flags = 0, rowIndex = -1, nSetup = 0;
    bool  setupDone = true;
    const char* sLabel[16] = {nullptr};
    int   sFlags[16] = {0};
    float sVal[16] = {0};
    unsigned sUser[16] = {0};
    TblStore* store = nullptr;
    // scrolling boxes (Table_ScrollX / Table_ScrollY): the outer rectangle, the scroll offsets and the frozen part
    bool  scrollX = false, scrollY = false, needV = false, needH = false;
    float ox = 0, oy = 0, ow = 0, oh = 0, viewW = 0, contentW = 0, innerWidth = 0;
    float* pSX = nullptr; float* pSY = nullptr; float* pMaxY = nullptr; float* pH = nullptr;
    float sx = 0, sy = 0;                 // this frame's scroll
    int   freezeCols = 0, freezeRows = 0, rowsDone = 0;
    float frozenW = 0, frozenBottom = 0;
    bool  bodyStarted = false, rowFrozen = true, headerDrawn = false;
    float shiftY = 0;                     // added to the logical row top to get the screen y (minus the scroll, for body rows)
    bool  clipOn = false; float clipR[4] = {0, 0, 0, 0};   // the clip rectangle in force (nothing changes when the next one is the same)
    float rowBgH = 0;                     // height of the band the current row's background got
    bool  rowCulled = false;              // the current row is wholly outside the visible clip: its geometry is skipped (the row clipper)
    LayoutState saved;                    // the parent layout, restored by EndTable of a scrolling box
    unsigned hidden = 0;                  // bit i: column i is hidden (flag-style tables: Col_DefaultHide, the header menu, TableSetColumnEnabled)
    float*   pHidden = nullptr;           // its persisted copy (a float holding the mask; negative before the first frame)
    float    zeroW[16] = {0};             // what a hidden column's width points at, so no layout loop touches its remembered width
};
extern TableState g_tbl;
extern int g_sortSpecsFor;
extern TableSortSpecs* g_sortSpecsOut;
float* floatState(int id, float dflt);
extern unsigned long long g_frameCount;
extern IO g_io;
extern float g_mouseDX, g_mouseDY;
// ID stack: an extra seed mixed into label hashing so same-labelled widgets in a
// loop stay distinct. PushID/PopID adjust g_idSeed; a small stack remembers prior
// seeds to restore on pop.
extern unsigned g_idSeed;
// FNV-1a hash of a label -> stable nonzero widget id (so callers needn't pass ids).
int hashLabel(const char* s);
extern const Font kFontBold;
extern const Font* g_font;
inline int  fH() { return g_font->height; }
// Width of `s` in font-cell units (multiply by textScale for pixels). With the
// launcher look it's the TrueType width at the 8-unit cell height (proportional).
float fMeasure(const char* s);
// StyleVar_FramePadding (y): -1 = OkayUI's frames (a text line + 15, or + 12 without the launcher look); 0 or more: a text line plus
// twice that (Dear ImGui's frame height). Checkbox and RadioButton boxes follow it too (boxSize).
extern float g_framePadY;
// StyleVar_ScrollbarSize (< 0 = none pushed: OkayUI's own 10-pixel bars): the width of the scroll bars of children and scrolling tables.
// A pushed size also gives Dear ImGui's proportions: the bar inside a framed child's outline and its grab 2 pixels in on every side.
extern float g_sbSize;
inline float rowH() { return fH() * g_theme.textScale + (g_framePadY >= 0.0f ? 2.0f * g_framePadY : (g_look ? 15.0f : 12.0f)); }
inline float textH() { return fH() * g_theme.textScale; }
inline float labelW(const char* s) { return s && *s ? fMeasure(s) * g_theme.textScale : 0.0f; }
// Item-width override: SetNextItemWidth (one-shot) / PushItemWidth (scoped). availW()
// returns this instead of the full remaining width when set, so callers can size a
// widget explicitly. Always clamped to what actually remains on the line.
extern float g_nextItemW;
// Remaining width from the next item's left edge to the content's right edge.
float availW();
void place(float w, float h, float& x, float& y);
inline SDL_Color toColor(const unsigned char c[4]) {
    SDL_Color sc; sc.r = c[0]; sc.g = c[1]; sc.b = c[2]; sc.a = c[3];
    if (g_disCount > 0 && !g_rawDraw) {   // inside BeginDisabled: halfway to the panel colour, alpha kept
        const unsigned char* p = g_theme.panel;   // (so stacked layers of one widget do not blend through each other)
        sc.r = (unsigned char)((sc.r + p[0]) / 2); sc.g = (unsigned char)((sc.g + p[1]) / 2); sc.b = (unsigned char)((sc.b + p[2]) / 2);
    }
    return sc;
}
void quad(float x, float y, float w, float h, const unsigned char c[4]);
// A textured quad: binds `tex`, samples uv [u0,v0]..[u1,v1], modulated by `tint`
// (use white for the raw image). Reverts to the untextured state afterward so the
// next flat quad starts a new span. Textured geometry always goes to the main batch.
void texQuad(float x, float y, float w, float h, SDL_Texture* tex, float u0, float v0, float u1, float v1, const unsigned char tint[4]);
// A quad from four arbitrary corners (for rotated/skewed shapes and thick lines).
void quadPts(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy, const unsigned char c[4]);
// An axis-aligned quad whose four corners each have their own color; the GPU
// interpolates between them (for gradients: hue bars, SV squares, fades).
void gradQuad(float x, float y, float w, float h, const unsigned char tl[4], const unsigned char tr[4], const unsigned char br[4], const unsigned char bl[4]);
// A straight line of thickness `th` between two points, as a thin rotated quad.
void line(float x0, float y0, float x1, float y1, float th, const unsigned char c[4]);
// A single filled triangle.
void tri(float ax, float ay, float bx, float by, float cx, float cy, const unsigned char c[4]);
// The rim of a unit circle cut into n wedges: (cos, sin) of step * i for i = 1..n, worked out once per n (the same float
// angles fillCircle always used, so the geometry is unchanged). n is 3..128.
const float* unitCircle(int n);
// A filled circle as a triangle fan of `segments` wedges that share the centre and rim vertices (the same triangles as one
// tri() per wedge, with a third of the vertices).
void fillCircle(float cx, float cy, float radius, const unsigned char c[4], int segments = 24);
// A filled rounded rectangle (falls back to a plain quad when rad <= 0). Decomposed
// into three bars + four quarter-circle corners so translucent fills never overlap.
void rrect(float x, float y, float w, float h, float rad, const unsigned char c[4]);
// Text with characters the atlas lacks: every glyph from the engine font `f`, bound
// to its atlas (a span of its own; in the overlay, an overlay span).
void bindTexture(SDL_Texture* t);
// Draw a C string from the 8x8 bitmap font, each lit pixel a `s`x`s` quad.
void drawText(float x, float y, const char* str, float s, const unsigned char c[4]);
// `c` with its alpha scaled (0..1), into `out`.
const unsigned char* withAlpha(const unsigned char c[4], float a, unsigned char out[4]);
inline bool pointIn(float px, float py, float x, float y, float w, float h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}
extern bool g_wheelUsed;
bool hit(float x, float y, float w, float h);
// A scroll bar's track (classic look, or when ScrollbarBg is set) and thumb in the Col_Scrollbar* colours.
// With a pushed StyleVar_ScrollbarSize the bar is that wide and the grab sits 2 pixels inside it on every side, fully rounded.
void scrollbarThumb(float sbx, float top, float trackH, float thumbY, float thumbH, bool active, bool over, bool track = true);
// StyleVar_CellPadding: the gap inside a cell, across (default 4) and above and below (default -1: none, and the row ends after the
// last item's spacing, as it always did; 0 or more: that much above the first item and below the last one, without its spacing).
extern float g_cellPadX, g_cellPadY;
void flushTooltip();
void dropTooltip();
// Shared popup body setup: redirect layout into a floating overlay box at (px,py),
// drawing the background using last frame's measured height. Returns true if open.
bool beginPopupBody(int id);
constexpr int kMaxPath = 256;
extern Vec2 g_path[kMaxPath];
extern int g_pathN;
inline bool finite1(float v) { return v == v && v > -1.0e7f && v < 1.0e7f; }
inline bool finite2(Vec2 p) { return finite1(p.x) && finite1(p.y); }
extern const DrawListHandle g_windowList;
extern const DrawListHandle g_foregroundList;

extern const fontatlas::Atlas* g_ttf;
extern int g_itemWTop;
extern float g_itemWStack[16];
extern int g_winItemWDepth;
extern int g_focusAtFrameStart;
extern int g_editId;
extern bool g_inMenuBar;
extern int g_popupSubOpen;
extern float g_barX, g_barY, g_barH;
extern int g_menuDepth;
extern bool g_framedWidgets;
extern bool g_escapeUsed;
extern bool g_dblClick;
extern float g_popupPadX, g_popupPadY, g_popupW, g_innerX;
extern float g_childPadY;
extern PopupSlot g_popups[32];
extern unsigned long long g_zCounter;
extern float g_winBaseScale;
extern bool g_ptrOverWindow, g_ptrOverWindowPrev;
extern bool g_nextWinFocus;
extern float g_nextBgAlpha;
extern float g_bgAlphaCore;
extern bool g_hitWinOn; extern int g_hitWinId; extern unsigned long long g_hitWinZ;
extern int g_childTop;
void panelColored(float x, float y, float w, float h, const unsigned char* fill);
void framedComboBox(float x, float y, float w, float h, bool hovered, const char* text);
void chevron(float cx, float cy, float sz, bool down, const unsigned char c[4]);
void xMark(float cx, float cy, float sz, const unsigned char c[4]);
void softShadow(float x, float y, float w, float h, float rad);
bool* openState(int id, bool dflt);
struct MenuKb { int id = 0; int count = 0; unsigned long long mask = 0; int idx = -1; bool childOpen = false, childOpenNext = false;
                bool keyMoved = false; };   // keyMoved: the arrows moved the highlight this frame (a scrolling list brings it into view)
MenuKb* kbSlot(int id);
struct CurMenu {
    bool  active = false;
    int   menuId = 0;
    float x = 0, w = 0;                          // dropdown left + width
    float bx = 0, by = 0, bw = 0, bh = 0;        // the menu's button on the bar
    float itemStartY = 0, itemY = 0;             // dropdown item cursor
    float subX = 0, subY = 0, subW = 0, subH = 0;   // the open sub-menu's list this frame (a press inside it is not "outside")
    struct MenuKb* kb = nullptr;                 // keyboard highlight state of this menu
    int   n = 0;                                 // items seen so far this frame (the next item's ordinal)
    unsigned long long mask = 0;                 // which of them are enabled
    bool  activate = false;                      // Enter pressed for the highlighted item this frame
    int   popupId = 0;                           // a sub-menu opened from a popup's row: that popup (picking an item closes it too)
    // The list's rows: their height (0: rowH()) and where the label starts. A chain of sub-menus opened from a popup's row uses the
    // popup's row height and padding (Dear ImGui's menu rows: one text line plus the item spacing), so it matches the popup.
    float rowH = 0.0f, textInset = 10.0f;
    bool  fromPopup = false;
    // Kept inside the display: the list starts higher when last frame's height would run past the bottom; when it is taller than the
    // display it shows the band [clipY0, clipY1) and scrolls by whole rows (the wheel over it, or the keyboard highlight).
    float clipY0 = 0.0f, clipY1 = 0.0f;
    float virtTop = 0.0f;                        // where row 0 would be (itemStartY minus the scroll)
    float* heightSlot = nullptr;                 // the list's full height last frame
    float* scrollSlot = nullptr;                 // its scroll offset in pixels
};
extern CurMenu g_curMenu;
extern CurMenu g_menuStack[4];
struct ChildSave {
    LayoutState lay;      // parent layout to restore
    float rx, ry, rw, rh; // child region rect
    float top;            // unscrolled content top (ry + pad)
    float* scroll;        // persisted scroll offset
    float* maxSlot;       // persisted max scroll from last frame (clamps this frame)
    float* autoH = nullptr;   // Child_AutoResizeY: where EndChild leaves the content height for the next frame
    float* autoSlot = nullptr;   // main's flags form with Child_AutoResizeY: where EndChild stores the measured height plus the padding
    float pad;            // above and below the content
    float padX;           // left and right
    bool  hovered;        // mouse over the region this frame
    float setScroll;      // SetScrollY's request, applied once the content has been measured (< 0: none)
    float sbW = 10.0f;    // the scroll bar's width (StyleVar_ScrollbarSize when pushed)
    bool sbPushed = false;    // ScrollbarSize was pushed when the child began (Dear ImGui's proportions)
    bool sbInside = false;    // the bar sits inside the child's one-pixel outline (a pushed ScrollbarSize and Child_Borders)
    bool  trim = false;          // the child began through BeginChild(id, size, childFlags): the gap after its last item does not count as content (no scroll bar for a few pixels)
};
extern ChildSave g_childStack[8];
constexpr float kScrollbarW = 10.0f;
struct PopupCtx { LayoutState lay; int id; float x, y, w; float* hSlot; float* wSlot; float padX, padY; bool exact;
                  float subX0 = 0, subY0 = 0, subX1 = 0, subY1 = 0; };   // the sub-menu lists opened from its rows this frame (a press there is inside)
extern PopupCtx g_popupStack[4];
extern int g_popupTop;
struct WinSlot { int id = 0; float x = 0, y = 0; bool used = false; float w = 0, h = 0; bool sized = false;
                 bool posInit = false, sizeInit = false, collInit = false; unsigned long long lastFrame = 0; char title[64] = {0};   // title: for WindowRef::Name
                 unsigned long long z = 0;          // stacking order: higher is in front, 0 is the bottom (docked content, NoBringToFront)
                 float scroll = 0, maxScroll = 0;   // content scroll and the most it could scroll last frame
                 float autoW = 0, autoH = 0; bool measured = false;   // AlwaysAutoResize: the content's size from last frame
               };
extern WinSlot g_wins[64];
struct NextCollapse { bool set = false; bool value = false; int cond = 0; };
extern NextCollapse g_nextCollapse;
inline int  fW() { return g_font->width; }
inline float fCharW() { return (g_ttf || g_font->ttf) ? fMeasure("n") : (float)fW(); }
struct VisLabel {
    char b[128];
    explicit VisLabel(const char* s) {
        int n = 0;
        for (; s && s[n] && n < 127; ++n) if (s[n] == '#' && s[n + 1] == '#') break;
        for (int i = 0; i < n; ++i) b[i] = s[i];
        b[n] = 0;
    }
};

inline float visW(const char* s) { return s && *s ? labelW(VisLabel(s).b) : 0.0f; }
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
inline bool widthOverridden() { return g_nextItemW > 0.0f || g_itemWTop > 0; }
inline float realAvailW() { return g_lay.ox + g_lay.contentW - cursorX(); }
inline float rnd() { return g_theme.rounding; }
struct OvRect { float x, y, w, h; bool modal; };
extern OvRect g_ovCur[16];  extern int g_ovCurN;
extern OvRect g_ovPrev[16]; extern int g_ovPrevN;
inline void registerOverlay(float x, float y, float w, float h, bool modal) {
    if (g_ovCurN < 16) g_ovCur[g_ovCurN++] = OvRect{ x, y, w, h, modal };
}
struct PopRect { float x, y, w, h; int depth; };
extern PopRect g_popCur[8], g_popPrev[8]; extern int g_popCurN, g_popPrevN;
struct WinRect { int id; unsigned long long z; float x, y, w, h; };
extern WinRect g_wCur[64], g_wPrev[64]; extern int g_wCurN, g_wPrevN;
inline bool coveredByHigher(float px, float py, unsigned long long z, int selfId) {
    for (int i = 0; i < g_wCurN; ++i) {
        const WinRect& e = g_wCur[i];
        if (e.id != selfId && e.z > z && pointIn(px, py, e.x, e.y, e.w, e.h)) return true;
    }
    for (int i = 0; i < g_wPrevN; ++i) {
        const WinRect& e = g_wPrev[i];
        if (e.id == selfId || e.z <= z || !pointIn(px, py, e.x, e.y, e.w, e.h)) continue;
        bool replaced = false;
        for (int j = 0; j < g_wCurN; ++j) if (g_wCur[j].id == e.id) { replaced = true; break; }
        if (!replaced) return true;
    }
    return false;
}
inline void registerWindowRect(int id, unsigned long long z, float x, float y, float w, float h) {
    for (int i = 0; i < g_wCurN; ++i) if (g_wCur[i].id == id) { g_wCur[i] = WinRect{id, z, x, y, w, h}; return; }
    if (g_wCurN < 64) g_wCur[g_wCurN++] = WinRect{id, z, x, y, w, h};
}
// Geometry of each window of the frame: its index range, and what is needed to drop it again (a hidden measuring frame).
struct WinRec { int id; unsigned long long z; int i0, i1, v0, ns0; bool clipOn; SDL_Rect clip; SDL_Texture* tex; };
extern WinRec g_wrec[64]; extern int g_nwrec; extern int g_wrecOpen;
inline void wrecEnd() { if (g_wrecOpen >= 0) { g_wrec[g_wrecOpen].i1 = g_ni; g_wrecOpen = -1; } }
inline void wrecBegin(int id, unsigned long long z) {
    wrecEnd();
    if (g_nwrec >= 64) return;
    g_wrecOpen = g_nwrec++;
    g_wrec[g_wrecOpen] = WinRec{id, z, g_ni, g_ni, g_nv, g_nspans, g_curClipOn, g_curClipRect, g_curTex};
}
// The windows' geometry in stamp order. Free geometry between windows keeps its place in the call order; only the windows
inline bool hitOverlay(float x, float y, float w, float h) {
    return !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
}
inline bool inClip() {
    if (g_toOverlay || g_clipTop <= 0) return true;
    const SDL_Rect& c = g_clipStack[g_clipTop - 1];
    return pointIn(g_in.mouseX, g_in.mouseY, (float)c.x, (float)c.y, (float)c.w, (float)c.h);
}
inline int clampPx(float v, int nan) {
    if (v != v) return nan;
    if (v > 1.0e7f) return 10000000;
    if (v < -1.0e7f) return -10000000;
    return (int)v;
}

struct CurWin { bool valid = false; int id = 0; float x = 0, y = 0, w = 0, h = 0; bool appearing = false; unsigned long long z = 0; char title[96] = {0}; };
extern CurWin g_curWin;
struct WinEnd { bool open = false, laid = false, scrolls = false, showBar = false, autoSize = false, hidden = false;
                int slot = -1, id = 0; float x = 0, y = 0, w = 0, h = 0, titleH = 0, pad = 0, ox = 0, oy = 0; };
extern WinEnd g_we;
constexpr int kWinFlagStyle = 1 << 30;   // set by Begin(title, p_open, flags): the window is an ImGui-style one
struct NextWin { bool set = false; float x = 0, y = 0; int cond = 0; float pivX = 0, pivY = 0; };
extern NextWin g_nextWinPos, g_nextWinSize;
extern int g_wflagsNext;
extern int g_focusWin;
extern bool g_pinNext, g_bareNext, g_noTitleNext, g_resizeNext;
extern bool g_dockAware, g_dockRouted;
struct TabSel { int bar = 0; int sel = 0; };
// The remembered order and widths of a Reorderable tab bar's tabs (the tabs are placed from last frame's widths, so a drag can swap them).
struct TabOrder { int bar = 0; int n = 0; int ids[32] = {}; float w[32] = {}; unsigned long long lastFrame = 0; };
struct TabBarCtx { bool active = false; int id = 0; float x = 0, y = 0, w = 0, h = 0, cx = 0, tr = 0; int* sel = nullptr; bool seenSel = false; int firstId = 0;
                   int flags = 0; TabOrder* ord = nullptr; int seen[32] = {}; int nSeen = 0; bool fresh = false; };
struct ColorSave { unsigned char* field; unsigned char prev[4]; int ext = -1; bool prevSet = false; };   // ext >= 0: a colour of Theme::colors
struct DragDrop {
    bool  active = false;
    int   payload = 0;
    bool  armed = false;
    int   armPayload = 0;
    float armX = 0, armY = 0;
};
struct TypedDrag {
    bool active = false, armed = false, hasPayload = false; int srcId = 0; float armX = 0, armY = 0;
    DragPayload pay; char preview[64] = {0};
};
inline float clamp01(float v) { return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f; }   // NaN -> 0
extern TabSel g_tabSel[16];
extern TabOrder g_tabOrders[16];
extern TabBarCtx g_tabBar;
extern DragDrop g_dd;
extern TypedDrag g_tdd;
extern bool g_tddExt, g_tddExtSeen, g_tddExtReleased;
extern int g_nextOpen;
extern int g_toggledId;
extern bool g_mPressed;
extern float g_pressX, g_pressY;
extern SDL_Texture* const kAtlasTex;
extern int g_nfb;
extern okay::TtfFont* g_fbFonts[8];
extern unsigned g_frame;
extern ColorSave g_colStack[64];
extern int g_colTop;
extern float g_btnAlignX;
void wrapLines(const char* text, float maxW, const unsigned char* col, float maxRest = -1.0f);
void captureFrameEnd();
void tickMark(float x, float y, float sz, const unsigned char c[4]);
void framedTick(float x, float y, float sz, const unsigned char c[4]);
} // namespace ui_core
} // namespace OkayUI
