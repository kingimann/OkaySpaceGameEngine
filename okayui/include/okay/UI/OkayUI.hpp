#pragma once
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <cstring>
// ---------------------------------------------------------------------------
// OkayUI — a tiny immediate-mode UI toolkit that draws THROUGH SDL_Renderer, the
// very same pipeline Dear ImGui uses in the editor. Because it shares that
// pipeline it composes cleanly ON TOP of ImGui with no GL/D3D state conflicts,
// and it is GPU-accelerated by whatever backend SDL selected: Direct3D 11 on
// Windows, Metal on macOS, OpenGL on Linux.
//
// "No STL": this toolkit's own code uses only fixed-capacity C arrays, C strings,
// and integer hot/active tracking — no std::vector/std::string/exceptions. Its
// only dependencies are SDL (for the renderer) and the engine's 8x8 bitmap font.
//
// v1 scope: OkayUI::Button. Usage each frame:
//     OkayUI::BeginFrame(input);                       // latch mouse, reset batch
//     if (OkayUI::Button(id, x, y, w, h, "Play")) ...  // immediate-mode widget
//     OkayUI::EndFrame(renderer);                       // flush — AFTER ImGui draws,
//                                                       // so OkayUI sits on top
// ---------------------------------------------------------------------------
struct SDL_Renderer;
struct SDL_Texture;
namespace okay { class TtfFont; }

namespace OkayUI {

/// A 2D point or size.
struct Vec2 {
    float x = 0.0f, y = 0.0f;
    Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}   // so ported code can write Vec2(x, y) and ImVec2(x, y)
    float& operator[](int i) { return i ? y : x; }
    float operator[](int i) const { return i ? y : x; }
};

struct Vec4 { float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f; };
/// Dear ImGui's colour-vector type under its own name, for ported code that writes `ImVec4(r, g, b, a)` and reads `.x .y .z .w`. It
/// converts both ways with Vec4 (so it can be passed to PushStyleColor and assigned from GetStyleColorVec4).
struct ImVec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
    ImVec4() {}
    ImVec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    ImVec4(const Vec4& v) : x(v.x), y(v.y), z(v.z), w(v.w) {}
    operator Vec4() const { return Vec4{x, y, z, w}; }
};

typedef unsigned Color32;   // a packed colour, 0xAABBGGRR

/// This frame's input, supplied by the host (OkayUI polls nothing itself).
/// Named keys for IsKeyDown / IsKeyPressed / IsKeyReleased. The host fills Input::keyDown[id] with whether each key is held this frame.
enum KeyId : int {
    KeyId_None = 0,
    KeyId_Tab, KeyId_Left, KeyId_Right, KeyId_Up, KeyId_Down, KeyId_PageUp, KeyId_PageDown, KeyId_Home, KeyId_End,
    KeyId_Insert, KeyId_Delete, KeyId_Backspace, KeyId_Space, KeyId_Enter, KeyId_Escape,
    KeyId_A, KeyId_B, KeyId_C, KeyId_D, KeyId_E, KeyId_F, KeyId_G, KeyId_H, KeyId_I, KeyId_J, KeyId_K, KeyId_L, KeyId_M,
    KeyId_N, KeyId_O, KeyId_P, KeyId_Q, KeyId_R, KeyId_S, KeyId_T, KeyId_U, KeyId_V, KeyId_W, KeyId_X, KeyId_Y, KeyId_Z,
    KeyId_0, KeyId_1, KeyId_2, KeyId_3, KeyId_4, KeyId_5, KeyId_6, KeyId_7, KeyId_8, KeyId_9,
    KeyId_F1, KeyId_F2, KeyId_F3, KeyId_F4, KeyId_F5, KeyId_F6, KeyId_F7, KeyId_F8, KeyId_F9, KeyId_F10, KeyId_F11, KeyId_F12,
    KeyId_Ctrl, KeyId_Shift, KeyId_Alt,
    // Punctuation and keypad keys (the host fills Input::keyDown for them; a host that does not leaves them up).
    KeyId_Minus, KeyId_Equal, KeyId_Slash, KeyId_Backslash, KeyId_Period, KeyId_Comma, KeyId_LeftBracket, KeyId_RightBracket,
    KeyId_Keypad0, KeyId_Keypad1, KeyId_Keypad2, KeyId_Keypad3, KeyId_Keypad4, KeyId_Keypad5, KeyId_Keypad6, KeyId_Keypad7, KeyId_Keypad8, KeyId_Keypad9,
    KeyId_KeypadAdd, KeyId_KeypadSubtract, KeyId_KeypadMultiply, KeyId_KeypadDivide, KeyId_KeypadDecimal,
    KeyId_MouseWheelY,   // the wheel as an owner-able "key" (SetItemKeyOwner); the host never reports it down
    // Keys a platform feed (shared/ImGuiPlatformFeed.hpp, okay::platform events) reports: the right-hand modifiers as their own keys, the modifier flags Dear ImGui
    // sends as keys of their own (KeyId_Mod*), and the rest of a full keyboard. KeyId_F13 .. KeyId_F24 are consecutive.
    KeyId_RightCtrl, KeyId_RightShift, KeyId_RightAlt, KeyId_LeftSuper, KeyId_RightSuper, KeyId_ModCtrl, KeyId_ModShift, KeyId_ModAlt, KeyId_ModSuper,
    KeyId_Semicolon, KeyId_Apostrophe, KeyId_GraveAccent, KeyId_CapsLock, KeyId_PrintScreen, KeyId_ScrollLock, KeyId_Pause, KeyId_NumLock,
    KeyId_KeypadEqual, KeyId_Oem102, KeyId_Menu,
    KeyId_F13, KeyId_F14, KeyId_F15, KeyId_F16, KeyId_F17, KeyId_F18, KeyId_F19, KeyId_F20, KeyId_F21, KeyId_F22, KeyId_F23, KeyId_F24,
    KeyId_COUNT,
    // The left modifier keys are the modifier keys; the right ones are their own ids (above).
    KeyId_LeftCtrl = KeyId_Ctrl, KeyId_LeftShift = KeyId_Shift, KeyId_LeftAlt = KeyId_Alt,
    // Dear ImGui's spellings of the same keys (aliases, not new keys; the numeric-keypad Enter arrives as Enter).
    KeyId_UpArrow = KeyId_Up, KeyId_DownArrow = KeyId_Down, KeyId_LeftArrow = KeyId_Left, KeyId_RightArrow = KeyId_Right,
    KeyId_KeypadEnter = KeyId_Enter
};

struct Input;
/// The most text the clipboard holds (bytes, with the end mark).
inline constexpr int kClipboardMax = 4096;
struct Input {
    float       mouseX    = 0.0f;
    float       mouseY    = 0.0f;
    bool        mouseDown = false;     // left mouse button held this frame
    bool        rightDown = false;     // right mouse button held this frame (context menus)
    bool        middleDown = false;    // middle mouse button held this frame (IsMouseDown(2), GetMouseDragDelta(2))
    bool        blocked   = false;     // another layer (e.g. ImGui) owns the mouse -> ignore clicks
    // Keyboard (for TextField). The host forwards SDL_TEXTINPUT text and key edges
    // only when OkayUI should receive them (e.g. ImGui isn't capturing the keyboard).
    const char* text      = nullptr;   // UTF-8 characters typed this frame, or null
    bool        backspace = false;     // Backspace pressed this frame (delete last char)
    float       wheel     = 0.0f;      // mouse-wheel delta this frame (+up / -down), for scrolling
    // More keys for TextField, all "pressed this frame" edges like `backspace` (the host decides
    // about key repeat by sending the edge again), plus `shift` as a held state. Hosts that set
    // none of these get the old behaviour (typing and Backspace).
    bool        del       = false;     // Delete
    bool        left      = false;     // Left arrow (moves the caret; with shift extends the selection)
    bool        right     = false;     // Right arrow
    bool        home      = false;     // Home
    bool        end       = false;     // End
    bool        enter     = false;     // Enter / Return
    bool        escape    = false;     // Escape (drops keyboard focus; closes the innermost open menu)
    bool        copy      = false;     // the copy shortcut (Ctrl+C) was pressed this frame
    bool        cut       = false;     // Ctrl+X
    bool        paste     = false;     // Ctrl+V
    bool        up        = false;     // Up arrow (moves the highlight in an open menu)
    bool        down      = false;     // Down arrow
    bool        selectAll = false;     // the select-all shortcut (Ctrl+A)
    bool        shift     = false;     // shift held
    bool        ctrl      = false;     // Ctrl held (Ctrl+click on a drag or slider box types a number into it)
    float       displayW  = 0.0f;      // size of the area OkayUI draws into (BeginPopupModal centres in it); 0 = unknown
    float       displayH  = 0.0f;
    bool        keyDown[KeyId_COUNT] = {};   // which named keys are held this frame (see KeyId); Ctrl/Shift also follow `ctrl` / `shift`
    double      time      = 0.0;       // seconds since start (for double clicks and GetTime); 0 = derive from the frame count at 60 fps
    // Input method (IME) composition. While the user is composing, the host passes the current preedit text EVERY frame (it is a state,
    // not an edge): the focused text field or CodeEdit shows it underlined at the caret and the keys (Backspace, arrows, Enter,
    // Escape, ...) are left to the input method. Passing null or "" ends the composition; with nothing else that cancels it (the text
    // is untouched). The finished text arrives once, in `commit`, and is inserted like typed text (replacing a selection).
    const char* composition      = nullptr;   // preedit, UTF-8: control characters and invalid bytes are dropped, 256 bytes at most
    int         compositionCaret = -1;        // byte offset of the caret inside `composition` (-1 or past the end: at its end)
    const char* commit           = nullptr;   // text the input method committed this frame
};

/// Visual style (colors are RGBA, 0..255). Mutable via Style().
struct Theme {
    unsigned char bg[4]      = { 54,  58,  66, 255};
    unsigned char bgHover[4] = { 72,  78,  90, 255};
    unsigned char bgDown[4]  = { 36,  40,  48, 255};
    unsigned char border[4]  = { 18,  20,  24, 255};
    unsigned char text[4]    = {236, 239, 245, 255};
    unsigned char panel[4]   = { 40,  43,  50, 235};   // Panel() background
    unsigned char track[4]   = { 26,  28,  34, 255};   // Slider/ProgressBar groove
    unsigned char accent[4]  = { 84, 150, 240, 255};   // fill / handle / checkmark
    float borderPx  = 1.0f;
    float textScale = 2.0f;    // 8x8 font cell * scale (2 -> 16px-tall glyphs)
    float rounding  = 4.0f;    // corner radius (px) for panels/buttons/inputs; 0 = square
    unsigned char textDim[4] = {150, 155, 166, 255};   // TextDisabled / secondary text
    unsigned char titleBg[4] = { 36,  40,  48, 255};   // window title bars
    unsigned char field[4]   = { 26,  28,  34, 255};   // text-field fill
    float windowRounding = 4.0f;   // window corners
    float windowPadding  = 10.0f;  // inside a window, around its content
    bool  windowShadow   = false;  // soft drop shadow under windows
    bool  caretBlink     = true;   // text caret blinks (off: always visible, for tests and screenshots)
    // Colours beyond the eight fields above (the Col_* ids from Col_TextDisabled up), kept by PushStyleColor / SetStyleColor:
    // colors[id] holds the value and bit `id` of colorSet says it is in force. A bit that is clear means "follow the base fields".
    unsigned char      colors[64][4] = {};
    unsigned long long colorSet = 0;
};

/// The OkaySpace launcher's look for game UI: its font (Roboto, smooth TrueType
/// text instead of the 8x8 bitmap), its "Slate" colours (blue-tinted dark panels,
/// hairline borders, accent for interactive states), rounder controls, pill tabs,
/// shadows. The player turns it on; `false` restores the classic look. Needs the
/// SDL_Renderer path (EndFrame(SDL_Renderer*)).
void UseLauncherLook(bool on = true);
bool LauncherLook();
/// The Slate theme on its own (what UseLauncherLook applies), e.g. to tweak and reapply.
Theme SlateTheme();

/// Latch this frame's input and reset the geometry batch.
void BeginFrame(const Input& in);

/// Static text at (x, y), top-left aligned, in the theme's text color.
void Label(float x, float y, const char* text);

/// A filled background container (panel) with a border. Purely decorative —
/// lay widgets on top of it. Good for grouping a HUD or a settings box.
void Panel(float x, float y, float w, float h);

/// An immediate-mode button. `id` must be unique and stable across frames.
/// Returns true on the frame the press is RELEASED while still inside the rect
/// (a click), matching how desktop buttons behave.
bool Button(int id, float x, float y, float w, float h, const char* label);

/// A checkbox of side `size` at (x, y) with `label` drawn to its right. Toggles
/// *value on click; returns true on the frame the value changed. `value` required.
bool Checkbox(int id, float x, float y, float size, const char* label, bool* value);

/// A sliding on/off switch (pill track + knob) `w`*`h` at (x, y), `label` to its
/// right. Toggles *value on click; the knob slides and the track lights the accent
/// colour when on. Returns true the frame the value changed. `value` required.
bool ToggleSwitch(int id, float x, float y, float w, float h, const char* label, bool* value);

/// A horizontal slider over [minV, maxV]. Click or drag the groove to set *value;
/// returns true on any frame the value changed. `value` required.
bool Slider(int id, float x, float y, float w, float h, float* value, float minV, float maxV);

/// A non-interactive progress/stat bar: fills `t` (clamped to [0,1]) of the groove
/// with the accent color. Perfect for health/hunger/XP bars.
void ProgressBar(float x, float y, float w, float h, float t);

/// One option of a mutually-exclusive group: sets *value to `option` when clicked.
/// Drawn filled when *value == option, with `label` to the right. Returns true on
/// the frame the selection changed. `value` required.
bool RadioButton(int id, float x, float y, float size, const char* label, int* value, int option);

/// A tab in a tab bar: clicking sets *current to `index`. Returns true while THIS
/// tab is the selected one (so: `if (Tab(...)) { draw this tab's contents }`).
/// `current` required.
bool Tab(int id, float x, float y, float w, float h, const char* label, int* current, int index);

