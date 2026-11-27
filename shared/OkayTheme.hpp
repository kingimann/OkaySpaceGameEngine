// OkaySpace "Slate" look, shared by the launcher and the editor.
//
// It grew out of the chat window: blue-tinted dark surfaces in three depths
// (sunken rails/toolbars, the base panel, raised cards/popups), soft white
// hairlines instead of black borders, rounded controls, light text, and the
// accent colour reserved for interactive states (selection bars, checks,
// sliders, the active tab, primary buttons). Keep both apps on these values so
// they read as one product.
#pragma once
#include "imgui.h"
#include "okay/UI/OkayUIDesign.hpp"   // the designs: the one source of every colour and shape below
#include <cmath>
#include <cstdio>

namespace okaytheme {

// ---- The palette: every colour the launcher draws comes from these named colours ------------------------------------------
// Three themes (Slate, the default; Midnight; Light) define every token; the editor uses Slate. Page code never writes an RGB
// value: it asks for a token (surfaces, text, status colours) or for Ink/Shade (a see-through tint over a surface: white on the
// dark themes, black on Light). tests/test_launcher_theme.cpp checks the text contrast of every theme and that no page has a
// hard-coded colour.
struct Palette {
    const char* name = "Slate";
    bool light = false;
    ImVec4 deep, sunken, base, raised, field, control;   // surfaces, darkest to lightest on the dark themes (title bars and empty
                                                         // space; side rails and tool bars; the page; cards and popups; inputs; buttons)
    ImVec4 text, textDim;                                // body text; secondary text (both 4.5:1 or better on every surface)
    ImVec4 success, danger, warning, info;               // status colours, readable as text on every surface
    ImVec4 onColor;                                      // text on a filled accent or status colour
    ImVec4 ink;                                          // the tint colour for hovers, hairlines and soft fills (alpha given by the caller)
};
/// The palette of a resolved design (okay/UI/OkayUIDesign.hpp, the ONE theme source): what Dear ImGui and the page code use.
inline ImVec4 V4(const OkayUI::design::Rgba& c) { return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f); }
inline Palette PaletteFrom(const OkayUI::design::Tokens& t) {
    const OkayUI::design::Palette& d = t.c;
    Palette p; p.name = t.designName; p.light = d.light;
    p.deep = V4(d.deep); p.sunken = V4(d.sunken); p.base = V4(d.base); p.raised = V4(d.raised); p.field = V4(d.field); p.control = V4(d.control);
    p.text = V4(d.text); p.textDim = V4(d.textDim);
    p.success = V4(d.success); p.danger = V4(d.danger); p.warning = V4(d.warning); p.info = V4(d.info);
    p.onColor = V4(d.onAccent); p.ink = V4(OkayUI::design::WithAlpha(d.ink, 255));
    return p;
}
/// A built-in design's palette for one scheme (Auto: the design's own first choice).
inline Palette DesignPalette(const char* design, OkayUI::design::Scheme scheme = OkayUI::design::Scheme::Auto) {
    OkayUI::design::Options o; std::snprintf(o.design, sizeof o.design, "%s", design); o.scheme = scheme;
    return PaletteFrom(OkayUI::design::Resolve(o));
}
/// The three palettes the launcher offered before designs (frozen names): Slate, Midnight, and Slate's light scheme.
inline Palette SlatePalette()    { return DesignPalette("Slate", OkayUI::design::Scheme::Dark); }
inline Palette MidnightPalette() { return DesignPalette("Midnight"); }
inline Palette LightPalette()    { Palette p = DesignPalette("Slate", OkayUI::design::Scheme::Light); p.name = "Light"; return p; }
inline Palette& Current() { static Palette p = SlatePalette(); return p; }

// The surfaces and text as variables (the current theme's; Slate until a theme is set).
inline ImVec4 kDeep    = SlatePalette().deep;
inline ImVec4 kSunken  = SlatePalette().sunken;
inline ImVec4 kBase    = SlatePalette().base;
inline ImVec4 kRaised  = SlatePalette().raised;
inline ImVec4 kField   = SlatePalette().field;
inline ImVec4 kControl = SlatePalette().control;
inline ImVec4 kText    = SlatePalette().text;
inline ImVec4 kTextDim = SlatePalette().textDim;
inline void SetPalette(const Palette& p) {
    Current() = p;
    kDeep = p.deep; kSunken = p.sunken; kBase = p.base; kRaised = p.raised; kField = p.field; kControl = p.control;
    kText = p.text; kTextDim = p.textDim;
}

