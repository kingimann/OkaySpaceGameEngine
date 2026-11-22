#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUIDock.hpp"
#include "okay/UI/OkayUIInternals.hpp"
#include "okay/UI/OkayUICompat.hpp"   // BeginTooltip, GetStyle, ListClipper (defined here: they need the private layout state)
#include "okay/Graphics/Font.hpp"   // okay::Font8x8 — a no-STL C API over a bitmap font
#include "OkayUI_FontAtlas.hpp"     // the launcher font (TrueType) for UseLauncherLook()
#include "okay/Graphics/TtfFont.hpp" // fallback fonts for characters the atlas lacks
#include <SDL.h>
#include "okay/Platform/Platform.hpp"   // okay::platform: timers (and the SDL seam)
#include <cstddef>  // offsetof
#include <cstdarg>
#include <cstring>
#include <string>
#include <cstdio>   // snprintf for numeric widgets (C library, not STL)

// All rendering goes through SDL_RenderGeometry (SDL >= 2.0.18), which is exactly
// the call Dear ImGui's SDL_Renderer backend uses — so OkayUI shares ImGui's GPU
// pipeline (Direct3D 11 on Windows, Metal on macOS, OpenGL on Linux) and can never
// conflict with it. No STL is used here: fixed C arrays, C strings, plain ints.

namespace OkayUI {
void DockReset();   // okayui/src/OkayUIDock.cpp: ResetState forgets the dock layout too
void ColorPickerReset();
void ChannelsMergeAll();   // the window draw list's channels (below)
void StateStorageReset();   // okayui/src/OkayUICompat.cpp: the state store and key owners (OkayUIInternals.hpp)   // okayui/src/OkayUIColor.cpp: and the colour pickers' remembered hue and original colour
namespace {

// Fixed-capacity geometry batch. Text is drawn as one small quad per lit font
// pixel, so size generously for several labelled buttons per frame; on overflow we
// silently drop further geometry rather than grow/allocate.
constexpr int kMaxVerts = 262144;   // per batch (main and overlay): about 5 MB of vertices each
constexpr int kMaxIdx   = 393216;
// Geometry that did not fit is counted, not hidden: DrawData::dropped, DroppedGeometry() and a message on stderr.
int g_dropped = 0;
bool g_rowClipper = true;   // the table row clipper (SetTableRowClipping)
bool g_cullRow = false;   // a table row wholly outside the clip: its geometry is skipped, not emitted (see TableNextRow)
inline void noteDrop() { ++g_dropped; }
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
constexpr int kMaxSpans = 256;   // 128 before window z-order: reordering adds a span per window segment
DrawSpan g_spans[kMaxSpans];
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
    if (g_nspans < kMaxSpans) g_spans[g_nspans++] = DrawSpan{ g_ni, g_curClipOn, g_curClipRect, g_curTex };
}
// Backwards-compatible helper used by the clip API.
void pushClipSpan(bool on, SDL_Rect rect) { g_curClipOn = on; g_curClipRect = rect; pushSpan(); }
// Switch the bound texture for subsequent geometry (nullptr = untextured/flat color).
void setTexture(SDL_Texture* t) { if (t != g_curTex) { g_curTex = t; pushSpan(); } }

Input g_in;                      // this frame's input
// Item state for the IsItem* queries: the id of the last widget that registered itself, and which id was
// active when the frame began (so "activated" and "deactivated" are edges). place() clears g_lastId.
int   g_lastId = 0;
int   g_activeAtFrameStart = 0;
// BeginDisabled/EndDisabled: while any enclosing block is disabled, widgets take no input and draw with their
// colours halfway to the panel colour. The public draw list bypasses the dimming (g_rawDraw), like a raw draw list ignores style alpha.
bool  g_disStack[16];
int   g_disTop = 0;          // blocks pushed (disabled or not)
int   g_disCount = 0;        // how many of them are disabled
bool  g_rawDraw = false;
bool  g_rReleased = false;   // right button went up this frame
bool  g_prevDown = false;        // last frame's mouse-down (for edge detection)
bool  g_pressed  = false;        // mouse went down this frame
bool  g_released = false;        // mouse went up this frame
bool  g_rPrevDown = false;       // last frame's right-mouse (edge detection)
bool  g_rPressed  = false;       // right button went down this frame
bool  g_mPrevDown = false, g_mPressed = false, g_mReleased = false;   // the middle button: last frame's state, went down / went up this frame
float g_dragOriginX[3] = {0, 0, 0}, g_dragOriginY[3] = {0, 0, 0};    // where each button went down (GetMouseDragDelta measures from here; ResetMouseDragDelta moves it)
double g_lastMouseMove = 0.0;    // g_clock when the pointer last moved (the Stationary hover flag)

// Popup/context-menu registry. Each open popup remembers where it opened and its
// measured height (so the background can be drawn before the content next frame).
struct PopupSlot { int id = 0; bool open = false; bool used = false; float x = 0, y = 0; float w = 0; bool modal = false; bool fixedW = false;   // w <= 0: the default width; modal: a click outside does not close it
                   unsigned long long touched = 0;   // the last frame it was asked for (a closed slot not asked for longest is reused)
                   bool combo = false; float boxTop = 0, boxRight = 0; };   // a combo's list: placed around its box as Dear ImGui places one
PopupSlot g_popups[32];
extern unsigned long long g_frameCount;
// Every combo box and menu keeps a slot. When all are taken (an editor with many hosted panels has more combos than slots), the closed
// slot that was asked for longest ago is given to the new id: a closed popup has nothing worth keeping but its last place.
PopupSlot* popupSlot(int id, bool create) {
    for (PopupSlot& p : g_popups) if (p.used && p.id == id) { p.touched = g_frameCount; return &p; }
    if (!create) return nullptr;
    PopupSlot* reuse = nullptr;
    for (PopupSlot& p : g_popups) {
        if (!p.used) { reuse = &p; break; }
        if (!p.open && p.touched < g_frameCount && (!reuse || p.touched < reuse->touched)) reuse = &p;
    }
    if (!reuse) return nullptr;
    *reuse = PopupSlot();
    reuse->used = true; reuse->id = id; reuse->touched = g_frameCount;
    return reuse;
}
int      g_openPopupThisFrame = 0;   // popup id requested to open this frame
int   g_hot = 0, g_active = 0;   // hovered id / pressed-and-holding id (0 = none)
int   g_focus = 0;               // keyboard-focused widget id (TextField); 0 = none
// Edit tracking (IsItemEdited / IsItemDeactivatedAfterEdit): the item whose value changed this frame, and the item that
// changed at some point while it was held or focused (kept until it is neither, so a drag that edits for many frames
// answers "edited" on the frame it is let go).
int   g_edFrameId = 0, g_edStickyId = 0, g_focusAtFrameStart = 0, g_focusLostEnd = 0, g_focusLostPrev = 0;   // ...LostEnd: focus dropped by the end-of-frame click-away rule
inline bool noteEdit(bool changed) { if (changed && g_lastId != 0) g_edFrameId = g_edStickyId = g_lastId; return changed; }
int   g_fieldFocus = 0;          // the text field that had the keyboard when last drawn ...
bool  g_fieldFocusSeen = false;  // ... and whether it was drawn this frame (a field that is not drawn, as in a popup that closed, gives the keyboard up)
float g_fieldPadL = -1.0f, g_fieldPadR = -1.0f;   // a text field's inner padding at the left / right while SearchField draws one (< 0: the usual)
int   g_nextOpen = -1;             // SetNextItemOpen: -1 none, 0 closed, 1 open (consumed by the next tree node / header)
int   g_toggledId = 0;             // the tree node or header whose open state the user flipped this frame
float g_dispW = 0.0f, g_dispH = 0.0f;   // Input::displayW / displayH of this frame
float g_wrapPos = -1.0f;            // PushTextWrapPos: < 0 = off, 0 = wrap at the right edge, > 0 = wrap at that width
const char* g_hintText = nullptr;   // placeholder for the next text field (InputTextWithHint)
bool  g_focusNextField = false;
bool  g_focusSelectAll = false;     // together with g_focusNextField: select the whole text, not just put the caret at the end
int   g_editId = 0;                // the drag / slider box being typed into (0 = none)
char  g_editBuf[40] = {0};     // SetKeyboardFocusHere: the next text field takes the keyboard
bool  g_focusClaimed = false;    // a widget took the click this frame (keep focus)
unsigned g_frame = 0;            // frame counter (drives the text caret blink)
float    g_dragAccum = 0.0f;     // sub-unit accumulator for DragInt while dragging

// Menu bar / open-menu state.
bool  g_inMenuBar = false;
int   g_popupSubOpen = 0;   // open inline submenus inside a plain popup (EndMenu closes them)
float g_barX = 0.0f, g_barY = 0.0f, g_barH = 0.0f;
struct MenuKb { int id = 0; int count = 0; unsigned long long mask = 0; int idx = -1; bool childOpen = false, childOpenNext = false;
                bool keyMoved = false; };   // keyMoved: the arrows moved the highlight this frame (a scrolling list brings it into view)
MenuKb g_menuKbs[16];
MenuKb* kbSlot(int id) {
    for (auto& s : g_menuKbs) if (s.id == id) return &s;
    for (auto& s : g_menuKbs) if (s.id == 0) { s = MenuKb(); s.id = id; return &s; }
    g_menuKbs[0] = MenuKb(); g_menuKbs[0].id = id; return &g_menuKbs[0];   // table full: reuse the first slot
}
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
CurMenu g_curMenu;
CurMenu g_menuStack[4];
struct TabSel { int bar = 0; int sel = 0; };
TabSel g_tabSel[16];
// The remembered order and widths of a Reorderable tab bar's tabs (the tabs are placed from last frame's widths, so a drag can swap them).
struct TabOrder { int bar = 0; int n = 0; int ids[32] = {}; float w[32] = {}; unsigned long long lastFrame = 0; };
TabOrder g_tabOrders[16];
struct TabBarCtx { bool active = false; int id = 0; float x = 0, y = 0, w = 0, h = 0, cx = 0, tr = 0; int* sel = nullptr; bool seenSel = false; int firstId = 0;
                   int flags = 0; TabOrder* ord = nullptr; int seen[32] = {}; int nSeen = 0; bool fresh = false; };
TabBarCtx g_tabBar;
// Hover delay tracking (IsItemHovered with the Delay / Stationary flags) and the tooltip scope.
unsigned g_hoverKey = 0; unsigned long long g_hoverFrame = 0; double g_hoverStart = 0.0;
bool g_inTip = false;
                          // the menus a nested BeginMenu came from (outermost first)
int g_menuDepth = 0;
Theme g_theme;

// Scoped color-override stack: each entry remembers a theme color slot and its prior
// value, so PopStyleColor can restore it. Reset every BeginFrame to survive imbalance.
struct ColorSave { unsigned char* field; unsigned char prev[4]; int ext = -1; bool prevSet = false; };   // ext >= 0: a colour of Theme::colors
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
// The colours beyond the original eight: set (Theme::colorSet) or following the base theme.
inline bool colorIsSet(int id) { return id >= 8 && id < 64 && ((g_theme.colorSet >> id) & 1ull) != 0; }
// `dflt` unless the colour was set: how a widget honours an override while keeping its own default look.
inline const unsigned char* pick(int id, const unsigned char* dflt) { return colorIsSet(id) ? g_theme.colors[id] : dflt; }
// The base-theme value of a colour that was never set (what GetStyleColorVec4 answers; the classic look's default).
void deriveColor(int id, unsigned char out[4]) {
    auto cp = [&](const unsigned char* c) { out[0] = c[0]; out[1] = c[1]; out[2] = c[2]; out[3] = c[3]; };
    auto rgba = [&](int r, int g, int b, int a) { out[0] = (unsigned char)r; out[1] = (unsigned char)g; out[2] = (unsigned char)b; out[3] = (unsigned char)a; };
    auto tint = [&](const unsigned char* c, int a) { out[0] = c[0]; out[1] = c[1]; out[2] = c[2]; out[3] = (unsigned char)a; };
    switch (id) {
        case Col_TextDisabled: cp(g_theme.textDim); break;
        case Col_WindowBg: case Col_ChildBg: case Col_PopupBg: cp(g_theme.panel); break;
        case Col_BorderShadow: rgba(0, 0, 0, 0); break;
        case Col_FrameBg: case Col_FrameBgHovered: case Col_FrameBgActive: cp(g_theme.field); break;
        case Col_TitleBg: case Col_TitleBgActive: case Col_TitleBgCollapsed: cp(g_theme.titleBg); break;
        case Col_MenuBarBg: cp(g_theme.bgDown); break;
        case Col_ScrollbarBg: cp(g_theme.track); break;
        case Col_ScrollbarGrab: cp(g_theme.accent); break;
        case Col_ScrollbarGrabHovered: case Col_ScrollbarGrabActive: cp(g_theme.text); break;
        case Col_CheckMark: cp(g_theme.accent); break;
        case Col_SliderGrab: cp(g_theme.accent); break;
        case Col_SliderGrabActive: cp(g_theme.text); break;
        case Col_Header: cp(g_theme.track); break;
        case Col_HeaderHovered: cp(g_theme.bgHover); break;
        case Col_HeaderActive: cp(g_theme.bgDown); break;
        case Col_Separator: case Col_SeparatorHovered: case Col_SeparatorActive: cp(g_theme.border); break;
        case Col_ResizeGrip: cp(g_theme.textDim); break;
        case Col_ResizeGripHovered: case Col_ResizeGripActive: cp(g_theme.accent); break;
        case Col_Tab: case Col_TabUnfocused: cp(g_theme.track); break;
        case Col_TabHovered: cp(g_theme.bgHover); break;
        case Col_TabActive: case Col_TabUnfocusedActive: cp(g_theme.bg); break;
        case Col_DockingPreview: tint(g_theme.accent, 77); break;
        case Col_DockingEmptyBg: cp(g_theme.panel); break;
        case Col_PlotLines: cp(g_theme.accent); break;
        case Col_PlotLinesHovered: cp(g_theme.text); break;
        case Col_PlotHistogram: cp(g_theme.accent); break;
        case Col_PlotHistogramHovered: cp(g_theme.text); break;
        case Col_TableHeaderBg: cp(g_theme.bgDown); break;
        case Col_TableBorderStrong: case Col_TableBorderLight: cp(g_theme.border); break;
        case Col_TableRowBg: rgba(0, 0, 0, 0); break;
        case Col_TableRowBgAlt: rgba(255, 255, 255, 11); break;
        case Col_TextSelectedBg: tint(g_theme.accent, 110); break;
        case Col_DragDropTarget: cp(g_theme.accent); break;
        case Col_NavHighlight: cp(g_theme.accent); break;
        case Col_NavWindowingHighlight: rgba(255, 255, 255, 178); break;
        case Col_NavWindowingDimBg: rgba(204, 204, 204, 51); break;
        case Col_ModalWindowDimBg: rgba(0, 0, 0, 140); break;
        default: rgba(0, 0, 0, 0); break;
    }
}
// Any colour as bytes: the original eight, a set one, or the derived default (valid until the next call with `tmp`).
inline const unsigned char* styleColor(int id, unsigned char tmp[4]) {
    if (id >= 0 && id < 8) return themeColor(id);
    if (id < 8 || id >= Col_COUNT) { tmp[0] = tmp[1] = tmp[2] = tmp[3] = 0; return tmp; }
    if (colorIsSet(id)) return g_theme.colors[id];
    deriveColor(id, tmp);
    return tmp;
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
LayoutState g_lay;
// BeginGroup / EndGroup: the bounding box of the items placed since BeginGroup (nested up to 8 deep).
struct GroupBox { float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f; bool any = false; };
GroupBox g_grp[8];
int      g_grpTop = 0;
inline void groupAdd(float x, float y, float w, float h) {
    if (g_grpTop <= 0) return;
    GroupBox& g = g_grp[g_grpTop - 1];
    if (x < g.x0) g.x0 = x;
    if (y < g.y0) g.y0 = y;
    if (x + w > g.x1) g.x1 = x + w;
    if (y + h > g.y1) g.y1 = y + h;
    g.any = true;
}
bool  g_posOverride = false;      // SetCursorScreenPos: the next placed item uses (g_posX, g_posY)
bool  g_posSameLine = false;      // ... and it is on the line of the item before it (SameLine, then SetCursorPosX)
bool  g_centerNext = false;       // CenterNextOnLine: the next item, on the line of a taller one, is centred on it
float g_posX = 0.0f, g_posY = 0.0f;

// Saved parent layout + child metadata while inside a BeginChild/EndChild pair.
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
// Per-table memory that outlives a frame: the sort keys and the row heights of the last frame (for row backgrounds).
constexpr int kTblRows = 256, kTblSortKeys = 4;
struct TblStore {
    int id = 0; bool used = false; unsigned long long lastFrame = 0;
    float rowH[kTblRows] = {};
    int sortN = 0; int sortCol[kTblSortKeys] = {}; bool sortDesc[kTblSortKeys] = {};
    bool dirty = false, inited = false;
};
TblStore g_tstore[8];
TblStore* tblStoreFor(int id) {
    TblStore* free = nullptr; TblStore* oldest = &g_tstore[0];
    for (TblStore& t : g_tstore) {
        if (t.used && t.id == id) return &t;
        if (!t.used && !free) free = &t;
        if (t.lastFrame < oldest->lastFrame) oldest = &t;
    }
    TblStore* t = free ? free : oldest;
    *t = TblStore(); t->used = true; t->id = id;
    return t;
}
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
TableState g_tbl;
int g_sortSpecsFor = 0; TableSortSpecs* g_sortSpecsOut = nullptr;   // the specs struct TableGetSortSpecs handed out (EndTable reads specsDirty back from it)

// Popup body layout context (uses LayoutState, so defined after it).
struct PopupCtx { LayoutState lay; int id; float x, y, w; float* hSlot; float* wSlot; float padX, padY; bool exact;
                  float subX0 = 0, subY0 = 0, subX1 = 0, subY1 = 0; };   // the sub-menu lists opened from its rows this frame (a press there is inside)
PopupCtx g_popupStack[4];
int      g_popupTop = 0;
extern int g_textScaleTop;   // defined with the text-scale stack below (reset here in BeginFrame)
extern int g_ncards;         // defined with the card stack below (reset here in BeginFrame)

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
struct WinSlot { int id = 0; float x = 0, y = 0; bool used = false; float w = 0, h = 0; bool sized = false;
                 bool posInit = false, sizeInit = false, collInit = false; unsigned long long lastFrame = 0; char title[64] = {0};   // title: for WindowRef::Name
                 unsigned long long z = 0;          // stacking order: higher is in front, 0 is the bottom (docked content, NoBringToFront)
                 float scroll = 0, maxScroll = 0;   // content scroll and the most it could scroll last frame
                 float autoW = 0, autoH = 0; bool measured = false;   // AlwaysAutoResize: the content's size from last frame
               };
WinSlot g_wins[64];   // 64: a dock space (OkayUIDock) can hold many windows

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
unsigned long long g_frameCount = 0;              // frames begun so far
// Typed-payload drag and drop (BeginDragDropSource / BeginDragDropTarget), next to the older int-payload one (g_dd).
struct TypedDrag {
    bool active = false, armed = false, hasPayload = false; int srcId = 0; float armX = 0, armY = 0;
    DragPayload pay; char preview[64] = {0};
};
TypedDrag g_tdd;
bool g_tddExt = false, g_tddExtSeen = false, g_tddExtReleased = false;   // the drag came from outside OkayUI (SetExternalDragDrop)
float g_winBaseScale = 0.0f;          // the text scale the window started with (SetWindowFontScale multiplies it; End restores it)
MouseCursor g_cursor = MouseCursor_Arrow;
IO g_io;                              // GetIO(): filled in BeginFrame
bool ioTypedTextConsumed();           // defined by GetIO(): the ported code took this frame's typed characters
ImeRect g_ime;                        // GetImeRect(): the focused text input's caret, set while the frame is built
bool g_ptrOverWindow = false, g_ptrOverWindowPrev = false;   // the pointer was over a window's rectangle this / last frame (WantCaptureMouse)
double g_clock = 0.0;                             // seconds: Input::time when the host gives it, else frames / 60
double g_lastPressTime = -1.0; float g_lastPressX = 0, g_lastPressY = 0;   // for double clicks
bool   g_dblClick = false;                        // this frame's press was the second of a double click
bool   g_nextWinFocus = false;                    // SetNextWindowFocus: the next ImGui-style Begin takes the focus
int    g_nextFrameKbd = -1;                       // SetNextFrameWantCaptureKeyboard: -1 none, else 0 / 1 for the next BeginFrame
struct NextCollapse { bool set = false; bool value = false; int cond = 0; };
NextCollapse g_nextCollapse;                      // SetNextWindowCollapsed, used up by the next ImGui-style Begin
float  g_nextBgAlpha = -1.0f;                     // SetNextWindowBgAlpha: -1 none, else 0..1 for the next ImGui-style Begin
float  g_bgAlphaCore = -1.0f;                     // the same, handed to the core Begin that draws the body
bool   g_anyHoverNow = false, g_anyHoverPrev = false;   // some item was under the pointer this frame so far / last frame
float  g_pressX = 0, g_pressY = 0;                // where the left button went down (for dragging)
bool   g_keyNow[KeyId_COUNT] = {}, g_keyPrev[KeyId_COUNT] = {};   // named keys: this frame and last
double g_keySince[KeyId_COUNT] = {};              // when each held key went down
double g_keyNextRepeat[KeyId_COUNT] = {};
float g_mouseDX = 0.0f, g_mouseDY = 0.0f;         // cursor delta this frame

// ID stack: an extra seed mixed into label hashing so same-labelled widgets in a
// loop stay distinct. PushID/PopID adjust g_idSeed; a small stack remembers prior
// seeds to restore on pop.
unsigned g_idSeed = 0;
unsigned g_idStack[32];
int      g_idTop = 0;
int g_idSkipped = 0;                 // pushes skipped while the ID stack was full, so matching pops are skipped too
inline void pushIdSeed(unsigned mix) {
    if (g_idTop >= 32) {   // stack full: skip the push entirely so PopID cannot restore a seed it never saved
        ++g_idSkipped;
        return;
    }
    g_idStack[g_idTop++] = g_idSeed;
    g_idSeed = (g_idSeed ^ mix) * 16777619u;
}
inline void popIdSeed() {
    if (g_idSkipped > 0) { --g_idSkipped; return; }   // this pop matches a skipped push: leave the stack alone
    if (g_idTop > 0) g_idSeed = g_idStack[--g_idTop];
}

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
// The glyph for the character at *p, and *p moved past it: an ASCII byte is looked up in the size's table right here (text is
// nearly all ASCII), anything else is decoded and searched.
inline const fontatlas::Glyph* asciiGlyph(const fontatlas::Size& z, const char*& p) {
    const unsigned char b = (unsigned char)*p;
    if (b < 0x80) { ++p; return z.ascii[b] > 0 ? &z.glyphs[z.ascii[b] - 1] : nullptr; }
    return z.Find(fontatlas::NextCodepoint(&p));
}
// Width of `s` in font-cell units (multiply by textScale for pixels). With the
// launcher look it's the TrueType width at the 8-unit cell height (proportional).
inline float fMeasure(const char* s) {
    if (g_font->ttf && s) return static_cast<const okay::TtfFont*>(g_font->ttf)->Measure(s, 8.0f);   // a user font (AddFontTtf): its width at the 8-unit cell
    if (g_ttf && s) {
        if (okay::TtfFont* f = fontatlas::FallbackFont(s)) return f->Measure(s, 8.0f);
        const fontatlas::Size& z = g_ttf->Pick(16.0f);
        float w = 0.0f;
        for (const char* p = s; *p;) {
            const fontatlas::Glyph* g = asciiGlyph(z, p); if (!g) g = z.Find('?');
            if (g) w += g->advance;
        }
        return w * 8.0f / z.px;
    }
    return (float)(fW() * cstrlen(s));
}
// Average character width in cell units (spacing glyph-like icons).
inline float fCharW() { return (g_ttf || g_font->ttf) ? fMeasure("n") : (float)fW(); }
// A text's width as Dear ImGui measures it for wrapping: each glyph's advance at the drawn size rounded to a whole pixel (its baked
// advances are whole pixels; ours are exact, so a long line came out about 2 pixels wider than Dear ImGui's and a wrapped note broke a
// word earlier: the Vehicle's "Space =", stage 8). In pixels at the current text scale.
inline float fMeasureWhole(const char* s, float scale) {
    if (g_ttf && s && !fontatlas::FallbackFont(s)) {
        const fontatlas::Size& z = g_ttf->Pick(16.0f);
        const float k = 8.0f / z.px * scale;
        float w = 0.0f;
        for (const char* p = s; *p;) {
            const fontatlas::Glyph* g = asciiGlyph(z, p); if (!g) g = z.Find('?');
            if (g) w += std::floor(g->advance * k + 0.5f);
        }
        return w;
    }
    return s && *s ? fMeasure(s) * scale : 0.0f;
}

// StyleVar_FramePadding (y): -1 = OkayUI's frames (a text line + 15, or + 12 without the launcher look); 0 or more: a text line plus
// twice that (Dear ImGui's frame height). Checkbox and RadioButton boxes follow it too (boxSize).
float g_framePadY = -1.0f;
// StyleVar_FramePadding (x; < 0 = none pushed): where a framed combo's text starts (OkayUI's own combos start it 6 pixels in).
float g_framePadX = -1.0f;
// StyleVar_ScrollbarSize (< 0 = none pushed: OkayUI's own 10-pixel bars): the width of the scroll bars of children and scrolling tables.
// A pushed size also gives Dear ImGui's proportions: the bar inside a framed child's outline and its grab 2 pixels in on every side.
float g_sbSize = -1.0f;
// StyleVar_FramedWidgets (OkayUI's own; 0 = off): check boxes as a frame with a check mark, combos as a field with a square arrow button.
bool g_framedWidgets = false;
// A pushed StyleVar_WindowPadding (y; < 0 = none pushed): a framed or padded child (the flags form of BeginChild) then pads its content by
// the window padding (x, y) instead of 6 pixels each way.
float g_childPadY = -1.0f;
inline float rowH() { return fH() * g_theme.textScale + (g_framePadY >= 0.0f ? 2.0f * g_framePadY : (g_look ? 15.0f : 12.0f)); }
inline float textH() { return fH() * g_theme.textScale; }
inline float labelW(const char* s) { return s && *s ? fMeasure(s) * g_theme.textScale : 0.0f; }
// ImGui's convention for labels: what follows "##" is only part of the widget id and is not shown.
struct VisLabel {
    char b[128];
    explicit VisLabel(const char* s) {
        int n = 0;
        for (; s && s[n] && n < 127; ++n) if (s[n] == '#' && s[n + 1] == '#') break;
        for (int i = 0; i < n; ++i) b[i] = s[i];
        b[n] = 0;
    }
};

// Width of a label's visible part (what follows "##" is not shown, so it takes no room).
inline float visW(const char* s) { return s && *s ? labelW(VisLabel(s).b) : 0.0f; }

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
int   g_winItemWDepth = 0;   // item-width stack height just above the default SetWindowItemWidthDefault pushed (0: none pushed)
// Remaining width from the next item's left edge to the content's right edge.
inline float availW() {
    float real = g_lay.ox + g_lay.contentW - cursorX();
    float over = g_nextItemW > 0.0f ? g_nextItemW : (g_itemWTop > 0 ? g_itemWStack[g_itemWTop - 1] : -1.0f);
    if (over > 0.0f && over < real) return over;
    return real;
}

// True when SetNextItemWidth / PushItemWidth set a width for the next item. In Dear ImGui that width is the FIELD's, the label comes on top of it;
// the labelled widgets that take it that way add their label's room to it (see BeginCombo).
inline bool widthOverridden() { return g_nextItemW > 0.0f || g_itemWTop > 0; }
inline float realAvailW() { return g_lay.ox + g_lay.contentW - cursorX(); }
// Reserve a w*h slot for the next item; returns its top-left and advances the cursor
// to the next line (SameLine() keeps it on the current line instead).
inline bool hit(float x, float y, float w, float h);
void place(float w, float h, float& x, float& y) {
    bool sameLine = false;
    const float lineH = g_lay.prevH;   // the item before, for CenterNextOnLine
    if (g_posOverride) {   // SetCursorScreenPos: this item goes exactly there
        g_posOverride = false;
        x = g_posX; y = g_posY; g_lay.pendingSameLine = false; g_lay.sameLineSpacing = -1.0f;
        sameLine = g_posSameLine; g_posSameLine = false;
    } else if (g_lay.pendingSameLine) {
        float sp = g_lay.sameLineSpacing >= 0.0f ? g_lay.sameLineSpacing : g_lay.spacingX;
        x = g_lay.prevX + g_lay.prevW + sp;
        y = g_lay.hasLineTop && !g_centerNext ? g_lay.lineTopY : g_lay.prevY;   // one centred on the item before it: on that item
        g_lay.pendingSameLine = false;
        g_lay.sameLineSpacing = -1.0f;
        sameLine = true;
    } else {
        x = leftEdge();
        y = g_lay.cy;
    }
    if (g_centerNext && sameLine && lineH > h) y += (lineH - h) * 0.5f;   // CenterNextOnLine
    g_centerNext = false;
    g_lay.prevX = x; g_lay.prevY = y; g_lay.prevW = w; g_lay.prevH = h; g_lay.hasLineTop = false;
    if (!g_anyHoverNow && hit(x, y, w, h)) g_anyHoverNow = true;
    if (x + w > g_lay.maxRight) g_lay.maxRight = x + w;
    groupAdd(x, y, w, h);
    g_lastId = 0;   // the widget that follows registers its own id
    g_lay.cx = leftEdge();
    // The next line starts below the tallest item of this one, not just below the last (a short
    // item after a tall one on the same line used to let the next line overlap the tall one).
    const float bottom = y + h + g_lay.spacingY;
    g_lay.cy = (sameLine && g_lay.cy > bottom) ? g_lay.cy : bottom;
    g_nextItemW = -1.0f;   // a one-shot SetNextItemWidth applies to just this item
}

inline SDL_Color toColor(const unsigned char c[4]) {
    SDL_Color sc; sc.r = c[0]; sc.g = c[1]; sc.b = c[2]; sc.a = c[3];
    if (g_disCount > 0 && !g_rawDraw) {   // inside BeginDisabled: halfway to the panel colour, alpha kept
        const unsigned char* p = g_theme.panel;   // (so stacked layers of one widget do not blend through each other)
        sc.r = (unsigned char)((sc.r + p[0]) / 2); sc.g = (unsigned char)((sc.g + p[1]) / 2); sc.b = (unsigned char)((sc.b + p[2]) / 2);
    }
    return sc;
}

// Append an axis-aligned rectangle (two triangles) in a flat color, to whichever
// batch is active (main, or overlay when g_toOverlay is set).
// Wholly outside the current clip rectangle of the main batch: nothing of it would show, so its geometry is not emitted (a long list in
// a scrolling child, a section scrolled out of view). The overlay batch has no clip and is never culled.
inline bool clippedAway(float x0, float y0, float x1, float y1) {
    if (g_toOverlay || !g_curClipOn) return false;
    const SDL_Rect& r = g_curClipRect;
    return x1 <= (float)r.x || y1 <= (float)r.y || x0 >= (float)(r.x + r.w) || y0 >= (float)(r.y + r.h);
}
void quad(float x, float y, float w, float h, const unsigned char c[4]) {
    if (clippedAway(x, y, x + w, y + h)) return;
    SDL_Vertex* V = g_toOverlay ? g_ovVerts : g_verts;
    int*        I = g_toOverlay ? g_ovIdx   : g_idx;
    int&        nv = g_toOverlay ? g_onv : g_nv;
    int&        ni = g_toOverlay ? g_oni : g_ni;
    if (g_cullRow && !g_toOverlay) return;
    if (nv + 4 > kMaxVerts || ni + 6 > kMaxIdx) { noteDrop(); return; }   // batch full: counted and reported
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
    if (g_cullRow) return;
    setTexture(tex);
    if (g_nv + 4 > kMaxVerts || g_ni + 6 > kMaxIdx) { noteDrop(); setTexture(g_defTex); return; }
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
    if (g_cullRow && !g_toOverlay) return;
    if (nv + 4 > kMaxVerts || ni + 6 > kMaxIdx) { noteDrop(); return; }
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
    if (g_cullRow && !g_toOverlay) return;
    if (nv + 4 > kMaxVerts || ni + 6 > kMaxIdx) { noteDrop(); return; }
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
    if (g_cullRow && !g_toOverlay) return;
    if (nv + 3 > kMaxVerts || ni + 3 > kMaxIdx) { noteDrop(); return; }
    const SDL_Color sc = toColor(c);
    const int base = nv;
    SDL_FPoint uv; uv.x = g_whiteU; uv.y = g_whiteV;
    SDL_Vertex v; v.color = sc; v.tex_coord = uv;
    v.position.x = ax; v.position.y = ay; V[nv++] = v;
    v.position.x = bx; v.position.y = by; V[nv++] = v;
    v.position.x = cx; v.position.y = cy; V[nv++] = v;
    I[ni++] = base + 0; I[ni++] = base + 1; I[ni++] = base + 2;
}

// The rim of a unit circle cut into n wedges: (cos, sin) of step * i for i = 1..n, worked out once per n (the same float
// angles fillCircle always used, so the geometry is unchanged). n is 3..128.
const float* unitCircle(int n) {
    static float* table[129] = {};
    if (!table[n]) {
        float* t = new float[(std::size_t)n * 2];
        const float step = 6.2831853f / n;
        for (int i = 1; i <= n; ++i) { const float a = step * i; t[(i - 1) * 2] = SDL_cosf(a); t[(i - 1) * 2 + 1] = SDL_sinf(a); }
        table[n] = t;
    }
    return table[n];
}
// A filled circle as a triangle fan of `segments` wedges that share the centre and rim vertices (the same triangles as one
// tri() per wedge, with a third of the vertices).
void fillCircle(float cx, float cy, float radius, const unsigned char c[4], int segments = 24) {
    if (segments < 3) segments = 3;
    if (segments > 128) segments = 128;
    SDL_Vertex* V = g_toOverlay ? g_ovVerts : g_verts;
    int*        I = g_toOverlay ? g_ovIdx   : g_idx;
    int&        nv = g_toOverlay ? g_onv : g_nv;
    int&        ni = g_toOverlay ? g_oni : g_ni;
    if (g_cullRow && !g_toOverlay) return;
    if (nv + segments + 2 > kMaxVerts || ni + segments * 3 > kMaxIdx) { noteDrop(); return; }
    const float* u = unitCircle(segments);
    SDL_Vertex v; v.color = toColor(c); v.tex_coord.x = g_whiteU; v.tex_coord.y = g_whiteV;
    const int base = nv;
    v.position.x = cx; v.position.y = cy; V[nv++] = v;
    v.position.x = cx + radius; v.position.y = cy; V[nv++] = v;   // angle 0
    for (int i = 0; i < segments; ++i) { v.position.x = cx + radius * u[i * 2]; v.position.y = cy + radius * u[i * 2 + 1]; V[nv++] = v; }
    for (int i = 1; i <= segments; ++i) { I[ni++] = base; I[ni++] = base + i; I[ni++] = base + i + 1; }
}

// The four quarter-circle corners of rrect, 5 wedges each: the unit-circle points are worked out once (the same float angles
// arcFan would use, so the geometry is unchanged) instead of 12 sines and cosines per corner on every call.
struct CornerTable {
    float c[4][6], s[4][6];
    CornerTable() {
        const float PI = 3.14159265f;
        const float a0[4] = {PI, 1.5f * PI, 0.0f, 0.5f * PI}, a1[4] = {1.5f * PI, 2.0f * PI, 0.5f * PI, PI};
        for (int q = 0; q < 4; ++q) {
            const float step = (a1[q] - a0[q]) / 5;
            for (int i = 0; i <= 5; ++i) { const float a = i == 0 ? a0[q] : a0[q] + step * i; c[q][i] = SDL_cosf(a); s[q][i] = SDL_sinf(a); }
        }
    }
};
// One corner as a fan that shares its centre and rim points (7 vertices, 5 triangles): the same triangles arcFan emits.
void cornerFan(float cx, float cy, float r, int q, const unsigned char c[4]) {
    static const CornerTable T;
    SDL_Vertex* V = g_toOverlay ? g_ovVerts : g_verts;
    int*        I = g_toOverlay ? g_ovIdx   : g_idx;
    int&        nv = g_toOverlay ? g_onv : g_nv;
    int&        ni = g_toOverlay ? g_oni : g_ni;
    if (g_cullRow && !g_toOverlay) return;
    if (nv + 7 > kMaxVerts || ni + 15 > kMaxIdx) { noteDrop(); return; }
    SDL_Vertex v; v.color = toColor(c); v.tex_coord.x = g_whiteU; v.tex_coord.y = g_whiteV;
    const int base = nv;
    v.position.x = cx; v.position.y = cy; V[nv++] = v;
    for (int i = 0; i <= 5; ++i) { v.position.x = cx + r * T.c[q][i]; v.position.y = cy + r * T.s[q][i]; V[nv++] = v; }
    for (int i = 1; i <= 5; ++i) { I[ni++] = base; I[ni++] = base + i; I[ni++] = base + i + 1; }
}

// A filled rounded rectangle (falls back to a plain quad when rad <= 0). Decomposed
// into three bars + four quarter-circle corners so translucent fills never overlap.
void rrect(float x, float y, float w, float h, float rad, const unsigned char c[4]) {
    if (clippedAway(x, y, x + w, y + h)) return;
    if (rad <= 0.5f || w <= 0.0f || h <= 0.0f) { quad(x, y, w, h, c); return; }
    if (rad > w * 0.5f) rad = w * 0.5f;
    if (rad > h * 0.5f) rad = h * 0.5f;
    quad(x, y + rad, w, h - 2 * rad, c);            // middle band (full width)
    quad(x + rad, y, w - 2 * rad, rad, c);          // top strip
    quad(x + rad, y + h - rad, w - 2 * rad, rad, c);// bottom strip
    cornerFan(x + rad,     y + rad,     rad, 0, c);  // top-left
    cornerFan(x + w - rad, y + rad,     rad, 1, c);  // top-right
    cornerFan(x + w - rad, y + h - rad, rad, 2, c);  // bottom-right
    cornerFan(x + rad,     y + h - rad, rad, 3, c);  // bottom-left
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
    if (g_cullRow && !g_toOverlay) return;
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
        if (g->x1 > g->x0 && g->y1 > g->y0 && !(nv + 4 <= kMaxVerts && ni + 6 <= kMaxIdx)) noteDrop();
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
    if (!str || (g_cullRow && !g_toOverlay)) return;
    { const float lh = fH() * s; if (clippedAway(x - lh, y - lh, 1e30f, y + 2.0f * lh)) return; }   // a line above, below or right of the clip
    if (g_font->ttf) { drawTextWith(const_cast<okay::TtfFont*>(static_cast<const okay::TtfFont*>(g_font->ttf)), x, y, str, s, c); return; }
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
            const fontatlas::Glyph* g = asciiGlyph(z, p); if (!g) g = z.Find('?');
            if (!g) continue;
            if (g->x1 > g->x0 && !(nv + 4 <= kMaxVerts && ni + 6 <= kMaxIdx)) noteDrop();
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
        // One quad per horizontal run of lit pixels (not per pixel): the same pixels with about a
        // third of the vertices, so a screen of classic-look text fits the fixed batch.
        for (int gy = 0; gy < fH(); ++gy) {
            int gx = 0;
            while (gx < fW()) {
                if (!fPix(*p, gx, gy)) { ++gx; continue; }
                const int g0 = gx;
                while (gx < fW() && fPix(*p, gx, gy)) ++gx;
                quad(penX + g0 * s, y + gy * s, (float)(gx - g0) * s, s, c);
            }
        }
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
// The tick of a framed check box (StyleVar_FramedWidgets): two strokes an eighth of the box thick, from the left of the middle
// down to a point a quarter up from the bottom and on to the upper right, inside a box at (x, y) of side `sz`. The proportions were
// measured from Dear ImGui's box in the editor at 34 pixels; each stroke runs half its thickness past the corner so the joint is full.
void framedTick(float x, float y, float sz, const unsigned char c[4]) {
    const float t = std::max(1.5f, sz * 0.125f);
    const float ax = x + sz * 0.15f, ay = y + sz * 0.52f, bx = x + sz * 0.39f, by = y + sz * 0.73f, cx = x + sz * 0.77f, cy = y + sz * 0.31f;
    const float l1 = std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay)), e = t * 0.5f / std::max(l1, 0.001f);
    line(ax, ay, bx + (bx - ax) * e, by + (by - ay) * e, t, c);
    line(bx, by, cx, cy, t, c);
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

// ---- Mouse hit testing with overlays ---------------------------------------------
// Overlays (combo lists, popups, menu dropdowns) draw on top of later widgets, so
// they must also take the mouse from the widgets under them. Each open overlay
// registers its rectangle; widgets test against LAST frame's rectangles (one frame of
// lag, like any immediate-mode toolkit). A "modal" overlay (combo list, popup) makes
// every ordinary widget inert while it is open, so the click that closes it is not
// passed on to whatever is below. Widgets inside a popup body, and the overlay
// owners themselves (hitOverlay), are not affected.
struct OvRect { float x, y, w, h; bool modal; };
OvRect g_ovCur[16];  int g_ovCurN = 0;
OvRect g_ovPrev[16]; int g_ovPrevN = 0;
inline void registerOverlay(float x, float y, float w, float h, bool modal) {
    if (g_ovCurN < 16) g_ovCur[g_ovCurN++] = OvRect{ x, y, w, h, modal };
}
// The open popups' boxes with their depth in the popup stack (1 = opened from a window), this frame's and last frame's. A widget in a
// popup body does not take the pointer where a popup opened from it (deeper, drawn above it) covered it on the last frame: a press on
// a right-click menu opened from a row of a list must not also click the list rows under that menu.
struct PopRect { float x, y, w, h; int depth; };
PopRect g_popCur[8], g_popPrev[8]; int g_popCurN = 0, g_popPrevN = 0;
// ---- Window stacking (z-order) -------------------------------------------------------------------
// Every window has a stamp (WinSlot::z): higher draws in front, a click raises it, 0 is the bottom (docked content). The windows
// begun this frame register their rectangles (g_wCur); last frame's rectangles (g_wPrev) cover the windows not begun yet. A
// widget of a window ignores the pointer where a window with a higher stamp covers it, so a click never reaches what is behind.
// The geometry follows the stamps: the windows' index ranges are put in stamp order when the frame ends (reorderWindows).
struct WinRect { int id; unsigned long long z; float x, y, w, h; };
WinRect g_wCur[64], g_wPrev[64]; int g_wCurN = 0, g_wPrevN = 0;
unsigned long long g_zCounter = 0;
bool g_wheelUsed = false;   // a scrolling child took this frame's wheel (the window does not scroll as well)
bool g_escapeUsed = false;  // a popup closed on this frame's Escape (the popups it was opened from stay open)
bool g_hitWinOn = false; int g_hitWinId = 0; unsigned long long g_hitWinZ = 0;   // the window being built, for hit()
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
WinRec g_wrec[64]; int g_nwrec = 0; int g_wrecOpen = -1;
inline void wrecEnd() { if (g_wrecOpen >= 0) { g_wrec[g_wrecOpen].i1 = g_ni; g_wrecOpen = -1; } }
inline void wrecBegin(int id, unsigned long long z) {
    wrecEnd();
    if (g_nwrec >= 64) return;
    g_wrecOpen = g_nwrec++;
    g_wrec[g_wrecOpen] = WinRec{id, z, g_ni, g_ni, g_nv, g_nspans, g_curClipOn, g_curClipRect, g_curTex};
}
// The windows' geometry in stamp order. Free geometry between windows keeps its place in the call order; only the windows
// trade places, so a frame whose stamps agree with the call order is left exactly as it was.
void reorderWindows() {
    wrecEnd();
    const int n = g_nwrec;
    if (n < 2) return;
    int ord[64];
    for (int i = 0; i < n; ++i) ord[i] = i;
    for (int i = 1; i < n; ++i) { const int v = ord[i]; int j = i - 1; while (j >= 0 && g_wrec[ord[j]].z > g_wrec[v].z) { ord[j + 1] = ord[j]; --j; } ord[j + 1] = v; }
    bool same = true;
    for (int i = 0; i < n; ++i) if (ord[i] != i) { same = false; break; }
    if (same) return;
    static int tmp[kMaxIdx];
    static DrawSpan nsp[kMaxSpans];
    int nn = 0, nns = 0;
    bool fail = false;
    auto stateAt = [&](int idx) { DrawSpan st{0, false, SDL_Rect{0, 0, 0, 0}, g_defTex}; for (int k = 0; k < g_nspans && g_spans[k].startIdx <= idx; ++k) st = g_spans[k]; return st; };
    auto emit = [&](int a, int b) {
        if (b <= a || fail) return;
        if (nns + 1 + g_nspans > kMaxSpans) { fail = true; return; }
        DrawSpan st = stateAt(a); st.startIdx = nn;
        if (nns > 0 && nsp[nns - 1].startIdx == nn) nsp[nns - 1] = st; else nsp[nns++] = st;
        for (int k = 0; k < g_nspans; ++k) {
            const int si = g_spans[k].startIdx;
            if (si > a && si < b) { DrawSpan t = g_spans[k]; t.startIdx = nn + (si - a); if (nns > 0 && nsp[nns - 1].startIdx == t.startIdx) nsp[nns - 1] = t; else nsp[nns++] = t; }
        }
        for (int k = a; k < b; ++k) tmp[nn++] = g_idx[k];
    };
    int prevEnd = 0;
    for (int p = 0; p < n; ++p) {
        emit(prevEnd, g_wrec[p].i0);
        emit(g_wrec[ord[p]].i0, g_wrec[ord[p]].i1);
        prevEnd = g_wrec[p].i1;
    }
    emit(prevEnd, g_ni);
    if (fail || nn != g_ni) return;
    for (int k = 0; k < nn; ++k) g_idx[k] = tmp[k];
    for (int k = 0; k < nns; ++k) g_spans[k] = nsp[k];
    g_nspans = nns;
}
inline bool hitOverlay(float x, float y, float w, float h) {
    return !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, x, y, w, h);
}
// Is the mouse inside the current clip rectangle? A widget scrolled out of a child region (or past
// the edge of a clipped window) is invisible, so it must not take clicks either.
inline bool inClip() {
    if (g_toOverlay || g_clipTop <= 0) return true;
    const SDL_Rect& c = g_clipStack[g_clipTop - 1];
    return pointIn(g_in.mouseX, g_in.mouseY, (float)c.x, (float)c.y, (float)c.w, (float)c.h);
}
inline bool hit(float x, float y, float w, float h) {
    if (g_disCount > 0) return false;   // inside BeginDisabled: no hover, no click
    if (!hitOverlay(x, y, w, h)) return false;
    if (g_popupTop > 0) {   // a widget inside a popup body: only a deeper popup above it can take the pointer
        for (int i = 0; i < g_popPrevN; ++i) {
            const PopRect& r = g_popPrev[i];
            if (r.depth > g_popupTop && pointIn(g_in.mouseX, g_in.mouseY, r.x, r.y, r.w, r.h)) return false;
        }
        return true;
    }
    if (!inClip()) return false;
    if (g_hitWinOn && coveredByHigher(g_in.mouseX, g_in.mouseY, g_hitWinZ, g_hitWinId)) return false;   // a window above covers the pointer
    for (int i = 0; i < g_ovPrevN; ++i) {
        const OvRect& o = g_ovPrev[i];
        if (o.modal || pointIn(g_in.mouseX, g_in.mouseY, o.x, o.y, o.w, o.h)) return false;
    }
    return true;
}

// A float to a pixel coordinate: NaN becomes `nan`, anything huge is clamped (a plain (int) cast
// of either is undefined behaviour).
inline int clampPx(float v, int nan) {
    if (v != v) return nan;
    if (v > 1.0e7f) return 10000000;
    if (v < -1.0e7f) return -10000000;
    return (int)v;
}

inline float clamp01(float v) { return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f; }   // NaN -> 0

constexpr int kMaxHexSlots = 16;

} // namespace


namespace {
void flushTooltip();   // the frame's tooltip (asked for with SetTooltip / Tooltip), drawn last by finalizeFrame
void dropTooltip();
} // namespace
void BeginFrame(const Input& in) {
    g_in       = in;
    g_pressed  =  in.mouseDown && !g_prevDown;
    g_released = !in.mouseDown &&  g_prevDown;
    g_prevDown = in.mouseDown;
    if (in.escape && (g_tdd.active || g_tdd.armed || g_dd.active || g_dd.armed)) CancelDragDrop();   // Escape drops the drag (no drop)
    g_rPressed  = in.rightDown && !g_rPrevDown;
    g_rReleased = !in.rightDown && g_rPrevDown;
    g_rPrevDown = in.rightDown;
    g_mPressed  =  in.middleDown && !g_mPrevDown;
    g_mReleased = !in.middleDown &&  g_mPrevDown;
    g_mPrevDown = in.middleDown;
    g_activeAtFrameStart = g_active;
    g_anyHoverPrev = g_anyHoverNow; g_anyHoverNow = false;
    g_focusAtFrameStart = g_focus;
    g_focusLostPrev = g_focusLostEnd; g_focusLostEnd = 0;
    g_edFrameId = 0;
    if (g_edStickyId != 0 && g_active != g_edStickyId && g_focus != g_edStickyId && g_focusLostPrev != g_edStickyId) g_edStickyId = 0;
    if (g_pressed)  { g_dragOriginX[0] = in.mouseX; g_dragOriginY[0] = in.mouseY; }
    if (g_rPressed) { g_dragOriginX[1] = in.mouseX; g_dragOriginY[1] = in.mouseY; }
    if (g_mPressed) { g_dragOriginX[2] = in.mouseX; g_dragOriginY[2] = in.mouseY; }
    g_lastId = 0;
    g_disTop = g_disCount = 0; g_rawDraw = false;   // an unbalanced BeginDisabled never leaks into the next frame
    g_openPopupThisFrame = 0;
    g_popupTop = 0;
    PopStyleVar(32);                                // an unbalanced PushStyleVar never leaks into the next frame
    ++g_frameCount;
    dropTooltip();   // a tooltip belongs to the frame that asked for it
    if (g_winBaseScale > 0.0f) { g_theme.textScale = g_winBaseScale; g_winBaseScale = 0.0f; }   // a window left open never keeps its font scale
    g_cursor = MouseCursor_Arrow;
    g_ime = ImeRect();
    g_ptrOverWindowPrev = g_ptrOverWindow; g_ptrOverWindow = false;
    {
        const double prev = g_clock;
        g_clock = in.time > 0.0 ? in.time : (double)g_frameCount / 60.0;
        g_io.deltaTime = (g_frameCount > 1 && g_clock > prev) ? (float)(g_clock - prev) : 1.0f / 60.0f;
    }
    g_io.mousePos = Vec2{in.mouseX, in.mouseY}; g_io.mouseDelta = Vec2{0, 0}; g_io.mouseWheel = in.wheel;
    g_io.displaySize = Vec2{in.displayW, in.displayH};
    g_io.keyCtrl = in.ctrl || in.keyDown[KeyId_Ctrl]; g_io.keyShift = in.shift || in.keyDown[KeyId_Shift]; g_io.keyAlt = in.keyDown[KeyId_Alt];
    g_io.mouseDown[0] = in.mouseDown; g_io.mouseDown[1] = in.rightDown; g_io.mouseDown[2] = in.middleDown;
    g_io.wantCaptureKeyboard = g_focus != 0; g_io.wantTextInput = g_focus != 0;
    if (g_nextFrameKbd >= 0) { g_io.wantCaptureKeyboard = g_nextFrameKbd != 0; g_nextFrameKbd = -1; }   // SetNextFrameWantCaptureKeyboard
    for (int k = 0; k < KeyId_COUNT; ++k) {   // named keys: keep last frame's state, take this frame's
        g_keyPrev[k] = g_keyNow[k];
        g_keyNow[k] = in.keyDown[k] || (k == KeyId_Ctrl && in.ctrl) || (k == KeyId_Shift && in.shift);
        if (g_keyNow[k] && !g_keyPrev[k]) { g_keySince[k] = g_clock; g_keyNextRepeat[k] = g_clock + 0.5; }
    }
    g_dblClick = false;
    if (g_pressed) {
        const float dx = in.mouseX - g_lastPressX, dy = in.mouseY - g_lastPressY;
        g_dblClick = g_lastPressTime >= 0.0 && g_clock - g_lastPressTime < 0.30 && dx * dx + dy * dy < 36.0f;
        g_lastPressTime = g_dblClick ? -1.0 : g_clock;     // a third click is a new first click
        g_lastPressX = in.mouseX; g_lastPressY = in.mouseY;
        g_pressX = in.mouseX; g_pressY = in.mouseY;
    }
    g_mouseDX = in.mouseX - g_prevMouseX;
    g_mouseDY = in.mouseY - g_prevMouseY;
    if (g_mouseDX != 0.0f || g_mouseDY != 0.0f) g_lastMouseMove = g_clock;
    g_prevMouseX = in.mouseX;
    g_prevMouseY = in.mouseY;
    g_hot = 0;
    g_wrapPos = -1.0f; g_grpTop = 0; g_nextOpen = -1; g_toggledId = 0;
    g_dispW = in.displayW > 0.0f ? in.displayW : 0.0f; g_dispH = in.displayH > 0.0f ? in.displayH : 0.0f;
    g_focusClaimed = false;
    for (int i = 0; i < g_ovCurN; ++i) g_ovPrev[i] = g_ovCur[i];
    g_ovPrevN = g_ovCurN; g_ovCurN = 0;
    for (int i = 0; i < g_popCurN; ++i) g_popPrev[i] = g_popCur[i];
    g_popPrevN = g_popCurN; g_popCurN = 0;
    for (int i = 0; i < g_wCurN; ++i) g_wPrev[i] = g_wCur[i];   // last frame's window rectangles, for stacking
    g_wPrevN = g_wCurN; g_wCurN = 0; g_nwrec = 0; g_wrecOpen = -1; g_hitWinOn = false; g_wheelUsed = false; g_escapeUsed = false;
    g_lay.active = false;            // widgets called outside Begin/End no-op
    g_lay.pendingSameLine = false;
    ++g_frame;
    g_nv = 0; g_ni = 0; g_dropped = 0;
    g_onv = 0; g_oni = 0; g_toOverlay = false; g_novSpans = 0;
    g_inMenuBar = false; g_popupSubOpen = 0; g_curMenu.active = false; g_menuDepth = 0; g_tabBar.active = false;
    g_idSeed = 0; g_idTop = 0; g_idSkipped = 0;   // reset the ID stack each frame
    // Restore any colors left pushed by an imbalanced previous frame, then clear.
    PopStyleColor(g_colTop);
    g_colTop = 0;
    while (g_textScaleTop > 0) PopTextScale();   // an unbalanced PushTextScale never leaks into the next frame
    g_ncards = 0;                        // reset card stack (BeginCard reserves per frame)
    g_nextItemW = -1.0f; g_itemWTop = 0; g_winItemWDepth = 0;   // reset item-width overrides
    g_nspans = 0; g_clipTop = 0;           // reset draw spans/stack
    g_curClipOn = false; g_curTex = g_defTex;
    g_childTop = 0;                        // reset child-region stack
    g_col.active = false;                  // reset column layout
    g_tbl.active = false;                  // reset table layout
}

static float g_btnAlignX = 0.5f;   // StyleVar_ButtonTextAlign (x): 0 left, 0.5 centred, 1 right, inside a 12-pixel padding
bool Button(int id, float x, float y, float w, float h, const char* label) {
    const bool inside = hit(x, y, w, h);
    g_lastId = id; if (inside) g_hot = id;

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

    // Centered label (what follows "##" is only part of the id).
    const VisLabel vis(label);
    if (vis.b[0]) {
        const float s  = g_theme.textScale;
        const float tw = visW(label);
        const float th = fH() * s;
        if (tw > w + 16.0f) {   // a label much wider than its button is shortened with "..." (a clip rectangle does not reach the overlay layer that popups draw in)
            char cut[160]; const VisLabel vl(label); std::size_t n = 0;
            while (vl.b[n] && n < sizeof cut - 4) {
                std::size_t m = n + 1; while (vl.b[m] && ((unsigned char)vl.b[m] & 0xC0) == 0x80) ++m;   // whole UTF-8 characters
                std::memcpy(cut, vl.b, m); cut[m] = '.'; cut[m + 1] = '.'; cut[m + 2] = '.'; cut[m + 3] = 0;
                if (labelW(cut) > w - 8.0f) break;
                n = m;
            }
            std::memcpy(cut, vl.b, n); cut[n] = '.'; cut[n + 1] = '.'; cut[n + 2] = '.'; cut[n + 3] = 0;
            drawText(x + 4.0f, y + (h - th) * 0.5f, cut, s, g_theme.text);
        } else {
            // StyleVar_ButtonTextAlign is honoured with StyleVar_FramedWidgets (Dear ImGui's look); OkayUI's own buttons centre their label
            const float tx = (g_btnAlignX == 0.5f || !g_framedWidgets) ? x + (w - tw) * 0.5f : x + 12.0f + std::max(0.0f, w - 24.0f - tw) * g_btnAlignX;
            drawText(tx, y + (h - th) * 0.5f, vis.b, s, g_theme.text);
        }
    }
    return clicked;
}

void Label(float x, float y, const char* text) {
    if (text && *text) drawText(x, y, text, g_theme.textScale, g_theme.text);
}

static void panelColored(float x, float y, float w, float h, const unsigned char* fill) {
    const float rd = rnd();
    rrect(x, y, w, h, rd, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) rrect(x + b, y + b, w - 2.0f * b, h - 2.0f * b, rd > b ? rd - b : 0.0f, fill);
}
void Panel(float x, float y, float w, float h) { panelColored(x, y, w, h, pick(Col_WindowBg, g_theme.panel)); }

bool Checkbox(int id, float x, float y, float size, const char* label, bool* value) {
    if (!value) return false;
    // The label is part of the target (clicking the text toggles, like every desktop toolkit).
    const bool inside = hit(x, y, size + (label && *label ? 8.0f + visW(label) : 0.0f), size);
    g_lastId = id; if (inside) g_hot = id;

    bool changed = false;
    if (g_active == id) {
        if (g_released) { if (inside) { *value = !*value; changed = true; } g_active = 0; }
    } else if (inside && g_pressed) {
        g_active = id;
    }

    if (g_framedWidgets) {   // Dear ImGui's box: a rounded frame in the field colours (the accent at 45% once ticked, 55% under the
                             // pointer) and the tick in Col_CheckMark
        unsigned char tint[4];
        const unsigned char* fb = *value ? withAlpha(g_theme.accent, inside ? 0.55f : 0.45f, tint)
                                         : (g_active == id && inside) ? pick(Col_FrameBgActive, g_theme.bgDown)
                                         : inside ? pick(Col_FrameBgHovered, g_theme.bgHover) : pick(Col_FrameBg, g_theme.field);
        rrect(x, y, size, size, rnd(), fb);
        if (*value) framedTick(x, y, size, pick(Col_CheckMark, g_theme.accent));
        if (label && *label) drawText(x + size + 8.0f, y + (size - textH()) * 0.5f, label, g_theme.textScale, g_theme.text);
        return changed;
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
        rrect(x, y, size, size, rd, pick(Col_CheckMark, g_theme.accent));
        const unsigned char white[4] = {255, 255, 255, 255};
        tickMark(x, y, size, white);
    } else if (*value) {
        const float pad = size * 0.28f;
        rrect(x + pad, y + pad, size - 2.0f * pad, size - 2.0f * pad, rd * 0.5f, pick(Col_CheckMark, g_theme.accent));
    }
    // Label to the right, vertically centered against the box.
    if (label && *label) {
        const float s  = g_theme.textScale;
        const float th = fH() * s;
        drawText(x + size + 8.0f, y + (size - th) * 0.5f, VisLabel(label).b, s, g_theme.text);
    }
    return changed;
}

bool ToggleSwitch(int id, float x, float y, float w, float h, const char* label, bool* value) {
    if (!value || w <= 0.0f || h <= 0.0f) return false;
    const bool inside = hit(x, y, w + (label && *label ? 8.0f + visW(label) : 0.0f), h);
    g_lastId = id; if (inside) g_hot = id;

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
        drawText(x + w + 8.0f, y + (h - th) * 0.5f, VisLabel(label).b, s, g_theme.text);
    }
    return changed;
}

bool Slider(int id, float x, float y, float w, float h, float* value, float minV, float maxV) {
    if (!value || w <= 0.0f || maxV <= minV) return false;
    const bool inside = hit(x, y, w, h);
    g_lastId = id; if (inside) g_hot = id;

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
        fillCircle(kcx, y + h * 0.5f, kr, (g_active == id || inside) ? pick(Col_SliderGrabActive, g_theme.text) : pick(Col_SliderGrab, knob));
        return changed;
    }
    const float gy = y + h * 0.5f - 3.0f;     // 6px groove, vertically centered
    rrect(x, gy, w, 6.0f, 3.0f, g_theme.track);
    if (w * t > 0.0f) rrect(x, gy, w * t, 6.0f, 3.0f, g_theme.accent);
    const float hr = h * 0.42f;                // handle radius
    float hcx = x + w * t;
    if (hcx < x + hr) hcx = x + hr;
    if (hcx > x + w - hr) hcx = x + w - hr;
    const unsigned char* hc = (g_active == id || inside) ? pick(Col_SliderGrabActive, g_theme.text) : pick(Col_SliderGrab, g_theme.accent);
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
    const bool inside = hit(x, y, size + (label && *label ? 8.0f + visW(label) : 0.0f), size);
    g_lastId = id; if (inside) g_hot = id;

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
    if (*value == option) fillCircle(cx, cy, size * 0.22f, pick(Col_CheckMark, g_theme.accent));
    if (label && *label) {
        const float s  = g_theme.textScale;
        const float th = fH() * s;
        drawText(x + size + 8.0f, y + (size - th) * 0.5f, VisLabel(label).b, s, g_theme.text);
    }
    return changed;
}

bool Tab(int id, float x, float y, float w, float h, const char* label, int* current, int index) {
    if (!current) return false;
    const bool inside = hit(x, y, w, h);
    g_lastId = id; if (inside) g_hot = id;

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
            const float s = g_theme.textScale, tw = visW(label), th = fH() * s;
            drawText(x + (w - tw) * 0.5f, y + (h - th) * 0.5f, VisLabel(label).b, s, g_theme.text);
        }
        return *current == index;
    }
    const unsigned char* bg = selected ? pick(Col_TabActive, g_theme.bg) : pick(Col_Tab, g_theme.track);
    if (!selected && inside) bg = pick(Col_TabHovered, g_theme.bgHover);

    quad(x, y, w, h, g_theme.border);
    const float b = g_theme.borderPx;
    if (w > 2.0f * b && h > 2.0f * b) quad(x + b, y + b, w - 2.0f * b, h - 2.0f * b, bg);
    if (selected) quad(x, y, w, 3.0f, g_theme.accent);   // accent strip on the active tab
    if (label && *label) {
        const float s  = g_theme.textScale;
        const float tw = visW(label);
        const float th = fH() * s;
        drawText(x + (w - tw) * 0.5f, y + (h - th) * 0.5f, VisLabel(label).b, s, g_theme.text);
    }
    return *current == index;
}

// ---- Text field -----------------------------------------------------------------
namespace {
// Caret, selection anchor and horizontal scroll of a text field, kept per widget id.
struct TextSlot { int id = 0; int caret = 0, anchor = 0, first = 0; bool used = false; unsigned long long lastUse = 0; };
TextSlot g_texts[64];
// The caret state of up to 64 text fields. When the table is full the field drawn longest ago gives its slot up (never the one with
// the keyboard), so a form with many fields (the 32 layer names) keeps a working caret in the field being typed into.
TextSlot* textSlot(int id) {
    for (TextSlot& t : g_texts) if (t.used && t.id == id) { t.lastUse = g_frameCount; return &t; }
    TextSlot* pick = nullptr;
    for (TextSlot& t : g_texts) if (!t.used) { pick = &t; break; }
    if (!pick) for (TextSlot& t : g_texts) if (t.id != g_focus && (!pick || t.lastUse < pick->lastUse)) pick = &t;
    if (!pick) { static TextSlot sink; sink = TextSlot(); return &sink; }
    *pick = TextSlot(); pick->used = true; pick->id = id; pick->lastUse = g_frameCount;
    return pick;
}
// Length of the valid, printable UTF-8 character at p, or 0 for a control character
// or an invalid / overlong / surrogate / out-of-range sequence.
int printableUtf8Len(const char* p) {
    const unsigned char c = (unsigned char)p[0];
    if (c < 0x20 || c == 0x7f) return 0;
    if (c < 0x80) return 1;
    const int n = (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
    if (!n) return 0;
    unsigned cp = n == 2 ? (c & 0x1Fu) : n == 3 ? (c & 0x0Fu) : (c & 0x07u);
    for (int i = 1; i < n; ++i) {
        const unsigned char cc = (unsigned char)p[i];
        if ((cc & 0xC0) != 0x80) return 0;   // also stops at the terminating NUL
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    if ((n == 3 && cp < 0x800u) || (n == 4 && (cp < 0x10000u || cp > 0x10FFFFu))) return 0;
    if (cp >= 0xD800u && cp <= 0xDFFFu) return 0;
    return n;
}
// If the string ends in an incomplete UTF-8 sequence (a lead byte whose continuation bytes were cut off),
// cut that sequence off.
void trimPartialChar(char* b) {
    const int len = cstrlen(b);
    int j = len - 1;
    while (j >= 0 && len - j < 4 && ((unsigned char)b[j] & 0xC0) == 0x80) --j;   // back over continuation bytes
    if (j < 0) return;
    const unsigned char c = (unsigned char)b[j];
    const int need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    if (need > len - j) b[j] = '\0';
}
// Move `i` back to the start of the character it points into (a stale index after the text changed).
inline int snapToChar(const char* b, int len, int i) {
    if (i > len) i = len;
    while (i > 0 && i < len && ((unsigned char)b[i] & 0xC0) == 0x80) --i;
    return i;
}
inline int prevChar(const char* b, int i) { if (i > 0) --i; while (i > 0 && ((unsigned char)b[i] & 0xC0) == 0x80) --i; return i; }
inline int nextChar(const char* b, int len, int i) { if (i < len) ++i; while (i < len && ((unsigned char)b[i] & 0xC0) == 0x80) ++i; return i; }
// Width in pixels of buf[from, to).
float textWidth(char* b, int from, int to) {
    const char save = b[to]; b[to] = '\0';
    const float w = fMeasure(b + from) * g_theme.textScale;
    b[to] = save;
    return w;
}
// The character boundary nearest to pixel `mx`, with the text drawn from byte `first` at `tx`.
int indexAtX(char* b, int len, int first, float tx, float mx) {
    float prevW = 0.0f;
    int i = first;
    while (i < len) {
        const int nx = nextChar(b, len, i);
        const float nw = textWidth(b, first, nx);
        if (mx < tx + (prevW + nw) * 0.5f) return i;
        prevW = nw; i = nx;
    }
    return len;
}
// Remove buf[from, to) and shift the tail down.
void eraseBytes(char* b, int& len, int from, int to) {
    if (to <= from) return;
    for (int i = to; i <= len; ++i) b[from + (i - to)] = b[i];
    len -= (to - from);
}
} // namespace

Composition GetComposition() {
    Composition c;
    const char* src = g_in.composition;
    if (!src || !*src) return c;
    const int want = g_in.compositionCaret;
    int srcCaret = -1;
    int n = 0;
    for (int i = 0; src[i] && n < 256;) {
        const int k = printableUtf8Len(src + i);
        if (srcCaret < 0 && want >= 0 && want < i + (k ? k : 1)) srcCaret = n;   // a caret inside a character goes to its start
        if (!k) { ++i; continue; }
        if (n + k > 256) break;
        for (int j = 0; j < k; ++j) c.text[n + j] = src[i + j];
        n += k; i += k;
    }
    c.text[n] = '\0';
    c.length = n;
    if (n == 0) return c;
    c.active = true;
    if (want < 0) srcCaret = n;
    else if (srcCaret < 0) srcCaret = n;   // the caret was past the end (or after the last kept character)
    c.caret = snapToChar(c.text, n, srcCaret);
    return c;
}
ImeRect GetImeRect() { return g_ime; }
void SetImeRect(const ImeRect& r) { g_ime = r; g_ime.valid = true; }

bool TextField(int id, float x, float y, float w, float h, char* buf, int cap, unsigned flags) {
    if (!buf || cap < 1) return false;
    if (buf[cap - 1] != '\0') {   // never trust the caller's buffer to be terminated
        buf[cap - 1] = '\0';
        trimPartialChar(buf);      // and cutting it must not leave half a character
    }
    // A field wholly outside the clip (a row of a long list scrolled out of view) that neither has the keyboard nor is about to
    // get it: the mouse cannot reach it and nothing of it would be drawn, so its caret, text and box are not worked out at all.
    if (g_focus != id && g_active != id && !g_focusNextField && clippedAway(x, y, x + w, y + h)) { g_lastId = id; return false; }
    int len = cstrlen(buf);
    const bool readOnly = (flags & InputText_ReadOnly) != 0;
    TextSlot* ts = textSlot(id);
    // The caret, selection anchor and first shown character are byte offsets kept between frames. Keep them
    // on character boundaries: the text may have changed under them (the caller edits the buffer, or an edit
    // below `first` replaced characters there), and an offset inside a character would let Delete or a click
    // split it.
    ts->caret = snapToChar(buf, len, ts->caret);
    ts->anchor = snapToChar(buf, len, ts->anchor);
    ts->first = snapToChar(buf, len, ts->first);

    const float s = g_theme.textScale, th = fH() * s, b = g_theme.borderPx;
    const float pad = g_look ? 9.0f : 6.0f;
    const float padL = g_fieldPadL >= 0.0f ? g_fieldPadL : pad, padR = g_fieldPadR >= 0.0f ? g_fieldPadR : pad;   // SearchField makes room for its icons
    const float tx = x + padL;
    const bool inside = hit(x, y, w, h);
    g_lastId = id; if (inside) g_hot = id;
    const bool wasFocused = (g_focus == id);
    if (inside && g_pressed) {
        if (g_focus != 0 && g_focus != id) g_focusLostEnd = g_focus;   // another field takes the keyboard
        g_focus = id; g_focusClaimed = true; g_active = id;
        ts->caret = ts->anchor = indexAtX(buf, len, ts->first, tx, g_in.mouseX);
        if ((flags & InputText_AutoSelectAll) && !wasFocused) { ts->anchor = 0; ts->caret = len; }
    } else if (g_active == id && g_in.mouseDown && g_focus == id) {
        ts->caret = indexAtX(buf, len, ts->first, tx, g_in.mouseX);   // drag selects
    }
    // SetKeyboardFocusHere / a typed number: applied after the click handling above, so the same-frame click on the box
    // cannot undo the caret or the select-all.
    if (g_focusNextField) { g_focusNextField = false; g_focus = id; g_focusClaimed = true; ts->caret = len; ts->anchor = (g_focusSelectAll || (flags & InputText_AutoSelectAll)) ? 0 : len; g_focusSelectAll = false; }   // AutoSelectAll also when the focus is given with SetKeyboardFocusHere
    if (g_disCount > 0 && g_focus == id) g_focus = 0;   // a disabled field gives up the keyboard
    bool focused = (g_focus == id);
    if (focused) { g_fieldFocus = id; g_fieldFocusSeen = true; }
    Composition comp = (focused && !(flags & InputText_ReadOnly)) ? GetComposition() : Composition();
    const bool composing = comp.active;   // the input method owns Backspace, the arrows, Enter and Escape while it composes

    // Keystrokes, applied only while focused (the host forwards the ones it wants OkayUI to have).
    bool changed = false, enter = false;
    if (focused) {
        auto selLo = [&] { return ts->caret < ts->anchor ? ts->caret : ts->anchor; };
        auto selHi = [&] { return ts->caret < ts->anchor ? ts->anchor : ts->caret; };
        auto deleteSelection = [&] {
            if (ts->caret == ts->anchor) return false;
            const int lo = selLo(), hi = selHi();
            eraseBytes(buf, len, lo, hi);
            ts->caret = ts->anchor = lo; changed = true; return true;
        };
        auto insertOne = [&](const char* p, int n) {
            const int lo = selLo(), hi = selHi();
            if (len - (hi - lo) + n > cap - 1) return;
            deleteSelection();
            for (int i = len; i >= ts->caret; --i) buf[i + n] = buf[i];   // moves the NUL too
            for (int i = 0; i < n; ++i) buf[ts->caret + i] = p[i];
            len += n; ts->caret += n; ts->anchor = ts->caret; changed = true;
        };
        // Ctrl+A first: the host gathers one frame's keys without their order, and a select-all typed just before text (Ctrl+A, then
        // "2.5" from a fast typist or xdotool) must select the old text for the new text to replace. Applied after the text, it selected
        // the "2" just typed, and the ".5" of the next frame replaced it: 0.5 instead of 2.5.
        if (g_in.selectAll && !composing) { ts->anchor = 0; ts->caret = len; }
        if (g_in.commit && g_in.commit[0] && !readOnly) {   // what the input method finished: typed text, replacing a selection
            for (const char* p = g_in.commit; *p;) {
                const int n = printableUtf8Len(p);
                if (!n) { ++p; continue; }
                insertOne(p, n);
                p += n;
            }
        }
        if (g_in.text && g_in.text[0] && !readOnly && !ioTypedTextConsumed()) {
            for (const char* p = g_in.text; *p;) {
                const int n = printableUtf8Len(p);
                if (!n) { ++p; continue; }
                if ((flags & InputText_CharsNoBlank) && n == 1 && (*p == ' ' || *p == '\t')) { p += n; continue; }   // CharsNoBlank
                if ((flags & InputText_CharsDecimal) && !(n == 1 && ((*p >= '0' && *p <= '9') || *p == '.' || *p == '-' || *p == '+' || *p == '*' || *p == '/'))) { p += n; continue; }   // CharsDecimal
                insertOne(p, n);
                p += n;
            }
        }
        // Clipboard: copy and cut take the selection, paste replaces it (new lines and tabs become spaces: it is a one-line field).
        if (!composing && !(flags & InputText_Password) && (g_in.copy || g_in.cut) && ts->caret != ts->anchor) {   // a password is never copied
            const int lo = selLo(), hi = selHi();
            char tmp[kClipboardMax]; int n = hi - lo; if (n > kClipboardMax - 1) n = kClipboardMax - 1;
            for (int i = 0; i < n; ++i) tmp[i] = buf[lo + i];
            tmp[n] = 0; trimPartialChar(tmp);
            SetClipboardText(tmp);
            if (g_in.cut && !readOnly) deleteSelection();
        }
        if (g_in.paste && !readOnly && !composing) {
            const char* clip = GetClipboardText();
            for (const char* p = clip; p && *p;) {
                const int n = printableUtf8Len(p);
                if (!n) { if (*p == '\n' || *p == '\r' || *p == '\t') insertOne(" ", 1); ++p; continue; }
                insertOne(p, n);
                p += n;
            }
        }
        if (g_in.backspace && !composing && !readOnly && !deleteSelection() && ts->caret > 0) {
            const int from = prevChar(buf, ts->caret);
            eraseBytes(buf, len, from, ts->caret); ts->caret = ts->anchor = from; changed = true;
        }
        if (g_in.del && !composing && !readOnly && !deleteSelection() && ts->caret < len) {
            eraseBytes(buf, len, ts->caret, nextChar(buf, len, ts->caret)); ts->anchor = ts->caret; changed = true;
        }
        // Caret movement: with shift the anchor stays and the selection grows; otherwise a
        // selection collapses to the edge in the direction of travel.
        auto move = [&](int to, int collapseTo) {
            if (g_in.shift) ts->caret = to;
            else { ts->caret = ts->anchor != ts->caret ? collapseTo : to; ts->anchor = ts->caret; }
        };
        if (g_in.left && !composing)  move(prevChar(buf, ts->caret), selLo());
        if (g_in.right && !composing) move(nextChar(buf, len, ts->caret), selHi());
        if (g_in.home && !composing)  { ts->caret = 0;   if (!g_in.shift) ts->anchor = 0; }
        if (g_in.end && !composing)   { ts->caret = len; if (!g_in.shift) ts->anchor = len; }
        if (g_in.enter && !composing) enter = true;
        if (g_in.escape && !composing) { g_focus = 0; focused = false; }   // seen this frame by the field's own IsItemDeactivatedAfterEdit (no g_focusLostEnd: that would count it twice)
    }

    // Rounded box (focused gets an accent border hint). With Dear ImGui's framed look (StyleVar_FramedWidgets) a frame has no border, as
    // with the editor's FrameBorderSize of 0: only the frame colour, lighter when hovered and while it has the keyboard.
    const float rd = rnd();
    if (g_framedWidgets) rrect(x, y, w, h, rd, pick(focused ? Col_FrameBgActive : (inside ? Col_FrameBgHovered : Col_FrameBg), g_theme.field));
    else {
    rrect(x, y, w, h, rd, focused ? g_theme.accent : g_theme.border);
    if (w > 2.0f * b && h > 2.0f * b) rrect(x + b, y + b, w - 2.0f * b, h - 2.0f * b, rd > b ? rd - b : 0.0f,
                                            pick(focused ? Col_FrameBgActive : (inside ? Col_FrameBgHovered : Col_FrameBg), g_theme.field));
    }

    // Password: show one '*' per byte (offsets stay equal to the real text's, so caret and selection need no mapping).
    static char maskBuf[4096];
    if (flags & InputText_Password) {
        const int m = len < 4095 ? len : 4095;
        for (int i = 0; i < m; ++i) maskBuf[i] = '*';
        maskBuf[m] = 0; buf = maskBuf; len = m;
        if (ts->caret > m) ts->caret = m;
        if (ts->anchor > m) ts->anchor = m;
        if (ts->first > m) ts->first = m;
    }
    // Scroll horizontally so the caret stays visible: drop leading characters until the text
    // between the first shown character and the caret fits (whole UTF-8 characters only).
    const float innerW = w - padL - padR;
    ts->caret = snapToChar(buf, len, ts->caret); ts->anchor = snapToChar(buf, len, ts->anchor);
    ts->first = snapToChar(buf, len, ts->first);   // an edit below `first` can leave it inside a new character
    if (ts->first > ts->caret) ts->first = ts->caret;
    const int selLoD = ts->caret < ts->anchor ? ts->caret : ts->anchor, selHiD = ts->caret < ts->anchor ? ts->anchor : ts->caret;
    const float preW = composing ? fMeasure(comp.text) * s : 0.0f;
    float preCaretW = 0.0f;
    if (composing) { const char sv = comp.text[comp.caret]; comp.text[comp.caret] = '\0'; preCaretW = fMeasure(comp.text) * s; comp.text[comp.caret] = sv; }
    if (composing) {   // keep the preedit's caret visible; the text before it starts at `first`
        if (ts->first > selLoD) ts->first = selLoD;
        while (ts->first < selLoD && textWidth(buf, ts->first, selLoD) + preCaretW > innerW - 2.0f) ts->first = nextChar(buf, len, ts->first);
    } else
    while (ts->first < ts->caret && textWidth(buf, ts->first, ts->caret) > innerW - 2.0f) ts->first = nextChar(buf, len, ts->first);
    const float ty = y + (h - th) * 0.5f;
    PushClipRect(x + b, y + b, w - 2.0f * b, h - 2.0f * b);
    if (focused && !composing && ts->caret != ts->anchor) {   // selection highlight behind the text
        const int lo = ts->caret < ts->anchor ? ts->caret : ts->anchor, hi = ts->caret < ts->anchor ? ts->anchor : ts->caret;
        const int a = lo < ts->first ? ts->first : lo;
        if (hi > a) {
            unsigned char sel[4] = { g_theme.accent[0], g_theme.accent[1], g_theme.accent[2], 110 };
            const float x0 = tx + textWidth(buf, ts->first, a), x1 = tx + textWidth(buf, ts->first, hi);
            quad(x0, ty, x1 - x0, th, pick(Col_TextSelectedBg, sel));
        }
    }
    float caretX = tx + textWidth(buf, ts->first, ts->caret);
    if (composing) {   // text before the selection, the underlined preedit in place of the selection, then the text after it
        const float px = tx + textWidth(buf, ts->first, selLoD);
        { const char sv = buf[selLoD]; buf[selLoD] = '\0'; drawText(tx, ty, buf + ts->first, s, g_theme.text); buf[selLoD] = sv; }
        unsigned char wash[4] = { g_theme.accent[0], g_theme.accent[1], g_theme.accent[2], 50 };
        quad(px, ty, preW, th, wash);
        drawText(px, ty, comp.text, s, g_theme.text);
        quad(px, ty + th - 2.0f, preW, 2.0f, g_theme.text);   // the preedit underline
        drawText(px + preW, ty, buf + selHiD, s, g_theme.text);
        caretX = px + preCaretW;
    } else {
        drawText(tx, ty, buf + ts->first, s, g_theme.text);
        if (len == 0 && g_hintText && g_hintText[0]) drawText(tx, ty, g_hintText, s, pick(Col_TextDisabled, g_theme.textDim));   // the placeholder, while the field is empty
    }
    // Blinking caret (steady when Theme::caretBlink is off, which tests use).
    if (focused && (!g_theme.caretBlink || (g_frame / 30u) % 2u == 0u)) quad(caretX, ty, 2.0f, th, g_theme.text);
    if (focused) {   // where the host should put the candidate window: the caret, kept inside the box
        ImeRect ir;
        const float cxr = caretX < x + b ? x + b : (caretX > x + w - b - 2.0f ? x + w - b - 2.0f : caretX);
        ir.caret = Rect{cxr, ty, 2.0f, th};
        if (composing) { const float px = tx + textWidth(buf, ts->first, selLoD); ir.composition = Rect{px, ty, preW, th}; }
        else ir.composition = Rect{cxr, ty, 0.0f, th};
        SetImeRect(ir);
    }
    PopClipRect();
    // IsItemDeactivatedAfterEdit (OkayUICompat.hpp): with InputText_EnterReturnsTrue, an Enter that returned true committed the text, so
    // only edits after it count when the field later loses the keyboard.
    if (changed) g_edFrameId = g_edStickyId = id;   // an edit (also when the return value is the Enter)
    if (enter && focused && (flags & InputText_EnterReturnsTrue) && g_edStickyId == id) g_edStickyId = 0;
    return (flags & InputText_EnterReturnsTrue) ? (enter && focused) : changed;
}

namespace {
// Append the overlay batch after the main batch so popups/tooltips draw on top.
void mergeOverlay() {
    if (g_oni > 0) { setTexture(g_defTex); pushClipSpan(false, SDL_Rect{ 0, 0, 0, 0 }); }  // overlay: no clip/tex
    const int base = g_nv;
    for (int i = 0; i < g_onv && g_nv < kMaxVerts; ++i) g_verts[g_nv++] = g_ovVerts[i];
    const int fit = g_nv - base;   // overlay vertices that made it in
    int sp = 0;
    // Whole triangles only: when the batch is full, a triangle that references a vertex that
    // did not fit (or does not fit in the index buffer) is dropped, so the index buffer always
    // stays valid and a multiple of three.
    if (g_ni + g_oni > kMaxIdx || g_nv + g_onv > kMaxVerts) noteDrop();
    for (int j = 0; j + 3 <= g_oni && g_ni + 3 <= kMaxIdx; j += 3) {
        while (sp < g_novSpans && g_ovSpans[sp].startIdx <= j) setTexture(g_ovSpans[sp++].tex);
        if (g_ovIdx[j] >= fit || g_ovIdx[j + 1] >= fit || g_ovIdx[j + 2] >= fit) { noteDrop(); continue; }
        g_idx[g_ni++] = base + g_ovIdx[j]; g_idx[g_ni++] = base + g_ovIdx[j + 1]; g_idx[g_ni++] = base + g_ovIdx[j + 2];
    }
    g_onv = 0; g_oni = 0; g_novSpans = 0;
}
// End-of-frame interaction bookkeeping, shared by every backend.
void finalizeFrame() {
    ChannelsMergeAll();
    // A text field that had the keyboard and was not drawn this frame (its popup closed, its panel was not shown) gives it up, as Dear
    // ImGui drops an active item that was not submitted: the keys must not go on to a field nobody sees, and WantsTextInput() turns false.
    if (g_focus != 0 && g_focus == g_fieldFocus && !g_fieldFocusSeen) { g_focusLostEnd = g_focus; g_focus = 0; }
    g_fieldFocusSeen = false;
    if (g_released) g_active = 0;                    // clear if the active item wasn't drawn
    if (g_pressed && !g_focusClaimed) { if (g_focus != 0) g_focusLostEnd = g_focus; g_focus = 0; }   // click on empty space drops keyboard focus
    // A floating chip follows the cursor while dragging (drawn in the overlay so it's
    // on top). Any release ends the drag (a DropTarget already consumed it if hit).
    if (g_dd.active) {
        g_toOverlay = true;
        const unsigned char* ac = g_theme.accent;
        fillCircle(g_in.mouseX + 10.0f, g_in.mouseY + 10.0f, 7.0f, ac);
        g_toOverlay = false;
    }
    if (g_tdd.active && !g_tddExt) {   // the dragged thing follows the cursor: a dot, plus the preview text when there is one
        g_toOverlay = true;
        fillCircle(g_in.mouseX + 10.0f, g_in.mouseY + 10.0f, 7.0f, g_theme.accent);
        if (g_tdd.preview[0]) {
            const float tw = labelW(g_tdd.preview), th = textH();
            quad(g_in.mouseX + 22.0f, g_in.mouseY + 4.0f, tw + 12.0f, th + 8.0f, g_theme.border);
            quad(g_in.mouseX + 23.0f, g_in.mouseY + 5.0f, tw + 10.0f, th + 6.0f, g_theme.panel);
            drawText(g_in.mouseX + 28.0f, g_in.mouseY + 8.0f, g_tdd.preview, g_theme.textScale, g_theme.text);
        }
        g_toOverlay = false;
    }
    if (g_tddExt) {   // an outside drag ends when the host stops reporting it, or on the frame it was let go (after the targets had it)
        if (!g_tddExtSeen || g_tddExtReleased) { g_tddExt = false; g_tdd = TypedDrag(); }
        g_tddExtSeen = false; g_tddExtReleased = false;
    } else if (!g_in.mouseDown) { g_dd.active = false; g_dd.armed = false; g_tdd.active = false; g_tdd.armed = false; g_tdd.hasPayload = false; g_tdd.preview[0] = 0; }
    flushTooltip();                                  // the frame's tooltip, over everything
    reorderWindows();                                // windows in stacking order, then the overlay on top
    mergeOverlay();
    // Fail loudly: a full batch means shapes are missing from the picture. Say so on stderr (the first few
    // frames only, so a permanently too-big UI does not flood the log) and in DrawData::dropped.
    static int reports = 0;
    if (g_dropped > 0 && reports < 5) {
        ++reports;
        std::fprintf(stderr, "OkayUI: the draw batch is full (%d vertices, %d indices): %d shape(s) were dropped this frame\n",
                     kMaxVerts, kMaxIdx, g_dropped);
    }
}
} // namespace

static_assert(sizeof(DrawVertex) == sizeof(SDL_Vertex), "DrawVertex must match SDL_Vertex");
static_assert(offsetof(DrawVertex, x) == offsetof(SDL_Vertex, position), "DrawVertex layout");
static_assert(offsetof(DrawVertex, r) == offsetof(SDL_Vertex, color), "DrawVertex layout");
static_assert(offsetof(DrawVertex, u) == offsetof(SDL_Vertex, tex_coord), "DrawVertex layout");

namespace {
// The span list as backend-neutral commands: the same index ranges, clip rectangles
// and textures EndFrame(SDL_Renderer*) walks, including the unclipped range before the
// first span (window frames drawn before any clip was pushed).
DrawCmd g_cmds[kMaxSpans + 2];
int buildCmds() {
    int n = 0;
    auto add = [&](int from, int to, bool on, const SDL_Rect& r, SDL_Texture* tex) {
        if (to <= from || n >= kMaxSpans + 2) return;
        DrawCmd& c = g_cmds[n++];
        c.idxOffset = from; c.idxCount = to - from;
        c.clipOn = on; c.clipX = r.x; c.clipY = r.y; c.clipW = r.w; c.clipH = r.h;
        c.texture = (const void*)tex;
    };
    const SDL_Rect none = { 0, 0, 0, 0 };
    if (g_nspans == 0) { add(0, g_ni, false, none, g_defTex); return n; }
    add(0, g_spans[0].startIdx, false, none, g_defTex);
    for (int s = 0; s < g_nspans; ++s)
        add(g_spans[s].startIdx, (s + 1 < g_nspans) ? g_spans[s + 1].startIdx : g_ni,
            g_spans[s].on, g_spans[s].rect, g_spans[s].tex);
    return n;
}
} // namespace

int DroppedGeometry() { return g_dropped; }

DrawData GetDrawData() {
    DrawData d;
    d.vertices = g_verts; d.vertexCount = g_nv;
    d.indices = g_idx;    d.indexCount = g_ni;
    d.vertexStride = (int)sizeof(DrawVertex);
    d.cmds = g_cmds; d.cmdCount = buildCmds();
    d.dropped = g_dropped;
    if (g_ttf && g_ttf->ok) {
        d.atlasTexture = (const void*)kAtlasTex; d.atlasRgba = g_ttf->rgba; d.atlasW = g_ttf->w; d.atlasH = g_ttf->h;
    }
    return d;
}

// The Dear ImGui view of the frame's draw data (see ImDrawData in OkayUI.hpp): one list holding every vertex, index and command.
DrawData::operator ImDrawData*() const {
    static ImDrawData dd;
    static DrawList list{false};
    static ImTextureData atlasTex;
    static ImVector<ImTextureData*> texList;
    list.VtxBuffer.clear(); list.IdxBuffer.clear(); list.CmdBuffer.clear(); texList.clear();
    const DrawVertex* v = static_cast<const DrawVertex*>(vertices);
    if (v && vertexCount > 0) {
        list.VtxBuffer.reserve(vertexCount);
        for (int i = 0; i < vertexCount; ++i) {
            ImDrawVert o; o.pos = Vec2{v[i].x, v[i].y}; o.uv = Vec2{v[i].u, v[i].v};
            o.col = (unsigned)v[i].r | ((unsigned)v[i].g << 8) | ((unsigned)v[i].b << 16) | ((unsigned)v[i].a << 24);
            list.VtxBuffer.push_back(o);
        }
    }
    if (indices && indexCount > 0) {
        list.IdxBuffer.reserve(indexCount);
        for (int i = 0; i < indexCount; ++i) list.IdxBuffer.push_back((ImDrawIdx)(indices[i] < 0 ? 0 : indices[i]));
    }
    const bool haveAtlas = atlasTexture && atlasRgba && atlasW > 0 && atlasH > 0;
    if (haveAtlas) { atlasTex.Status = ImTextureStatus_OK; atlasTex.Width = atlasW; atlasTex.Height = atlasH; atlasTex.BytesPerPixel = 4; atlasTex.Pixels = const_cast<unsigned char*>(atlasRgba); texList.push_back(&atlasTex); }
    for (int i = 0; i < cmdCount; ++i) {
        const DrawCmd& c = cmds[i];
        if (c.idxCount <= 0) continue;
        ImDrawCmd o;
        o.ClipRect = c.clipOn ? ImVec4((float)c.clipX, (float)c.clipY, (float)(c.clipX + c.clipW), (float)(c.clipY + c.clipH)) : ImVec4(-8192.0f, -8192.0f, 8192.0f, 8192.0f);
        o.TexRef._TexID = (ImTextureID)const_cast<void*>(c.texture);
        if (haveAtlas && c.texture == atlasTexture) o.TexRef._TexData = &atlasTex;
        o.IdxOffset = (unsigned)c.idxOffset; o.ElemCount = (unsigned)c.idxCount; o.VtxOffset = 0;
        list.CmdBuffer.push_back(o);
    }
    dd.Valid = true;
    dd.CmdLists.clear(); dd.CmdLists.push_back(&list);
    dd.CmdListsCount = 1; dd.TotalVtxCount = list.VtxBuffer.Size; dd.TotalIdxCount = list.IdxBuffer.Size;
    const IO& io = GetIO();
    dd.DisplayPos = Vec2{0, 0}; dd.DisplaySize = io.displaySize; dd.FramebufferScale = io.DisplayFramebufferScale;
    dd.Textures = &texList;
    return &dd;
}

okay::TtfFont* FallbackFontOf(const void* texture) {
    for (int i = 0; i < g_nfb; ++i) if (texture == (const void*)g_fbFonts[i]) return g_fbFonts[i];
    return nullptr;
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
    const Uint32 now = okay::platform::TicksMs32();
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

void EndFrameData(int outputWidth, int outputHeight) {
    if (g_ntoasts && outputWidth > 0 && outputHeight > 0) drawToasts((float)outputWidth, (float)outputHeight);
    finalizeFrame();
}

void EndFrame(SDL_Renderer* r) {
    if (r && g_ntoasts) { int ow = 0, oh = 0; SDL_GetRendererOutputSize(r, &ow, &oh); drawToasts((float)ow, (float)oh); }
    finalizeFrame();
    DrawFinishedFrame(r);
}

void DrawFinishedFrame(SDL_Renderer* r) {
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
    const int id = (int)which;
    ColorSave& s = g_colStack[g_colTop];
    if (id >= 8 && id < Col_COUNT) {
        unsigned char* f = g_theme.colors[id];
        s.field = f; s.ext = id; s.prevSet = colorIsSet(id);
        s.prev[0] = f[0]; s.prev[1] = f[1]; s.prev[2] = f[2]; s.prev[3] = f[3];
        f[0] = r; f[1] = g; f[2] = b; f[3] = a;
        g_theme.colorSet |= 1ull << id;
    } else {
        unsigned char* f = themeColor(id >= 0 && id < 8 ? id : 4);
        s.field = f; s.ext = -1; s.prevSet = false;
        s.prev[0] = f[0]; s.prev[1] = f[1]; s.prev[2] = f[2]; s.prev[3] = f[3];
        f[0] = r; f[1] = g; f[2] = b; f[3] = a;
    }
    ++g_colTop;
}
void PushStyleColor(Col which, Color32 c) { PushStyleColor(which, (unsigned char)(c & 255u), (unsigned char)((c >> 8) & 255u), (unsigned char)((c >> 16) & 255u), (unsigned char)((c >> 24) & 255u)); }
void PushStyleColor(Col which, Vec4 v) {
    auto b = [](float f) { return (unsigned char)(clamp01(f) * 255.0f + 0.5f); };
    PushStyleColor(which, b(v.x), b(v.y), b(v.z), b(v.w));
}

void PopStyleColor(int count) {
    while (count-- > 0 && g_colTop > 0) {
        ColorSave& s = g_colStack[--g_colTop];
        s.field[0] = s.prev[0]; s.field[1] = s.prev[1]; s.field[2] = s.prev[2]; s.field[3] = s.prev[3];
        if (s.ext >= 0) { if (s.prevSet) g_theme.colorSet |= 1ull << s.ext; else g_theme.colorSet &= ~(1ull << s.ext); }
    }
}
void SetStyleColor(Col which, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    const int id = (int)which;
    unsigned char* f = (id >= 8 && id < Col_COUNT) ? g_theme.colors[id] : (id >= 0 && id < 8 ? themeColor(id) : nullptr);
    if (!f) return;
    f[0] = r; f[1] = g; f[2] = b; f[3] = a;
    if (id >= 8) g_theme.colorSet |= 1ull << id;
}
void ResetStyleColor(Col which) { const int id = (int)which; if (id >= 8 && id < Col_COUNT) g_theme.colorSet &= ~(1ull << id); }
ImVec4 GetStyleColorVec4(Col which) {
    unsigned char tmp[4];
    const unsigned char* c = styleColor((int)which, tmp);
    return Vec4{c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f};
}
const char* GetStyleColorName(Col which) {
    static const char* const names[] = {
        "Col_Bg", "Col_BgHover", "Col_BgDown", "Col_Border", "Col_Text", "Col_Panel", "Col_Track", "Col_Accent",
        "Col_TextDisabled", "Col_WindowBg", "Col_ChildBg", "Col_PopupBg", "Col_BorderShadow", "Col_FrameBg", "Col_FrameBgHovered", "Col_FrameBgActive",
        "Col_TitleBg", "Col_TitleBgActive", "Col_TitleBgCollapsed", "Col_MenuBarBg", "Col_ScrollbarBg", "Col_ScrollbarGrab", "Col_ScrollbarGrabHovered",
        "Col_ScrollbarGrabActive", "Col_CheckMark", "Col_SliderGrab", "Col_SliderGrabActive", "Col_Header", "Col_HeaderHovered", "Col_HeaderActive",
        "Col_Separator", "Col_SeparatorHovered", "Col_SeparatorActive", "Col_ResizeGrip", "Col_ResizeGripHovered", "Col_ResizeGripActive",
        "Col_Tab", "Col_TabHovered", "Col_TabActive", "Col_TabUnfocused", "Col_TabUnfocusedActive", "Col_DockingPreview", "Col_DockingEmptyBg",
        "Col_PlotLines", "Col_PlotLinesHovered", "Col_PlotHistogram", "Col_PlotHistogramHovered", "Col_TableHeaderBg", "Col_TableBorderStrong",
        "Col_TableBorderLight", "Col_TableRowBg", "Col_TableRowBgAlt", "Col_TextSelectedBg", "Col_DragDropTarget", "Col_NavHighlight",
        "Col_NavWindowingHighlight", "Col_NavWindowingDimBg", "Col_ModalWindowDimBg"};
    static_assert(sizeof names / sizeof names[0] == Col_COUNT, "one name per colour");
    return (int)which >= 0 && (int)which < Col_COUNT ? names[(int)which] : "";
}
Vec4 ColorConvertU32ToFloat4(Color32 c) { return Vec4{(c & 255u) / 255.0f, ((c >> 8) & 255u) / 255.0f, ((c >> 16) & 255u) / 255.0f, ((c >> 24) & 255u) / 255.0f}; }
Color32 ColorConvertFloat4ToU32(Vec4 v) {
    auto b = [](float f) { return (unsigned)(clamp01(f) * 255.0f + 0.5f); };
    return RGBA(b(v.x), b(v.y), b(v.z), b(v.w));
}
void ColorConvertRGBtoHSV(float r, float g, float b, float* h, float* s, float* v) {
    r = clamp01(r); g = clamp01(g); b = clamp01(b);
    const float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b)), d = mx - mn;
    float hh = 0.0f;
    if (d > 1e-9f) {
        if (mx == r) hh = (g - b) / d + (g < b ? 6.0f : 0.0f);
        else if (mx == g) hh = (b - r) / d + 2.0f;
        else hh = (r - g) / d + 4.0f;
        hh /= 6.0f;
    }
    if (h) *h = hh;
    if (s) *s = mx > 1e-9f ? d / mx : 0.0f;
    if (v) *v = mx;
}
void ColorConvertHSVtoRGB(float h, float s, float v, float* r, float* g, float* b) {
    if (!std::isfinite(h)) h = 0.0f;
    h = h - std::floor(h); s = clamp01(s); v = clamp01(v);
    const float hh = h * 6.0f;
    const int i = (int)hh % 6;
    const float f = hh - std::floor(hh), p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
    float R = v, G = t, B = p;
    switch (i) { case 0: R = v; G = t; B = p; break; case 1: R = q; G = v; B = p; break; case 2: R = p; G = v; B = t; break;
                 case 3: R = p; G = q; B = v; break; case 4: R = t; G = p; B = v; break; default: R = v; G = p; B = q; break; }
    if (r) *r = R;
    if (g) *g = G;
    if (b) *b = B;
}

namespace { float g_userSavedScale = 0.0f; }   // textScale before a user font set its own (0: none active)
void SetFont(const Font* f) {
    if (g_font->ttf && g_userSavedScale > 0.0f) { g_theme.textScale = g_userSavedScale; g_userSavedScale = 0.0f; }   // leaving a user font gives the scale back
    g_font = f ? f : &kFontDefault;
    if (g_font->ttf && g_font->px > 0.0f) { g_userSavedScale = g_theme.textScale; g_theme.textScale = g_font->px / 8.0f; }   // the line is px tall: 8 cell units times px / 8
}
Font* GetFont()             { return const_cast<Font*>(g_font); }
const Font* FontDefault()   { return &kFontDefault; }
const Font* FontBold()      { return &kFontBold; }

// ---- Auto-layout window + ImGui-style overloads --------------------------------

static bool g_pinNext = false;   // PinNextWindow: the next Begin sits where it is told
// The window being built (for GetWindowPos / IsWindowHovered ...) and the one that was clicked last (IsWindowFocused).
struct CurWin { bool valid = false; int id = 0; float x = 0, y = 0, w = 0, h = 0; bool appearing = false; unsigned long long z = 0; char title[96] = {0}; };
static CurWin g_curWin;
// What End() needs to finish the window Begin() started: scrolling, the scroll bar, auto-resize, a hidden measuring frame.
struct WinEnd { bool open = false, laid = false, scrolls = false, showBar = false, autoSize = false, hidden = false;
                int slot = -1, id = 0; float x = 0, y = 0, w = 0, h = 0, titleH = 0, pad = 0, ox = 0, oy = 0; };
static WinEnd g_we;
static int  g_wflagsNext = 0;      // WindowFlags for the next core Begin
constexpr int kWinFlagStyle = 1 << 30;   // set by Begin(title, p_open, flags): the window is an ImGui-style one


static int g_focusWin = 0;
// SetNextWindowPos / SetNextWindowSize, consumed by the next Begin(title, p_open, flags).
struct NextWin { bool set = false; float x = 0, y = 0; int cond = 0; float pivX = 0, pivY = 0; };
static NextWin g_nextWinPos, g_nextWinSize;
void PinNextWindow() { g_pinNext = true; }
static bool g_bareNext = false;   // BareNextWindow: the next Begin has no title bar (a panel that fills a host's rectangle)
void BareNextWindow() { g_bareNext = true; }
static bool  g_noTitleNext = false;  // Window_NoTitleBar: the next Begin has no title bar (docked windows)
static bool  g_resizeNext = false;   // ResizableNextWindow: the next Begin gets a resize grip
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
        const bool fresh = slot && (slot->lastFrame == 0 || slot->lastFrame + 1 < g_frameCount);
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
    g_curWin = CurWin{true, id, wx, wy, w, bodyH, slot != nullptr && slot->lastFrame + 1 < g_frameCount};   // appearing: not drawn on the frame before
    g_curWin.z = slot ? slot->z : 0;
    if (title) std::snprintf(g_curWin.title, sizeof g_curWin.title, "%s", title);
    if (slot) { slot->lastFrame = g_frameCount; if (title) std::snprintf(slot->title, sizeof slot->title, "%s", title); }
    if (!hidden) registerWindowRect(id, g_curWin.z, wx, wy, w, bodyH);
    wrecBegin(id, g_curWin.z);
    g_we = WinEnd(); g_we.open = true; g_we.hidden = hidden; g_we.id = id; g_we.slot = slot ? (int)(slot - g_wins) : -1;
    g_we.autoSize = autoSize; g_we.x = wx; g_we.y = wy; g_we.w = w; g_we.h = h; g_we.titleH = titleH;
    if (bare) {
        // A bare window draws no frame at all: the host's own window behind it is the background.
    } else if (g_look) {   // launcher window: shadow, rounded body with a hairline, slim title bar
        const float wr = g_theme.windowRounding;
        if (!noBg) {
            if (g_theme.windowShadow) softShadow(wx, wy, w, bodyH, wr);
            rrect(wx, wy, w, bodyH, wr, g_theme.border);
            rrect(wx + 1.0f, wy + 1.0f, w - 2.0f, bodyH - 2.0f, wr > 1.0f ? wr - 1.0f : 0.0f, pick(Col_WindowBg, g_theme.panel));
        }
        scaleBodyAlpha(bodyV0, bgAlpha);
        if (!noTitle) rrect(wx + 1.0f, wy + 1.0f, w - 2.0f, titleH - 1.0f, wr > 1.0f ? wr - 1.0f : 0.0f, titleCol);
        if (!noTitle && !*collapsed && titleH > wr) quad(wx + 1.0f, wy + wr, w - 2.0f, titleH - wr, titleCol);
        if (!noTitle && !*collapsed) quad(wx + 1.0f, wy + titleH - 1.0f, w - 2.0f, 1.0f, g_theme.border);
        const float s0 = g_theme.textScale;
        if (!noTitle) {
        if (!noCollapse) chevron(wx + btn * 0.5f, wy + titleH * 0.5f, 9.0f, !*collapsed, overCaret ? g_theme.text : pick(Col_TextDisabled, g_theme.textDim));
        if (title && *title) drawText(noCollapse ? wx + 10.0f : wx + btn, wy + (titleH - textH()) * 0.5f, VisLabel(title).b, s0, g_theme.text);
        }
        if (p_open && !noTitle) {
            unsigned char tmp[4];
            const unsigned char white[4] = {255, 255, 255, 255};
            if (overClose) rrect(closeX + 5.0f, wy + 5.0f, btn - 10.0f, titleH - 10.0f, 6.0f, withAlpha(white, 0.10f, tmp));
            xMark(closeX + btn * 0.5f, wy + titleH * 0.5f, 9.0f, overClose ? g_theme.text : pick(Col_TextDisabled, g_theme.textDim));
        }
    } else if (!noBg) {
    Panel(wx, wy, w, bodyH);
    scaleBodyAlpha(bodyV0, bgAlpha);
    }
    if (!g_look && !bare && !noTitle) {
    // Title bar: round the TOP corners to match the panel, square at the bottom edge
    // (drawn as a rounded rect, then a square strip fills its lower half flush to body).
    const float rd = rnd();
    rrect(wx, wy, w, titleH, rd, titleCol);
    if (titleH > rd) quad(wx, wy + rd, w, titleH - rd, titleCol);
    quad(wx, wy + titleH - 2.0f, w, 2.0f, g_theme.accent);
    const float s = g_theme.textScale;
    if (!noCollapse) drawText(wx + 8.0f, wy + (titleH - textH()) * 0.5f, *collapsed ? "+" : "-", s, g_theme.text);  // caret
    if (title && *title) drawText(wx + 8.0f + (noCollapse ? 0.0f : fCharW() * s * 2.0f), wy + (titleH - textH()) * 0.5f, VisLabel(title).b, s, g_theme.text);
    if (p_open) {
        const unsigned char* xcol = overClose ? g_theme.accent : g_theme.text;
        drawText(closeX + (btn - fCharW() * s) * 0.5f, wy + (titleH - textH()) * 0.5f, "x", s, xcol);
    }
    }

    if (*collapsed) { g_lay.active = false; return false; }   // folded: skip content

    if (resizable) {   // grip marks: two diagonal strokes in the corner
        const bool gripAct = g_active == (id ^ 0x6f1d), gripHot = g_hot == (id ^ 0x6f1d);
        const unsigned char* gc = gripAct ? pick(Col_ResizeGripActive, g_theme.accent) : (gripHot ? pick(Col_ResizeGripHovered, g_theme.accent) : pick(Col_ResizeGrip, g_theme.textDim));
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
    g_lay.cx = g_lay.ox;
    g_lay.cy = g_lay.oy;
    g_lay.maxRight = g_lay.ox;
    g_we.laid = true; g_we.scrolls = flagStyle; g_we.showBar = showBar; g_we.pad = pad; g_we.ox = g_lay.ox; g_we.oy = g_lay.oy;
    g_lay.contentW = w - 2.0f * pad - (showBar ? kScrollbarW : 0.0f);
    g_lay.bottom = wy + h - pad;
    g_lay.spacingX = g_look ? 9.0f : 8.0f; g_lay.spacingY = g_look ? 8.0f : 6.0f;
    g_lay.indent = 0.0f;
    g_lay.pendingSameLine = false;
    g_lay.prevX = g_lay.ox; g_lay.prevY = g_lay.oy; g_lay.prevW = 0.0f; g_lay.prevH = 0.0f;
    return true;
}

// A scroll bar's track (classic look, or when ScrollbarBg is set) and thumb in the Col_Scrollbar* colours.
// With a pushed StyleVar_ScrollbarSize the bar is that wide and the grab sits 2 pixels inside it on every side, fully rounded.
static void scrollbarThumb(float sbx, float top, float trackH, float thumbY, float thumbH, bool active, bool over, bool track = true) {
    const float bw = g_sbSize > 0.0f ? g_sbSize : kScrollbarW;
    if (track && (!g_look || colorIsSet(Col_ScrollbarBg))) quad(sbx, top, bw, trackH, pick(Col_ScrollbarBg, g_theme.track));
    if (g_look || g_sbSize > 0.0f) {   // thin rounded thumb, faint until hovered
        unsigned char tmp[4];
        const unsigned char white[4] = {255, 255, 255, 255};
        const unsigned char* tc2 = active ? pick(Col_ScrollbarGrabActive, g_theme.accent) : (over ? pick(Col_ScrollbarGrabHovered, withAlpha(white, 0.26f, tmp)) : pick(Col_ScrollbarGrab, withAlpha(white, 0.14f, tmp)));
        if (g_sbSize > 0.0f) { const float gw = std::max(1.0f, bw - 4.0f); rrect(sbx + 2.0f, thumbY + 2.0f, gw, thumbH - 4.0f, gw * 0.5f, tc2); }
        else rrect(sbx + 3.0f, thumbY + 2.0f, kScrollbarW - 5.0f, thumbH - 4.0f, (kScrollbarW - 5.0f) * 0.5f, tc2);
    } else {
        const unsigned char* tc = active ? pick(Col_ScrollbarGrabActive, g_theme.text) : (over ? pick(Col_ScrollbarGrabHovered, g_theme.text) : pick(Col_ScrollbarGrab, g_theme.accent));
        quad(sbx + 1.0f, thumbY, kScrollbarW - 2.0f, thumbH, tc);
    }
}

static bool g_dockAware = false, g_dockRouted = false;   // SetDockAwareBegin: Begin runs a window of the dock layout as DockBegin; the End that closes it as DockEnd
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
void SetMouseCursor(MouseCursor cursor) { g_cursor = ((int)cursor >= 0 && (int)cursor < MouseCursor_COUNT) ? cursor : MouseCursor_Arrow; }
MouseCursor GetMouseCursor() { return g_cursor; }
namespace {
long long g_ioQueueFrame = -1; int g_ioQueueFilled = 0;
// Pending input fed through IO::AddKeyEvent / AddInputCharacter, applied by NewFrame (OkayUICompat.cpp).
bool g_pendMouseSet = false; float g_pendMX = 0, g_pendMY = 0; int g_pendBtn[3] = {-1, -1, -1}; bool g_pendClick[3] = {false, false, false};
float g_pendWheel = 0.0f; bool g_pendFocusLost = false;
std::string g_pendText; bool g_pendKeyDown[KeyId_COUNT] = {}; bool g_pendKeyEdge[KeyId_COUNT] = {};
unsigned decodeOne(const char*& p) {   // one UTF-8 code point (invalid bytes give U+FFFD)
    const unsigned char c = (unsigned char)*p++;
    if (c < 0x80) return c;
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    unsigned cp = n == 3 ? (c & 7u) : n == 2 ? (c & 15u) : n == 1 ? (c & 31u) : 0xFFFDu;
    for (int i = 0; i < n; ++i) { if (((unsigned char)*p & 0xC0) != 0x80) return 0xFFFDu; cp = (cp << 6) | ((unsigned char)*p++ & 63u); }
    return cp;
}
bool ioTypedTextConsumed() { return g_ioQueueFrame == (long long)g_frameCount && g_io.InputQueueCharacters.Size < g_ioQueueFilled; }
}
void IO::AddKeyEvent(int key, bool down) {
    if (key <= 0 || key >= KeyId_COUNT) return;
    g_pendKeyDown[key] = down;
    if (down) g_pendKeyEdge[key] = true;
}
void IO::AddMouseWheelEvent(float, float wheelY) { if (std::isfinite(wheelY)) g_pendWheel += wheelY; }
void IO::AddFocusEvent(bool focused) {
    if (focused) return;
    g_pendFocusLost = true;   // the next NewFrame lets go of every key and button the window can no longer see released
    for (int k = 0; k < KeyId_COUNT; ++k) g_pendKeyDown[k] = false;
    for (int b = 0; b < 3; ++b) { g_pendBtn[b] = 0; }
}
void IO::AddInputCharactersUTF8(const char* utf8) { if (!utf8) return; for (const char* p = utf8; *p;) AddInputCharacter(decodeOne(p)); }
void IO::AddMousePosEvent(float x, float y) { if (std::isfinite(x) && std::isfinite(y)) { g_pendMouseSet = true; g_pendMX = x; g_pendMY = y; } }
void IO::AddMouseButtonEvent(int b, bool down) { if (b < 0 || b > 1) return; g_pendBtn[b] = down ? 1 : 0; if (down) g_pendClick[b] = true; }
void IO::AddInputCharacter(unsigned cp) {
    if (cp == 0 || cp > 0x10FFFFu || (cp >= 0xD800u && cp < 0xE000u) || g_pendText.size() > 4096) return;
    char b[4]; int n;
    if (cp < 0x80) { b[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { b[0] = (char)(0xC0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 63)); n = 2; }
    else if (cp < 0x10000) { b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 63)); b[2] = (char)(0x80 | (cp & 63)); n = 3; }
    else { b[0] = (char)(0xF0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 63)); b[2] = (char)(0x80 | ((cp >> 6) & 63)); b[3] = (char)(0x80 | (cp & 63)); n = 4; }
    g_pendText.append(b, (size_t)n);
}
// Merge what AddKeyEvent / AddInputCharacter collected into a frame's input (NewFrame calls this), then forget the one-frame parts.
void MergePendingIo(Input& in) {
    static std::string text;
    text.clear();   // typed text is one-shot: only what AddInputCharacter collected
    text += g_pendText; g_pendText.clear();
    in.text = text.empty() ? nullptr : text.c_str();
    // NewFrame reuses the last frame's input, so its one-frame parts (the typed text, the key flags, the wheel) are cleared first.
    in.backspace = in.del = in.left = in.right = in.up = in.down = in.home = in.end = in.enter = in.escape = false;
    in.wheel = g_pendWheel; g_pendWheel = 0.0f; in.selectAll = in.copy = in.cut = in.paste = false; in.commit = nullptr;
    static bool mine[KeyId_COUNT] = {};   // keys whose state came from events last time
    for (int k = 1; k < KeyId_COUNT; ++k) {
        const bool now = g_pendKeyDown[k] || g_pendKeyEdge[k];   // pressed and released inside one frame still counts as a press
        if (now) in.keyDown[k] = true; else if (mine[k]) in.keyDown[k] = false;
        mine[k] = now;
    }
    {   // the modifier keys: either hand, or the modifier flag key; events decide once any arrived
        static bool mineCtrl = false, mineShift = false;
        const bool c = g_pendKeyDown[KeyId_Ctrl] || g_pendKeyDown[KeyId_RightCtrl] || g_pendKeyDown[KeyId_ModCtrl] || g_pendKeyDown[KeyId_LeftSuper] || g_pendKeyDown[KeyId_RightSuper] || g_pendKeyDown[KeyId_ModSuper];
        const bool sh = g_pendKeyDown[KeyId_Shift] || g_pendKeyDown[KeyId_RightShift] || g_pendKeyDown[KeyId_ModShift];
        if (c) in.ctrl = true; else if (mineCtrl) in.ctrl = false;
        if (sh) in.shift = true; else if (mineShift) in.shift = false;
        mineCtrl = c; mineShift = sh;
    }
    if (g_pendMouseSet) { in.mouseX = g_pendMX; in.mouseY = g_pendMY; g_pendMouseSet = false; }
    {   // a press and a release inside one frame show as one pressed frame; the NEXT frame must show the release (nothing else would: the input is carried over)
        static int releaseNext[2] = {0, 0};   // frames the press is still shown after a one-frame click (a click must last two frames: the first presses, the second is when a widget sees the pointer held)
        bool* down[2] = {&in.mouseDown, &in.rightDown};
        for (int b = 0; b < 2; ++b) {
            if (g_pendBtn[b] >= 0 || g_pendClick[b]) {
                const bool oneFrameClick = g_pendClick[b] && g_pendBtn[b] != 1;
                *down[b] = g_pendBtn[b] == 1 || oneFrameClick;
                releaseNext[b] = oneFrameClick ? 2 : 0;
            } else if (releaseNext[b] > 0) { *down[b] = releaseNext[b] > 1; --releaseNext[b]; }
        }
    }
    for (int b = 0; b < 3; ++b) { g_pendBtn[b] = -1; g_pendClick[b] = false; }
    if (g_pendKeyEdge[KeyId_Enter]) in.enter = true;
    if (g_pendKeyEdge[KeyId_Escape]) in.escape = true;
    if (g_pendKeyEdge[KeyId_Backspace]) in.backspace = true;
    if (g_pendKeyEdge[KeyId_Delete]) in.del = true;
    if (g_pendKeyEdge[KeyId_Left]) in.left = true;
    if (g_pendKeyEdge[KeyId_Right]) in.right = true;
    if (g_pendKeyEdge[KeyId_Up]) in.up = true;
    if (g_pendKeyEdge[KeyId_Down]) in.down = true;
    if (g_pendKeyEdge[KeyId_Home]) in.home = true;
    if (g_pendKeyEdge[KeyId_End]) in.end = true;
    for (int k = 0; k < KeyId_COUNT; ++k) g_pendKeyEdge[k] = false;
}
IO& IoRaw() { return g_io; }   // what a host wrote into the IO (DisplaySize, DeltaTime ...), before GetIO() overwrites those fields with this frame's values
IO& GetIO() {
    if (g_ioQueueFrame != (long long)g_frameCount) {   // this frame's typed characters
        g_io.InputQueueCharacters.clear();
        if (g_in.text) for (const char* p = g_in.text; *p;) { const unsigned cp = decodeOne(p); if (cp > 0 && cp <= 0xFFFFu) g_io.InputQueueCharacters.push_back((unsigned short)cp); }
        g_ioQueueFrame = (long long)g_frameCount; g_ioQueueFilled = g_io.InputQueueCharacters.Size;
    }
    g_io.Fonts = GetFontAtlasRef();
    g_io.MouseClickedCount[0] = IsMouseDoubleClicked(0) ? 2 : (g_pressed ? 1 : 0);
    g_io.KeySuper = g_io.KeySuper;
    // These two depend on windows drawn since BeginFrame, so they are worked out when asked.
    g_io.mouseDelta = Vec2{g_mouseDX, g_mouseDY};
    g_io.wantCaptureMouse = !g_in.blocked && (g_ptrOverWindow || g_ptrOverWindowPrev || g_active != 0);
    g_io.MousePos = g_io.mousePos; g_io.MouseDelta = g_io.mouseDelta; g_io.DisplaySize = g_io.displaySize; g_io.MouseWheel = g_io.mouseWheel;
    g_io.MouseDown[0] = g_io.mouseDown[0]; g_io.MouseDown[1] = g_io.mouseDown[1];
    g_io.KeyCtrl = g_io.keyCtrl; g_io.KeyShift = g_io.keyShift; g_io.KeyAlt = g_io.keyAlt;
    g_io.WantCaptureMouse = g_io.wantCaptureMouse; g_io.WantCaptureKeyboard = g_io.wantCaptureKeyboard; g_io.WantTextInput = g_io.wantTextInput;
    g_io.DeltaTime = g_io.deltaTime; g_io.Framerate = g_io.deltaTime > 0.0f ? 1.0f / g_io.deltaTime : 0.0f;
    return g_io;
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
        const bool appearing = slot->lastFrame + 1 < g_frameCount;      // not drawn on the frame before
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
            const bool appearing = slot->lastFrame + 1 < g_frameCount;
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

Vec2 GetWindowPos() { return g_curWin.valid ? Vec2{g_curWin.x, g_curWin.y} : Vec2{0, 0}; }
Vec2 GetWindowSize() { return g_curWin.valid ? Vec2{g_curWin.w, g_curWin.h} : Vec2{0, 0}; }
float GetWindowWidth() { return g_curWin.valid ? g_curWin.w : 0.0f; }
float GetWindowHeight() { return g_curWin.valid ? g_curWin.h : 0.0f; }
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
        const bool appearing = ws.lastFrame + 1 < g_frameCount;
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
Vec2 GetMousePosOnOpeningCurrentPopup() {
    if (g_popupTop > 0) { const PopupSlot* p = popupSlot(g_popupStack[g_popupTop - 1].id, false); if (p) return Vec2{p->x, p->y}; }
    return Vec2{g_in.mouseX, g_in.mouseY};
}
const DragPayload* GetDragDropPayload() {
    if (!g_tdd.active || !g_tdd.hasPayload) return nullptr;
    static DragPayload copy;
    copy = g_tdd.pay; copy.delivery = false; copy.Data = copy.data; copy.DataSize = copy.size;
    return &copy;
}
unsigned GetActiveID() { return (unsigned)g_active; }
void ClearActiveID() { g_active = 0; if (g_focus != 0) { g_focusLostEnd = g_focus; g_focus = 0; } }
CurrentWindow GetCurrentWindow() {
    CurrentWindow w;
    if (!g_curWin.valid) return w;
    w.valid = true; w.id = (unsigned)g_curWin.id; w.pos = Vec2{g_curWin.x, g_curWin.y}; w.size = Vec2{g_curWin.w, g_curWin.h};
    w.focused = g_focusWin == g_curWin.id; w.hovered = IsWindowHovered(); w.appearing = g_curWin.appearing; w.z = g_curWin.z;
    w.ID = w.id; w.Pos = w.pos; w.Size = w.size;
    std::snprintf(w.title, sizeof w.title, "%s", g_curWin.title);
    return w;
}
Vec2 GetMouseDragDelta(int button, float threshold) {
    if (!IsMouseDragging(button, threshold)) return Vec2{0, 0};
    return Vec2{g_in.mouseX - g_pressX, g_in.mouseY - g_pressY};
}
void ResetMouseDragDelta(int button) { if (button == 0) { g_pressX = g_in.mouseX; g_pressY = g_in.mouseY; } }
bool IsAnyItemHovered() { return g_lay.active && (g_anyHoverNow || g_anyHoverPrev); }
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
        const bool appearing = ws.lastFrame + 1 < g_frameCount;
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
Context* RefreshContext() {
    static Context c;
    c.ActiveId = (unsigned)g_active; c.HoveredId = (unsigned)g_hot; c.FrameCount = (int)g_frameCount; c.Time = GetTime();
    { static unsigned lastHot = 0; static double hotSince = 0.0; if (c.HoveredId != lastHot) { lastHot = c.HoveredId; hotSince = c.Time; } c.HoveredIdTimer = c.HoveredId ? (float)(c.Time - hotSince) : 0.0f; }
    const WinSlot* hov = nullptr;
    if (!g_in.blocked) for (const WinSlot& ws : g_wins) {
        if (!ws.used || ws.lastFrame + 1 < g_frameCount || !pointIn(g_in.mouseX, g_in.mouseY, ws.x, ws.y, ws.w, ws.h)) continue;
        if (!hov || ws.z >= hov->z) hov = &ws;
    }
    unsigned activeWin = 0;
    if (g_active != 0 && g_focusWin != 0) for (const WinSlot& ws : g_wins) if (ws.used && ws.id == g_focusWin) { activeWin = (unsigned)ws.id; break; }
    unsigned curWin = 0;
    if (g_curWin.valid) for (const WinSlot& ws : g_wins) if (ws.used && ws.id == g_curWin.id) { curWin = (unsigned)ws.id; break; }
    FillContextFromWindowTable(c, hov ? (unsigned)hov->id : 0u, activeWin, curWin, (unsigned)g_focusWin);
    return &c;
}
WindowRef* GetTopMostPopupModal() {
    static WindowRef ref;
    for (const PopupSlot& p : g_popups) if (p.used && p.open && p.modal) { ref = WindowRef(); ref.ID = (unsigned)p.id; ref.Pos = Vec2{p.x, p.y}; ref.Size = Vec2{p.w, 0.0f}; ref.Active = ref.WasActive = true; ref.RootWindow = &ref; return &ref; }
    return nullptr;
}
void SetNextFrameWantCaptureKeyboard(bool want) { g_nextFrameKbd = want ? 1 : 0; }
float GetTreeNodeToLabelSpacing() {
    if (g_look) return 24.0f;
    return 4.0f + fCharW() * g_theme.textScale * 1.5f;
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
    if (outline) {   // a stroked outline (filling under it would cost a whole child's area twice on a CPU rasteriser)
        const float rd = rnd();
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
        if (!g_look || colorIsSet(Col_ScrollbarBg)) quad(sbx, trackY, c.sbW, trackH, pick(Col_ScrollbarBg, g_theme.track));
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

void SameLine(float spacing) {
    if (!g_lay.active) return;
    g_lay.pendingSameLine = true;
    g_lay.sameLineSpacing = spacing;
}

void SameLine(float offsetFromStartX, float spacing) {
    if (!g_lay.active) return;
    g_lay.pendingSameLine = true;
    if (std::isfinite(offsetFromStartX) && offsetFromStartX > 0.0f) {
        const float target = g_lay.ox + offsetFromStartX, end = g_lay.prevX + g_lay.prevW;
        g_lay.sameLineSpacing = target > end ? target - end : 0.0f;
    } else g_lay.sameLineSpacing = std::isfinite(spacing) ? spacing : -1.0f;
}

void SetNextItemWidth(float w) { g_nextItemW = w; }
void PushItemWidth(float w)    { if (g_itemWTop < 16) g_itemWStack[g_itemWTop++] = w; }
void PopItemWidth()            { if (g_itemWTop > 0) --g_itemWTop; }

void PushClipRect(Vec2 mn, Vec2 mx, bool) { PushClipRect(mn.x, mn.y, mx.x - mn.x, mx.y - mn.y); }
bool BeginChild(unsigned id, Vec2 size, int childFlags, int windowFlags) { char name[24]; std::snprintf(name, sizeof name, "##%u", id); return BeginChild(name, size, childFlags, windowFlags); }
bool ImageButton(const char* id, SDL_Texture* tex, Vec2 size, Vec2, Vec2, Vec4, Vec4) { return ImageButton(id, tex, size.x, size.y); }
void Render() { EndFrameData(); }
void PushClipRect(float x, float y, float w, float h) {
    SDL_Rect r; r.x = clampPx(x, 0); r.y = clampPx(y, 0); r.w = clampPx(w < 0 ? 0 : w, 0); r.h = clampPx(h < 0 ? 0 : h, 0);
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
    quad(leftEdge(), y + gap * 0.5f, fullW(), 1.0f, pick(Col_Separator, g_theme.border));
}

namespace {
// One line of text at the cursor in colour `col`.
float g_textRowH = 0.0f;   // StyleVar_TextRowHeight: 0 = a text line takes a frame's height (rowH)
// StyleVar_CellPadding: the gap inside a cell, across (default 4) and above and below (default -1: none, and the row ends after the
// last item's spacing, as it always did; 0 or more: that much above the first item and below the last one, without its spacing).
float g_cellPadX = 4.0f, g_cellPadY = -1.0f;
// The boxes of Checkbox and RadioButton: a text line + 6, or with StyleVar_FramePadding (y) a text line plus twice it (g_framePadY).
// OkayUI's own style values for Dear ImGui-looking menus and fields (PushStyleVar; < 0 = the usual OkayUI value):
// StyleVar_PopupPadding (x, y) inside a popup (default 6, 6), StyleVar_PopupWidth (default 160), StyleVar_ItemInnerSpacing (x only: the gap
// between the cells of a formatted drag row and before its label).
float g_popupPadX = -1.0f, g_popupPadY = -1.0f, g_popupW = -1.0f, g_innerX = -1.0f;
inline float boxSize() { return textH() + (g_framePadY >= 0.0f ? 2.0f * g_framePadY : 6.0f); }
void textLine(const char* s, const unsigned char* col, float rowHeight = -1.0f) {
    const float h = rowHeight > 0.0f ? rowHeight : (g_textRowH > 0.0f ? g_textRowH : rowH());
    const bool after = g_lay.pendingSameLine && !g_posOverride;
    const float prevH = g_lay.prevH;
    const bool onText = after && g_lay.hasLineTop;   // after a centred text: on that text's line (one baseline), the line's top kept
    const float top = g_lay.lineTopY;
    g_lay.hasLineTop = false;
    float x, y; place(s && *s ? labelW(s) : 1.0f, h, x, y);
    if (onText) { g_lay.hasLineTop = true; g_lay.lineTopY = top; }
    else if (after && prevH > h && g_textRowH > 0.0f) { g_lay.hasLineTop = true; g_lay.lineTopY = y; y += (prevH - h) * 0.5f; g_lay.prevY = y; }   // Dear ImGui's text rows: centred on the taller item before it on this line
    if (s && *s) drawText(x, y + (h - textH()) * 0.5f, s, g_theme.textScale, col);
}

// The lines of one paragraph. The first line is exactly what a one-line text is (a row of the normal height, so a bullet or an icon the caller
// aligned to it stays aligned); each further line sits one text pitch (text height + 4) below the previous line's text, not a framed
// widget's row below it, which would double-space the paragraph.
struct ParaLines {
    const unsigned char* col; char prev[512]; bool has = false; int idx = 0; float t1 = 0.0f;
    explicit ParaLines(const unsigned char* c) : col(c) { prev[0] = 0; }
    void Emit(const char* line) {
        if (idx == 0) { textLine(line, col); t1 = g_lay.prevY + (rowH() - textH()) * 0.5f; }
        else { const float pitch = textH() + 4.0f; g_lay.cy = t1 + (float)idx * pitch - 2.0f; textLine(line, col, pitch); }
        ++idx;
    }
    void Add(const char* line) {
        if (has) Emit(prev);
        std::strncpy(prev, line, sizeof prev - 1); prev[sizeof prev - 1] = 0; has = true;
    }
    void Done() { if (has) Emit(prev); has = false; }
};

// Word-wrapped text: lines are filled word by word up to maxW, '\n' always breaks. With Dear ImGui's text rows (TextRowHeight > 0)
// the lines of one paragraph are one text line apart (no item spacing between them), and the lines after the first fill the width
// from the left edge (`maxRest`) even when the first one began after another item on its line.
void wrapLines(const char* text, float maxW, const unsigned char* col, float maxRest = -1.0f) {
    char line[512]; int n = 0;
    const char* p = text;
    const bool imguiRows = g_textRowH > 0.0f;
    bool first = true;
    ParaLines para(col);   // OkayUI's own rows: the lines of a paragraph one text pitch apart (main's ParaLines)
    auto emit = [&](const char* l) {
        if (!imguiRows) { para.Add(l); first = false; return; }
        if (!first) { g_lay.cy = g_lay.prevY + g_lay.prevH; g_lay.pendingSameLine = false; }
        textLine(l, col);
        if (first && imguiRows && maxRest > 0.0f) maxW = maxRest;
        first = false;
    };
    if (imguiRows) {   // Dear ImGui's wrapping: the text's own spaces are kept, each word is measured once, a line breaks before the word
                       // that does not fit, and the spaces at the break are dropped
        const char* q = text;
        while (*q) {
            const char* r = q; const char* fitEnd = nullptr; float w = 0.0f;
            while (*r && *r != '\n') {
                const char* we = r;
                while (*we == ' ') ++we;
                while (*we && *we != ' ' && *we != '\n') ++we;
                char piece[512]; int k = 0;
                for (const char* c = r; c < we && k < 500; ++c) piece[k++] = *c;
                piece[k] = 0;
                const float add = fMeasureWhole(piece, g_theme.textScale);   // whole-pixel advances, as Dear ImGui's wrap measures
                if (fitEnd && w + add > maxW) break;
                w += add; fitEnd = we; r = we;
            }
            const char* end = fitEnd ? fitEnd : r;
            int k = 0;
            for (const char* c = q; c < end && k < 500; ++c) line[k++] = *c;
            line[k] = 0;
            emit(line);
            q = end;
            while (*q == ' ') ++q;
            if (*q == '\n') ++q;
        }
        return;
    }
    // A paragraph that fits on one line is one line (most calls): measured once.
    if (!std::strchr(text, '\n') && labelW(text) <= maxW) {
        int k = 0;
        for (const char* q = text; *q && k < 500; ++q) line[k++] = *q;
        if (k < 500 || !text[k]) {   // whole (a longer text goes through the loop below, which cuts it at 500 bytes a line)
            line[k] = 0;
            // the same words and single spaces the loop would keep: a run of spaces collapses there, so only take the shortcut without one
            if (k > 0 && !std::strstr(text, "  ") && text[0] != ' ' && text[k - 1] != ' ') { textLine(line, col); return; }
        }
    }
    while (*p) {
        const char* wordEnd = p;
        while (*wordEnd && *wordEnd != ' ' && *wordEnd != '\n') ++wordEnd;
        const int wl = (int)(wordEnd - p);
        char trial[512]; int tn = 0;
        for (int i = 0; i < n && tn < 500; ++i) trial[tn++] = line[i];
        if (n > 0 && tn < 500) trial[tn++] = ' ';
        for (int i = 0; i < wl && tn < 500; ++i) trial[tn++] = p[i];
        trial[tn] = 0;
        if (n > 0 && labelW(trial) > maxW) { line[n] = 0; emit(line); n = 0; continue; }
        for (int i = 0; i < tn && i < 500; ++i) line[i] = trial[i];
        n = tn < 500 ? tn : 500;
        p = wordEnd;
        if (*p == '\n') { line[n] = 0; emit(line); n = 0; ++p; }
        else if (*p == ' ') ++p;
    }
    if (n > 0) { line[n] = 0; emit(line); }
    para.Done();
}

// Text honouring PushTextWrapPos.
void textBlock(const char* s, const unsigned char* col) {
    if (g_wrapPos >= 0.0f && s && *s) {
        float maxW = availW(), rest = fullW();
        if (g_wrapPos > 0.0f && g_wrapPos < maxW) maxW = g_wrapPos;
        if (g_wrapPos > 0.0f && g_wrapPos < rest) rest = g_wrapPos;
        wrapLines(s, maxW, col, rest);
    } else if (s && g_textRowH <= 0.0f && std::strchr(s, '\n')) {   // several lines: each on its own row, close together (Dear ImGui's text rows keep one item)
        ParaLines para(col);
        char line[512];
        for (const char* p = s;;) {
            const char* e = std::strchr(p, '\n');
            const std::size_t n = e ? (std::size_t)(e - p) : std::strlen(p);
            const std::size_t m = n < sizeof line - 1 ? n : sizeof line - 1;
            std::memcpy(line, p, m); line[m] = 0;
            para.Add(line);
            if (!e) break;
            p = e + 1;
        }
        para.Done();
    } else textLine(s, col);
}
} // namespace

void BeginGroup() {
    if (!g_lay.active || g_grpTop >= 8) return;
    g_grp[g_grpTop++] = GroupBox();
}

void EndGroup() {
    if (!g_lay.active || g_grpTop <= 0) return;
    const GroupBox g = g_grp[--g_grpTop];
    g_lay.hasLineTop = false;
    if (g.any) {
        g_lay.prevX = g.x0; g_lay.prevY = g.y0; g_lay.prevW = g.x1 - g.x0; g_lay.prevH = g.y1 - g.y0;
    } else {   // an empty group is a zero-size item at the cursor
        g_lay.prevX = leftEdge(); g_lay.prevY = g_lay.cy; g_lay.prevW = 0.0f; g_lay.prevH = 0.0f;
    }
    g_lastId = 0;
    g_lay.pendingSameLine = false;
    groupAdd(g_lay.prevX, g_lay.prevY, g_lay.prevW, g_lay.prevH);   // it counts for the group around it
}

void PushTextWrapPos(float width) { g_wrapPos = (std::isfinite(width) && width >= 0.0f) ? width : 0.0f; }
void PopTextWrapPos() { g_wrapPos = -1.0f; }

void Text(const char* s) {
    if (!g_lay.active) return;
    textBlock(s, g_theme.text);
}

void TextColored(unsigned char r, unsigned char g, unsigned char b, const char* s) {
    if (!g_lay.active) return;
    const unsigned char col[4] = { r, g, b, 255 };
    textBlock(s, col);
}

void TextDisabled(const char* s) {
    if (g_look || colorIsSet(Col_TextDisabled)) { const unsigned char* d = pick(Col_TextDisabled, g_theme.textDim); TextColored(d[0], d[1], d[2], s); return; }
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
    const float lw = label && *label ? visW(label) : 0.0f;
    if (lw > 0.0f) drawText(x, ty, VisLabel(label).b, g_theme.textScale, g_theme.text);
    // A line filling the space to the right of the label.
    const float lineX = x + (lw > 0.0f ? lw + 8.0f : 0.0f);
    const float lineW = (x + fullW()) - lineX;
    if (lineW > 2.0f) quad(lineX, y + h * 0.5f, lineW, 1.0f, g_theme.border);
}

void Dummy(Vec2 size) { Dummy(size.x, size.y); }
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
    const float w = visW(label) + 24.0f, h = rowH();
    float x, y; place(w, h, x, y);
    return Button(hashLabel(label), x, y, w, h, label);
}

// A size from ImGui-style arguments: 0 or less (or not a number) in either axis keeps the automatic value.
static float sizeOr(float v, float automatic) { return (v > 0.0f && std::isfinite(v)) ? std::min(v, 1.0e6f) : automatic; }

bool Button(const char* label, Vec2 size) {
    if (!g_lay.active) return false;
    const float sx = std::isfinite(size.x) ? size.x : 0.0f, sy = std::isfinite(size.y) ? size.y : 0.0f;
    const float w = sx > 0.0f ? sx : (sx < 0.0f ? availW() : visW(label) + 24.0f), h = sy > 0.0f ? sy : rowH();
    float x, y; place(w, h, x, y);
    return Button(hashLabel(label), x, y, w, h, label);
}

bool SmallButton(const char* label) {
    if (!g_lay.active) return false;
    const float w = visW(label) + 12.0f, h = textH() + 6.0f;   // tight padding
    g_lay.hasLineTop = false;   // a small button sits on the text line it follows (Dear ImGui aligns it to the text's baseline)
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


// ---- Tab bar with per-tab bodies (BeginTabBar / BeginTabItem) ---------------------------------------
namespace {
int* tabSelSlot(int bar) {
    for (auto& s : g_tabSel) if (s.bar == bar) return &s.sel;
    for (auto& s : g_tabSel) if (s.bar == 0) { s.bar = bar; s.sel = 0; return &s.sel; }
    g_tabSel[0].bar = bar; g_tabSel[0].sel = 0; return &g_tabSel[0].sel;
}
} // namespace

bool BeginTabBar(const char* id, int tabBarFlags) {
    if (!g_lay.active || !id || g_tabBar.active) return false;
    const float h = rowH();
    float x, y; place(fullW(), h, x, y);
    g_tabBar = TabBarCtx();
    g_tabBar.active = true; g_tabBar.id = hashLabel(id) | 1;
    g_tabBar.x = x; g_tabBar.y = y; g_tabBar.w = fullW(); g_tabBar.h = h;
    g_tabBar.cx = x; g_tabBar.tr = x + fullW();
    g_tabBar.sel = tabSelSlot(g_tabBar.id);
    g_tabBar.flags = tabBarFlags;
    if (tabBarFlags & TabBar_Reorderable) {   // the order slot of this bar (or the least recently used one)
        TabOrder* slot = nullptr; TabOrder* oldest = &g_tabOrders[0];
        for (TabOrder& t : g_tabOrders) {
            if (t.bar == g_tabBar.id) { slot = &t; break; }
            if (t.lastFrame < oldest->lastFrame) oldest = &t;
        }
        if (!slot) { slot = oldest; *slot = TabOrder(); slot->bar = g_tabBar.id; g_tabBar.fresh = true; }
        slot->lastFrame = g_frameCount;
        g_tabBar.ord = slot;
    }
    if (!g_look) quad(x, y + h - 2.0f, fullW(), 2.0f, g_theme.border);   // a baseline under the whole row (classic look)
    return true;
}
bool BeginTabBar(const char* id) { return BeginTabBar(id, 0); }

int TabDragTarget(const float* widths, int n, int from, float left, float gap, float mouseX) {
    if (!widths || n <= 0 || from < 0 || from >= n) return from;
    if (!std::isfinite(mouseX) || !std::isfinite(left) || !std::isfinite(gap)) return from;
    float x = left;
    for (int i = 0; i < n; ++i) {
        const float w = std::isfinite(widths[i]) && widths[i] > 0.0f ? widths[i] : 0.0f;
        if (mouseX < x + w + gap * 0.5f) return i;
        x += w + gap;
    }
    return n - 1;   // past the last tab
}

bool BeginTabItem(const char* label, bool* open, int flags) {
    if (!g_lay.active || !g_tabBar.active || !label) return false;
    TabBarCtx& tb = g_tabBar;
    const int id = (hashLabel(label) ^ tb.id) | 1;
    const float closeW = open ? 16.0f : 0.0f;
    const float w = visW(label) + (g_look ? 28.0f : 20.0f) + closeW, gap = g_look ? 6.0f : 2.0f;
    float x;
    int ordIdx = -1;
    if (flags & Tab_Trailing) { tb.tr -= w; x = tb.tr; tb.tr -= gap; }
    else if (tb.ord) {
        TabOrder& o = *tb.ord;
        for (int i = 0; i < o.n; ++i) if (o.ids[i] == id) { ordIdx = i; break; }
        if (ordIdx < 0 && o.n < 32) {   // a tab the bar has not met: it goes last
            ordIdx = o.n++; o.ids[ordIdx] = id; o.w[ordIdx] = w;
            if ((tb.flags & TabBar_AutoSelectNewTabs) && !tb.fresh) *tb.sel = id;
        }
        if (ordIdx >= 0) o.w[ordIdx] = w;
        x = tb.x;
        for (int i = 0; i < ordIdx; ++i) x += o.w[i] + gap;
        if (ordIdx < 0) { x = tb.cx; }
        tb.cx = x + w + gap;
        if (tb.nSeen < 32) tb.seen[tb.nSeen++] = id;
    }
    else { x = tb.cx; tb.cx += w + gap; }
    if (tb.firstId == 0) tb.firstId = id;
    if (*tb.sel == 0) *tb.sel = id;                  // nothing chosen yet: the first tab
    if (flags & Tab_SetSelected) *tb.sel = id;
    Tab(id, x, tb.y, w, tb.h, label, tb.sel, id);
    g_lay.prevX = x; g_lay.prevY = tb.y; g_lay.prevW = w; g_lay.prevH = tb.h;   // the item rect / hover queries that follow are about this tab's header
    if (flags & TabItem_UnsavedDocument) {            // a small dot after the label
        const float dx = x + w - closeW - 9.0f, dy = tb.y + tb.h * 0.5f - 2.0f;
        quad(dx, dy, 4.0f, 4.0f, g_theme.accent);
    }
    if (tb.ord && ordIdx >= 0 && !(flags & (Tab_Trailing | TabItem_NoReorder)) && g_active == id && IsMouseDragging(0, 4.0f) && g_in.mouseDown) {
        TabOrder& o = *tb.ord;   // dragging: the tab goes where the pointer is
        const int to = TabDragTarget(o.w, o.n, ordIdx, tb.x, gap, g_in.mouseX);
        if (to != ordIdx) {
            const int tid = o.ids[ordIdx]; const float tw = o.w[ordIdx];
            if (to > ordIdx) for (int i = ordIdx; i < to; ++i) { o.ids[i] = o.ids[i + 1]; o.w[i] = o.w[i + 1]; }
            else for (int i = ordIdx; i > to; --i) { o.ids[i] = o.ids[i - 1]; o.w[i] = o.w[i - 1]; }
            o.ids[to] = tid; o.w[to] = tw;
        }
    }
    bool visible = (*tb.sel == id);
    if (open) {                                       // a small x on the right closes the tab
        const float cx = x + w - closeW, cy = tb.y + (tb.h - textH()) * 0.5f;
        const int cid = id ^ 0x7ab;
        const bool inside = hit(cx, tb.y, closeW - 2.0f, tb.h);
        if (inside) g_hot = cid;
        if (g_active == cid) { if (g_released) { if (inside) { *open = false; } g_active = 0; } }
        else if (inside && g_pressed) g_active = cid;
        drawText(cx + 3.0f, cy, "x", g_theme.textScale, inside ? g_theme.text : pick(Col_TextDisabled, g_theme.textDim));
        // a middle click anywhere on the tab closes it too
        if (g_mPressed && !(flags & TabItem_NoCloseWithMiddleMouseButton) && !(tb.flags & TabBar_NoCloseWithMiddleMouseButton) && hit(x, tb.y, w, tb.h)) *open = false;
        if (!*open) visible = false;
    }
    if (visible) tb.seenSel = true;
    return visible;
}

void EndTabItem() {}

// A button on a dock node's strip (DockNodeBeginAmendTabBar in OkayUIDockNodes.cpp): placed right-to-left from the strip's right edge.
namespace { struct TabAmendState { bool on = false; float x = 0, y = 0, w = 0, h = 0, used = 0; } g_amend; }
void SetTabAmend(bool on, Rect strip) { g_amend.on = on; g_amend.x = strip.x; g_amend.y = strip.y; g_amend.w = strip.w; g_amend.h = strip.h; g_amend.used = 0.0f; }
float TabAmendUsed() { return g_amend.used; }
bool TabItemButton(const char* label, int flags) {
    if (g_amend.on && g_lay.active && !g_tabBar.active && label) {
        const int id = (hashLabel(label) ^ 0x7bc) | 1;
        const float w = g_nextItemW > 0.0f ? g_nextItemW : visW(label) + 20.0f, gap = g_look ? 6.0f : 2.0f;
        g_nextItemW = -1.0f;
        const float x = g_amend.x + g_amend.w - g_amend.used - w;
        g_amend.used += w + gap;
        if (w < 1.0f || x < g_amend.x) return false;
        const bool clicked = Button(id, x, g_amend.y, w, g_amend.h, label);
        g_lay.prevX = x; g_lay.prevY = g_amend.y; g_lay.prevW = w; g_lay.prevH = g_amend.h;
        return clicked;
    }
    if (!g_lay.active || !g_tabBar.active || !label) return false;
    TabBarCtx& tb = g_tabBar;
    const int id = (hashLabel(label) ^ tb.id ^ 0x7bb) | 1;
    const float w = visW(label) + 20.0f, gap = g_look ? 6.0f : 2.0f;
    float x;
    if (flags & Tab_Trailing) { tb.tr -= w; x = tb.tr; tb.tr -= gap; } else { x = tb.cx; tb.cx += w + gap; }
    const bool clicked = Button(id, x, tb.y, w, tb.h, label);
    g_lay.prevX = x; g_lay.prevY = tb.y; g_lay.prevW = w; g_lay.prevH = tb.h;
    return clicked;
}

void EndTabBar() {
    if (!g_tabBar.active) return;
    // The chosen tab vanished (closed or removed): fall back to the first.
    if (!g_tabBar.seenSel && g_tabBar.firstId != 0) *g_tabBar.sel = g_tabBar.firstId;
    if (g_tabBar.ord) {   // forget tabs that were not shown this frame (closed or removed), so no gap is left
        TabOrder& o = *g_tabBar.ord; int k = 0;
        for (int i = 0; i < o.n; ++i) {
            bool seen = false;
            for (int j = 0; j < g_tabBar.nSeen; ++j) if (g_tabBar.seen[j] == o.ids[i]) { seen = true; break; }
            if (seen) { o.ids[k] = o.ids[i]; o.w[k] = o.w[i]; ++k; }
        }
        o.n = k;
    }
    g_tabBar.active = false;
}

// ---- Style variables (PushStyleVar / PopStyleVar) -----------------------------------------------------
namespace {
struct StyleSave { int var; float a, b; };
StyleSave g_styleStack[32];
int g_styleTop = 0;
}
void PushStyleVar(StyleVar var, Vec2 value) {
    if (g_styleTop >= 32) return;                    // too deep: ignored (and the matching Pop is ignored too, see PopStyleVar)
    StyleSave s{(int)var, 0.0f, 0.0f};
    switch (var) {
        case StyleVar_ItemSpacing:   s.a = g_lay.spacingX; s.b = g_lay.spacingY; g_lay.spacingX = value.x; g_lay.spacingY = value.y; break;
        case StyleVar_WindowPadding: s.a = g_theme.windowPadding; s.b = g_childPadY; g_theme.windowPadding = value.x;
                                     g_childPadY = std::isfinite(value.y) && value.y >= 0.0f ? std::min(value.y, 256.0f) : 6.0f; break;
        case StyleVar_FrameRounding:
        case StyleVar_ChildRounding: s.a = g_theme.rounding; g_theme.rounding = value.x; break;
        case StyleVar_WindowRounding: s.a = g_theme.windowRounding; g_theme.windowRounding = value.x; break;
        case StyleVar_BorderSize:    s.a = g_theme.borderPx; g_theme.borderPx = value.x; break;
        case StyleVar_CellPadding:   s.a = g_cellPadX; s.b = g_cellPadY;
                                     g_cellPadX = std::isfinite(value.x) ? std::min(std::max(value.x, 0.0f), 256.0f) : 4.0f;
                                     g_cellPadY = std::isfinite(value.y) ? std::min(std::max(value.y, 0.0f), 256.0f) : -1.0f; break;
        case StyleVar_FramePadding:  s.a = g_framePadY; s.b = g_framePadX;
                                     g_framePadY = std::isfinite(value.y) ? std::min(std::max(value.y, 0.0f), 64.0f) : -1.0f;
                                     g_framePadX = std::isfinite(value.x) ? std::min(std::max(value.x, 0.0f), 64.0f) : -1.0f; break;
        case StyleVar_ScrollbarSize: s.a = g_sbSize; g_sbSize = std::isfinite(value.x) && value.x >= 4.0f ? std::min(value.x, 64.0f) : -1.0f; break;
        case StyleVar_FramedWidgets: s.a = g_framedWidgets ? 1.0f : 0.0f; g_framedWidgets = std::isfinite(value.x) && value.x != 0.0f; break;
        case StyleVar_ButtonTextAlign: s.a = g_btnAlignX; g_btnAlignX = std::isfinite(value.x) ? std::min(1.0f, std::max(0.0f, value.x)) : 0.5f; break;
        case StyleVar_TextRowHeight: s.a = g_textRowH; g_textRowH = (std::isfinite(value.x) && value.x > 0.0f) ? std::min(value.x, 4096.0f) : 0.0f; break;
        case StyleVar_PopupPadding:  s.a = g_popupPadX; s.b = g_popupPadY;
                                     g_popupPadX = std::isfinite(value.x) && value.x >= 0.0f ? std::min(value.x, 256.0f) : -1.0f;
                                     g_popupPadY = std::isfinite(value.y) && value.y >= 0.0f ? std::min(value.y, 256.0f) : -1.0f; break;
        case StyleVar_PopupWidth:    s.a = g_popupW; g_popupW = std::isfinite(value.x) && value.x > 0.0f ? std::min(value.x, 4096.0f) : -1.0f; break;
        case StyleVar_ItemInnerSpacing: s.a = g_innerX; g_innerX = std::isfinite(value.x) && value.x >= 0.0f ? std::min(value.x, 256.0f) : -1.0f; break;
        default: break;                              // accepted, nothing to change (see the header)
    }
    g_styleStack[g_styleTop++] = s;
}
void PushStyleVar(StyleVar var, float value) { PushStyleVar(var, Vec2{value, value}); }
void PopStyleVar(int count) {
    while (count-- > 0 && g_styleTop > 0) {
        const StyleSave s = g_styleStack[--g_styleTop];
        switch ((StyleVar)s.var) {
            case StyleVar_ItemSpacing:   g_lay.spacingX = s.a; g_lay.spacingY = s.b; break;
            case StyleVar_WindowPadding: g_theme.windowPadding = s.a; g_childPadY = s.b; break;
            case StyleVar_FrameRounding:
            case StyleVar_ChildRounding: g_theme.rounding = s.a; break;
            case StyleVar_WindowRounding: g_theme.windowRounding = s.a; break;
            case StyleVar_BorderSize:    g_theme.borderPx = s.a; break;
            case StyleVar_TextRowHeight: g_textRowH = s.a; break;
            case StyleVar_ButtonTextAlign: g_btnAlignX = s.a; break;
            case StyleVar_FramePadding:  g_framePadY = s.a; g_framePadX = s.b; break;
            case StyleVar_ScrollbarSize: g_sbSize = s.a; break;
            case StyleVar_FramedWidgets: g_framedWidgets = s.a != 0.0f; break;
            case StyleVar_CellPadding:   g_cellPadX = s.a; g_cellPadY = s.b; break;
            case StyleVar_PopupPadding:  g_popupPadX = s.a; g_popupPadY = s.b; break;
            case StyleVar_PopupWidth:    g_popupW = s.a; break;
            case StyleVar_ItemInnerSpacing: g_innerX = s.a; break;
            default: break;
        }
    }
}

static bool Checkbox_raw(const char* label, bool* value) {
    if (!g_lay.active || !value) return false;
    const VisLabel vis(label);   // "##" and what follows only name the widget
    const float sz = boxSize(), h = std::max(rowH(), sz);
    const float w = sz + ((vis.b[0] || !g_framedWidgets) ? 8.0f + labelW(vis.b) : 0.0f);   // no label with StyleVar_FramedWidgets: just the box (Dear ImGui's: no gap after it either)
    float x, y; place(w, h, x, y);
    return Checkbox(hashLabel(label), x, y + (h - sz) * 0.5f, sz, vis.b, value);
}

static bool RadioButton_raw(const char* label, int* value, int option) {
    if (!g_lay.active || !value) return false;
    const VisLabel vis(label);
    const float sz = boxSize(), h = std::max(rowH(), sz);
    const float w = sz + ((vis.b[0] || !g_framedWidgets) ? 8.0f + labelW(vis.b) : 0.0f);
    float x, y; place(w, h, x, y);
    return RadioButton(hashLabel(label), x, y + (h - sz) * 0.5f, sz, vis.b, value, option);
}

static bool ToggleSwitch_raw(const char* label, bool* value) {
    if (!g_lay.active || !value) return false;
    const float h = rowH();
    const float sh = textH() + 4.0f, sw = sh * 1.9f;   // switch pill size (track ~1.9:1)
    const VisLabel vis(label);
    const float w = sw + 8.0f + labelW(vis.b);
    float x, y; place(w, h, x, y);
    return ToggleSwitch(hashLabel(label), x, y + (h - sh) * 0.5f, sw, sh, vis.b, value);
}

namespace {
// Ctrl+click on a drag or slider box turns it into a text box for typing a number (Enter or a click elsewhere
// commits, Escape cancels; a value that is not a finite number is dropped). 0 = not editing (draw the box as
// usual), 1 = editing (this call drew the text box), 2 = a typed value was committed (*out holds it, clamped).
int numberEdit(int id, float x, float y, float w, float h, bool inside, double cur, bool isInt, double minV, double maxV, double* out, bool needCtrl = true) {
    if (g_editId != id && inside && g_pressed && (g_in.ctrl || !needCtrl) && !g_in.blocked && g_disCount == 0) {
        g_editId = id;
        std::snprintf(g_editBuf, sizeof g_editBuf, isInt ? "%.0f" : "%.6g", cur);
        g_focusNextField = true; g_focusSelectAll = true;
    }
    if (g_editId != id) return 0;
    const bool entered = TextField(id ^ 0x3ed17, x, y, w, h, g_editBuf, (int)sizeof g_editBuf, InputText_EnterReturnsTrue);
    const bool lost = g_focus != (id ^ 0x3ed17);
    const bool clickedAway = g_pressed && !inside;
    if (!(entered || lost || clickedAway)) return 1;
    if (g_focus == (id ^ 0x3ed17)) g_focus = 0;
    g_editId = 0;
    if (g_in.escape) return 0;                                         // cancelled
    char* end = nullptr;
    const double v = std::strtod(g_editBuf, &end);
    if (end == g_editBuf || !std::isfinite(v)) return 0;               // not a number: keep the old value
    double r = isInt ? std::floor(v + 0.5) : v;
    if (minV < maxV) r = r < minV ? minV : (r > maxV ? maxV : r);
    *out = r;
    return 2;
}
} // namespace

namespace {
struct NextNumber { bool has = false; char fmt[64] = {0}; bool has2 = false; char fmt2[64] = {0}; bool noInput = false; bool noRound = true; bool logarithmic = false; };
NextNumber g_nextNum;
char g_numText[4][64] = {{0}, {0}, {0}, {0}};
int g_numLast = 0;
// Take (and clear) the pending format for one widget.
NextNumber takeNextNumber() { NextNumber n = g_nextNum; g_nextNum = NextNumber(); return n; }
void noteNumberText(int cell, const char* t) {
    if (cell < 0 || cell > 3) return;
    std::snprintf(g_numText[cell], sizeof g_numText[cell], "%s", t ? t : "");
    g_numLast = cell;
}
double roundDecimals(double v, int d) {
    if (d < 0 || d > 9 || !std::isfinite(v)) return v;
    double sc = 1.0; for (int i = 0; i < d; ++i) sc *= 10.0;
    return std::floor(v * sc + 0.5) / sc;
}
}

bool FormatNumber(char* out, int cap, const char* fmt, double value, bool isInt, int* decimals) {
    if (decimals) *decimals = -1;
    if (!out || cap <= 0) return false;
    out[0] = 0;
    std::string res;
    bool ok = fmt != nullptr, seen = false;
    int dec = -1;
    for (const char* p = fmt; ok && *p;) {
        if (*p != '%') { res += *p++; continue; }
        ++p;
        if (*p == '%') { res += '%'; ++p; continue; }
        if (seen) { ok = false; break; }
        std::string flags, width, prec; bool hasPrec = false;
        while (*p && std::strchr("-+ 0#", *p) && flags.size() < 5) flags += *p++;
        while (*p >= '0' && *p <= '9' && width.size() < 2) width += *p++;
        if (*p == '.') { hasPrec = true; ++p; while (*p >= '0' && *p <= '9' && prec.size() < 2) prec += *p++; }
        for (int k = 0; k < 2 && *p && std::strchr("hlLzjtq", *p); ++k) ++p;
        const char c = *p;
        if (!c || !std::strchr("diuxXfFeEgG", c)) { ok = false; break; }
        ++p; seen = true;
        std::string spec = "%" + flags + width + (hasPrec ? "." + prec : std::string());
        char piece[160];
        if (std::strchr("diuxX", c)) {
            const double r = std::isfinite(value) ? std::floor(value + 0.5) : 0.0;
            const long long iv = r > 9e18 ? (long long)9e18 : (r < -9e18 ? (long long)-9e18 : (long long)r);
            spec += "ll"; spec += c;
            if (c == 'd' || c == 'i') std::snprintf(piece, sizeof piece, spec.c_str(), iv);
            else std::snprintf(piece, sizeof piece, spec.c_str(), (unsigned long long)iv);
            dec = 0;
        } else {
            spec += c;
            std::snprintf(piece, sizeof piece, spec.c_str(), value);
            if (c == 'f' || c == 'F') dec = hasPrec ? (prec.empty() ? 0 : std::atoi(prec.c_str())) : 6;
        }
        res += piece;
    }
    if (!ok) { if (isInt) std::snprintf(out, (size_t)cap, "%d", std::isfinite(value) ? (int)std::floor(value + 0.5) : 0); else std::snprintf(out, (size_t)cap, "%.3f", value); if (decimals) *decimals = isInt ? 0 : 3; return false; }
    std::snprintf(out, (size_t)cap, "%s", res.c_str());
    if (decimals) *decimals = dec;
    return true;
}

void SetNextNumberFormat(const char* fmt, bool noTypedInput, bool noRoundToFormat, const char* fmtMax, bool logarithmic) {
    g_nextNum = NextNumber();
    g_nextNum.logarithmic = logarithmic;
    if (fmt) { g_nextNum.has = true; std::snprintf(g_nextNum.fmt, sizeof g_nextNum.fmt, "%s", fmt); }
    if (fmtMax) { g_nextNum.has2 = true; std::snprintf(g_nextNum.fmt2, sizeof g_nextNum.fmt2, "%s", fmtMax); }
    g_nextNum.noInput = noTypedInput; g_nextNum.noRound = noRoundToFormat;
}

const char* GetLastNumberText(int cell) {
    if (cell < 0) cell = g_numLast;
    if (cell < 0 || cell > 3) return "";
    return g_numText[cell];
}

static bool SliderFloat_raw(const char* label, float* value, float minV, float maxV, bool isInt = false) {
    if (!g_lay.active || !value) return false;
    const NextNumber nn = takeNextNumber();
    noteNumberText(0, "");
    const float h = rowH();
    const float lw = visW(label) > 0.0f ? visW(label) + 8.0f : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    float ctrlW = w - lw; if (ctrlW < 24.0f) ctrlW = 24.0f;
    int decs = -1;
    if (nn.has) { char probe[8]; FormatNumber(probe, sizeof probe, nn.fmt, 0.0, isInt, &decs); }
    if (!nn.noInput) {   // Ctrl+click: type a number
        double typed = 0.0;
        const int e = numberEdit(hashLabel(label), x, y, ctrlW, h, hit(x, y, ctrlW, h), (double)*value, false, (double)minV, (double)maxV, &typed);
        if (e != 0) {
            if (lw > 0.0f) drawText(x + ctrlW + 8.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, g_theme.textScale, g_theme.text);
            if (e == 2 && !nn.noRound) typed = roundDecimals(typed, decs);
            if (e == 2 && (float)typed != *value) { *value = (float)typed; return true; }
            return false;
        }
    }
    const float before = *value;
    bool ch;
    if (nn.logarithmic && minV * maxV > 0.0f && std::isfinite(minV) && std::isfinite(maxV) && *value * minV > 0.0f) {
        // Equal steps of the handle multiply the value by the same factor (the range must keep one sign and not touch 0).
        const double sgn = minV < 0.0f ? -1.0 : 1.0;   // work on the sizes, keep the sign
        const double lo = std::log(std::fabs((double)minV)), hi = std::log(std::fabs((double)maxV));
        const double lv = std::log(std::fabs((double)*value));
        float t = (float)((lv - lo) / (hi - lo));
        const float t0 = t;
        const bool moved = Slider(hashLabel(label), x, y, ctrlW, h, &t, 0.0f, 1.0f);
        ch = moved && t != t0;
        if (ch) {
            double nv = sgn * std::exp(lo + (double)t * (hi - lo));
            if (nv < std::fmin(minV, maxV)) nv = std::fmin(minV, maxV);
            if (nv > std::fmax(minV, maxV)) nv = std::fmax(minV, maxV);
            *value = (float)nv;
        }
    } else ch = Slider(hashLabel(label), x, y, ctrlW, h, value, minV, maxV);
    if (ch && !nn.noRound && nn.has) {   // the value follows the decimals the format shows
        double r = roundDecimals((double)*value, decs);
        if (r < minV) r = minV;
        if (r > maxV) r = maxV;
        *value = (float)r;
        ch = *value != before;
    }
    if (nn.has && nn.fmt[0]) {   // the value, centred on the slider
        char vb[64]; FormatNumber(vb, sizeof vb, nn.fmt, (double)*value, isInt);
        noteNumberText(0, vb);
        const float tw = labelW(vb);
        drawText(x + (ctrlW - tw) * 0.5f, y + (h - textH()) * 0.5f, vb, g_theme.textScale, g_theme.text);
    }
    if (lw > 0.0f) drawText(x + ctrlW + 8.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, g_theme.textScale, g_theme.text);
    return ch;
}

static bool SliderInt_raw(const char* label, int* value, int minV, int maxV) {
    if (!g_lay.active || !value) return false;
    float f = (float)*value;
    // Reuse the float slider, then snap to the nearest integer.
    bool ch = SliderFloat_raw(label, &f, (float)minV, (float)maxV, true);
    int iv = (int)(f + (f >= 0.0f ? 0.5f : -0.5f));
    if (iv < minV) iv = minV;
    if (iv > maxV) iv = maxV;
    if (iv != *value) { *value = iv; return true; }
    return ch && false;   // value unchanged after snapping
}

static bool VSliderFloat_raw(const char* label, float w, float h, float* value, float minV, float maxV) {
    if (!g_lay.active || !value) return false;
    const int id = hashLabel(label);
    if (w <= 0.0f) w = rowH();
    if (h <= 0.0f) h = rowH() * 3.0f;
    float x, y; place(w, h, x, y);
    const bool inside = hit(x, y, w, h);
    g_lastId = id; if (inside) g_hot = id;
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

static bool Knob_raw(const char* label, float* value, float minV, float maxV, float size) {
    if (!g_lay.active || !value) return false;
    const int id = hashLabel(label);
    const float d = size > 0.0f ? size : rowH() * 2.0f;
    const float lw = visW(label) > 0.0f ? visW(label) + 8.0f : 0.0f;
    float x, y; place(d + lw, d, x, y);
    const float cx = x + d * 0.5f, cy = y + d * 0.5f, rad = d * 0.5f - 2.0f;
    const bool inside = hit(x, y, d, d);
    g_lastId = id; if (inside) g_hot = id;
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
    if (lw > 0.0f) drawText(x + d + 8.0f, y + (d - textH()) * 0.5f, VisLabel(label).b, g_theme.textScale, g_theme.text);
    return changed;
}

void ProgressBar(float fraction, Vec2 size, const char* overlay) {
    if (!g_lay.active) return;
    const float h = (size.y > 0.0f && std::isfinite(size.y)) ? size.y : rowH();
    float w = (size.x > 0.0f && std::isfinite(size.x)) ? size.x : availW();
    float x, y; place(w, h, x, y);
    if (fraction == fraction && fraction < 0.0f) {   // indeterminate: a block sweeping back and forth
        ProgressBar(x, y, w, h, 0.0f);
        const double t = GetTime() * 0.8;
        const float tri = (float)(t - std::floor(t));                     // 0..1 sawtooth
        const float pos = tri < 0.5f ? tri * 2.0f : (1.0f - tri) * 2.0f;  // 0..1..0
        const float b = g_theme.borderPx, bw = std::max(8.0f, (w - 2.0f * b) * 0.25f);
        const float bx = x + b + pos * std::max(0.0f, w - 2.0f * b - bw);
        PushClipRect(x + b, y + b, std::max(1.0f, w - 2.0f * b), std::max(1.0f, h - 2.0f * b));
        rrect(bx, y + b, bw, std::max(1.0f, h - 2.0f * b), rnd(), g_theme.accent);
        PopClipRect();
    } else ProgressBar(x, y, w, h, fraction);
    if (overlay && *overlay) {
        const float tw = labelW(overlay);
        drawText(x + (w - tw) * 0.5f, y + (h - textH()) * 0.5f, overlay, g_theme.textScale, g_theme.text);
    }
}
void ProgressBar(float fraction, const char* overlay) { ProgressBar(fraction, Vec2{}, overlay); }

static bool plotFrame(const char* label, const float* values, int count,
                      float& scaleMin, float& scaleMax, float height,
                      float& px, float& py, float& pw, float& ph) {
    if (!g_lay.active || !values || count <= 0) return false;
    const float lw = visW(label) > 0.0f ? visW(label) + 8.0f : 0.0f;
    const float h = height > 0.0f ? height : rowH() * 2.4f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    pw = w - lw; if (pw < 24.0f) pw = 24.0f;
    px = x; py = y; ph = h;
    if (lw > 0.0f) drawText(x + pw + 8.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, g_theme.textScale, g_theme.text);
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
        line(x0, y0, x1, y1, 1.5f, pick(Col_PlotLines, g_theme.accent));
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
        quad(px + 2.0f + bw * i + 1.0f, py + ph - 2.0f - bh, bw - 2.0f, bh, pick(Col_PlotHistogram, g_theme.accent));
    }
}

void LabelText(const char* label, const char* value) {
    if (!g_lay.active) return;
    const float h = rowH(), w = availW();
    float x, y; place(w, h, x, y);
    if (value && *value) drawText(x, y + (h - textH()) * 0.5f, value, g_theme.textScale, g_theme.text);
    if (label && *label) {
        const float lw = visW(label);
        drawText(x + w - lw, y + (h - textH()) * 0.5f, VisLabel(label).b, g_theme.textScale, g_theme.text);
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
    const bool inside = hit(x, y, w + pad * 2, h + pad * 2);
    if (inside) g_hot = hid;
    bool clicked = false;
    if (g_active == hid) { if (g_released) { if (inside) clicked = true; g_active = 0; } }
    else if (inside && g_pressed) g_active = hid;
    const unsigned char* bg = (g_active == hid && inside) ? g_theme.bgDown : (inside ? g_theme.bgHover : g_theme.bg);
    quad(x, y, w + pad * 2, h + pad * 2, bg);
    DrawTexture(tex, x + pad, y + pad, w, h);
    return clicked;
}

static bool InputText_raw(const char* label, char* buf, int cap, unsigned flags) {
    if (!g_lay.active || !buf) return false;
    const VisLabel vis(label);
    const float h = rowH();
    const float lw = labelW(vis.b) > 0.0f ? labelW(vis.b) + 8.0f : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    float ctrlW = w - lw; if (ctrlW < 24.0f) ctrlW = 24.0f;
    const bool ch = TextField(hashLabel(label), x, y, ctrlW, h, buf, cap, flags);
    if (lw > 0.0f) drawText(x + ctrlW + 8.0f, y + (h - textH()) * 0.5f, vis.b, g_theme.textScale, g_theme.text);
    return ch;
}

static bool InputInt_raw(const char* label, int* value, int step) {
    if (!g_lay.active || !value) return false;
    const int id = hashLabel(label);
    const float h = rowH();
    const float lw = visW(label) > 0.0f ? visW(label) + 8.0f : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    if (g_framedWidgets) {   // Dear ImGui's look: a field in the frame colours with the value FramePadding.x in, then the two square
                             // steppers, StyleVar_ItemInnerSpacing (x, default 4) apart and before the label
        const VisLabel vis(label);   // "##" and what follows only name the widget
        const float gap = g_innerX >= 0.0f ? g_innerX : 4.0f, padX = g_framePadX >= 0.0f ? g_framePadX : 6.0f;
        const float lw2 = labelW(vis.b) > 0.0f ? labelW(vis.b) + gap : 0.0f;
        const float fieldW = std::max(24.0f, w - lw2 - 2.0f * (h + gap));
        const bool inField = hit(x, y, fieldW, h);
        bool ch = false;
        double typed = 0.0;
        const int ed = numberEdit(id ^ 0x1e, x, y, fieldW, h, inField, (double)*value, true, -2147483648.0, 2147483647.0, &typed, false);
        if (ed == 2) { const int nv = (int)typed; if (nv != *value) { *value = nv; ch = true; } }
        if (ed == 0) {
            rrect(x, y, fieldW, h, rnd(), inField ? pick(Col_FrameBgHovered, g_theme.bgHover) : pick(Col_FrameBg, g_theme.field));
            char buf[32]; std::snprintf(buf, sizeof(buf), "%d", *value);
            PushClipRect(x, y, fieldW, h);   // a narrow field cuts the number at its edge
            drawText(x + padX, y + (h - textH()) * 0.5f, buf, g_theme.textScale, g_theme.text);
            PopClipRect();
        }
        const float bx = x + fieldW + gap;
        if (Button(id ^ 0x2d, bx, y, h, h, "-"))              { *value -= step; ch = true; }
        if (Button(id ^ 0x2b, bx + h + gap, y, h, h, "+")) { *value += step; ch = true; }
        if (lw2 > 0.0f) drawText(bx + 2.0f * h + 2.0f * gap, y + (h - textH()) * 0.5f, vis.b, g_theme.textScale, g_theme.text);
        return ch;
    }
    const float btnW = h;                 // square [-]/[+] steppers
    float fieldW = w - lw - btnW * 2.0f - 8.0f;
    if (fieldW < 24.0f) fieldW = 24.0f;
    // Value box: a click on it types a number (Enter or clicking away applies it, Escape cancels).
    quad(x, y, fieldW, h, g_theme.track);
    bool ch = false;
    double typed = 0.0;
    const int ed = numberEdit(id ^ 0x1e, x, y, fieldW, h, hit(x, y, fieldW, h), (double)*value, true, -2147483648.0, 2147483647.0, &typed, false);
    if (ed == 2) { const int nv = (int)typed; if (nv != *value) { *value = nv; ch = true; } }
    if (ed == 0) { char buf[32]; std::snprintf(buf, sizeof(buf), "%d", *value); drawText(x + 6.0f, y + (h - textH()) * 0.5f, buf, g_theme.textScale, g_theme.text); }
    // Steppers (unique ids via the field id so they don't collide).
    const float bx = x + fieldW + 4.0f;
    if (Button(id ^ 0x2d, bx, y, btnW, h, "-"))              { *value -= step; ch = true; }
    if (Button(id ^ 0x2b, bx + btnW + 4.0f, y, btnW, h, "+")) { *value += step; ch = true; }
    if (lw > 0.0f) drawText(x + fieldW + btnW * 2.0f + 12.0f, y + (h - textH()) * 0.5f,
                            VisLabel(label).b, g_theme.textScale, g_theme.text);
    return ch;
}

bool CollapsingHeader(const char* label, bool defaultOpen);
bool CollapsingHeader(const char* label) { return CollapsingHeader(label, false); }
bool CollapsingHeader(const char* label, bool defaultOpen) {
    if (!g_lay.active) return false;
    const int id = hashLabel(label);
    bool* open = openState(id, defaultOpen);
    if (g_nextOpen >= 0) { *open = g_nextOpen != 0; g_nextOpen = -1; }
    const float h = rowH(), w = fullW();
    float x, y; place(w, h, x, y);
    const bool inside = hit(x, y, w, h);
    g_lastId = id; if (inside) g_hot = id;
    if (g_active == id) { if (g_released) { if (inside) { *open = !*open; g_toggledId = id; } g_active = 0; } }
    else if (inside && g_pressed) g_active = id;
    const unsigned char* bg = (g_active == id && inside) ? pick(Col_HeaderActive, g_theme.bgDown) : (inside ? pick(Col_HeaderHovered, g_theme.bgHover) : pick(Col_Header, g_theme.track));
    const float s = g_theme.textScale;
    if (g_look) {
        rrect(x, y, w, h, rnd(), inside ? pick(Col_HeaderHovered, g_theme.bgHover) : pick(Col_Header, g_theme.bg));
        chevron(x + 14.0f, y + h * 0.5f, 9.0f, *open, pick(Col_TextDisabled, g_theme.textDim));
        drawText(x + 28.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);
        return *open;
    }
    quad(x, y, w, h, bg);
    drawText(x + 8.0f, y + (h - textH()) * 0.5f, *open ? "-" : "+", s, g_theme.text);   // expander glyph
    drawText(x + 8.0f + fCharW() * s * 2.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);
    return *open;
}

namespace {
// A tooltip box next to the cursor: several lines ("\n"), kept inside the display (flipped to the left / above when it would run off).
// A tooltip is drawn at the end of the frame, on top of everything (as Dear ImGui's tooltip window is): drawn where it was asked for, a
// tooltip inside an open list was painted over by the list's later rows, and it switched the overlay off for them, so they went under
// the list (stage 9: the First Person Hands presets lost their last three rows while one was hovered). The last one asked for in a
// frame is the one shown, as Dear ImGui's SetTooltip replaces an earlier one. The text scale it was asked with is kept.
struct PendingTip { char text[1024]; float scale; bool on; };
PendingTip g_pendingTip = {{0}, 1.0f, false};
void paintTooltipBox(const char* text);
void drawTooltipBox(const char* text) {
    if (!text || !*text) return;
    std::snprintf(g_pendingTip.text, sizeof g_pendingTip.text, "%s", text);
    g_pendingTip.scale = g_theme.textScale;
    g_pendingTip.on = true;
}
void flushTooltip() {
    if (!g_pendingTip.on) return;
    g_pendingTip.on = false;
    const float was = g_theme.textScale;
    g_theme.textScale = g_pendingTip.scale;
    paintTooltipBox(g_pendingTip.text);
    g_theme.textScale = was;
}
void dropTooltip() { g_pendingTip.on = false; }
void paintTooltipBox(const char* text) {
    if (!text || !*text) return;
    const float s = g_theme.textScale, th = textH(), lineGap = 2.0f;
    int lines = 1; float tw = 0.0f;
    {   // measure: the widest line and the line count
        char line[256]; int n = 0;
        for (const char* c = text;; ++c) {
            if (*c == '\n' || *c == 0) {
                line[n] = 0; tw = std::max(tw, labelW(line));
                if (*c == 0) break;
                ++lines; n = 0;
            } else if (n < 255) line[n++] = *c;
        }
    }
    const float pad = g_look ? 8.0f : 6.0f;
    const float bw = tw + pad * 2.0f, bh = th * (float)lines + lineGap * (float)(lines - 1) + pad * 2.0f;
    float bx = g_in.mouseX + 14.0f, by = g_in.mouseY + 14.0f;
    if (g_in.displayW > 0.0f && bx + bw > g_in.displayW - 2.0f) bx = std::max(2.0f, g_in.mouseX - 10.0f - bw);   // flip to the left of the cursor
    if (g_in.displayH > 0.0f && by + bh > g_in.displayH - 2.0f) by = std::max(2.0f, g_in.mouseY - 10.0f - bh);   // and above it
    g_toOverlay = true;
    if (g_look) {
        const unsigned char tip[4] = {38, 42, 51, 250};
        rrect(bx, by, bw, bh, 7.0f, g_theme.border);
        rrect(bx + 1.0f, by + 1.0f, bw - 2.0f, bh - 2.0f, 6.0f, tip);
    } else {
        quad(bx, by, bw, bh, g_theme.border);
        quad(bx + 1.0f, by + 1.0f, bw - 2.0f, bh - 2.0f, g_theme.panel);
    }
    char line[256]; int n = 0; float ly = by + pad;
    for (const char* c = text;; ++c) {
        if (*c == '\n' || *c == 0) {
            line[n] = 0; drawText(bx + pad, ly, line, s, g_theme.text);
            ly += th + lineGap; n = 0;
            if (*c == 0) break;
        } else if (n < 255) line[n++] = *c;
    }
    g_toOverlay = false;
}
} // namespace

void Tooltip(const char* text) {
    if (!g_lay.active || !text || !*text || g_in.blocked) return;
    // Only when the most recent item is hovered.
    if (!pointIn(g_in.mouseX, g_in.mouseY, g_lay.prevX, g_lay.prevY, g_lay.prevW, g_lay.prevH)) return;
    drawTooltipBox(text);
}

void SetTooltip(const char* fmt, ...) {
    if (!g_lay.active || !fmt || g_in.blocked) return;
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    drawTooltipBox(buf);   // unconditional: the caller decides when (usually `if (IsItemHovered()) SetTooltip(...)`)
}

// ---- Tooltip scope: BeginTooltip / EndTooltip --------------------------------------------------------------------------
// The widgets between the two calls are laid out into the overlay batch at a provisional spot next to the cursor, with input
// switched off. EndTooltip measures what they covered (their group box), moves their vertices to the final spot (flipped to the
// left / above like SetTooltip when it would run off the display), draws the box and moves its indices in front of the content.
namespace {
struct TipScope {
    bool open = false;
    int nested = 0;                 // BeginTooltip calls made while one is open: they draw into the outer one
    LayoutState lay;                // the layout to restore
    int grpTop = 0, lastId = 0; bool blocked = false, posOverride = false, toOverlay = false, discard = false;
    float nextItemW = -1.0f;
    int startV = 0, startI = 0, spanBase = 0;
};
TipScope g_tip;
} // namespace

bool BeginTooltip() {
    if (g_tip.open) { ++g_tip.nested; return true; }
    if (!g_lay.active || g_grpTop >= 8) return false;
    TipScope& t = g_tip;
    t.open = true; t.nested = 0; t.lay = g_lay; t.grpTop = g_grpTop; t.lastId = g_lastId; t.blocked = g_in.blocked;
    t.posOverride = g_posOverride; t.toOverlay = g_toOverlay; t.nextItemW = g_nextItemW;
    t.discard = g_in.blocked;            // input is blocked (a window covers the pointer): the tooltip is not shown, its widgets still run
    t.spanBase = g_novSpans;
    g_toOverlay = true;
    bindTexture(g_defTex);               // the box (drawn last, placed first) is flat: a flat span at the start of the tooltip
    t.startV = g_onv; t.startI = g_oni;
    g_in.blocked = true; g_posOverride = false; g_nextItemW = -1.0f;
    const float pad = g_look ? 8.0f : 6.0f;
    const float maxW = g_in.displayW > 0.0f ? std::max(120.0f, std::min(480.0f, g_in.displayW - 40.0f)) : 480.0f;
    g_lay.winClip = false;
    g_lay.ox = g_in.mouseX + 14.0f + pad; g_lay.oy = g_in.mouseY + 14.0f + pad;
    g_lay.cx = g_lay.ox; g_lay.cy = g_lay.oy; g_lay.contentW = maxW; g_lay.indent = 0.0f;
    g_lay.bottom = 1e7f; g_lay.maxRight = 0.0f; g_lay.pendingSameLine = false;
    g_lay.prevX = g_lay.ox; g_lay.prevY = g_lay.oy; g_lay.prevW = 0.0f; g_lay.prevH = 0.0f;
    g_grp[g_grpTop++] = GroupBox();      // the tooltip's own bounding box
    return true;
}

void EndTooltip() {
    if (!g_tip.open) return;
    TipScope& t = g_tip;
    if (t.nested > 0) { --t.nested; return; }
    const GroupBox box = g_grp[--g_grpTop];
    const float pad = g_look ? 8.0f : 6.0f;
    if (!box.any || t.discard) {         // nothing to show: drop whatever was drawn
        g_onv = t.startV; g_oni = t.startI; g_novSpans = t.spanBase;
    } else {
        // A text row is rowH tall (the text plus padding of its own), so the vertical inset is what is left of `pad` after that
        // padding: a one-line tooltip then has the same box as SetTooltip.
        const float padY = std::max(0.0f, pad - (rowH() - textH()) * 0.5f);
        const float bw = (box.x1 - box.x0) + pad * 2.0f, bh = (box.y1 - box.y0) + padY * 2.0f;
        float bx = g_in.mouseX + 14.0f, by = g_in.mouseY + 14.0f;
        if (g_in.displayW > 0.0f && bx + bw > g_in.displayW - 2.0f) bx = std::max(2.0f, g_in.mouseX - 10.0f - bw);
        if (g_in.displayH > 0.0f && by + bh > g_in.displayH - 2.0f) by = std::max(2.0f, g_in.mouseY - 10.0f - bh);
        const float dx = bx + pad - box.x0, dy = by + padY - box.y0;
        for (int i = t.startV; i < g_onv; ++i) { g_ovVerts[i].position.x += dx; g_ovVerts[i].position.y += dy; }
        const int contentEndI = g_oni, spansBefore = g_novSpans;
        g_toOverlay = true;
        if (g_look) {
            const unsigned char tip[4] = {38, 42, 51, 250};
            rrect(bx, by, bw, bh, 7.0f, g_theme.border);
            rrect(bx + 1.0f, by + 1.0f, bw - 2.0f, bh - 2.0f, 6.0f, tip);
        } else {
            quad(bx, by, bw, bh, g_theme.border);
            quad(bx + 1.0f, by + 1.0f, bw - 2.0f, bh - 2.0f, g_theme.panel);
        }
        const int boxN = g_oni - contentEndI;
        g_novSpans = spansBefore;        // the box takes the flat span at the start of the tooltip
        if (boxN > 0 && contentEndI > t.startI) {
            std::rotate(g_ovIdx + t.startI, g_ovIdx + contentEndI, g_ovIdx + g_oni);   // box indices first, then the content's
            // The content's texture changes moved with it: shift them past the box, and keep a flat span at the start.
            for (int i = t.spanBase; i < g_novSpans; ++i) if (g_ovSpans[i].startIdx >= t.startI) g_ovSpans[i].startIdx += boxN;
            int first = t.spanBase; while (first < g_novSpans && g_ovSpans[first].startIdx < t.startI) ++first;
            // after the shift no span starts at startI any more (the flat one from BeginTooltip was shifted too): put a flat one back
            if (g_novSpans < 32) {
                for (int i = g_novSpans; i > first; --i) g_ovSpans[i] = g_ovSpans[i - 1];
                g_ovSpans[first] = OverlaySpan{t.startI, g_defTex}; ++g_novSpans;
            }
        }
    }
    g_lay = t.lay; g_grpTop = t.grpTop; g_lastId = t.lastId; g_in.blocked = t.blocked; g_posOverride = t.posOverride;
    g_nextItemW = t.nextItemW; g_toOverlay = t.toOverlay;
    { const bool keep = g_toOverlay; g_toOverlay = true; bindTexture(g_defTex); g_toOverlay = keep; }   // whatever the overlay draws next starts flat again
    t.open = false;
}

// ---- GetStyle: the style as Dear ImGui-shaped fields, read off OkayUI's own settings each call ----------------------------
StyleInfo& GetStyle() {
    static StyleInfo s;
    const float rh = rowH(), th = textH();
    s.Alpha = 1.0f;
    s.WindowPadding = Vec2{g_theme.windowPadding, g_theme.windowPadding};
    s.FramePadding = Vec2{8.0f, (rh - th) * 0.5f};
    s.ItemSpacing = Vec2{g_lay.spacingX, g_lay.spacingY};
    s.ItemInnerSpacing = Vec2{g_lay.spacingX * 0.5f, g_lay.spacingY};
    s.CellPadding = Vec2{6.0f, 3.0f};
    s.ScrollbarSize = kScrollbarW;
    s.WindowRounding = g_theme.windowRounding; s.ChildRounding = g_theme.rounding; s.FrameRounding = g_theme.rounding;
    s.WindowBorderSize = g_theme.borderPx; s.ChildBorderSize = g_theme.borderPx; s.FrameBorderSize = g_theme.borderPx;
    s.FontScale = g_theme.textScale;
    for (int i = 0; i < Col_COUNT && i < 64; ++i) s.Colors[i] = GetStyleColorVec4((Col)i);
    return s;
}

// ---- ListClipper: lay out only the rows of a long list that can be seen --------------------------------------------------
namespace {
// The vertical span, in screen pixels, that is on screen right now: the innermost clip rectangle (a child or a clipped window), else
// the current window, else the display; always cut to the display when the host gave one.
void visibleSpanY(float& top, float& bottom) {
    top = -1e30f; bottom = 1e30f;
    if (g_clipTop > 0) { const SDL_Rect& r = g_clipStack[g_clipTop - 1]; top = (float)r.y; bottom = (float)(r.y + r.h); }
    else if (g_curWin.valid) { top = g_curWin.y; bottom = g_curWin.y + g_curWin.h; }
    if (g_dispH > 0.0f) { top = std::max(top, 0.0f); bottom = std::min(bottom, g_dispH); }
}
} // namespace

ListClipper::ListClipper() {}
ListClipper::~ListClipper() { End(); }

void ListClipper::Begin(int itemsCount, float itemHeight) {
    End();
    ItemsCount = itemsCount > 0 ? itemsCount : 0;
    ItemsHeight = (itemHeight > 0.0f && std::isfinite(itemHeight)) ? itemHeight : -1.0f;   // the step from one row to the next, spacing included
    mStartY = g_lay.active ? g_lay.cy : 0.0f;
    mStage = 1; mForceMin = mForceMax = -1;
    DisplayStart = DisplayEnd = 0;
    mActive = true; mMeasured = false;
}

void ListClipper::ForceDisplayRangeByIndices(int itemMin, int itemMax) { mForceMin = itemMin; mForceMax = itemMax; }

bool ListClipper::Step() {
    if (!mActive) return false;
    if (!g_lay.active || ItemsCount == 0) { End(); return false; }
    if (mStage == 1) {
        if (ItemsHeight <= 0.0f) {       // row height unknown: show row 0 alone, measure it, then decide
            mStage = 2; DisplayStart = 0; DisplayEnd = 1; mMeasured = true;
            return true;
        }
        mStage = 3;                      // known: go straight to the visible range
    } else if (mStage == 2) {
        const float used = g_lay.cy - mStartY;
        ItemsHeight = used > 0.5f ? used : rowH() + g_lay.spacingY;
        mStage = 3;
    }
    if (mStage == 3) {
        float top, bottom; visibleSpanY(top, bottom);
        int first = (int)std::floor((top - mStartY) / ItemsHeight) - 1;
        int last = (int)std::ceil((bottom - mStartY) / ItemsHeight) + 1;
        if (mForceMin >= 0) { first = std::min(first, mForceMin); last = std::max(last, mForceMax + 1); }
        const int already = mMeasured ? 1 : 0;                       // row 0 is on screen already when it was measured
        if (first < already) first = already;
        if (last > ItemsCount) last = ItemsCount;
        mStage = 4;
        if (last <= first) { g_lay.cy = mStartY + (float)ItemsCount * ItemsHeight; g_lay.pendingSameLine = false; End(); return false; }
        g_lay.cy = mStartY + (float)first * ItemsHeight;             // skip the rows above: the cursor jumps to where row `first` belongs
        g_lay.pendingSameLine = false;
        DisplayStart = first; DisplayEnd = last;
        return true;
    }
    if (mStage == 4) {                   // the caller's loop is done: jump past the rows below so the content height is right
        g_lay.cy = mStartY + (float)ItemsCount * ItemsHeight;
        g_lay.pendingSameLine = false;
        End();
    }
    return false;
}

void ListClipper::End() { mActive = false; mStage = 0; mMeasured = false; }

void TextUnformatted(const char* text, const char* textEnd) {
    if (!g_lay.active || !text) return;
    if (!textEnd) { textBlock(text, g_theme.text); return; }
    const std::size_t n = textEnd > text ? (std::size_t)(textEnd - text) : 0;
    char buf[1024]; const std::size_t m = n < sizeof buf - 1 ? n : sizeof buf - 1;
    std::memcpy(buf, text, m); buf[m] = 0;
    textBlock(buf, g_theme.text);
}

void AlignTextToFramePadding() {}   // text in a row is already vertically centred with framed widgets

Vec2 GetItemRectMin() { const Rect r = GetItemRect(); return Vec2{ r.x, r.y }; }
Vec2 GetItemRectMax() { const Rect r = GetItemRect(); return Vec2{ r.x + r.w, r.y + r.h }; }
Vec2 GetItemRectSize() { const Rect r = GetItemRect(); return Vec2{ r.w, r.h }; }
double GetTime() { return g_clock; }
int GetFrameCount() { return (int)g_frameCount; }
bool IsKeyDown(KeyId key) { return key > 0 && key < KeyId_COUNT && g_keyNow[key]; }
bool IsKeyPressed(KeyId key, bool repeat) {
    if (!(key > 0 && key < KeyId_COUNT) || !g_keyNow[key]) return false;
    if (!g_keyPrev[key]) return true;
    if (repeat && g_clock >= g_keyNextRepeat[key]) { g_keyNextRepeat[key] = g_clock + 0.05; return true; }
    return false;
}
bool IsKeyReleased(KeyId key) { return key > 0 && key < KeyId_COUNT && !g_keyNow[key] && g_keyPrev[key]; }
bool IsMouseDoubleClicked(int button) { return button == 0 && !g_in.blocked && g_dblClick; }
bool IsMouseDragging(int button, float threshold) {
    if (g_in.blocked) return false;
    if (button < 0 || button > 2 || !IsMouseDown(button)) return false;
    const float dx = g_in.mouseX - g_dragOriginX[button], dy = g_in.mouseY - g_dragOriginY[button], t = threshold >= 0.0f ? threshold : 4.0f;
    return dx * dx + dy * dy > t * t;
}

namespace {
// A framed combo's box (StyleVar_FramedWidgets): a rounded field in Col_FrameBg, the text FramePadding.x in (clipped before the button),
// and at the right end a square button as tall as the field in Col_Button with a filled triangle pointing down in the text colour.
// The triangle's size and place (0.37 of the text height either side of the button's centre, its top edge 0.34 of the text height
// above the centre and its point 0.22 below) were measured from Dear ImGui's combo in the editor.
void framedComboBox(float x, float y, float w, float h, bool hovered, const char* text) {
    const float rd = std::min(rnd(), h * 0.5f);
    rrect(x, y, w, h, rd, hovered ? pick(Col_FrameBgHovered, g_theme.bgHover) : pick(Col_FrameBg, g_theme.field));
    const float bw = std::min(h, w), bx = x + w - bw;
    const unsigned char* bc = hovered ? pick(Col_ButtonHovered, g_theme.bgHover) : pick(Col_Button, g_theme.bg);
    rrect(bx, y, bw, h, rd, bc);
    if (rd > 0.0f) quad(bx, y, rd, h, bc);   // square on its left, where it meets the field
    const float padX = g_framePadX >= 0.0f ? g_framePadX : 6.0f;
    PushClipRect(x, y, std::max(1.0f, w - bw), h);   // to the arrow, as Dear ImGui clips it (the editor fits a long text with "...")
    drawText(x + padX, y + (h - textH()) * 0.5f, text ? text : "", g_theme.textScale, g_theme.text);
    PopClipRect();
    const float fs = textH(), cx = bx + bw * 0.5f, cy = y + h * 0.5f, r = fs * 0.37f;
    tri(cx - r, cy - fs * 0.34f, cx + r, cy - fs * 0.34f, cx, cy + fs * 0.22f, g_theme.text);
}
} // namespace

static bool Combo_raw(const char* label, const char* const* items, int count, int* current) {
    if (!g_lay.active || !items || !current || count <= 0) return false;
    const int id = hashLabel(label);
    bool* open = openState(id, false);
    const VisLabel vis(label);
    const float h = rowH(), s = g_theme.textScale, b = g_theme.borderPx;
    const float lw = labelW(vis.b) > 0.0f ? labelW(vis.b) + 8.0f : 0.0f;
    float w = availW();
    // an explicit width is the box's; the label is extra (with StyleVar_FramedWidgets the views pass the whole row's width, label included)
    if (lw > 0.0f && widthOverridden() && !g_framedWidgets) { const float room = realAvailW(); w = w + lw < room ? w + lw : room; }
    float x, y; place(w, h, x, y);
    float boxW = w - lw; if (boxW < 48.0f) boxW = 48.0f;

    const bool insideBox = hitOverlay(x, y, boxW, h) && inClip();
    g_lastId = id; if (insideBox) g_hot = id;
    if (g_active == id) { if (g_released) { if (insideBox) *open = !*open; g_active = 0; } }
    else if (insideBox && g_pressed) g_active = id;

    const char* cur = (*current >= 0 && *current < count) ? items[*current] : "";
    if (g_framedWidgets) framedComboBox(x, y, boxW, h, insideBox, cur);
    else {
    quad(x, y, boxW, h, g_theme.border);
    if (boxW > 2*b && h > 2*b) quad(x + b, y + b, boxW - 2*b, h - 2*b, insideBox ? g_theme.bgHover : g_theme.bg);
    PushClipRect(x, y, std::max(1.0f, boxW - 24.0f), h);   // a long item stops before the arrow (it ran over the arrow and the label)
    drawText(x + 6.0f, y + (h - textH()) * 0.5f, cur, s, g_theme.text);
    PopClipRect();
    if (g_look) chevron(x + boxW - 14.0f, y + h * 0.5f, 8.0f, true, pick(Col_TextDisabled, g_theme.textDim));
    else drawText(x + boxW - fCharW() * s - 6.0f, y + (h - textH()) * 0.5f, *open ? "-" : "+", s, g_theme.text);
    }
    if (lw > 0.0f) drawText(x + boxW + 8.0f, y + (h - textH()) * 0.5f, vis.b, s, g_theme.text);

    bool changed = false;
    if (*open) {
        const float listY = y + h;
        registerOverlay(x, listY, boxW, (float)count * h, true);   // takes the mouse from widgets below
        g_toOverlay = true;   // the dropdown list draws on top of later widgets
        for (int k = 0; k < count; ++k) {
            const float ry = listY + (float)k * h;
            const int iid = id ^ (int)(0x9e3779b9u * (unsigned)(k + 1));
            const bool ih = hitOverlay(x, ry, boxW, h);
            if (ih) g_hot = iid;
            if (g_active == iid) { if (g_released) { if (ih) { *current = k; changed = true; *open = false; } g_active = 0; } }
            else if (ih && g_pressed) g_active = iid;
            // Opaque rows: the theme's panel colour is slightly translucent, and widgets under the
            // list showed through it.
            const unsigned char rowBg[4] = { g_theme.panel[0], g_theme.panel[1], g_theme.panel[2], 255 };
            quad(x, ry, boxW, h, ih ? g_theme.bgHover : rowBg);
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
    g_lastId = id; if (inside) g_hot = id;
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

static bool DragFloat_raw(const char* label, float* v, float speed, float minV, float maxV) {
    if (!g_lay.active || !v) return false;
    const NextNumber nn = takeNextNumber();
    int decs = -1;
    if (nn.has) { char probe[8]; FormatNumber(probe, sizeof probe, nn.fmt, 0.0, false, &decs); }
    const int id = hashLabel(label);
    const VisLabel vis(label);
    const float h = rowH(), s = g_theme.textScale;
    const float lw = visW(label) > 0.0f ? visW(label) + 8.0f : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    float ctrlW = w - lw; if (ctrlW < 32.0f) ctrlW = 32.0f;
    const bool inside = hit(x, y, ctrlW, h);
    if (!nn.noInput) {   // Ctrl+click: type a number
        double typed = 0.0;
        const int e = numberEdit(id, x, y, ctrlW, h, inside, (double)*v, false, minV < maxV ? (double)minV : 0.0, minV < maxV ? (double)maxV : 0.0, &typed);
        if (e != 0) {
            if (lw > 0.0f) drawText(x + ctrlW + 8.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);
            if (e == 2 && !nn.noRound) typed = roundDecimals(typed, decs);
            if (e == 2 && (float)typed != *v) { *v = (float)typed; return true; }
            return false;
        }
    }
    float delta; dragBox(id, x, y, ctrlW, h, inside, delta);
    bool changed = false;
    if (delta != 0.0f) {
        float nv = *v + delta * speed;
        if (minV < maxV) nv = nv < minV ? minV : (nv > maxV ? maxV : nv);
        if (nv != *v) { *v = nv; changed = true; }
    }
    char buf[64]; FormatNumber(buf, sizeof(buf), nn.has ? nn.fmt : "%.3f", (double)*v, false);
    noteNumberText(0, buf);
    drawText(x + 6.0f, y + (h - textH()) * 0.5f, buf, s, g_theme.text);
    if (lw > 0.0f) drawText(x + ctrlW + 8.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);
    return changed;
}

static bool DragInt_raw(const char* label, int* v, float speed, int minV, int maxV) {
    if (!g_lay.active || !v) return false;
    const NextNumber nn = takeNextNumber();
    const int id = hashLabel(label);
    const VisLabel vis(label);
    const float h = rowH(), s = g_theme.textScale;
    const float lw = visW(label) > 0.0f ? visW(label) + 8.0f : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    float ctrlW = w - lw; if (ctrlW < 32.0f) ctrlW = 32.0f;
    const bool inside = hit(x, y, ctrlW, h);
    if (!nn.noInput) {   // Ctrl+click: type a number
        double typed = 0.0;
        const int e = numberEdit(id, x, y, ctrlW, h, inside, (double)*v, true, minV < maxV ? (double)minV : 0.0, minV < maxV ? (double)maxV : 0.0, &typed);
        if (e != 0) {
            if (lw > 0.0f) drawText(x + ctrlW + 8.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);
            if (e == 2 && (int)typed != *v) { *v = (int)typed; return true; }
            return false;
        }
    }
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
    char buf[64]; FormatNumber(buf, sizeof(buf), nn.has ? nn.fmt : "%d", (double)*v, true);
    noteNumberText(0, buf);
    drawText(x + 6.0f, y + (h - textH()) * 0.5f, buf, s, g_theme.text);
    if (lw > 0.0f) drawText(x + ctrlW + 8.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);
    return changed;
}

namespace {
// One row of `n` (1..4) drag cells side by side that share one label (the vector fields of an inspector:
// Position, Rotation, Scale). Every cell has its own id (the label's id plus the cell number), scrubs like
// DragFloat / DragInt, and the label is drawn once to the right of the last cell. `range` keeps cell 0 <= cell 1.
static bool dragCells_ed(const char* label, float* fv, int* iv, int n, float speed, float minV, float maxV, bool range);
bool dragCells(const char* label, float* fv, int* iv, int n, float speed, float minV, float maxV, bool range) { return noteEdit(dragCells_ed(label, fv, iv, n, speed, minV, maxV, range)); }
static bool dragCells_ed(const char* label, float* fv, int* iv, int n, float speed, float minV, float maxV, bool range) {
    if (!g_lay.active || (!fv && !iv) || n < 1 || n > 4) return false;
    const NextNumber nn = takeNextNumber();
    const int base = hashLabel(label);
    const VisLabel vis(label);
    const float h = rowH(), s = g_theme.textScale;
    const float lw = visW(label) > 0.0f ? visW(label) + 8.0f : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    const float gap = 4.0f;
    float ctrlW = w - lw; if (ctrlW < 32.0f * (float)n) ctrlW = 32.0f * (float)n;
    const float cw = (ctrlW - gap * (float)(n - 1)) / (float)n;
    bool changed = false;
    for (int i = 0; i < n; ++i) {
        const float cx = x + (float)i * (cw + gap);
        const int id = base + (i + 1) * 7919;
        const bool inside = hit(cx, y, cw, h);
        const char* cellFmt = (i == 1 && nn.has2) ? nn.fmt2 : (nn.has ? nn.fmt : nullptr);
        if (!nn.noInput) {   // Ctrl+click: type a number into this cell
            double typed = 0.0;
            const int e = numberEdit(id, cx, y, cw, h, inside, fv ? (double)fv[i] : (double)iv[i], fv == nullptr, minV < maxV ? (double)minV : 0.0, minV < maxV ? (double)maxV : 0.0, &typed);
            if (e == 2) {
                if (fv) {
                    float nv = (float)typed;
                    if (range && n == 2) { if (i == 0 && nv > fv[1]) nv = fv[1]; if (i == 1 && nv < fv[0]) nv = fv[0]; }
                    if (std::isfinite(nv) && nv != fv[i]) { fv[i] = nv; changed = true; }
                } else {
                    int nv = (int)typed;
                    if (range && n == 2) { if (i == 0 && nv > iv[1]) nv = iv[1]; if (i == 1 && nv < iv[0]) nv = iv[0]; }
                    if (nv != iv[i]) { iv[i] = nv; changed = true; }
                }
                continue;
            }
            if (e == 1) continue;
        }
        float delta; dragBox(id, cx, y, cw, h, inside, delta);
        if (delta != 0.0f && std::isfinite(delta) && std::isfinite(speed)) {
            if (fv) {
                float nv = fv[i] + delta * speed;
                if (minV < maxV) nv = nv < minV ? minV : (nv > maxV ? maxV : nv);
                if (range && n == 2) { if (i == 0 && nv > fv[1]) nv = fv[1]; if (i == 1 && nv < fv[0]) nv = fv[0]; }
                if (std::isfinite(nv) && nv != fv[i]) { fv[i] = nv; changed = true; }
            } else {
                g_dragAccum += delta * speed;
                const int step = (int)g_dragAccum;
                if (step != 0) {
                    g_dragAccum -= (float)step;
                    int nv = iv[i] + step;
                    if (minV < maxV) { const int lo = (int)minV, hi = (int)maxV; nv = nv < lo ? lo : (nv > hi ? hi : nv); }
                    if (range && n == 2) { if (i == 0 && nv > iv[1]) nv = iv[1]; if (i == 1 && nv < iv[0]) nv = iv[0]; }
                    if (nv != iv[i]) { iv[i] = nv; changed = true; }
                }
            }
        }
        char buf[64];
        if (cellFmt) FormatNumber(buf, sizeof(buf), cellFmt, fv ? (double)fv[i] : (double)iv[i], fv == nullptr);
        else if (fv) std::snprintf(buf, sizeof(buf), cw < 64.0f ? "%.1f" : "%.3f", (double)fv[i]);
        else std::snprintf(buf, sizeof(buf), "%d", iv[i]);
        noteNumberText(i, buf);
        drawText(cx + 6.0f, y + (h - textH()) * 0.5f, buf, s, g_theme.text);
    }
    if (lw > 0.0f) drawText(x + ctrlW + 8.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);
    return changed;
}
} // namespace

static bool DragFloat2_raw(const char* label, float v[2], float speed, float minV, float maxV) { return dragCells(label, v, nullptr, 2, speed, minV, maxV, false); }
static bool DragFloat3_raw(const char* label, float v[3], float speed, float minV, float maxV) { return dragCells(label, v, nullptr, 3, speed, minV, maxV, false); }
static bool DragFloat4_raw(const char* label, float v[4], float speed, float minV, float maxV) { return dragCells(label, v, nullptr, 4, speed, minV, maxV, false); }
static bool DragInt2_raw(const char* label, int v[2], float speed, int minV, int maxV) { return dragCells(label, nullptr, v, 2, speed, (float)minV, (float)maxV, false); }
static bool DragInt3_raw(const char* label, int v[3], float speed, int minV, int maxV) { return dragCells(label, nullptr, v, 3, speed, (float)minV, (float)maxV, false); }
static bool DragInt4_raw(const char* label, int v[4], float speed, int minV, int maxV) { return dragCells(label, nullptr, v, 4, speed, (float)minV, (float)maxV, false); }
static bool DragFloatRange2_raw(const char* label, float* lo, float* hi, float speed, float minV, float maxV) {
    if (!lo || !hi) return false;
    float pair[2] = {*lo, *hi};
    const bool changed = dragCells(label, pair, nullptr, 2, speed, minV, maxV, true);
    if (changed) { *lo = pair[0]; *hi = pair[1]; }
    return changed;
}

static bool DragIntRange2_raw(const char* label, int* lo, int* hi, float speed, int minV, int maxV) {
    if (!lo || !hi) return false;
    int pair[2] = {*lo, *hi};
    const bool changed = dragCells(label, nullptr, pair, 2, speed, (float)minV, (float)maxV, true);
    if (changed) { *lo = pair[0]; *hi = pair[1]; }
    return changed;
}

static bool InputTextWithHint_raw(const char* label, const char* hint, char* buf, int cap, unsigned flags) {
    g_hintText = hint;
    const bool ch = InputText_raw(label, buf, cap, flags);
    g_hintText = nullptr;
    return ch;
}

void SetKeyboardFocusHere() { if (g_lay.active) g_focusNextField = true; }
void CenterNextOnLine() { g_centerNext = true; }

namespace {
// `n` (2..4) slider cells in one row that share a label (see dragCells).
static bool sliderCells_ed(const char* label, float* v, int n, float minV, float maxV);
bool sliderCells(const char* label, float* v, int n, float minV, float maxV) { return noteEdit(sliderCells_ed(label, v, n, minV, maxV)); }
static bool sliderCells_ed(const char* label, float* v, int n, float minV, float maxV) {
    if (!g_lay.active || !v || n < 2 || n > 4) return false;
    const int base = hashLabel(label);
    const float h = rowH();
    const float lw = visW(label) > 0.0f ? visW(label) + 8.0f : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    const float gap = 4.0f;
    float ctrlW = w - lw; if (ctrlW < 24.0f * (float)n) ctrlW = 24.0f * (float)n;
    const float cw = (ctrlW - gap * (float)(n - 1)) / (float)n;
    bool changed = false;
    for (int i = 0; i < n; ++i) {
        const float cx = x + (float)i * (cw + gap);
        const int cid = base + (i + 1) * 7919;
        double typed = 0.0;
        const int e = numberEdit(cid, cx, y, cw, h, hit(cx, y, cw, h), (double)v[i], false, (double)minV, (double)maxV, &typed);
        if (e == 2) { if ((float)typed != v[i]) { v[i] = (float)typed; changed = true; } continue; }
        if (e == 1) continue;
        if (Slider(cid, cx, y, cw, h, &v[i], minV, maxV)) changed = true;
    }
    if (lw > 0.0f) drawText(x + ctrlW + 8.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, g_theme.textScale, g_theme.text);
    return changed;
}
} // namespace

static bool SliderFloat2_raw(const char* label, float v[2], float minV, float maxV) { return sliderCells(label, v, 2, minV, maxV); }
static bool SliderFloat3_raw(const char* label, float v[3], float minV, float maxV) { return sliderCells(label, v, 3, minV, maxV); }
static bool SliderFloat4_raw(const char* label, float v[4], float minV, float maxV) { return sliderCells(label, v, 4, minV, maxV); }

static bool InputFloat_raw(const char* label, float* value, float step) {
    if (!g_lay.active || !value) return false;
    const NextNumber nn = takeNextNumber();
    const int id = hashLabel(label);
    const float h = rowH();
    const float lw = visW(label) > 0.0f ? visW(label) + 8.0f : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    const float btnW = h;
    float fieldW = w - lw - btnW * 2.0f - 8.0f;
    if (fieldW < 24.0f) fieldW = 24.0f;
    quad(x, y, fieldW, h, g_theme.track);
    bool ch = false;
    double typed = 0.0;
    const int ed = numberEdit(id ^ 0x1e, x, y, fieldW, h, hit(x, y, fieldW, h), (double)*value, false, 0.0, 0.0, &typed, false);
    if (ed == 2 && std::isfinite((float)typed) && (float)typed != *value) { *value = (float)typed; ch = true; }
    if (ed == 0) { char buf[64]; FormatNumber(buf, sizeof(buf), nn.has ? nn.fmt : "%.3f", (double)*value, false); noteNumberText(0, buf); drawText(x + 6.0f, y + (h - textH()) * 0.5f, buf, g_theme.textScale, g_theme.text); }
    const float bx = x + fieldW + 4.0f;
    const bool okStep = std::isfinite(step);
    if (Button(id ^ 0x2d, bx, y, btnW, h, "-") && okStep && std::isfinite(*value - step))              { *value -= step; ch = true; }
    if (Button(id ^ 0x2b, bx + btnW + 4.0f, y, btnW, h, "+") && okStep && std::isfinite(*value + step)) { *value += step; ch = true; }
    if (lw > 0.0f) drawText(x + fieldW + btnW * 2.0f + 12.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, g_theme.textScale, g_theme.text);
    return ch;
}

bool ArrowButton(const char* id, int dir) {
    if (!g_lay.active || !id) return false;
    const float h = rowH();
    float x, y; place(h, h, x, y);
    const bool clicked = Button(hashLabel(id), x, y, h, h, "");
    // a filled triangle, its bounding box centred on the button (0 left, 1 right, 2 up, 3 down; anything else is left)
    const float cx = x + h * 0.5f, cy = y + h * 0.5f, r = h * 0.22f;
    const bool hot = hit(x, y, h, h);
    const unsigned char* col = hot ? g_theme.text : g_theme.textDim;
    switch ((unsigned)dir < 4u ? dir : 0) {
        case 0: tri(cx - r, cy, cx + r, cy - r * 1.2f, cx + r, cy + r * 1.2f, col); break;
        case 1: tri(cx + r, cy, cx - r, cy - r * 1.2f, cx - r, cy + r * 1.2f, col); break;
        case 2: tri(cx, cy - r, cx - r * 1.2f, cy + r, cx + r * 1.2f, cy + r, col); break;
        default: tri(cx, cy + r, cx - r * 1.2f, cy - r, cx + r * 1.2f, cy - r, col); break;
    }
    return clicked;
}

// A checkerboard behind a translucent colour (the pattern tells "see-through" from "dark").
static void checkerboard(float x, float y, float w, float h) {
    if (w <= 0.0f || h <= 0.0f) return;
    const unsigned char light[4] = {204, 204, 204, 255}, dark[4] = {153, 153, 153, 255};
    quad(x, y, w, h, light);
    const float cell = std::max(4.0f, std::floor(h / 4.0f));
    for (float cy = 0.0f; cy < h; cy += cell) {
        const int row = (int)(cy / cell);
        for (float cx = (row & 1) ? 0.0f : cell; cx < w; cx += 2.0f * cell)
            quad(x + cx, y + cy, std::min(cell, w - cx), std::min(cell, h - cy), dark);
    }
}
// The swatch picture: border, then the colour over a checkerboard (whole swatch, or alpha on the left half and opaque on the right).
static void drawSwatch(float x, float y, float w, float h, const float rgba[4], int flags) {
    const float b = g_theme.borderPx;
    quad(x, y, w, h, g_theme.border);
    if (w <= 2.0f * b || h <= 2.0f * b) return;
    const float ix = x + b, iy = y + b, iw = w - 2.0f * b, ih = h - 2.0f * b;
    const bool alpha = (flags & ColorEdit_NoAlpha) == 0;
    auto bt = [](float f) { return (unsigned char)(clamp01(f) * 255.0f + 0.5f); };
    const unsigned char opaque[4] = {bt(rgba[0]), bt(rgba[1]), bt(rgba[2]), 255};
    const unsigned char withA[4] = {opaque[0], opaque[1], opaque[2], alpha ? bt(rgba[3]) : (unsigned char)255};
    if (withA[3] < 255) checkerboard(ix, iy, iw, ih);
    if (alpha && (flags & ColorEdit_AlphaPreviewHalf)) {
        quad(ix, iy, iw * 0.5f, ih, withA);
        quad(ix + iw * 0.5f, iy, iw * 0.5f, ih, opaque);
    } else quad(ix, iy, iw, ih, withA);
}

bool ColorButton(const char* id, const float rgba[4], int flags, float size) {
    if (!g_lay.active || !id || !rgba) return false;
    const float h = size > 0.0f && std::isfinite(size) ? size : rowH();
    float x, y; place(h, h, x, y);
    const bool clicked = Button(hashLabel(id), x, y, h, h, "");
    drawSwatch(x, y, h, h, rgba, flags);
    return clicked;
}
bool ColorButton(const char* id, const float rgba[4], float size) { return ColorButton(id, rgba, 0, size); }

static bool beginPopupBody(int id);
// ColorEdit3 / ColorEdit4: one row of swatch, channel sliders (or a hex field) and label; a click on the swatch opens a picker popup.
static bool colorEditRow_ed(const char* label, float* c, int n, int flags);
static bool colorEditRow(const char* label, float* c, int n, int flags) { return noteEdit(colorEditRow_ed(label, c, n, flags)); }
static bool colorEditRow_ed(const char* label, float* c, int n, int flags) {
    if (!g_lay.active || !c) return false;
    if (n == 3 || (flags & ColorEdit_NoAlpha)) { n = 3; flags |= ColorEdit_NoAlpha; }
    const int id = hashLabel(label);
    const float h = rowH(), s = g_theme.textScale;
    const bool showLabel = !(flags & ColorEdit_NoLabel) && visW(label) > 0.0f;
    const float lw = showLabel ? visW(label) + 8.0f : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    const float row = w - lw, swatch = h;
    float rgba[4] = {c[0], c[1], c[2], n == 4 ? c[3] : 1.0f};
    const bool swatchClicked = Button(id ^ 0x5a5a, x, y, swatch, h, "");
    drawSwatch(x, y, swatch, h, rgba, flags);
    bool changed = false;
    if (!(flags & ColorEdit_NoInputs)) {
        const float ix = x + swatch + 6.0f;
        float slidersW = row - swatch - 6.0f; if (slidersW < 40.0f) slidersW = 40.0f;
        if (flags & ColorEdit_Hex) {
            static char hexBuf[kMaxHexSlots][16]; static int hexId[kMaxHexSlots]; static int hexNext = 0;
            int slot = -1;
            for (int i = 0; i < kMaxHexSlots; ++i) if (hexId[i] == id) slot = i;
            if (slot < 0) { slot = hexNext; hexNext = (hexNext + 1) % kMaxHexSlots; hexId[slot] = id; hexBuf[slot][0] = 0; }
            char* buf = hexBuf[slot];
            const int fid = id ^ 0x7e5;
            const bool editing = g_focus == fid;
            if (!editing) ColorToHex(rgba, (flags & ColorEdit_NoAlpha) == 0, buf, 16);
            if (TextField(fid, ix, y, slidersW, h, buf, 16) && ColorFromHex(buf, rgba)) { for (int i = 0; i < n; ++i) c[i] = rgba[i]; changed = true; }
        } else {
            const float each = (slidersW - (n == 4 ? 12.0f : 8.0f)) / (float)n;
            for (int ch = 0; ch < n; ++ch) {
                const int cid = id ^ (int)(0x9e3779b9u * (unsigned)(ch + 1));
                if (Slider(cid, ix + (float)ch * (each + 4.0f), y, each, h, &c[ch], 0.0f, 1.0f)) changed = true;
            }
        }
    }
    if (showLabel) drawText(x + row + 8.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);
    // the picker popup, anchored under the swatch
    if (!(flags & ColorEdit_NoPicker)) {
        const int pkey = id ^ 0x7c1c;
        if (swatchClicked) {
            PopupSlot* p = popupSlot(pkey, true);
            if (p) { p->open = true; p->x = x; p->y = y + h + 2.0f; p->w = std::max(240.0f, std::min(300.0f, w)); p->modal = false; g_openPopupThisFrame = pkey; }
        }
        if (beginPopupBody(pkey)) {
            float tmp[4] = {c[0], c[1], c[2], n == 4 ? c[3] : 1.0f};
            if (ColorPicker4("##picker", tmp, flags | ColorEdit_NoLabel)) { for (int i = 0; i < n; ++i) c[i] = tmp[i]; changed = true; }
            EndPopup();
        }
    }
    return changed;
}
static bool ColorEdit4_raw(const char* label, float rgba[4], int flags) { return colorEditRow(label, rgba, 4, flags); }
static bool ColorEdit3_raw(const char* label, float rgb[3], int flags) { return colorEditRow(label, rgb, 3, flags); }

void NewLine() { if (g_lay.active) g_lay.pendingSameLine = false; }

bool TreeNodeEx(const char* label, unsigned flags) {
    if (!g_lay.active) return false;
    const int id = hashLabel(label);
    const bool leaf = (flags & TreeNode_Leaf) != 0;
    bool* open = openState(id, (flags & TreeNode_DefaultOpen) != 0);
    if (g_nextOpen >= 0) { *open = g_nextOpen != 0; g_nextOpen = -1; }
    const float h = rowH(), s = g_theme.textScale;
    const float ax = leftEdge();                                  // where the arrow sits: at the indent
    float x, y; place(fullW(), h, x, y);
    float w = fullW();
    if (flags & TreeNode_SpanFullWidth) { x = g_lay.ox; w = g_lay.contentW; g_lay.prevX = x; g_lay.prevW = w; }
    const bool inside = hit(x, y, w, h);
    g_lastId = id; if (inside) g_hot = id;
    const float arrowW = std::max(24.0f, h);
    const bool onArrowOnly = (flags & TreeNode_OpenOnArrow) != 0, onDbl = (flags & TreeNode_OpenOnDoubleClick) != 0;
    bool doToggle = false;
    if (g_active == id) {
        if (g_released) {
            if (inside && !leaf) {
                const bool arrow = g_pressX < ax + arrowW;          // where the press went down
                if (onDbl) { if (onArrowOnly && arrow) doToggle = true; }   // double click opens it elsewhere; with OpenOnArrow the arrow also takes one click
                else if (!onArrowOnly || arrow) doToggle = true;
            }
            g_active = 0;
        }
    } else if (inside && g_pressed) {
        g_active = id;
        if (!leaf && onDbl && g_dblClick) doToggle = true;
    }
    if (doToggle) { *open = !*open; g_toggledId = id; }
    const bool shown = leaf || *open;
    if ((flags & TreeNode_Framed) != 0) quad(x, y, w, h, pick((g_active == id && inside) ? Col_HeaderActive : (inside ? Col_HeaderHovered : Col_Header), g_theme.track));
    if ((flags & TreeNode_Selected) != 0) {
        unsigned char sel[4] = { g_theme.accent[0], g_theme.accent[1], g_theme.accent[2], 90 };
        quad(x, y, w, h, sel);
    }
    const bool bullet = (flags & TreeNode_Bullet) != 0;
    if (g_look) {
        unsigned char tmp[4]; const unsigned char white[4] = {255, 255, 255, 255};
        if (inside && !(flags & TreeNode_Framed)) rrect(x, y, w, h, 6.0f, pick(Col_HeaderHovered, withAlpha(white, 0.06f, tmp)));
        if (bullet) fillCircle(ax + 11.0f, y + h * 0.5f, 3.0f, g_theme.text);
        else if (!leaf) chevron(ax + 11.0f, y + h * 0.5f, 8.0f, *open, pick(Col_TextDisabled, g_theme.textDim));
        drawText(ax + 24.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);
    } else {
        if (inside && !(flags & TreeNode_Framed)) quad(x, y, w, h, pick(Col_HeaderHovered, g_theme.bgHover));
        if (bullet) fillCircle(ax + 4.0f + fCharW() * s * 0.5f, y + h * 0.5f, 3.0f, g_theme.text);
        else if (!leaf) drawText(ax + 4.0f, y + (h - textH()) * 0.5f, *open ? "v" : ">", s, g_theme.text);
        drawText(ax + 4.0f + fCharW() * s * 1.5f, y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);
    }
    if (shown && !(flags & TreeNode_NoTreePushOnOpen)) g_lay.indent += 16.0f;   // children indent until TreePop()
    return shown;
}
bool TreeNode(const char* label) { return TreeNodeEx(label, 0); }

void SetNextItemOpen(bool open) { g_nextOpen = open ? 1 : 0; }
bool IsItemToggledOpen() { return g_toggledId != 0 && g_toggledId == g_lastId; }

void TreePop() {
    if (!g_lay.active) return;
    g_lay.indent -= 16.0f;
    if (g_lay.indent < 0.0f) g_lay.indent = 0.0f;
}

bool BeginMenuBar() {
    if (!g_lay.active) return false;
    const float h = rowH();
    float x, y; place(fullW(), h, x, y);
    if (!g_look || colorIsSet(Col_MenuBarBg)) quad(x, y, fullW(), h, pick(Col_MenuBarBg, g_theme.bgDown));
    if (g_look) quad(x, y + h - 1.0f, fullW(), 1.0f, g_theme.border);
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
            quad(x, y, w, h, (ih || *open || ksel) ? pick(Col_HeaderHovered, g_theme.bgHover) : pick(Col_PopupBg, g_theme.panel));
            drawText(x + g_curMenu.textInset, ty, label, s, g_theme.text);
            if (g_curMenu.fromPopup) {   // Dear ImGui's arrow, as on the popup's own sub-menu rows
                const float th = textH(), r = th * 0.40f, cx = x + w - g_curMenu.textInset - th * 0.5f, cy = ty + th * 0.5f;
                tri(cx + 0.750f * r, cy, cx - 0.750f * r, cy + 0.866f * r, cx - 0.750f * r, cy - 0.866f * r, g_theme.text);
            } else drawText(x + w - labelW(">") - 8.0f, ty, ">", s, pick(Col_TextDisabled, g_theme.textDim));
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
    if (*open || inside) quad(x, y, bw, h, g_theme.bgHover);
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
    if (ih || lit) quad(x, y, w, h, pick(Col_HeaderHovered, g_theme.bgHover));
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
        const float r = th * 0.40f, cx = right - th * 0.5f, cy = iy + th * 0.5f;
        tri(cx + 0.750f * r, cy, cx - 0.750f * r, cy + 0.866f * r, cx - 0.750f * r, cy - 0.866f * r, col);
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
    quad(x, y, w, h, (ih || ksel) ? pick(Col_HeaderHovered, g_theme.bgHover) : pick(Col_PopupBg, g_theme.panel));
    if (selected) quad(x + 3.0f, y + (h - 6.0f) * 0.5f, 6.0f, 6.0f, col);   // a small filled square marks a checked item
    drawText(x + in, y + (h - textH()) * 0.5f, VisLabel(label).b, s, col);
    if (shortcut && shortcut[0]) {   // right-aligned, dim; skipped when it would run into the label
        const float sw = labelW(shortcut), sx = x + w - sw - in;
        if (sx > x + in + visW(label) + 12.0f) drawText(sx, y + (h - textH()) * 0.5f, shortcut, s, pick(Col_TextDisabled, g_theme.textDim));
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
    quad(x, y, w, h, pick(Col_PopupBg, g_theme.panel));
    quad(x + 6.0f, y + h * 0.5f, w - 12.0f, 1.0f, pick(Col_Separator, g_theme.border));
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
    } else if (g_look) {   // launcher rows: soft accent tint + bar when selected, faint white on hover
        unsigned char tmp[4];
        const unsigned char white[4] = {255, 255, 255, 255};
        if (selected) { rrect(x, y, w, h, 6.0f, withAlpha(g_theme.accent, 0.28f, tmp)); rrect(x, y + 5.0f, 3.0f, h - 10.0f, 1.5f, g_theme.accent); }
        else if (inside) rrect(x, y, w, h, 6.0f, pick(Col_HeaderHovered, withAlpha(white, 0.06f, tmp)));
        drawText(textX + 10.0f, ty, VisLabel(label).b, s, tcol);
    } else {
        if (selected)      quad(x, y, w, h, pick(Col_HeaderActive, g_theme.accent));
        else if (inside)   quad(x, y, w, h, pick(Col_HeaderHovered, g_theme.bgHover));
        drawText(textX + 6.0f, ty, VisLabel(label).b, s, tcol);
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
static bool beginPopupBody(int id) {
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
    panelColored(px, py, w, *hSlot + padY * 2.0f, pick(Col_PopupBg, pick(Col_WindowBg, g_theme.panel)));
    // Save parent layout; set up the popup's inner layout.
    PopupCtx& c = g_popupStack[g_popupTop++];
    c.lay = g_lay; c.id = id; c.x = px; c.y = py; c.w = w; c.hSlot = hSlot; c.wSlot = wSlot;
    c.padX = padX; c.padY = padY; c.exact = g_popupPadY >= 0.0f;
    c.subX0 = c.subY0 = c.subX1 = c.subY1 = 0.0f;
    g_lay.active = true;
    g_lay.ox = px + padX; g_lay.oy = py + padY;
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
    else {
    quad(x, y, boxW, h, g_theme.border);
    if (boxW > 2 * b && h > 2 * b) quad(x + b, y + b, boxW - 2 * b, h - 2 * b, insideBox ? g_theme.bgHover : g_theme.bg);
    PushClipRect(x, y, std::max(1.0f, boxW - 24.0f), h);
    drawText(x + 6.0f, y + (h - textH()) * 0.5f, preview ? preview : "", s, g_theme.text);
    PopClipRect();
    if (g_look) chevron(x + boxW - 14.0f, y + h * 0.5f, 8.0f, true, pick(Col_TextDisabled, g_theme.textDim));
    else drawText(x + boxW - fCharW() * s - 6.0f, y + (h - textH()) * 0.5f, open ? "-" : "+", s, g_theme.text);
    }
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

bool IsDragging() { return g_dd.active || g_tdd.active; }

bool DragSource(int payload) {
    if (!g_lay.active) return false;
    const float px = g_lay.prevX, py = g_lay.prevY, pw = g_lay.prevW, ph = g_lay.prevH;
    const bool overPrev = hit(px, py, pw, ph);
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

bool BeginDragDropSource(int flags) {
    (void)flags;
    if (!g_lay.active) return false;
    const float px = g_lay.prevX, py = g_lay.prevY, pw = g_lay.prevW, ph = g_lay.prevH;
    // Hash the float bits (a NaN, infinity or 1e30 position must not go through a float-to-int cast: undefined).
    auto bitsOf = [](float f) { std::uint32_t u; std::memcpy(&u, &f, sizeof u); return u; };
    const int sid = (int)((bitsOf(px) * 73856093u) ^ (bitsOf(py) * 19349663u) ^ (unsigned)g_lay.seed) | 1;
    if (!g_tdd.active && !g_tdd.armed && g_pressed && hit(px, py, pw, ph)) {
        g_tdd.armed = true; g_tdd.srcId = sid; g_tdd.armX = g_in.mouseX; g_tdd.armY = g_in.mouseY;
    }
    if (g_tdd.armed && g_tdd.srcId == sid && g_in.mouseDown) {
        const float dx = g_in.mouseX - g_tdd.armX, dy = g_in.mouseY - g_tdd.armY;
        if ((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) > 6.0f) { g_tdd.active = true; g_tdd.armed = false; g_tdd.hasPayload = false; g_tdd.pay = DragPayload(); }
    }
    return g_tdd.active && g_tdd.srcId == sid;
}
bool SetDragDropPayload(const char* type, const void* data, int size) {
    if (!g_tdd.active || !type || !*type || size < 0 || size > kDragPayloadMax || (size > 0 && !data)) return false;
    std::snprintf(g_tdd.pay.type, sizeof g_tdd.pay.type, "%s", type);
    if (size > 0) std::memcpy(g_tdd.pay.data, data, (std::size_t)size);
    g_tdd.pay.size = size; g_tdd.hasPayload = true;
    return true;
}
void SetDragDropPreview(const char* text) { if (g_tdd.active) std::snprintf(g_tdd.preview, sizeof g_tdd.preview, "%s", text ? text : ""); }
void EndDragDropSource() {}

static bool g_tddOver = false;   // BeginDragDropTarget said yes for the item being asked about
bool BeginDragDropTarget() { return BeginDragDropTarget(0); }
bool BeginDragDropTarget(int flags) {
    g_tddOver = false;
    if (!g_lay.active || !g_tdd.active || !g_tdd.hasPayload) return false;
    const float px = g_lay.prevX, py = g_lay.prevY, pw = g_lay.prevW, ph = g_lay.prevH;
    if (g_tddExt) {   // the other toolkit owns the mouse (Input::blocked): the pointer's position, the clip and the windows still count
        if (g_disCount > 0 || !pointIn(g_in.mouseX, g_in.mouseY, px, py, pw, ph) || !inClip()) return false;
        if (g_hitWinOn && coveredByHigher(g_in.mouseX, g_in.mouseY, g_hitWinZ, g_hitWinId)) return false;
    } else if (!hit(px, py, pw, ph)) return false;
    if (!(flags & DragDrop_NoDefaultRect)) {
        const unsigned char* dc = pick(Col_DragDropTarget, g_theme.accent);
        quad(px, py, pw, 2.0f, dc); quad(px, py + ph - 2.0f, pw, 2.0f, dc);   // an accent outline marks the drop spot
    }
    g_tddOver = true;
    return true;
}
void SetExternalDragDrop(const char* type, const void* data, int size, bool released) {
    if (!type || !*type || size < 0 || size > kDragPayloadMax || (size > 0 && !data)) return;
    if (g_tdd.active && !g_tddExt) return;   // OkayUI's own drag is in flight: it wins
    g_tddExt = true; g_tddExtSeen = true; g_tddExtReleased = released;
    g_tdd.active = true; g_tdd.armed = false; g_tdd.srcId = 0; g_tdd.preview[0] = 0;
    std::snprintf(g_tdd.pay.type, sizeof g_tdd.pay.type, "%s", type);
    if (size > 0) std::memcpy(g_tdd.pay.data, data, (std::size_t)size);
    g_tdd.pay.size = size; g_tdd.hasPayload = true;
}
bool IsDragDropExternal() { return g_tddExt && g_tdd.active; }
void CancelDragDrop() {
    g_dd = DragDrop(); g_tdd = TypedDrag(); g_tddExt = false; g_tddExtSeen = g_tddExtReleased = false;
}
const DragPayload* AcceptDragDropPayload(const char* type, int flags) {
    if (!g_tddOver || !g_tdd.active || !g_tdd.hasPayload) return nullptr;
    if (type && *type && std::strncmp(type, g_tdd.pay.type, sizeof g_tdd.pay.type - 1) != 0) return nullptr;
    static DragPayload out;
    out = g_tdd.pay; out.Data = out.data; out.DataSize = out.size;
    if (g_tddExt ? g_tddExtReleased : g_released) { out.delivery = true; g_tdd.active = false; g_tdd.hasPayload = false; return &out; }
    if (flags & DragDrop_AcceptBeforeDelivery) { out.delivery = false; return &out; }
    return nullptr;
}
void EndDragDropTarget() { g_tddOver = false; }
// A drop target over a rectangle given by the caller (an item with no auto-layout slot: a node, a timeline lane). Same rules as
// BeginDragDropTarget: true while a drag with a payload is over `bb`; the accent outline marks it; AcceptDragDropPayload works after it.
bool BeginDragDropTargetCustom(Rect bb, unsigned id) {
    (void)id;
    g_tddOver = false;
    if (!g_lay.active || !g_tdd.active || !g_tdd.hasPayload) return false;
    if (!std::isfinite(bb.x) || !std::isfinite(bb.y) || !std::isfinite(bb.w) || !std::isfinite(bb.h) || bb.w <= 0.0f || bb.h <= 0.0f) return false;
    if (!hit(bb.x, bb.y, bb.w, bb.h)) return false;
    const unsigned char* dc = pick(Col_DragDropTarget, g_theme.accent);
    quad(bb.x, bb.y, bb.w, 2.0f, dc); quad(bb.x, bb.y + bb.h - 2.0f, bb.w, 2.0f, dc);
    g_tddOver = true;
    return true;
}

bool DropTarget(int* outPayload) {
    if (!g_lay.active || !g_dd.active) return false;
    const float px = g_lay.prevX, py = g_lay.prevY, pw = g_lay.prevW, ph = g_lay.prevH;
    const bool over = hit(px, py, pw, ph);
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

static bool ListBox_raw(const char* label, int* current, const char* const* items, int count, int visibleRows) {
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
// Left edge of column `i` (sum of prior column widths from the table origin), before any scrolling.
float tblColX(int i) {
    float cx = g_tbl.x0;
    for (int k = 0; k < i && k < g_tbl.count; ++k) cx += *g_tbl.w[k];
    return cx;
}
inline bool tblScrolls() { return g_tbl.scrollX || g_tbl.scrollY; }
inline bool tblColFrozen(int i) { return g_tbl.scrollX && i < g_tbl.freezeCols; }
// Where column `i` is on the screen.
inline float tblRealX(int i) { return tblColX(i) - ((g_tbl.scrollX && i >= g_tbl.freezeCols) ? g_tbl.sx : 0.0f); }
constexpr float kBarThick = 10.0f;
// A scrolling table's bars: StyleVar_ScrollbarSize when pushed (then inside the one-pixel outer border), else kBarThick.
inline float tblBar() { return g_sbSize > 0.0f ? g_sbSize : kBarThick; }

// Clip to a part of the scrolling box: the rows band (frozen rows: the whole box height; body rows: below the frozen rows), and the
// column band (frozen columns, the scrolling columns, or the full width for a row background). Only changes the clip when it differs.
void tblSetClip(bool frozenRow, int colBand /*0 frozen cols, 1 scrolling cols, 2 full*/) {
    if (!tblScrolls()) return;
    const float hbar = g_tbl.needH ? kBarThick : 0.0f;
    const float top = frozenRow ? g_tbl.oy : g_tbl.frozenBottom, bottom = g_tbl.oy + g_tbl.oh - hbar;
    float left = g_tbl.ox, right = g_tbl.ox + g_tbl.viewW;
    if (colBand == 0) right = g_tbl.ox + std::min(g_tbl.frozenW, g_tbl.viewW);
    else if (colBand == 1) left = g_tbl.ox + std::min(g_tbl.frozenW, g_tbl.viewW);
    const float r[4] = {left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)};
    if (g_tbl.clipOn && r[0] == g_tbl.clipR[0] && r[1] == g_tbl.clipR[1] && r[2] == g_tbl.clipR[2] && r[3] == g_tbl.clipR[3]) return;
    if (g_tbl.clipOn) PopClipRect();
    PushClipRect(r[0], r[1], r[2], r[3]);
    g_tbl.clipOn = true; for (int i = 0; i < 4; ++i) g_tbl.clipR[i] = r[i];
}
// The clip of column `col`'s cell in the current row. A body row far outside the box is "culled": it keeps the plain full-width body clip, so
// its cells draw nowhere and the alternating frozen / scrolling clips do not pile up spans.
void tblCellClip(int col) {
    if (!tblScrolls()) return;
    const float rowRealTop = g_tbl.rowTop + g_tbl.shiftY;
    const bool culled = !g_tbl.rowFrozen && (rowRealTop + rowH() * 4.0f < g_tbl.frozenBottom || rowRealTop > g_tbl.oy + g_tbl.oh);
    tblSetClip(g_tbl.rowFrozen, culled ? 2 : (tblColFrozen(col) ? 0 : 1));
}
// The row clipper. Is the row whose top is at screen y `top` (and about `h` tall) wholly outside what can be seen? Then its cells still
// lay out (so the row heights, widths and ids stay exact) but nothing is emitted for them: no quads, no glyphs, so a table of thousands
// of rows neither fills the vertex batch nor costs time. Below the visible band is certain (rows only grow downward); above it allows
// four row heights of slack for a row taller than its remembered height. Frozen rows and overlay content are never culled.
bool tblRowOutside(float top, float h) {
    if (g_toOverlay || !g_rowClipper) return false;
    float clipTop, clipBot;
    if (tblScrolls()) {
        if (g_tbl.rowFrozen) return false;
        clipTop = g_tbl.frozenBottom; clipBot = g_tbl.oy + g_tbl.oh;
    } else if (g_clipTop > 0) {
        const SDL_Rect& c = g_clipStack[g_clipTop - 1]; clipTop = (float)c.y; clipBot = (float)(c.y + c.h);
    } else if (g_dispH > 0.0f) {
        clipTop = 0.0f; clipBot = g_dispH;
    } else return false;
    return top > clipBot + 1.0f || top + 4.0f * h < clipTop;
}
// Point the layout at the current cell (column g_tbl.index at row g_tbl.rowTop).
void tblEnterCell() {
    const float pad = g_cellPadX;
    const float cx = tblRealX(g_tbl.index);
    tblCellClip(g_tbl.index);
    g_lay.ox = cx + pad;
    g_lay.contentW = *g_tbl.w[g_tbl.index] - 2.0f * pad;
    if (g_lay.contentW < 8.0f) g_lay.contentW = 8.0f;
    g_lay.cy = g_tbl.rowTop + g_tbl.shiftY + (g_cellPadY > 0.0f ? g_cellPadY : 0.0f);
    g_lay.indent = 0.0f;
    g_lay.pendingSameLine = false;
    g_lay.prevX = g_lay.ox; g_lay.prevY = g_lay.cy; g_lay.prevW = 0.0f; g_lay.prevH = 0.0f;
}

// Size the columns of a flag-style table from its TableSetupColumn calls: fixed columns take their pixel width, the rest share what is left by weight.
inline bool tblHidden(int i) { return i >= 0 && i < 16 && ((g_tbl.hidden >> i) & 1u) != 0; }
// A column mask kept in a float state slot. It is only a mask when it is a whole number from 0 to 0xFFFF (16 columns at most);
// anything else (never set, NaN, or a slot another widget wrote to) counts as unset, so it is never cast to unsigned out of range.
inline bool tblStoredMask(float v, unsigned& out) {
    if (!(v >= 0.0f && v <= 65535.0f)) return false;   // also false for NaN
    const unsigned m = (unsigned)v;
    if ((float)m != v) return false;
    out = m;
    return true;
}
void tblFinishSetup() {
    if (g_tbl.setupDone) return;
    g_tbl.setupDone = true;
    {   // which columns are hidden: remembered per table; the first time, the Col_DefaultHide ones. The last visible column cannot be hidden.
        g_tbl.pHidden = floatState(g_tbl.id ^ 0x7a00, -1.0f);
        unsigned stored = 0;
        if (!tblStoredMask(*g_tbl.pHidden, stored)) {
            for (int i = 0; i < g_tbl.nSetup && i < 16; ++i) if ((g_tbl.sFlags[i] & Col_DefaultHide) && !(g_tbl.sFlags[i] & Col_NoHide)) stored |= 1u << i;
            *g_tbl.pHidden = (float)stored;
        }
        unsigned m = stored & ((1u << g_tbl.count) - 1u);
        for (int i = 0; i < g_tbl.nSetup && i < 16; ++i) if (g_tbl.sFlags[i] & Col_NoHide) m &= ~(1u << i);
        if (m == ((1u << g_tbl.count) - 1u)) m &= ~1u;
        g_tbl.hidden = m; *g_tbl.pHidden = (float)m;
    }
    const float avail = g_tbl.scrollX && g_tbl.innerWidth > 0.0f ? g_tbl.innerWidth : g_tbl.fullW;
    float fixed = 0.0f, wsum = 0.0f; int nstretch = 0;
    for (int i = 0; i < g_tbl.count; ++i) {
        if (tblHidden(i)) continue;
        if (i < g_tbl.nSetup && (g_tbl.sFlags[i] & Col_WidthFixed) && g_tbl.sVal[i] > 0.0f) fixed += g_tbl.sVal[i];
        else { wsum += (i < g_tbl.nSetup && g_tbl.sVal[i] > 0.0f) ? g_tbl.sVal[i] : 1.0f; ++nstretch; }
    }
    const float rest = std::max(avail - fixed, 24.0f * (float)nstretch);
    float sum = 0.0f;
    for (int i = 0; i < g_tbl.count; ++i) {
        float seed;
        if (tblHidden(i)) { g_tbl.zeroW[i] = 0.0f; g_tbl.w[i] = &g_tbl.zeroW[i]; continue; }
        if (i < g_tbl.nSetup && (g_tbl.sFlags[i] & Col_WidthFixed) && g_tbl.sVal[i] > 0.0f) seed = g_tbl.sVal[i];
        else seed = rest * ((i < g_tbl.nSetup && g_tbl.sVal[i] > 0.0f) ? g_tbl.sVal[i] : 1.0f) / (wsum > 0.0f ? wsum : 1.0f);
        g_tbl.w[i] = floatState(g_tbl.id ^ (0x7100 + i), seed);
        if (*g_tbl.w[i] < 12.0f) *g_tbl.w[i] = 12.0f;
        sum += *g_tbl.w[i];
    }
    {   // columns shown again this frame: they come back at the width they had, the others give that back (so hide and show is exact)
        float* prevM = floatState(g_tbl.id ^ 0x7a02, -1.0f);
        unsigned prevStored = 0;
        const unsigned prev = tblStoredMask(*prevM, prevStored) ? (prevStored & ((1u << g_tbl.count) - 1u)) : g_tbl.hidden;
        const unsigned newly = prev & ~g_tbl.hidden;
        *prevM = (float)g_tbl.hidden;
        if (newly && !g_tbl.scrollX) {
            float total = 0.0f, others = 0.0f;
            for (int i = 0; i < g_tbl.count; ++i) { if (tblHidden(i)) continue; if ((newly >> i) & 1u) total += *g_tbl.w[i]; else others += *g_tbl.w[i]; }
            if (others > 0.0f && g_tbl.fullW > total + 24.0f) {
                const float f = (g_tbl.fullW - total) / others;
                for (int i = 0; i < g_tbl.count; ++i) if (!tblHidden(i) && !((newly >> i) & 1u)) *g_tbl.w[i] *= f;
            }
            sum = 0.0f;
            for (int i = 0; i < g_tbl.count; ++i) if (!tblHidden(i)) sum += *g_tbl.w[i];
        }
    }
    if (g_tbl.scrollX) {   // a box that scrolls sideways keeps its column widths
        g_tbl.contentW = sum;
        g_tbl.needH = sum > g_tbl.viewW + 0.5f;
        g_tbl.fullW = sum;
        g_tbl.frozenW = 0.0f;
        for (int i = 0; i < g_tbl.freezeCols && i < g_tbl.count; ++i) g_tbl.frozenW += *g_tbl.w[i];
        if (g_tbl.frozenW > g_tbl.viewW * 0.9f) g_tbl.frozenW = 0.0f, g_tbl.freezeCols = 0;   // frozen columns that fill the box would leave nothing to scroll
    } else if (sum > 0.0f) {
        const float f = g_tbl.fullW / sum; for (int i = 0; i < g_tbl.count; ++i) if (!tblHidden(i)) *g_tbl.w[i] *= f;
    }
    // the sort keys: the DefaultSort columns the first time the table is seen
    if ((g_tbl.flags & Table_Sortable) && g_tbl.store && !g_tbl.store->inited) {
        TblStore& t = *g_tbl.store;
        t.inited = true; t.sortN = 0;
        for (int i = 0; i < g_tbl.nSetup && t.sortN < kTblSortKeys; ++i)
            if ((g_tbl.sFlags[i] & Col_DefaultSort) && !(g_tbl.sFlags[i] & Col_NoSort)) { t.sortCol[t.sortN] = i; t.sortDesc[t.sortN] = (g_tbl.sFlags[i] & Col_PreferSortDescending) != 0; ++t.sortN; }
        t.dirty = t.sortN > 0;
    }
}
// Sorting by a header click (ImGui's rules): a plain click makes the column the only key (or flips it if it already is the only key,
// and with tristate removes it after descending); Ctrl+click with SortMulti adds it as a further key or flips / removes it.
void tblSortClick(int col) {
    TblStore& t = *g_tbl.store;
    const bool multi = (g_tbl.flags & Table_SortMulti) != 0 && g_io.keyCtrl, tri = (g_tbl.flags & Table_SortTristate) != 0;
    const bool pref = (g_tbl.sFlags[col] & Col_PreferSortDescending) != 0;
    int at = -1;
    for (int k = 0; k < t.sortN; ++k) if (t.sortCol[k] == col) at = k;
    auto removeKey = [&](int k) { for (int j = k; j + 1 < t.sortN; ++j) { t.sortCol[j] = t.sortCol[j + 1]; t.sortDesc[j] = t.sortDesc[j + 1]; } --t.sortN; };
    if (multi) {
        if (at < 0) { if (t.sortN < kTblSortKeys) { t.sortCol[t.sortN] = col; t.sortDesc[t.sortN] = pref; ++t.sortN; } }
        else if (t.sortDesc[at] == pref && !(tri && t.sortDesc[at] != pref)) t.sortDesc[at] = !pref;       // first click flipped it
        else if (tri) removeKey(at);
        else t.sortDesc[at] = !t.sortDesc[at];
    } else if (at >= 0 && t.sortN == 1) {
        if (t.sortDesc[at] == pref) t.sortDesc[at] = !pref;
        else if (tri) t.sortN = 0;
        else t.sortDesc[at] = pref;
    } else { t.sortN = 1; t.sortCol[0] = col; t.sortDesc[0] = pref; }
    t.dirty = true;
}
// The header bar with bold column labels (and sort arrows); moves the row cursor below it.
void tblDrawHeader(const char* const* labels, bool interactive) {
    const float hh = rowH();
    const float rowY = g_tbl.rowTop;                   // headers are never scrolled vertically
    const bool frozen = g_tbl.rowsDone < g_tbl.freezeRows || !g_tbl.scrollY;
    if (tblScrolls()) tblSetClip(true, 2);
    const float fullL = g_tbl.scrollX ? g_tbl.ox : g_tbl.x0, fullW2 = g_tbl.scrollX ? g_tbl.viewW : g_tbl.fullW;
    if (g_look) quad(fullL, rowY + hh - 1.0f, fullW2, 1.0f, pick(Col_TableBorderStrong, g_theme.border));   // hairline under dim headers
    else quad(fullL, rowY, fullW2, hh, pick(Col_TableHeaderBg, g_theme.bgDown));
    const Font* prevF = g_font; g_font = &kFontBold;
    for (int i = 0; i < g_tbl.count; ++i) {
        if (tblHidden(i)) continue;
        const float cx = tblRealX(i), cw = *g_tbl.w[i];
        if (tblScrolls()) tblSetClip(true, tblColFrozen(i) ? 0 : 1);
        const bool sortable = interactive && (g_tbl.flags & Table_Sortable) && g_tbl.store && i < g_tbl.nSetup && !(g_tbl.sFlags[i] & Col_NoSort);
        bool hot = false, down = false;
        if (sortable) {
            const int hid = g_tbl.id ^ (0x7c00 + i);
            const float hitW = cw - (i + 1 < g_tbl.count ? 4.0f : 0.0f);      // the border's resize handle keeps its few pixels
            hot = hit(cx, rowY, hitW, hh);
            if (hot) g_hot = hid;
            if (g_active == hid) { if (g_released) { if (hot) tblSortClick(i); g_active = 0; } else down = true; }
            else if (hot && g_pressed) { g_active = hid; g_focusClaimed = true; down = true; }
            if (hot || down) quad(cx, rowY, cw, hh, pick(down ? Col_HeaderActive : Col_HeaderHovered, down ? g_theme.bgDown : g_theme.bgHover));
        }
        if (labels[i] && labels[i][0] && !(i < g_tbl.nSetup && (g_tbl.sFlags[i] & Col_NoHeaderLabel)))
            drawText(cx + 4.0f, rowY + (hh - textH()) * 0.5f, labels[i], g_theme.textScale, g_look ? pick(Col_TextDisabled, g_theme.textDim) : g_theme.text);
        if (sortable) {
            const TblStore& t = *g_tbl.store;
            for (int k = 0; k < t.sortN; ++k) if (t.sortCol[k] == i) {
                const float ax = cx + cw - 14.0f, ay = rowY + hh * 0.5f, r = 4.0f;
                if (t.sortDesc[k]) tri(ax - r, ay - r * 0.6f, ax + r, ay - r * 0.6f, ax, ay + r * 0.8f, g_theme.text);
                else tri(ax, ay - r * 0.8f, ax - r, ay + r * 0.6f, ax + r, ay + r * 0.6f, g_theme.text);
                if (t.sortN > 1) { char d[4]; std::snprintf(d, sizeof d, "%d", k + 1); drawText(ax - r - 12.0f, rowY + (hh - textH()) * 0.5f, d, g_theme.textScale * 0.7f, g_theme.text); }
            }
        }
    }
    g_font = prevF;
    (void)frozen;
    g_tbl.rowTop += hh;
    g_tbl.rowBottom = g_tbl.rowTop;
    g_tbl.rowStart = g_tbl.rowTop;
    g_tbl.headerBottom = g_tbl.rowTop;
    g_tbl.headerDrawn = true;
    ++g_tbl.rowsDone;
}
// Close the row in progress: the lowest point its cells reached becomes the row's bottom; its height is remembered for the next frame's band.
void tblFinishRow() {
    const float cyLogical = g_lay.cy - g_tbl.shiftY + (g_cellPadY >= 0.0f && g_tbl.index >= 0 ? g_cellPadY - g_lay.spacingY : 0.0f);
    if (cyLogical > g_tbl.rowBottom) g_tbl.rowBottom = cyLogical;
    if (g_tbl.rowTop + g_tbl.minRowH > g_tbl.rowBottom) g_tbl.rowBottom = g_tbl.rowTop + g_tbl.minRowH;
}
void tblRememberRow() {
    if (g_tbl.store && g_tbl.rowIndex >= 0 && g_tbl.rowIndex < kTblRows) g_tbl.store->rowH[g_tbl.rowIndex] = std::max(0.0f, g_tbl.rowBottom - g_tbl.rowTop);
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
    g_tbl = TableState();
    g_tbl.active = true; g_tbl.id = tid; g_tbl.count = columns; g_tbl.index = -1;
    g_tbl.x0 = g_lay.ox; g_tbl.fullW = g_lay.contentW;
    g_tbl.rowTop = g_lay.cy; g_tbl.rowBottom = g_lay.cy; g_tbl.rowStart = g_lay.cy;
    g_tbl.topY = g_lay.cy; g_tbl.flags = 0; g_tbl.rowIndex = -1; g_tbl.setupDone = true; g_tbl.nSetup = 0;

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
        if (g_look) quad(g_tbl.x0, g_tbl.rowTop + hh - 1.0f, g_tbl.fullW, 1.0f, pick(Col_TableBorderStrong, g_theme.border));   // hairline under dim headers
        else quad(g_tbl.x0, g_tbl.rowTop, g_tbl.fullW, hh, pick(Col_TableHeaderBg, g_theme.bgDown));
        const Font* prevF = g_font; g_font = &kFontBold;
        for (int i = 0; i < columns; ++i) {
            float cx = tblColX(i);
            if (headers[i] && headers[i][0])
                drawText(cx + 4.0f, g_tbl.rowTop + (hh - textH()) * 0.5f, headers[i], g_theme.textScale, g_look ? pick(Col_TextDisabled, g_theme.textDim) : g_theme.text);
        }
        g_font = prevF;
        g_tbl.rowTop += hh;
        g_tbl.rowBottom = g_tbl.rowTop;
        g_tbl.rowStart = g_tbl.rowTop;
    }
    g_tbl.headerBottom = g_tbl.rowTop;
    return true;
}

bool BeginTable(const char* id, int columns, TableFlags flags) {
    return BeginTable(id, columns, flags, Vec2{}, 0.0f);
}

bool BeginTable(const char* id, int columns, TableFlags flags, Vec2 outerSize, float innerWidth) {
    if (!g_lay.active || columns < 1) return false;
    if (columns > 16) columns = 16;
    const int tid = hashLabel(id);
    g_tbl = TableState();
    g_tbl.active = true; g_tbl.id = tid; g_tbl.count = columns; g_tbl.index = -1;
    g_tbl.x0 = g_lay.ox; g_tbl.fullW = g_lay.contentW;
    g_tbl.rowTop = g_tbl.rowBottom = g_tbl.rowStart = g_tbl.headerBottom = g_tbl.topY = g_lay.cy;
    g_tbl.flags = (int)flags; g_tbl.rowIndex = -1; g_tbl.nSetup = 0; g_tbl.setupDone = false;
    if (flags & Table_Sortable) { g_tbl.store = tblStoreFor(tid); g_tbl.store->lastFrame = g_frameCount; }
    else if (flags & Table_RowBg) { g_tbl.store = tblStoreFor(tid); g_tbl.store->lastFrame = g_frameCount; }
    g_tbl.scrollX = (flags & Table_ScrollX) != 0; g_tbl.scrollY = (flags & Table_ScrollY) != 0;
    if (g_tbl.scrollX || g_tbl.scrollY) {
        g_tbl.pSX = floatState(tid ^ 0x7800, 0.0f); g_tbl.pSY = floatState(tid ^ 0x7801, 0.0f);
        g_tbl.pMaxY = floatState(tid ^ 0x7802, 0.0f); g_tbl.pH = floatState(tid ^ 0x7803, rowH() * 3.0f);
        const float ow = (outerSize.x > 0.0f && std::isfinite(outerSize.x)) ? outerSize.x : availW();
        float oh;
        if (outerSize.y > 0.0f && std::isfinite(outerSize.y)) oh = outerSize.y;
        else if (g_tbl.scrollY) oh = std::max(rowH() * 4.0f, g_lay.bottom - g_lay.cy);
        else oh = *g_tbl.pH + (g_tbl.scrollX ? kBarThick : 0.0f);
        float bx, by; place(ow, oh, bx, by);
        g_tbl.saved = g_lay;                  // the cursor is already below the box; the cells work from its top
        g_lay.ox = bx; g_lay.cy = by; g_lay.contentW = ow; g_lay.indent = 0.0f; g_lay.pendingSameLine = false;
        g_tbl.ox = bx; g_tbl.oy = by; g_tbl.ow = ow; g_tbl.oh = oh;
        g_tbl.needV = g_tbl.scrollY && *g_tbl.pMaxY > 0.0f;
        g_tbl.viewW = ow - (g_tbl.needV ? tblBar() + (g_sbSize > 0.0f ? 1.0f : 0.0f) : 0.0f);
        g_tbl.innerWidth = innerWidth > 0.0f && std::isfinite(innerWidth) ? innerWidth : 0.0f;
        g_tbl.x0 = bx; g_tbl.fullW = g_tbl.viewW;
        g_tbl.rowTop = g_tbl.rowBottom = g_tbl.rowStart = g_tbl.headerBottom = g_tbl.topY = by;
        g_tbl.sx = *g_tbl.pSX; g_tbl.sy = *g_tbl.pSY;
        g_tbl.frozenBottom = by;
        // the wheel (Shift+wheel for the sideways direction) scrolls while the pointer is over the box
        if (g_in.wheel != 0.0f && !g_wheelUsed && hit(bx, by, ow, oh)) {
            const bool sideways = g_tbl.scrollX && (g_io.keyShift || !g_tbl.scrollY);
            if (sideways) { g_tbl.sx -= g_in.wheel * rowH() * 2.0f; g_wheelUsed = true; }
            else if (g_tbl.scrollY && *g_tbl.pMaxY > 0.0f) { g_tbl.sy -= g_in.wheel * rowH(); g_wheelUsed = true; }
        }
        g_tbl.sy = std::max(0.0f, std::min(g_tbl.sy, *g_tbl.pMaxY));
        g_tbl.sx = std::max(0.0f, g_tbl.sx);
    }
    return true;
}

void TableSetupColumn(const char* label, int columnFlags, float widthOrWeight, unsigned userId) {
    if (!g_tbl.active || g_tbl.setupDone || g_tbl.nSetup >= g_tbl.count) return;
    if (!std::isfinite(widthOrWeight) || widthOrWeight < 0.0f) widthOrWeight = 0.0f;
    if (widthOrWeight > 100000.0f) widthOrWeight = 100000.0f;
    g_tbl.sLabel[g_tbl.nSetup] = label; g_tbl.sFlags[g_tbl.nSetup] = columnFlags; g_tbl.sVal[g_tbl.nSetup] = widthOrWeight; g_tbl.sUser[g_tbl.nSetup] = userId;
    ++g_tbl.nSetup;
}

void TableSetupScrollFreeze(int cols, int rows) {
    if (!g_tbl.active || g_tbl.setupDone) return;
    g_tbl.freezeCols = std::max(0, std::min(cols, g_tbl.count)); g_tbl.freezeRows = std::max(0, std::min(rows, 32));
}

void TableHeadersRow() {
    g_tbl.rowCulled = false; g_cullRow = false;
    if (!g_lay.active || !g_tbl.active) return;
    tblFinishSetup();
    tblFinishRow();
    g_tbl.rowTop = g_tbl.rowBottom;
    g_tbl.shiftY = 0.0f;
    const char* none[16] = {nullptr};
    for (int i = 0; i < g_tbl.count && i < g_tbl.nSetup; ++i) none[i] = g_tbl.sLabel[i];
    tblDrawHeader(none, true);
    if ((g_tbl.flags & Table_Hideable) && g_tbl.pHidden && g_lay.active && !g_tbl.scrollX && !g_tbl.scrollY) {
        // a right click on the header row opens the show / hide menu (every column with a check box; a NoHide column, and the last visible one, are locked)
        const float hh = rowH(), rowY = g_tbl.rowTop - hh;
        const int pid = g_tbl.id ^ 0x7a01;
        if (g_rPressed && !g_in.blocked && hit(g_tbl.x0, rowY, g_tbl.fullW, hh)) {
            PopupSlot* ps = popupSlot(pid, true);
            if (ps) { ps->open = true; ps->x = g_in.mouseX; ps->y = g_in.mouseY; ps->w = 0.0f; g_openPopupThisFrame = pid; }
        }
        const TableState keep = g_tbl;   // the menu's widgets use the layout, not the table; the table state is restored after
        if (beginPopupBody(pid)) {
            int visible = 0; for (int i = 0; i < keep.count; ++i) if (!((keep.hidden >> i) & 1u)) ++visible;
            for (int i = 0; i < keep.count; ++i) {
                const bool on = !((keep.hidden >> i) & 1u);
                const bool locked = (i < keep.nSetup && (keep.sFlags[i] & Col_NoHide)) || (on && visible <= 1);
                bool v = on;
                BeginDisabled(locked);
                char idb[16]; std::snprintf(idb, sizeof idb, "tc%d", i);
                PushID(idb);   // two columns may share a label: each check box gets its own id
                if (Checkbox((i < keep.nSetup && keep.sLabel[i] && keep.sLabel[i][0]) ? keep.sLabel[i] : "(column)", &v) && v != on) { unsigned m = keep.hidden; if (v) m &= ~(1u << i); else m |= 1u << i; *keep.pHidden = (float)m; }
                PopID();
                EndDisabled();
            }
            EndPopup();
        }
    }
}

bool TableSetColumnEnabled(int column, bool enabled) {
    if (!g_tbl.active || column < 0 || column >= g_tbl.count) return false;
    tblFinishSetup();
    if (!g_tbl.pHidden) return false;
    if (column < g_tbl.nSetup && (g_tbl.sFlags[column] & Col_NoHide) && !enabled) return false;
    unsigned m = g_tbl.hidden;
    if (tblStoredMask(*g_tbl.pHidden, m)) m &= (1u << g_tbl.count) - 1u;   // the pending set (a change made earlier this frame counts)
    else m = g_tbl.hidden;
    if (enabled) m &= ~(1u << column); else m |= 1u << column;
    if (m == ((1u << g_tbl.count) - 1u)) return false;   // never hide them all
    *g_tbl.pHidden = (float)m;                           // takes effect next frame (this frame's widths are set)
    return true;
}
bool TableGetColumnIsEnabled(int column) { return g_tbl.active && column >= 0 && column < g_tbl.count && !tblHidden(column); }
int TableGetColumnCount() { return g_tbl.active ? g_tbl.count : 0; }
const char* TableGetColumnName(int column) {
    if (!g_tbl.active) return "";
    if (column < 0) column = g_tbl.index;
    return (column >= 0 && column < g_tbl.nSetup && g_tbl.sLabel[column]) ? g_tbl.sLabel[column] : "";
}
int TableGetColumnFlags(int column) {
    if (!g_tbl.active) return 0;
    if (column < 0) column = g_tbl.index;
    return (column >= 0 && column < g_tbl.nSetup) ? g_tbl.sFlags[column] : 0;
}
int TableGetHoveredColumn() {
    if (!g_tbl.active || g_in.blocked || g_tbl.count <= 0) return -1;
    tblFinishSetup();                                    // the widths exist once the columns are set up
    const float top = g_tbl.scrollX || g_tbl.scrollY ? g_tbl.oy : g_tbl.topY;
    const float bottom = std::max(g_tbl.rowBottom, g_lay.cy);
    if (g_in.mouseY < top || g_in.mouseY > bottom) return -1;
    for (int i = 0; i < g_tbl.count; ++i) {
        if (tblHidden(i)) continue;
        const float x = tblRealX(i);
        if (g_in.mouseX >= x && g_in.mouseX < x + *g_tbl.w[i]) return i;
    }
    return -1;
}

void TableNextRow(int, float minRowHeight) { TableNextRow(); if (g_tbl.active && std::isfinite(minRowHeight) && minRowHeight > 0.0f) g_tbl.minRowH = minRowHeight; }
TableSortSpecs* TableGetSortSpecs() {
    static TableColumnSortSpecs arr[kTblSortKeys];
    static TableSortSpecs out;
    if (!g_tbl.active || !(g_tbl.flags & Table_Sortable) || !g_tbl.store) return nullptr;
    tblFinishSetup();
    const TblStore& t = *g_tbl.store;
    for (int k = 0; k < t.sortN; ++k) { arr[k].columnIndex = t.sortCol[k]; arr[k].sortOrder = k; arr[k].descending = t.sortDesc[k]; arr[k].columnUserId = t.sortCol[k] < g_tbl.nSetup ? g_tbl.sUser[t.sortCol[k]] : 0; arr[k].ColumnUserID = arr[k].columnUserId; arr[k].ColumnIndex = arr[k].columnIndex; arr[k].SortOrder = k; arr[k].SortDirection = arr[k].descending ? 2 : 1; }
    out.specs = arr; out.specsCount = t.sortN; out.specsDirty = t.dirty; out.SpecsDirty = t.dirty;
    out.Specs = arr; out.SpecsCount = t.sortN;
    g_sortSpecsFor = g_tbl.id; g_sortSpecsOut = &out;
    return &out;
}

float TableGetScrollX() { return g_tbl.active && g_tbl.scrollX ? g_tbl.sx : 0.0f; }
float TableGetScrollY() { return g_tbl.active && g_tbl.scrollY ? g_tbl.sy : 0.0f; }
void TableSetScroll(float x, float y) {
    if (!g_tbl.active || !(g_tbl.scrollX || g_tbl.scrollY)) return;
    if (g_tbl.scrollX && std::isfinite(x)) g_tbl.sx = std::max(0.0f, x);
    if (g_tbl.scrollY && std::isfinite(y)) g_tbl.sy = std::max(0.0f, std::min(y, *g_tbl.pMaxY));
}

void SetTableRowClipping(bool on) { g_rowClipper = on; }
bool TableRowIsVisible() { return !(g_tbl.active && g_tbl.rowCulled); }
bool TableSetColumnIndex(int column) {
    if (!g_lay.active || !g_tbl.active || column < 0 || column >= g_tbl.count) return false;
    g_tbl.index = column - 1;
    return TableNextColumn();
}
int TableGetColumnIndex() { return g_tbl.active ? g_tbl.index : -1; }
int TableGetRowIndex() { return g_tbl.active ? g_tbl.rowIndex : -1; }

void TableNextRow() {
    if (!g_lay.active || !g_tbl.active) return;
    tblFinishSetup();
    tblFinishRow();
    tblRememberRow();
    g_tbl.minRowH = 0.0f;
    g_tbl.rowTop = g_tbl.rowBottom;   // next row starts below the tallest cell so far
    g_tbl.index = -1;
    ++g_tbl.rowIndex;
    // scrolling boxes: rows after the frozen ones are shifted by the scroll and clipped below the frozen part
    g_tbl.rowFrozen = !g_tbl.scrollY || g_tbl.rowsDone < g_tbl.freezeRows;
    if (!g_tbl.rowFrozen && !g_tbl.bodyStarted) { g_tbl.bodyStarted = true; g_tbl.frozenBottom = g_tbl.rowTop; }
    g_tbl.shiftY = (g_tbl.scrollY && !g_tbl.rowFrozen) ? -g_tbl.sy : 0.0f;
    ++g_tbl.rowsDone;
    const float bandH = (g_tbl.store && g_tbl.rowIndex < kTblRows && g_tbl.store->rowH[g_tbl.rowIndex] > 0.0f) ? g_tbl.store->rowH[g_tbl.rowIndex] : rowH();
    g_tbl.rowBgH = bandH;
    const float by = g_tbl.rowTop + g_tbl.shiftY;
    g_tbl.rowCulled = tblRowOutside(by, bandH); g_cullRow = g_tbl.rowCulled;
    const float bx = g_tbl.scrollX ? g_tbl.ox : g_tbl.x0, bw = g_tbl.scrollX ? g_tbl.viewW : g_tbl.fullW;
    if (tblScrolls()) tblSetClip(g_tbl.rowFrozen, 2);
    if ((g_tbl.flags & Table_RowBg) && (g_tbl.rowIndex & 1)) {   // banded rows: drawn first so the cells go on top
        unsigned char tmp[4]; const unsigned char white[4] = {255, 255, 255, 255};
        quad(bx, by, bw, bandH, pick(Col_TableRowBgAlt, withAlpha(white, 0.045f, tmp)));
    } else if ((g_tbl.flags & Table_RowBg) && colorIsSet(Col_TableRowBg)) {
        quad(bx, by, bw, bandH, g_theme.colors[Col_TableRowBg]);
    }
    if ((g_tbl.flags & Table_BordersInnerH) && g_tbl.rowTop > g_tbl.rowStart + 0.5f) quad(bx, by, bw, 1.0f, pick(Col_TableBorderLight, g_theme.border));
}

bool TableNextColumn() {
    if (!g_lay.active || !g_tbl.active) return false;
    tblFinishSetup();
    if (g_tbl.index >= 0) tblFinishRow();   // remember how tall the cell we're leaving got
    ++g_tbl.index;
    if (g_tbl.index >= g_tbl.count) return false;
    tblEnterCell();
    if (tblHidden(g_tbl.index)) { g_lay.ox = -30000.0f; return false; }   // a hidden column's cell lays out far off screen, so widgets drawn there are never seen or hit
    return true;
}

void TableSetBgColor(TableBgTarget target, Color32 color, int column) {
    if (!g_lay.active || !g_tbl.active || g_tbl.rowIndex < 0) return;
    const unsigned char c[4] = {(unsigned char)(color & 255u), (unsigned char)((color >> 8) & 255u), (unsigned char)((color >> 16) & 255u), (unsigned char)((color >> 24) & 255u)};
    const float by = g_tbl.rowTop + g_tbl.shiftY;
    if (target == TableBg_CellBg) {
        const int col = column >= 0 ? column : g_tbl.index;
        if (col < 0 || col >= g_tbl.count) return;
        if (tblScrolls()) tblSetClip(g_tbl.rowFrozen, tblColFrozen(col) ? 0 : 1);
        quad(tblRealX(col), by, *g_tbl.w[col], g_tbl.rowBgH, c);
    } else if (target == TableBg_RowBg0 || target == TableBg_RowBg1) {
        const float bx = g_tbl.scrollX ? g_tbl.ox : g_tbl.x0, bw = g_tbl.scrollX ? g_tbl.viewW : g_tbl.fullW;
        if (tblScrolls()) tblSetClip(g_tbl.rowFrozen, 2);
        quad(bx, by, bw, g_tbl.rowBgH, c);
    }
    if (tblScrolls() && g_tbl.index >= 0) tblCellClip(g_tbl.index);
}

// Table_BordersOuter: a one-pixel frame in Col_TableBorderStrong around the table (or its scrolling box).
static void tblOuterBorder(float x, float y, float w, float h) {
    if (!(w > 1.0f) || !(h > 1.0f)) return;
    const unsigned char* c = pick(Col_TableBorderStrong, g_theme.border);
    quad(x, y, w, 1.0f, c); quad(x, y + h - 1.0f, w, 1.0f, c);
    quad(x, y + 1.0f, 1.0f, h - 2.0f, c); quad(x + w - 1.0f, y + 1.0f, 1.0f, h - 2.0f, c);
}

void EndTable() {
    if (!g_lay.active || !g_tbl.active) return;
    g_tbl.rowCulled = false; g_cullRow = false;
    tblFinishSetup();
    tblFinishRow();
    tblRememberRow();
    const float top = g_tbl.scrollX || g_tbl.scrollY ? g_tbl.oy : g_tbl.topY, bottom = g_tbl.rowBottom;
    if (g_tbl.scrollX || g_tbl.scrollY) {
        // the body's own height, the scroll limits, then the borders, the bars and the wheel
        if (g_tbl.clipOn) { PopClipRect(); g_tbl.clipOn = false; }
        const float hbar = g_tbl.needH ? kBarThick : 0.0f;
        const float bodyTop = g_tbl.bodyStarted ? g_tbl.frozenBottom : std::max(g_tbl.frozenBottom, g_tbl.rowBottom);
        const float viewH = g_tbl.oy + g_tbl.oh - hbar - bodyTop;
        const float contentH = std::max(0.0f, g_tbl.rowBottom - bodyTop);
        const float maxY = g_tbl.scrollY ? std::max(0.0f, contentH - viewH) : 0.0f;
        *g_tbl.pMaxY = maxY;
        if (g_tbl.scrollX) *g_tbl.pH = std::max(rowH(), g_tbl.rowBottom - g_tbl.oy);
        else *g_tbl.pH = std::max(rowH(), g_tbl.rowBottom - g_tbl.oy);
        float sy = std::max(0.0f, std::min(g_tbl.sy, maxY)), sx = g_tbl.sx;
        const float maxX = g_tbl.scrollX ? std::max(0.0f, g_tbl.contentW - g_tbl.viewW) : 0.0f;
        sx = std::max(0.0f, std::min(sx, maxX));
        // column borders and the resize handles (clipped to the box; a frozen border stays put)
        const float bTop = g_tbl.oy, bBot = g_tbl.oy + g_tbl.oh - hbar;
        PushClipRect(g_tbl.ox, bTop, g_tbl.viewW, std::max(0.0f, bBot - bTop));
        for (int i = 1; i < g_tbl.count; ++i) {
            int pj = i - 1; while (pj >= 0 && tblHidden(pj)) --pj;
            if (tblHidden(i) || pj < 0) continue;                                  // a hidden column has no border of its own
            const bool noRes = (pj < g_tbl.nSetup && (g_tbl.sFlags[pj] & Col_NoResize)) || (i < g_tbl.nSetup && (g_tbl.sFlags[i] & Col_NoResize));
            const float bx = tblColX(i) - (i > g_tbl.freezeCols || !g_tbl.scrollX ? (g_tbl.scrollX ? sx : 0.0f) : 0.0f);
            if (g_tbl.scrollX && i > g_tbl.freezeCols && (bx < g_tbl.ox + g_tbl.frozenW || bx > g_tbl.ox + g_tbl.viewW)) continue;   // scrolled under the frozen columns or out of the box
            quad(bx - 0.5f, bTop, 1.0f, std::min(bBot - bTop, std::max(0.0f, g_tbl.rowBottom - g_tbl.oy)), pick(Col_TableBorderStrong, g_theme.border));
            const int hid = g_tbl.id ^ (0x7b00 + i);
            const float grab = 4.0f;
            const bool over = !(g_tbl.flags & Table_NoResize) && !noRes && hit(bx - grab, bTop, grab * 2.0f, std::max(0.0f, bBot - bTop));
            if (over) g_hot = hid;
            if (g_active == hid) {
                if (!g_in.mouseDown) g_active = 0;
                else {
                    float dx = g_mouseDX;
                    float a = *g_tbl.w[pj] + dx, b2 = *g_tbl.w[i] - dx;
                    if (g_tbl.scrollX) { if (a >= 20.0f) *g_tbl.w[pj] = a; }   // sideways boxes just grow the column
                    else if (a >= 20.0f && b2 >= 20.0f) { *g_tbl.w[pj] = a; *g_tbl.w[i] = b2; }
                }
            } else if (over && g_pressed) { g_active = hid; g_focusClaimed = true; }
        }
        PopClipRect();
        // the bars
        if (g_tbl.scrollY && maxY > 0.0f) {
            const float bar = tblBar(), in = g_sbSize > 0.0f ? 1.0f : 0.0f;
            const float sbx = g_tbl.ox + g_tbl.ow - bar - in, trackTop = bodyTop, trackH = std::max(8.0f, g_tbl.oy + g_tbl.oh - hbar - bodyTop - in);
            float frac = viewH / (viewH + maxY); if (frac > 1.0f) frac = 1.0f;
            float thumbH = std::min(trackH, std::max(16.0f, frac * trackH));
            const float thumbY = trackTop + (sy / maxY) * (trackH - thumbH);
            const int bid = g_tbl.id ^ 0x7d10;
            const bool over = hit(sbx, thumbY, bar, thumbH);
            if (over) g_hot = bid;
            if (g_active == bid) { if (!g_in.mouseDown) g_active = 0; else if (trackH - thumbH > 0.0f) sy += g_mouseDY * (maxY / (trackH - thumbH)); }
            else if (over && g_pressed) { g_active = bid; g_focusClaimed = true; }
            sy = std::max(0.0f, std::min(sy, maxY));
            scrollbarThumb(sbx, trackTop, trackH, trackTop + (sy / maxY) * (trackH - thumbH), thumbH, g_active == bid, over);
        }
        if (g_tbl.scrollX && maxX > 0.0f) {
            const float sby = g_tbl.oy + g_tbl.oh - kBarThick, trackW = g_tbl.viewW;
            float frac = g_tbl.viewW / g_tbl.contentW; if (frac > 1.0f) frac = 1.0f;
            float thumbW = std::min(trackW, std::max(16.0f, frac * trackW));
            const float thumbX = g_tbl.ox + (sx / maxX) * (trackW - thumbW);
            const int bid = g_tbl.id ^ 0x7d11;
            const bool over = hit(thumbX, sby, thumbW, kBarThick);
            if (over) g_hot = bid;
            if (g_active == bid) { if (!g_in.mouseDown) g_active = 0; else if (trackW - thumbW > 0.0f) sx += g_mouseDX * (maxX / (trackW - thumbW)); }
            else if (over && g_pressed) { g_active = bid; g_focusClaimed = true; }
            sx = std::max(0.0f, std::min(sx, maxX));
            const float tx = g_tbl.ox + (sx / maxX) * (trackW - thumbW);
            if (!g_look || colorIsSet(Col_ScrollbarBg)) quad(g_tbl.ox, sby, trackW, kBarThick, pick(Col_ScrollbarBg, g_theme.track));
            const bool act = g_active == bid;
            quad(tx + 1.0f, sby + 1.0f, thumbW - 2.0f, kBarThick - 2.0f, act ? pick(Col_ScrollbarGrabActive, g_theme.text) : (over ? pick(Col_ScrollbarGrabHovered, g_theme.text) : pick(Col_ScrollbarGrab, g_theme.accent)));
        }
        *g_tbl.pSX = sx; *g_tbl.pSY = sy;
        if (g_tbl.flags & Table_BordersOuter) tblOuterBorder(g_tbl.ox, g_tbl.oy, g_tbl.ow, g_tbl.oh);
        if (g_sortSpecsFor == g_tbl.id && g_sortSpecsOut && g_tbl.store) g_tbl.store->dirty = g_sortSpecsOut->specsDirty && g_sortSpecsOut->SpecsDirty;
        g_sortSpecsFor = 0; g_sortSpecsOut = nullptr;
        g_lay = g_tbl.saved;                 // the box was placed as one item: the cursor is already below it
        g_lay.pendingSameLine = false;
        g_posOverride = false;
        g_tbl.active = false;
        return;
    }

    // Column borders + draggable resize handles (applied next frame).
    for (int i = 1; i < g_tbl.count; ++i) {
        int pj = i - 1; while (pj >= 0 && tblHidden(pj)) --pj;
        if (tblHidden(i) || pj < 0) continue;                                      // a hidden column has no border of its own
        const bool noRes = (pj < g_tbl.nSetup && (g_tbl.sFlags[pj] & Col_NoResize)) || (i < g_tbl.nSetup && (g_tbl.sFlags[i] & Col_NoResize));
        float bx = tblColX(i);
        quad(bx - 0.5f, top, 1.0f, bottom - top, pick(Col_TableBorderStrong, g_theme.border));
        const int hid = g_tbl.id ^ (0x7b00 + i);
        const float grab = 4.0f;
        const bool over = !(g_tbl.flags & Table_NoResize) && !noRes && hit(bx - grab, top, grab * 2.0f, bottom - top);
        if (over) g_hot = hid;
        if (g_active == hid) {
            if (!g_in.mouseDown) g_active = 0;
            else {
                float dx = g_mouseDX;
                float a = *g_tbl.w[pj] + dx, b = *g_tbl.w[i] - dx;
                if (a >= 20.0f && b >= 20.0f) { *g_tbl.w[pj] = a; *g_tbl.w[i] = b; }
            }
        } else if (over && g_pressed) { g_active = hid; g_focusClaimed = true; }
    }

    if (g_tbl.flags & Table_BordersOuter) tblOuterBorder(g_tbl.x0, top, g_tbl.fullW, bottom - top);
    if (g_sortSpecsFor == g_tbl.id && g_sortSpecsOut && g_tbl.store) g_tbl.store->dirty = g_sortSpecsOut->specsDirty && g_sortSpecsOut->SpecsDirty;
    g_sortSpecsFor = 0; g_sortSpecsOut = nullptr;
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
    wrapLines(text, availW(), g_theme.text);
}

bool PrimaryButton(const char* label) {
    if (!g_lay.active || !label) return false;
    const int id = hashLabel(label);
    const float w = visW(label) + 40.0f, h = rowH();
    float x, y; place(w, h, x, y);
    const bool inside = hit(x, y, w, h);
    g_lastId = id; if (inside) g_hot = id;
    bool clicked = false;
    if (g_active == id) { if (g_released) { if (inside) clicked = true; g_active = 0; } }
    else if (inside && g_pressed) g_active = id;
    unsigned char c[4] = {g_theme.accent[0], g_theme.accent[1], g_theme.accent[2], 255};
    if (g_active == id && inside) { c[0] = (unsigned char)(c[0] * 0.85f); c[1] = (unsigned char)(c[1] * 0.85f); c[2] = (unsigned char)(c[2] * 0.85f); }
    else if (inside) { c[0] = (unsigned char)(c[0] + (255 - c[0]) * 0.12f); c[1] = (unsigned char)(c[1] + (255 - c[1]) * 0.12f); c[2] = (unsigned char)(c[2] + (255 - c[2]) * 0.12f); }
    rrect(x, y, w, h, rnd(), c);
    const unsigned char white[4] = {255, 255, 255, 255};
    drawText(x + (w - visW(label)) * 0.5f, y + (h - textH()) * 0.5f, VisLabel(label).b, g_theme.textScale, white);
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
    const bool inside = hit(x, y, w, h);
    g_lastId = id; if (inside) g_hot = id;
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
    if (right && *right) { rw = labelW(right); drawText(x + w - rw - 10.0f, y + (h - textH()) * 0.5f, right, s, pick(Col_TextDisabled, g_theme.textDim)); }
    if (two) {
        drawText(tx, y + 6.0f, title ? title : "", s, g_theme.text);
        PushTextScale(0.8125f);
        drawText(tx, y + h - 6.0f - textH() - 1.0f, subtitle, g_theme.textScale, pick(Col_TextDisabled, g_theme.textDim));
        PopTextScale();
    } else drawText(tx, y + (h - textH()) * 0.5f, title ? title : "", s, g_theme.text);
    return clicked;
}

void KeyValue(const char* label, const char* value) {
    if (!g_lay.active) return;
    const float h = rowH(), w = fullW();
    float x, y; place(w, h, x, y);
    const float ty = y + (h - textH()) * 0.5f;
    if (label) drawText(x, ty, VisLabel(label).b, g_theme.textScale, pick(Col_TextDisabled, g_theme.textDim));
    if (value) drawText(x + w - labelW(value), ty, value, g_theme.textScale, g_theme.text);
}

void Spinner(float size) {
    if (!g_lay.active) return;
    const float d = size > 0.0f ? size : rowH();
    float x, y; place(d, d, x, y);
    const float cx = x + d * 0.5f, cy = y + d * 0.5f, r = d * 0.38f;
    const float a0 = (float)(okay::platform::TicksMs32() % 1000u) / 1000.0f * 6.2831853f;
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
    t.born = okay::platform::TicksMs32();
    t.until = t.born + (Uint32)((seconds > 0.5f ? seconds : 0.5f) * 1000.0f);
}


Rect GetItemRect() {
    Rect r;
    if (g_lay.active) { r.x = g_lay.prevX; r.y = g_lay.prevY; r.w = g_lay.prevW; r.h = g_lay.prevH; }
    return r;
}

int GetOverlayCount() { return g_ovCurN; }
Rect GetOverlayRect(int index) {
    if (index < 0 || index >= g_ovCurN) return Rect{};
    const OvRect& o = g_ovCur[index];
    return Rect{o.x, o.y, o.w, o.h};
}

unsigned GetID(const char* label) { return (unsigned)hashLabel(label ? label : ""); }

bool IsItemHovered() {
    return g_lay.active && hit(g_lay.prevX, g_lay.prevY, g_lay.prevW, g_lay.prevH);
}

bool WantsTextInput() { return g_focus != 0; }
const Input& GetFrameInput() { return g_in; }
int GetKeyboardFocusId() { return g_focus; }
void SetKeyboardFocusId(int id) { g_focus = id; if (id != 0) g_focusClaimed = true; }

// The clipboard: kept here, and mirrored to the system clipboard when a window system is up (so it works headless too).
static char g_clip[kClipboardMax] = {0};
bool PlatformClipboardSet(const char* text);    // OkayUICompat.cpp: the clipboard a platform feed installed in PlatformIO (false: none)
const char* PlatformClipboardGet();              // null: none installed
void SetClipboardText(const char* text) {
    if (!text) text = "";
    std::size_t n = std::strlen(text); if (n > (std::size_t)kClipboardMax - 1) n = (std::size_t)kClipboardMax - 1;
    std::memcpy(g_clip, text, n); g_clip[n] = 0;
    trimPartialChar(g_clip);
    if (PlatformClipboardSet(g_clip)) return;          // a platform feed owns the clipboard (no SDL)
    SDL_SetClipboardText(g_clip);                      // fails quietly with no video: the copy above is enough
}
const char* GetClipboardText() {
    if (const char* pc = PlatformClipboardGet()) { std::size_t n = std::strlen(pc); if (n > (std::size_t)kClipboardMax - 1) n = (std::size_t)kClipboardMax - 1; std::memcpy(g_clip, pc, n); g_clip[n] = 0; trimPartialChar(g_clip); return g_clip; }
    if (char* sys = SDL_GetClipboardText()) {          // someone else may have copied something since
        if (sys[0] && std::strcmp(sys, g_clip) != 0) {
            std::size_t n = std::strlen(sys); if (n > (std::size_t)kClipboardMax - 1) n = (std::size_t)kClipboardMax - 1;
            std::memcpy(g_clip, sys, n); g_clip[n] = 0; trimPartialChar(g_clip);
        }
        SDL_free(sys);
    }
    return g_clip;
}

// ---------------------------------------------------------------------------------------------
// Public draw list, cursor and measurement, item state, disabled blocks (docs/ui/own_ui_toolkit.md, section 4)
// ---------------------------------------------------------------------------------------------
namespace {

constexpr int kMaxPath = 256;
Vec2 g_path[kMaxPath];
int  g_pathN = 0;

inline bool finite1(float v) { return v == v && v > -1.0e7f && v < 1.0e7f; }
inline bool finite2(Vec2 p) { return finite1(p.x) && finite1(p.y); }

void unpack(Color32 c, unsigned char out[4]) {
    out[0] = (unsigned char)(c & 0xffu);         out[1] = (unsigned char)((c >> 8) & 0xffu);
    out[2] = (unsigned char)((c >> 16) & 0xffu); out[3] = (unsigned char)((c >> 24) & 0xffu);
}

// A drawing call of the public draw list: dimming off for its duration, overlay batch for the foreground list.
struct ListScope {
    bool prevRaw, prevOv;
    explicit ListScope(bool overlay) : prevRaw(g_rawDraw), prevOv(g_toOverlay) { g_rawDraw = true; g_toOverlay = overlay || prevOv; }   // the window list draws where the widgets around it do: in a popup, that is the overlay
    ~ListScope() { g_rawDraw = prevRaw; g_toOverlay = prevOv; }
};

// cos(pi / n) for n = 3..128, worked out once: the mitre of a regular n-gon's stroke.
float cosHalfTurn(int n) {
    static float t[129] = {};
    if (t[n] == 0.0f) t[n] = SDL_cosf(3.14159265f / (float)n);
    return t[n];
}

int circleSegments(float r, int requested) {
    if (requested >= 3) return requested > 128 ? 128 : requested;
    int n = 10 + (int)(r * 0.9f);
    return n < 12 ? 12 : (n > 64 ? 64 : n);
}

// Stroke a polyline as one quad strip with mitred joins (so a translucent stroke never double-blends at a
// joint). Consecutive duplicate points are dropped; a join sharper than the mitre limit is clamped.
void strokePath(const Vec2* in, int n, bool closed, float th, const unsigned char c[4]) {
    if (n < 2 || !(th > 0.0f) || !finite1(th)) return;
    Vec2 p[kMaxPath]; int m = 0;
    for (int i = 0; i < n && m < kMaxPath; ++i) {
        if (m > 0) {
            const float dx = in[i].x - p[m - 1].x, dy = in[i].y - p[m - 1].y;
            if (dx * dx + dy * dy < 1.0e-6f) continue;
        }
        p[m++] = in[i];
    }
    if (closed && m > 2) { const float dx = p[0].x - p[m - 1].x, dy = p[0].y - p[m - 1].y; if (dx * dx + dy * dy < 1.0e-6f) --m; }
    if (m < 2) { if (m == 1) quad(p[0].x - th * 0.5f, p[0].y - th * 0.5f, th, th, c); return; }
    if (m == 2) closed = false;
    const float half = th * 0.5f;
    Vec2 off[kMaxPath];
    auto segNormal = [&](int a, int b) {
        float dx = p[b].x - p[a].x, dy = p[b].y - p[a].y;
        const float l = SDL_sqrtf(dx * dx + dy * dy);
        return Vec2{ -dy / l, dx / l };
    };
    for (int i = 0; i < m; ++i) {
        const bool hasPrev = closed || i > 0, hasNext = closed || i < m - 1;
        const Vec2 nb = hasNext ? segNormal(i, (i + 1) % m) : segNormal(i - 1, i);
        const Vec2 na = hasPrev ? segNormal((i + m - 1) % m, i) : nb;
        float mx = na.x + nb.x, my = na.y + nb.y;
        const float ml = SDL_sqrtf(mx * mx + my * my);
        if (ml < 1.0e-4f) { off[i] = Vec2{ na.x * half, na.y * half }; continue; }   // a U-turn: no mitre
        mx /= ml; my /= ml;
        float k = mx * na.x + my * na.y;                  // cos of half the turn angle
        if (k < 0.34f) k = 0.34f;                         // mitre limit about 3x
        off[i] = Vec2{ mx * half / k, my * half / k };
    }
    const int segs = closed ? m : m - 1;
    for (int i = 0; i < segs; ++i) {
        const int j = (i + 1) % m;
        quadPts(p[i].x + off[i].x, p[i].y + off[i].y, p[j].x + off[j].x, p[j].y + off[j].y,
                p[j].x - off[j].x, p[j].y - off[j].y, p[i].x - off[i].x, p[i].y - off[i].y, c);
    }
}

void fillConvex(const Vec2* p, int n, const unsigned char c[4]) {
    if (n < 3) return;
    for (int i = 1; i + 1 < n; ++i) tri(p[0].x, p[0].y, p[i].x, p[i].y, p[i + 1].x, p[i + 1].y, c);
}

// The rounded-rectangle outline as a closed path (corner radius `rad`, arcs of 6 segments).
int roundedRectPath(Vec2* out, float x0, float y0, float x1, float y1, float rad) {
    const float w = x1 - x0, h = y1 - y0;
    if (rad > w * 0.5f) rad = w * 0.5f;
    if (rad > h * 0.5f) rad = h * 0.5f;
    int n = 0;
    if (rad <= 0.5f) {
        out[n++] = Vec2{ x0, y0 }; out[n++] = Vec2{ x1, y0 }; out[n++] = Vec2{ x1, y1 }; out[n++] = Vec2{ x0, y1 };
        return n;
    }
    // The corners' unit points, worked out once (the same float angles as before: the path is unchanged), not 56 sines and cosines
    // per rectangle.
    struct Arcs {
        float c[4][7], s[4][7];
        Arcs() {
            const float PI = 3.14159265f;
            const float a0[4] = { PI, 1.5f * PI, 0.0f, 0.5f * PI };
            for (int k = 0; k < 4; ++k)
                for (int i = 0; i <= 6; ++i) { const float a = a0[k] + (0.5f * PI) * (float)i / 6.0f; c[k][i] = SDL_cosf(a); s[k][i] = SDL_sinf(a); }
        }
    };
    static const Arcs A;
    const float cx[4] = { x0 + rad, x1 - rad, x1 - rad, x0 + rad }, cy[4] = { y0 + rad, y0 + rad, y1 - rad, y1 - rad };
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i <= 6; ++i) out[n++] = Vec2{ cx[k] + rad * A.c[k][i], cy[k] + rad * A.s[k][i] };
    return n;
}

} // namespace

Color32 GetColorU32(Col which, float alphaMul) {
    unsigned char tmp[4];
    const unsigned char* c = styleColor((int)which, tmp);
    float a = (float)c[3] * (alphaMul != alphaMul ? 0.0f : (alphaMul < 0.0f ? 0.0f : (alphaMul > 1.0f ? 1.0f : alphaMul)));
    return RGBA(c[0], c[1], c[2], (unsigned char)(a + 0.5f));
}

Color32 ColorMulAlpha(Color32 c, float alphaMul) {
    if (alphaMul != alphaMul) alphaMul = 0.0f;
    if (alphaMul < 0.0f) alphaMul = 0.0f;
    if (alphaMul > 1.0f) alphaMul = 1.0f;
    const unsigned a = (unsigned)((float)(c >> 24) * alphaMul + 0.5f);
    return (c & 0x00ffffffu) | (a << 24);
}

// ---- DrawList ---------------------------------------------------------------------------------
void DrawList::AddLine(Vec2 a, Vec2 b, Color32 col, float th) const {
    if (!finite2(a) || !finite2(b) || !finite1(th) || th <= 0.0f) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    line(a.x, a.y, b.x, b.y, th, c);
}

void DrawList::AddRectFilled(Vec2 mn, Vec2 mx, Color32 col, float rounding) const {
    if (!finite2(mn) || !finite2(mx) || mx.x <= mn.x || mx.y <= mn.y) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    rrect(mn.x, mn.y, mx.x - mn.x, mx.y - mn.y, finite1(rounding) ? rounding : 0.0f, c);
}

void DrawList::AddRect(Vec2 mn, Vec2 mx, Color32 col, float rounding, float th) const {
    if (!finite2(mn) || !finite2(mx) || mx.x <= mn.x || mx.y <= mn.y || !finite1(th) || th <= 0.0f) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    const float w = mx.x - mn.x, h = mx.y - mn.y;
    if (th * 2.0f >= w || th * 2.0f >= h) { quad(mn.x, mn.y, w, h, c); return; }   // the stroke fills the box
    if (!finite1(rounding) || rounding <= 0.5f) {   // four bars, drawn inside the rectangle, no overlap
        quad(mn.x, mn.y, w, th, c);
        quad(mn.x, mx.y - th, w, th, c);
        quad(mn.x, mn.y + th, th, h - 2.0f * th, c);
        quad(mx.x - th, mn.y + th, th, h - 2.0f * th, c);
        return;
    }
    Vec2 pts[32];
    const int n = roundedRectPath(pts, mn.x + th * 0.5f, mn.y + th * 0.5f, mx.x - th * 0.5f, mx.y - th * 0.5f,
                                  rounding - th * 0.5f > 0.0f ? rounding - th * 0.5f : 0.0f);
    strokePath(pts, n, true, th, c);
}

void DrawList::AddRectFilledMultiColor(Vec2 mn, Vec2 mx, Color32 tl, Color32 tr, Color32 br, Color32 bl) const {
    if (!finite2(mn) || !finite2(mx) || mx.x <= mn.x || mx.y <= mn.y) return;
    ListScope s(overlay); unsigned char a[4], b[4], c[4], d[4];
    unpack(tl, a); unpack(tr, b); unpack(br, c); unpack(bl, d);
    gradQuad(mn.x, mn.y, mx.x - mn.x, mx.y - mn.y, a, b, c, d);
}

void DrawList::AddCircleFilled(Vec2 center, float radius, Color32 col, int segments) const {
    if (!finite2(center) || !finite1(radius) || radius <= 0.0f) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    fillCircle(center.x, center.y, radius, c, circleSegments(radius, segments));
}

void DrawList::AddCircle(Vec2 center, float radius, Color32 col, int segments, float th) const {
    if (!finite2(center) || !finite1(radius) || radius <= 0.0f || !finite1(th) || th <= 0.0f) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    const int n = circleSegments(radius, segments);
    float r = radius - th * 0.5f; if (r < 0.0f) r = 0.0f;   // the stroke sits inside `radius`
    if (th >= radius) { fillCircle(center.x, center.y, radius, c, n); return; }
    // A regular polygon's stroke: every mitre is radial, half the thickness over cos(pi / n) long (what strokePath works out per
    // corner), so the ring is drawn straight from the cached unit points: no sines, cosines or square roots per call.
    const float* u = unitCircle(n);
    const float k = cosHalfTurn(n), ro = r + th * 0.5f / k, ri = r - th * 0.5f / k;
    float px = u[(n - 1) * 2], py = u[(n - 1) * 2 + 1];   // angle 2 pi: the first corner
    for (int i = 0; i < n; ++i) {
        const float qx = u[i * 2], qy = u[i * 2 + 1];
        quadPts(center.x + px * ro, center.y + py * ro, center.x + qx * ro, center.y + qy * ro,
                center.x + qx * ri, center.y + qy * ri, center.x + px * ri, center.y + py * ri, c);
        px = qx; py = qy;
    }
}

void DrawList::AddTriangleFilled(Vec2 a, Vec2 b, Vec2 c3, Color32 col) const {
    if (!finite2(a) || !finite2(b) || !finite2(c3)) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    tri(a.x, a.y, b.x, b.y, c3.x, c3.y, c);
}

void DrawList::AddTriangle(Vec2 a, Vec2 b, Vec2 c3, Color32 col, float th) const {
    if (!finite2(a) || !finite2(b) || !finite2(c3)) return;
    const Vec2 p[3] = { a, b, c3 };
    AddPolyline(p, 3, col, true, th);
}

void DrawList::AddQuadFilled(Vec2 a, Vec2 b, Vec2 c3, Vec2 d, Color32 col) const {
    if (!finite2(a) || !finite2(b) || !finite2(c3) || !finite2(d)) return;
    const Vec2 p[4] = { a, b, c3, d };
    AddConvexPolyFilled(p, 4, col);
}

void DrawList::AddQuad(Vec2 a, Vec2 b, Vec2 c3, Vec2 d, Color32 col, float th) const {
    const Vec2 p[4] = { a, b, c3, d };
    AddPolyline(p, 4, col, true, th);
}

void DrawList::AddPolyline(const Vec2* pts, int count, Color32 col, bool closed, float th) const {
    if (!pts || count < 2 || count > kMaxPath || !finite1(th) || th <= 0.0f) return;
    for (int i = 0; i < count; ++i) if (!finite2(pts[i])) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    strokePath(pts, count, closed, th, c);
}

void DrawList::AddPolyline(const Vec2* pts, int count, Color32 col, int flags, float th) const { AddPolyline(pts, count, col, (flags & DrawFlags_Closed) != 0, th); }

void DrawList::AddConcavePolyFilled(const Vec2* pts, int count, Color32 col) const {
    if (!pts || count < 3 || count > kMaxPath) return;
    for (int i = 0; i < count; ++i) if (!finite2(pts[i])) return;
    int idx[kMaxPath]; for (int i = 0; i < count; ++i) idx[i] = i;
    float area = 0.0f; for (int i = 0; i < count; ++i) { const Vec2 a = pts[i], b = pts[(i + 1) % count]; area += a.x * b.y - b.x * a.y; }
    const float sgn = area < 0.0f ? -1.0f : 1.0f;
    int n = count, guard = 0;
    auto cross = [&](Vec2 a, Vec2 b, Vec2 c) { return ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * sgn; };
    while (n > 3 && guard++ < count * count) {
        bool clipped = false;
        for (int i = 0; i < n && !clipped; ++i) {
            const int ia = idx[(i + n - 1) % n], ib = idx[i], ic = idx[(i + 1) % n];
            const Vec2 a = pts[ia], b = pts[ib], c = pts[ic];
            if (cross(a, b, c) <= 0.0f) continue;   // a reflex or flat corner is not an ear
            bool inside = false;
            for (int j = 0; j < n && !inside; ++j) {
                const int ip = idx[j]; if (ip == ia || ip == ib || ip == ic) continue;
                const Vec2 p = pts[ip];
                if (cross(a, b, p) >= 0.0f && cross(b, c, p) >= 0.0f && cross(c, a, p) >= 0.0f) inside = true;
            }
            if (inside) continue;
            const Vec2 tri[3] = {a, b, c};
            AddConvexPolyFilled(tri, 3, col);
            for (int j = i; j + 1 < n; ++j) idx[j] = idx[j + 1];
            --n; clipped = true;
        }
        if (!clipped) break;   // degenerate outline: stop rather than loop
    }
    if (n == 3) { const Vec2 tri[3] = {pts[idx[0]], pts[idx[1]], pts[idx[2]]}; AddConvexPolyFilled(tri, 3, col); }
}

void DrawList::AddConvexPolyFilled(const Vec2* pts, int count, Color32 col) const {
    if (!pts || count < 3 || count > kMaxPath) return;
    for (int i = 0; i < count; ++i) if (!finite2(pts[i])) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    fillConvex(pts, count, c);
}

void DrawList::AddBezierCubic(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, Color32 col, float th, int segments) const {
    if (!finite2(p1) || !finite2(p2) || !finite2(p3) || !finite2(p4)) return;
    int n = segments > 0 ? segments : 24;
    if (n > kMaxPath - 1) n = kMaxPath - 1;
    Vec2 pts[kMaxPath];
    for (int i = 0; i <= n; ++i) {
        const float t = (float)i / (float)n, u = 1.0f - t;
        const float a = u * u * u, b = 3.0f * u * u * t, cc = 3.0f * u * t * t, d = t * t * t;
        pts[i] = Vec2{ a * p1.x + b * p2.x + cc * p3.x + d * p4.x, a * p1.y + b * p2.y + cc * p3.y + d * p4.y };
    }
    AddPolyline(pts, n + 1, col, false, th);
}

void DrawList::AddText(Vec2 pos, Color32 col, const char* text, float scale) const {
    if (!text || !*text || !finite2(pos)) return;
    if (scale == 0.0f) scale = g_theme.textScale;
    else if (!(scale > 0.0f) || !finite1(scale)) return;
    ListScope s(overlay); unsigned char c[4]; unpack(col, c);
    drawText(pos.x, pos.y, text, scale, c);
}

void DrawList::AddImage(SDL_Texture* tex, Vec2 mn, Vec2 mx, Vec2 uv0, Vec2 uv1, Color32 tint) const {
    if (!tex || !finite2(mn) || !finite2(mx) || mx.x <= mn.x || mx.y <= mn.y) return;
    ListScope s(false); unsigned char c[4]; unpack(tint, c);   // textured geometry always goes to the main batch
    texQuad(mn.x, mn.y, mx.x - mn.x, mx.y - mn.y, tex, uv0.x, uv0.y, uv1.x, uv1.y, c);
}

// A textured convex polygon: a fan from point 0, each point with its own texture coordinate.
static void texPoly(SDL_Texture* tex, const Vec2* p, const Vec2* uv, int n, const unsigned char tint[4]) {
    if (g_cullRow || n < 3) return;
    setTexture(tex);
    if (g_nv + n > kMaxVerts || g_ni + (n - 2) * 3 > kMaxIdx) { noteDrop(); setTexture(g_defTex); return; }
    const SDL_Color sc = toColor(tint);
    const int base = g_nv;
    for (int i = 0; i < n; ++i) {
        SDL_Vertex v; v.color = sc; v.position.x = p[i].x; v.position.y = p[i].y; v.tex_coord.x = uv[i].x; v.tex_coord.y = uv[i].y;
        g_verts[g_nv++] = v;
    }
    for (int i = 1; i + 1 < n; ++i) { g_idx[g_ni++] = base; g_idx[g_ni++] = base + i; g_idx[g_ni++] = base + i + 1; }
    setTexture(g_defTex);
}

Vec2 Font::CalcTextSizeA(float size, float, float, const char* text, const char** remaining) const {
    if (remaining) *remaining = text ? text + std::strlen(text) : nullptr;
    if (!text || !*text || !(size > 0.0f) || !std::isfinite(size)) return Vec2{0, 0};
    const float scale = size / (height > 0 ? (float)height : 8.0f), cw = (float)width * scale;
    float best = 0.0f; int lines = 1, cur = 0;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') { if ((float)cur > best) best = (float)cur; cur = 0; ++lines; }
        else if (((unsigned char)*p & 0xC0) != 0x80) ++cur;   // count characters, not UTF-8 continuation bytes
    }
    if ((float)cur > best) best = (float)cur;
    return Vec2{best * cw, (float)lines * size};
}

void DrawList::AddText(const Font* font, float fontSize, Vec2 pos, Color32 col, const char* text, const char* textEnd, float, const ImVec4*) const {
    if (!text || !finite1(fontSize) || fontSize <= 0.0f) return;
    char buf[1024]; std::size_t n = textEnd && textEnd >= text ? (std::size_t)(textEnd - text) : std::strlen(text);
    if (n > sizeof buf - 1) n = sizeof buf - 1;
    std::memcpy(buf, text, n); buf[n] = 0;
    const Font* f = font ? font : GetFont();
    if (f && f->ttf) {   // a user font: draw with it for this call at `fontSize` pixels
        const Font* prev = GetFont(); SetFont(f); const float saved = g_theme.textScale; g_theme.textScale = fontSize / 8.0f;
        AddText(pos, col, buf, fontSize / 8.0f);
        g_theme.textScale = saved; SetFont(prev);
        return;
    }
    const float h = f && f->height > 0 ? (float)f->height : 8.0f;
    AddText(pos, col, buf, fontSize / h);
}

// A rounded rectangle as a closed convex path, clockwise from the top-left; `flags` 0 means all four corners, DrawFlags_None none.
static int rectPath(Vec2 mn, Vec2 mx, float rounding, int flags, Vec2* pts) {
    const float w = mx.x - mn.x, h = mx.y - mn.y, maxR = 0.5f * (w < h ? w : h);
    if (!std::isfinite(rounding) || rounding < 0.0f) rounding = 0.0f;
    if (rounding > maxR) rounding = maxR;
    if (flags == 0) flags = DrawFlags_RoundCornersAll;
    if (flags & DrawFlags_None) flags = 0;
    const int arc = circleSegments(rounding, 0) / 4 + 1 > 16 ? 16 : circleSegments(rounding, 0) / 4 + 1;
    struct C { float cx, cy, a0; int bit; float px, py; };
    const C cs[4] = { { mn.x + rounding, mn.y + rounding, 3.14159265f, DrawFlags_RoundCornersTopLeft, mn.x, mn.y },
                      { mx.x - rounding, mn.y + rounding, 4.71238898f, DrawFlags_RoundCornersTopRight, mx.x, mn.y },
                      { mx.x - rounding, mx.y - rounding, 0.0f, DrawFlags_RoundCornersBottomRight, mx.x, mx.y },
                      { mn.x + rounding, mx.y - rounding, 1.57079633f, DrawFlags_RoundCornersBottomLeft, mn.x, mx.y } };
    int n = 0;
    for (const C& c : cs) {
        if (rounding >= 0.5f && (flags & c.bit)) for (int i = 0; i <= arc; ++i) { const float a = c.a0 + 1.57079633f * (float)i / (float)arc; pts[n++] = Vec2{ c.cx + rounding * SDL_cosf(a), c.cy + rounding * SDL_sinf(a) }; }
        else pts[n++] = Vec2{c.px, c.py};
    }
    return n;
}
void DrawList::AddRectFilled(Vec2 mn, Vec2 mx, Color32 col, float rounding, int flags) const {
    if (!finite2(mn) || !finite2(mx) || mx.x <= mn.x || mx.y <= mn.y) return;
    if (flags == 0 || flags == DrawFlags_RoundCornersAll) { AddRectFilled(mn, mx, col, rounding); return; }
    Vec2 p[4 * 17]; const int n = rectPath(mn, mx, rounding, flags, p);
    AddConvexPolyFilled(p, n, col);
}
void DrawList::AddRect(Vec2 mn, Vec2 mx, Color32 col, float rounding, int flags, float th) const {
    if (!finite2(mn) || !finite2(mx) || mx.x <= mn.x || mx.y <= mn.y) return;
    if (flags == 0 || flags == DrawFlags_RoundCornersAll) { AddRect(mn, mx, col, rounding, th); return; }
    Vec2 p[4 * 17]; const int n = rectPath(mn, mx, rounding, flags, p);
    AddPolyline(p, n, col, true, th);
}

void DrawList::AddText(Vec2 pos, Color32 col, const char* text, const char* textEnd) const {
    if (!text) return;
    char buf[1024]; std::size_t n = textEnd && textEnd >= text ? (std::size_t)(textEnd - text) : std::strlen(text);
    if (n > sizeof buf - 1) n = sizeof buf - 1;
    std::memcpy(buf, text, n); buf[n] = 0;
    AddText(pos, col, static_cast<const char*>(buf), 0.0f);
}

void DrawList::AddImageQuad(SDL_Texture* tex, Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, Vec2 uv1, Vec2 uv2, Vec2 uv3, Vec2 uv4, Color32 tint) const {
    if (!tex || !finite2(p1) || !finite2(p2) || !finite2(p3) || !finite2(p4) || !finite2(uv1) || !finite2(uv2) || !finite2(uv3) || !finite2(uv4)) return;
    ListScope s(false); unsigned char c[4]; unpack(tint, c);
    const Vec2 p[4] = {p1, p2, p3, p4}, uv[4] = {uv1, uv2, uv3, uv4};
    texPoly(tex, p, uv, 4, c);
}

void DrawList::AddImageRounded(SDL_Texture* tex, Vec2 mn, Vec2 mx, Vec2 uv0, Vec2 uv1, Color32 tint, float rounding, int flags) const {
    if (!tex || !finite2(mn) || !finite2(mx) || !finite2(uv0) || !finite2(uv1) || mx.x <= mn.x || mx.y <= mn.y) return;
    if (!std::isfinite(rounding) || rounding < 0.0f) rounding = 0.0f;
    const float w = mx.x - mn.x, h = mx.y - mn.y;
    const float maxR = 0.5f * (w < h ? w : h);
    if (rounding > maxR) rounding = maxR;
    if (flags == 0) flags = DrawFlags_RoundCornersAll;
    if (flags & DrawFlags_None) flags = 0;
    if (rounding < 0.5f || (flags & DrawFlags_RoundCornersAll) == 0) { AddImage(tex, mn, mx, uv0, uv1, tint); return; }
    // Corners clockwise from the top-left; each rounded corner is an arc of `arc` steps, a square one is a single point.
    const int arc = circleSegments(rounding, 0) / 4 + 1 > 16 ? 16 : circleSegments(rounding, 0) / 4 + 1;
    Vec2 pts[4 * 17]; int n = 0;
    struct Corner { float cx, cy, a0; int bit; float px, py; };
    const Corner cs[4] = {
        { mn.x + rounding, mn.y + rounding, 3.14159265f,        DrawFlags_RoundCornersTopLeft,     mn.x, mn.y },
        { mx.x - rounding, mn.y + rounding, 4.71238898f,        DrawFlags_RoundCornersTopRight,    mx.x, mn.y },
        { mx.x - rounding, mx.y - rounding, 0.0f,               DrawFlags_RoundCornersBottomRight, mx.x, mx.y },
        { mn.x + rounding, mx.y - rounding, 1.57079633f,        DrawFlags_RoundCornersBottomLeft,  mn.x, mx.y },
    };
    for (const Corner& c : cs) {
        if (flags & c.bit) {
            for (int i = 0; i <= arc; ++i) {
                const float a = c.a0 + 1.57079633f * (float)i / (float)arc;
                pts[n++] = Vec2{ c.cx + rounding * SDL_cosf(a), c.cy + rounding * SDL_sinf(a) };
            }
        } else pts[n++] = Vec2{c.px, c.py};
    }
    Vec2 uv[4 * 17];
    for (int i = 0; i < n; ++i) {
        uv[i].x = uv0.x + (uv1.x - uv0.x) * (pts[i].x - mn.x) / w;
        uv[i].y = uv0.y + (uv1.y - uv0.y) * (pts[i].y - mn.y) / h;
    }
    ListScope s(false); unsigned char c[4]; unpack(tint, c);
    // A fan from the first point is convex-safe for a rounded rectangle, but the first point is on a corner arc: use the centre instead.
    Vec2 fan[4 * 17 + 2]; Vec2 fuv[4 * 17 + 2]; int m = 0;
    fan[m] = Vec2{ (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f };
    fuv[m] = Vec2{ (uv0.x + uv1.x) * 0.5f, (uv0.y + uv1.y) * 0.5f }; ++m;
    for (int i = 0; i < n; ++i) { fan[m] = pts[i]; fuv[m] = uv[i]; ++m; }
    fan[m] = pts[0]; fuv[m] = uv[0]; ++m;
    texPoly(tex, fan, fuv, m, c);
}

void DrawList::AddNgon(Vec2 center, float radius, Color32 col, int n, float th) const {
    if (!finite2(center) || !finite1(radius) || radius < 0.0f || n < 3) return;
    if (n > 128) n = 128;
    Vec2 p[128];
    for (int i = 0; i < n; ++i) { const float a = 6.28318531f * (float)i / (float)n; p[i] = Vec2{ center.x + radius * SDL_cosf(a), center.y + radius * SDL_sinf(a) }; }
    AddPolyline(p, n, col, true, th);
}
static int ellipsePoints(Vec2 c, Vec2 r, float rot, int seg, Vec2* p) {
    if (seg <= 0) seg = 32;
    if (seg < 3) seg = 3;
    if (seg > 128) seg = 128;
    const float cr = SDL_cosf(rot), sr = SDL_sinf(rot);
    for (int i = 0; i < seg; ++i) {
        const float a = 6.28318531f * (float)i / (float)seg, ex = r.x * SDL_cosf(a), ey = r.y * SDL_sinf(a);
        p[i] = Vec2{ c.x + ex * cr - ey * sr, c.y + ex * sr + ey * cr };
    }
    return seg;
}
void DrawList::AddEllipse(Vec2 c, Vec2 r, Color32 col, float rot, int seg, float th) const {
    if (!finite2(c) || !finite2(r) || !finite1(rot) || r.x < 0.0f || r.y < 0.0f) return;
    Vec2 p[128]; const int n = ellipsePoints(c, r, rot, seg, p);
    AddPolyline(p, n, col, true, th);
}
void DrawList::AddEllipseFilled(Vec2 c, Vec2 r, Color32 col, float rot, int seg) const {
    if (!finite2(c) || !finite2(r) || !finite1(rot) || r.x < 0.0f || r.y < 0.0f) return;
    Vec2 p[128]; const int n = ellipsePoints(c, r, rot, seg, p);
    AddConvexPolyFilled(p, n, col);
}

void DrawList::AddNgonFilled(Vec2 center, float radius, Color32 col, int n) const {
    if (!finite2(center) || !finite1(radius) || radius < 0.0f || n < 3) return;
    if (n > 128) n = 128;
    Vec2 p[128];
    for (int i = 0; i < n; ++i) { const float a = 6.28318531f * (float)i / (float)n; p[i] = Vec2{ center.x + radius * SDL_cosf(a), center.y + radius * SDL_sinf(a) }; }
    AddConvexPolyFilled(p, n, col);
}

void DrawList::PushClipRect(Vec2 mn, Vec2 mx) const {
    OkayUI::PushClipRect(mn.x, mn.y, mx.x - mn.x, mx.y - mn.y);
}
void DrawList::PushClipRect(Vec2 mn, Vec2 mx, bool) const { PushClipRect(mn, mx); }
void DrawList::PopClipRect() const { OkayUI::PopClipRect(); }

static void syncPath(const DrawList& d) { d._Path.clear(); for (int i = 0; i < g_pathN; ++i) d._Path.push_back(g_path[i]); }
void DrawList::PathClear() const { g_pathN = 0; _Path.clear(); }
void DrawList::PathLineTo(Vec2 p) const { if (g_pathN < kMaxPath && finite2(p)) g_path[g_pathN++] = p; syncPath(*this); }
void DrawList::PathArcTo(Vec2 center, float radius, float a0, float a1, int segments) const {
    if (!finite2(center) || !finite1(radius) || !finite1(a0) || !finite1(a1) || radius < 0.0f) return;
    int n = segments > 0 ? segments : circleSegments(radius, 0) / 4 + 1;
    if (n > 128) n = 128;
    for (int i = 0; i <= n; ++i) {
        const float a = a0 + (a1 - a0) * (float)i / (float)n;
        PathLineTo(Vec2{ center.x + radius * SDL_cosf(a), center.y + radius * SDL_sinf(a) });
    }
}
void DrawList::PathRect(Vec2 mn, Vec2 mx, float rounding, int) const {
    if (!finite2(mn) || !finite2(mx)) return;
    if (mx.x < mn.x) std::swap(mn.x, mx.x);
    if (mx.y < mn.y) std::swap(mn.y, mx.y);
    Vec2 pts[kMaxPath];
    const int n = roundedRectPath(pts, mn.x, mn.y, mx.x, mx.y, rounding > 0.0f && std::isfinite(rounding) ? rounding : 0.0f);
    for (int i = 0; i < n && g_pathN < kMaxPath; ++i) g_path[g_pathN++] = pts[i];
    syncPath(*this);
}
void DrawList::PathStroke(Color32 col, bool closed, float th) const {
    if (g_pathN >= 2) AddPolyline(g_path, g_pathN, col, closed, th);
    g_pathN = 0; _Path.clear();
}
void DrawList::PathFillConvex(Color32 col) const {
    if (g_pathN >= 3) AddConvexPolyFilled(g_path, g_pathN, col);
    g_pathN = 0; _Path.clear();
}

static const DrawListHandle g_windowList(false);
static const DrawListHandle g_foregroundList(true);
DrawListHandle::operator DrawList*() const { if (isNull) return nullptr; return const_cast<DrawListHandle*>(overlay ? &g_foregroundList : &g_windowList); }
// ---- raw geometry ----
Vec2 GetFontTexUvWhitePixel() { return Vec2{g_whiteU, g_whiteV}; }
namespace { SDL_Texture* g_texStack[8]; int g_texTop = 0; }
void DrawList::PrimReserve(int idxCount, int vtxCount) const {
    (void)idxCount; (void)vtxCount;
    _VtxCurrentIdx = (unsigned)(overlay ? g_onv : g_nv);
}
void DrawList::PrimWriteVtx(Vec2 pos, Vec2 uv, Color32 col) const {
    ListScope s(overlay);
    SDL_Vertex* V = g_toOverlay ? g_ovVerts : g_verts;
    int& nv = g_toOverlay ? g_onv : g_nv;
    if ((g_cullRow && !g_toOverlay)) return;
    if (nv + 1 > kMaxVerts) { noteDrop(); return; }
    unsigned char c[4]; unpack(col, c);
    SDL_Vertex v; v.color = toColor(c);
    v.position.x = std::isfinite(pos.x) ? pos.x : 0.0f; v.position.y = std::isfinite(pos.y) ? pos.y : 0.0f;
    v.tex_coord.x = std::isfinite(uv.x) ? uv.x : 0.0f; v.tex_coord.y = std::isfinite(uv.y) ? uv.y : 0.0f;
    V[nv++] = v;
}
void DrawList::PrimWriteIdx(ImDrawIdx idx) const {
    ListScope s(overlay);
    int* I = g_toOverlay ? g_ovIdx : g_idx;
    int& ni = g_toOverlay ? g_oni : g_ni;
    const int nv = g_toOverlay ? g_onv : g_nv;
    if ((g_cullRow && !g_toOverlay)) return;
    if (ni + 1 > kMaxIdx) { noteDrop(); return; }
    I[ni++] = idx < (unsigned)nv ? (int)idx : 0;   // an index past the vertices written is clamped (it would read garbage)
}
void DrawList::PrimVtx(Vec2 pos, Vec2 uv, Color32 col) const {
    PrimWriteIdx((ImDrawIdx)(overlay ? g_onv : g_nv));
    PrimWriteVtx(pos, uv, col);
}
void DrawList::PushTexture(const ImTextureRef& tex) const {
    ListScope s(overlay);
    if (g_texTop < 8) g_texStack[g_texTop++] = tex._TexID;
    bindTexture(tex._TexID);
}
void DrawList::PopTexture() const {
    ListScope s(overlay);
    if (g_texTop <= 0) return;
    --g_texTop;
    bindTexture(g_texTop > 0 ? g_texStack[g_texTop - 1] : g_defTex);
}

// ---- channels of the window list ----
// A split keeps, per channel, the triangles (index runs) and span changes drawn on it, relative to where the split began; the vertices stay
// where they are. Switching channel saves the current run, cuts the batch back to the split point and lays the other channel's run down
// again; a merge lays all runs down in channel order. Clip and texture state at the split is the state every channel starts from.
namespace {
struct SpanSave { int rel; bool on; SDL_Rect rect; SDL_Texture* tex; };
struct Chan { std::vector<int> idx; std::vector<SpanSave> spans; bool started = false; bool eOn = false; SDL_Rect eRect{0, 0, 0, 0}; SDL_Texture* eTex = nullptr; };
struct SplitState { bool on = false; int count = 1, cur = 0, baseIdx = 0, baseSpans = 0; bool bOn = false; SDL_Rect bRect{0, 0, 0, 0}; SDL_Texture* bTex = nullptr; Chan ch[8]; };
SplitState g_split;
void saveChan(Chan& c) {
    c.idx.assign(g_idx + g_split.baseIdx, g_idx + g_ni);
    c.spans.clear();
    for (int k = g_split.baseSpans; k < g_nspans; ++k) c.spans.push_back(SpanSave{g_spans[k].startIdx - g_split.baseIdx, g_spans[k].on, g_spans[k].rect, g_spans[k].tex});
    c.started = true; c.eOn = g_curClipOn; c.eRect = g_curClipRect; c.eTex = g_curTex;
}
void layChan(const Chan& c) {   // append a saved run at the end of the batch
    const int at = g_ni;
    if (g_ni + (int)c.idx.size() > kMaxIdx) { noteDrop(); return; }
    for (size_t k = 0; k < c.idx.size(); ++k) g_idx[g_ni++] = c.idx[k];
    for (const SpanSave& sp : c.spans) {
        if (g_nspans >= kMaxSpans) break;
        if (g_nspans > 0 && g_spans[g_nspans - 1].startIdx == at + sp.rel) { g_spans[g_nspans - 1] = DrawSpan{at + sp.rel, sp.on, sp.rect, sp.tex}; continue; }
        g_spans[g_nspans++] = DrawSpan{at + sp.rel, sp.on, sp.rect, sp.tex};
    }
}
void cutBack() { g_ni = g_split.baseIdx; g_nspans = g_split.baseSpans; }
}   // namespace
void ChannelsMergeAll() {
    if (!g_split.on) return;
    SplitState& s = g_split;
    saveChan(s.ch[s.cur]);
    cutBack();
    g_curClipOn = s.bOn; g_curClipRect = s.bRect; g_curTex = s.bTex;
    for (int i = 0; i < s.count; ++i) {
        if (!s.ch[i].started) continue;
        // each run is laid with the state its channel began in
        g_curClipOn = s.bOn; g_curClipRect = s.bRect; g_curTex = s.bTex; pushSpan();
        layChan(s.ch[i]);
    }
    const Chan& last = s.ch[s.count - 1].started ? s.ch[s.count - 1] : s.ch[s.cur];
    if (last.started) { g_curClipOn = last.eOn; g_curClipRect = last.eRect; g_curTex = last.eTex; }
    pushSpan();
    for (int i = 0; i < 8; ++i) s.ch[i] = Chan();
    s.on = false; s.count = 1; s.cur = 0;
    g_windowList._Splitter._Count = 1; g_windowList._Splitter._Current = 0;
}
void DrawList::ChannelsSplit(int count) const {
    if (overlay || g_split.on || count < 2 || count > 8) return;
    SplitState& s = g_split;
    s = SplitState();
    s.on = true; s.count = count; s.cur = 0; s.baseIdx = g_ni; s.baseSpans = g_nspans;
    s.bOn = g_curClipOn; s.bRect = g_curClipRect; s.bTex = g_curTex;
    _Splitter._Count = count; _Splitter._Current = 0;
}
void DrawList::ChannelsSetCurrent(int index) const {
    if (overlay || !g_split.on || index < 0 || index >= g_split.count || index == g_split.cur) return;
    SplitState& s = g_split;
    saveChan(s.ch[s.cur]);
    cutBack();
    g_curClipOn = s.bOn; g_curClipRect = s.bRect; g_curTex = s.bTex;
    Chan& n = s.ch[index];
    pushSpan();
    if (n.started) { layChan(n); g_curClipOn = n.eOn; g_curClipRect = n.eRect; g_curTex = n.eTex; pushSpan(); }
    // (the saved run of a channel is laid down again as the batch tail so its geometry keeps being appended after it)
    s.cur = index;
    _Splitter._Current = index;
}
void DrawList::ChannelsMerge() const { if (!overlay) ChannelsMergeAll(); }

const DrawListHandle& GetWindowDrawList() { return g_windowList; }
const DrawListHandle& GetForegroundDrawList() { return g_foregroundList; }

// ---- Measurement and cursor -------------------------------------------------------------------
Vec2 CalcTextSize(const char* text) {
    if (!text || !*text) return Vec2{ 0.0f, textH() };
    return Vec2{ labelW(text), textH() };
}
float GetTextLineHeight() { return textH(); }
float GetFontSize() { return textH(); }
float GetFrameHeight() { return rowH(); }
float GetTextLineHeightWithSpacing() { return textH() + g_lay.spacingY; }
float GetFrameHeightWithSpacing() { return rowH() + g_lay.spacingY; }

Vec2 GetCursorScreenPos() {
    if (!g_lay.active) return Vec2{};
    if (g_posOverride) return Vec2{ g_posX, g_posY };
    return g_lay.pendingSameLine ? Vec2{ cursorX(), g_lay.hasLineTop ? g_lay.lineTopY : g_lay.prevY } : Vec2{ leftEdge(), g_lay.cy };
}
void SetCursorScreenPos(Vec2 p) {
    if (!g_lay.active || !finite2(p)) return;
    // SameLine then SetCursorPosX (Dear ImGui's SameLine(offset)): still the same line, so the next line still starts below its tallest item.
    const bool onLine = g_lay.pendingSameLine && (p.y == g_lay.prevY || (g_lay.hasLineTop && p.y == g_lay.lineTopY));
    g_lay.pendingSameLine = false;
    g_lay.cx = p.x; if (!onLine) g_lay.cy = p.y;
    g_posOverride = true; g_posX = p.x; g_posY = p.y; g_posSameLine = onLine;
}
Vec2 GetCursorPos() {
    const Vec2 s = GetCursorScreenPos();
    return Vec2{ s.x - g_lay.ox, s.y - g_lay.oy };
}
void SetCursorPos(Vec2 p) { SetCursorScreenPos(Vec2{ g_lay.ox + p.x, g_lay.oy + p.y }); }
float GetCursorPosX() { return GetCursorPos().x; }
float GetCursorPosY() { return GetCursorPos().y; }
void SetCursorPosX(float x) { SetCursorScreenPos(Vec2{ g_lay.ox + x, GetCursorScreenPos().y }); }
void SetCursorPosY(float y) { SetCursorScreenPos(Vec2{ GetCursorScreenPos().x, g_lay.oy + y }); }
Vec2 GetContentRegionAvail() {
    if (!g_lay.active) return Vec2{};
    const Vec2 c = GetCursorScreenPos();
    const float w = g_lay.ox + g_lay.contentW - c.x, h = g_lay.bottom - c.y;
    return Vec2{ w > 0.0f ? w : 0.0f, h > 0.0f ? h : 0.0f };
}

// ---- Item state and mouse queries ---------------------------------------------------------------
bool InvisibleButton(const char* id, float w, float h) {
    if (!g_lay.active || !finite1(w) || !finite1(h)) return false;
    const int wid = hashLabel(id);
    if (w <= 0.0f) w = availW();
    if (h <= 0.0f) h = rowH();
    float x, y; place(w, h, x, y);
    g_lastId = wid;
    const bool inside = hit(x, y, w, h);
    if (inside) g_hot = wid;
    bool clicked = false;
    if (g_active == wid) { if (g_released) { if (inside) clicked = true; g_active = 0; } }
    else if (inside && g_pressed) g_active = wid;
    return clicked;
}
bool IsItemFocused()     { return g_lastId != 0 && g_focus == g_lastId; }          // the last item has the keyboard (a text field)
unsigned GetItemID()     { return (unsigned)g_lastId; }                              // 0 when the last item is not a widget with an id
bool IsAnyItemActive()   { return g_active != 0; }                                   // some widget is being held (pressed or dragged)
bool IsItemVisible() {                                                                // the last item overlaps the current clip rectangle
    if (!g_lay.active) return false;
    float x0 = 0.0f, y0 = 0.0f, x1 = 1e9f, y1 = 1e9f;
    if (g_clipTop > 0) { const SDL_Rect& c = g_clipStack[g_clipTop - 1]; x0 = (float)c.x; y0 = (float)c.y; x1 = (float)(c.x + c.w); y1 = (float)(c.y + c.h); }
    return g_lay.prevX < x1 && g_lay.prevX + g_lay.prevW > x0 && g_lay.prevY < y1 && g_lay.prevY + g_lay.prevH > y0;
}
bool IsRectVisible(Vec2 min, Vec2 max) {
    if (!g_lay.active) return false;
    float x0 = 0.0f, y0 = 0.0f, x1 = 1e9f, y1 = 1e9f;
    if (g_clipTop > 0) { const SDL_Rect& c = g_clipStack[g_clipTop - 1]; x0 = (float)c.x; y0 = (float)c.y; x1 = (float)(c.x + c.w); y1 = (float)(c.y + c.h); }
    return min.x < x1 && max.x > x0 && min.y < y1 && max.y > y0;
}
bool IsRectVisible(Vec2 size) { const Vec2 p = GetCursorScreenPos(); return IsRectVisible(p, Vec2{p.x + size.x, p.y + size.y}); }
bool IsAnyMouseDown() { return g_in.mouseDown || g_in.rightDown; }
bool IsItemActive()      { return g_lastId != 0 && g_active == g_lastId; }
bool IsItemActivated()   { return IsItemActive() && g_activeAtFrameStart != g_lastId; }
bool IsItemDeactivated() { return g_lastId != 0 && g_activeAtFrameStart == g_lastId && g_active != g_lastId; }
bool IsItemEdited() { return g_lastId != 0 && g_edFrameId == g_lastId; }
bool IsItemDeactivatedAfterEdit() {
    if (g_lastId == 0 || g_edStickyId != g_lastId) return false;
    return IsItemDeactivated() || (g_focusAtFrameStart == g_lastId && g_focus != g_lastId) || g_focusLostPrev == g_lastId;
}
float CalcItemWidth() { return g_lay.active ? availW() : 0.0f; }
bool BeginPopupContextWindow(const char* id) {
    const int pid = hashLabel(id);
    // A right click on the window's own background (over the window, not over any item or a window above) opens it at the pointer.
    if (g_lay.active && g_curWin.valid && g_rPressed && g_hot == 0 && g_active == 0 && IsWindowHovered()) {
        PopupSlot* p = popupSlot(pid, true);
        if (p) { p->open = true; p->x = g_in.mouseX; p->y = g_in.mouseY; p->w = 0.0f; g_openPopupThisFrame = pid; }
    }
    return beginPopupBody(pid);
}
bool IsItemClicked(int button) {
    if (!IsItemHovered()) return false;
    return button == 2 ? g_mPressed : (button == 1 ? g_rPressed : g_pressed);
}
bool IsMouseDown(int button)     { return !g_in.blocked && (button == 2 ? g_in.middleDown : (button == 1 ? g_in.rightDown : g_in.mouseDown)); }
bool IsMouseClicked(int button)  { return !g_in.blocked && (button == 2 ? g_mPressed : (button == 1 ? g_rPressed : g_pressed)); }
bool IsMouseReleased(int button) { return button == 2 ? g_mReleased : (button == 1 ? g_rReleased : g_released); }
Vec2 GetMousePos()   { return Vec2{ g_in.mouseX, g_in.mouseY }; }
Vec2 GetMouseDelta() { return Vec2{ g_mouseDX, g_mouseDY }; }
bool IsMouseHoveringRect(Vec2 mn, Vec2 mx) {
    return !g_in.blocked && pointIn(g_in.mouseX, g_in.mouseY, mn.x, mn.y, mx.x - mn.x, mx.y - mn.y);
}

// ---- Disabled blocks --------------------------------------------------------------------------
void BeginDisabled(bool disabled) {
    if (g_disTop >= 16) return;
    g_disStack[g_disTop++] = disabled;
    if (disabled) ++g_disCount;
}
void EndDisabled() {
    if (g_disTop <= 0) return;
    if (g_disStack[--g_disTop]) --g_disCount;
}
bool IsDisabled() { return g_disCount > 0; }

void ResetState() {
    g_nextNum = NextNumber(); for (auto& t : g_numText) t[0] = 0; g_numLast = 0;
    g_dockAware = false; g_dockRouted = false;
    g_split = SplitState(); g_windowList._Splitter._Count = 1; g_windowList._Splitter._Current = 0;
    UseLauncherLook(false);
    g_theme = Theme();
    g_font = &kFontDefault;
    g_in = Input();
    g_prevDown = g_pressed = g_released = g_rPrevDown = g_rPressed = g_rReleased = false;
    g_posOverride = false; g_lastId = g_activeAtFrameStart = 0; g_edFrameId = g_edStickyId = g_focusAtFrameStart = g_focusLostEnd = g_focusLostPrev = 0; g_disTop = g_disCount = 0; g_rawDraw = false; g_pathN = 0;
    g_mPrevDown = g_mPressed = g_mReleased = false;
    for (int i = 0; i < 3; ++i) g_dragOriginX[i] = g_dragOriginY[i] = 0.0f;
    for (TabOrder& t : g_tabOrders) t = TabOrder();
    g_posSameLine = false; g_centerNext = false;
    g_fieldFocus = 0; g_fieldFocusSeen = false;
    g_prevMouseX = g_prevMouseY = g_mouseDX = g_mouseDY = 0.0f;
    g_frameCount = 0; g_clock = 0.0; g_lastPressTime = -1.0; g_dblClick = false;
    for (int k = 0; k < KeyId_COUNT; ++k) { g_keyNow[k] = g_keyPrev[k] = false; g_keySince[k] = g_keyNextRepeat[k] = 0.0; }
    g_hot = g_active = g_focus = 0; g_fieldFocus = 0; g_fieldFocusSeen = false; g_focusClaimed = false; g_frame = 0; g_dragAccum = 0.0f;
    g_fieldPadL = g_fieldPadR = -1.0f;
    g_popupPadX = g_popupPadY = g_popupW = g_innerX = g_childPadY = -1.0f;
    g_wrapPos = -1.0f; g_hintText = nullptr; g_focusNextField = false; g_focusSelectAll = false; g_editId = 0; g_grpTop = 0; g_nextOpen = -1; g_toggledId = 0;
    g_openPopupThisFrame = 0;
    for (PopupSlot& p : g_popups) p = PopupSlot();
    for (WinSlot& w : g_wins) w = WinSlot();
    for (OpenSlot& o : g_opens) o = OpenSlot();
    for (FloatSlot& f : g_floats) f = FloatSlot();
    for (TextSlot& t : g_texts) t = TextSlot();
    g_dd = DragDrop(); g_tdd = TypedDrag(); g_tddExt = g_tddExtSeen = g_tddExtReleased = false; g_curMenu = CurMenu(); g_inMenuBar = false;
    g_lay = LayoutState(); g_col = ColumnState(); g_tbl = TableState(); g_cullRow = false;
    for (TblStore& t : g_tstore) t = TblStore();
    g_sortSpecsFor = 0; g_sortSpecsOut = nullptr;
    g_childTop = g_popupTop = g_colTop = g_idTop = g_idSkipped = g_itemWTop = g_clipTop = 0;
    g_textScaleTop = 0; g_ncards = 0; g_ntoasts = 0;
    g_idSeed = 0; g_nextItemW = -1.0f; g_pinNext = false; g_bareNext = false; g_resizeNext = false; g_noTitleNext = false;
    DockReset();
    StateStorageReset();
    ColorPickerReset();
    g_ovCurN = g_ovPrevN = 0;
    g_popCurN = g_popPrevN = 0;
    g_wCurN = g_wPrevN = 0; g_zCounter = 0; g_nwrec = 0; g_wrecOpen = -1; g_hitWinOn = false; g_we = WinEnd(); g_wheelUsed = false; g_wflagsNext = 0;
    g_ime = ImeRect();
    g_nv = g_ni = g_onv = g_oni = g_novSpans = g_nspans = 0; g_toOverlay = false; g_dropped = 0;
}

// ---- Edit tracking: every value widget below reports through noteEdit (see IsItemEdited) ----
bool Checkbox(const char* label, bool* value) { return noteEdit(Checkbox_raw(label, value)); }
bool RadioButton(const char* label, int* value, int option) { return noteEdit(RadioButton_raw(label, value, option)); }
bool ToggleSwitch(const char* label, bool* value) { return noteEdit(ToggleSwitch_raw(label, value)); }
bool SliderFloat(const char* label, float* value, float minV, float maxV) { return noteEdit(SliderFloat_raw(label, value, minV, maxV)); }
bool SliderInt(const char* label, int* value, int minV, int maxV) { return noteEdit(SliderInt_raw(label, value, minV, maxV)); }
bool VSliderFloat(const char* label, float w, float h, float* value, float minV, float maxV) { return noteEdit(VSliderFloat_raw(label, w, h, value, minV, maxV)); }
bool Knob(const char* label, float* value, float minV, float maxV, float size) { return noteEdit(Knob_raw(label, value, minV, maxV, size)); }
// With InputText_EnterReturnsTrue the return value is the Enter, not an edit: TextField marks the edits itself.
bool InputText(const char* label, char* buf, int cap, unsigned flags) { const bool r = InputText_raw(label, buf, cap, flags); return (flags & InputText_EnterReturnsTrue) ? r : noteEdit(r); }
bool InputInt(const char* label, int* value, int step) { return noteEdit(InputInt_raw(label, value, step)); }
bool Combo(const char* label, const char* const* items, int count, int* current) { return noteEdit(Combo_raw(label, items, count, current)); }
bool DragFloat(const char* label, float* v, float speed, float minV, float maxV) { return noteEdit(DragFloat_raw(label, v, speed, minV, maxV)); }
bool DragInt(const char* label, int* v, float speed, int minV, int maxV) { return noteEdit(DragInt_raw(label, v, speed, minV, maxV)); }
bool DragFloat2(const char* label, float v[2], float speed, float minV, float maxV) { return noteEdit(DragFloat2_raw(label, v, speed, minV, maxV)); }
bool DragFloat3(const char* label, float v[3], float speed, float minV, float maxV) { return noteEdit(DragFloat3_raw(label, v, speed, minV, maxV)); }
bool DragFloat4(const char* label, float v[4], float speed, float minV, float maxV) { return noteEdit(DragFloat4_raw(label, v, speed, minV, maxV)); }
bool DragInt2(const char* label, int v[2], float speed, int minV, int maxV) { return noteEdit(DragInt2_raw(label, v, speed, minV, maxV)); }
bool DragInt3(const char* label, int v[3], float speed, int minV, int maxV) { return noteEdit(DragInt3_raw(label, v, speed, minV, maxV)); }
bool DragInt4(const char* label, int v[4], float speed, int minV, int maxV) { return noteEdit(DragInt4_raw(label, v, speed, minV, maxV)); }
bool DragIntRange2(const char* label, int* lo, int* hi, float speed, int minV, int maxV) { return noteEdit(DragIntRange2_raw(label, lo, hi, speed, minV, maxV)); }
bool DragFloatRange2(const char* label, float* lo, float* hi, float speed, float minV, float maxV) { return noteEdit(DragFloatRange2_raw(label, lo, hi, speed, minV, maxV)); }
bool InputTextWithHint(const char* label, const char* hint, char* buf, int cap, unsigned flags) { const bool r = InputTextWithHint_raw(label, hint, buf, cap, flags); return (flags & InputText_EnterReturnsTrue) ? r : noteEdit(r); }
bool SliderFloat2(const char* label, float v[2], float minV, float maxV) { return noteEdit(SliderFloat2_raw(label, v, minV, maxV)); }
bool SliderFloat3(const char* label, float v[3], float minV, float maxV) { return noteEdit(SliderFloat3_raw(label, v, minV, maxV)); }
bool SliderFloat4(const char* label, float v[4], float minV, float maxV) { return noteEdit(SliderFloat4_raw(label, v, minV, maxV)); }
bool InputFloat(const char* label, float* value, float step) { return noteEdit(InputFloat_raw(label, value, step)); }
bool ColorEdit4(const char* label, float rgba[4], int flags) { return noteEdit(ColorEdit4_raw(label, rgba, flags)); }
bool ColorEdit3(const char* label, float rgb[3], int flags) { return noteEdit(ColorEdit3_raw(label, rgb, flags)); }
bool ListBox(const char* label, int* current, const char* const* items, int count, int visibleRows) { return noteEdit(ListBox_raw(label, current, items, count, visibleRows)); }

// ---- Formatted value fields, framed sliders and the search field (docs/own_ui_toolkit.md section 10) ---------------------------
// Written from what Dear ImGui's documentation and the editor's panels show: a drag field is a frame with its value in the middle,
// formatted with a printf format; a slider with a format is a frame with a grab block and the value in the middle; the editor's
// search field has a magnifier at the left and a clear button at the right. Our own code; no code from any other project.
namespace {
// A printf float format we can trust: exactly one conversion, of a floating type, and nothing that writes (%n). Anything else is "%.3f".
const char* safeFloatFormat(const char* f, bool isInt) {
    const char* dflt = isInt ? "%d" : "%.3f";
    if (!f || !*f) return dflt;
    int conv = 0;
    for (const char* q = f; *q; ++q) {
        if (*q != '%') continue;
        if (q[1] == '%') { ++q; continue; }
        ++q;
        while (*q && std::strchr("-+ #0123456789.", *q)) ++q;
        if (!*q) return dflt;
        const bool ok = isInt ? (*q == 'd' || *q == 'i') : (*q == 'f' || *q == 'g' || *q == 'e' || *q == 'F' || *q == 'G' || *q == 'E');
        if (!ok) return dflt;
        ++conv;
    }
    return conv <= 1 ? f : dflt;   // no number in it ("-", a mixed value): shown as it is, as Dear ImGui shows such a format
}
void formatValue(char* out, std::size_t n, const char* fmt, double v, bool isInt) {
    const char* f = safeFloatFormat(fmt, isInt);
    if (isInt) std::snprintf(out, n, f, (int)v); else std::snprintf(out, n, f, v);
}
// Dear ImGui's field: a rounded frame in the frame colours (no border), lighter when hovered, lighter still while held.
void frameField(float x, float y, float w, float h, bool hovered, bool held) {
    rrect(x, y, w, h, rnd(), held ? pick(Col_FrameBgActive, g_theme.bgDown) : hovered ? pick(Col_FrameBgHovered, g_theme.bgHover) : pick(Col_FrameBg, g_theme.field));
}
void centredText(float x, float y, float w, float h, const char* t, const unsigned char* col) {
    const float tw = labelW(t);
    PushClipRect(x, y, w, h);
    drawText(x + std::max(2.0f, (w - tw) * 0.5f), y + (h - textH()) * 0.5f, t, g_theme.textScale, col);
    PopClipRect();
}
// `n` (1..4) formatted drag cells in a row sharing a label: each scrubs like DragFloat and can be Ctrl+clicked to type a number.
// The decimals a number format shows ("%.1f" 1, "%.0f deg" 0); 3 when it names none.
int formatDecimals(const char* format) {
    const char* p = format ? std::strchr(format, '%') : nullptr;
    if (!p) return 3;
    while (*++p && (std::strchr("-+ #0", *p) || std::isdigit((unsigned char)*p))) {}
    if (*p != '.') return 3;   // no precision: the toolkit's "%.3f"
    int d = 0;
    while (std::isdigit((unsigned char)*++p)) d = d * 10 + (*p - '0');
    return d > 9 ? 9 : d;
}

// A logarithmic drag (SliderFlags_Logarithmic on a drag): the pointer moves a position t from 0 to 1 along the range's logarithmic scale,
// `speed` per pixel taken as that share of the whole range (speed / (max - min)), so a wide range needs a long drag. A range from 0 starts
// its scale at the smallest step the format shows (0.1 for "%.1f"); below that the value is the minimum. Worked out from Dear ImGui's
// documented behaviour and checked against it in okayui_inspector_imgui; ranges that cross zero stay linear.
struct LogScale {
    bool ok = false; double lo = 0, hi = 0, floorV = 0;
    LogScale(float minV, float maxV, const char* format) {
        if (!(minV < maxV) || minV < 0.0f || !std::isfinite(maxV)) return;
        const double step = std::pow(10.0, -formatDecimals(format));
        floorV = minV; lo = minV > 0.0f ? (double)minV : std::min(step, (double)maxV * 0.5); hi = maxV;
        ok = hi > lo && lo > 0.0;
    }
    double T(double v) const { return v <= lo ? 0.0 : v >= hi ? 1.0 : std::log(v / lo) / std::log(hi / lo); }
    double V(double t) const { return t <= 0.0 ? floorV : t >= 1.0 ? hi : lo * std::pow(hi / lo, t); }
};

bool fmtDragCells(const char* label, float* v, int n, float speed, float minV, float maxV, const char* format, bool logScale = false) {
    if (!g_lay.active || !v || n < 1 || n > 4) return false;
    const LogScale ls(minV, maxV, format);
    if (logScale && !ls.ok) logScale = false;
    static double s_logT = 0.0;   // the held drag's place on the logarithmic scale (kept so rounding to the format does not stop it)
    const VisLabel vis(label);
    const int base = hashLabel(label);
    const float h = rowH(), gap = g_innerX >= 0.0f ? g_innerX : 4.0f, lgap = g_innerX >= 0.0f ? g_innerX : 8.0f;
    const float lw = labelW(vis.b) > 0.0f ? labelW(vis.b) + lgap : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    float ctrlW = w - lw; if (ctrlW < 24.0f * (float)n) ctrlW = 24.0f * (float)n;
    const float cw = (ctrlW - gap * (float)(n - 1)) / (float)n;
    bool changed = false;
    for (int i = 0; i < n; ++i) {
        const float cx = x + (float)i * (cw + gap);
        const int id = n == 1 ? base : base + (i + 1) * 7919;
        const bool inside = hit(cx, y, cw, h);
        double typed = 0.0;
        // Ctrl+click or a double click types a number (Dear ImGui's drag fields take both)
        const bool dbl = inside && g_pressed && IsMouseDoubleClicked(0);
        const int e = numberEdit(id, cx, y, cw, h, inside, (double)v[i], false, minV < maxV ? (double)minV : 0.0, minV < maxV ? (double)maxV : 0.0, &typed, !dbl);
        if (e == 2) { const float nv = (float)typed; if (std::isfinite(nv) && nv != v[i]) { v[i] = nv; changed = true; } }
        if (e != 0) continue;
        g_lastId = id; if (inside) g_hot = id;
        // Dear ImGui's drag threshold: the value follows the pointer only once it has moved 3 pixels from the press (half of
        // io.MouseDragThreshold), so a click does not nudge it; from then on every pixel counts.
        static bool s_pastThreshold = false;
        if (g_active == id) {
            if (!g_in.mouseDown) g_active = 0;
            else {
                if (!s_pastThreshold) {   // at least 3 pixels from where it was pressed
                    const float ddx = g_in.mouseX - g_dragOriginX[0], ddy = g_in.mouseY - g_dragOriginY[0];
                    s_pastThreshold = ddx * ddx + ddy * ddy >= 9.0f;
                }
                if (s_pastThreshold && g_mouseDX != 0.0f && std::isfinite(g_mouseDX) && std::isfinite(speed) && logScale) {
                    s_logT = std::min(1.0, std::max(0.0, s_logT + (double)g_mouseDX * (double)speed / ((double)maxV - (double)minV)));
                    const double k = std::pow(10.0, formatDecimals(format));
                    const float nv = (float)(std::floor(ls.V(s_logT) * k + 0.5) / k);
                    if (std::isfinite(nv) && nv != v[i]) { v[i] = nv; changed = true; }
                } else if (s_pastThreshold && g_mouseDX != 0.0f && std::isfinite(g_mouseDX) && std::isfinite(speed)) {
                    float nv = v[i] + g_mouseDX * speed;
                    if (minV < maxV) nv = nv < minV ? minV : (nv > maxV ? maxV : nv);
                    if (std::isfinite(nv) && nv != v[i]) { v[i] = nv; changed = true; }
                }
            }
        } else if (inside && g_pressed && g_disCount == 0) {
            g_active = id; g_focusClaimed = true; s_pastThreshold = false;
            if (logScale) s_logT = ls.T((double)v[i]);
        }
        frameField(cx, y, cw, h, inside, g_active == id);
        char buf[64]; formatValue(buf, sizeof buf, format, (double)v[i], false);
        centredText(cx, y, cw, h, buf, g_theme.text);
    }
    if (lw > 0.0f) drawText(x + ctrlW + lgap, y + (h - textH()) * 0.5f, vis.b, g_theme.textScale, g_theme.text);
    g_lay.prevX = x; g_lay.prevW = w;   // GetItemRect: the whole row
    return changed;
}
// A framed slider box at (x, y): the frame, a grab block (at least 16 pixels, or one step of an integer range), the formatted value in
// the middle; Ctrl+click types a number. True when *v changed.
bool fmtSliderBox(int id, float x, float y, float ctrlW, float h, double* v, bool isInt, double minV, double maxV, const char* format,
                  bool logScale = false) {
    if (logScale && !(minV > 0.0 && maxV > minV)) logScale = false;   // a logarithmic scale needs a range of positive numbers
    const bool inside = hit(x, y, ctrlW, h);
    bool changed = false;
    double typed = 0.0;
    const int e = numberEdit(id, x, y, ctrlW, h, inside, *v, isInt, minV, maxV, &typed);
    if (e == 2 && typed != *v) { *v = typed; changed = true; }
    if (e == 0) {
        const float pad = 2.0f, track = ctrlW - 2.0f * pad;
        const double range = maxV - minV;
        float grab = 16.0f;
        if (isInt && range < 1e6) grab = std::max(grab, track / (float)(range + 1.0));
        grab = std::min(grab, track);
        g_lastId = id; if (inside) g_hot = id;
        auto fromMouse = [&] {
            const float t = track > grab ? clamp01((g_in.mouseX - x - pad - grab * 0.5f) / (track - grab)) : 0.0f;
            double nv = logScale ? minV * std::pow(maxV / minV, (double)t) : minV + (double)t * range;
            if (isInt) nv = std::floor(nv + 0.5);
            if (nv != *v) { *v = nv; changed = true; }
        };
        if (g_active == id) { if (!g_in.mouseDown) g_active = 0; else fromMouse(); }
        else if (inside && g_pressed && g_disCount == 0) { g_active = id; g_focusClaimed = true; fromMouse(); }
        frameField(x, y, ctrlW, h, inside, g_active == id);
        const float t = logScale ? (float)std::min(1.0, std::max(0.0, std::log(std::max(*v, minV) / minV) / std::log(maxV / minV)))
                                 : (float)std::min(1.0, std::max(0.0, (*v - minV) / range));
        const float gx = x + pad + t * (track - grab);
        rrect(gx, y + pad, grab, h - 2.0f * pad, std::max(0.0f, rnd() - pad), g_active == id ? pick(Col_SliderGrabActive, g_theme.text) : pick(Col_SliderGrab, g_theme.accent));
        char buf[64]; formatValue(buf, sizeof buf, format, *v, isInt);
        centredText(x, y, ctrlW, h, buf, g_theme.text);
    }
    return changed;
}
// A framed slider with its label after it.
bool fmtSlider(const char* label, double* v, bool isInt, double minV, double maxV, const char* format, bool logScale = false) {
    if (!g_lay.active || !v || !(maxV > minV)) return false;
    const VisLabel vis(label);
    const int id = hashLabel(label);
    const float h = rowH(), lgap = g_innerX >= 0.0f ? g_innerX : 8.0f;
    const float lw = labelW(vis.b) > 0.0f ? labelW(vis.b) + lgap : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    float ctrlW = w - lw; if (ctrlW < 32.0f) ctrlW = 32.0f;
    const bool changed = fmtSliderBox(id, x, y, ctrlW, h, v, isInt, minV, maxV, format, logScale);
    if (lw > 0.0f) drawText(x + ctrlW + lgap, y + (h - textH()) * 0.5f, vis.b, g_theme.textScale, g_theme.text);
    return changed;
}
// `n` (2..4) framed slider cells in a row sharing a label, StyleVar_ItemInnerSpacing (x) apart (default 4), as fmtDragCells lays them out.
bool fmtSliderCells(const char* label, float* v, int n, float minV, float maxV, const char* format) {
    if (!g_lay.active || !v || n < 2 || n > 4 || !(maxV > minV)) return false;
    const VisLabel vis(label);
    const int base = hashLabel(label);
    const float h = rowH(), gap = g_innerX >= 0.0f ? g_innerX : 4.0f, lgap = g_innerX >= 0.0f ? g_innerX : 8.0f;
    const float lw = labelW(vis.b) > 0.0f ? labelW(vis.b) + lgap : 0.0f;
    const float w = availW();
    float x, y; place(w, h, x, y);
    float ctrlW = w - lw; if (ctrlW < 32.0f * (float)n) ctrlW = 32.0f * (float)n;
    const float cw = (ctrlW - gap * (float)(n - 1)) / (float)n;
    bool changed = false;
    for (int i = 0; i < n; ++i) {
        double d = std::isfinite(v[i]) ? (double)v[i] : (double)minV;
        if (fmtSliderBox(base + (i + 1) * 7919, x + (float)i * (cw + gap), y, cw, h, &d, false, (double)minV, (double)maxV, format)) {
            const float nv = (float)std::min((double)maxV, std::max((double)minV, d));
            if (nv != v[i]) { v[i] = nv; changed = true; }
        }
    }
    if (lw > 0.0f) drawText(x + ctrlW + lgap, y + (h - textH()) * 0.5f, vis.b, g_theme.textScale, g_theme.text);
    g_lay.prevX = x; g_lay.prevW = w;   // GetItemRect: the whole row
    return changed;
}
} // namespace

bool FramedNumbersOn() { return g_framedWidgets; }
bool FramedDragFloatN(const char* label, float* v, int n, float speed, float minV, float maxV, const char* format, bool logarithmic) {
    return noteEdit(fmtDragCells(label, v, n, speed, minV, maxV, format, logarithmic));
}
bool FramedSliderInt(const char* label, int* v, int minV, int maxV, const char* format) {
    if (!v) return false;
    double d = (double)*v;
    if (!fmtSlider(label, &d, true, (double)minV, (double)maxV, format)) return false;
    const int nv = (int)std::min((double)maxV, std::max((double)minV, d));
    if (nv == *v) return false;
    *v = nv; return noteEdit(true);
}
bool FramedSliderFloatN(const char* label, float* v, int n, float minV, float maxV, const char* format) { return noteEdit(fmtSliderCells(label, v, n, minV, maxV, format)); }
bool FramedSliderFloat(const char* label, float* v, float minV, float maxV, const char* format, bool logarithmic) {
    if (!v) return false;
    double d = (double)*v;
    if (!fmtSlider(label, &d, false, (double)minV, (double)maxV, format, logarithmic)) return false;
    const float nv = (float)std::min((double)maxV, std::max((double)minV, d));
    if (nv == *v) return false;
    *v = nv; return noteEdit(true);
}

bool SearchField(const char* id, const char* hint, char* buf, int cap, float width) {
    if (!g_lay.active || !buf || cap < 1) return false;
    const float h = rowH();
    float w = width > 0.0f && std::isfinite(width) ? width : availW() + (std::isfinite(width) ? width : 0.0f);
    if (w < h * 3.0f) w = h * 3.0f;
    float x, y; place(w, h, x, y);
    buf[cap - 1] = '\0';
    const bool hadText = buf[0] != '\0';
    g_hintText = hint;
    g_fieldPadL = h * 0.95f;                              // the text starts after the magnifier
    g_fieldPadR = hadText ? h * 0.95f : -1.0f;            // and stops before the clear button
    const int fid = hashLabel(id);
    bool changed = TextField(fid, x, y, w, h, buf, cap, 0);
    g_hintText = nullptr; g_fieldPadL = g_fieldPadR = -1.0f;
    const unsigned char* dim = pick(Col_TextDisabled, g_theme.textDim);
    {   // the magnifier: a ring up and to the left of the icon's centre, a handle to the lower right
        const float sz = h * 0.42f, cx = x + h * 0.5f, cy = y + h * 0.5f, r = sz * 0.27f;
        const float ox = cx - sz * 0.08f, oy = cy - sz * 0.08f;
        GetWindowDrawList().AddCircle(Vec2{ox, oy}, r + 0.8f, RGBA(dim[0], dim[1], dim[2], dim[3]), 14, 1.6f);
        line(ox + r * 0.7f, oy + r * 0.7f, cx + sz * 0.4f, cy + sz * 0.4f, 2.2f, dim);
    }
    if (buf[0]) {   // the clear button over the right end: a cross, brighter under the pointer; a click empties the field
        const float xc = x + w - h * 0.5f, yc = y + h * 0.5f, r = h * 0.17f;
        const bool hot = hit(xc - h * 0.45f, yc - h * 0.45f, h * 0.9f, h * 0.9f);
        const unsigned char* c = hot ? g_theme.text : dim;
        line(xc - r, yc - r, xc + r, yc + r, 1.6f, c);
        line(xc - r, yc + r, xc + r, yc - r, 1.6f, c);
        if (hot && g_pressed) {
            buf[0] = '\0'; changed = true;
            if (g_focus == fid) g_focus = 0;
            g_focusClaimed = true;   // the press was on the clear button, not empty space
        }
    }
    g_lastId = fid;
    return changed;
}


} // namespace OkayUI

// ---- Capture (OkayUICapture.hpp): stub, the test comes first ----------------------------------------------------------------
#include "okay/UI/OkayUICapture.hpp"
namespace OkayUI {
void BeginCapture() {}
void EndCapture(CapturedGeometry& out) { out.Clear(); }
bool Capturing() { return false; }
bool GetTexturePixels(const void*, TexturePixels&) { return false; }
} // namespace OkayUI