/// A single-line text field editing the caller-owned buffer `buf` (capacity `cap`,
/// always kept NUL-terminated). Click to focus and place the caret (drag selects);
/// typed text is inserted at the caret (replacing a selection), Backspace / Delete /
/// Left / Right / Home / End / select-all work on whole UTF-8 characters, Escape drops
/// focus. Control characters and invalid UTF-8 are dropped; a character that does not
/// fit in `cap` is dropped whole. Keys come from Input. Returns true on any frame the
/// text changed. The buffer is owned by the caller — no allocation here.
/// Flags for TextField / InputText.
enum InputTextFlags {
    InputText_EnterReturnsTrue = 1,   // return true when Enter is pressed instead of on every edit
    InputText_ReadOnly         = 2,   // selection and caret work, edits are ignored
    InputText_AutoSelectAll    = 4,   // clicking into the field selects all of its text
    InputText_Password         = 8,   // shows one '*' per byte of text; copy and cut are off
    InputText_CharsNoBlank     = 16,  // typed spaces and tabs are dropped (paste is not filtered)
    InputText_CharsDecimal     = 32,  // only 0-9 . - + * / are typed (paste is not filtered)
    // Accepted for ported code, no effect here: multi-line Enter handling, resize / always callbacks, Tab typing.
    InputText_CtrlEnterForNewLine = 64, InputText_CallbackResize = 128, InputText_CallbackAlways = 256, InputText_AllowTabInput = 512,
    // Real, with the callback overloads of InputText / InputTextWithHint (OkayUICompat.hpp): Up / Down call it while the field
    // has the keyboard (EventKey = KeyId_Up / KeyId_Down), Tab calls it for completion (EventKey = KeyId_Tab).
    InputText_CallbackHistory = 1024, InputText_CallbackCompletion = 2048,
};
bool TextField(int id, float x, float y, float w, float h, char* buf, int cap, unsigned flags = 0);

/// Flush the batched geometry to the renderer. Call AFTER ImGui has rendered so
/// OkayUI draws on top; the renderer's blend mode is saved and restored so nothing
/// else is disturbed.
void EndFrame(SDL_Renderer* renderer);
/// The drawing half of EndFrame(SDL_Renderer*) alone: draws the frame that EndFrameData already finished through the SDL
/// renderer (nothing is finished or built here, so the toasts must have been built by EndFrameData(w, h)). For a host that
/// finishes the frame first and then decides which renderer draws it (OkayUI::render::SdlHost falls back to this one).
void DrawFinishedFrame(SDL_Renderer* renderer);

// ---- Custom backends (e.g. Direct3D 11, the software rasteriser) --------------
// The frame's geometry as plain data, valid after the frame's widget calls (call
// EndFrameData first so the overlay batch is merged in) until the next BeginFrame.
// It is the same data EndFrame(SDL_Renderer*) draws, so every backend can match it.
//
// Vertices are `DrawVertex` (20 bytes, identical in layout to SDL_Vertex):
//   offset 0: float position[2]   offset 8: uint8 color[4] (RGBA)   offset 12: float uv[2]
// Indices are 32-bit, three per triangle. The index buffer is split into commands
// (`DrawCmd`): consecutive index ranges that share a clip rectangle and a texture,
// in draw order. A backend that draws only `vertices` + `indices` (the Direct3D 11
// backend does) ignores clipping and textures.
struct DrawVertex {
    float         x, y;
    unsigned char r, g, b, a;
    float         u, v;
};
struct DrawCmd {
    int         idxOffset = 0;      // first index of the range
    int         idxCount  = 0;      // number of indices (a multiple of 3)
    bool        clipOn    = false;  // false: no scissor
    int         clipX = 0, clipY = 0, clipW = 0, clipH = 0;   // scissor, pixels, top-left origin
    /// nullptr: untextured (flat colour, ignore uv). DrawData::atlasTexture: the launcher
    /// look's glyph atlas (white pixels, coverage in alpha, multiplied by the vertex colour).
    /// Anything else is a backend-owned texture (an Image, a fallback font) that only the
    /// SDL_Renderer backend can resolve.
    const void* texture   = nullptr;
};
struct ImDrawData;
struct DrawData {
    const void*     vertices     = nullptr;   // DrawVertex[vertexCount]
    int             vertexCount  = 0;
    const int*      indices      = nullptr;
    int             indexCount   = 0;
    int             vertexStride = 0;         // bytes per vertex (sizeof(DrawVertex))
    const DrawCmd*  cmds         = nullptr;   // the index buffer split by clip rect and texture
    int             cmdCount     = 0;
    /// The launcher look's atlas (RGBA8, atlasW x atlasH, top row first), or null in the
    /// classic look. A DrawCmd whose texture == atlasTexture samples it.
    const void*          atlasTexture = nullptr;
    const unsigned char* atlasRgba    = nullptr;
    int                  atlasW = 0, atlasH = 0;
    /// Shapes (quads, triangles, glyphs) that did not fit the batch this frame and are missing from the picture.
    /// The batch holds 262,144 vertices and 393,216 indices, each for the main and the overlay layer. Zero in
    /// any normal UI. When it is not zero OkayUI also writes a message to stderr (the first few frames).
    int                  dropped = 0;
    /// `ImDrawData* dd = GetDrawData();` (a Dear ImGui renderer's input): builds the ImDrawData view of this draw data (see the shapes above). The
    /// pointer is to one static object that the next conversion rebuilds; it is valid until then.
    operator ImDrawData*() const;
};
DrawData GetDrawData();
/// The TrueType font whose glyph atlas a DrawCmd::texture stands for (a fallback font for characters the launcher
/// look's own font lacks), or null when `texture` is not one. A custom backend uploads that font's atlas itself.
okay::TtfFont* FallbackFontOf(const void* texture);
/// The same count as DrawData::dropped, for the frame being built or the last one finished.
int DroppedGeometry();

/// Finalize the frame's interaction state WITHOUT drawing — for custom backends that
/// submit GetDrawData() themselves instead of calling EndFrame(SDL_Renderer*).
void EndFrameData();
/// The same, for a backend that knows its output size: also builds the toast messages (Toast()), which
/// EndFrame(SDL_Renderer*) builds with the renderer's size and EndFrameData() cannot.
void EndFrameData(int outputWidth, int outputHeight);

// ---------------------------------------------------------------------------
// Auto-layout (ImGui-style). Everything above takes explicit coordinates; the API
// below stacks widgets automatically inside a window, so you write a UI as a
// sequence of calls instead of placing every rect by hand. Widget IDs are derived
// from their label text (like ImGui), so most calls take no id.
// ---------------------------------------------------------------------------

/// Begin an auto-layout window at (x, y) sized w*h. The position is the INITIAL
/// placement; the window is draggable by its title bar thereafter (state keyed by
/// the title). A caret on the left of the title bar collapses/expands the window.
/// Returns false when collapsed — skip the widget calls but ALWAYS pair with End().
/// Pass `p_open` to add a close [x] button that sets *p_open = false when clicked.
bool Begin(const char* title, float x, float y, float w, float h, bool* p_open = nullptr);
/// The ImGui-style window: position and size come from SetNextWindowPos / SetNextWindowSize (else a first-use default of
/// (60, 60) and 320 x 240), and afterwards the user can drag it by the title bar and resize it by the grip in the corner.
/// `cond` says when the call takes effect: always (and then the window cannot be dragged / resized), only the first time the
/// window is ever seen, or each time it appears again after not being drawn for a frame.
enum Cond : int { Cond_Always = 1, Cond_Once = 2, Cond_FirstUseEver = 4, Cond_Appearing = 8 };
/// Window flags (OkayUI's own values; they are not the Dear ImGui numbers).
///  NoTitleBar: no title bar, so no collapse, close button or drag handle either (docked windows use it).
///  NoScrollbar: a window whose content is taller than it scrolls with the wheel; this hides the bar (the wheel still works).
///  NoBackground: the body is not drawn (the title bar is, unless NoTitleBar), so only the widgets show.
///  NoCollapse: no collapse caret, the title bar cannot fold the window.
///  AlwaysAutoResize: the window is as big as its content (one frame behind; the first frame is not shown, it only measures).
///  NoFocusOnAppearing: a window that appears (is drawn after a frame without it) does not take focus or come to the front.
///  NoBringToFront: clicking never raises the window and it sits below every other window (docked content uses it).
/// Windows overlap in z-order: a click on a window raises it (and focuses it) and is not seen by widgets of windows behind
/// it, nor by docked content under it. See docs/ui/own_ui_toolkit.md section 4.32.
enum WindowFlags : int {
    Window_None = 0, Window_NoResize = 1, Window_NoMove = 2, Window_NoTitleBar = 4,
    Window_NoScrollbar = 8, Window_NoBackground = 16, Window_NoCollapse = 32, Window_AlwaysAutoResize = 64,
    Window_NoFocusOnAppearing = 128, Window_NoBringToFront = 256,
};

void End();
/// Pin the next Begin() at its (x, y) this frame (it can't be dragged away): for
/// overlays anchored to the screen, such as a dialogue box or a quest tracker.
void PinNextWindow();
/// Make the next Begin() a bare panel: pinned at its (x, y), sized w*h, with no title bar, no collapse caret, no close
/// button, no dragging and no frame or background of its own: just a layout region that clips to its rectangle. For a host that gives OkayUI a rectangle of its own window to fill
/// (the launcher's pages, the editor's panels), where a title bar would repeat what the host already shows. The title
/// passed to Begin still names the window (it keys the widget ids and the scroll state).
void BareNextWindow();
/// Give the next Begin() a resize grip in its bottom-right corner (drag it to resize; the w and h
/// passed to Begin are the starting size). The size is remembered per window. `minW`/`minH` bound it.
void ResizableNextWindow(float minW = 100.0f, float minH = 60.0f);

/// A scrolling sub-region of `w`*`h` at the cursor. Widgets called until EndChild()
/// stack inside it and are CLIPPED to the box; if they overflow vertically the region
/// scrolls (mouse wheel when hovered, or drag the scrollbar). `id` keys the scroll
/// position. `w`/`h` <= 0 fill the remaining width / use a default height. Always pair
/// with EndChild(). Returns true (visible) — call EndChild regardless.
bool BeginChild(const char* id, float w = 0.0f, float h = 0.0f, bool border = true);
/// Dear ImGui's form, BeginChild(id, size, childFlags, windowFlags), and its ChildFlags are in OkayUICompat.hpp.
void EndChild();
/// The scroll offset (pixels from the top) of the innermost open BeginChild region, and the largest offset its content
/// allowed on the last frame (0 until the region has been laid out once). Both are 0 outside a child.
float GetScrollY();
float GetScrollMaxY();
/// Set the innermost child's scroll offset; it is clamped when EndChild measures the content, so SetScrollY(1e9) just
/// before EndChild scrolls to the end (the next frame shows it). Negative or non-finite values mean the top.
void SetScrollY(float y);

/// Manually clip subsequent geometry to a rectangle (screen coords). Nestable and
/// intersected with any enclosing clip. Pair with PopClipRect. (BeginChild uses this.)
void PushClipRect(float x, float y, float w, float h);
void PopClipRect();

/// Keep the next widget on the current line instead of starting a new one
/// (`spacing` < 0 uses the theme default gap).
void SameLine(float spacing = -1.0f);
/// The Dear ImGui two-argument form: stay on the line, and when `offsetFromStartX` > 0 start the next item that far from the left edge of
/// the content (never left of where the previous item ends); `spacing` >= 0 is the gap when the offset is 0 (negative: the default gap).
void SameLine(float offsetFromStartX, float spacing);
/// Set the width (in pixels) of the NEXT full-width widget (slider, input, combo,
/// plot, ...). One-shot: applies to just the next item. Clamped to the space left.
void SetNextItemWidth(float w);
/// Set a width for ALL following full-width widgets until PopItemWidth. Nestable.
void PushItemWidth(float w);
void PopItemWidth();
/// A full-width horizontal divider.
void Separator();
/// Vertical empty space of height `h`.
void Spacing(float h = 8.0f);
/// Reserve an invisible `w`*`h` block at the cursor (for manual spacing/alignment).
void Dummy(float w, float h);
void Dummy(Vec2 size);
/// Shift the content's left edge right (Indent) or back left (Unindent). `w` <= 0
/// uses the default step. Pair each Indent with an Unindent.
void Indent(float w = 0.0f);
void Unindent(float w = 0.0f);
/// A small filled dot at the cursor (list marker). BulletText draws a dot + text.
void Bullet();
void BulletText(const char* s);
/// A line of text at the layout cursor.
void Text(const char* s);
/// Text in an explicit RGB color (0..255) — for labels, warnings, headings.
void TextColored(unsigned char r, unsigned char g, unsigned char b, const char* s);
/// Dimmed text (hints, secondary info).
void TextDisabled(const char* s);
/// Until PopTextWrapPos, Text, TextColored and TextDisabled wrap their words. `width` 0 wraps at the right edge
/// of the window; a positive `width` wraps at that many pixels (or the right edge, whichever is nearer). This
/// is a width, not the x position the Dear ImGui call takes. Not nested: Pop turns it off.
/// Treat everything drawn until EndGroup as one item: GetItemRect, IsItemHovered and SameLine then refer to the
/// whole block (a column of widgets next to another column). Groups nest up to 8 deep; a missing EndGroup is
/// cleared at the start of the next frame.
void BeginGroup();
void EndGroup();
void PushTextWrapPos(float width = 0.0f);
void PopTextWrapPos();
/// A labeled divider: a line with `label` at the left (ImGui's SeparatorText).
void SeparatorText(const char* label);