/// A colour as ImU32 (no ImGui context needed), with its alpha times `alphaMul`.
inline ImU32 U32(const ImVec4& c, float alphaMul = 1.0f) {
    auto ch = [](float v) { v = v < 0 ? 0 : v > 1 ? 1 : v; return (ImU32)(v * 255.0f + 0.5f); };
    return IM_COL32(ch(c.x), ch(c.y), ch(c.z), ch(c.w * alphaMul));
}
/// A see-through tint over a surface (hover fills, hairlines, soft backgrounds): `alpha` 0..255 as it was written for the dark
/// themes; on Light the tint is dark and a little stronger so it still shows on white.
inline ImU32 Ink(int alpha) {
    const Palette& p = Current();
    const float a = (float)alpha / 255.0f * (p.light ? 1.25f : 1.0f);
    return U32(ImVec4(p.ink.x, p.ink.y, p.ink.z, a > 1 ? 1 : a));
}
inline ImVec4 InkV(float alpha) { const Palette& p = Current(); return ImVec4(p.ink.x, p.ink.y, p.ink.z, alpha * (p.light ? 1.25f : 1.0f)); }
/// A shadow or a dim layer (black in every theme, lighter on Light).
inline ImU32 Shade(int alpha) { return IM_COL32(0, 0, 0, Current().light ? alpha / 2 : alpha); }
inline ImU32 TextU32(float a = 1.0f)    { return U32(Current().text, a); }
inline ImU32 TextDimU32(float a = 1.0f) { return U32(Current().textDim, a); }
inline ImU32 SuccessU32(float a = 1.0f) { return U32(Current().success, a); }
inline ImU32 DangerU32(float a = 1.0f)  { return U32(Current().danger, a); }
inline ImU32 WarningU32(float a = 1.0f) { return U32(Current().warning, a); }
inline ImU32 InfoU32(float a = 1.0f)    { return U32(Current().info, a); }
inline ImU32 OnColorU32(float a = 1.0f) { return U32(Current().onColor, a); }
inline ImU32 SurfaceU32(const ImVec4& surface, float a = 1.0f) { return U32(surface, a); }
/// Filled status colours (a badge, a dot, a button) keep their full colour in every theme: one per status.
inline ImVec4 SuccessFill() { return ImVec4(0.180f, 0.600f, 0.350f, 1); }
inline ImVec4 DangerFill()  { return ImVec4(0.850f, 0.260f, 0.290f, 1); }
inline ImVec4 WarningFill() { return ImVec4(0.900f, 0.650f, 0.150f, 1); }
inline ImU32 SuccessFillU32(float a = 1.0f) { return U32(SuccessFill(), a); }
inline ImU32 DangerFillU32(float a = 1.0f)  { return U32(DangerFill(), a); }
inline ImU32 WarningFillU32(float a = 1.0f) { return U32(WarningFill(), a); }

/// WCAG contrast ratio of two opaque colours (1..21).
inline float Luminance(const ImVec4& c) {
    auto lin = [](float v) { return v <= 0.03928f ? v / 12.92f : (float)std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126f * lin(c.x) + 0.7152f * lin(c.y) + 0.0722f * lin(c.z);
}
inline float Contrast(const ImVec4& a, const ImVec4& b) {
    const float la = Luminance(a), lb = Luminance(b);
    return la > lb ? (la + 0.05f) / (lb + 0.05f) : (lb + 0.05f) / (la + 0.05f);
}

inline ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}
inline ImVec4 WithAlpha(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }
/// Any colour (a section's, a category's) as text on the page: lightened on the dark themes, darkened on Light.
inline ImVec4 ReadableOn(const ImVec4& c) {
    return Current().light ? Mix(ImVec4(c.x, c.y, c.z, 1), ImVec4(0, 0, 0, 1), 0.45f) : Mix(ImVec4(c.x, c.y, c.z, 1), ImVec4(1, 1, 1, 1), 0.5f);
}
/// The softer text on a filled colour (the time, "edited" and the receipt ticks on my chat bubble): the theme's onColor, made as
/// soft (see-through) as it can be while the blend of it over `fill` still has `minContrast` (WCAG) against `fill`; never softer
/// than `softest`. A fill too light for that keeps the full onColor, as readable as the bubble's body text. Owner bug
/// 2026-11-25: the time on my blue bubble used the info blue and could not be read on Light.
inline ImVec4 OnFillSoft(const ImVec4& fill, float minContrast = 4.5f, float softest = 0.72f) {
    const ImVec4 on = Current().onColor, f(fill.x, fill.y, fill.z, 1.0f);
    for (float a = softest; a < 1.0f; a += 0.02f)
        if (Contrast(Mix(f, ImVec4(on.x, on.y, on.z, 1.0f), a), f) >= minContrast) return WithAlpha(on, a);
    return WithAlpha(on, 1.0f);
}
/// The accent as text or a selected label: lighter on the dark themes, darker on Light (so it reads on the page).
inline ImVec4 AccentText(const ImVec4& accent) {
    return Current().light ? Mix(ImVec4(accent.x, accent.y, accent.z, 1), ImVec4(0, 0, 0, 1), 0.35f)
                           : Mix(ImVec4(accent.x, accent.y, accent.z, 1), ImVec4(1, 1, 1, 1), 0.45f);
}


