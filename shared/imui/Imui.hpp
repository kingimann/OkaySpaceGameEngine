#pragma once
// okay::imui - our own immediate-mode UI core. Written from scratch (no Dear ImGui code), no SDL,
// no OpenGL, no engine dependency: input comes in as a plain struct each frame, output is a draw
// list (vertex / index / command buffers) that any backend can draw. A software backend lives in
// ImuiSoft.hpp. This is side by side with Dear ImGui and not used by the editor or launcher yet;
// see docs/own_ui_toolkit.md for what is missing before it could replace it.
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {
namespace imui {

// ---------------------------------------------------------------------------------------------
// Basic types
// ---------------------------------------------------------------------------------------------
struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float X, float Y) : x(X), y(Y) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    bool operator==(Vec2 o) const { return x == o.x && y == o.y; }
};

struct Rect {
    Vec2 min, max;
    Rect() = default;
    Rect(Vec2 a, Vec2 b) : min(a), max(b) {}
    Rect(float x0, float y0, float x1, float y1) : min(x0, y0), max(x1, y1) {}
    float Width() const { return max.x - min.x; }
    float Height() const { return max.y - min.y; }
    Vec2 Size() const { return {Width(), Height()}; }
    bool Contains(Vec2 p) const { return p.x >= min.x && p.y >= min.y && p.x < max.x && p.y < max.y; }
    Rect Intersect(const Rect& o) const;   // empty (zero area) when they do not overlap
};

/// Packed colour, 0xAABBGGRR (red in the low byte, so the bytes read R,G,B,A in memory).
using Col = std::uint32_t;
inline Col MakeCol(int r, int g, int b, int a = 255) {
    return (Col)(r & 255) | ((Col)(g & 255) << 8) | ((Col)(b & 255) << 16) | ((Col)(a & 255) << 24);
}
inline int ColR(Col c) { return (int)(c & 255); }
inline int ColG(Col c) { return (int)((c >> 8) & 255); }
inline int ColB(Col c) { return (int)((c >> 16) & 255); }
inline int ColA(Col c) { return (int)((c >> 24) & 255); }

/// 32-bit FNV-1a hash seeded from `seed` (the ID stack top). Never returns 0.
std::uint32_t HashBytes(const void* data, std::size_t size, std::uint32_t seed = 0);
std::uint32_t HashString(const char* s, std::uint32_t seed = 0);

/// UTF-8 helpers. Decode returns the number of bytes consumed (>= 1 while begin < end); an invalid
/// or truncated sequence yields U+FFFD and consumes one byte.
int Utf8Decode(const char* begin, const char* end, std::uint32_t* cp);
void Utf8Append(std::string& out, std::uint32_t cp);
bool Utf8Valid(const std::string& s);

// ---------------------------------------------------------------------------------------------
// Font atlas
// ---------------------------------------------------------------------------------------------
struct Glyph { float u0 = 0, v0 = 0, u1 = 0, v1 = 0; float w = 0, h = 0, advance = 0; };

/// A single-channel (alpha) glyph atlas plus metrics. Built from a bitmap glyph source (the engine's
/// own Font8x8 hand-drawn glyphs) scaled by an integer factor. A TTF rasterizer can fill the same
/// structure later; nothing else depends on how the pixels got there.
class FontAtlas {
public:
    /// `rows(c)` returns the 8 row bytes of a printable ASCII glyph (bit x = column x, row 0 top).
    using GlyphSource = const unsigned char* (*)(char c);
    void BuildFrom8x8(GlyphSource rows, int scale);

    bool Valid() const { return width > 0; }
    const Glyph& GetGlyph(std::uint32_t cp) const;           // unknown code points map to '?'
    Vec2 WhiteUV() const { return whiteUV; }                  // inside a fully opaque block
    /// Size of the text between begin and end (end null = NUL terminated); '\n' starts a new line.
    Vec2 CalcTextSize(const char* begin, const char* end = nullptr) const;

    int width = 0, height = 0;
    std::vector<std::uint8_t> alpha;                          // width * height
    float lineHeight = 0;
    float fontSize = 0;
private:
    Glyph glyphs_[96];                                        // 32..126, [95] = the solid block
    Vec2 whiteUV;
};