// ImGui-style widget overloads that auto-place inside the current window (id is
// hashed from the label). The explicit-coordinate versions above still work for
// free-form placement.
bool Button(const char* label);
/// A button of a given size: `size.x` > 0 is the width in pixels (0 fits the label, negative fills the line), `size.y` > 0 the height (0 is the row height).
bool Button(const char* label, Vec2 size);
/// A compact button (tight padding, no full-line height) — for inline actions.
bool SmallButton(const char* label);
/// A row of tabs: clicking a tab sets *current to its index. Draw the selected
/// tab's contents below based on *current. `labels` and `current` required.
void TabBar(const char* const* labels, int count, int* current);
/// Tabs with their own bodies, the usual way to build a tabbed panel:
///     if (BeginTabBar("main")) {
///         if (BeginTabItem("Scene")) { ...widgets for this tab...; EndTabItem(); }
///         if (BeginTabItem("Game", &gameOpen)) { ...; EndTabItem(); }     // with a bool*: a small x closes the tab
///         if (TabItemButton("+", Tab_Trailing)) addTab();                  // a button in the bar, not a tab
///         EndTabBar();
///     }
/// The chosen tab is remembered per bar (the first one at the start; Tab_SetSelected on a tab chooses it). Only the chosen tab's
/// BeginTabItem returns true. If the chosen tab disappears the first one is chosen. A bar does not nest inside another bar.
enum TabItemFlags : int { Tab_None = 0, Tab_SetSelected = 1, Tab_Trailing = 2, Tab_NoTooltip = 4 /* accepted, there are no tab tooltips */ };
bool BeginTabBar(const char* id);
bool BeginTabItem(const char* label, bool* open = nullptr, int flags = 0);
/// The same flags under the names ported code uses (aliases of the Tab_* values).
enum TabItemFlagNames : int { TabItem_None = 0, TabItem_SetSelected = Tab_SetSelected, TabItem_Trailing = Tab_Trailing, TabItem_NoTooltip = Tab_NoTooltip };
void EndTabItem();
bool TabItemButton(const char* label, int flags = 0);
void EndTabBar();
bool Checkbox(const char* label, bool* value);
/// Auto-placed sliding on/off switch (pill + knob) with `label` to its right.
bool ToggleSwitch(const char* label, bool* value);
bool RadioButton(const char* label, int* value, int option);
bool SliderFloat(const char* label, float* value, float minV, float maxV);
/// An integer slider over [minV, maxV]. Edits *value in place; true on change.
bool SliderInt(const char* label, int* value, int minV, int maxV);
/// How a number is shown (the printf-style `fmt` of Dear ImGui's drag, slider and float input widgets). `fmt` holds literal text and at most
/// ONE conversion: %d %i %u %x %X (whole numbers), %f %F %e %E %g %G (decimals), with the printf flags `-+ 0#`, a width, a precision and the
/// length marks h l ll z j t q L (skipped); `%%` is a percent sign. A float shown with a whole-number conversion is rounded first, a whole
/// number shown with a decimal conversion is converted. Anything else (%s, %n, a second conversion, a lone % at the end) is refused: the
/// text becomes the default ("%d" or "%.3f") and false is returned, so a bad format cannot reach printf. `*decimals` (optional) gets the number of
/// decimals the text shows (%f: its precision, 6 by default; whole-number conversions: 0), or -1 when that is not known (%g, %e, no conversion).
bool FormatNumber(char* out, int cap, const char* fmt, double value, bool isInt, int* decimals = nullptr);
/// Set the format and flags of the NEXT Drag*, SliderFloat/SliderInt or InputFloat widget (used up by that widget; a null `fmt` keeps the widget's
/// own default). `fmtMax` is the second cell's format of DragFloatRange2. `noTypedInput` turns Ctrl+click typing off for that widget (Dear ImGui's
/// NoInput); `noRoundToFormat` false rounds a slider's or typed value to the decimals of `fmt`. A slider shows its value (centred) only when
/// a format was set; without one it stays as before. `logarithmic` (sliders only) spaces the values by a factor instead of by a step; it works when the
/// range keeps one sign and does not touch 0 (the handle position is then log(value); for other ranges the slider stays linear).
void SetNextNumberFormat(const char* fmt, bool noTypedInput = false, bool noRoundToFormat = true, const char* fmtMax = nullptr, bool logarithmic = false);
/// The text the last number widget showed in its cell `cell` (0 for single widgets, 0..3 for vector cells, -1 for the last cell drawn); "" when
/// nothing was shown. For tests and for hosts that read a value back.
const char* GetLastNumberText(int cell = -1);
/// A vertical slider `w`*`h` tall — drag the handle up/down over [minV, maxV].
/// Edits *value in place; returns true on change. Great for mixers/levels.
bool VSliderFloat(const char* label, float w, float h, float* value, float minV, float maxV);
/// A rotary knob of diameter `size`. Drag up/down (or left/right) to change *value
/// over [minV, maxV]; an indicator points to the current value. True on change.
bool Knob(const char* label, float* value, float minV, float maxV, float size = 0.0f);
void ProgressBar(float fraction, const char* overlay = nullptr);
/// ProgressBar with a size (`size.x` <= 0: the width left, `size.y` <= 0: one row). A negative fraction is an indeterminate bar: a block
/// that sweeps back and forth with the time (GetTime).
void ProgressBar(float fraction, Vec2 size, const char* overlay = nullptr);
/// A line graph of `values[0..count)`. If scaleMin >= scaleMax the range is taken
/// from the data. `height` <= 0 uses a default. Great for FPS/telemetry HUDs.
void PlotLines(const char* label, const float* values, int count,
               float scaleMin = 0.0f, float scaleMax = 0.0f, float height = 0.0f);
/// A bar chart of `values[0..count)` (same scaling rules as PlotLines).
void PlotHistogram(const char* label, const float* values, int count,
                   float scaleMin = 0.0f, float scaleMax = 0.0f, float height = 0.0f);
/// The full plots (okayui/src/OkayUIPlots.cpp): a ring buffer start, an overlay text, a size, values from a getter, and a hover: the point
/// or bar under the pointer is drawn in the Col_PlotLinesHovered / Col_PlotHistogramHovered colour and a tooltip names it. Non-finite
/// values leave a gap (lines) or an empty slot (bars); values outside the scale are clamped to its edge. Both return the index of
/// the hovered value (the index into the data, not the ring), or -1. `count` <= 0 or no data: nothing is drawn.
struct PlotOptions {
    int         valuesOffset = 0;        // the first value shown is data[valuesOffset], then on round the ring
    const char* overlay = nullptr;       // text centred at the top of the plot
    float       scaleMin = 0.0f, scaleMax = 0.0f;   // scaleMin >= scaleMax: the range of the data
    Vec2        size;                    // size.x <= 0: the width left; size.y <= 0: 2.4 rows
};
int PlotLines(const char* label, const float* values, int count, const PlotOptions& options);
int PlotLines(const char* label, float (*getter)(void* data, int index), void* data, int count, const PlotOptions& options);
int PlotHistogram(const char* label, const float* values, int count, const PlotOptions& options);
int PlotHistogram(const char* label, float (*getter)(void* data, int index), void* data, int count, const PlotOptions& options);
/// A read-only "label: value" row (ImGui's LabelText) — value on the left, label right.
void LabelText(const char* label, const char* value);

// ---- Images (textured quads) ---------------------------------------------------
// GPU textures drawn through the same batch. Requires the SDL_Renderer backend
// (EndFrame); custom GetDrawData backends receive untextured geometry only.
/// Draw a texture at explicit coordinates, tinted by (r,g,b,a) — white = as-is.
void DrawTexture(SDL_Texture* tex, float x, float y, float w, float h,
                 unsigned char r = 255, unsigned char g = 255, unsigned char b = 255, unsigned char a = 255);
/// Auto-layout image `w`*`h` at the cursor.
void Image(SDL_Texture* tex, float w, float h,
           unsigned char r = 255, unsigned char g = 255, unsigned char b = 255, unsigned char a = 255);
/// A clickable image button (`id` for identity). Returns true the frame it's clicked.
bool ImageButton(const char* id, SDL_Texture* tex, float w, float h);
bool InputText(const char* label, char* buf, int cap, unsigned flags = 0);
/// A multi-line text box editing the caller's buffer (capacity `cap`, always NUL-terminated), built on OkayUI::CodeEdit without the gutter,
/// highlighting or auto-indent (okayui/src/OkayUIText.cpp). `size.x` <= 0 fills the width left (less the label), `size.y` <= 0 is five rows.
/// Enter breaks the line, Tab inserts a tab, the mouse and keys select, copy, cut, paste, undo (Ctrl+Z) and scroll. Returns true on any
/// frame the text changed. An edit that would not fit in `cap` is cut at a whole character and the editor's undo history restarts.
/// Flags: InputText_ReadOnly. Text with CRLF, control characters or bad UTF-8 is cleaned the way CodeEdit cleans it (the buffer is
/// rewritten only when the user edits). The state of up to 16 boxes is kept (by label); the least recently drawn is forgotten.
bool InputTextMultiline(const char* label, char* buf, int cap, Vec2 size = Vec2{}, unsigned flags = 0);
/// InputText that shows `hint` in a dim colour while the field is empty (a placeholder such as "Search...").
bool InputTextWithHint(const char* label, const char* hint, char* buf, int cap, unsigned flags = 0);
/// The next text field drawn takes the keyboard focus (and puts the caret at the end of its text).
void SetKeyboardFocusHere();
/// The next item, when it is on the line of a taller item (after SameLine), is centred on that item instead of sharing its top: Dear
/// ImGui lines up a small button with the text of a frame-tall row this way. Applies to one item.
void CenterNextOnLine();
/// An integer field with [-]/[+] stepper buttons that change *value by `step`.
/// Returns true on any frame the value changed.
bool InputInt(const char* label, int* value, int step = 1);

/// A collapsing section header. Returns true while expanded — put the section's
/// widgets in the if-body. The open/closed state is remembered per label.
bool CollapsingHeader(const char* label);
/// The same, with the state a header starts in the first time it is seen (later frames use what the person chose).
bool CollapsingHeader(const char* label, bool defaultOpen);

/// A dropdown selecting one of items[0..count) into *current. The open list draws
/// on top of later widgets (overlay pass). Returns true the frame the selection
/// changed. `items` and `current` required.
bool Combo(const char* label, const char* const* items, int count, int* current);
/// A combo whose list is whatever you draw: the box shows `preview`; while it is open, the widgets between
/// BeginCombo and EndCombo (usually Selectable rows, each calling CloseCurrentPopup when chosen) fill a popup under
/// the box, as wide as the box. Call EndCombo only when BeginCombo returned true. Clicking the box again, or
/// anywhere outside the list, closes it.
bool BeginCombo(const char* label, const char* preview);
void EndCombo();

/// If the most recently issued widget is hovered, show a tooltip box at the cursor.
/// Call right after the widget you want to annotate.
void Tooltip(const char* text);
/// A tooltip at the cursor, always (the caller decides when: `if (IsItemHovered()) SetTooltip("%d items", n);`). printf-style;
/// lines are split at "\n", and the box stays inside the display (Input::displayW/H) by flipping to the left / above.
void SetTooltip(const char* fmt, ...);
/// Draw text as is (no formatting). With `textEnd` only the characters before it.
void TextUnformatted(const char* text, const char* textEnd = nullptr);
/// Accepted for compatibility: text in a row is already vertically centred with framed widgets, so this does nothing.
void AlignTextToFramePadding();


/// Drag-to-scrub a float: hold and move the cursor left/right to change *v by
/// `speed` per pixel. If minV < maxV the value is clamped. Returns true on change.
bool DragFloat(const char* label, float* v, float speed = 0.1f, float minV = 0.0f, float maxV = 0.0f);
/// Drag-to-scrub an int (same idea). minV < maxV clamps. Returns true on change.
bool DragInt(const char* label, int* v, float speed = 0.25f, int minV = 0, int maxV = 0);
/// Two, three or four drag cells in one row that share one label (an inspector's Position / Rotation / Scale
/// fields). Each cell scrubs like DragFloat; minV < maxV clamps every cell. True on any frame a cell changed.
bool DragFloat2(const char* label, float v[2], float speed = 0.1f, float minV = 0.0f, float maxV = 0.0f);
bool DragFloat3(const char* label, float v[3], float speed = 0.1f, float minV = 0.0f, float maxV = 0.0f);
bool DragFloat4(const char* label, float v[4], float speed = 0.1f, float minV = 0.0f, float maxV = 0.0f);
/// The integer versions (same rules as DragInt for speed and clamping).
bool DragInt2(const char* label, int v[2], float speed = 0.25f, int minV = 0, int maxV = 0);
bool DragInt3(const char* label, int v[3], float speed = 0.25f, int minV = 0, int maxV = 0);
bool DragInt4(const char* label, int v[4], float speed = 0.25f, int minV = 0, int maxV = 0);
/// A low and a high value in one row: the low cell never goes above the high one and the high never below the low.
bool DragFloatRange2(const char* label, float* lo, float* hi, float speed = 0.1f, float minV = 0.0f, float maxV = 0.0f);
/// Dear ImGui's formatted fields and framed sliders (StyleVar_FramedWidgets pushed): the format forms in OkayUICompat.hpp (DragFloat(.., fmt, flags),
/// DragFloat2/3, SliderFloat(.., fmt, flags), SliderFloat2/3, SliderInt) draw as Dear ImGui does while that style is pushed: a drag field is a
/// frame in the Col_FrameBg colours with the value in the middle (Ctrl+click or a double click types a number); a slider is a frame, a grab
/// block (16 pixels, or one step of an integer range) and the value in the middle. Cells of a row are StyleVar_ItemInnerSpacing.x apart
/// (default 4) and the label follows at that gap (default 8). Slider_Logarithmic moves on a logarithmic scale of a positive range.
/// Without the style they keep OkayUI's own look (SetNextNumberFormat). The hooks below are what OkayUICompat.cpp calls.
bool FramedNumbersOn();
bool FramedDragFloatN(const char* label, float* v, int n, float speed, float minV, float maxV, const char* format, bool logarithmic);
bool FramedSliderFloat(const char* label, float* v, float minV, float maxV, const char* format, bool logarithmic);
bool FramedSliderInt(const char* label, int* v, int minV, int maxV, const char* format);
bool FramedSliderFloatN(const char* label, float* v, int n, float minV, float maxV, const char* format);
/// The editor's search field: a text field with a magnifier drawn at the left (the text starts after it), `hint` while it is empty, and a
/// clear button (a cross) at the right while it has text. A click on the cross empties the buffer, gives up the keyboard and returns true.
/// `width` > 0 is the width; 0 or less fills the width left minus |width|. At least three rows wide. True on any frame the text changed.
bool SearchField(const char* id, const char* hint, char* buf, int cap, float width = 0.0f);

/// The integer version of DragFloatRange2.
bool DragIntRange2(const char* label, int* lo, int* hi, float speed = 0.25f, int minV = 0, int maxV = 0);

