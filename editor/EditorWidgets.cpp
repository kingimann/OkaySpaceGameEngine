// Shared editor widgets: the two looks (Dark / Light) and their one accent, component category tints, the drawn
// toolbar icons and the icon / search / toggle buttons every panel uses.
#include "EditorCommon.hpp"
#include "EditorWidgets.hpp"

namespace oked {

using okay::edtheme::Rgb;

// ---- the looks --------------------------------------------------------------------------------------------------
// The palettes are numbers in ThemeModel.hpp (neutral greys, one accent per look, unit tested); this is the ImGui side.
const okay::edtheme::Palette& ThemePalette() { return okay::edtheme::PaletteFor(okay::edtheme::LookFromSaved(g_theme)); }
const okay::edtheme::CodePalette& ThemeCodePalette() { return okay::edtheme::CodePaletteFor(okay::edtheme::LookFromSaved(g_theme)); }
bool ThemeIsLight() { return okay::edtheme::LookFromSaved(g_theme) == okay::edtheme::Look::Light; }

static ImVec4 V4(const Rgb& c, float a = 1.0f) { return ImVec4(c.r, c.g, c.b, a); }
static ImVec4 MixV(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

// The current accent colour, for panels that want to tint their own widgets to match the theme (component headers, the
// Add Component button, selection bars, ...). One fixed neutral accent per look; no menu.
ImVec4 AccentCol(float alpha) { return V4(ThemePalette().accent, alpha); }
ImU32  AccentU32(int alpha255) {   // (no style alpha, like an IM_COL32 literal)
    return ImGui::ColorConvertFloat4ToU32(AccentCol((float)(alpha255 < 0 ? 0 : (alpha255 > 255 ? 255 : alpha255)) / 255.0f));
}
ImVec4 AccentLitCol(float alpha) { return V4(ThemePalette().accentLit, alpha); }
ImVec4 AccentOnCol() { return V4(ThemePalette().accentOn); }
ImVec4 TextCol(float alpha) { return V4(ThemePalette().text, alpha); }
ImVec4 DimCol(float alpha) { return V4(ThemePalette().textDim, alpha); }
ImVec4 InkCol(float alpha) { return V4(ThemePalette().ink, alpha); }
ImVec4 SurfaceCol(int which) {
    const okay::edtheme::Palette& p = ThemePalette();
    switch (which) {
        case 0: return V4(p.deep);   case 1: return V4(p.sunken); case 2: return V4(p.base);
        case 3: return V4(p.raised); case 4: return V4(p.field);  default: return V4(p.control);
    }
}
ImVec4 StatusCol(int kind, float alpha) {
    const okay::edtheme::Palette& p = ThemePalette();
    return V4(kind == 0 ? p.good : (kind == 1 ? p.warn : p.bad), alpha);
}

ImVec4 PrefabCol(float alpha) {
    const ImVec4 c(0.62f, 0.86f, 0.55f, alpha);
    return ThemeIsLight() ? MixV(c, ImVec4(0, 0, 0, alpha), 0.55f) : c;
}
ImU32 PrefabU32(int alpha255) {
    return ImGui::ColorConvertFloat4ToU32(PrefabCol((float)(alpha255 < 0 ? 0 : (alpha255 > 255 ? 255 : alpha255)) / 255.0f));
}

// The whole ImGui palette for the current look. Starts from ImGui's dark style for the few entries nobody sets, sets every
// colour the editor draws from the palette, then turns anything that still leans blue into a grey.
void ApplyEditorColors(ImGuiStyle& s) {
    ImGui::StyleColorsDark(&s);
    const okay::edtheme::Palette& p = ThemePalette();
    const bool light = ThemeIsLight();
    const float hl = light ? 1.8f : 1.0f;   // hairlines and washes are stronger on the light look so they still show
    const ImVec4 deep = V4(p.deep), sunken = V4(p.sunken), base = V4(p.base), raised = V4(p.raised), field = V4(p.field),
                 control = V4(p.control), text = V4(p.text), dim = V4(p.textDim), accent = V4(p.accent),
                 accentLit = V4(p.accentLit), ink = V4(p.ink);
    auto lift = [&](const ImVec4& c, float t) { return MixV(c, ink, t); };
    auto inkA = [&](float a) { return V4(p.ink, a * hl > 1.0f ? 1.0f : a * hl); };
    auto withA = [](const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); };
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                  = text;
    c[ImGuiCol_TextDisabled]          = dim;
    c[ImGuiCol_WindowBg]              = base;
    c[ImGuiCol_ChildBg]               = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]               = withA(MixV(raised, base, 0.35f), 0.985f);
    c[ImGuiCol_Border]                = inkA(0.075f);
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = field;
    c[ImGuiCol_FrameBgHovered]        = lift(field, 0.05f);
    c[ImGuiCol_FrameBgActive]         = lift(field, 0.09f);
    c[ImGuiCol_TitleBg]               = deep;
    c[ImGuiCol_TitleBgActive]         = MixV(deep, sunken, 0.6f);
    c[ImGuiCol_TitleBgCollapsed]      = deep;
    c[ImGuiCol_MenuBarBg]             = sunken;
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]         = inkA(0.12f);
    c[ImGuiCol_ScrollbarGrabHovered]  = inkA(0.24f);
    c[ImGuiCol_ScrollbarGrabActive]   = accent;
    c[ImGuiCol_CheckMark]             = accentLit;
    c[ImGuiCol_CheckboxSelectedBg]    = lift(field, 0.09f);
    c[ImGuiCol_SliderGrab]            = accent;
    c[ImGuiCol_SliderGrabActive]      = accentLit;
    c[ImGuiCol_Button]                = control;
    c[ImGuiCol_ButtonHovered]         = lift(control, 0.08f);
    c[ImGuiCol_ButtonActive]          = lift(control, 0.16f);   // (normal text stays on it, so it is a lift, not the accent fill)
    c[ImGuiCol_Header]                = MixV(control, accent, 0.18f);
    c[ImGuiCol_HeaderHovered]         = lift(control, 0.07f);
    c[ImGuiCol_HeaderActive]          = MixV(control, accent, 0.28f);
    c[ImGuiCol_Separator]             = inkA(0.08f);
    c[ImGuiCol_SeparatorHovered]      = withA(accent, 0.70f);
    c[ImGuiCol_SeparatorActive]       = accent;
    c[ImGuiCol_ResizeGrip]            = inkA(0.06f);
    c[ImGuiCol_ResizeGripHovered]     = withA(accent, 0.60f);
    c[ImGuiCol_ResizeGripActive]      = accent;
    c[ImGuiCol_InputTextCursor]       = text;
    // The selected tab of every group matches the panel under it (so it reads as "connected"), focused or not; only the
    // overline says which group has focus.
    c[ImGuiCol_Tab]                   = sunken;
    c[ImGuiCol_TabHovered]            = MixV(sunken, control, 0.55f);
    c[ImGuiCol_TabSelected]           = base;
    c[ImGuiCol_TabSelectedOverline]   = accent;
    c[ImGuiCol_TabDimmed]             = sunken;
    c[ImGuiCol_TabDimmedSelected]     = base;
    c[ImGuiCol_TabDimmedSelectedOverline] = withA(dim, 0.35f);
    c[ImGuiCol_DockingPreview]        = withA(accent, 0.55f);
    c[ImGuiCol_DockingEmptyBg]        = deep;
    c[ImGuiCol_TableHeaderBg]         = MixV(sunken, base, 0.5f);
    c[ImGuiCol_TableBorderStrong]     = inkA(0.07f);
    c[ImGuiCol_TableBorderLight]      = inkA(0.04f);
    c[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]         = inkA(0.022f);
    c[ImGuiCol_TextLink]              = accentLit;
    c[ImGuiCol_TextSelectedBg]        = withA(accent, 0.32f);
    c[ImGuiCol_TreeLines]             = inkA(0.12f);
    c[ImGuiCol_DragDropTarget]        = accentLit;
    c[ImGuiCol_DragDropTargetBg]      = withA(accent, 0.10f);
    c[ImGuiCol_UnsavedMarker]         = text;
    c[ImGuiCol_NavCursor]             = accent;
    c[ImGuiCol_NavWindowingHighlight] = withA(ink, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0, 0, 0, light ? 0.25f : 0.45f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, light ? 0.35f : 0.55f);
    // Plot colours: ImGui's own (grey / orange) are fine; they are not blue. Last pass for anything still leaning blue.
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        ImVec4& v = c[i];
        if (v.z > v.x + 0.02f) { const float l = 0.299f * v.x + 0.587f * v.y + 0.114f * v.z; v = ImVec4(l, l, l, v.w); }
    }
}

