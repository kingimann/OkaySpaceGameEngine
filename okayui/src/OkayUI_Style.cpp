#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include "OkayUIInternal.hpp"
#include "okay/UI/OkayUI.hpp"
#include "okay/UI/OkayUICompat.hpp"
#include "okay/UI/OkayUIInternals.hpp"
#include "okay/UI/OkayUIDesign.hpp"

namespace OkayUI {
using namespace ui_core;


Theme& Style() { return g_theme; }

Theme SlateTheme() {
    design::Options o; std::snprintf(o.design, sizeof o.design, "Slate");
    return design::ThemeFor(design::Resolve(o), true);
}

// TrueType text on (or back to the bitmap font) with the design in force. The theme of the other text mode is kept and comes back,
// so a program (or a test) that changed Style() by hand and turns the TrueType text off again gets its own values back.
void UseLauncherLook(bool on) {
    static Theme other;
    if (on == g_look) return;
    if (on) {
        other = g_theme;
        Theme next = design::ThemeFor(design::CurrentTokens(), true);
        next.caretBlink = g_theme.caretBlink;
        g_theme = next;
        const fontatlas::Atlas& a = fontatlas::Get();
        g_ttf = a.ok ? &a : nullptr;
        if (g_ttf) { g_defTex = kAtlasTex; g_whiteU = a.whiteU; g_whiteV = a.whiteV; }
    } else {
        g_theme = other;
        g_ttf = nullptr; g_defTex = nullptr; g_whiteU = g_whiteV = 0.0f;
    }
    g_look = on;
    g_curTex = g_defTex;
}
bool LauncherLook() { return g_look; }

void PushStyleColor(Col which, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    if (g_colTop >= 64) return;
    const int id = (int)which;
    ColorSave& s = g_colStack[g_colTop];
    if (id >= 8 && id < Col_COUNT) {
        unsigned char* f = g_theme.colors[id];
        s.field = f; s.ext = id; s.prevSet = colorIsSet(id);
        s.prev[0] = f[0]; s.prev[1] = f[1]; s.prev[2] = f[2]; s.prev[3] = f[3];
        f[0] = r; f[1] = g; f[2] = b; f[3] = a;
        g_theme.colorSet |= 1ull << id;
    } else {
        unsigned char* f = themeColor(id >= 0 && id < 8 ? id : 4);
        s.field = f; s.ext = -1; s.prevSet = false;
        s.prev[0] = f[0]; s.prev[1] = f[1]; s.prev[2] = f[2]; s.prev[3] = f[3];
        f[0] = r; f[1] = g; f[2] = b; f[3] = a;
    }
    ++g_colTop;
}
void PushStyleColor(Col which, Color32 c) { PushStyleColor(which, (unsigned char)(c & 255u), (unsigned char)((c >> 8) & 255u), (unsigned char)((c >> 16) & 255u), (unsigned char)((c >> 24) & 255u)); }
void PushStyleColor(Col which, Vec4 v) {
    auto b = [](float f) { return (unsigned char)(clamp01(f) * 255.0f + 0.5f); };
    PushStyleColor(which, b(v.x), b(v.y), b(v.z), b(v.w));
}

void PopStyleColor(int count) {
    while (count-- > 0 && g_colTop > 0) {
        ColorSave& s = g_colStack[--g_colTop];
        s.field[0] = s.prev[0]; s.field[1] = s.prev[1]; s.field[2] = s.prev[2]; s.field[3] = s.prev[3];
        if (s.ext >= 0) { if (s.prevSet) g_theme.colorSet |= 1ull << s.ext; else g_theme.colorSet &= ~(1ull << s.ext); }
    }
}
void SetStyleColor(Col which, unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    const int id = (int)which;
    unsigned char* f = (id >= 8 && id < Col_COUNT) ? g_theme.colors[id] : (id >= 0 && id < 8 ? themeColor(id) : nullptr);
    if (!f) return;
    f[0] = r; f[1] = g; f[2] = b; f[3] = a;
    if (id >= 8) g_theme.colorSet |= 1ull << id;
}
void ResetStyleColor(Col which) { const int id = (int)which; if (id >= 8 && id < Col_COUNT) g_theme.colorSet &= ~(1ull << id); }
ImVec4 GetStyleColorVec4(Col which) {
    unsigned char tmp[4];
    const unsigned char* c = styleColor((int)which, tmp);
    return Vec4{c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f};
}
const char* GetStyleColorName(Col which) {
    static const char* const names[] = {
        "Col_Bg", "Col_BgHover", "Col_BgDown", "Col_Border", "Col_Text", "Col_Panel", "Col_Track", "Col_Accent",
        "Col_TextDisabled", "Col_WindowBg", "Col_ChildBg", "Col_PopupBg", "Col_BorderShadow", "Col_FrameBg", "Col_FrameBgHovered", "Col_FrameBgActive",
        "Col_TitleBg", "Col_TitleBgActive", "Col_TitleBgCollapsed", "Col_MenuBarBg", "Col_ScrollbarBg", "Col_ScrollbarGrab", "Col_ScrollbarGrabHovered",
        "Col_ScrollbarGrabActive", "Col_CheckMark", "Col_SliderGrab", "Col_SliderGrabActive", "Col_Header", "Col_HeaderHovered", "Col_HeaderActive",
        "Col_Separator", "Col_SeparatorHovered", "Col_SeparatorActive", "Col_ResizeGrip", "Col_ResizeGripHovered", "Col_ResizeGripActive",
        "Col_Tab", "Col_TabHovered", "Col_TabActive", "Col_TabUnfocused", "Col_TabUnfocusedActive", "Col_DockingPreview", "Col_DockingEmptyBg",
        "Col_PlotLines", "Col_PlotLinesHovered", "Col_PlotHistogram", "Col_PlotHistogramHovered", "Col_TableHeaderBg", "Col_TableBorderStrong",
        "Col_TableBorderLight", "Col_TableRowBg", "Col_TableRowBgAlt", "Col_TextSelectedBg", "Col_DragDropTarget", "Col_NavHighlight",
        "Col_NavWindowingHighlight", "Col_NavWindowingDimBg", "Col_ModalWindowDimBg"};
    static_assert(sizeof names / sizeof names[0] == Col_COUNT, "one name per colour");
    return (int)which >= 0 && (int)which < Col_COUNT ? names[(int)which] : "";
}
Vec4 ColorConvertU32ToFloat4(Color32 c) { return Vec4{(c & 255u) / 255.0f, ((c >> 8) & 255u) / 255.0f, ((c >> 16) & 255u) / 255.0f, ((c >> 24) & 255u) / 255.0f}; }
Color32 ColorConvertFloat4ToU32(Vec4 v) {
    auto b = [](float f) { return (unsigned)(clamp01(f) * 255.0f + 0.5f); };
    return RGBA(b(v.x), b(v.y), b(v.z), b(v.w));
}
void ColorConvertRGBtoHSV(float r, float g, float b, float* h, float* s, float* v) {
    r = clamp01(r); g = clamp01(g); b = clamp01(b);
    const float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b)), d = mx - mn;
    float hh = 0.0f;
    if (d > 1e-9f) {
        if (mx == r) hh = (g - b) / d + (g < b ? 6.0f : 0.0f);
        else if (mx == g) hh = (b - r) / d + 2.0f;
        else hh = (r - g) / d + 4.0f;
        hh /= 6.0f;
    }
    if (h) *h = hh;
    if (s) *s = mx > 1e-9f ? d / mx : 0.0f;
    if (v) *v = mx;
}
void ColorConvertHSVtoRGB(float h, float s, float v, float* r, float* g, float* b) {
    if (!std::isfinite(h)) h = 0.0f;
    h = h - std::floor(h); s = clamp01(s); v = clamp01(v);
    const float hh = h * 6.0f;
    const int i = (int)hh % 6;
    const float f = hh - std::floor(hh), p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
    float R = v, G = t, B = p;
    switch (i) { case 0: R = v; G = t; B = p; break; case 1: R = q; G = v; B = p; break; case 2: R = p; G = v; B = t; break;
                 case 3: R = p; G = q; B = v; break; case 4: R = t; G = p; B = v; break; default: R = v; G = p; B = q; break; }
    if (r) *r = R;
    if (g) *g = G;
    if (b) *b = B;
}


} // namespace OkayUI