/// Two, three or four sliders in one row sharing one label; every cell runs over [minV, maxV]. True on any change.
bool SliderFloat2(const char* label, float v[2], float minV, float maxV);
bool SliderFloat3(const char* label, float v[3], float minV, float maxV);
bool SliderFloat4(const char* label, float v[4], float minV, float maxV);
/// A float field with [-]/[+] steppers that change *value by `step` (the float twin of InputInt). A NaN or
/// infinite step, or a step that would make the value non-finite, changes nothing.
bool InputFloat(const char* label, float* value, float step = 0.1f);
/// A square button with an arrow: dir 0 left, 1 right, 2 up, 3 down (anything else is left). `id` names it.
bool ArrowButton(const char* id, int dir);
/// Colour widgets (new files: okayui/src/OkayUIColor.cpp for the picker; the swatch and the row editors are in OkayUI.cpp).
/// Flags for ColorEdit3/4, ColorPicker3/4 and ColorButton (OkayUI's own values).
///  NoAlpha: three channels only; the alpha of a 4-float array is left alone and drawn as 1.
///  NoPicker: ColorEdit: clicking the swatch does not open the picker.   NoInputs: ColorEdit: the swatch only; picker: no hex field or cells.
///  NoLabel: do not draw the label.   NoSidePreview: picker: no current / original preview strip.   NoAlphaBar: picker: no alpha bar.
///  Hex: ColorEdit: one hex text field (#RRGGBB or #RRGGBBAA) in place of the channel sliders.
///  AlphaPreviewHalf: a swatch shows the colour with its alpha (over a checkerboard) on the left half and opaque on the right half; without
///  the flag the whole swatch shows the alpha over the checkerboard.
enum ColorEditFlags : int {
    ColorEdit_None = 0, ColorEdit_NoAlpha = 1, ColorEdit_NoPicker = 2, ColorEdit_NoInputs = 4, ColorEdit_NoLabel = 8,
    ColorEdit_NoSidePreview = 16, ColorEdit_NoAlphaBar = 32, ColorEdit_Hex = 64, ColorEdit_AlphaPreviewHalf = 128,
    ColorEdit_AlphaBar = 0,       // the alpha bar is on unless ColorEdit_NoAlphaBar: accepted for ported code
    ColorEdit_NoTooltip = 256,    // accepted and ignored: swatches have no tooltip
};
/// A square swatch showing rgba[0..3] (each 0..1, a checkerboard shows through the alpha) that can be clicked; `size` <= 0 uses the row height.
bool ColorButton(const char* id, const float rgba[4], float size = 0.0f);
bool ColorButton(const char* id, const float rgba[4], int flags, float size);
/// An RGBA color editor: a swatch plus four 0..1 sliders (see ColorEditFlags); clicking the swatch opens a ColorPicker4 popup.
/// Returns true on any frame a channel changed.
bool ColorEdit4(const char* label, float rgba[4], int flags = 0);
/// Ends the current line: the next widget starts on a new row even right after SameLine().
void NewLine();

/// An RGB color editor: a swatch plus three 0..1 channel sliders on one row (ColorEdit4 with ColorEdit_NoAlpha).
/// Edits rgb[0..2] in place; returns true on any frame a channel changed.
bool ColorEdit3(const char* label, float rgb[3], int flags = 0);

/// A full color picker: a saturation/value square, a hue bar, an alpha bar (ColorPicker4), a current / original preview, a hex field
/// (#RRGGBB or #RRGGBBAA, typed text applies once it is a whole colour) and R G B A cells (0..255). Drag the square or the bars,
/// click the original swatch to go back. The hue is kept while the colour is grey. Edits the floats in place (each 0..1); true on any change.
bool ColorPicker4(const char* label, float rgba[4], int flags = 0);
bool ColorPicker3(const char* label, float rgb[3], int flags = 0);
/// "#RRGGBB" (or "#RRGGBBAA" with `alpha`) from floats 0..1, written into `out` (cap >= 10 bytes), and back. ColorFromHex accepts an
/// optional '#', then exactly 6 or 8 hex digits in either case; it changes `rgba` only when the whole text is a colour (6 digits leave
/// rgba[3] alone) and returns whether it did.
void ColorToHex(const float rgba[4], bool alpha, char* out, int cap);
bool ColorFromHex(const char* text, float rgba[4]);

/// A collapsing tree node. Returns true while expanded; when it does, the widgets
/// that follow are INDENTED until you call TreePop(). Nestable. State kept per label.
bool TreeNode(const char* label);
enum TreeNodeFlags {
    TreeNode_DefaultOpen = 1,   // starts open the first time it is seen
    TreeNode_Leaf        = 2,   // no arrow and no toggling; still returns true (and indents until TreePop) so its children can be drawn
    TreeNode_Selected    = 4,   // drawn highlighted
    TreeNode_OpenOnArrow = 8,        // only a click on the arrow toggles it; a click on the label leaves it (IsItemClicked still sees the click)
    TreeNode_OpenOnDoubleClick = 16, // a double click on the row toggles it (alone: a single click does not; with OpenOnArrow: either does)
    TreeNode_Bullet      = 32,       // a bullet instead of the arrow (the node still opens and closes)
    TreeNode_SpanFullWidth = 64,     // the row, its highlight and its click area start at the window's content edge, under the indent
    TreeNode_NoTreePushOnOpen = 128, // an open node does not indent: no TreePop() for it
    TreeNode_SpanAvailWidth = 0,     // the row already runs from the node to the right edge: the default, accepted for ported code
    TreeNode_Framed      = 256,      // drawn with a header-style background (the Col_Header colours)
};
/// TreeNode with flags (TreeNodeFlags).
bool TreeNodeEx(const char* label, unsigned flags = 0);
/// The next TreeNode, TreeNodeEx or CollapsingHeader is forced open or closed (once).
void SetNextItemOpen(bool open);
/// True when the last TreeNode or CollapsingHeader was opened or closed by a click this frame.
bool IsItemToggledOpen();
/// Close the most recent open TreeNode (removes one indent level). Pair with each
/// TreeNode() that returned true.
void TreePop();

// ---- Menus ---------------------------------------------------------------------
// Place a menu bar right after Begin(): a row of menus across the top of the window.
//     if (OkayUI::BeginMenuBar()) { ... } style isn't used; call Begin/End in pairs:
//     OkayUI::BeginMenuBar();
//       if (OkayUI::BeginMenu("File")) { if (OkayUI::MenuItem("Open")) ...; OkayUI::EndMenu(); }
//     OkayUI::EndMenuBar();
/// Returns true when a menu bar began (inside a window), so `if (!BeginMenuBar()) return;` ports unchanged; EndMenuBar is still always safe to call.
bool BeginMenuBar();
void EndMenuBar();
/// A menu on the bar; returns true while its dropdown is open (then add MenuItems).
bool BeginMenu(const char* label);
/// Close a BeginMenu() block (handles click-outside dismissal).
/// Keyboard: while a menu is open, Down/Up move a highlight over its enabled items (wrapping), Enter runs the highlighted one,
/// Right opens a highlighted sub-menu, Left closes a sub-menu, Escape closes the innermost open menu. The mouse moves the same
/// highlight. The host sends Input::up / down / left / right / enter / escape as key edges.
/// BeginMenu inside a BeginMenu block is a sub-menu: a row with an arrow that opens its list to the right while the pointer is
/// on the row or the list (up to four levels); picking any item closes the whole chain. Call EndMenu() for it as well.
void EndMenu();
/// An item in the currently-open menu; returns true the frame it is clicked.
bool MenuItem(const char* label);
/// MenuItem with right-aligned `shortcut` text (display only, dim; null for none), a mark when `selected`
/// (a checked item) and `enabled` = false to grey it out and make it unclickable.
bool MenuItem(const char* label, const char* shortcut, bool selected = false, bool enabled = true);
/// A thin dividing line between groups of items inside a BeginMenu block.
void MenuSeparator();

/// A full-width selectable row (list entries). Drawn highlighted when `selected`;
/// returns true the frame it is clicked.
bool Selectable(const char* label, bool selected);
/// A selectable row that is never drawn selected (the one-argument Dear ImGui form).
bool Selectable(const char* label);
/// Selectable with flags and a size. `size.x` <= 0 fills the width to the right edge, `size.y` <= 0 is one row. With the pointer
/// the row toggles *p_selected when clicked. Flags: Disabled (dim, no hover or click), ClosePopup (choosing it closes the innermost
/// popup), SpanAllColumns (in a table: the row covers every column; the cell widgets after it stay in place), DoubleClickOnly (only a
/// double click chooses it).
/// Selectable_Header (OkayUI's own) draws Dear ImGui's row: flat and square, Col_Header when selected, Col_HeaderHovered under the
/// pointer, Col_HeaderActive while held, the label half the item spacing in from the row's left edge (Dear ImGui's highlight reaches
/// that far past its text). Without it a row has OkayUI's look (the launcher look: a rounded accent tint and a bar when selected).
enum SelectableFlags : unsigned { Selectable_None = 0, Selectable_Disabled = 1, Selectable_ClosePopup = 2, Selectable_SpanAllColumns = 4, Selectable_DoubleClickOnly = 8,
                                  Selectable_Header = 16 };
bool Selectable(const char* label, bool selected, unsigned flags, Vec2 size = Vec2{});
bool Selectable(const char* label, bool* p_selected, unsigned flags = 0, Vec2 size = Vec2{});

// ---- Popups / context menus ----------------------------------------------------
// A floating menu that opens on demand. Fill it with Selectable/Button/Text/Separator.
/// Request the named popup to open at the cursor (call once on the triggering event).
void OpenPopup(const char* id);
/// Open a popup when the LAST widget is right-clicked; returns true while it's open
/// (then add items; clicking one should call CloseCurrentPopup()).
/// With no id the popup is keyed by the last item, so every item can have its own.
bool BeginPopupContextItem(const char* id = nullptr);
/// Open a popup when the empty background of the current window is right-clicked (not an item, not a window above it); returns
/// true while it is open, like BeginPopupContextItem. Call it after the window's widgets, so the hovered item is known.
bool BeginPopupContextWindow(const char* id);
/// Returns true while the named popup (opened via OpenPopup) is showing.
bool BeginPopup(const char* id);
/// A modal dialog: while the popup named `name` is open (OpenPopup(name) first) the area behind it is dimmed and
/// inert, it is centred in Input::displayW x displayH (or stays where it opened when those are 0), it has a title
/// line, and a click outside does NOT close it. Escape closes it; with `open` given it also has a close button and
/// *open is cleared when it closes. Fill it with widgets and a button that calls CloseCurrentPopup(), then EndPopup().
bool BeginPopupModal(const char* name, bool* open = nullptr);
/// The Dear ImGui three-argument form: `flags` (window flags such as NoTitleBar, AlwaysAutoResize) are accepted; a modal here is always centred and auto-sized.
bool BeginPopupModal(const char* name, bool* open, int flags);
/// True while the named popup (or modal) is open.
bool IsPopupOpen(const char* name);
/// Close the popup body opened by the matching Begin*Popup. Pair each with End only
/// when Begin returned true.
void EndPopup();
/// Close the currently-open popup (call after an item is chosen).
void CloseCurrentPopup();

/// A scrolling list box: `visibleRows` tall, one selectable row per item. Clicking a
/// row sets *current to its index. Returns true the frame the selection changed.
/// `items` and `current` required. Built on BeginChild, so it scrolls when it overflows.
bool ListBox(const char* label, int* current, const char* const* items, int count,
             int visibleRows = 4);

// ---- Columns -------------------------------------------------------------------
// Split the layout into `count` equal columns. Widgets flow into the current column;
// NextColumn() moves to the next (wrapping to a new row after the last). EndColumns()
// restores single-column flow below the widest column.
void Columns(int count);
/// Dear ImGui's form: an id string (accepted) and the border flag (accepted; OkayUI columns have no borders).
void Columns(int count, const char* id, bool border = true);
void NextColumn();
void EndColumns();