// A tasteful category tint for a component header, derived from its label — so the
// Inspector groups visually by system (physics/render/audio/UI/animation/script)
// while staying cohesive. Falls back to the theme accent.
ImVec4 CategoryColor(const char* label) {
    // The tints are for the dark look; on the light look they are darkened so they still read on a pale header.
    auto Cat = [](ImVec4 c) { return ThemeIsLight() ? MixV(c, ImVec4(0, 0, 0, c.w), 0.38f) : c; };
    auto has = [&](const char* k) { return std::strstr(label, k) != nullptr; };
    if (has("Collider") || has("Rigidbody") || has("Joint"))          return Cat(ImVec4(0.42f, 0.80f, 0.50f, 1.0f)); // physics — green
    if (has("Audio"))                                                 return Cat(ImVec4(0.95f, 0.62f, 0.30f, 1.0f)); // audio — orange
    if (has("Animat") || has("IK") || has("Root Motion"))            return Cat(ImVec4(0.92f, 0.50f, 0.74f, 1.0f)); // animation — pink
    if (has("Script") || has("Visual Script"))                       return Cat(ImVec4(0.90f, 0.80f, 0.35f, 1.0f)); // scripting — yellow
    if (has("UI ") || has("Canvas") || has("Button") || has("Panel") || has("Slider") ||
        has("Toggle") || has("Tabs") || has("Dropdown") || has("Scroll") || has("Layout") ||
        has("Progress") || has("Rating") || has("Stepper") || has("Input Field") || has("Tooltip"))
                                                                       return Cat(ImVec4(0.78f, 0.54f, 0.84f, 1.0f)); // UI — purple
    if (has("Renderer") || has("Sprite") || has("Mesh") || has("Text") || has("Particle") ||
        has("Light") || has("Camera") || has("Tilemap") || has("Terrain") || has("Water"))
                                                                       return Cat(ImVec4(0.94f, 0.56f, 0.46f, 1.0f)); // rendering — coral
    return AccentCol(1.0f);
}
// Icon family for a component header, matching CategoryColor's keywords:
// 0 dot (default), 1 square (rendering), 2 circle (physics), 3 triangle
// (animation), 4 diamond (scripts), 5 speaker (audio), 6 ring (UI).
int CategoryGlyph(const char* label) {
    auto has = [&](const char* k) { return std::strstr(label, k) != nullptr; };
    if (has("Collider") || has("Rigidbody") || has("Joint"))          return 2;
    if (has("Audio"))                                                 return 5;
    if (has("Animat") || has("IK") || has("Root Motion"))             return 3;
    if (has("Script") || has("Visual Script"))                        return 4;
    if (has("UI ") || has("Canvas") || has("Button") || has("Panel") || has("Slider") ||
        has("Toggle") || has("Tabs") || has("Dropdown") || has("Scroll") || has("Layout") ||
        has("Progress") || has("Rating") || has("Stepper") || has("Input Field") || has("Tooltip"))
                                                                       return 6;
    if (has("Renderer") || has("Sprite") || has("Mesh") || has("Text") || has("Particle") ||
        has("Light") || has("Camera") || has("Tilemap") || has("Terrain") || has("Water"))
                                                                       return 1;
    return 0;
}

