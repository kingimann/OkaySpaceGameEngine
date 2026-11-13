#pragma once
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
struct Vec2 { float x = 0.0f, y = 0.0f; };

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
    KeyId_COUNT
};

struct Input;
/// The most text the clipboard holds (bytes, with the end mark).
inline constexpr int kClipboardMax = 4096;
struct Input {
    float       mouseX    = 0.0f;
    float       mouseY    = 0.0f;
    bool        mouseDown = false;     // left mouse button held this frame
    bool        rightDown = false;     // right mouse button held this frame (context menus)
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
};
bool TextField(int id, float x, float y, float w, float h, char* buf, int cap, unsigned flags = 0);

/// Flush the batched geometry to the renderer. Call AFTER ImGui has rendered so
/// OkayUI draws on top; the renderer's blend mode is saved and restored so nothing
/// else is disturbed.
void EndFrame(SDL_Renderer* renderer);

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
/// it, nor by docked content under it. See docs/own_ui_toolkit.md section 4.32.
enum WindowFlags : int {
    Window_None = 0, Window_NoResize = 1, Window_NoMove = 2, Window_NoTitleBar = 4,
    Window_NoScrollbar = 8, Window_NoBackground = 16, Window_NoCollapse = 32, Window_AlwaysAutoResize = 64,
    Window_NoFocusOnAppearing = 128, Window_NoBringToFront = 256,
};

void End();
/// Pin the next Begin() at its (x, y) this frame (it can't be dragged away): for
/// overlays anchored to the screen, such as a dialogue box or a quest tracker.
void PinNextWindow();
/// Give the next Begin() a resize grip in its bottom-right corner (drag it to resize; the w and h
/// passed to Begin are the starting size). The size is remembered per window. `minW`/`minH` bound it.
void ResizableNextWindow(float minW = 100.0f, float minH = 60.0f);

/// A scrolling sub-region of `w`*`h` at the cursor. Widgets called until EndChild()
/// stack inside it and are CLIPPED to the box; if they overflow vertically the region
/// scrolls (mouse wheel when hovered, or drag the scrollbar). `id` keys the scroll
/// position. `w`/`h` <= 0 fill the remaining width / use a default height. Always pair
/// with EndChild(). Returns true (visible) — call EndChild regardless.
bool BeginChild(const char* id, float w = 0.0f, float h = 0.0f, bool border = true);
void EndChild();

/// Manually clip subsequent geometry to a rectangle (screen coords). Nestable and
/// intersected with any enclosing clip. Pair with PopClipRect. (BeginChild uses this.)
void PushClipRect(float x, float y, float w, float h);
void PopClipRect();

/// Keep the next widget on the current line instead of starting a new one
/// (`spacing` < 0 uses the theme default gap).
void SameLine(float spacing = -1.0f);
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
/// An integer field with [-]/[+] stepper buttons that change *value by `step`.
/// Returns true on any frame the value changed.
bool InputInt(const char* label, int* value, int step = 1);

/// A collapsing section header. Returns true while expanded — put the section's
/// widgets in the if-body. The open/closed state is remembered per label.
bool CollapsingHeader(const char* label);

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
    TreeNode_Framed      = 256,      // drawn with a header-style background (the Col_Header colours)
};
/// TreeNode with flags (TreeNodeFlags).
bool TreeNodeEx(const char* label, unsigned flags);
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
void BeginMenuBar();
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
/// Selectable with flags and a size. `size.x` <= 0 fills the width to the right edge, `size.y` <= 0 is one row. With the pointer
/// the row toggles *p_selected when clicked. Flags: Disabled (dim, no hover or click), ClosePopup (choosing it closes the innermost
/// popup), SpanAllColumns (in a table: the row covers every column; the cell widgets after it stay in place), DoubleClickOnly (only a
/// double click chooses it).
enum SelectableFlags : unsigned { Selectable_None = 0, Selectable_Disabled = 1, Selectable_ClosePopup = 2, Selectable_SpanAllColumns = 4, Selectable_DoubleClickOnly = 8 };
bool Selectable(const char* label, bool selected, unsigned flags, Vec2 size = Vec2{});
bool Selectable(const char* label, bool* p_selected, unsigned flags = 0, Vec2 size = Vec2{});