// ---------------------------------------------------------------------------------------------
// Draw list
// ---------------------------------------------------------------------------------------------
struct Vertex { Vec2 pos; Vec2 uv; Col col = 0; };
/// One draw call: `idxCount` indices starting at `idxOffset`, scissored to `clip`. Texture 0 is the
/// font atlas (alpha only); any other value is up to the backend (AddImage).
struct DrawCmd { Rect clip; std::uint32_t idxOffset = 0, idxCount = 0, tex = 0; };

class DrawList {
public:
    std::vector<Vertex> vtx;
    std::vector<std::uint32_t> idx;
    std::vector<DrawCmd> cmds;

    void Init(const FontAtlas* atlas, Rect fullClip);
    void Clear();

    void PushClipRect(const Rect& r, bool intersect = true);
    void PopClipRect();
    Rect ClipRect() const { return clipStack_.empty() ? fullClip_ : clipStack_.back(); }

    void AddRectFilled(Vec2 a, Vec2 b, Col c, float rounding = 0);
    void AddRect(Vec2 a, Vec2 b, Col c, float rounding = 0, float thickness = 1);
    void AddLine(Vec2 a, Vec2 b, Col c, float thickness = 1);
    void AddTriangleFilled(Vec2 a, Vec2 b, Vec2 c, Col col);
    void AddCircleFilled(Vec2 center, float radius, Col c, int segments = 0);
    void AddText(Vec2 pos, Col c, const char* begin, const char* end = nullptr);
    void AddImage(std::uint32_t tex, Vec2 a, Vec2 b, Vec2 uv0 = Vec2(0, 0), Vec2 uv1 = Vec2(1, 1), Col tint = 0xFFFFFFFFu);

private:
    void AddQuadUV(std::uint32_t tex, Vec2 a, Vec2 b, Vec2 uv0, Vec2 uv1, Col c);
    void AddConvexFill(const std::vector<Vec2>& pts, Col c);
    void AddPolyline(const std::vector<Vec2>& pts, bool closed, Col c, float thickness);
    void RoundedPath(std::vector<Vec2>& out, Vec2 a, Vec2 b, float rounding) const;
    DrawCmd& Cmd(std::uint32_t tex);
    const FontAtlas* atlas_ = nullptr;
    Rect fullClip_;
    std::vector<Rect> clipStack_;
};

/// What a backend draws for one frame: window lists back to front, valid until the next NewFrame.
struct DrawData {
    std::vector<const DrawList*> lists;
    Vec2 display;
};

// ---------------------------------------------------------------------------------------------
// Input, style, flags
// ---------------------------------------------------------------------------------------------
enum Key { Key_Enter, Key_Escape, Key_Backspace, Key_Delete, Key_Left, Key_Right, Key_Home, Key_End, Key_Tab, Key_A, Key_Count };

/// Everything the UI needs to know about the outside world for one frame. Plain data: an SDL (or any
/// other) layer fills it; tests fill it by hand. Press and release edges are found by comparing with
/// the previous frame, so hold a state for at least one frame.
struct Input {
    Vec2 mousePos = Vec2(-1e6f, -1e6f);       // far away = no mouse
    bool mouseDown[3] = {false, false, false};
    float wheel = 0;                           // positive = scroll up, in lines
    bool keyDown[Key_Count] = {};
    bool ctrl = false, shift = false;
    std::string text;                          // UTF-8 typed this frame
    float dt = 1.0f / 60.0f;
};

enum ColIdx {
    Col_Text, Col_TextDisabled, Col_WindowBg, Col_TitleBg, Col_TitleBgActive, Col_Border,
    Col_FrameBg, Col_FrameBgHovered, Col_FrameBgActive, Col_Button, Col_ButtonHovered, Col_ButtonActive,
    Col_CheckMark, Col_SliderGrab, Col_Header, Col_HeaderHovered, Col_HeaderActive, Col_Separator,
    Col_ScrollbarBg, Col_ScrollbarGrab, Col_PopupBg, Col_ResizeGrip, Col_TextSelectedBg, Col_Count
};

