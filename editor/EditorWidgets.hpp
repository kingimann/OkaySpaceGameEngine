#pragma once
// Shared editor widgets (EditorWidgets.cpp): the theme accent, component category
// tints, drawn toolbar icons, and the icon / search / toggle buttons.
#include "imgui.h"
#include <cstddef>

namespace oked {

extern int g_accent;   // accent colour preset (index into kAccents; saved in settings)

// Selectable editor accent colours (the highlight used on selections, buttons,
// checks, tabs, etc.). Index persists in settings.
struct AccentPreset { const char* name; float r, g, b; };
inline constexpr AccentPreset kAccents[] = {
    {"Classic", 0.235f, 0.475f, 0.800f}, // muted selection blue (default)
    {"Blue",    0.26f, 0.59f, 0.98f},
    {"Teal",    0.18f, 0.78f, 0.74f},
    {"Violet",  0.56f, 0.46f, 0.96f},
    {"Green",   0.36f, 0.80f, 0.46f},
    {"Amber",   0.95f, 0.66f, 0.26f},
    {"Rose",    0.96f, 0.42f, 0.56f},
    {"Slate",   0.52f, 0.58f, 0.68f},
    {"Cyan",    0.30f, 0.80f, 0.92f},
    {"Orange",  0.96f, 0.55f, 0.22f},
    {"Crimson", 0.92f, 0.34f, 0.40f},
    {"Mint",    0.40f, 0.86f, 0.66f},
    {"Indigo",  0.42f, 0.40f, 0.92f},
};
inline constexpr int kAccentCount = (int)(sizeof(kAccents) / sizeof(kAccents[0]));

// The current accent colour, for panels that tint their own widgets to match the theme.
ImVec4 AccentCol(float alpha = 1.0f);
// A category tint for a component header, from its label (physics, render, audio, UI, ...).
ImVec4 CategoryColor(const char* label);
// The icon family for a component header (0 dot, 1 square, 2 circle, 3 triangle, 4 diamond, 5 speaker, 6 ring).
int CategoryGlyph(const char* label);
// A button with an accent "pressed" look while `active` (toolbar toggles).
bool AccentToggleButton(const char* label, bool active, const ImVec2& size = ImVec2(0, 0));

// Drawn vector glyphs for compact toolbar buttons (the UI font has no icons).
enum class TbIcon { More, Plus, Move, Rotate, Scale, Global, Local, Snap, Frame, Search, Clear, Expand, Collapse, Sort, Folder, Grid, List };
void DrawTbIcon(ImDrawList* dl, TbIcon ic, ImVec2 c, float s, ImU32 col);
// Square icon button (frame height); accent-tinted while `active`. `tip` shows on hover.
bool IconButton(const char* id, TbIcon ic, const char* tip = nullptr, bool active = false);
// A search field with a magnifier and a clear (x) button. `width` <= 0 fills the rest minus |width|.
bool SearchField(const char* id, char* buf, std::size_t bufSize, const char* hint, float width);
// Right-align the next `w` pixels of items on the current line.
void SameLineRight(float w);

} // namespace oked