// ---- Popups / context menus ----------------------------------------------------
// A floating menu that opens on demand. Fill it with Selectable/Button/Text/Separator.
/// Request the named popup to open at the cursor (call once on the triggering event).
void OpenPopup(const char* id);
/// Open a popup when the LAST widget is right-clicked; returns true while it's open
/// (then add items; clicking one should call CloseCurrentPopup()).
bool BeginPopupContextItem(const char* id);
/// Returns true while the named popup (opened via OpenPopup) is showing.
bool BeginPopup(const char* id);
/// A modal dialog: while the popup named `name` is open (OpenPopup(name) first) the area behind it is dimmed and
/// inert, it is centred in Input::displayW x displayH (or stays where it opened when those are 0), it has a title
/// line, and a click outside does NOT close it. Escape closes it; with `open` given it also has a close button and
/// *open is cleared when it closes. Fill it with widgets and a button that calls CloseCurrentPopup(), then EndPopup().
bool BeginPopupModal(const char* name, bool* open = nullptr);
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
enum TableFlags : int {
    Table_None         = 0,
    Table_RowBg        = 1,   // every second row gets a faint band (drawn one text row tall)
    Table_BordersInnerH = 2,  // a hairline between rows
    Table_NoResize     = 4,   // the column borders cannot be dragged
};
enum TableColumnFlags : int {
    Col_None       = 0,
    Col_WidthFixed = 1,       // `widthOrWeight` is pixels
};
inline TableFlags operator|(TableFlags a, TableFlags b) { return (TableFlags)((int)a | (int)b); }
bool BeginTable(const char* id, int columns, TableFlags flags);
void TableSetupColumn(const char* label, int columnFlags = 0, float widthOrWeight = 0.0f);
/// The header bar with the labels given to TableSetupColumn.
void TableHeadersRow();
/// Jump to column `column` of the current row (false when it does not exist); like TableNextColumn but not in order.
bool TableSetColumnIndex(int column);
int  TableGetColumnIndex();   // -1 before the first cell of a row
int  TableGetRowIndex();      // -1 before the first row
/// Width in pixels of `text` in the current UI font (for fitting/truncating).
float CalcTextWidth(const char* text);
/// Start a new row (call before the row's TableNextColumn cells).
void TableNextRow();
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
struct DragPayload { char type[33] = {0}; unsigned char data[kDragPayloadMax] = {0}; int size = 0; bool delivery = false; };
enum DragDropFlags : int { DragDrop_None = 0, DragDrop_AcceptBeforeDelivery = 1 };
bool BeginDragDropSource(int flags = 0);
bool SetDragDropPayload(const char* type, const void* data, int size);
void SetDragDropPreview(const char* text);
void EndDragDropSource();
bool BeginDragDropTarget();
const DragPayload* AcceptDragDropPayload(const char* type = nullptr, int flags = 0);
void EndDragDropTarget();

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
};
/// Set the active font (null restores the built-in 8x8).
void SetFont(const Font* font);
const Font* GetFont();
/// Built-in fonts: the default 8x8, and a synthesized bold variant.
const Font* FontDefault();
const Font* FontBold();

// ---- Item queries and state reset ------------------------------------------------
/// A rectangle in screen pixels.
struct Rect { float x = 0, y = 0, w = 0, h = 0; };
/// The rectangle of the last auto-layout item (Button, Checkbox, SliderFloat, InputText, Combo,
/// Selectable, TreeNode, Text ...): the whole row including a label. Zero outside a window.
Rect GetItemRect();
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