// ---- Tables --------------------------------------------------------------------
// A grid with resizable columns (drag the borders between headers/cells). Column
// widths persist per table id. Usage:
//     const char* hdr[] = {"Name", "Value"};
//     if (BeginTable("stats", 2, hdr)) {
//         TableNextRow(); TableNextColumn(); Text("HP"); TableNextColumn(); Text("100");
//         EndTable();
//     }
/// Begin a table of `columns` columns. If `headers` is given, a header row is drawn.
/// Returns true (open) — pair with EndTable(). Rows/cells via TableNextRow/Column.
bool BeginTable(const char* id, int columns, const char* const* headers = nullptr);
/// Same, with starting column widths in proportion to `weights` (e.g. {3, 1, 1}
/// makes the first column three times as wide). Users can still drag them.
bool BeginTable(const char* id, int columns, const char* const* headers, const float* weights);
/// The flag-style table, for tables whose columns are described one by one (the usual way to build a table):
///     if (BeginTable("stats", 3, Table_RowBg | Table_BordersInnerH)) {
///         TableSetupColumn("Name", Col_WidthFixed, 120); TableSetupColumn("Value"); TableSetupColumn("Note", 0, 2);
///         TableHeadersRow();
///         TableNextRow(); TableSetColumnIndex(0); Text("HP"); TableSetColumnIndex(1); Text("100");
///         EndTable();
///     }
/// Set the columns up right after BeginTable (up to 16). A Col_WidthFixed column keeps `widthOrWeight` pixels at first; the others
/// share the rest by weight (0 = weight 1). Users can still drag the borders unless Table_NoResize is set.
///
/// Sorting (Table_Sortable): clicking a header sorts by that column (the arrow shows the direction); clicking the sorted column again
/// flips it. With Table_SortMulti a Ctrl+click adds the column as a further sort key (up to four) or flips it; with Table_SortTristate
/// a third click removes it. The table does not sort anything itself: after TableHeadersRow, ask for the specs and sort your data:
///     if (TableSortSpecs* sp = TableGetSortSpecs()) if (sp->specsDirty) { sort(by sp->specs[0..specsCount)); sp->specsDirty = false; }
/// A column flagged Col_DefaultSort starts as a sort key (specsDirty is true on the first frame so you sort once).
/// Scrolling (Table_ScrollX / Table_ScrollY): the table is a box of the given outer size whose rows (Y) and columns (X) scroll with the
/// wheel (Shift+wheel for X) and scroll bars; TableSetupScrollFreeze(cols, rows) keeps the first columns and rows (the header row counts
/// as a row) in place. With Table_ScrollX the columns keep their pixel widths: stretch columns share `innerWidth` (or the box's width).
/// Row backgrounds (Table_RowBg): even rows use Col_TableRowBg (clear unless set), odd rows Col_TableRowBgAlt; a band is as tall as
/// the row was last frame (one text row on the first). TableSetBgColor paints a row or a cell: call it right after TableNextRow /
/// TableSetColumnIndex, before the cell's widgets, so they draw on top.
enum TableFlags : int {
    Table_None         = 0,
    Table_RowBg        = 1,   // rows alternate between Col_TableRowBg and Col_TableRowBgAlt (the faint band on every second row)
    Table_BordersInnerH = 2,  // a hairline between rows
    Table_NoResize     = 4,   // the column borders cannot be dragged
    Table_Sortable     = 8,   // header clicks sort (see TableGetSortSpecs)
    Table_SortMulti    = 16,  // Ctrl+click on a header adds a sort key
    Table_SortTristate = 32,  // ascending, descending, then unsorted
    Table_ScrollX      = 64,  // horizontal scrolling inside the outer size
    Table_ScrollY      = 128, // vertical scrolling inside the outer size
    Table_Hideable     = 256, // a right click on the header row opens a menu that shows and hides columns
    // (Table_BordersOuter and the other names ported tables use are in OkayUICompat.hpp.)
};
enum TableColumnFlags : int {
    Col_None       = 0,
    Col_WidthFixed = 1,       // `widthOrWeight` is pixels
    Col_NoSort     = 2,       // a Sortable table's header click does not sort by this column
    Col_DefaultSort = 4,      // starts as a sort key
    Col_PreferSortDescending = 8,   // the first click sorts descending (the default is ascending)
    Col_WidthStretch = 16,    // `widthOrWeight` is a weight (the default; accepted so ported code can say it)
    Col_NoResize     = 32,    // the borders on either side of this column cannot be dragged
    Col_DefaultHide  = 64,    // starts hidden (a Table_Hideable table's header menu, or TableSetColumnEnabled, shows it)
    Col_NoHide       = 128,   // cannot be hidden
    Col_NoHeaderLabel = 256,  // the header row draws no label for this column (the sort arrow still shows)
    Col_NoReorder = 512, Col_NoClip = 1024, Col_NoSortAscending = 2048, Col_NoSortDescending = 4096, Col_IndentEnable = 8192,   // accepted, nothing to do yet
};
/// The same column flags under the names ported code uses. Stretch is the default (flag 0), Fixed is Col_WidthFixed.
enum TableColumnFlagNames : int { TableColumn_None = 0, TableColumn_WidthStretch = 0, TableColumn_WidthFixed = Col_WidthFixed, TableColumn_NoSort = Col_NoSort,
                                  TableColumn_DefaultSort = Col_DefaultSort, TableColumn_PreferSortDescending = Col_PreferSortDescending };
inline TableFlags operator|(TableFlags a, TableFlags b) { return (TableFlags)((int)a | (int)b); }
bool BeginTable(const char* id, int columns, TableFlags flags);
/// BeginTable with a box: `outerSize.x` <= 0 is the width left; `outerSize.y` <= 0 is the height left (at least four rows) for a
/// ScrollY table and the content's height (as of the last frame) for a table that scrolls only sideways. `innerWidth` > 0 is the width the
/// columns add up to in a ScrollX table. Without Table_ScrollX / Table_ScrollY it is the plain table (the size is ignored).
bool BeginTable(const char* id, int columns, TableFlags flags, Vec2 outerSize, float innerWidth = 0.0f);
/// `userId` comes back in the sort specs.
void TableSetupColumn(const char* label, int columnFlags = 0, float widthOrWeight = 0.0f, unsigned userId = 0);
/// Keep the first `cols` columns and `rows` rows (the header row counts) fixed while the rest scrolls (ScrollX / ScrollY tables).
void TableSetupScrollFreeze(int cols, int rows);
/// The header bar with the labels given to TableSetupColumn.
void TableHeadersRow();
/// One sort key: the column, its position among the keys (0 = main) and the direction.
struct TableColumnSortSpecs {
    unsigned columnUserId = 0; int columnIndex = 0; int sortOrder = 0; bool descending = false;
    // Dear ImGui's field names for the same values (SortDirection: 1 ascending, 2 descending), filled together with the ones above.
    unsigned ColumnUserID = 0; int ColumnIndex = 0; int SortOrder = 0; int SortDirection = 0;
};
struct TableSortSpecs {
    const TableColumnSortSpecs* specs = nullptr; int specsCount = 0; bool specsDirty = false;
    const TableColumnSortSpecs* Specs = nullptr; int SpecsCount = 0; bool SpecsDirty = false;   // the same, under Dear ImGui's names (SpecsDirty is read back like specsDirty)
};
/// The sort keys of the current Sortable table (null when it is not sortable). Valid until EndTable; clear `specsDirty` once you have sorted.
TableSortSpecs* TableGetSortSpecs();
enum TableBgTarget : int { TableBg_RowBg0 = 1, TableBg_RowBg1 = 2, TableBg_CellBg = 3,
                           TableBgTarget_RowBg0 = TableBg_RowBg0, TableBgTarget_RowBg1 = TableBg_RowBg1, TableBgTarget_CellBg = TableBg_CellBg };   // the second set: Dear ImGui's spelling
/// Paint the current row (RowBg0 replaces the even-row colour, RowBg1 the odd-row one, whichever the row is) or a cell (`column` < 0: the
/// current one) with a packed colour (RGBA()).
void TableSetBgColor(TableBgTarget target, Color32 color, int column = -1);
/// The scroll position of the current ScrollX / ScrollY table, and a way to set it (clamped to the content on the next frame).
float TableGetScrollX();
float TableGetScrollY();
void TableSetScroll(float x, float y);
/// Jump to column `column` of the current row (false when it does not exist); like TableNextColumn but not in order.
bool TableSetColumnIndex(int column);
int  TableGetColumnIndex();   // -1 before the first cell of a row
int  TableGetRowIndex();      // -1 before the first row
/// Hiding columns (flag-style tables). A column starts hidden with Col_DefaultHide; a Table_Hideable table's header row opens a menu on
/// right click (a check box per column; NoHide columns and the last visible one are locked); TableSetColumnEnabled(col, false) hides one
/// from code (false when it cannot be hidden), taking effect next frame. The choice is remembered per table. A hidden column has no
/// width, header or border; TableNextColumn / TableSetColumnIndex return false for it and anything drawn in its cell lands off screen.
bool TableSetColumnEnabled(int column, bool enabled);
bool TableGetColumnIsEnabled(int column);
int  TableGetColumnCount();
/// The label and the flags given to TableSetupColumn (column < 0: the current column; "" and 0 when there is none).
const char* TableGetColumnName(int column = -1);
int  TableGetColumnFlags(int column = -1);
/// The visible column under the pointer while it is over the table (header and rows so far), else -1.
int  TableGetHoveredColumn();
/// The row clipper is on by default (rows wholly outside the clip emit no geometry; the picture is identical); off only to compare or debug.
void SetTableRowClipping(bool on);
bool TableRowIsVisible();     // false while the current row is wholly outside the clip (the row clipper): its widgets still lay out but emit no geometry, so skip expensive cell content
/// Width in pixels of `text` in the current UI font (for fitting/truncating).
float CalcTextWidth(const char* text);
/// Start a new row (call before the row's TableNextColumn cells).
void TableNextRow();
/// With Dear ImGui's flags (accepted) and a minimum height for the row in pixels (the row is at least that tall; 0 = as tall as its cells).
void TableNextRow(int flags, float minRowHeight);
/// Advance to the next cell; returns true (visible). Widgets after it land in-cell.
bool TableNextColumn();
void EndTable();

// ---- Launcher-style building blocks ----------------------------------------------
// The pieces the OkaySpace launcher is made of, for menus, HUDs, lobbies and
// scoreboards. They work in either look; with UseLauncherLook() they match it.
/// A page / panel title: bigger text with the accent underline.
void Heading(const char* text);
/// Small coloured text, e.g. a green "NOW PLAYING" label.
void Caption(const char* text, unsigned char r = 115, unsigned char g = 209, unsigned char b = 143);
/// Text that wraps to the available width.
void TextWrapped(const char* text);
/// An accent-filled call-to-action button (Play, Join, Confirm).
bool PrimaryButton(const char* label);
/// A rounded pill with short text (counts, tags, "NEW"). r=g=b=0: the accent colour.
void Badge(const char* text, unsigned char r = 0, unsigned char g = 0, unsigned char b = 0);
/// A circle with the name's initial, coloured per name. size 0 = one row tall.
void Avatar(const char* name, float size = 0.0f);
/// A raised, rounded card; widgets until EndCard() go inside (it fits its content).
void BeginCard(const char* id);
void EndCard();
/// A list row: avatar, title, dim subtitle, optional right-aligned text. True when clicked.
bool ListItem(const char* title, const char* subtitle = nullptr, const char* right = nullptr, bool selected = false);
/// A dim label on the left and its value on the right (stats, settings summaries).
void KeyValue(const char* label, const char* value);
/// A spinning "busy" ring. size 0 = one row tall.
void Spinner(float size = 0.0f);
/// Show a toast in the bottom-right corner (kind: 0 info, 1 success, 2 error).
/// Call from anywhere; it stays up for `seconds`.
void Toast(const char* text, int kind = 0, float seconds = 3.0f);
/// Scale the text of the following widgets (1 = normal) until PopTextScale().
void PushTextScale(float scale);
void PopTextScale();

// ---- Drag and drop -------------------------------------------------------------
// Lightweight int-payload drag & drop (e.g. reorder a list by dragging item indices).
// Call DragSource right AFTER the widget that should be draggable, and DropTarget
// right after a widget that can receive a drop.
/// Mark the last-issued widget as a drag source carrying `payload`. Returns true
/// while it is being dragged.
bool DragSource(int payload);
/// Mark the last-issued widget as a drop target. On the frame a drag is released over
/// it, writes the payload to *outPayload and returns true.
bool DropTarget(int* outPayload);
/// True while any drag is in progress (e.g. to change hover styling).
bool IsDragging();
/// Typed byte payloads, the way ImGui does it:
///     Button("item"); if (BeginDragDropSource()) { SetDragDropPayload("ITEM", &index, sizeof index); SetDragDropPreview("item 3"); EndDragDropSource(); }
///     Button("slot"); if (BeginDragDropTarget()) { if (const DragPayload* p = AcceptDragDropPayload("ITEM")) use(*(int*)p->data); EndDragDropTarget(); }
/// The source starts a drag once the pointer has moved a few pixels while pressed on the item before it (call it right after that
/// item); it returns true every frame of the drag, and the payload (a copy of up to 256 bytes with a type name of up to 32
/// characters) must be set during it. The target returns true while a drag with a payload is over the item before it and outlines
/// it; AcceptDragDropPayload returns the payload on the frame the button is released over it (delivery = true), or earlier on every
/// hover frame with DragDrop_AcceptBeforeDelivery (delivery = false). A null or empty `type` accepts any type. Widgets drawn inside
/// the source block are drawn in the window, not in a floating box; use SetDragDropPreview(text) for what follows the pointer.
/// The older int-payload DragSource / DropTarget keep working next to this and share IsDragging().
inline constexpr int kDragPayloadMax = 256;
struct DragPayload {
    char type[33] = {0}; unsigned char data[kDragPayloadMax] = {0}; int size = 0; bool delivery = false;
    // Dear ImGui's names for the same facts, filled in the copies AcceptDragDropPayload and GetDragDropPayload hand out (Data points at `data` of that copy).
    const void* Data = nullptr; int DataSize = 0;
    bool IsDataType(const char* t) const { return t && std::strncmp(type, t, sizeof type - 1) == 0; }
    bool IsDelivery() const { return delivery; }
};
typedef DragPayload ImGuiPayload;
/// DragDrop_NoDefaultRect (for BeginDragDropTarget) leaves out the accent outline, for a target that draws where the drop would land
/// itself (a list showing a line between rows).
enum DragDropFlags : int { DragDrop_None = 0, DragDrop_AcceptBeforeDelivery = 1, DragDrop_NoDefaultRect = 2 };
bool BeginDragDropSource(int flags = 0);
bool SetDragDropPayload(const char* type, const void* data, int size);
void SetDragDropPreview(const char* text);
void EndDragDropSource();
bool BeginDragDropTarget();
bool BeginDragDropTarget(int flags);
const DragPayload* AcceptDragDropPayload(const char* type = nullptr, int flags = 0);
void EndDragDropTarget();
/// The drag in flight once it has a payload (its type and bytes; delivery false), or null. Lets a list show where a drop would land,
/// and lets a host hand an OkayUI drag on to another toolkit when the pointer leaves the OkayUI area.
const DragPayload* GetDragDropPayload();
/// A drag that began OUTSIDE OkayUI (in another toolkit's panel beside an OkayUI one, such as a file dragged from a Dear ImGui panel):
/// the host calls this every frame the drag lasts, after BeginFrame and before the OkayUI widgets that may take it, with `released` true
/// on the frame the button is let go. OkayUI's drop targets see it like one of their own drags (the pointer counts even while
/// Input::blocked is set, since the other toolkit owns the mouse) and AcceptDragDropPayload delivers it on the released frame. A frame
/// without the call ends it. No chip follows the pointer (the other toolkit draws its own).
void SetExternalDragDrop(const char* type, const void* data, int size, bool released);
/// True while the drag in flight came from SetExternalDragDrop.
bool IsDragDropExternal();
/// End the drag in flight without a drop. Escape does this too (until the button is let go nothing starts again).
void CancelDragDrop();

// ---- ID stack ------------------------------------------------------------------
// Widget ids are hashed from their label, so two widgets with the same label in a
// loop would collide. Push a per-iteration id (an int index or a string) to keep
// them distinct, then Pop it. Nestable.
void PushID(int id);
void PushID(const char* id);
void PopID();