struct Style {
    Col colors[Col_Count];
    Vec2 windowPadding{8, 8}, framePadding{6, 4}, itemSpacing{8, 4};
    float frameRounding = 3, windowRounding = 4, indentSpacing = 16, scrollbarSize = 10, grabMinSize = 10;
    float itemInnerSpacing = 6;
    bool cursorBlink = true;
    Style();
};

enum WindowFlags {
    Win_None = 0, Win_NoTitleBar = 1, Win_NoMove = 2, Win_NoResize = 4, Win_NoCollapse = 8,
    Win_AlwaysAutoResize = 16, Win_NoScrollbar = 32, Win_NoBackground = 64, Win_Popup = 128, Win_Child = 256
};
enum InputTextFlags { Input_None = 0, Input_EnterReturnsTrue = 1, Input_ReadOnly = 2, Input_AutoSelectAll = 4 };
enum Cond { Cond_Always = 0, Cond_Once = 1 };   // Once = only the first time this window is ever seen

// ---------------------------------------------------------------------------------------------
// Context
// ---------------------------------------------------------------------------------------------
struct Window {
    std::uint32_t id = 0;
    std::string name;
    Vec2 pos, size;
    float scrollY = 0;
    bool collapsed = false, activeThisFrame = false, hasContent = false;
    int flags = 0;
    int lastFrame = -1;
    DrawList own;
    DrawList* dl = nullptr;
    Window* parent = nullptr;
    Window* root = nullptr;
    // layout
    Vec2 cur;                       // top-left of the next item
    float lineY = 0, lineH = 0, lineEndX = 0, lineAlign = 0, pendingAlign = 0;
    bool sameLine = false, alignFresh = false;
    float leftX = 0, rightX = 0, indent = 0;
    Vec2 itemsMax;                  // extent of everything placed so far (screen space)
    Vec2 contentSize;               // measured last frame: extent from content origin
    Rect contentRect;               // clip rect for items
    Vec2 contentOrigin;
    float itemWidth = 0;            // SetNextItemWidth, consumed by the next item
        bool skipItems = false;
    bool hasScrollbar = false;
    Rect lastItemRect;
    std::uint32_t lastItemId = 0;
    bool lastHovered = false, lastClicked = false;
    // columns
    int colCount = 1, colIdx = 0;
    float colLeft0 = 0, colRight0 = 0, colWidth = 0, colRowY = 0, colMaxY = 0;
    // id stack and groups
    std::vector<std::uint32_t> idStack;
    struct Group { Vec2 cur; float lineY, lineH, lineEndX, lineAlign; bool sameLine, alignFresh; Vec2 itemsMax; };
    std::vector<Group> groups;
    std::vector<std::pair<std::uint32_t, Rect>> childRects;   // this frame: child id, clipped rect
    std::vector<std::pair<std::uint32_t, Rect>> prevChildRects;
    Rect prevRect;                  // where the window was last frame (hit testing)
    bool measuring = false;         // auto-sizing window not yet measured: drawn nowhere this frame
    Rect sbTrack;
};

struct InputTextState { int cursor = 0, anchor = 0; float scrollX = 0; };

class Context {
public:
    Context(FontAtlas::GlyphSource rows, int fontScale);
    Style style;
    FontAtlas font;
    Input in, prev;
    Vec2 display;
    int frame = 0;
    double time = 0;
    std::uint32_t hoveredId = 0, activeId = 0, focusId = 0;
    Window* hoveredWin = nullptr;       // innermost window under the mouse (may be a child)
    Window* hoveredRoot = nullptr;
    Window* focusedRoot = nullptr;
    Window* cur = nullptr;
    int errors = 0;                     // API misuse counted (unbalanced End / Pop ...) instead of crashing
    int keyRepeat[Key_Count] = {};
    float keyHeld[Key_Count] = {};
    bool keyPressed[Key_Count] = {};
    std::unordered_map<std::uint32_t, std::unique_ptr<Window>> windows;
    std::vector<Window*> order;         // root windows, back to front
    std::vector<Window*> stack;         // Begin / End nesting
    std::unordered_map<std::uint32_t, bool> treeOpen;
    std::unordered_map<std::uint32_t, InputTextState> inputState;
    std::uint32_t popupId = 0, popupClosedId = 0;
    std::vector<std::pair<int, Col>> colorStack;
    bool nextPosSet = false, nextSizeSet = false;
    Vec2 nextPos, nextSize;
    int nextPosCond = 0, nextSizeCond = 0;
    DrawData drawData;
    bool inFrame = false;
    Vec2 mouseDelta;
    Vec2 dragStartDummy;
};

