#pragma once
// Shared editor widgets (EditorWidgets.cpp): the two looks (Dark / Light) and their one accent, component category
// tints, drawn toolbar icons, and the icon / search / toggle buttons.
#include "imgui.h"
#include "ThemeModel.hpp"
#include <cstddef>

namespace oked {

// The editor's look: 0 Dark, 1 Light (saved in okay_settings.txt as `theme`; the old Classic look and the accent presets are gone).
// The palettes are plain numbers in ThemeModel.hpp; every colour here follows the current look.
extern int g_theme;

const okay::edtheme::Palette& ThemePalette();    // the current look's chrome palette
const okay::edtheme::CodePalette& ThemeCodePalette();   // ... and its script editor palette
bool ThemeIsLight();
// The whole ImGui style palette for the current look (called by ApplyTheme in main.cpp). Neutral greys, one neutral accent,
// and a last pass that turns any colour still leaning blue (an ImGui default nobody set) into the grey of the same brightness.
void ApplyEditorColors(ImGuiStyle& s);

// The ONE accent colour of the current look, for panels that tint their own widgets (selection bars, active toggles, links).
// Dark: light graphite. Light: dark graphite. There is no accent menu and no blue.
ImVec4 AccentCol(float alpha = 1.0f);
ImU32  AccentU32(int alpha255 = 255);
// The accent pushed further from the surface (hovered / active accent text, links).
ImVec4 AccentLitCol(float alpha = 1.0f);
// Text drawn ON an accent fill (a primary button, an active toolbar toggle): dark on Dark, light on Light.
ImVec4 AccentOnCol();
// The look's normal / secondary text, the hairline-and-hover-lift colour (white on Dark, black on Light) and a surface colour,
// for the few places that draw their own chrome with a draw list instead of an ImGui style colour.
ImVec4 TextCol(float alpha = 1.0f);
ImVec4 DimCol(float alpha = 1.0f);
ImVec4 InkCol(float alpha = 1.0f);
ImVec4 SurfaceCol(int which);   // 0 deep, 1 sunken, 2 base, 3 raised, 4 field, 5 control
// Status colours (never blue): 0 good, 1 warning, 2 error. Readable on every surface of the look.
ImVec4 StatusCol(int kind, float alpha = 1.0f);
// The colour that marks prefab instances, prefab changes and Prefab Mode (a lime green; it was blue once). Darker on the light look.
ImVec4 PrefabCol(float alpha = 1.0f);
ImU32  PrefabU32(int alpha255 = 255);
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

} // namespace oked
