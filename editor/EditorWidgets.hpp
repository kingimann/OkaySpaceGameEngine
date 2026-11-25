#pragma once
// Shared editor widgets (EditorWidgets.cpp): the theme accent, component category
// tints, drawn toolbar icons, and the icon / search / toggle buttons.
#include "imgui.h"
#include <cstddef>

namespace oked {


// The editor's one accent colour (the highlight used on selections, buttons, checks, tabs, etc.).
// There is no accent picker any more: Edit > Dark Mode / Light Mode is the only look choice.
struct AccentPreset { const char* name; float r, g, b; };
inline constexpr AccentPreset kAccents[] = {
    {"Classic", 0.235f, 0.475f, 0.800f}, // muted selection blue
};

// The current accent colour, for panels that tint their own widgets to match the theme.
ImVec4 AccentCol(float alpha = 1.0f);
// A category tint for a component header, from its label (physics, render, audio, UI, ...).
ImVec4 CategoryColor(const char* label);
// The icon family for a component header (0 dot, 1 square, 2 circle, 3 triangle, 4 diamond, 5 speaker, 6 ring).
int CategoryGlyph(const char* label);
// A button with an accent "pressed" look while `active` (toolbar toggles).
bool AccentToggleButton(const char* label, bool active, const ImVec2& size = ImVec2(0, 0));

// Drawn vector glyphs for compact toolbar buttons (the UI font has no icons).
enum class TbIcon { More, Plus, Move, Rotate, Scale, Global, Local, Snap, Frame, Search, Clear, Expand, Collapse, Sort, Folder, Grid, List,
                    // the Modeling window's page list (one per page) and its fold button
                    Star, Shapes, Steps, Spin, Bend, Brush, Sparkle, Import, PanelLeft };
void DrawTbIcon(ImDrawList* dl, TbIcon ic, ImVec2 c, float s, ImU32 col);
// Square icon button (frame height); accent-tinted while `active`. `tip` shows on hover.
bool IconButton(const char* id, TbIcon ic, const char* tip = nullptr, bool active = false);
// Icon button with the tool's name beside the icon (the Scene toolbar's "Toolbar labels" mode); same look and tooltip as IconButton.
bool IconLabelButton(const char* id, TbIcon ic, const char* label, const char* tip = nullptr, bool active = false);
// Plain buttons that always show a tooltip on hover: for the small remove (x), browse (...), step and arrow buttons.
bool TipButton(const char* tip, const char* label, const ImVec2& size = ImVec2(0, 0));
bool TipSmallButton(const char* tip, const char* label);
bool TipArrowButton(const char* tip, const char* id, ImGuiDir dir);
// A search field with a magnifier and a clear (x) button. `width` <= 0 fills the rest minus |width|.
bool SearchField(const char* id, char* buf, std::size_t bufSize, const char* hint, float width);
// Right-align the next `w` pixels of items on the current line.
void SameLineRight(float w);

// ---- Controls with a state you can see -------------------------------------------------------------------------
// An on / off switch drawn as a button with a check box inside it: the box is empty and outlined when off, filled with
// the accent colour and a tick when on, so the state reads at a glance (a plain checkbox in this theme looked like an
// empty square). Click anywhere on it to flip `*v`. Returns true when it changed. `tip` shows on hover.
bool ToggleChip(const char* label, bool* v, const char* tip = nullptr);
float ToggleChipWidth(const char* label);   // the width ToggleChip takes (for toolbar planning)
// A button that clearly looks off when it cannot be used: when `enabled` it is a normal filled button with bright text;
// when not, it has no fill, a faint outline and dimmed text, and cannot be clicked. `tip` (enabled) / `whyNot`
// (disabled) show on hover.
bool StateButton(const char* label, bool enabled, const ImVec2& size, const char* tip = nullptr, const char* whyNot = nullptr);
// The editor's dropdown: the chevron inside the box, the same look as the launcher's (shared/ImGuiDropdown.hpp).
// Same arguments as ImGui::BeginCombo; close with ImGui::EndCombo.
bool BeginDropdown(const char* label, const char* preview, int flags = 0);
// Tabs that show which one is open: the open tab is filled with the accent colour and white text, the others are
// plain buttons. Wrap ImGui::BeginTabBar ... EndTabBar in these.
void PushClearTabColors();
void PopClearTabColors();

} // namespace oked