// ---- Fonts ---------------------------------------------------------------------
// A bitmap font is a glyph cell size plus a pixel test. OkayUI defaults to the
// engine's built-in 8x8 font; supply another (e.g. a bold or larger bitmap, or your
// own glyph set) to change the typeface. `Style().textScale` still scales it.
struct Font {
    int width  = 8;
    int height = 8;
    bool (*pixel)(char c, int x, int y) = nullptr;   // is pixel (x,y) of glyph `c` lit?
    /// A TrueType font (a user font made by AddFontTtf, see OkayUICompat.hpp): when set, text is drawn and measured with it at `px` pixels (SetFont
    /// scales the text so the line is `px` tall until the next SetFont) and `pixel` is not used. Null for the bitmap fonts.
    const void* ttf = nullptr; float px = 0.0f;
    /// The size of `text` drawn at a glyph height of `size` pixels (Dear ImGui's CalcTextSizeA): the widest line by the lines' height. `maxWidth`
    /// and `wrapWidth` are not used (no wrapping here). `remaining` (optional) is set to the end of the text.
    Vec2 CalcTextSizeA(float size, float maxWidth, float wrapWidth, const char* text, const char** remaining = nullptr) const;
};
typedef Font ImFont;
/// What Dear ImGui code puts in an ImFontConfig: accepted, nothing in it changes how OkayUI draws (fonts are set with SetFont / UseLauncherLook).
struct ImFontConfig { float SizePixels = 0.0f; int OversampleH = 1, OversampleV = 1; bool PixelSnapH = false, MergeMode = false; Vec2 GlyphOffset; const unsigned short* GlyphRanges = nullptr; char Name[40] = {0}; };
/// Set the active font (null restores the built-in 8x8).
void SetFont(const Font* font);
Font* GetFont();   // not const so `ImFont* f = GetFont();` compiles; the font is not changed through it
/// Built-in fonts: the default 8x8, and a synthesized bold variant.
const Font* FontDefault();
const Font* FontBold();

// ---- Item queries and state reset ------------------------------------------------
/// A rectangle in screen pixels.
struct Rect { float x = 0, y = 0, w = 0, h = 0; };
/// The rectangle of the last auto-layout item (Button, Checkbox, SliderFloat, InputText, Combo,
/// Selectable, TreeNode, Text ...): the whole row including a label. Zero outside a window.
Rect GetItemRect();
/// The overlays opened so far this frame (combo lists, popups, menu dropdowns), in the order they were opened, each as a rectangle in
/// screen pixels (index out of range: a zero Rect). A host that hands the mouse to OkayUI by region offers the ones its panel opened,
/// so a list that hangs out of the panel still takes the click.
int GetOverlayCount();
Rect GetOverlayRect(int index);
/// True when the mouse is over the last auto-layout item and nothing covers it (a blocked
/// Input, or an open combo list / popup, counts as covered).
bool IsItemHovered();
/// True while a text field has keyboard focus: the host should send typed text and key edges
/// to OkayUI and not to the game (ImGui's io.WantTextInput).
bool WantsTextInput();
/// Copy text to the clipboard / read it back (up to kClipboardMax - 1 bytes, never cut inside a character). It is also mirrored to the
/// system clipboard when a window system is running; with none (tests, servers) the copy lives inside OkayUI. The text of
/// GetClipboardText() is valid until the next call. Text fields copy (Input::copy), cut (Input::cut) and paste (Input::paste) with it.
void SetClipboardText(const char* text);
const char* GetClipboardText();
/// Hooks for widgets written in other files (OkayUICodeEdit.hpp): this frame's raw input, and the keyboard focus. SetKeyboardFocusId
/// gives the keyboard to the widget with this id (the GetItemID() of its InvisibleButton) and marks this frame's click as taken, so
/// the click does not drop the focus again; 0 releases it.
const Input& GetFrameInput();
int GetKeyboardFocusId();
void SetKeyboardFocusId(int id);
/// Forget all remembered state: window positions and collapsed flags, tree / header / combo
/// open flags, scroll offsets, popups, keyboard focus, text carets, toasts, the theme and font
/// (back to the classic defaults). For tests and for returning to a clean slate.
void ResetState();

// ---- Draw list, cursor, measurement, item state, disabled blocks ---------------------------------
// The calls a custom-drawn panel needs (a gizmo, a timeline, a node graph, a viewport overlay): lines, shapes, text at
// a point, and the cursor and size queries that let it sit inside the auto-layout. All coordinates are screen pixels.
// Every call ignores non-finite or absurd (beyond 1e7) numbers instead of drawing them.

/// A packed colour, 0xAABBGGRR: the bytes in memory are R, G, B, A (this is the byte order of the draw-list vertices); see RGBA().
constexpr Color32 RGBA(unsigned r, unsigned g, unsigned b, unsigned a = 255u) {
    return (r & 0xffu) | ((g & 0xffu) << 8) | ((b & 0xffu) << 16) | ((a & 0xffu) << 24);
}
enum Col : int;   // declared in full further down; the theme colours
/// A theme colour packed, with its alpha scaled by `alphaMul` (0..1).
Color32 GetColorU32(Col which, float alphaMul = 1.0f);
/// `c` with its alpha scaled by `alphaMul` (0..1).
Color32 ColorMulAlpha(Color32 c, float alphaMul);

/// Drawing into the current window's batch (GetWindowDrawList) or on top of everything (GetForegroundDrawList). Both
/// respect the clip rectangle stack (PushClipRect), except that the foreground list is never clipped. Not dimmed by
/// BeginDisabled. Rectangle and circle outlines are drawn INSIDE the given bounds (a rectangle of thickness 2 covers
/// exactly its border pixels); polylines, and so triangle and quad outlines, are centred on their path. Joins are
/// mitred, so a translucent stroke does not double up at a corner.
/// Which corners a rounded shape rounds (AddImageRounded). 0 means all four; DrawFlags_None (no corner) draws a plain rectangle.
/// Our own values, not Dear ImGui's numbers.
enum DrawFlags : int {
    DrawFlags_RoundCornersTopLeft = 1, DrawFlags_RoundCornersTopRight = 2, DrawFlags_RoundCornersBottomRight = 4, DrawFlags_RoundCornersBottomLeft = 8,
    DrawFlags_RoundCornersTop = 3, DrawFlags_RoundCornersRight = 6, DrawFlags_RoundCornersBottom = 12, DrawFlags_RoundCornersLeft = 9,
    DrawFlags_RoundCornersAll = 15, DrawFlags_None = 16,
    DrawFlags_Closed = 32      // AddPolyline: join the last point to the first
};

/// A growable array of plain values with the field names and calls ported code uses (`Size`, `Data`, `begin()`, `push_front`, `find_index`,
/// `erase(pointer)`). Own design: a small malloc'd buffer; only for types that can be copied with memcpy (pointers, numbers, plain structs).
template <typename T> struct ImVector {
    static_assert(std::is_trivially_copyable<T>::value, "ImVector holds plain values only");
    int Size = 0, Capacity = 0;
    T* Data = nullptr;
    typedef T* iterator; typedef const T* const_iterator;
    ImVector() {}
    ImVector(const ImVector& o) { *this = o; }
    ImVector& operator=(const ImVector& o) {
        if (this == &o) return *this;
        Size = 0; reserve(o.Size);
        if (o.Size) std::memcpy(static_cast<void*>(Data), static_cast<const void*>(o.Data), sizeof(T) * (size_t)o.Size);
        Size = o.Size;
        return *this;
    }
    ~ImVector() { std::free(Data); }
    bool empty() const { return Size == 0; }
    int size() const { return Size; }
    T& operator[](int i) { return Data[i]; }
    const T& operator[](int i) const { return Data[i]; }
    T* begin() { return Data; }
    const T* begin() const { return Data; }
    T* end() { return Data + Size; }
    const T* end() const { return Data + Size; }
    T& front() { return Data[0]; }
    T& back() { return Data[Size - 1]; }
    void clear() { Size = 0; }
    void reserve(int n) {
        if (n <= Capacity) return;
        T* d = static_cast<T*>(std::malloc(sizeof(T) * (size_t)n));
        if (!d) return;
        if (Size) std::memcpy(static_cast<void*>(d), static_cast<const void*>(Data), sizeof(T) * (size_t)Size);
        std::free(Data); Data = d; Capacity = n;
    }
    void resize(int n) { reserve(n); if (n > Size) std::memset(static_cast<void*>(Data + Size), 0, sizeof(T) * (size_t)(n - Size)); Size = Capacity >= n ? n : Size; }
    // `v` may point into Data (v.push_back(v[0])): growing frees the old block, so copy it out first.
    void push_back(const T& v) {
        if (Size < Capacity) { Data[Size++] = v; return; }
        const T keep = v;
        reserve(Capacity ? Capacity * 2 : 8);
        if (Size < Capacity) Data[Size++] = keep;
    }
    void push_front(const T& v) {
        if (Size == 0) { push_back(v); return; }
        const T keep = v;
        push_back(keep);
        if (Size < 2) return;
        std::memmove(static_cast<void*>(Data + 1), static_cast<const void*>(Data), sizeof(T) * (size_t)(Size - 1));
        Data[0] = keep;
    }
    T* erase(const T* it) {
        const int off = (int)(it - Data);
        if (off < 0 || off >= Size) return Data + Size;
        std::memmove(static_cast<void*>(Data + off), static_cast<const void*>(Data + off + 1), sizeof(T) * (size_t)(Size - off - 1));
        --Size;
        return Data + off;
    }
    int find_index(const T& v) const { for (int i = 0; i < Size; ++i) if (Data[i] == v) return i; return -1; }
    bool contains(const T& v) const { return find_index(v) >= 0; }
};