/// `rows` is the 8x8 glyph source (e.g. okay::Font8x8::Glyph); `fontScale` the integer pixel scale.
Context* CreateContext(FontAtlas::GlyphSource rows, int fontScale = 2);
void DestroyContext(Context* ctx);
void SetCurrentContext(Context* ctx);
Context* GetContext();

/// Start a frame.
void NewFrame(const Input& in, Vec2 displaySize);
/// End the frame and return the draw data (valid until the next NewFrame).
const DrawData& Render();

// ---------------------------------------------------------------------------------------------
// Windows and layout
// ---------------------------------------------------------------------------------------------
void SetNextWindowPos(Vec2 pos, Cond cond = Cond_Always);
void SetNextWindowSize(Vec2 size, Cond cond = Cond_Always);
bool Begin(const char* name, bool* open = nullptr, int flags = Win_None);
void End();
bool BeginScrollRegion(const char* id, Vec2 size = Vec2(0, 0));   // a child window with its own scrolling
void EndScrollRegion();

void SameLine(float offsetFromStart = 0, float spacing = -1);
void Spacing();
void Dummy(Vec2 size);
void Indent(float w = 0);
void Unindent(float w = 0);
void Separator();
void BeginGroup();
void EndGroup();
void Columns(int count = 1);
void NextColumn();
void SetNextItemWidth(float w);
void AlignTextToFramePadding();
Vec2 GetCursorScreenPos();
void SetCursorScreenPos(Vec2 p);
Vec2 GetContentRegionAvail();
Vec2 CalcTextSize(const char* text, const char* end = nullptr);
float GetTextLineHeight();
float GetFrameHeight();
Rect GetItemRect();
Vec2 GetWindowPos();
Vec2 GetWindowSize();
DrawList* GetWindowDrawList();

// ---------------------------------------------------------------------------------------------
// IDs and style stacks
// ---------------------------------------------------------------------------------------------
void PushID(const char* s);
void PushID(int i);
void PopID();
std::uint32_t GetID(const char* s);          // within the current window and ID stack
std::uint32_t GetWindowID(const char* name); // a top-level window by name
void PushStyleColor(ColIdx idx, Col c);
void PopStyleColor(int count = 1);
Col GetColor(ColIdx idx);

// ---------------------------------------------------------------------------------------------
// Widgets. Labels may carry a hidden ID part after "##" ("Name##3").
// ---------------------------------------------------------------------------------------------
void Text(const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
void TextUnformatted(const char* text, const char* end = nullptr);
void TextColored(Col c, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
void TextDisabled(const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
bool Button(const char* label, Vec2 size = Vec2(0, 0));
bool SmallButton(const char* label);
bool Checkbox(const char* label, bool* v);
bool SliderFloat(const char* label, float* v, float lo, float hi, const char* fmt = "%.3f");
bool SliderInt(const char* label, int* v, int lo, int hi, const char* fmt = "%d");
bool DragFloat(const char* label, float* v, float speed = 1.0f, float lo = 0, float hi = 0, const char* fmt = "%.3f");
bool InputText(const char* label, std::string* buf, int flags = Input_None, int maxBytes = 4096);
bool BeginCombo(const char* label, const char* preview);
void EndCombo();
bool Combo(const char* label, int* current, const char* const* items, int count);
bool Selectable(const char* label, bool selected = false, Vec2 size = Vec2(0, 0));
bool TreeNode(const char* label);            // true = open; call TreePop only then
void TreePop();
void CloseCurrentPopup();

// Item queries about the last widget.
bool IsItemHovered();
bool IsItemActive();
bool IsItemClicked();
std::uint32_t GetHoveredId();
std::uint32_t GetActiveId();
std::uint32_t GetFocusId();
bool IsKeyPressed(Key k);
bool IsMouseClicked(int button = 0);
bool IsMouseDown(int button = 0);

} // namespace imui
} // namespace okay
