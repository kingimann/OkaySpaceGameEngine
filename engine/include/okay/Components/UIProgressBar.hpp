#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Mathf.hpp"

namespace okay {

/// A screen-space bar that fills left-to-right by `value` (0..1) — health,
/// mana, loading, XP. Position/size are window pixels (origin top-left).
class UIProgressBar : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{200.0f, 24.0f};
    float value = 1.0f;                              // clamped to [0, 1]
    Color background = Color::FromBytes(40, 40, 50);
    Color fill = Color::FromBytes(90, 200, 110);
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

    void SetValue(float v) { value = Mathf::Clamp01(v); }
    float Fraction() const { return Mathf::Clamp01(value); }

    /// The filled sub-rectangle (origin + size, in local pixels) for the current
    /// value and fill direction, given the bar's width/height.
    void FillRect(float w, float h, float& ox, float& oy, float& fw, float& fh) const {
        float f = Fraction();
        ox = 0; oy = 0; fw = w; fh = h;
        switch (fillDir) {
            case FillDir::LeftRight: fw = w * f; break;
            case FillDir::RightLeft: fw = w * f; ox = w - fw; break;
            case FillDir::BottomTop: fh = h * f; oy = h - fh; break;
            case FillDir::TopBottom: fh = h * f; break;
        }
    }
};

} // namespace okay
