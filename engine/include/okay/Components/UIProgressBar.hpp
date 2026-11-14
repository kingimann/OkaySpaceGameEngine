#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Mathf.hpp"
#include <cstdio>
#include <string>

namespace okay {

/// A screen-space bar that fills left-to-right by `value` (0..1) — health,
/// mana, loading, XP. Position/size are window pixels (origin top-left).
class UIProgressBar final : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{200.0f, 24.0f};
    float value = 1.0f;                              // clamped to [0, 1]
    Color background = Color::FromBytes(41, 46, 59);
    Color fill = Color::FromBytes(66, 150, 250);
    UIAnchor anchor = UIAnchor::TopLeft;
    // Silhouette of the track/fill (Pill gives the clean capsule health-bar look).
    UIShape shape = UIShape::Rounded;
    // Customization: rounded ends and an optional centered percent readout.
    float cornerRadius = 3.0f;
    bool  showPercent = false;
    Color textColor = Color::White;
    // Optional gradient along the fill (e.g. green->yellow): `fill` is the start,
    // `fillEnd` the far end.
    bool  gradientFill = false;
    Color fillEnd = Color::FromBytes(210, 200, 90);
    /// Which way the fill grows from empty to full.
    enum class FillDir { LeftRight, RightLeft, BottomTop, TopBottom };
    FillDir fillDir = FillDir::LeftRight;
    // grok/ui-options: extra controls. 0 segments / 0 trail / empty format keep the old draw.
    int   segments = 0;                              // 0 or 1 = one solid fill; >=2 = chunks
    float segmentGap = 3.0f;
    float trailDelay = 0.0f;                         // seconds for a damage trail to catch a drop; 0 = off
    Color trailColor = Color::FromBytes(220, 90, 80);
    bool  colorByValue = false;                      // lerp lowColor -> highColor by the fill
    Color lowColor = Color::FromBytes(226, 82, 82);
    Color highColor = Color::FromBytes(72, 196, 128);
    std::string textFormat;                          // empty = "%d%%"; "{n}" is the percent, "{v}" is 0..1

    void SetValue(float v) { value = Mathf::Clamp01(v); }
    void SetSegments(int n) { segments = n < 0 ? 0 : n; }
    void SetTrail(float seconds) { trailDelay = seconds < 0.0f ? 0.0f : seconds; }
    void SetTextFormat(const std::string& f) { textFormat = f; }
    float Fraction() const { return Mathf::Clamp01(value); }
    /// Where the delayed trail sits (equals the fill when the trail is off).
    float TrailFraction() const {
        if (trailDelay <= 0.0f) return Fraction();
        return Mathf::Clamp01(m_trail < 0.0f ? value : m_trail);
    }
    Color ValueColor() const {
        float f = Fraction();
        return Color(lowColor.r + (highColor.r - lowColor.r) * f,
                     lowColor.g + (highColor.g - lowColor.g) * f,
                     lowColor.b + (highColor.b - lowColor.b) * f,
                     lowColor.a + (highColor.a - lowColor.a) * f);
    }
    /// Readout text. "{n}" is the percent, "{v}" is the 0..1 fill. Not a printf format.
    std::string FormatText() const {
        int pct = (int)(Fraction() * 100.0f + 0.5f);
        char nbuf[16], vbuf[16];
        std::snprintf(nbuf, sizeof(nbuf), "%d", pct);
        std::snprintf(vbuf, sizeof(vbuf), "%.2f", Fraction());
        std::string out;
        for (std::size_t i = 0; i < textFormat.size(); ++i) {
            if (textFormat[i] == '{' && i + 2 < textFormat.size() && textFormat[i + 2] == '}') {
                if (textFormat[i + 1] == 'n') { out += nbuf; i += 2; continue; }
                if (textFormat[i + 1] == 'v') { out += vbuf; i += 2; continue; }
            }
            out += textFormat[i];
        }
        return out;
    }

    /// The filled sub-rectangle (origin + size, in local pixels) for `frac` (0..1).
    void FillRectAt(float frac, float w, float h, float& ox, float& oy, float& fw, float& fh) const {
        float f = Mathf::Clamp01(frac);
        ox = 0; oy = 0; fw = w; fh = h;
        switch (fillDir) {
            case FillDir::LeftRight: fw = w * f; break;
            case FillDir::RightLeft: fw = w * f; ox = w - fw; break;
            case FillDir::BottomTop: fh = h * f; oy = h - fh; break;
            case FillDir::TopBottom: fh = h * f; break;
        }
    }
    /// The filled sub-rectangle for the current value and fill direction.
    void FillRect(float w, float h, float& ox, float& oy, float& fw, float& fh) const {
        FillRectAt(Fraction(), w, h, ox, oy, fw, fh);
    }

    void Update(float dt) override {
        value = Mathf::Clamp01(value);
        if (m_trail < 0.0f || trailDelay <= 0.0f) { m_trail = value; return; }
        if (value >= m_trail) { m_trail = value; return; }   // healing snaps; damage eases down
        float k = dt / trailDelay;
        if (k > 1.0f) k = 1.0f;
        m_trail += (value - m_trail) * k;
    }

private:
    float m_trail = -1.0f;
};

} // namespace okay