// Rounding + borders from the design (spacing/padding stay per-app; ApplyDensity scales them). Call before ScaleAllSizes.
inline void ApplyDesignMetrics(ImGuiStyle& s, const OkayUI::design::Tokens& t) {
    const OkayUI::design::Shape& k = t.shape;
    s.WindowRounding = k.radiusLarge * 0.75f; s.ChildRounding = k.radiusLarge * 0.75f; s.PopupRounding = k.radiusLarge * 0.75f;
    s.FrameRounding  = k.radius;  s.GrabRounding = k.radius;
    s.TabRounding    = k.tabs == OkayUI::design::TabShape::Pill ? 12.0f : k.tabs == OkayUI::design::TabShape::Segmented ? k.radiusSmall : k.radiusSmall * 0.5f;
    s.ScrollbarRounding = 8.0f;
    s.WindowBorderSize = k.borderPx; s.ChildBorderSize = k.borderPx; s.PopupBorderSize = k.borderPx;
    s.FrameBorderSize  = k.controlBorders ? k.borderPx : 0.0f; s.TabBorderSize = 0.0f;
}
/// The person's density (Compact / Comfortable / Spacious): paddings and gaps times the design's spacing. Call before ScaleAllSizes.
inline void ApplyDensity(ImGuiStyle& s, const OkayUI::design::Tokens& t) {
    const float k = t.spacing;
    if (k == 1.0f) return;
    s.WindowPadding = ImVec2(s.WindowPadding.x * k, s.WindowPadding.y * k); s.FramePadding = ImVec2(s.FramePadding.x * k, s.FramePadding.y * k);
    s.ItemSpacing = ImVec2(s.ItemSpacing.x * k, s.ItemSpacing.y * k); s.ItemInnerSpacing = ImVec2(s.ItemInnerSpacing.x * k, s.ItemInnerSpacing.y * k);
    s.CellPadding = ImVec2(s.CellPadding.x * k, s.CellPadding.y * k); s.IndentSpacing *= k;
}
/// The design's accent as an ImVec4.
inline ImVec4 DesignAccent(const OkayUI::design::Tokens& t) { return V4(OkayUI::design::WithAlpha(t.c.accent, 255)); }