/// A packed colour, 0xAABBGGRR: the bytes in memory are R, G, B, A (this is the byte order of the draw-list vertices).
typedef unsigned Color32;
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
    /// A convex polygon (a triangle fan; concave shapes draw wrongly: no ear clipping yet), 3 to 256 points.
    void AddConvexPolyFilled(const Vec2* points, int count, Color32 col) const;
    /// A cubic Bezier curve flattened to `segments` lines (0 = 24, at most 255).
    void AddBezierCubic(Vec2 p1, Vec2 p2, Vec2 p3, Vec2 p4, Color32 col, float thickness = 1.0f, int segments = 0) const;
    /// Text with its top-left at `pos`; `scale` 0 uses the theme text scale (a negative or non-finite scale draws nothing). One line (no newline handling).
    void AddText(Vec2 pos, Color32 col, const char* text, float scale = 0.0f) const;
    /// A textured rectangle. Always goes to the main batch, also on the foreground list. The software test backend
    /// skips textured commands.
    void AddImage(SDL_Texture* tex, Vec2 min, Vec2 max, Vec2 uv0 = Vec2{0.0f, 0.0f}, Vec2 uv1 = Vec2{1.0f, 1.0f},
                  Color32 tint = 0xffffffffu) const;
    void PushClipRect(Vec2 min, Vec2 max) const;
    void PopClipRect() const;
    /// A path of up to 256 points shared by both lists (cleared by PathStroke and PathFillConvex).
    void PathClear() const;
    void PathLineTo(Vec2 p) const;
    /// Points along an arc (radians; 0 points right, angles grow clockwise on screen). `segments` 0 picks a count.
    void PathArcTo(Vec2 center, float radius, float a0, float a1, int segments = 0) const;
    void PathStroke(Color32 col, bool closed = false, float thickness = 1.0f) const;
    void PathFillConvex(Color32 col) const;
};
const DrawList& GetWindowDrawList();
const DrawList& GetForegroundDrawList();

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
bool IsItemActive();         // pressed and held (or, for a text field, being dragged)
bool IsItemActivated();      // became active this frame
bool IsItemDeactivated();    // stopped being active this frame
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
/// next frame). Supported: ItemSpacing (x, y between widgets), WindowPadding (x), FrameRounding and ChildRounding (corner radius),
/// WindowRounding, BorderSize (all three ImGui border sizes). The others are accepted and do nothing, so ported code compiles:
/// FramePadding, ButtonTextAlign, SelectableTextAlign, ItemInnerSpacing, ScrollbarSize, Alpha.
enum StyleVar : int {
    StyleVar_Alpha = 0, StyleVar_WindowPadding, StyleVar_WindowRounding, StyleVar_FramePadding, StyleVar_FrameRounding,
    StyleVar_ItemSpacing, StyleVar_ItemInnerSpacing, StyleVar_ChildRounding, StyleVar_BorderSize, StyleVar_ButtonTextAlign,
    StyleVar_SelectableTextAlign, StyleVar_ScrollbarSize, StyleVar_COUNT
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
enum MouseCursor : int { MouseCursor_Arrow = 0, MouseCursor_TextInput, MouseCursor_ResizeAll, MouseCursor_ResizeNS, MouseCursor_ResizeEW,
                         MouseCursor_ResizeNESW, MouseCursor_ResizeNWSE, MouseCursor_Hand, MouseCursor_NotAllowed, MouseCursor_COUNT };
void SetMouseCursor(MouseCursor cursor);
MouseCursor GetMouseCursor();
/// Read-only facts about the frame, filled by BeginFrame: pointer, wheel, display size, modifier keys, the frame time, and whether
/// the UI wants the mouse (pointer over a window or a widget is active) or the keyboard (a text field has focus), so the host knows
/// not to also give them to the game.
struct IO {
    Vec2 mousePos, mouseDelta, displaySize; float mouseWheel = 0.0f; bool mouseDown[2] = {false, false};
    bool keyCtrl = false, keyShift = false, keyAlt = false;
    bool wantCaptureMouse = false, wantCaptureKeyboard = false, wantTextInput = false;
    float deltaTime = 1.0f / 60.0f;
};
const IO& GetIO();

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
// a set one overrides it. Which widget draws with which colour is listed in docs/own_ui_toolkit.md section 4.33; a few
// (the navigation highlights, BorderShadow, DragDropTarget, the unfocused tab colours, the hovered / active separator)
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
    // the button colours are the original three fields under their Dear ImGui names
    Col_Button = Col_Bg, Col_ButtonHovered = Col_BgHover, Col_ButtonActive = Col_BgDown,
};
/// Temporarily override a theme color (RGBA 0..255). Every PushStyleColor must be
/// matched by a PopStyleColor. Nestable — great for coloring one widget or section.
void PushStyleColor(Col which, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255);
/// The same with a packed colour (see RGBA) or four floats 0..1 (non-finite values count as 0; out of range ones are clamped).
void PushStyleColor(Col which, Color32 color);
struct Vec4 { float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f; };
void PushStyleColor(Col which, Vec4 rgba);
void PopStyleColor(int count = 1);
/// Set a colour until ResetStyleColor (or the next UseLauncherLook), outside the push / pop stack.
void SetStyleColor(Col which, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255);
void ResetStyleColor(Col which);
/// What the colour is now, as RGBA floats 0..1: the pushed / set value, or the base theme's value for a colour never set.
/// An id outside the enum answers (0, 0, 0, 0). GetStyleColorName gives "Col_Text" for Col_Text (or "" outside the enum).
Vec4 GetStyleColorVec4(Col which);
const char* GetStyleColorName(Col which);
/// Dear ImGui style colour conversions (all components 0..1; hue wraps).
Vec4 ColorConvertU32ToFloat4(Color32 c);
Color32 ColorConvertFloat4ToU32(Vec4 c);
void ColorConvertRGBtoHSV(float r, float g, float b, float* h, float* s, float* v);
void ColorConvertHSVtoRGB(float h, float s, float v, float* r, float* g, float* b);

} // namespace OkayUI
