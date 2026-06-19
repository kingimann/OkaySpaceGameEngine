#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
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
    // Customization: rounded ends and an optional centered percent readout.
    float cornerRadius = 3.0f;
    bool  showPercent = false;
    Color textColor = Color::White;

    void SetValue(float v) { value = Mathf::Clamp01(v); }
    float Fraction() const { return Mathf::Clamp01(value); }
};

} // namespace okay