// ImGui's colours from a palette and an accent (every theme; the editor passes Slate).
inline void ApplyColors(ImGuiStyle& s, const Palette& P, const ImVec4& accentIn) {
    const ImVec4 kDeep = P.deep, kSunken = P.sunken, kBase = P.base, kRaised = P.raised, kField = P.field, kControl = P.control;
    const ImVec4 kText = P.text, kTextDim = P.textDim;
    const ImVec4 ink(P.ink.x, P.ink.y, P.ink.z, 1.0f);
    auto I = [&](float a) { return ImVec4(ink.x, ink.y, ink.z, a * (P.light ? 1.6f : 1.0f)); };
    const ImVec4 accent(accentIn.x, accentIn.y, accentIn.z, 1.0f);
    const ImVec4 accentLit = P.light ? Mix(accent, ImVec4(0, 0, 0, 1), 0.18f) : Mix(accent, ImVec4(1, 1, 1, 1), 0.28f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                  = kText;
    c[ImGuiCol_TextDisabled]          = kTextDim;
    c[ImGuiCol_WindowBg]              = kBase;
    c[ImGuiCol_ChildBg]               = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]               = WithAlpha(Mix(kRaised, kBase, 0.35f), 0.985f);
    c[ImGuiCol_Border]                = I(0.075f);
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = kField;
    c[ImGuiCol_FrameBgHovered]        = Mix(kField, ink, 0.05f);
    c[ImGuiCol_FrameBgActive]         = Mix(kField, ink, 0.09f);
    c[ImGuiCol_TitleBg]               = kDeep;
    c[ImGuiCol_TitleBgActive]         = Mix(kDeep, kSunken, 0.6f);
    c[ImGuiCol_TitleBgCollapsed]      = kDeep;
    c[ImGuiCol_MenuBarBg]             = kSunken;
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]         = I(0.12f);
    c[ImGuiCol_ScrollbarGrabHovered]  = I(0.24f);
    c[ImGuiCol_ScrollbarGrabActive]   = accent;
    c[ImGuiCol_CheckMark]             = accentLit;
    c[ImGuiCol_SliderGrab]            = accent;
    c[ImGuiCol_SliderGrabActive]      = accentLit;
    c[ImGuiCol_Button]                = kControl;
    c[ImGuiCol_ButtonHovered]         = Mix(kControl, ink, 0.08f);
    c[ImGuiCol_ButtonActive]          = accent;
    c[ImGuiCol_Header]                = Mix(kControl, accent, 0.18f);
    c[ImGuiCol_HeaderHovered]         = Mix(kControl, ink, 0.07f);
    c[ImGuiCol_HeaderActive]          = Mix(kControl, accent, 0.45f);
    c[ImGuiCol_Separator]             = I(0.08f);
    c[ImGuiCol_SeparatorHovered]      = WithAlpha(accent, 0.70f);
    c[ImGuiCol_SeparatorActive]       = accent;
    c[ImGuiCol_ResizeGrip]            = I(0.06f);
    c[ImGuiCol_ResizeGripHovered]     = WithAlpha(accent, 0.60f);
    c[ImGuiCol_ResizeGripActive]      = accent;
    c[ImGuiCol_InputTextCursor]       = kText;
    c[ImGuiCol_Tab]                   = kSunken;
    c[ImGuiCol_TabHovered]            = Mix(kSunken, kControl, 0.7f);
    c[ImGuiCol_TabSelected]           = kBase;
    c[ImGuiCol_TabSelectedOverline]   = accent;
    c[ImGuiCol_TabDimmed]             = kSunken;
    c[ImGuiCol_TabDimmedSelected]     = Mix(kSunken, kBase, 0.6f);
    c[ImGuiCol_TabDimmedSelectedOverline] = WithAlpha(accent, 0.45f);
    c[ImGuiCol_DockingPreview]        = WithAlpha(accent, 0.55f);
    c[ImGuiCol_DockingEmptyBg]        = kDeep;
    c[ImGuiCol_TableHeaderBg]         = Mix(kSunken, kBase, 0.5f);
    c[ImGuiCol_TableBorderStrong]     = I(0.07f);
    c[ImGuiCol_TableBorderLight]      = I(0.04f);
    c[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]         = I(0.022f);
    c[ImGuiCol_TextLink]              = accentLit;
    c[ImGuiCol_TextSelectedBg]        = WithAlpha(accent, 0.40f);
    c[ImGuiCol_DragDropTarget]        = accentLit;
    c[ImGuiCol_NavCursor]             = accent;
    c[ImGuiCol_NavWindowingHighlight] = I(0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0, 0, 0, P.light ? 0.25f : 0.45f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, P.light ? 0.30f : 0.55f);
}

/// Everything a Dear ImGui host takes from the design in force: the live palette (SetPalette), rounding and borders, the
/// colours with the design's (or the person's) accent, and the hairline the design draws round its controls. The host
/// sets its own paddings first (and ApplyDensity), then calls this, then ScaleAllSizes.
inline Palette ApplyDesign(ImGuiStyle& s, const OkayUI::design::Tokens& t) {
    const Palette P = PaletteFrom(t);
    SetPalette(P);
    ApplyDesignMetrics(s, t);
    ApplyColors(s, P, DesignAccent(t));
    s.Colors[ImGuiCol_Border] = V4(t.c.hairline);
    return P;
}

}  // namespace okaytheme
