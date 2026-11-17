#pragma once
// Internal to okayui/screens: the launcher's Dear ImGui look (sizes, colours, the page header, its flat buttons and line icons)
// shared by the OkayUI launcher pages (OkayUILauncherPages.cpp and OkayUILauncherMorePages.cpp). Written from the launcher's
// own style values (launcher/main.cpp DarkTheme, shared/OkayTheme.hpp) and its icon drawing (DrawNavIcon); no code from any
// other project. Not part of OkayUI's public API.
#include "okay/UI/OkayUI.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string>

namespace UI = OkayUI;

namespace okayuiscreens {
namespace look {

inline constexpr float kPi = 3.14159265f;

inline UI::Color32 Col(const unsigned char c[4], float alpha = 1.0f) {
    return UI::RGBA(c[0], c[1], c[2], (unsigned)(c[3] * std::max(0.0f, std::min(1.0f, alpha)) + 0.5f));
}
inline UI::Color32 Rgb(int r, int g, int b, int a = 255) { return UI::RGBA((unsigned)r, (unsigned)g, (unsigned)b, (unsigned)a); }
// The accent mixed toward white (the launcher's "accent light" for text on accent-tinted fills).
inline UI::Color32 AccentLight() {
    const unsigned char* a = UI::Style().accent;
    return Rgb((int)(a[0] * 0.55f + 115), (int)(a[1] * 0.55f + 115), (int)(a[2] * 0.55f + 115));
}
inline UI::Color32 AccentAlpha(float alpha) { const unsigned char* a = UI::Style().accent; return Rgb(a[0], a[1], a[2], (int)(alpha * 255.0f + 0.5f)); }
inline UI::Vec2 V(float x, float y) { return UI::Vec2{x, y}; }

inline std::string Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// ---- the launcher's Dear ImGui look ----------------------------------------------------------------------------------------
// Sizes from the launcher's style (launcher/main.cpp DarkTheme): frames are padded 10 x 7, items sit 9 x 8 apart, the gap
// inside an item is 7. Positions move down the way the launcher's layout moves them: an item's bottom plus the 8 px gap, cut
// to a whole pixel.
inline constexpr float kFrameX = 10.0f, kFrameY = 7.0f, kSpX = 9.0f, kSpY = 8.0f, kInnerX = 7.0f;
inline float Tr(float v) { return std::floor(v); }
// Text scaled by k, rounded to whole pixels as the launcher's scaled text is (17 px x 1.34 is drawn at 23 px).
inline void PushScale(float k) {
    const float fs = std::max(1.0f, UI::GetTextLineHeight());
    UI::PushTextScale(std::max(1.0f, std::round(fs * k)) / fs);
}
inline float NextY(float itemY, float itemH) { return Tr(itemY + itemH + kSpY); }

// The launcher's Slate colours (shared/OkayTheme.hpp) in bytes.
inline UI::Color32 CRaised() { return Rgb(38, 42, 51); }
inline UI::Color32 CField() { return Rgb(41, 46, 59); }
inline UI::Color32 CControl(float alpha = 1.0f) { return Rgb(51, 56, 71, (int)(255.0f * alpha + 0.5f)); }
inline UI::Color32 CControlHover(float alpha = 1.0f) { return Rgb(67, 72, 86, (int)(255.0f * alpha + 0.5f)); }
inline UI::Color32 CBorder() { return Rgb(255, 255, 255, 19); }
inline UI::Color32 CTitle() { return Rgb(242, 245, 250); }
inline UI::Color32 CProgress() { return Rgb(230, 179, 0); }   // the bar fill the launcher's progress bars use

// For the length of a page: text at the launcher's 17 px (16 px times 17/16; the atlas has that size baked), frames padded as
// the launcher's, and the launcher's tooltip and combo box. Theme values it changes are put back at the end.
class LauncherLook {
public:
    LauncherLook() {
        UI::Theme& t = UI::Style();
        m_tipOffX = t.tooltipOffsetX; m_tipOffY = t.tooltipOffsetY; m_tipPadX = t.tooltipPadX; m_tipPadY = t.tooltipPadY;
        m_tipGap = t.tooltipLineGap; m_tipRound = t.tooltipRounding; std::memcpy(m_tipBg, t.tooltipBg, 4); m_combo = t.comboArrowBox;
        m_ascent = t.ascentRoundsUp; m_snap = t.snapAdvances; m_aa = t.antiAliased; m_cache = t.cacheDrawing;
        // Its tooltips: 25 px right of and 10 px below the mouse, padded as its windows (14 x 12), lines touching, 8 px corners,
        // the popup colour; its combo: a frame-coloured box with an arrow button.
        t.tooltipOffsetX = 25.0f; t.tooltipOffsetY = 10.0f; t.tooltipPadX = 14.0f; t.tooltipPadY = 12.0f;
        t.tooltipLineGap = 0.0f; t.tooltipRounding = 8.0f;
        t.tooltipBg[0] = 35; t.tooltipBg[1] = 39; t.tooltipBg[2] = 48; t.tooltipBg[3] = 251;
        t.comboArrowBox = true;
        t.ascentRoundsUp = true;   // its text sits where the launcher's sits (the ascent rounded up)
        t.snapAdvances = true;     // and steps by whole pixels, as the launcher's font does
        t.antiAliased = true;      // its shapes have smooth (anti-aliased) edges, as the launcher's do
        t.cacheDrawing = true;        // its texts are copied from the frame before when unchanged (the same pixels, less work)
        // Its text fields and combo boxes brighten by 5% towards white under the mouse and 9% while typing (the Slate theme's
        // FrameBgHovered and FrameBgActive).
        auto mix = [](float k) {   // the launcher's field colour (0.16, 0.18, 0.23) mixed towards white by k
            const float f[3] = {0.16f, 0.18f, 0.23f};
            int o[3]; for (int i = 0; i < 3; ++i) o[i] = (int)((f[i] + (1.0f - f[i]) * k) * 255.0f + 0.5f);
            return Rgb(o[0], o[1], o[2]);
        };
        UI::PushStyleColor(UI::Col_FrameBgHovered, mix(0.05f));
        UI::PushStyleColor(UI::Col_FrameBgActive, mix(0.09f));
        // Its popups (the sort list, the right-click menu): Dear ImGui's padding (14 x 12), 8 px corners, a hairline edge, the
        // Slate popup colour, rows 8 px apart, hover and current-item colours (measured from the Dear ImGui page).
        m_pop = t.imguiPopups; t.imguiPopups = true;
        UI::PushStyleColor(UI::Col_PopupBg, Rgb(35, 39, 48, 251));
        UI::PushStyleColor(UI::Col_Border, Rgb(255, 255, 255, 19));
        UI::PushStyleColor(UI::Col_Header, Rgb(53, 68, 95));
        UI::PushStyleColor(UI::Col_HeaderHovered, Rgb(65, 70, 84));
        const float base = UI::GetTextLineHeight();   // 16 px times the UI scale
        UI::PushTextScale(std::max(1.0f, std::round(base * 17.0f / 16.0f)) / std::max(1.0f, base));
        UI::PushStyleVar(UI::StyleVar_FramePadding, V(kFrameX, kFrameY));
    }
    ~LauncherLook() {
        UI::PopStyleColor(6);
        UI::PopStyleVar(1);
        UI::PopTextScale();
        UI::Theme& t = UI::Style();
        t.tooltipOffsetX = m_tipOffX; t.tooltipOffsetY = m_tipOffY; t.tooltipPadX = m_tipPadX; t.tooltipPadY = m_tipPadY;
        t.tooltipLineGap = m_tipGap; t.tooltipRounding = m_tipRound; std::memcpy(t.tooltipBg, m_tipBg, 4); t.comboArrowBox = m_combo;
        t.ascentRoundsUp = m_ascent; t.snapAdvances = m_snap; t.antiAliased = m_aa; t.imguiPopups = m_pop; t.cacheDrawing = m_cache;
    }
    LauncherLook(const LauncherLook&) = delete;
    LauncherLook& operator=(const LauncherLook&) = delete;
private:
    float m_tipOffX, m_tipOffY, m_tipPadX, m_tipPadY, m_tipGap, m_tipRound;
    unsigned char m_tipBg[4];
    bool m_combo, m_ascent, m_snap, m_aa, m_pop, m_cache;
};

// A key for DrawList::Cached: the words that stand for a drawing's look, mixed (FNV-1a over 32-bit words).
inline unsigned long long LookKey(std::initializer_list<unsigned> words) {
    unsigned long long k = 1469598103934665603ull;
    for (unsigned v : words) k = (k ^ v) * 1099511628211ull;
    return k;
}
inline unsigned FloatBits(float f) { unsigned u; std::memcpy(&u, &f, 4); return u; }

// ---- defined in OkayUILauncherPages.cpp ----
bool BeginPlainChild(const char* id, float w, float h, float bar = 8.0f);
void EndPlainChild(float contentBottom);
float PageHeader(const UI::DrawList& dl, float x0, float y0, float w, const char* title, const char* subtitle);
bool FlatButton(const char* id, const char* label, float x, float y, float w, float h, bool enabled = true, bool primary = false,
                float rounding = 6.0f);
void CenteredAt(const UI::DrawList& dl, float x, float y, float width, const char* s, UI::Color32 col);
const UI::Vec2* UnitArc(float a0, float a1, int n);
void ArcTo(const UI::DrawList& dl, UI::Vec2 c, float r, float a0, float a1, int n);
void Ring(const UI::DrawList& dl, UI::Vec2 c, float r, UI::Color32 col, int n, float t);
enum NavIcon { kIcoPlay = 1, kIcoGrid = 2, kIcoTrophy = 5, kIcoFriends = 6, kIcoBag = 7, kIcoDownload = 10, kIcoBell = 11, kIcoChat = 12, kIcoRefresh = 14 };
void OutlineRect(const UI::DrawList& dl, UI::Vec2 mn, UI::Vec2 mx, UI::Color32 col, float rounding, float t);
void DrawNavIcon(const UI::DrawList& d, int id, UI::Vec2 c, UI::Color32 col);
void DrawNavIconNow(const UI::DrawList& d, int id, UI::Vec2 c, UI::Color32 col);   // drawn each time (inside another Cached drawing)
void DrawTrophy(const UI::DrawList& dl, UI::Vec2 c, float s, UI::Color32 col);
void DrawStar(const UI::DrawList& dl, UI::Vec2 c, float r, UI::Color32 col);
void DrawMedal(const UI::DrawList& dl, UI::Vec2 c, float r, const unsigned char rgb[3], float alpha);
void DrawCross(const UI::DrawList& dl, UI::Vec2 c, float r, float arm, bool hover, UI::Color32 col, int baseAlpha, int hoverAlpha, float t);

} // namespace look
} // namespace okayuiscreens