// ---- Dear ImGui's draw-data shapes (what a custom renderer written for Dear ImGui reads) ---------------------------------------------------------
// OkayUI's own draw data is DrawData / DrawCmd above. These are the same facts in the field names and layout a Dear ImGui renderer expects, built
// from DrawData on request (`ImDrawData* dd = GetDrawData();` converts, see DrawData::operator ImDrawData*) so such a renderer can run unchanged
// while the host is moved over to OkayUI's own FrameDrawer. Docs: docs/ui/own_ui_toolkit.md 6.22.
typedef SDL_Texture* ImTextureID;                    // what AddImage and Image take
typedef unsigned int ImDrawIdx;                       // 32-bit: OkayUI batches hold more than 65,535 vertices
struct ImDrawVert { Vec2 pos; Vec2 uv; unsigned col = 0; };   // col is 0xAABBGGRR
struct ImTextureRect { unsigned short x = 0, y = 0, w = 0, h = 0; };
enum ImTextureStatus : int { ImTextureStatus_OK = 0, ImTextureStatus_Destroyed = 1, ImTextureStatus_WantCreate = 2, ImTextureStatus_WantUpdates = 3, ImTextureStatus_WantDestroy = 4 };
struct ImTextureData {
    ImTextureStatus Status = ImTextureStatus_OK;
    int Width = 0, Height = 0, BytesPerPixel = 4;
    unsigned char* Pixels = nullptr;                   // RGBA8, top row first; owned by whoever made the data (OkayUI: the launcher look's atlas)
    ImVector<ImTextureRect> Updates;                   // always empty here
    unsigned char* GetPixels() const { return Pixels; }
    int GetPitch() const { return Width * BytesPerPixel; }
    unsigned char* GetPixelsAt(int x, int y) const { return Pixels + (size_t)y * (size_t)GetPitch() + (size_t)x * (size_t)BytesPerPixel; }
};
struct ImDrawCmd;
struct DrawList;
typedef void (*ImDrawCallback)(const DrawList* list, const ImDrawCmd* cmd);
#ifndef ImDrawCallback_ResetRenderState   // a program that also includes Dear ImGui's imgui.h first keeps ITS macro (its own callback type)
#define ImDrawCallback_ResetRenderState ((::OkayUI::ImDrawCallback)(intptr_t)-8)
#endif
#ifndef ImTextureID_Invalid
#define ImTextureID_Invalid ((::OkayUI::ImTextureID)nullptr)
#endif
struct ImTextureRef {
    ImTextureData* _TexData = nullptr; ImTextureID _TexID = nullptr;
    ImTextureRef() {}
    ImTextureRef(ImTextureID id) : _TexID(id) {}
};
struct ImDrawCmd {
    ImVec4 ClipRect;                                   // x y z w = left top right bottom, pixels
    ImTextureRef TexRef;
    unsigned VtxOffset = 0, IdxOffset = 0, ElemCount = 0;
    ImDrawCallback UserCallback = nullptr; void* UserCallbackData = nullptr;
    ImTextureID GetTexID() const { return TexRef._TexID; }
};
struct DrawList {
    bool overlay;
    void AddLine(Vec2 a, Vec2 b, Color32 col, float thickness = 1.0f) const;
    /// `rounding` is the corner radius in pixels (0 = square; clamped to half the shorter side).
    void AddRect(Vec2 min, Vec2 max, Color32 col, float rounding = 0.0f, float thickness = 1.0f) const;
    void AddRectFilled(Vec2 min, Vec2 max, Color32 col, float rounding = 0.0f) const;
    /// A gradient: one colour per corner (top-left, top-right, bottom-right, bottom-left).
    void AddRectFilledMultiColor(Vec2 min, Vec2 max, Color32 tl, Color32 tr, Color32 br, Color32 bl) const;
    /// `segments` 0 picks a count from the radius (12 to 64); explicit counts are clamped to 3..128.
    void AddCircle(Vec2 center, float radius, Color32 col, int segments = 0, float thickness = 1.0f) const;
    void AddCircleFilled(Vec2 center, float radius, Color32 col, int segments = 0) const;
    void AddTriangle(Vec2 a, Vec2 b, Vec2 c, Color32 col, float thickness = 1.0f) const;
    void AddTriangleFilled(Vec2 a, Vec2 b, Vec2 c, Color32 col) const;
    void AddQuad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color32 col, float thickness = 1.0f) const;
    void AddQuadFilled(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color32 col) const;
    /// 2 to 256 points. `closed` joins the last point to the first.
    void AddPolyline(const Vec2* points, int count, Color32 col, bool closed, float thickness = 1.0f) const;
    /// The same with DrawFlags (DrawFlags_Closed joins the ends; other bits are ignored).
    void AddPolyline(const Vec2* points, int count, Color32 col, int flags, float thickness = 1.0f) const;
    /// A convex polygon (a triangle fan; concave shapes draw wrongly: no ear clipping yet), 3 to 256 points.
    void AddConvexPolyFilled(const Vec2* points, int count, Color32 col) const;
    /// A cubic Bezier curve flattened to `segments` lines (0 = 24, at most 255).
    void AddBezierCubic(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, Color32 col, float thickness = 1.0f, int segments = 0) const;
    /// Text with its top-left at `pos`; `scale` 0 uses the theme text scale (a negative or non-finite scale draws nothing). One line (no newline handling).
    void AddText(Vec2 pos, Color32 col, const char* text, float scale = 0.0f) const;
    /// Dear ImGui's (pos, col, text, textEnd): the text between the two pointers (`textEnd` null = to the zero).
    void AddText(Vec2 pos, Color32 col, const char* text, const char* textEnd) const;
    /// The Dear ImGui form with a font and a pixel size: draws with OkayUI's active font scaled so glyphs are `fontSize` pixels tall (the `font` argument is not
    /// used to pick a typeface). `textEnd` null means up to the zero. `wrapWidth` and `clipRect` are ignored.
    void AddText(const Font* font, float fontSize, Vec2 pos, Color32 col, const char* text, const char* textEnd = nullptr, float wrapWidth = 0.0f, const ImVec4* clipRect = nullptr) const;
    /// With corner flags (DrawFlags, 0 = all corners, DrawFlags_None = square). Rounded outlines are centred on their path.
    void AddRect(Vec2 min, Vec2 max, Color32 col, float rounding, int flags, float thickness) const;
    void AddRectFilled(Vec2 min, Vec2 max, Color32 col, float rounding, int flags) const;
    /// A textured rectangle. Always goes to the main batch, also on the foreground list. The software test backend
    /// skips textured commands.
    void AddImage(SDL_Texture* tex, Vec2 min, Vec2 max, Vec2 uv0 = Vec2{0.0f, 0.0f}, Vec2 uv1 = Vec2{1.0f, 1.0f},
                  Color32 tint = 0xffffffffu) const;
    /// A textured four-corner shape (corners in order around the shape, each with its own texture coordinate); convex shapes only (drawn as a
    /// fan from corner 0). Same rules as AddImage (main batch, skipped by the software test backend).
    void AddImageQuad(SDL_Texture* tex, Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, Vec2 uv1 = Vec2{0.0f, 0.0f}, Vec2 uv2 = Vec2{1.0f, 0.0f},
                      Vec2 uv3 = Vec2{1.0f, 1.0f}, Vec2 uv4 = Vec2{0.0f, 1.0f}, Color32 tint = 0xffffffffu) const;
    /// AddImage with rounded corners (`rounding` in pixels, clamped to half the shorter side; which corners: DrawFlags, 0 = all). The
    /// texture coordinates follow the position inside the rectangle, so the rounded shape cuts the picture.
    void AddImageRounded(SDL_Texture* tex, Vec2 min, Vec2 max, Vec2 uv0, Vec2 uv1, Color32 tint, float rounding, int flags = 0) const;
    /// A regular polygon with `n` corners (3 to 128), the first corner pointing right of the centre, corners going clockwise on screen.
    void AddNgon(Vec2 center, float radius, Color32 col, int n, float thickness = 1.0f) const;
    void AddNgonFilled(Vec2 center, float radius, Color32 col, int n) const;
    /// A filled polygon that may be concave (ear clipping; 3 to 256 points, a self-crossing outline draws something unspecified; winding either way).
    void AddConcavePolyFilled(const Vec2* points, int count, Color32 col) const;
    /// An ellipse outline (radii per axis, rotated by `rot` radians, `segments` 0 = 32).
    void AddEllipse(Vec2 center, Vec2 radius, Color32 col, float rot = 0.0f, int segments = 0, float thickness = 1.0f) const;
    void AddEllipseFilled(Vec2 center, Vec2 radius, Color32 col, float rot = 0.0f, int segments = 0) const;
    void PushClipRect(Vec2 min, Vec2 max) const;
    /// Channels: after ChannelsSplit(n) the geometry drawn goes into one of n lists (ChannelsSetCurrent(i); channel 0 is current at first), and
    /// ChannelsMerge() puts the lists back in channel order, so what was drawn on channel 1 shows above everything on channel 0 whatever
    /// order the calls came in. Only the window list has channels: on the foreground list the calls are accepted and do nothing (it is already
    /// in call order). A split left open is merged when its window ends or the frame ends. n is 2 to 8 (a split of fewer or more than that does
    /// nothing); a split while one is open, a bad index and a merge without a split do nothing. Written from the behaviour of the calls.
    void ChannelsSplit(int count) const;
    void ChannelsSetCurrent(int index) const;
    void ChannelsMerge() const;
    /// Dear ImGui's field names for the same facts (`_Splitter._Count` is 1 with no split open, `_Current` the channel being drawn on).
    struct SplitterView { int _Current = 0; int _Count = 1; };
    mutable SplitterView _Splitter;
    /// The three buffers a Dear ImGui renderer reads. Empty on the handles GetWindowDrawList / GetForegroundDrawList return; filled on the one list of
    /// an ImDrawData (see the shapes above).
    ImVector<ImDrawVert> VtxBuffer;
    ImVector<ImDrawIdx> IdxBuffer;
    ImVector<ImDrawCmd> CmdBuffer;
    /// Dear ImGui's three-argument form: `intersectWithCurrent` false would replace the clip rectangle; here it always intersects.
    void PushClipRect(Vec2 min, Vec2 max, bool intersectWithCurrent) const;
    void PopClipRect() const;
    /// A path of up to 256 points shared by both lists (cleared by PathStroke and PathFillConvex).
    void PathClear() const;
    void PathLineTo(Vec2 p) const;
    /// Points along an arc (radians; 0 points right, angles grow clockwise on screen). `segments` 0 picks a count.
    void PathArcTo(Vec2 center, float radius, float a0, float a1, int segments = 0) const;
    void PathStroke(Color32 col, bool closed = false, float thickness = 1.0f) const;
    void PathFillConvex(Color32 col) const;
    /// The path of a rectangle with rounded corners (`rounding` in pixels, DrawFlags for which corners: 0 = all), added to the path.
    void PathRect(Vec2 min, Vec2 max, float rounding = 0.0f, int flags = 0) const;
    /// The points of the path as Dear ImGui's field shows them (a copy refreshed by every Path call on this list, not read back).
    mutable ImVector<Vec2> _Path;
    /// Raw geometry, the way Dear ImGui hosts write their own triangles: PrimReserve(indexCount, vertexCount) says how much follows, PrimWriteVtx adds
    /// a vertex (`pos`, `uv`, `col` as 0xAABBGGRR), PrimWriteIdx an index in the batch's own numbering (`_VtxCurrentIdx` is the number of the first
    /// vertex reserved; read it after PrimReserve), PrimVtx adds a vertex AND the next index (for a plain triangle list). More than was reserved, or
    /// a batch that is full, is dropped (counted by DroppedGeometry). With the default texture a vertex wants GetFontTexUvWhitePixel() as its uv for a flat colour.
    void PrimReserve(int idxCount, int vtxCount) const;
    void PrimWriteVtx(Vec2 pos, Vec2 uv, Color32 col) const;
    void PrimWriteIdx(ImDrawIdx idx) const;
    void PrimVtx(Vec2 pos, Vec2 uv, Color32 col) const;
    /// The texture the following geometry samples (a stack of 8; PopTexture goes back). The texture is an SDL_Texture the host owns.
    void PushTexture(const ImTextureRef& tex) const;
    void PopTexture() const;
    mutable unsigned _VtxCurrentIdx = 0;
};
/// The uv a flat-coloured vertex should carry (a white texel of the glyph atlas in the launcher look; (0, 0) in the classic look, which ignores uv).
Vec2 GetFontTexUvWhitePixel();
/// What GetWindowDrawList / GetForegroundDrawList return: a DrawList (so `GetWindowDrawList().AddLine(...)` and `const DrawList& dl = ...` work
/// as before) that also behaves like the pointer Dear ImGui returns: `dl->AddLine(...)` and `DrawList* dl = GetWindowDrawList();` compile.
/// There is one handle per list, living for the whole program.
struct DrawListHandle : DrawList {
    bool isNull = false;
    DrawListHandle(bool isOverlay) { overlay = isOverlay; }
    /// So `cond ? GetWindowDrawList() : nullptr` compiles: a handle that converts to a null pointer.
    DrawListHandle(std::nullptr_t) { overlay = false; isNull = true; }
    const DrawListHandle* operator->() const { return this; }
    operator DrawList*() const;   // the one window / foreground handle (not a copy), or null for the nullptr form
};
/// What a Dear ImGui renderer reads per frame: one list (OkayUI has one batch), the display, and the textures. `Textures` points at a list holding
/// the launcher look's atlas as an ImTextureData (Status OK, never Destroyed) when the frame uses it, else an empty list.
struct ImDrawData {
    bool Valid = false;
    int CmdListsCount = 0, TotalIdxCount = 0, TotalVtxCount = 0;
    ImVector<DrawList*> CmdLists;
    Vec2 DisplayPos{0, 0}, DisplaySize{0, 0}, FramebufferScale{1, 1};
    ImVector<ImTextureData*>* Textures = nullptr;
};
const DrawListHandle& GetWindowDrawList();
const DrawListHandle& GetForegroundDrawList();
/// Dear ImGui's name for the draw list type.
typedef DrawList ImDrawList;

/// Width and height of one line of text in the current font and text scale (height is the line height).
Vec2 CalcTextSize(const char* text);
float GetTextLineHeight();
float GetFontSize();
float GetFrameHeight();                  // height of a standard widget row (a button, a field)
float GetTextLineHeightWithSpacing();
float GetFrameHeightWithSpacing();
/// Where the next item will be placed (screen pixels), honouring a pending SameLine. Zero outside a window.
Vec2 GetCursorScreenPos();
/// Place the NEXT item at this screen position (a one-shot; the layout continues below it afterwards).
void SetCursorScreenPos(Vec2 p);
/// The same relative to the window's (or child's) content origin.
Vec2 GetCursorPos();
void SetCursorPos(Vec2 p);
float GetCursorPosX();
float GetCursorPosY();
void SetCursorPosX(float x);
void SetCursorPosY(float y);
/// Width from the cursor to the right edge of the content, and height down to the bottom of the visible content area.
Vec2 GetContentRegionAvail();

/// An item with no drawing, `w`*`h` pixels (0 = full width / one row), that behaves like a button: use it as a hit area
/// for custom drawing (draw over GetItemRect()). Returns true on the frame it is clicked.
bool InvisibleButton(const char* id, float w, float h);
/// Queries about the LAST item. They work after the widgets that register an id (button, checkbox, radio, switch,
/// slider, drag field, text field, combo, selectable, tree node, header, InvisibleButton); after any other item they
/// answer false.
/// True while the last item has the keyboard (a text field that was clicked into).
bool IsItemFocused();
/// The id a widget of this label would get in the current ID scope (what PushID changes).
unsigned GetID(const char* label);
/// An id for the last item (0 if it has none); two different widgets in the same ID scope get different ids.
unsigned GetItemID();
/// True while any widget is held (pressed or being dragged).
bool IsAnyItemActive();
/// True when the last item overlaps the current clip rectangle (false when it was scrolled out of a child).
bool IsItemVisible();
bool IsRectVisible(Vec2 size);                  // a rectangle of this size at the cursor: is any of it inside the current clip rectangle?
bool IsRectVisible(Vec2 min, Vec2 max);
bool IsAnyMouseDown();                           // any mouse button is held
bool IsItemActive();         // pressed and held (or, for a text field, being dragged)
bool IsItemActivated();      // became active this frame
bool IsItemDeactivated();    // stopped being active this frame
/// True when the last value widget (checkbox, radio, switch, slider, drag, input, combo, list box, colour edit) changed its value
/// this frame (the frame it returned true). For a widget made of several cells (DragFloat3, InputInt) "the last item" is its last cell.
bool IsItemEdited();
/// True on the frame the last item stops being held (or, for a text field, loses the keyboard) if its value changed at any point
/// while it was held or focused: the "now commit it" moment for a drag or a typed value. A click-to-change widget (checkbox)
/// answers true on its click frame.
bool IsItemDeactivatedAfterEdit();
/// The width the next full-width item would get (the room left on the line, or the SetNextItemWidth / PushItemWidth width when
/// that is smaller). 0 outside a frame.
float CalcItemWidth();
bool IsItemClicked(int button = 0);   // hovered and `button` (0 left, 1 right) went down this frame
bool IsMouseDown(int button = 0);
bool IsMouseClicked(int button = 0);
bool IsMouseReleased(int button = 0);
Vec2 GetMousePos();
Vec2 GetMouseDelta();
/// The last item's rectangle as corners / size (the same rectangle as GetItemRect()).
Vec2 GetItemRectMin();
Vec2 GetItemRectMax();
Vec2 GetItemRectSize();
/// Seconds since the start (Input::time when the host sets it, otherwise frames / 60) and the number of frames begun.
double GetTime();
int GetFrameCount();
/// The left button went down for the second time within 0.3 s and 6 px of the first press (this frame only).
bool IsMouseDoubleClicked(int button = 0);
/// The left button is held and has moved more than `threshold` pixels (default 4) since it went down.
bool IsMouseDragging(int button = 0, float threshold = -1.0f);
/// Named key state from Input::keyDown. IsKeyPressed is true on the frame a key goes down and, with `repeat`, again every 0.05 s
/// once it has been held for 0.5 s (so holding an arrow key keeps moving); IsKeyReleased on the frame it goes up.
/// An id out of range is simply "not down". KeyId_Ctrl and KeyId_Shift are also down while Input::ctrl / Input::shift are.
bool IsKeyDown(KeyId key);
bool IsKeyPressed(KeyId key, bool repeat = true);
bool IsKeyReleased(KeyId key);
/// Temporarily change a style value until the matching PopStyleVar (they nest; up to 32 deep; an unbalanced push is undone at the
/// next frame). Supported: ItemSpacing (x, y between widgets), WindowPadding (x for windows; x and y for the padded and framed
/// children of the flags form of BeginChild, which otherwise pad by 6), FrameRounding and ChildRounding (corner radius),
/// WindowRounding, BorderSize (all three ImGui border sizes), ButtonTextAlign (x only: where a button's label sits, 0 left, 0.5
/// centred, 1 right), CellPadding (a table cell's inner gap: x across, default 4; y above the first item and below the last one, which
/// then ends the row without its item spacing; by default a row ends after the last item's spacing), FramePadding (y only: every
/// frame, button, field and row is a text line plus twice y tall, and the box of Checkbox and RadioButton too; by default a frame is a
/// text line plus 15 (12 without the launcher look) and a box a text line plus 6). The others are accepted and do
/// nothing, so ported code compiles: SelectableTextAlign, Alpha.
/// TextRowHeight is OkayUI's own: the height a line of plain text (Text, TextColored, TextDisabled, TextUnformatted, wrapped lines)
/// takes in the layout. 0 (the default) is a frame's height, so text lines up with buttons and fields on its row; a value above 0 is
/// that many pixels (Dear ImGui's text rows: pass GetFontSize() for one text line). A shorter text line that follows a taller item
/// on the same line is centred on it.
/// PopupPadding and PopupWidth are OkayUI's own too, read when a popup or context menu begins: the padding inside it (x, y; default
/// 6 and 6) and its width (default 160). With a pushed padding the popup is exactly as tall as its content (no item spacing after the
/// last item), as in Dear ImGui. ItemInnerSpacing (x only) is the gap inside the formatted drag rows and before their label.
/// FramePadding's x is read by framed combos only (where their text starts; see FramedWidgets).
/// ScrollbarSize sets the width of the scroll bars of children (the flags form of BeginChild) and the vertical bar of scrolling tables (default: OkayUI's
/// own 10-pixel bar); with a pushed size the bar sits inside a framed child's or a bordered table's one-pixel outline and its grab is 2
/// pixels inside the bar on every side and fully rounded, Dear ImGui's proportions.
/// FramedWidgets is OkayUI's own (0 off, the default; anything else on): Checkbox draws Dear ImGui's box (a rounded frame in Col_FrameBg, or
/// the accent at 45% once ticked, and a tick in Col_CheckMark), and Combo and BeginCombo draw a field in Col_FrameBg with a square arrow
/// button at its right end (Col_Button, a filled triangle in the text colour) and the text FramePadding.x in.
enum StyleVar : int {
    StyleVar_Alpha = 0, StyleVar_WindowPadding, StyleVar_WindowRounding, StyleVar_FramePadding, StyleVar_FrameRounding,
    StyleVar_ItemSpacing, StyleVar_ItemInnerSpacing, StyleVar_ChildRounding, StyleVar_BorderSize, StyleVar_ButtonTextAlign,
    StyleVar_SelectableTextAlign, StyleVar_ScrollbarSize, StyleVar_GrabMinSize, StyleVar_TextRowHeight, StyleVar_CellPadding, StyleVar_PopupPadding,
    StyleVar_PopupWidth, StyleVar_FramedWidgets, StyleVar_COUNT,
    // Names ported code uses for the border sizes (BorderSize sets all three, as before).
    StyleVar_WindowBorderSize = StyleVar_BorderSize, StyleVar_FrameBorderSize = StyleVar_BorderSize, StyleVar_ChildBorderSize = StyleVar_BorderSize
};
void PushStyleVar(StyleVar var, float value);
void PushStyleVar(StyleVar var, Vec2 value);
void PopStyleVar(int count = 1);
bool IsMouseHoveringRect(Vec2 min, Vec2 max);
/// About the window between Begin and End (zeros / false outside one). IsWindowHovered: the pointer is over it and no window above it
/// covers the pointer. IsWindowFocused: it was the last window clicked (or the last that appeared). IsWindowAppearing: it was not
/// drawn on the frame before. SetWindowFocus(title) makes a window the focused one and raises it.
Vec2 GetWindowPos();
Vec2 GetWindowSize();
float GetWindowWidth();
float GetWindowHeight();
bool IsWindowHovered();
bool IsWindowFocused();
bool IsWindowAppearing();
void SetWindowFocus(const char* title);
/// Scale the text (and with it every row height) of the current window until its End; 1 = unchanged, kept between 0.25 and 8.
void SetWindowFontScale(float scale);
/// The mouse cursor shape the UI wants this frame; the host reads GetMouseCursor() after the frame and shows the matching system
/// cursor. It starts each frame as the arrow.
enum MouseCursor : int { MouseCursor_None = -1, MouseCursor_Arrow = 0, MouseCursor_TextInput, MouseCursor_ResizeAll, MouseCursor_ResizeNS, MouseCursor_ResizeEW,
                         MouseCursor_ResizeNESW, MouseCursor_ResizeNWSE, MouseCursor_Hand, MouseCursor_NotAllowed, MouseCursor_Wait, MouseCursor_Progress, MouseCursor_COUNT };