// A button that shows an accent "pressed" look while `active` — for toolbar
// toggles (transform tools, Local/Global, Snap, edit modes) so the active state
// reads consistently in the theme colour instead of ad-hoc blues.
bool AccentToggleButton(const char* label, bool active, const ImVec2& size) {
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button,        AccentCol(0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, AccentCol(1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  AccentCol(0.80f));
        ImGui::PushStyleColor(ImGuiCol_Text,          AccentOnCol());
    }
    bool clicked = ImGui::Button(label, size);
    if (active) ImGui::PopStyleColor(4);
    return clicked;
}

// Compact square toolbar buttons with drawn vector glyphs (the UI font has no
// icon glyphs). Panels use these instead of rows of text buttons and checkboxes:
// the primary actions stay one click away and everything else lives in a "more"
// menu, so toolbars fit on one line like other editors.
void DrawTbIcon(ImDrawList* dl, TbIcon ic, ImVec2 c, float s, ImU32 col) {
    const float t = 1.6f;
    switch (ic) {
    case TbIcon::More:
        for (int i = -1; i <= 1; ++i) dl->AddCircleFilled(ImVec2(c.x + i * s * 0.36f, c.y), s * 0.1f, col);
        break;
    case TbIcon::Plus:
        dl->AddLine(ImVec2(c.x - s * 0.4f, c.y), ImVec2(c.x + s * 0.4f, c.y), col, t + 0.4f);
        dl->AddLine(ImVec2(c.x, c.y - s * 0.4f), ImVec2(c.x, c.y + s * 0.4f), col, t + 0.4f);
        break;
    case TbIcon::Move: {
        float r = s * 0.48f, a = s * 0.16f;
        dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, t);
        dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), col, t);
        dl->AddTriangleFilled(ImVec2(c.x + r + 1, c.y), ImVec2(c.x + r - a, c.y - a), ImVec2(c.x + r - a, c.y + a), col);
        dl->AddTriangleFilled(ImVec2(c.x - r - 1, c.y), ImVec2(c.x - r + a, c.y + a), ImVec2(c.x - r + a, c.y - a), col);
        dl->AddTriangleFilled(ImVec2(c.x, c.y - r - 1), ImVec2(c.x + a, c.y - r + a), ImVec2(c.x - a, c.y - r + a), col);
        dl->AddTriangleFilled(ImVec2(c.x, c.y + r + 1), ImVec2(c.x - a, c.y + r - a), ImVec2(c.x + a, c.y + r - a), col);
        break; }
    case TbIcon::Rotate: {
        float r = s * 0.38f;
        dl->PathArcTo(c, r, IM_PI * 0.15f, IM_PI * 1.75f, 18);
        dl->PathStroke(col, 0, t);
        ImVec2 e(c.x + r * std::cos(IM_PI * 0.15f), c.y + r * std::sin(IM_PI * 0.15f));
        float a = s * 0.17f;
        dl->AddTriangleFilled(ImVec2(e.x - a, e.y - a * 0.2f), ImVec2(e.x + a, e.y - a * 0.2f), ImVec2(e.x, e.y + a * 1.1f), col);
        break; }
    case TbIcon::Scale: {
        float r = s * 0.42f;
        dl->AddRect(ImVec2(c.x - r, c.y - r * 0.1f), ImVec2(c.x + r * 0.1f, c.y + r), col, 1.0f, 0, t);
        dl->AddLine(ImVec2(c.x - r * 0.1f, c.y + r * 0.1f), ImVec2(c.x + r, c.y - r), col, t);
        dl->AddLine(ImVec2(c.x + r, c.y - r), ImVec2(c.x + r * 0.35f, c.y - r), col, t);
        dl->AddLine(ImVec2(c.x + r, c.y - r), ImVec2(c.x + r, c.y - r * 0.35f), col, t);
        break; }
    case TbIcon::Global: {
        float r = s * 0.42f;
        dl->AddCircle(c, r, col, 20, t);
        dl->AddEllipse(c, ImVec2(r * 0.42f, r), col, 0.0f, 16, t * 0.8f);
        dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, t * 0.8f);
        break; }
    case TbIcon::Local: {   // isometric cube outline
        float r = s * 0.42f, h = r * 0.5f;
        ImVec2 top(c.x, c.y - r), lft(c.x - r, c.y - h), rgt(c.x + r, c.y - h), mid(c.x, c.y);
        ImVec2 bl(c.x - r, c.y + h), br(c.x + r, c.y + h), bot(c.x, c.y + r);
        ImVec2 hex[6] = {top, rgt, br, bot, bl, lft};
        dl->AddPolyline(hex, 6, col, ImDrawFlags_Closed, t);
        dl->AddLine(lft, mid, col, t); dl->AddLine(rgt, mid, col, t); dl->AddLine(mid, bot, col, t);
        break; }
    case TbIcon::Snap: {    // magnet
        float r = s * 0.3f, top = c.y - s * 0.4f;
        dl->PathArcTo(ImVec2(c.x, c.y + s * 0.05f), r, 0.0f, IM_PI, 14);
        dl->PathStroke(col, 0, t + 1.6f);
        dl->AddLine(ImVec2(c.x - r, c.y + s * 0.05f), ImVec2(c.x - r, top), col, t + 1.6f);
        dl->AddLine(ImVec2(c.x + r, c.y + s * 0.05f), ImVec2(c.x + r, top), col, t + 1.6f);
        dl->AddLine(ImVec2(c.x - r, top + s * 0.18f), ImVec2(c.x - r, top), IM_COL32(230, 90, 90, 255), t + 1.8f);
        dl->AddLine(ImVec2(c.x + r, top + s * 0.18f), ImVec2(c.x + r, top), IM_COL32(230, 90, 90, 255), t + 1.8f);
        break; }
    case TbIcon::Frame: {   // corner brackets around a dot
        float r = s * 0.44f, k = s * 0.2f;
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2) {
                ImVec2 p(c.x + sx * r, c.y + sy * r);
                dl->AddLine(p, ImVec2(p.x - sx * k, p.y), col, t);
                dl->AddLine(p, ImVec2(p.x, p.y - sy * k), col, t);
            }
        dl->AddCircleFilled(c, s * 0.12f, col);
        break; }
    case TbIcon::Search: {
        float r = s * 0.27f;
        ImVec2 o(c.x - s * 0.08f, c.y - s * 0.08f);
        dl->AddCircle(o, r, col, 14, t);
        dl->AddLine(ImVec2(o.x + r * 0.7f, o.y + r * 0.7f), ImVec2(c.x + s * 0.4f, c.y + s * 0.4f), col, t + 0.6f);
        break; }
    case TbIcon::Clear: {   // circle with a slash ("clear all")
        float r = s * 0.38f;
        dl->AddCircle(c, r, col, 18, t);
        dl->AddLine(ImVec2(c.x - r * 0.7f, c.y + r * 0.7f), ImVec2(c.x + r * 0.7f, c.y - r * 0.7f), col, t);
        break; }
    case TbIcon::Expand: case TbIcon::Collapse: {
        float r = s * 0.3f, d = (ic == TbIcon::Expand) ? 1.0f : -1.0f;
        for (int k = 0; k < 2; ++k) {
            float y = c.y + (k ? s * 0.22f : -s * 0.22f);
            dl->AddLine(ImVec2(c.x - r, y - d * r * 0.5f), ImVec2(c.x, y + d * r * 0.5f), col, t);
            dl->AddLine(ImVec2(c.x, y + d * r * 0.5f), ImVec2(c.x + r, y - d * r * 0.5f), col, t);
        }
        break; }
    case TbIcon::Folder: {
        float w = s * 0.46f, h = s * 0.34f;
        dl->AddRect(ImVec2(c.x - w, c.y - h * 0.6f), ImVec2(c.x + w, c.y + h), col, 1.5f, 0, t);
        dl->AddLine(ImVec2(c.x - w, c.y - h * 0.6f), ImVec2(c.x - w, c.y - h * 1.1f), col, t);
        dl->AddLine(ImVec2(c.x - w, c.y - h * 1.1f), ImVec2(c.x - w * 0.15f, c.y - h * 1.1f), col, t);
        dl->AddLine(ImVec2(c.x - w * 0.15f, c.y - h * 1.1f), ImVec2(c.x + w * 0.05f, c.y - h * 0.6f), col, t);
        break; }
    case TbIcon::Grid: {
        float g = s * 0.17f, o = s * 0.2f;
        for (int i = -1; i <= 1; i += 2)
            for (int j = -1; j <= 1; j += 2)
                dl->AddRectFilled(ImVec2(c.x + i * o - g, c.y + j * o - g), ImVec2(c.x + i * o + g, c.y + j * o + g), col, 1.0f);
        break; }
    case TbIcon::List: {
        for (int k = -1; k <= 1; ++k) {
            float y = c.y + k * s * 0.26f;
            dl->AddCircleFilled(ImVec2(c.x - s * 0.36f, y), s * 0.06f, col);
            dl->AddLine(ImVec2(c.x - s * 0.22f, y), ImVec2(c.x + s * 0.42f, y), col, t);
        }
        break; }
    case TbIcon::Sort: {
        for (int k = 0; k < 3; ++k) {
            float y = c.y - s * 0.3f + k * s * 0.3f, w = s * (0.25f + 0.2f * k);
            dl->AddLine(ImVec2(c.x - s * 0.42f, y), ImVec2(c.x - s * 0.42f + w, y), col, t);
        }
        break; }
    }
}
// Square icon button (frame-height); accent-tinted while `active`. `tip` shows on hover.
bool IconButton(const char* id, TbIcon ic, const char* tip, bool active) {
    float h = ImGui::GetFrameHeight();
    bool clicked = AccentToggleButton(id, active, ImVec2(h, h));
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImU32 col = ImGui::GetColorU32(active ? AccentOnCol()
                                          : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    if (!active && !ImGui::IsItemHovered()) col = ImGui::GetColorU32(ImGuiCol_Text, 0.8f);
    DrawTbIcon(ImGui::GetWindowDrawList(), ic, ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f),
               h * 0.5f, col);
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    return clicked;
}
// Icon + text button: the icon at the left, the name after it. The width is the icon plus the text.
bool IconLabelButton(const char* id, TbIcon ic, const char* label, const char* tip, bool active) {
    const float h = ImGui::GetFrameHeight();
    const float textW = ImGui::CalcTextSize(label).x;
    bool clicked = AccentToggleButton(id, active, ImVec2(h + textW + ImGui::GetStyle().FramePadding.x * 1.4f, h));
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    const bool hot = ImGui::IsItemHovered();
    ImU32 col = ImGui::GetColorU32(active ? AccentOnCol() : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    if (!active && !hot) col = ImGui::GetColorU32(ImGuiCol_Text, 0.8f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    DrawTbIcon(dl, ic, ImVec2(mn.x + h * 0.5f, (mn.y + mx.y) * 0.5f), h * 0.5f, col);
    dl->AddText(ImVec2(mn.x + h, mn.y + (mx.y - mn.y - ImGui::GetTextLineHeight()) * 0.5f), col, label);
    if (tip && hot) ImGui::SetTooltip("%s", tip);
    return clicked;
}
bool TipButton(const char* tip, const char* label, const ImVec2& size) {
    const bool c = ImGui::Button(label, size);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    return c;
}
bool TipSmallButton(const char* tip, const char* label) {
    const bool c = ImGui::SmallButton(label);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    return c;
}
bool TipArrowButton(const char* tip, const char* id, ImGuiDir dir) {
    const bool c = ImGui::ArrowButton(id, dir);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    return c;
}
// A search field with a drawn magnifier inside and a clear (x) affordance when it
// has text. `width` <= 0 fills the remaining width minus |width|.
bool SearchField(const char* id, char* buf, std::size_t bufSize, const char* hint, float width) {
    ImGuiStyle& st = ImGui::GetStyle();
    float h = ImGui::GetFrameHeight();
    float w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x + width;
    if (w < h * 3.0f) w = h * 3.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(h * 0.95f, st.FramePadding.y));
    ImGui::SetNextItemWidth(w);
    bool changed = ImGui::InputTextWithHint(id, hint, buf, bufSize);
    ImGui::PopStyleVar();
    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    DrawTbIcon(dl, TbIcon::Search, ImVec2(mn.x + h * 0.5f, (mn.y + mx.y) * 0.5f), h * 0.42f,
               ImGui::GetColorU32(ImGuiCol_TextDisabled));
    if (buf[0]) {   // clear button drawn over the right edge of the field
        ImVec2 xc(mx.x - h * 0.5f, (mn.y + mx.y) * 0.5f);
        ImVec2 mp = ImGui::GetIO().MousePos;
        bool hot = std::fabs(mp.x - xc.x) < h * 0.45f && std::fabs(mp.y - xc.y) < h * 0.45f &&
                   ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        float r = h * 0.17f;
        ImU32 xcCol = ImGui::GetColorU32(hot ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        dl->AddLine(ImVec2(xc.x - r, xc.y - r), ImVec2(xc.x + r, xc.y + r), xcCol, 1.6f);
        dl->AddLine(ImVec2(xc.x - r, xc.y + r), ImVec2(xc.x + r, xc.y - r), xcCol, 1.6f);
        if (hot && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            buf[0] = '\0'; changed = true;
            ImGui::ClearActiveID();
        }
    }
    return changed;
}
// Right-align the next `w` pixels of items on the current line.
void SameLineRight(float w) {
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - w);
}

} // namespace oked
