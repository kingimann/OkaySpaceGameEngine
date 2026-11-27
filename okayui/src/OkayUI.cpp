#include "OkayUI_ContextState.hpp"
#include <algorithm>
#include <functional>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUIDock.hpp"
#include "okay/UI/OkayUIInternals.hpp"
#include "OkayUIInternal.hpp"
#include "okay/UI/OkayUICompat.hpp"   // BeginTooltip, GetStyle, ListClipper (defined here: they need the private layout state)
#include "okay/Graphics/Font.hpp"   // okay::Font8x8 — a no-STL C API over a bitmap font
#include "okay/UI/OkayUIDesign.hpp"   // the design in force (DefaultTheme)
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
namespace ui_core {

// Geometry that did not fit is counted, not hidden: DrawData::dropped, DroppedGeometry() and a message on stderr.
int g_dropped = 0;
bool g_rowClipper = true;   // the table row clipper (SetTableRowClipping)
bool g_cullRow = false;   // a table row wholly outside the clip: its geometry is skipped, not emitted (see TableNextRow)
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
OverlaySpan g_ovSpans[kMaxOvSpans];
int g_clipBase = 0;
int         g_novSpans = 0;
SDL_Texture* fallbackTex(okay::TtfFont* f) {
    for (int i = 0; i < g_nfb; ++i) if (g_fbFonts[i] == f) return (SDL_Texture*)(void*)f;
    if (g_nfb == 8) return nullptr;
    g_fbFonts[g_nfb++] = f;
    return (SDL_Texture*)(void*)f;
}

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
MenuKb g_menuKbs[16];
MenuKb* kbSlot(int id) {
    for (auto& s : g_menuKbs) if (s.id == id) return &s;
    for (auto& s : g_menuKbs) if (s.id == 0) { s = MenuKb(); s.id = id; return &s; }
    g_menuKbs[0] = MenuKb(); g_menuKbs[0].id = id; return &g_menuKbs[0];   // table full: reuse the first slot
}
CurMenu g_curMenu;
CurMenu g_menuStack[4];
TabSel g_tabSel[16];
TabOrder g_tabOrders[16];
TabBarCtx g_tabBar;
// Hover delay tracking (IsItemHovered with the Delay / Stationary flags) and the tooltip scope.
unsigned g_hoverKey = 0; unsigned long long g_hoverFrame = 0; double g_hoverStart = 0.0;
bool g_inTip = false;
                          // the menus a nested BeginMenu came from (outermost first)
int g_menuDepth = 0;
Theme g_theme = design::DefaultTheme();

// Scoped color-override stack: each entry remembers a theme color slot and its prior
// value, so PopStyleColor can restore it. Reset every BeginFrame to survive imbalance.
ColorSave g_colStack[64];
int       g_colTop = 0;
// The base-theme value of a colour that was never set (what GetStyleColorVec4 answers): the design's tokens.
void deriveColor(int id, unsigned char out[4]) {
    auto cp = [&](const unsigned char* c) { out[0] = c[0]; out[1] = c[1]; out[2] = c[2]; out[3] = c[3]; };
    auto rgba = [&](int r, int g, int b, int a) { out[0] = (unsigned char)r; out[1] = (unsigned char)g; out[2] = (unsigned char)b; out[3] = (unsigned char)a; };
    auto tint = [&](const unsigned char* c, int a) { out[0] = c[0]; out[1] = c[1]; out[2] = c[2]; out[3] = (unsigned char)a; };
    switch (id) {
        case Col_TextDisabled: cp(g_theme.textDim); break;
        case Col_WindowBg: case Col_ChildBg: cp(g_theme.panel); break;
        case Col_PopupBg: cp(g_theme.overlay); break;
        case Col_BorderShadow: rgba(0, 0, 0, 0); break;
        case Col_FrameBg: case Col_FrameBgHovered: case Col_FrameBgActive: cp(g_theme.field); break;
        case Col_TitleBg: case Col_TitleBgActive: case Col_TitleBgCollapsed: cp(g_theme.titleBg); break;
        case Col_MenuBarBg: cp(g_theme.track); break;
        case Col_ScrollbarBg: cp(g_theme.track); break;
        case Col_ScrollbarGrab: tint(g_theme.ink, 46); break;
        case Col_ScrollbarGrabHovered: tint(g_theme.ink, 90); break;
        case Col_ScrollbarGrabActive: cp(g_theme.accent); break;
        case Col_CheckMark: cp(g_theme.accent); break;
        case Col_SliderGrab: cp(g_theme.accent); break;
        case Col_SliderGrabActive: cp(g_theme.text); break;
        case Col_Header: cp(g_theme.raised); break;
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
        case Col_TableHeaderBg: cp(g_theme.raised); break;
        case Col_TableBorderStrong: case Col_TableBorderLight: cp(g_theme.border); break;
        case Col_TableRowBg: rgba(0, 0, 0, 0); break;
        case Col_TableRowBgAlt: tint(g_theme.ink, 9); break;
        case Col_TextSelectedBg: cp(g_theme.selection); break;
        case Col_DragDropTarget: cp(g_theme.accent); break;
        case Col_NavHighlight: cp(g_theme.focusRing); break;
        case Col_NavWindowingHighlight: rgba(255, 255, 255, 178); break;
        case Col_NavWindowingDimBg: rgba(204, 204, 204, 51); break;
        case Col_ModalWindowDimBg: rgba(0, 0, 0, g_theme.lightScheme ? 90 : 150); break;
        default: rgba(0, 0, 0, 0); break;
    }
}

// Length of a C string without pulling in <cstring> (keeps the toolkit STL/libc-light).
inline int cstrlen(const char* s) { int n = 0; if (s) while (s[n]) ++n; return n; }

LayoutState g_lay;
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
ChildSave g_childStack[8];
int       g_childTop = 0;

ColumnState g_col;

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
TableState g_tbl;
int g_sortSpecsFor = 0; TableSortSpecs* g_sortSpecsOut = nullptr;   // the specs struct TableGetSortSpecs handed out (EndTable reads specsDirty back from it)

// Popup body layout context (uses LayoutState, so defined after it).
PopupCtx g_popupStack[kPopupStackMax];
int      g_popupTop = 0;
extern int g_textScaleTop;   // defined with the text-scale stack below (reset here in BeginFrame)
extern int g_ncards;         // defined with the card stack below (reset here in BeginFrame)

// Drag & drop: a single int payload. `armed` records a press on a source before it
// has moved far enough to count as a drag; `active` means a drag is in flight.
DragDrop g_dd;

// Draggable-window position store (no STL): position persists across frames per id.
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
int hashLabel(const char* s) {
    // Dear ImGui's documented "###" rule: "Text###id" is identified by the "###id" part alone, so the text may change (a count in a title)
    // and the window or item stays the same one.
    if (s) { const char* k = std::strstr(s, "###"); if (k) s = k; }
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
float fMeasure(const char* s) {
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
float g_childRounding = -1.0f;   // StyleVar_ChildRounding / GetStyle().ChildRounding: the corners of children (< 0: the frame rounding)
// A pushed StyleVar_WindowPadding (y; < 0 = none pushed): a framed or padded child (the flags form of BeginChild) then pads its content by
// the window padding (x, y) instead of 6 pixels each way.
float g_childPadY = -1.0f;
// ImGui's convention for labels: what follows "##" is only part of the widget id and is not shown.
// Width of a label's visible part (what follows "##" is not shown, so it takes no room).

// The indented content left edge and full indented content width.
// Item-width override: SetNextItemWidth (one-shot) / PushItemWidth (scoped). availW()
// returns this instead of the full remaining width when set, so callers can size a
// widget explicitly. Always clamped to what actually remains on the line.
float g_nextItemW = -1.0f;
float g_itemWStack[16];
int   g_itemWTop = 0;
int   g_winItemWDepth = 0;   // item-width stack height just above the default SetWindowItemWidthDefault pushed (0: none pushed)
// Remaining width from the next item's left edge to the content's right edge.
float availW() {
    float real = g_lay.ox + g_lay.contentW - cursorX();
    float over = g_nextItemW > 0.0f ? g_nextItemW : (g_itemWTop > 0 ? g_itemWStack[g_itemWTop - 1] : -1.0f);
    if (over > 0.0f && over < real) return over;
    return real;
}

// True when SetNextItemWidth / PushItemWidth set a width for the next item. In Dear ImGui that width is the FIELD's, the label comes on top of it;
// the labelled widgets that take it that way add their label's room to it (see BeginCombo).
// Reserve a w*h slot for the next item; returns its top-left and advances the cursor
// to the next line (SameLine() keeps it on the current line instead).
bool g_prevInOverlay = false;
void place(float w, float h, float& x, float& y) {
    g_prevInOverlay = false;
    bool sameLine = false;
    const float lineH = g_lay.prevH;   // the item before, for CenterNextOnLine
    if (g_posOverride) {   // SetCursorScreenPos: this item goes exactly there
        g_posOverride = false;
        x = g_posX; y = g_posY; g_lay.pendingSameLine = false; g_lay.sameLineSpacing = -1.0f; g_lay.sameLineAbs = false;
        sameLine = g_posSameLine; g_posSameLine = false;
    } else if (g_lay.pendingSameLine) {
        float sp = g_lay.sameLineSpacing >= 0.0f ? g_lay.sameLineSpacing : g_lay.spacingX;
        x = g_lay.sameLineAbs ? g_lay.sameLineX : g_lay.prevX + g_lay.prevW + sp;
        g_lay.sameLineAbs = false;
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
void fillCircle(float cx, float cy, float radius, const unsigned char c[4], int segments) {
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

// Text with characters the atlas lacks: every glyph from the engine font `f`, bound
// to its atlas (a span of its own; in the overlay, an overlay span).
void bindTexture(SDL_Texture* t) {
    if (!g_toOverlay) { setTexture(t); return; }
    if (g_novSpans > 0 && g_ovSpans[g_novSpans - 1].startIdx == g_oni) { g_ovSpans[g_novSpans - 1].tex = t; return; }
    OverlaySpan s = g_novSpans > 0 ? g_ovSpans[g_novSpans - 1] : OverlaySpan{g_oni, t};   // the clip in force goes on
    s.startIdx = g_oni; s.tex = t;
    if (g_novSpans < kMaxOvSpans) g_ovSpans[g_novSpans++] = s;
}
namespace {
// The overlay's clip from here on (a clipped child inside a popup body). The texture in force goes on.
void overlayClipSet(bool on, int x, int y, int w, int h) {
    OverlaySpan* s = nullptr;
    if (g_novSpans > 0 && g_ovSpans[g_novSpans - 1].startIdx == g_oni) s = &g_ovSpans[g_novSpans - 1];
    else if (g_novSpans < kMaxOvSpans) {
        const auto tex = g_novSpans > 0 ? g_ovSpans[g_novSpans - 1].tex : g_defTex;
        s = &g_ovSpans[g_novSpans++]; *s = OverlaySpan{g_oni, tex};
    }
    if (s) { s->clipOn = on; s->clipX = x; s->clipY = y; s->clipW = w; s->clipH = h; }
}
}   // namespace
void overlayClipOff() { overlayClipSet(false, 0, 0, 0, 0); }
void overlayClipToStack() {
    if (g_clipTop <= g_clipBase) { overlayClipOff(); return; }
    const auto& r = g_clipStack[g_clipTop - 1];
    overlayClipSet(true, r.x, r.y, r.w, r.h);
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
void chevron(float cx, float cy, float sz, bool down, const unsigned char c[4]) { chevronDir(cx, cy, sz, down ? 1 : 0, c); }
// A chevron in any of four directions (0 right, 1 down, 2 left, 3 up): two strokes meeting at the point.
void chevronDir(float cx, float cy, float sz, int dir, const unsigned char c[4]) {
    const float h = sz * 0.5f, t = sz < 12.0f ? 1.6f : 2.0f;
    switch (dir & 3) {
        case 1: line(cx - h, cy - h * 0.5f, cx, cy + h * 0.5f, t, c); line(cx, cy + h * 0.5f, cx + h, cy - h * 0.5f, t, c); break;
        case 3: line(cx - h, cy + h * 0.5f, cx, cy - h * 0.5f, t, c); line(cx, cy - h * 0.5f, cx + h, cy + h * 0.5f, t, c); break;
        case 2: line(cx + h * 0.5f, cy - h, cx - h * 0.5f, cy, t, c); line(cx - h * 0.5f, cy, cx + h * 0.5f, cy + h, t, c); break;
        default: line(cx - h * 0.5f, cy - h, cx + h * 0.5f, cy, t, c); line(cx + h * 0.5f, cy, cx - h * 0.5f, cy + h, t, c); break;
    }
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
// Soft drop shadow under a rounded rect, as deep as the design asks (Theme::shadowDepth).
void softShadow(float x, float y, float w, float h, float rad) { elevation(x, y, w, h, rad, g_theme.shadowDepth); }
// The shadow of a raised surface: `depth` 0 (none) to 3 (deep). Widening translucent layers, offset downwards (light from
// above), lighter on light schemes. The alphas were picked by eye at 1x and 2x.
void elevation(float x, float y, float w, float h, float rad, int depth) {
    if (depth <= 0) return;
    if (depth > 3) depth = 3;
    const int layers = 2 + depth;
    const float spread = 1.0f + 1.0f * depth, drop = 1.0f + 2.0f * depth;   // whole pixels: the same edges at every scale
    const float base = g_theme.lightScheme ? 7.0f : 15.0f;
    for (int i = layers; i >= 1; --i) {
        const float g = spread * i;
        const float a = base * (1.0f + 0.35f * depth) * (float)(layers + 1 - i) / (float)layers;
        const unsigned char c[4] = {0, 0, 0, (unsigned char)(a > 255.0f ? 255.0f : a)};
        rrect(x - g, y - g + drop, w + 2.0f * g, h + 2.0f * g, rad + g, c);
    }
}
// An outline of thickness `t` around a rounded rectangle (four bars and the corner arcs), drawn outside nothing: it covers the
// rectangle's own edge band, so call it after the fill. Corners as short lines along the arc.
void ringRect(float x, float y, float w, float h, float rad, float t, const unsigned char c[4]) {
    if (w <= 0.0f || h <= 0.0f || t <= 0.0f || c[3] == 0) return;
    if (rad > w * 0.5f) rad = w * 0.5f;
    if (rad > h * 0.5f) rad = h * 0.5f;
    if (rad < t) rad = 0.0f;
    quad(x + rad, y, w - 2.0f * rad, t, c);
    quad(x + rad, y + h - t, w - 2.0f * rad, t, c);
    quad(x, y + rad, t, h - 2.0f * rad, c);
    quad(x + w - t, y + rad, t, h - 2.0f * rad, c);
    if (rad <= 0.0f) return;
    const float r = rad - t * 0.5f;
    const float cx[4] = {x + rad, x + w - rad, x + w - rad, x + rad}, cy[4] = {y + rad, y + rad, y + h - rad, y + h - rad};
    const float a0[4] = {3.14159265f, 4.71238898f, 0.0f, 1.57079633f};
    const int n = rad > 8.0f ? 6 : 4;
    for (int q = 0; q < 4; ++q)
        for (int i = 0; i < n; ++i) {
            const float t0 = a0[q] + 1.57079633f * i / n, t1 = a0[q] + 1.57079633f * (i + 1) / n;
            line(cx[q] + r * std::cos(t0), cy[q] + r * std::sin(t0), cx[q] + r * std::cos(t1), cy[q] + r * std::sin(t1), t, c);
        }
}
// The keyboard focus ring: the design's ring colour, Theme::focusRingPx thick, 2 pixels outside the control's edge.
void focusRing(float x, float y, float w, float h, float rad) {
    const float t = g_theme.focusRingPx, gap = 2.0f;
    ringRect(x - gap - t, y - gap - t, w + 2.0f * (gap + t), h + 2.0f * (gap + t), rad > 0.0f ? rad + gap + t : 0.0f, t, g_theme.focusRing);
}
// A control's box: the design's outline when it has one (Theme::controlBorders), then the fill.
void controlBox(float x, float y, float w, float h, float rad, const unsigned char* fill) {
    const float b = g_theme.borderPx;
    if (g_theme.controlBorders && w > 2.0f * b && h > 2.0f * b) {
        rrect(x, y, w, h, rad, g_theme.border);
        rrect(x + b, y + b, w - 2.0f * b, h - 2.0f * b, rad > b ? rad - b : 0.0f, fill);
        return;
    }
    rrect(x, y, w, h, rad, fill);
}
// `a` blended toward `b` by t (0..1), into `out`.
const unsigned char* mixColor(const unsigned char a[4], const unsigned char b[4], float t, unsigned char out[4]) {
    t = clamp01(t);
    for (int i = 0; i < 4; ++i) out[i] = (unsigned char)(a[i] + (b[i] - a[i]) * t + 0.5f);
    return out;
}
// ---- Motion: short, time-based transitions per widget --------------------------------------------------------------------------
// A widget asks anim(id, channel, on) every frame and gets 0..1 that eases toward 1 while `on` (hover, press, open) and back
// to 0 after. The time comes from Input::time: a host with no clock (time 0, as in the headless tests) and Theme::motionMs <= 0
// (the person's "reduce motion") get the end state at once, so pictures in tests never depend on timing.
namespace {
struct AnimSlot { int key = 0; float v = 0.0f; unsigned long long seen = 0; };
AnimSlot g_anims[96];
double g_animPrevT = 0.0, g_animDt = 0.0;
}
void animFrame(double now) {
    g_animDt = (now > 0.0 && g_animPrevT > 0.0 && now > g_animPrevT) ? now - g_animPrevT : 0.0;
    if (g_animDt > 0.25) g_animDt = 0.25;
    g_animPrevT = now > 0.0 ? now : 0.0;
}
float anim(int id, int channel, bool on) {
    if (g_theme.motionMs <= 0.0f || g_in.time <= 0.0) return on ? 1.0f : 0.0f;
    const int key = (id * 31 + channel) | 1;
    AnimSlot* s = nullptr; AnimSlot* freeS = nullptr;
    for (AnimSlot& a : g_anims) {
        if (a.key == key) { s = &a; break; }
        if (!freeS && (a.key == 0 || a.seen + 2 < g_frameCount)) freeS = &a;
    }
    if (!s) {
        if (!on) return 0.0f;                 // at rest and not asked to move: no slot needed
        if (!freeS) return 1.0f;
        s = freeS; s->key = key; s->v = 0.0f;
    }
    s->seen = g_frameCount;
    const float step = (float)(g_animDt * 1000.0 / g_theme.motionMs);
    s->v = on ? (s->v + step > 1.0f ? 1.0f : s->v + step) : (s->v - step < 0.0f ? 0.0f : s->v - step);
    const float v = s->v;
    if (!on && v <= 0.0f) s->key = 0;         // back at rest: free the slot
    return v * v * (3.0f - 2.0f * v);         // smooth start and end
}
// `c` with its alpha scaled (0..1), into `out`.
const unsigned char* withAlpha(const unsigned char c[4], float a, unsigned char out[4]) {
    out[0] = c[0]; out[1] = c[1]; out[2] = c[2]; out[3] = (unsigned char)(c[3] * a);
    return out;
}


// ---- Mouse hit testing with overlays ---------------------------------------------
// Overlays (combo lists, popups, menu dropdowns) draw on top of later widgets, so
// they must also take the mouse from the widgets under them. Each open overlay
// registers its rectangle; widgets test against LAST frame's rectangles (one frame of
// lag, like any immediate-mode toolkit). A "modal" overlay (combo list, popup) makes
// every ordinary widget inert while it is open, so the click that closes it is not
// passed on to whatever is below. Widgets inside a popup body, and the overlay
// owners themselves (hitOverlay), are not affected.
OvRect g_ovCur[16];  int g_ovCurN = 0;
OvRect g_ovPrev[16]; int g_ovPrevN = 0;
// The open popups' boxes with their depth in the popup stack (1 = opened from a window), this frame's and last frame's. A widget in a
// popup body does not take the pointer where a popup opened from it (deeper, drawn above it) covered it on the last frame: a press on
// a right-click menu opened from a row of a list must not also click the list rows under that menu.
PopRect g_popCur[8], g_popPrev[8]; int g_popCurN = 0, g_popPrevN = 0;
// ---- Window stacking (z-order) -------------------------------------------------------------------
// Every window has a stamp (WinSlot::z): higher draws in front, a click raises it, 0 is the bottom (docked content). The windows
// begun this frame register their rectangles (g_wCur); last frame's rectangles (g_wPrev) cover the windows not begun yet. A
// widget of a window ignores the pointer where a window with a higher stamp covers it, so a click never reaches what is behind.
// The geometry follows the stamps: the windows' index ranges are put in stamp order when the frame ends (reorderWindows).
WinRect g_wCur[64], g_wPrev[64]; int g_wCurN = 0, g_wPrevN = 0;
unsigned long long g_zCounter = 0;
bool g_wheelUsed = false;   // a scrolling child took this frame's wheel (the window does not scroll as well)
bool g_escapeUsed = false;  // a popup closed on this frame's Escape (the popups it was opened from stay open)
bool g_hitWinOn = false; int g_hitWinId = 0; unsigned long long g_hitWinZ = 0;   // the window being built, for hit()
WinRec g_wrec[kMaxWinRecs]; int g_nwrec = 0; int g_wrecOpen = -1;
// trade places, so a frame whose stamps agree with the call order is left exactly as it was.
void reorderWindows() {
    wrecEnd();
    const int n = g_nwrec;
    if (n < 2) return;
    int ord[kMaxWinRecs];
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
// Is the mouse inside the current clip rectangle? A widget scrolled out of a child region (or past
// the edge of a clipped window) is invisible, so it must not take clicks either.
bool hit(float x, float y, float w, float h) {
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

constexpr int kMaxHexSlots = 16;

} // namespace
using namespace ui_core;


void ApplyStyleWrites();
void ApplyFontGlobalScale();
void BeginFrame(const Input& in) {
    ApplyStyleWrites();   // what Dear ImGui code wrote into GetStyle() since (see GetStyle)
    ApplyFontGlobalScale();
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
    animFrame(in.time);
    treeGuidesReset();
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
    g_inMenuBar = false; menuBarReset(); regionReset(); g_popupSubOpen = 0; g_curMenu.active = false; g_menuDepth = 0; tabBarsReset();
    g_idSeed = 0; g_idTop = 0; g_idSkipped = 0;   // reset the ID stack each frame
    // Restore any colors left pushed by an imbalanced previous frame, then clear.
    PopStyleColor(g_colTop);
    g_colTop = 0;
    while (g_textScaleTop > 0) PopTextScale();   // an unbalanced PushTextScale never leaks into the next frame
    g_ncards = 0;                        // reset card stack (BeginCard reserves per frame)
    g_nextItemW = -1.0f; g_itemWTop = 0; g_winItemWDepth = 0;   // reset item-width overrides
    g_nspans = 0; g_clipTop = 0; g_clipBase = 0;           // reset draw spans/stack
    g_curClipOn = false; g_curTex = g_defTex;
    g_childTop = 0;                        // reset child-region stack
    g_col.active = false;                  // reset column layout
    g_tbl.active = false;                  // reset table layout
}

float ui_core::g_btnAlignX = 0.5f;   // StyleVar_ButtonTextAlign (x): 0 left, 0.5 centred, 1 right, inside a 12-pixel padding
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
    const float pad = g_imguiCoords && g_framedWidgets && g_framePadX >= 0.0f ? g_framePadX : (g_look ? 9.0f : 6.0f);   // Dear ImGui's rules (UseImGuiCoordinates): a pushed FramePadding.x is where the text starts
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
        // The design's field: the field colour (a touch lighter under the pointer), an outline when the design has them or the
        // field has the keyboard (then in the accent), and the focus ring around it while it has the keyboard.
        unsigned char hv[4], st[4];
        mixColor(g_theme.field, g_theme.ink, 0.04f, hv);
        const unsigned char* fill = stateColor(id, inside && !focused, false, pick(Col_FrameBg, g_theme.field), pick(Col_FrameBgHovered, hv), hv, st);
        if (focused) fill = pick(Col_FrameBgActive, g_theme.field);
        const bool edge = focused || g_theme.controlBorders;
        if (edge && w > 2.0f * b && h > 2.0f * b) {
            rrect(x, y, w, h, rd, focused ? g_theme.accent : g_theme.border);
            rrect(x + b, y + b, w - 2.0f * b, h - 2.0f * b, rd > b ? rd - b : 0.0f, fill);
        } else rrect(x, y, w, h, rd, fill);
        if (focused) focusRing(x, y, w, h, rd);
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
        if (len == 0 && g_hintText && g_hintText[0]) drawText(tx, ty, g_hintText, s, pick(Col_TextDisabled, g_theme.textFaint));   // the placeholder, while the field is empty
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
        while (sp < g_novSpans && g_ovSpans[sp].startIdx <= j) {
            const OverlaySpan& s = g_ovSpans[sp++];
            g_curClipOn = s.clipOn; g_curClipRect.x = s.clipX; g_curClipRect.y = s.clipY; g_curClipRect.w = s.clipW; g_curClipRect.h = s.clipH;
            g_curTex = s.tex; pushSpan();
        }
        if (g_ovIdx[j] >= fit || g_ovIdx[j + 1] >= fit || g_ovIdx[j + 2] >= fit) { noteDrop(); continue; }
        g_idx[g_ni++] = base + g_ovIdx[j]; g_idx[g_ni++] = base + g_ovIdx[j + 1]; g_idx[g_ni++] = base + g_ovIdx[j + 2];
    }
    g_onv = 0; g_oni = 0; g_novSpans = 0;
}
// End-of-frame interaction bookkeeping, shared by every backend.
void finalizeFrame() {
    captureFrameEnd();
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
        // A raised card in the overlay colour with the design's shadow, a status bar on its left edge and a dot in the status
        // colour (accent: information, success, danger); it slides in from the right and fades.
        const unsigned char bgc[4] = {g_theme.overlay[0], g_theme.overlay[1], g_theme.overlay[2], (unsigned char)(250 * a)};
        unsigned char rim[4]; withAlpha(g_theme.border, a, rim);
        const unsigned char* sc = t.kind == 1 ? g_theme.success : t.kind == 2 ? g_theme.danger : g_theme.accent;
        const unsigned char dot[4] = {sc[0], sc[1], sc[2], (unsigned char)(255 * a)};
        const unsigned char txt[4] = {g_theme.text[0], g_theme.text[1], g_theme.text[2], (unsigned char)(255 * a)};
        const float rd = std::min(g_theme.windowRounding, 12.0f);
        if (a > 0.5f) elevation(x, y, w, h, rd, g_theme.shadowDepth);
        rrect(x, y, w, h, rd, rim);
        rrect(x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, rd > 1.0f ? rd - 1.0f : 0.0f, bgc);
        rrect(x + 1.0f, y + 6.0f, 3.0f, h - 12.0f, 1.5f, dot);
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

namespace { float g_userSavedScale = 0.0f; }   // textScale before a user font set its own (0: none active)
void SetFont(const Font* f) {
    if (g_font->ttf && g_userSavedScale > 0.0f) { g_theme.textScale = g_userSavedScale; g_userSavedScale = 0.0f; }   // leaving a user font gives the scale back
    g_font = f ? f : &kFontDefault;
    if (g_font->ttf && g_font->px > 0.0f) { g_userSavedScale = g_theme.textScale; g_theme.textScale = g_font->px / 8.0f; }   // the line is px tall: 8 cell units times px / 8
}
Font* GetFont()             { return const_cast<Font*>(g_font); }
const Font* FontDefault()   { return &kFontDefault; }
const Font* FontBold()      { return &kFontBold; }

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
} // namespace
namespace ui_core { bool ioTypedTextConsumed() { return g_ioQueueFrame == (long long)g_frameCount && g_io.InputQueueCharacters.Size < g_ioQueueFilled; } }
namespace {
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
namespace {
bool g_mergeMine[KeyId_COUNT] = {};          // keys whose state came from events last time
bool g_mergeMineCtrl = false, g_mergeMineShift = false;
int g_mergeReleaseNext[2] = {0, 0};          // frames the press is still shown after a one-frame click (a click must last two frames: the first presses, the second is when a widget sees the pointer held)
}
void MergePendingIo(Input& in) {
    static std::string text;
    text.clear();   // typed text is one-shot: only what AddInputCharacter collected
    text += g_pendText; g_pendText.clear();
    in.text = text.empty() ? nullptr : text.c_str();
    // NewFrame reuses the last frame's input, so its one-frame parts (the typed text, the key flags, the wheel) are cleared first.
    in.backspace = in.del = in.left = in.right = in.up = in.down = in.home = in.end = in.enter = in.escape = false;
    in.wheel = g_pendWheel; g_pendWheel = 0.0f; in.selectAll = in.copy = in.cut = in.paste = false; in.commit = nullptr;
    bool (&mine)[KeyId_COUNT] = g_mergeMine;
    for (int k = 1; k < KeyId_COUNT; ++k) {
        const bool now = g_pendKeyDown[k] || g_pendKeyEdge[k];   // pressed and released inside one frame still counts as a press
        if (now) in.keyDown[k] = true; else if (mine[k]) in.keyDown[k] = false;
        mine[k] = now;
    }
    {   // the modifier keys: either hand, or the modifier flag key; events decide once any arrived
        bool& mineCtrl = g_mergeMineCtrl; bool& mineShift = g_mergeMineShift;
        const bool c = g_pendKeyDown[KeyId_Ctrl] || g_pendKeyDown[KeyId_RightCtrl] || g_pendKeyDown[KeyId_ModCtrl] || g_pendKeyDown[KeyId_LeftSuper] || g_pendKeyDown[KeyId_RightSuper] || g_pendKeyDown[KeyId_ModSuper];
        const bool sh = g_pendKeyDown[KeyId_Shift] || g_pendKeyDown[KeyId_RightShift] || g_pendKeyDown[KeyId_ModShift];
        if (c) in.ctrl = true; else if (mineCtrl) in.ctrl = false;
        if (sh) in.shift = true; else if (mineShift) in.shift = false;
        mineCtrl = c; mineShift = sh;
    }
    if (g_pendMouseSet) { in.mouseX = g_pendMX; in.mouseY = g_pendMY; g_pendMouseSet = false; }
    {   // a press and a release inside one frame show as one pressed frame; the NEXT frame must show the release (nothing else would: the input is carried over)
        int (&releaseNext)[2] = g_mergeReleaseNext;
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

unsigned GetActiveID() { return (unsigned)g_active; }
void ClearActiveID() { g_active = 0; if (g_focus != 0) { g_focusLostEnd = g_focus; g_focus = 0; } }
Vec2 GetMouseDragDelta(int button, float threshold) {
    if (!IsMouseDragging(button, threshold)) return Vec2{0, 0};
    return Vec2{g_in.mouseX - g_pressX, g_in.mouseY - g_pressY};
}
void ResetMouseDragDelta(int button) { if (button == 0) { g_pressX = g_in.mouseX; g_pressY = g_in.mouseY; } }
bool IsAnyItemHovered() { return g_lay.active && (g_anyHoverNow || g_anyHoverPrev); }
namespace { unsigned g_ctxLastHot = 0; double g_ctxHotSince = 0.0; }   // RefreshContext's hover timer
Context* RefreshContext() {
    Context& c = *ctxstate::LiveHandle();   // the handle of the context in use (OkayUI_Context.cpp), so GImGui == GetCurrentContext()
    c.ActiveId = (unsigned)g_active; c.HoveredId = (unsigned)g_hot; c.FrameCount = (int)g_frameCount; c.Time = GetTime();
    { if (c.HoveredId != g_ctxLastHot) { g_ctxLastHot = c.HoveredId; g_ctxHotSince = c.Time; } c.HoveredIdTimer = c.HoveredId ? (float)(c.Time - g_ctxHotSince) : 0.0f; }
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
void SetNextFrameWantCaptureKeyboard(bool want) { g_nextFrameKbd = want ? 1 : 0; }
float GetTreeNodeToLabelSpacing() {
    if (g_framedWidgets) return textH() + framePadXNow() * 2.0f;   // Dear ImGui's: the font size and twice FramePadding.x (its tree nodes in that look)
    if (g_look) return 24.0f;
    return 4.0f + fCharW() * g_theme.textScale * 1.5f;
}


void SameLine(float spacing) {
    if (!g_lay.active) return;
    g_lay.pendingSameLine = true; g_lay.sameLineAbs = false;
    g_lay.sameLineSpacing = spacing;
}

// Dear ImGui's SameLine(offset_from_start_x, spacing): a non-zero offset puts the next item at that x from the window's left edge (the point
// GetCursorPos counts from, with UseImGuiCoordinates; else the content's left edge), even left of the item before it; 0 follows the item.
void SameLine(float offsetFromStartX, float spacing) {
    if (!g_lay.active) return;
    g_lay.pendingSameLine = true; g_lay.sameLineAbs = false;
    if (std::isfinite(offsetFromStartX) && offsetFromStartX != 0.0f) {
        const float base = g_imguiCoords && g_lay.winW > 0.0f ? g_lay.baseX : g_lay.ox;
        g_lay.sameLineAbs = true;
        g_lay.sameLineX = base + offsetFromStartX + (std::isfinite(spacing) && spacing > 0.0f ? spacing : 0.0f);
        g_lay.sameLineSpacing = -1.0f;
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
    if (g_clipTop > g_clipBase) {
        SDL_Rect& p = g_clipStack[g_clipTop - 1];
        int x0 = r.x > p.x ? r.x : p.x, y0 = r.y > p.y ? r.y : p.y;
        int x1 = (r.x + r.w) < (p.x + p.w) ? (r.x + r.w) : (p.x + p.w);
        int y1 = (r.y + r.h) < (p.y + p.h) ? (r.y + r.h) : (p.y + p.h);
        r.x = x0; r.y = y0; r.w = x1 > x0 ? x1 - x0 : 0; r.h = y1 > y0 ? y1 - y0 : 0;
    }
    if (g_clipTop < 16) g_clipStack[g_clipTop++] = r;
    if (g_toOverlay) overlayClipToStack();   // inside a popup body: the overlay batch gets the clip, the main batch keeps its own
    else pushClipSpan(true, r);
}

void PopClipRect() {
    if (g_clipTop > g_clipBase) --g_clipTop;
    if (g_toOverlay) { overlayClipToStack(); return; }
    if (g_clipTop > 0) pushClipSpan(true, g_clipStack[g_clipTop - 1]);   // (back in the main batch: the window's clips below the base count again)
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
float g_textRowH = 0.0f;   // StyleVar_TextRowHeight: 0 = a text line takes a frame's height (rowH); < 0 = the text's own height (textH, follows the scale)
// StyleVar_CellPadding: the gap inside a cell, across (default 4) and above and below (default -1: none, and the row ends after the
// last item's spacing, as it always did; 0 or more: that much above the first item and below the last one, without its spacing).
} // namespace
namespace ui_core { float g_cellPadX = 4.0f, g_cellPadY = -1.0f; }
namespace {
// The boxes of Checkbox and RadioButton: a text line + 6, or with StyleVar_FramePadding (y) a text line plus twice it (g_framePadY).
// OkayUI's own style values for Dear ImGui-looking menus and fields (PushStyleVar; < 0 = the usual OkayUI value):
// StyleVar_PopupPadding (x, y) inside a popup (default 6, 6), StyleVar_PopupWidth (default 160), StyleVar_ItemInnerSpacing (x only: the gap
// between the cells of a formatted drag row and before its label).
} // namespace
namespace ui_core { float g_popupPadX = -1.0f, g_popupPadY = -1.0f, g_popupW = -1.0f, g_innerX = -1.0f; }
namespace ui_core {
inline float boxSize() { return textH() + (g_framePadY >= 0.0f ? 2.0f * g_framePadY : 6.0f); }
void textLine(const char* s, const unsigned char* col, float rowHeight = -1.0f) {
    const float h = rowHeight > 0.0f ? rowHeight : (g_textRowH > 0.0f ? g_textRowH : (g_textRowH < 0.0f ? textH() : rowH()));
    const bool after = g_lay.pendingSameLine && !g_posOverride;
    const float prevH = g_lay.prevH;
    const bool onText = after && g_lay.hasLineTop;   // after a centred text: on that text's line (one baseline), the line's top kept
    const float top = g_lay.lineTopY;
    g_lay.hasLineTop = false;
    float x, y; place(s && *s ? labelW(s) : 1.0f, h, x, y);
    if (onText) { g_lay.hasLineTop = true; g_lay.lineTopY = top; }
    else if (after && prevH > h && g_textRowH != 0.0f) { g_lay.hasLineTop = true; g_lay.lineTopY = y; y += (prevH - h) * 0.5f; g_lay.prevY = y; }   // Dear ImGui's text rows: centred on the taller item before it on this line
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
// Dear ImGui's wrapping: the text's own spaces are kept, each word is measured once (whole-pixel advances, as Dear ImGui's wrap measures),
// a line breaks before the word that does not fit, the spaces at the break are dropped, and '\n' always breaks. `line` gets each line
// (at most 500 bytes) and answers the width for the next one. Lines are measured at `scale` (a text scale).
void imguiWrap(const char* text, float maxW, float scale, const std::function<float(const char*)>& line) {
    if (!text) return;
    char out[512];
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
            const float add = fMeasureWhole(piece, scale);
            if (fitEnd && w + add > maxW) break;
            if (!fitEnd && add > maxW && k > 1) {   // one word wider than the line: broken inside it, as many whole characters as fit (at least one)
                const char* c = r;
                while (*c == ' ') ++c;
                const char* cut = nullptr;
                while (c < we) {
                    const char* n = c + 1;
                    while (n < we && ((unsigned char)*n & 0xC0) == 0x80) ++n;   // a whole UTF-8 character
                    char part[512]; int pk = 0;
                    for (const char* x = r; x < n && pk < 500; ++x) part[pk++] = *x;
                    part[pk] = 0;
                    if (cut && fMeasureWhole(part, scale) > maxW) break;
                    cut = n; c = n;
                }
                fitEnd = cut ? cut : we; r = fitEnd;
                break;
            }
            w += add; fitEnd = we; r = we;
        }
        const char* end = fitEnd ? fitEnd : r;
        int k = 0;
        for (const char* c = q; c < end && k < 500; ++c) out[k++] = *c;
        out[k] = 0;
        maxW = line(out);
        q = end;
        while (*q == ' ') ++q;
        if (*q == '\n') ++q;
    }
}
// The size of `text` drawn at `scale`, wrapped at `wrapW` (<= 0: only at '\n'), Dear ImGui's CalcTextSize with a wrap width: the
// widest line, and one text line per line.
Vec2 wrappedTextSize(const char* text, float wrapW, float scale) {
    if (!text || !*text) return Vec2{0.0f, fH() * scale};
    float w = 0.0f; int n = 0;
    imguiWrap(text, wrapW > 0.0f ? wrapW : 1.0e9f, scale, [&](const char* l) {
        w = std::max(w, l[0] ? fMeasure(l) * scale : 0.0f); ++n; return wrapW > 0.0f ? wrapW : 1.0e9f; });
    if (text[std::strlen(text) - 1] == '\n') ++n;   // a final line break starts an empty line
    return Vec2{w, (float)std::max(n, 1) * fH() * scale};
}
void wrapLines(const char* text, float maxW, const unsigned char* col, float maxRest) {
    char line[512]; int n = 0;
    const char* p = text;
    const bool imguiRows = g_textRowH != 0.0f;
    bool first = true;
    ParaLines para(col);   // OkayUI's own rows: the lines of a paragraph one text pitch apart (main's ParaLines)
    float firstX = 0.0f;
    auto emit = [&](const char* l) {
        if (!imguiRows) { para.Add(l); first = false; return; }
        if (!first) {
            if (g_imguiCoords) SetCursorScreenPos(Vec2{firstX, g_lay.prevY + g_lay.prevH});   // Dear ImGui: every line starts where the text began
            else { g_lay.cy = g_lay.prevY + g_lay.prevH; g_lay.pendingSameLine = false; }
        }
        textLine(l, col);
        if (first) firstX = g_lay.prevX;
        if (first && imguiRows && maxRest > 0.0f && !g_imguiCoords) maxW = maxRest;
        first = false;
    };
    if (imguiRows) {   // Dear ImGui's wrapping (imguiWrap)
        imguiWrap(text, maxW, g_theme.textScale, [&](const char* l) { emit(l); return maxW; });
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
    } else if (s && g_textRowH == 0.0f && std::strchr(s, '\n')) {   // several lines: each on its own row, close together (Dear ImGui's text rows keep one item)
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
    } else if (s && g_textRowH != 0.0f && std::strchr(s, '\n')) {   // Dear ImGui's text rows: a line break starts a new line under the first (no '?' glyph)
        wrapLines(s, 1.0e9f, col, 1.0e9f);
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
    const unsigned char* d = pick(Col_TextDisabled, g_theme.textDim);
    TextColored(d[0], d[1], d[2], s);
}

void SeparatorText(const char* label) {
    if (!g_lay.active) return;
    const float h = rowH();
    float x, y; place(fullW(), h, x, y);
    const float ty = y + (h - textH()) * 0.5f;
    const float lw = label && *label ? visW(label) : 0.0f;
    if (lw > 0.0f) drawText(x, ty, VisLabel(label).b, g_theme.textScale, g_theme.textDim);   // a section label: dimmer than body text
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
        case StyleVar_FrameRounding: s.a = g_theme.rounding; g_theme.rounding = value.x; break;
        case StyleVar_ChildRounding: s.a = g_childRounding; g_childRounding = std::isfinite(value.x) && value.x >= 0.0f ? std::min(value.x, 256.0f) : -1.0f; break;
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
        case StyleVar_TextRowHeight: s.a = g_textRowH; g_textRowH = !std::isfinite(value.x) ? 0.0f : (value.x > 0.0f ? std::min(value.x, 4096.0f) : (value.x < 0.0f ? -1.0f : 0.0f)); break;
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
            case StyleVar_FrameRounding: g_theme.rounding = s.a; break;
            case StyleVar_ChildRounding: g_childRounding = s.a; break;
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
    const bool steppers = step > 0;   // Dear ImGui's rule: a step of 0 (or less) is a plain number field
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
        const float fieldW = std::max(24.0f, w - lw2 - (steppers ? 2.0f * (h + gap) : 0.0f));
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
        if (steppers) {
            if (Button(id ^ 0x2d, bx, y, h, h, "-"))              { *value -= step; ch = true; }
            if (Button(id ^ 0x2b, bx + h + gap, y, h, h, "+")) { *value += step; ch = true; }
        }
        if (lw2 > 0.0f) drawText(steppers ? bx + 2.0f * h + 2.0f * gap : bx, y + (h - textH()) * 0.5f, vis.b, g_theme.textScale, g_theme.text);
        return ch;
    }
    const float btnW = steppers ? h : 0.0f;   // square [-]/[+] steppers (none for a step of 0)
    float fieldW = w - lw - (steppers ? btnW * 2.0f + 8.0f : 0.0f);
    if (fieldW < 24.0f) fieldW = 24.0f;
    // Value box: a click on it types a number (Enter or clicking away applies it, Escape cancels).
    controlBox(x, y, fieldW, h, rnd(), g_theme.field);
    bool ch = false;
    double typed = 0.0;
    const int ed = numberEdit(id ^ 0x1e, x, y, fieldW, h, hit(x, y, fieldW, h), (double)*value, true, -2147483648.0, 2147483647.0, &typed, false);
    if (ed == 2) { const int nv = (int)typed; if (nv != *value) { *value = nv; ch = true; } }
    if (ed == 0) { char buf[32]; std::snprintf(buf, sizeof(buf), "%d", *value); drawText(x + 6.0f, y + (h - textH()) * 0.5f, buf, g_theme.textScale, g_theme.text); }
    // Steppers (unique ids via the field id so they don't collide).
    const float bx = x + fieldW + 4.0f;
    if (steppers) {
        if (Button(id ^ 0x2d, bx, y, btnW, h, "-"))              { *value -= step; ch = true; }
        if (Button(id ^ 0x2b, bx + btnW + 4.0f, y, btnW, h, "+")) { *value += step; ch = true; }
    }
    if (lw > 0.0f) drawText(steppers ? x + fieldW + btnW * 2.0f + 12.0f : x + fieldW + 8.0f, y + (h - textH()) * 0.5f,
                            VisLabel(label).b, g_theme.textScale, g_theme.text);
    return ch;
}



// ---- GetStyle: the style as Dear ImGui-shaped fields, read off OkayUI's own settings each call ----------------------------
// GetStyle() hands out one style object per context. Writes to it (Dear ImGui code sets colours and sizes through it) are taken in
// at the next GetStyle() call and at the start of the next frame: every field that differs from what GetStyle last wrote there is
// applied (the colours as set colours, see SetStyleColor; the window and frame rounding, the window padding, the border width and the
// font scale to the theme). The other fields are filled in for reading; a write to them is not used. Written by us from Dear ImGui's
// documented behaviour (ImGuiStyle is read by every widget, so a write changes how the UI draws); no code from any other project.
namespace ui_core {
float g_styleIndent = 21.0f;   // the style's IndentSpacing (a GetStyle() write; Dear ImGui's default 21): how far a tree node indents in Dear ImGui's look
float styleIndentSpacing() { return g_styleIndent; }
float framePadXNow() { return g_framePadX >= 0.0f ? g_framePadX : 8.0f; }
}
namespace {
StyleInfo g_styleOut;           // what GetStyle() returned
StyleInfo g_styleSeen;          // what it held when it was returned (to see what a caller changed)
bool g_styleHanded = false;
bool SameColor(const ImVec4& a, const ImVec4& b) { return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w; }
unsigned char ToByte(float v) { if (!(v > 0.0f)) return 0; if (v >= 1.0f) return 255; return (unsigned char)(v * 255.0f + 0.5f); }
float Sane(float v, float lo, float hi, float keep) { return std::isfinite(v) && v >= lo && v <= hi ? v : keep; }
} // namespace
void ApplyStyleWrites() {
    if (!g_styleHanded) return;
    const StyleInfo& o = g_styleOut; const StyleInfo& w = g_styleSeen;
    for (int i = 0; i < Col_COUNT && i < 64; ++i)
        if (!SameColor(o.Colors[i], w.Colors[i])) SetStyleColor((Col)i, ToByte(o.Colors[i].x), ToByte(o.Colors[i].y), ToByte(o.Colors[i].z), ToByte(o.Colors[i].w));
    if (o.WindowRounding != w.WindowRounding) g_theme.windowRounding = Sane(o.WindowRounding, 0.0f, 64.0f, g_theme.windowRounding);
    if (o.FrameRounding != w.FrameRounding) g_theme.rounding = Sane(o.FrameRounding, 0.0f, 64.0f, g_theme.rounding);
    if (o.ChildRounding != w.ChildRounding) g_childRounding = Sane(o.ChildRounding, 0.0f, 64.0f, g_childRounding);
    if (o.WindowPadding.x != w.WindowPadding.x) g_theme.windowPadding = Sane(o.WindowPadding.x, 0.0f, 200.0f, g_theme.windowPadding);
    if (o.FrameBorderSize != w.FrameBorderSize) g_theme.borderPx = Sane(o.FrameBorderSize, 0.0f, 16.0f, g_theme.borderPx);
    else if (o.WindowBorderSize != w.WindowBorderSize) g_theme.borderPx = Sane(o.WindowBorderSize, 0.0f, 16.0f, g_theme.borderPx);
    if (o.FontScale != w.FontScale) g_theme.textScale = Sane(o.FontScale, 0.25f, 16.0f, g_theme.textScale);
    if (o.IndentSpacing != w.IndentSpacing) g_styleIndent = Sane(o.IndentSpacing, 0.0f, 200.0f, g_styleIndent);   // the tree nodes of Dear ImGui's look
    g_styleSeen = g_styleOut;
}
// IO::FontGlobalScale (Dear ImGui: every font drawn this much larger): the theme's text scale follows it, applied at frame start.
namespace { float g_fontGlobalApplied = 1.0f; }
void ApplyFontGlobalScale() {
    const float want = Sane(g_io.FontGlobalScale, 0.25f, 4.0f, g_fontGlobalApplied);
    if (want == g_fontGlobalApplied) return;
    g_theme.textScale = Sane(g_theme.textScale / g_fontGlobalApplied * want, 0.25f, 32.0f, g_theme.textScale);
    g_fontGlobalApplied = want;
}
StyleInfo& GetStyle() {
    ApplyStyleWrites();
    StyleInfo& s = g_styleOut;
    const float rh = rowH(), th = textH();
    s.Alpha = 1.0f;
    s.WindowPadding = Vec2{g_theme.windowPadding, g_theme.windowPadding};
    s.FramePadding = Vec2{framePadXNow(), (rh - th) * 0.5f};
    s.ItemSpacing = Vec2{g_lay.spacingX, g_lay.spacingY};
    s.ItemInnerSpacing = Vec2{g_lay.spacingX * 0.5f, g_lay.spacingY};
    s.CellPadding = Vec2{6.0f, 3.0f};
    s.ScrollbarSize = kScrollbarW;
    s.IndentSpacing = styleIndentSpacing();
    s.WindowRounding = g_theme.windowRounding; s.ChildRounding = childRnd(); s.FrameRounding = g_theme.rounding;
    s.WindowBorderSize = g_theme.borderPx; s.ChildBorderSize = g_theme.borderPx; s.FrameBorderSize = g_theme.borderPx;
    s.FontScale = g_theme.textScale;
    for (int i = 0; i < Col_COUNT && i < 64; ++i) s.Colors[i] = GetStyleColorVec4((Col)i);
    g_styleSeen = s; g_styleHanded = true;
    return s;
}

// ---- ListClipper: lay out only the rows of a long list that can be seen --------------------------------------------------
namespace {
// The vertical span, in screen pixels, that is on screen right now: the innermost clip rectangle (a child or a clipped window), else
// the current window, else the display; always cut to the display when the host gave one.
void visibleSpanY(float& top, float& bottom) {
    top = -1e30f; bottom = 1e30f;
    if (g_clipTop > g_clipBase) { const SDL_Rect& r = g_clipStack[g_clipTop - 1]; top = (float)r.y; bottom = (float)(r.y + r.h); }
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

namespace ui_core {
// A framed combo's box (StyleVar_FramedWidgets): a rounded field in Col_FrameBg, the text FramePadding.x in (clipped before the button),
// and at the right end a square button as tall as the field in Col_Button with a filled triangle pointing down in the text colour.
// The triangle's size and place (0.37 of the text height either side of the button's centre, its top edge 0.34 of the text height
// above the centre and its point 0.22 below) were measured from Dear ImGui's combo in the editor.
void framedComboBox(float x, float y, float w, float h, bool hovered, const char* text) {
    const float rd = std::min(rnd(), h * 0.5f);
    rrect(x, y, w, h, rd, hovered ? pick(Col_FrameBgHovered, g_theme.bgHover) : pick(Col_FrameBg, g_theme.field));
    const float bw = std::min(h, w), bx = x + w - bw;   // the arrow's room (the text stops before it); a chevron, no block
    const float padX = g_framePadX >= 0.0f ? g_framePadX : 6.0f;
    PushClipRect(x, y, std::max(1.0f, w - bw), h);   // to the arrow, as Dear ImGui clips it (the editor fits a long text with "...")
    drawText(x + padX, y + (h - textH()) * 0.5f, text ? text : "", g_theme.textScale, g_theme.text);
    PopClipRect();
    chevronDir(bx + bw * 0.5f, y + h * 0.5f, std::min(9.0f, h * 0.36f), 1, hovered ? g_theme.text : pick(Col_TextDisabled, g_theme.textDim));
}
} // namespace
// The design's combo box (Combo and BeginCombo): a control box with the current choice, clipped before a chevron at the right
// that turns up while the list is open; the box eases to its hover colour.
void ui_core::comboBox(int id, float x, float y, float w, float h, bool hovered, bool open, const char* text) {
    unsigned char st[4];
    const unsigned char* fill = stateColor(id, hovered, false, g_theme.field, g_theme.bgHover, g_theme.bgDown, st);
    controlBox(x, y, w, h, rnd(), fill);
    if (open) ringRect(x, y, w, h, rnd(), std::max(1.0f, g_theme.borderPx), g_theme.focusRing);
    PushClipRect(x, y, std::max(1.0f, w - 24.0f), h);   // a long item stops before the arrow (it ran over the arrow and the label)
    drawText(x + (g_look ? 9.0f : 6.0f), y + (h - textH()) * 0.5f, text ? text : "", g_theme.textScale, g_theme.text);
    PopClipRect();
    chevron(x + w - 14.0f, y + h * 0.5f, 8.0f, true, hovered ? g_theme.text : pick(Col_TextDisabled, g_theme.textDim));
}
// A popover's box (popups, menus, combo lists): the design's shadow, a hairline and the overlay surface, with the large radius.
void ui_core::popoverBox(float x, float y, float w, float h, const unsigned char* fill) {
    const float rd = std::min(g_theme.windowRounding, 10.0f);
    elevation(x, y, w, h, rd, g_theme.shadowDepth > 0 ? g_theme.shadowDepth : 0);
    rrect(x, y, w, h, rd, g_theme.border);
    const float b = std::max(1.0f, g_theme.borderPx);
    rrect(x + b, y + b, w - 2.0f * b, h - 2.0f * b, rd > b ? rd - b : 0.0f, fill);
}

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

    // An open list of another combo (last frame's overlays) lying over this box takes the press: without this, a combo under a long
    // list grabbed the click meant for the list's row (the import settings' "Largest side" list over "Kind" and "Filter").
    bool underList = false;
    for (int i = 0; g_popupTop == 0 && i < g_ovPrevN && !underList; ++i)   // (inside a popup body the popup rules hold)
        if (pointIn(g_in.mouseX, g_in.mouseY, g_ovPrev[i].x, g_ovPrev[i].y, g_ovPrev[i].w, g_ovPrev[i].h) &&
            !(g_ovPrev[i].x == x && g_ovPrev[i].y == y + h)) underList = true;   // (not this combo's own list)
    const bool insideBox = hitOverlay(x, y, boxW, h) && inClip() && !underList;
    g_lastId = id; if (insideBox) g_hot = id;
    if (g_active == id) { if (g_released) { if (insideBox) *open = !*open; g_active = 0; } }
    else if (insideBox && g_pressed) g_active = id;

    const char* cur = (*current >= 0 && *current < count) ? items[*current] : "";
    if (g_framedWidgets) framedComboBox(x, y, boxW, h, insideBox, cur);
    else comboBox(id, x, y, boxW, h, insideBox, *open, cur);
    (void)b;
    if (lw > 0.0f) drawText(x + boxW + 8.0f, y + (h - textH()) * 0.5f, vis.b, s, g_theme.text);

    bool changed = false;
    if (*open) {
        const float listY = y + h;
        registerOverlay(x, listY, boxW, (float)count * h, true);   // takes the mouse from widgets below
        g_toOverlay = true;   // the dropdown list draws on top of later widgets
        // The list is a popover just under the box (the rows keep their places: row k at listY + k * h); the chosen row has a
        // tick, the row under the pointer a soft rounded highlight inset from the edges.
        const unsigned char listBg[4] = { g_theme.overlay[0], g_theme.overlay[1], g_theme.overlay[2], 255 };
        popoverBox(x, listY + 2.0f, boxW, (float)count * h, listBg);
        for (int k = 0; k < count; ++k) {
            const float ry = listY + (float)k * h;
            const int iid = id ^ (int)(0x9e3779b9u * (unsigned)(k + 1));
            const bool ih = hitOverlay(x, ry, boxW, h);
            if (ih) g_hot = iid;
            if (g_active == iid) { if (g_released) { if (ih) { *current = k; changed = true; *open = false; } g_active = 0; } }
            else if (ih && g_pressed) g_active = iid;
            if (ih) rrect(x + 4.0f, ry + 3.0f, boxW - 8.0f, h - 4.0f, g_theme.radiusSmall, g_theme.bgHover);
            if (k == *current) tickMark(x + boxW - h + 2.0f, ry + 2.0f, h - 4.0f, g_theme.accent);
            drawText(x + (g_look ? 12.0f : 8.0f), ry + (h - textH()) * 0.5f, items[k], s, g_theme.text);
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
    // A drag box is a field: the field colour easing to the hover colour, an accent edge while dragged.
    unsigned char st[4];
    const unsigned char* fill = stateColor(id, inside || g_active == id, false, g_theme.field, g_theme.bgHover, g_theme.bgDown, st);
    controlBox(x, y, w, h, rnd(), fill);
    if (g_active == id) ringRect(x, y, w, h, rnd(), 1.0f, g_theme.accent);
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
    // a chevron centred on the button (0 left, 1 right, 2 up, 3 down; anything else is left)
    const float cx = x + h * 0.5f, cy = y + h * 0.5f;
    const bool hot = hit(x, y, h, h);
    const unsigned char* col = hot ? g_theme.text : g_theme.textDim;
    static const int kDir[4] = {2, 0, 3, 1};   // ArrowButton's directions to chevronDir's
    chevronDir(cx, cy, h * 0.36f, kDir[(unsigned)dir < 4u ? dir : 0], col);
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

// ColorEdit3 / ColorEdit4: one row of swatch, channel sliders (or a hex field) and label; a click on the swatch opens a picker popup.
static bool colorEditRow_ed(const char* label, float* c, int n, int flags);
static bool colorEditRow(const char* label, float* c, int n, int flags) { return noteEdit(colorEditRow_ed(label, c, n, flags)); }
namespace { bool fmtDragCells(const char* label, float* v, int n, float speed, float minV, float maxV, const char* format, bool logScale); }
// The colour picker under a ColorEdit's swatch at (sx, sy, swatch h): opened by its click, closed like any popup.
static bool colorEditPicker(int id, bool swatchClicked, float sx, float sy, float h, float w, float* c, int n, int flags) {
    if (flags & ColorEdit_NoPicker) return false;
    bool changed = false;
    const int pkey = id ^ 0x7c1c;
    if (swatchClicked) {
        PopupSlot* p = popupSlot(pkey, true);
        if (p) { p->open = true; p->x = sx; p->y = sy + h + 2.0f; p->w = std::max(240.0f, std::min(300.0f, w)); p->modal = false; g_openPopupThisFrame = pkey; }
    }
    if (beginPopupBody(pkey)) {
        float tmp[4] = {c[0], c[1], c[2], n == 4 ? c[3] : 1.0f};
        if (ColorPicker4("##picker", tmp, flags | ColorEdit_NoLabel)) { for (int i = 0; i < n; ++i) c[i] = tmp[i]; changed = true; }
        EndPopup();
    }
    return changed;
}
// Dear ImGui's ColorEdit3/4 as it documents and shows it (StyleVar_FramedWidgets): a drag cell per channel, 0..255 (0..1 numbers with
// ColorEdit_Float or ColorEdit_HDR, HDR not held to 1), each with a colour marker on its left edge (R, G, B, A), then the swatch (a click
// opens the picker), then the label. The row is as wide as the item width.
static bool colorEditFramed(const char* label, float* c, int n, int flags) {
    const int id = hashLabel(label);
    const float h = rowH(), gap = g_innerX >= 0.0f ? g_innerX : 4.0f;
    const bool showLabel = !(flags & ColorEdit_NoLabel) && visW(label) > 0.0f;
    const float lw = showLabel ? visW(label) + gap : 0.0f;
    const float w = availW(), swatch = h;
    float cellsW = w - lw - swatch - gap; if (cellsW < 24.0f * (float)n) cellsW = 24.0f * (float)n;
    const bool asFloat = (flags & (ColorEdit_Float | ColorEdit_HDR)) != 0;
    float v[4] = {0, 0, 0, 1};
    for (int i = 0; i < n; ++i) v[i] = asFloat ? c[i] : std::floor(clamp01(c[i]) * 255.0f + 0.5f);
    char cellsId[192]; std::snprintf(cellsId, sizeof cellsId, "##%s#channels", label ? label : "");
    SetNextItemWidth(cellsW);
    bool changed = fmtDragCells(cellsId, v, n, asFloat ? 0.01f : 1.0f, 0.0f, (flags & ColorEdit_HDR) ? 0.0f : (asFloat ? 1.0f : 255.0f), asFloat ? "%.3f" : "%.0f", false);
    if (changed) for (int i = 0; i < n; ++i) c[i] = asFloat ? v[i] : clamp01(std::floor(v[i] + 0.5f) / 255.0f);
    const float x = g_lay.prevX, y = g_lay.prevY;
    const float cw = (cellsW - gap * (float)(n - 1)) / (float)n;
    static const unsigned char kMarker[4][4] = {{230, 70, 70, 255}, {90, 200, 90, 255}, {80, 130, 240, 255}, {200, 200, 200, 255}};
    for (int i = 0; i < n; ++i) quad(x + (float)i * (cw + gap), y, 3.0f, h, kMarker[i]);
    const float sx = x + cellsW + gap;
    float rgba[4] = {c[0], c[1], c[2], n == 4 ? c[3] : 1.0f};
    const bool swatchClicked = Button(id ^ 0x5a5a, sx, y, swatch, h, "");
    drawSwatch(sx, y, swatch, h, rgba, flags);
    if (showLabel) drawText(sx + swatch + gap, y + (h - textH()) * 0.5f, VisLabel(label).b, g_theme.textScale, g_theme.text);
    g_lay.prevX = x; g_lay.prevY = y; g_lay.prevW = w; g_lay.prevH = h; g_lastId = id;   // GetItemRect: the whole row
    if (x + w > g_lay.maxRight) g_lay.maxRight = x + w;
    changed = colorEditPicker(id, swatchClicked, sx, y, h, w, c, n, flags) || changed;
    return changed;
}
static bool colorEditRow_ed(const char* label, float* c, int n, int flags) {
    if (!g_lay.active || !c) return false;
    if (n == 3 || (flags & ColorEdit_NoAlpha)) { n = 3; flags |= ColorEdit_NoAlpha; }
    if (g_framedWidgets && !(flags & (ColorEdit_Hex | ColorEdit_NoInputs))) return colorEditFramed(label, c, n, flags);
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
            // HDR: the colour channels (not alpha) share one range, 0 .. the brightest channel rounded up (at least 1), fixed while a cell is
            // held so the knob does not run away from the pointer.
            float top = 1.0f;
            if (flags & ColorEdit_HDR) {
                float* held = floatState(id ^ 0x4d52, 1.0f);
                bool holding = false;
                for (int ch = 0; ch < 3; ++ch) holding |= g_active == (id ^ (int)(0x9e3779b9u * (unsigned)(ch + 1)));
                if (!holding) {
                    float m = 1.0f;
                    for (int ch = 0; ch < 3; ++ch) if (std::isfinite(c[ch]) && c[ch] > m) m = c[ch];
                    *held = std::min(std::ceil(m), 1.0e6f);
                }
                top = *held;
            }
            for (int ch = 0; ch < n; ++ch) {
                const int cid = id ^ (int)(0x9e3779b9u * (unsigned)(ch + 1));
                if (Slider(cid, ix + (float)ch * (each + 4.0f), y, each, h, &c[ch], 0.0f, ch < 3 ? top : 1.0f)) changed = true;
            }
        }
    }
    if (showLabel) drawText(x + row + 8.0f, y + (h - textH()) * 0.5f, VisLabel(label).b, s, g_theme.text);
    changed = colorEditPicker(id, swatchClicked, x, y, h, w, c, n, flags) || changed;   // the picker popup, anchored under the swatch
    return changed;
}
static bool ColorEdit4_raw(const char* label, float rgba[4], int flags) { return colorEditRow(label, rgba, 4, flags); }
static bool ColorEdit3_raw(const char* label, float rgb[3], int flags) { return colorEditRow(label, rgb, 3, flags); }

void NewLine() { if (g_lay.active) g_lay.pendingSameLine = false; }



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
    if (g_prevInOverlay) return g_lay.active && hitOverlay(g_lay.prevX, g_lay.prevY, g_lay.prevW, g_lay.prevH);
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
    if (std::strchr(text, '\n')) return wrappedTextSize(text, 0.0f, g_theme.textScale);   // several lines (Dear ImGui's measure)
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
// The point window-relative cursor positions count from: the content's corner, or with UseImGuiCoordinates Dear ImGui's (the window's,
// child's or popup's top-left corner moved by its scroll, so the first item of a padded window is at the padding).
namespace ui_core { bool g_imguiCoords = false; }
static float cursorBaseX() { return g_imguiCoords && g_lay.winW > 0.0f ? g_lay.baseX : g_lay.ox; }
static float cursorBaseY() { return g_imguiCoords && g_lay.winW > 0.0f ? g_lay.baseY : g_lay.oy; }
void UseImGuiCoordinates(bool on) { g_imguiCoords = on; }
bool ImGuiCoordinates() { return g_imguiCoords; }
Vec2 GetCursorPos() {
    const Vec2 s = GetCursorScreenPos();
    return Vec2{ s.x - cursorBaseX(), s.y - cursorBaseY() };
}
void SetCursorPos(Vec2 p) { SetCursorScreenPos(Vec2{ cursorBaseX() + p.x, cursorBaseY() + p.y }); }
float GetCursorPosX() { return GetCursorPos().x; }
float GetCursorPosY() { return GetCursorPos().y; }
void SetCursorPosX(float x) { SetCursorScreenPos(Vec2{ cursorBaseX() + x, GetCursorScreenPos().y }); }
void SetCursorPosY(float y) { SetCursorScreenPos(Vec2{ GetCursorScreenPos().x, cursorBaseY() + y }); }
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
    if (g_clipTop > g_clipBase) { const SDL_Rect& c = g_clipStack[g_clipTop - 1]; x0 = (float)c.x; y0 = (float)c.y; x1 = (float)(c.x + c.w); y1 = (float)(c.y + c.h); }
    return g_lay.prevX < x1 && g_lay.prevX + g_lay.prevW > x0 && g_lay.prevY < y1 && g_lay.prevY + g_lay.prevH > y0;
}
bool IsRectVisible(Vec2 min, Vec2 max) {
    if (!g_lay.active) return false;
    float x0 = 0.0f, y0 = 0.0f, x1 = 1e9f, y1 = 1e9f;
    if (g_clipTop > g_clipBase) { const SDL_Rect& c = g_clipStack[g_clipTop - 1]; x0 = (float)c.x; y0 = (float)c.y; x1 = (float)(c.x + c.w); y1 = (float)(c.y + c.h); }
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
    for (AnimSlot& a : g_anims) a = AnimSlot();
    g_animPrevT = g_animDt = 0.0;
    g_nextNum = NextNumber(); for (auto& t : g_numText) t[0] = 0; g_numLast = 0;
    g_dockAware = false; g_dockRouted = false;
    g_split = SplitState(); g_windowList._Splitter._Count = 1; g_windowList._Splitter._Current = 0;
    UseLauncherLook(false);
    g_theme = design::DefaultTheme();
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
    g_dd = DragDrop(); g_tdd = TypedDrag(); g_tddExt = g_tddExtSeen = g_tddExtReleased = false; g_curMenu = CurMenu(); g_inMenuBar = false; menuBarReset(); regionReset();
    g_lay = LayoutState(); g_col = ColumnState(); g_tbl = TableState(); g_cullRow = false;
    for (TblStore& t : g_tstore) t = TblStore();
    g_sortSpecsFor = 0; g_sortSpecsOut = nullptr;
    g_childTop = g_popupTop = g_colTop = g_idTop = g_idSkipped = g_itemWTop = g_clipTop = g_clipBase = 0;
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
    g_styleHanded = false; g_fontGlobalApplied = 1.0f; g_io.FontGlobalScale = 1.0f; g_childRounding = -1.0f; g_imguiCoords = false;
}

// ---- Edit tracking: every value widget below reports through noteEdit (see IsItemEdited) ----
bool Checkbox(const char* label, bool* value) { return noteEdit(Checkbox_raw(label, value)); }
bool RadioButton(const char* label, int* value, int option) { return noteEdit(RadioButton_raw(label, value, option)); }
bool ToggleSwitch(const char* label, bool* value) { return noteEdit(ToggleSwitch_raw(label, value)); }
// With StyleVar_FramedWidgets (Dear ImGui's look, the editor and launcher on OkayUI) the plain calls draw Dear ImGui's framed widgets: Dear ImGui's
// SliderFloat / DragFloat with no format show "%.3f", SliderInt "%d" (its documented defaults); a pending SetNextNumberFormat is used instead.
namespace {
struct FramedFmt { NextNumber n; const char* fmt; bool log; explicit FramedFmt(const char* dflt) : n(takeNextNumber()), fmt(n.has ? n.fmt : dflt), log(n.logarithmic) {} };
}
bool SliderFloat(const char* label, float* value, float minV, float maxV) {
    if (g_framedWidgets) { const FramedFmt f("%.3f"); return FramedSliderFloat(label, value, minV, maxV, f.fmt, f.log); }
    return noteEdit(SliderFloat_raw(label, value, minV, maxV));
}
bool SliderInt(const char* label, int* value, int minV, int maxV) {
    if (g_framedWidgets) { const FramedFmt f("%d"); return FramedSliderInt(label, value, minV, maxV, f.fmt); }
    return noteEdit(SliderInt_raw(label, value, minV, maxV));
}
bool VSliderFloat(const char* label, float w, float h, float* value, float minV, float maxV) { return noteEdit(VSliderFloat_raw(label, w, h, value, minV, maxV)); }
bool Knob(const char* label, float* value, float minV, float maxV, float size) { return noteEdit(Knob_raw(label, value, minV, maxV, size)); }
// With InputText_EnterReturnsTrue the return value is the Enter, not an edit: TextField marks the edits itself.
bool InputText(const char* label, char* buf, int cap, unsigned flags) { const bool r = InputText_raw(label, buf, cap, flags); return (flags & InputText_EnterReturnsTrue) ? r : noteEdit(r); }
bool InputInt(const char* label, int* value, int step) { return noteEdit(InputInt_raw(label, value, step)); }
bool Combo(const char* label, const char* const* items, int count, int* current) { return noteEdit(Combo_raw(label, items, count, current)); }
bool DragFloat(const char* label, float* v, float speed, float minV, float maxV) {
    if (g_framedWidgets) { const FramedFmt f("%.3f"); return FramedDragFloatN(label, v, 1, speed, minV, maxV, f.fmt, f.log); }
    return noteEdit(DragFloat_raw(label, v, speed, minV, maxV));
}
bool DragInt(const char* label, int* v, float speed, int minV, int maxV) { return noteEdit(DragInt_raw(label, v, speed, minV, maxV)); }
bool DragFloat2(const char* label, float v[2], float speed, float minV, float maxV) {
    if (g_framedWidgets) { const FramedFmt f("%.3f"); return FramedDragFloatN(label, v, 2, speed, minV, maxV, f.fmt, f.log); }
    return noteEdit(DragFloat2_raw(label, v, speed, minV, maxV));
}
bool DragFloat3(const char* label, float v[3], float speed, float minV, float maxV) {
    if (g_framedWidgets) { const FramedFmt f("%.3f"); return FramedDragFloatN(label, v, 3, speed, minV, maxV, f.fmt, f.log); }
    return noteEdit(DragFloat3_raw(label, v, speed, minV, maxV));
}
bool DragFloat4(const char* label, float v[4], float speed, float minV, float maxV) {
    if (g_framedWidgets) { const FramedFmt f("%.3f"); return FramedDragFloatN(label, v, 4, speed, minV, maxV, f.fmt, f.log); }
    return noteEdit(DragFloat4_raw(label, v, speed, minV, maxV));
}
bool DragInt2(const char* label, int v[2], float speed, int minV, int maxV) { return noteEdit(DragInt2_raw(label, v, speed, minV, maxV)); }
bool DragInt3(const char* label, int v[3], float speed, int minV, int maxV) { return noteEdit(DragInt3_raw(label, v, speed, minV, maxV)); }
bool DragInt4(const char* label, int v[4], float speed, int minV, int maxV) { return noteEdit(DragInt4_raw(label, v, speed, minV, maxV)); }
bool DragIntRange2(const char* label, int* lo, int* hi, float speed, int minV, int maxV) { return noteEdit(DragIntRange2_raw(label, lo, hi, speed, minV, maxV)); }
bool DragFloatRange2(const char* label, float* lo, float* hi, float speed, float minV, float maxV) { return noteEdit(DragFloatRange2_raw(label, lo, hi, speed, minV, maxV)); }
bool InputTextWithHint(const char* label, const char* hint, char* buf, int cap, unsigned flags) { const bool r = InputTextWithHint_raw(label, hint, buf, cap, flags); return (flags & InputText_EnterReturnsTrue) ? r : noteEdit(r); }
bool SliderFloat2(const char* label, float v[2], float minV, float maxV) {
    if (g_framedWidgets) { const FramedFmt f("%.3f"); return FramedSliderFloatN(label, v, 2, minV, maxV, f.fmt); }
    return noteEdit(SliderFloat2_raw(label, v, minV, maxV));
}
bool SliderFloat3(const char* label, float v[3], float minV, float maxV) {
    if (g_framedWidgets) { const FramedFmt f("%.3f"); return FramedSliderFloatN(label, v, 3, minV, maxV, f.fmt); }
    return noteEdit(SliderFloat3_raw(label, v, minV, maxV));
}
bool SliderFloat4(const char* label, float v[4], float minV, float maxV) {
    if (g_framedWidgets) { const FramedFmt f("%.3f"); return FramedSliderFloatN(label, v, 4, minV, maxV, f.fmt); }
    return noteEdit(SliderFloat4_raw(label, v, minV, maxV));
}
bool InputFloat(const char* label, float* value, float step) { return noteEdit(InputFloat_raw(label, value, step)); }
bool ColorEdit4(const char* label, float rgba[4], int flags) { return noteEdit(ColorEdit4_raw(label, rgba, flags)); }
bool ColorEdit3(const char* label, float rgb[3], int flags) { return noteEdit(ColorEdit3_raw(label, rgb, flags)); }
bool ListBox(const char* label, int* current, const char* const* items, int count, int visibleRows) { return noteEdit(ListBox_raw(label, current, items, count, visibleRows)); }

// ---- Text field callbacks (history and completion) -----------------------------------------------------------------------------
// Written by us from the behaviour Dear ImGui documents for these flags: while the field has the keyboard, Up / Down call the
// callback with EventFlag = InputText_CallbackHistory and EventKey = the arrow, and Tab calls it with InputText_CallbackCompletion.
// The callback may rewrite the text with DeleteChars / InsertChars; the caret then goes to the end of the new text. Other callback
// flags (resize, always, char filter) still do nothing. No code from any other project.
static void runTextCallbacks(const char* label, char* buf, int cap, unsigned flags, InputTextCallback cb, void* userData) {
    if (!cb || !buf || cap <= 0 || !g_lay.active || (flags & InputText_ReadOnly)) return;
    const int id = hashLabel(label);
    if (g_focus != id) return;
    int ev = 0, key = 0;
    if ((flags & InputText_CallbackHistory) && (g_in.up || g_in.down)) { ev = InputText_CallbackHistory; key = g_in.up ? KeyId_Up : KeyId_Down; }
    else if ((flags & InputText_CallbackCompletion) && IsKeyPressed(KeyId_Tab, false)) { ev = InputText_CallbackCompletion; key = KeyId_Tab; }
    if (!ev) return;
    TextSlot* ts = textSlot(id);
    InputTextCallbackData d;
    d.EventFlag = ev; d.Flags = (int)flags; d.EventKey = key; d.UserData = userData;
    d.Buf = buf; d.BufTextLen = cstrlen(buf); d.BufSize = cap;
    d.CursorPos = ts->caret;
    d.SelectionStart = ts->anchor < ts->caret ? ts->anchor : ts->caret;
    d.SelectionEnd = ts->anchor < ts->caret ? ts->caret : ts->anchor;
    cb(&d);
    if (d.BufTextLen < 0) d.BufTextLen = 0;
    if (d.BufTextLen > cap - 1) d.BufTextLen = cap - 1;
    if (d.BufDirty) { buf[d.BufTextLen] = '\0'; ts->caret = ts->anchor = d.BufTextLen; ts->first = 0; }
}
bool InputTextWithHint(const char* l, const char* h, char* b, int c, unsigned f, InputTextCallback cb, void* ud) {
    const bool r = InputTextWithHint(l, h, b, c, f); runTextCallbacks(l, b, c, f, cb, ud); return r;
}
bool InputText(const char* l, char* b, int c, unsigned f, InputTextCallback cb, void* ud) {
    const bool r = InputText(l, b, c, f); runTextCallbacks(l, b, c, f, cb, ud); return r;
}

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
        // The design's framed slider: the part up to the value tinted with the accent, a slim rounded thumb, and the value on the
        // side away from the thumb (it never sits under it). The thumb's hit area is the same as before (the whole box).
        const float gx = x + pad + t * (track - grab), thumbW = 4.0f, tx = gx + grab * 0.5f - thumbW * 0.5f;
        unsigned char tint[4];
        if (tx > x + pad) rrect(x + pad, y + pad, tx - x - pad + thumbW * 0.5f, h - 2.0f * pad, std::max(0.0f, rnd() - pad), withAlpha(g_theme.accent, 0.22f, tint));
        rrect(tx, y + pad + 1.0f, thumbW, h - 2.0f * pad - 2.0f, thumbW * 0.5f, g_active == id ? pick(Col_SliderGrabActive, g_theme.text) : pick(Col_SliderGrab, g_theme.accent));
        char buf[64]; formatValue(buf, sizeof buf, format, *v, isInt);
        const float vw = labelW(buf), ty = y + (h - textH()) * 0.5f;
        const float vx = t > 0.5f ? x + 8.0f : x + ctrlW - 8.0f - vw;
        if (vw + 16.0f < ctrlW * 0.5f) drawText(vx, ty, buf, g_theme.textScale, g_theme.text);
        else centredText(x, y, ctrlW, h, buf, g_theme.text);
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

// ---- What belongs to one context (OkayUI_ContextState.hpp: each context keeps its own copy of these) ----
namespace OkayUI {
OKAYUI_CONTEXT_STATE(
    OKAYUI_CTX_VAR(ui_core::g_active),
    OKAYUI_CTX_VAR(ui_core::g_activeAtFrameStart),
    OKAYUI_CTX_VAR(ui_core::g_anyHoverNow),
    OKAYUI_CTX_VAR(ui_core::g_anyHoverPrev),
    OKAYUI_CTX_VAR(ui_core::g_barH),
    OKAYUI_CTX_VAR(ui_core::g_barX),
    OKAYUI_CTX_VAR(ui_core::g_barY),
    OKAYUI_CTX_VAR(ui_core::g_bgAlphaCore),
    OKAYUI_CTX_VAR(ui_core::g_btnAlignX),
    OKAYUI_CTX_VAR(ui_core::g_cellPadX),
    OKAYUI_CTX_VAR(ui_core::g_cellPadY),
    OKAYUI_CTX_VAR(ui_core::g_centerNext),
    OKAYUI_CTX_VAR(ui_core::g_childPadY),
    OKAYUI_CTX_VAR(ui_core::g_childStack),
    OKAYUI_CTX_VAR(ui_core::g_childTop),
    OKAYUI_CTX_VAR(ui_core::g_clipStack),
    OKAYUI_CTX_VAR(ui_core::g_clipTop),
    OKAYUI_CTX_VAR(ui_core::g_clock),
    OKAYUI_CTX_VAR(ui_core::g_col),
    OKAYUI_CTX_VAR(ui_core::g_colStack),
    OKAYUI_CTX_VAR(ui_core::g_colTop),
    OKAYUI_CTX_VAR(g_ctxHotSince),
    OKAYUI_CTX_VAR(g_ctxLastHot),
    OKAYUI_CTX_VAR(ui_core::g_cullRow),
    OKAYUI_CTX_VAR(ui_core::g_curClipOn),
    OKAYUI_CTX_VAR(ui_core::g_curClipRect),
    OKAYUI_CTX_VAR(ui_core::g_curMenu),
    OKAYUI_CTX_VAR(ui_core::g_cursor),
    OKAYUI_CTX_VAR(ui_core::g_curTex),
    OKAYUI_CTX_VAR(ui_core::g_dblClick),
    OKAYUI_CTX_VAR(ui_core::g_dd),
    OKAYUI_CTX_VAR(ui_core::g_defTex),
    OKAYUI_CTX_VAR(ui_core::g_disCount),
    OKAYUI_CTX_VAR(ui_core::g_dispH),
    OKAYUI_CTX_VAR(ui_core::g_dispW),
    OKAYUI_CTX_VAR(ui_core::g_disStack),
    OKAYUI_CTX_VAR(ui_core::g_disTop),
    OKAYUI_CTX_VAR(ui_core::g_dragAccum),
    OKAYUI_CTX_VAR(ui_core::g_dragOriginX),
    OKAYUI_CTX_VAR(ui_core::g_dragOriginY),
    OKAYUI_CTX_VAR(ui_core::g_edFrameId),
    OKAYUI_CTX_VAR(ui_core::g_editBuf),
    OKAYUI_CTX_VAR(ui_core::g_editId),
    OKAYUI_CTX_VAR(ui_core::g_edStickyId),
    OKAYUI_CTX_VAR(ui_core::g_escapeUsed),
    OKAYUI_CTX_VAR(ui_core::g_fieldFocus),
    OKAYUI_CTX_VAR(ui_core::g_fieldFocusSeen),
    OKAYUI_CTX_VAR(ui_core::g_fieldPadL),
    OKAYUI_CTX_VAR(ui_core::g_fieldPadR),
    OKAYUI_CTX_VAR(ui_core::g_floats),
    OKAYUI_CTX_VAR(ui_core::g_focus),
    OKAYUI_CTX_VAR(ui_core::g_focusAtFrameStart),
    OKAYUI_CTX_VAR(ui_core::g_focusClaimed),
    OKAYUI_CTX_VAR(ui_core::g_focusLostEnd),
    OKAYUI_CTX_VAR(ui_core::g_focusLostPrev),
    OKAYUI_CTX_VAR(ui_core::g_focusNextField),
    OKAYUI_CTX_VAR(ui_core::g_focusSelectAll),
    OKAYUI_CTX_VAR(ui_core::g_font),
    OKAYUI_CTX_VAR(ui_core::g_frame),
    OKAYUI_CTX_VAR(ui_core::g_frameCount),
    OKAYUI_CTX_VAR(ui_core::g_childRounding),
    OKAYUI_CTX_VAR(ui_core::g_framedWidgets),
    OKAYUI_CTX_VAR(ui_core::g_framePadX),
    OKAYUI_CTX_VAR(ui_core::g_styleIndent),
    OKAYUI_CTX_VAR(ui_core::g_framePadY),
    OKAYUI_CTX_VAR(ui_core::g_grp),
    OKAYUI_CTX_VAR(ui_core::g_grpTop),
    OKAYUI_CTX_VAR(ui_core::g_hintText),
    OKAYUI_CTX_VAR(ui_core::g_hitWinId),
    OKAYUI_CTX_VAR(ui_core::g_hitWinOn),
    OKAYUI_CTX_VAR(ui_core::g_hitWinZ),
    OKAYUI_CTX_VAR(ui_core::g_clipBase),
    OKAYUI_CTX_VAR(ui_core::g_prevInOverlay),
    OKAYUI_CTX_VAR(ui_core::g_hot),
    OKAYUI_CTX_VAR(ui_core::g_imguiCoords),
    OKAYUI_CTX_VAR(ui_core::g_hoverFrame),
    OKAYUI_CTX_VAR(ui_core::g_hoverKey),
    OKAYUI_CTX_VAR(ui_core::g_hoverStart),
    OKAYUI_CTX_VAR(ui_core::g_idSeed),
    OKAYUI_CTX_VAR(ui_core::g_idSkipped),
    OKAYUI_CTX_VAR(ui_core::g_idStack),
    OKAYUI_CTX_VAR(ui_core::g_idTop),
    OKAYUI_CTX_VAR(ui_core::g_ime),
    OKAYUI_CTX_VAR(ui_core::g_in),
    OKAYUI_CTX_VAR(ui_core::g_inMenuBar),
    OKAYUI_CTX_VAR(ui_core::g_innerX),
    OKAYUI_CTX_VAR(ui_core::g_inTip),
    OKAYUI_CTX_VAR(ui_core::g_io),
    OKAYUI_CTX_VAR(g_ioQueueFilled),
    OKAYUI_CTX_VAR(g_ioQueueFrame),
    OKAYUI_CTX_VAR(ui_core::g_itemWStack),
    OKAYUI_CTX_VAR(ui_core::g_itemWTop),
    OKAYUI_CTX_VAR(ui_core::g_keyNextRepeat),
    OKAYUI_CTX_VAR(ui_core::g_keyNow),
    OKAYUI_CTX_VAR(ui_core::g_keyPrev),
    OKAYUI_CTX_VAR(ui_core::g_keySince),
    OKAYUI_CTX_VAR(ui_core::g_lastId),
    OKAYUI_CTX_VAR(ui_core::g_lastMouseMove),
    OKAYUI_CTX_VAR(ui_core::g_lastPressTime),
    OKAYUI_CTX_VAR(ui_core::g_lastPressX),
    OKAYUI_CTX_VAR(ui_core::g_lastPressY),
    OKAYUI_CTX_VAR(ui_core::g_lay),
    OKAYUI_CTX_VAR(ui_core::g_look),
    OKAYUI_CTX_VAR(ui_core::g_menuDepth),
    OKAYUI_CTX_VAR(ui_core::g_menuKbs),
    OKAYUI_CTX_VAR(ui_core::g_menuStack),
    OKAYUI_CTX_VAR(g_mergeMine),
    OKAYUI_CTX_VAR(g_mergeMineCtrl),
    OKAYUI_CTX_VAR(g_mergeMineShift),
    OKAYUI_CTX_VAR(g_mergeReleaseNext),
    OKAYUI_CTX_VAR(ui_core::g_mouseDX),
    OKAYUI_CTX_VAR(ui_core::g_mouseDY),
    OKAYUI_CTX_VAR(ui_core::g_mPressed),
    OKAYUI_CTX_VAR(ui_core::g_mPrevDown),
    OKAYUI_CTX_VAR(ui_core::g_mReleased),
    OKAYUI_CTX_VAR(ui_core::g_nextBgAlpha),
    OKAYUI_CTX_VAR(ui_core::g_nextCollapse),
    OKAYUI_CTX_VAR(ui_core::g_nextFrameKbd),
    OKAYUI_CTX_VAR(ui_core::g_nextItemW),
    OKAYUI_CTX_VAR(g_nextNum),
    OKAYUI_CTX_VAR(ui_core::g_nextOpen),
    OKAYUI_CTX_VAR(ui_core::g_nextWinFocus),
    OKAYUI_CTX_VAR(g_ntoasts),
    OKAYUI_CTX_VAR(g_numLast),
    OKAYUI_CTX_VAR(g_numText),
    OKAYUI_CTX_VAR(ui_core::g_nwrec),
    OKAYUI_CTX_VAR(ui_core::g_openPopupThisFrame),
    OKAYUI_CTX_VAR(ui_core::g_opens),
    OKAYUI_CTX_VAR(ui_core::g_ovCur),
    OKAYUI_CTX_VAR(ui_core::g_ovCurN),
    OKAYUI_CTX_VAR(ui_core::g_ovPrev),
    OKAYUI_CTX_VAR(ui_core::g_ovPrevN),
    OKAYUI_CTX_VAR(g_pendBtn),
    OKAYUI_CTX_VAR(g_pendClick),
    OKAYUI_CTX_VAR(g_pendFocusLost),
    OKAYUI_CTX_VAR(g_pendKeyDown),
    OKAYUI_CTX_VAR(g_pendKeyEdge),
    OKAYUI_CTX_VAR(g_pendMouseSet),
    OKAYUI_CTX_VAR(g_pendMX),
    OKAYUI_CTX_VAR(g_pendMY),
    OKAYUI_CTX_VAR(g_pendText),
    OKAYUI_CTX_VAR(g_pendWheel),
    OKAYUI_CTX_VAR(ui_core::g_popCur),
    OKAYUI_CTX_VAR(ui_core::g_popCurN),
    OKAYUI_CTX_VAR(ui_core::g_popPrev),
    OKAYUI_CTX_VAR(ui_core::g_popPrevN),
    OKAYUI_CTX_VAR(ui_core::g_popupPadX),
    OKAYUI_CTX_VAR(ui_core::g_popupPadY),
    OKAYUI_CTX_VAR(ui_core::g_popups),
    OKAYUI_CTX_VAR(ui_core::g_popupStack),
    OKAYUI_CTX_VAR(ui_core::g_popupSubOpen),
    OKAYUI_CTX_VAR(ui_core::g_popupTop),
    OKAYUI_CTX_VAR(ui_core::g_popupW),
    OKAYUI_CTX_VAR(ui_core::g_posOverride),
    OKAYUI_CTX_VAR(ui_core::g_posSameLine),
    OKAYUI_CTX_VAR(ui_core::g_posX),
    OKAYUI_CTX_VAR(ui_core::g_posY),
    OKAYUI_CTX_VAR(ui_core::g_pressed),
    OKAYUI_CTX_VAR(ui_core::g_pressX),
    OKAYUI_CTX_VAR(ui_core::g_pressY),
    OKAYUI_CTX_VAR(ui_core::g_prevDown),
    OKAYUI_CTX_VAR(ui_core::g_prevMouseX),
    OKAYUI_CTX_VAR(ui_core::g_prevMouseY),
    OKAYUI_CTX_VAR(ui_core::g_ptrOverWindow),
    OKAYUI_CTX_VAR(ui_core::g_ptrOverWindowPrev),
    OKAYUI_CTX_VAR(ui_core::g_rawDraw),
    OKAYUI_CTX_VAR(ui_core::g_released),
    OKAYUI_CTX_VAR(ui_core::g_rowClipper),
    OKAYUI_CTX_VAR(ui_core::g_rPressed),
    OKAYUI_CTX_VAR(ui_core::g_rPrevDown),
    OKAYUI_CTX_VAR(ui_core::g_rReleased),
    OKAYUI_CTX_VAR(ui_core::g_sbSize),
    OKAYUI_CTX_VAR(ui_core::g_sortSpecsFor),
    OKAYUI_CTX_VAR(ui_core::g_sortSpecsOut),
    OKAYUI_CTX_VAR(g_split),
    OKAYUI_CTX_VAR(ui_core::g_animDt),
    OKAYUI_CTX_VAR(ui_core::g_animPrevT),
    OKAYUI_CTX_VAR(ui_core::g_anims),
    OKAYUI_CTX_VAR(g_fontGlobalApplied),
    OKAYUI_CTX_VAR(g_styleHanded),
    OKAYUI_CTX_VAR(g_styleOut),
    OKAYUI_CTX_VAR(g_styleSeen),
    OKAYUI_CTX_VAR(g_styleStack),
    OKAYUI_CTX_VAR(g_styleTop),
    OKAYUI_CTX_VAR(ui_core::g_tabBar),
    OKAYUI_CTX_VAR(ui_core::g_tabOrders),
    OKAYUI_CTX_VAR(ui_core::g_tabSel),
    OKAYUI_CTX_VAR(ui_core::g_tbl),
    OKAYUI_CTX_VAR(ui_core::g_tdd),
    OKAYUI_CTX_VAR(ui_core::g_tddExt),
    OKAYUI_CTX_VAR(ui_core::g_tddExtReleased),
    OKAYUI_CTX_VAR(ui_core::g_tddExtSeen),
    OKAYUI_CTX_VAR(g_textRowH),
    OKAYUI_CTX_VAR(g_texts),
    OKAYUI_CTX_VAR(ui_core::g_theme),
    OKAYUI_CTX_VAR(g_toasts),
    OKAYUI_CTX_VAR(ui_core::g_toggledId),
    OKAYUI_CTX_VAR(ui_core::g_toOverlay),
    OKAYUI_CTX_VAR(ui_core::g_tstore),
    OKAYUI_CTX_VAR(ui_core::g_ttf),
    OKAYUI_CTX_VAR(g_userSavedScale),
    OKAYUI_CTX_VAR(ui_core::g_wCur),
    OKAYUI_CTX_VAR(ui_core::g_wCurN),
    OKAYUI_CTX_VAR(ui_core::g_wheelUsed),
    OKAYUI_CTX_VAR(ui_core::g_whiteU),
    OKAYUI_CTX_VAR(ui_core::g_whiteV),
    OKAYUI_CTX_VAR(ui_core::g_winBaseScale),
    OKAYUI_CTX_VAR(ui_core::g_winItemWDepth),
    OKAYUI_CTX_VAR(ui_core::g_wins),
    OKAYUI_CTX_VAR(ui_core::g_wPrev),
    OKAYUI_CTX_VAR(ui_core::g_wPrevN),
    OKAYUI_CTX_VAR(ui_core::g_wrapPos),
    OKAYUI_CTX_VAR(ui_core::g_wrec),
    OKAYUI_CTX_VAR(ui_core::g_wrecOpen),
    OKAYUI_CTX_VAR(ui_core::g_zCounter))
} // namespace OkayUI