void SetMouseCursor(MouseCursor cursor);
MouseCursor GetMouseCursor();
/// Read-only facts about the frame, filled by BeginFrame: pointer, wheel, display size, modifier keys, the frame time, and whether
/// the UI wants the mouse (pointer over a window or a widget is active) or the keyboard (a text field has focus), so the host knows
/// not to also give them to the game.
struct IO {
    Vec2 mousePos, mouseDelta, displaySize; float mouseWheel = 0.0f; bool mouseDown[3] = {false, false, false};
    bool keyCtrl = false, keyShift = false, keyAlt = false;
    bool wantCaptureMouse = false, wantCaptureKeyboard = false, wantTextInput = false;
    float deltaTime = 1.0f / 60.0f;
    // The same values under Dear ImGui's field names (filled by GetIO; writes to them are not read back), plus the fields a host sets.
    Vec2 MousePos, MouseDelta, DisplaySize; float MouseWheel = 0.0f; bool MouseDown[3] = {false, false, false};
    bool KeyCtrl = false, KeyShift = false, KeyAlt = false;
    bool WantCaptureMouse = false, WantCaptureKeyboard = false, WantTextInput = false;
    float DeltaTime = 1.0f / 60.0f, Framerate = 60.0f;
    float MouseWheelH = 0.0f;                 // horizontal wheel: OkayUI has none (always 0)
    float MouseDragThreshold = 6.0f, MouseDragMaxDistanceSqr[3] = {0, 0, 0};   // accepted; the drag threshold OkayUI uses is the argument of IsMouseDragging
    Vec2 DisplayFramebufferScale{1.0f, 1.0f};
    int ConfigFlags = 0, BackendFlags = 0;
    const char* IniFilename = nullptr; const char* LogFilename = nullptr;
    // Host-side fields of Dear ImGui's IO, see docs/ui/own_ui_toolkit.md 6.22.
    int MouseClickedCount[3] = {0, 0, 0};        // clicks in a row at this frame's press (1, 2 = double click), 0 when the button did not go down this frame (left button only)
    bool KeySuper = false;                       // the Windows / Command key: OkayUI has no such key; always false unless the host sets it
    float FontGlobalScale = 1.0f;                // accepted; the text scale is Style().textScale
    bool ConfigViewportsNoTaskBarIcon = false, ConfigDebugHighlightIdConflicts = true;   // accepted, unused
    struct FontAtlasRef* Fonts = nullptr;        // GetIO() points it at the one atlas (below): font requests are recorded, nothing is loaded
    /// The characters typed this frame (UTF-16 code units, characters above U+FFFF are left out), filled by GetIO() from Input::text. Removing any of
    /// them (`resize(0)`) means "I took the typing": the OkayUI text fields skip this frame's typed text (the code editor's field does not).
    ImVector<unsigned short> InputQueueCharacters;
    /// Feed input the way a Dear ImGui host does: applied by the next NewFrame() (Input::text gets the characters; a key event sets Input::keyDown[key],
    /// and a press of Enter / Escape / Backspace / Delete / Tab / the arrows / Home / End also sets that key's one-frame flag).
    void AddKeyEvent(int key, bool down);
    void AddInputCharacter(unsigned codePoint);
    void AddInputCharactersUTF8(const char* utf8);   // every character of the text, as AddInputCharacter
    void AddMouseWheelEvent(float wheelX, float wheelY);   // the vertical amount scrolls (OkayUI has no horizontal wheel)
    void AddFocusEvent(bool focused);                // the window lost or got focus: losing it lets go of every key and button
    const char* BackendPlatformName = nullptr, *BackendRendererName = nullptr;
    bool MouseDrawCursor = false;                    // accepted; the host draws the system cursor
    bool WantSetMousePos = false;                    // accepted; OkayUI never asks the host to move the pointer
    /// Mouse events as a Dear ImGui host feeds them: applied by the next NewFrame() (the last position and the button state win; a press and release
    /// inside one frame still makes one click).
    void AddMousePosEvent(float x, float y);
    void AddMouseButtonEvent(int button, bool down);
};
/// Not const so ported code can write the host fields (ConfigFlags, IniFilename); OkayUI never reads those.
IO& GetIO();

// ---- Input method (IME) support: API only, no operating-system code. See Input::composition and Input::commit. -----------
/// This frame's preedit after cleaning (printable valid UTF-8 only, at most 256 bytes, caret moved onto a character boundary).
struct Composition { char text[260] = {0}; int length = 0; int caret = 0; bool active = false; };
Composition GetComposition();
/// Where the focused text input's caret is on screen (pixels), and the span of the preedit (zero width when not composing). A host puts
/// the system candidate window under `caret` (or `composition`). Valid for the frame just built, until the next BeginFrame; `valid` is
/// false when no text input has the keyboard. Text fields and CodeEdit fill it; a custom text widget can with SetImeRect.
struct ImeRect { bool valid = false; Rect caret, composition; };
ImeRect GetImeRect();
void SetImeRect(const ImeRect& r);
/// Where / how big the next ImGui-style window is (see Cond and the Begin below); the Begin that takes the title and flags.
void SetNextWindowPos(Vec2 pos, Cond cond = Cond_Always);
void SetNextWindowSize(Vec2 size, Cond cond = Cond_Always);
/// With a pivot: the position is where the window's own point `pivot` (0,0 top-left, 0.5,0.5 centre, 1,1 bottom-right) goes. The window's size is the one
/// from SetNextWindowSize, else its last size (a window that has never been drawn is placed with the 320 x 240 default).
void SetNextWindowPos(Vec2 pos, Cond cond, Vec2 pivot);
bool Begin(const char* title, bool* p_open = nullptr, int flags = 0);

/// Widgets between BeginDisabled(true) and EndDisabled() take no input and draw with their colours halfway to the panel colour. Pairs nest; with
/// `disabled` false the pair does nothing (so `BeginDisabled(!ok)` is always safe). Unbalanced calls are reset each
/// frame. A drag already in progress when a block becomes disabled is not cancelled.
void BeginDisabled(bool disabled = true);
void EndDisabled();
bool IsDisabled();

/// The active theme (mutable — tweak colors/sizes in place).
Theme& Style();

// ---- Scoped style overrides (ImGui-style) --------------------------------------
// The theme colours a PushStyleColor / SetStyleColor / GetStyleColorVec4 call names. The first eight are the original theme
// fields; the rest are the full Dear ImGui colour set. A colour that was never set follows the base theme (Style().bg, ...);
// a set one overrides it. Which widget draws with which colour is listed in docs/ui/own_ui_toolkit.md section 4.33; a few
// (the navigation highlights, BorderShadow, the unfocused tab colours, the hovered / active separator)
// are accepted and stored but nothing draws with them yet.
enum Col : int {
    Col_Bg, Col_BgHover, Col_BgDown, Col_Border,
    Col_Text, Col_Panel, Col_Track, Col_Accent,
    Col_TextDisabled, Col_WindowBg, Col_ChildBg, Col_PopupBg, Col_BorderShadow,
    Col_FrameBg, Col_FrameBgHovered, Col_FrameBgActive,
    Col_TitleBg, Col_TitleBgActive, Col_TitleBgCollapsed, Col_MenuBarBg,
    Col_ScrollbarBg, Col_ScrollbarGrab, Col_ScrollbarGrabHovered, Col_ScrollbarGrabActive,
    Col_CheckMark, Col_SliderGrab, Col_SliderGrabActive,
    Col_Header, Col_HeaderHovered, Col_HeaderActive,
    Col_Separator, Col_SeparatorHovered, Col_SeparatorActive,
    Col_ResizeGrip, Col_ResizeGripHovered, Col_ResizeGripActive,
    Col_Tab, Col_TabHovered, Col_TabActive, Col_TabUnfocused, Col_TabUnfocusedActive,
    Col_DockingPreview, Col_DockingEmptyBg,
    Col_PlotLines, Col_PlotLinesHovered, Col_PlotHistogram, Col_PlotHistogramHovered,
    Col_TableHeaderBg, Col_TableBorderStrong, Col_TableBorderLight, Col_TableRowBg, Col_TableRowBgAlt,
    Col_TextSelectedBg, Col_DragDropTarget, Col_NavHighlight, Col_NavWindowingHighlight, Col_NavWindowingDimBg, Col_ModalWindowDimBg,
    Col_COUNT,
    Col_TabSelected = Col_TabActive,   // the newer Dear ImGui name for the selected tab colour
    // the button colours are the original three fields under their Dear ImGui names
    Col_Button = Col_Bg, Col_ButtonHovered = Col_BgHover, Col_ButtonActive = Col_BgDown,
};
/// Temporarily override a theme color (RGBA 0..255). Every PushStyleColor must be
/// matched by a PopStyleColor. Nestable — great for coloring one widget or section.
void PushStyleColor(Col which, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255);
/// The same with a packed colour (see RGBA) or four floats 0..1 (non-finite values count as 0; out of range ones are clamped).
void PushStyleColor(Col which, Color32 color);
void PushStyleColor(Col which, Vec4 rgba);
void PopStyleColor(int count = 1);
/// Set a colour until ResetStyleColor (or the next UseLauncherLook), outside the push / pop stack.
void SetStyleColor(Col which, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255);
void ResetStyleColor(Col which);
/// What the colour is now, as RGBA floats 0..1: the pushed / set value, or the base theme's value for a colour never set.
/// An id outside the enum answers (0, 0, 0, 0). GetStyleColorName gives "Col_Text" for Col_Text (or "" outside the enum).
ImVec4 GetStyleColorVec4(Col which);
const char* GetStyleColorName(Col which);
/// Dear ImGui style colour conversions (all components 0..1; hue wraps).
Vec4 ColorConvertU32ToFloat4(Color32 c);
Color32 ColorConvertFloat4ToU32(Vec4 c);
void ColorConvertRGBtoHSV(float r, float g, float b, float* h, float* s, float* v);
void ColorConvertHSVtoRGB(float h, float s, float v, float* r, float* g, float* b);
/// Dear ImGui's form with references.
void ColorConvertHSVtoRGB(float h, float s, float v, float& r, float& g, float& b);

} // namespace OkayUI
