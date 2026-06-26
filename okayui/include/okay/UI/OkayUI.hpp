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

// ---------------------------------------------------------------------------
// Auto-layout (ImGui-style). Everything above takes explicit coordinates; the API
// below stacks widgets automatically inside a window, so you write a UI as a
// sequence of calls instead of placing every rect by hand. Widget IDs are derived
// from their label text (like ImGui), so most calls take no id.
// ---------------------------------------------------------------------------

/// Begin an auto-layout window at (x, y) sized w*h. The position is the INITIAL
/// placement; the window is draggable by its title bar thereafter (state keyed by
/// the title). Widgets called until End() stack inside it. Returns true (reserved
/// for a future collapsed state) — pair every Begin() with End().
bool Begin(const char* title, float x, float y, float w, float h);
void End();

/// Keep the next widget on the current line instead of starting a new one
/// (`spacing` < 0 uses the theme default gap).
void SameLine(float spacing = -1.0f);
/// A full-width horizontal divider.
void Separator();
/// Vertical empty space of height `h`.
void Spacing(float h = 8.0f);
/// A line of text at the layout cursor.
void Text(const char* s);

// ImGui-style widget overloads that auto-place inside the current window (id is
// hashed from the label). The explicit-coordinate versions above still work for
// free-form placement.
bool Button(const char* label);
bool Checkbox(const char* label, bool* value);
bool RadioButton(const char* label, int* value, int option);
bool SliderFloat(const char* label, float* value, float minV, float maxV);
void ProgressBar(float fraction, const char* overlay = nullptr);
bool InputText(const char* label, char* buf, int cap);

/// The active theme (mutable — tweak colors/sizes in place).
Theme& Style();

} // namespace OkayUI
