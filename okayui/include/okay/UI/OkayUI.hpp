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

namespace OkayUI {

/// This frame's input, supplied by the host (OkayUI polls nothing itself).
struct Input {
    float       mouseX    = 0.0f;
    float       mouseY    = 0.0f;
    bool        mouseDown = false;     // left mouse button held this frame
    bool        blocked   = false;     // another layer (e.g. ImGui) owns the mouse -> ignore clicks
    // Keyboard (for TextField). The host forwards SDL_TEXTINPUT text and key edges
    // only when OkayUI should receive them (e.g. ImGui isn't capturing the keyboard).
    const char* text      = nullptr;   // UTF-8 characters typed this frame, or null
    bool        backspace = false;     // Backspace pressed this frame (delete last char)
    float       wheel     = 0.0f;      // mouse-wheel delta this frame (+up / -down), for scrolling
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
};

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
/// always kept NUL-terminated). Click to focus; typed text appends and Backspace
/// deletes, fed via Input::text / Input::backspace. Returns true on any frame the
/// text changed. The buffer is owned by the caller — no allocation here.
bool TextField(int id, float x, float y, float w, float h, char* buf, int cap);

/// Flush the batched geometry to the renderer. Call AFTER ImGui has rendered so
/// OkayUI draws on top; the renderer's blend mode is saved and restored so nothing
/// else is disturbed.
void EndFrame(SDL_Renderer* renderer);

// ---- Custom backends (e.g. Direct3D 11) ----------------------------------------
// Raw batched geometry for a renderer other than SDL. Valid after the frame's widget
// calls until the next BeginFrame. Vertex layout matches SDL_Vertex exactly:
//   offset 0: float position[2]   offset 8: uint8 color[4] (RGBA)   offset 12: float uv[2]
// stride = 20 bytes. Indices are 32-bit, two triangles per quad.
struct DrawData {
    const void* vertices     = nullptr;
    int         vertexCount  = 0;
    const int*  indices      = nullptr;
    int         indexCount   = 0;
    int         vertexStride = 0;   // bytes per vertex
};
DrawData GetDrawData();

/// Finalize the frame's interaction state WITHOUT drawing — for custom backends that
/// submit GetDrawData() themselves instead of calling EndFrame(SDL_Renderer*).
void EndFrameData();

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
void End();

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
bool Checkbox(const char* label, bool* value);
bool RadioButton(const char* label, int* value, int option);
bool SliderFloat(const char* label, float* value, float minV, float maxV);
/// An integer slider over [minV, maxV]. Edits *value in place; true on change.
bool SliderInt(const char* label, int* value, int minV, int maxV);
void ProgressBar(float fraction, const char* overlay = nullptr);
/// A line graph of `values[0..count)`. If scaleMin >= scaleMax the range is taken
/// from the data. `height` <= 0 uses a default. Great for FPS/telemetry HUDs.
void PlotLines(const char* label, const float* values, int count,
               float scaleMin = 0.0f, float scaleMax = 0.0f, float height = 0.0f);
/// A bar chart of `values[0..count)` (same scaling rules as PlotLines).
void PlotHistogram(const char* label, const float* values, int count,
                   float scaleMin = 0.0f, float scaleMax = 0.0f, float height = 0.0f);
/// A read-only "label: value" row (ImGui's LabelText) — value on the left, label right.
void LabelText(const char* label, const char* value);
bool InputText(const char* label, char* buf, int cap);
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

/// If the most recently issued widget is hovered, show a tooltip box at the cursor.
/// Call right after the widget you want to annotate.
void Tooltip(const char* text);

/// Drag-to-scrub a float: hold and move the cursor left/right to change *v by
/// `speed` per pixel. If minV < maxV the value is clamped. Returns true on change.
bool DragFloat(const char* label, float* v, float speed = 0.1f, float minV = 0.0f, float maxV = 0.0f);
/// Drag-to-scrub an int (same idea). minV < maxV clamps. Returns true on change.
bool DragInt(const char* label, int* v, float speed = 0.25f, int minV = 0, int maxV = 0);

/// An RGB color editor: a swatch plus three 0..1 channel sliders on one row.
/// Edits rgb[0..2] in place; returns true on any frame a channel changed.
bool ColorEdit3(const char* label, float rgb[3]);

/// A collapsing tree node. Returns true while expanded; when it does, the widgets
/// that follow are INDENTED until you call TreePop(). Nestable. State kept per label.
bool TreeNode(const char* label);
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
void EndMenu();
/// An item in the currently-open menu; returns true the frame it is clicked.
bool MenuItem(const char* label);

/// A full-width selectable row (list entries). Drawn highlighted when `selected`;
/// returns true the frame it is clicked.
bool Selectable(const char* label, bool selected);

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

/// The active theme (mutable — tweak colors/sizes in place).
Theme& Style();

// ---- Scoped style overrides (ImGui-style) --------------------------------------
// Which theme color a PushStyleColor call overrides.
enum Col {
    Col_Bg, Col_BgHover, Col_BgDown, Col_Border,
    Col_Text, Col_Panel, Col_Track, Col_Accent,
    Col_COUNT
};
/// Temporarily override a theme color (RGBA 0..255). Every PushStyleColor must be
/// matched by a PopStyleColor. Nestable — great for coloring one widget or section.
void PushStyleColor(Col which, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255);
void PopStyleColor(int count = 1);

} // namespace OkayUI
