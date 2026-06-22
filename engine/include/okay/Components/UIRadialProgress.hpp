#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

/// A circular / ring progress indicator (cooldowns, loaders, skill timers, score
/// dials) — the radial counterpart to UIProgressBar. Draws a ring of `thickness`
/// pixels (or a full pie when thickness <= 0) filled clockwise from `startAngle`
/// by `value` (0..1). Position/size are window pixels; `size` is the bounding box.
class UIRadialProgress : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{96.0f, 96.0f};
    float value = 0.6f;                              // 0..1 filled fraction
    float thickness = 12.0f;                         // ring width in px (<=0 = pie)
    float startAngle = 0.0f;                         // 0 = 12 o'clock; degrees
    bool  clockwise = true;
    Color background = Color::FromBytes(40, 44, 56);
    Color fill = Color::FromBytes(90, 200, 160);
    UIAnchor anchor = UIAnchor::TopLeft;
    bool  showPercent = false;
    Color textColor = Color::White;

    float Fraction() const { return Mathf::Clamp01(value); }
    void SetValue(float v) { value = Mathf::Clamp01(v); }

    /// Classify a local pixel (px,py) within the w x h box: 0 = outside the ring,
    /// 1 = ring background (unfilled), 2 = ring fill. Shared by the renderer (and
    /// unit tests) so the visible arc is well-defined.
    enum Region { Outside = 0, Track = 1, Fill = 2 };
    static int Sample(float w, float h, float thickness, float startAngle, bool clockwise,
                      float value, float px, float py) {
        const float cx = w * 0.5f, cy = h * 0.5f;
        const float dx = px - cx, dy = py - cy;
        const float outerR = std::min(w, h) * 0.5f;
        const float innerR = thickness > 0.0f ? std::max(0.0f, outerR - thickness) : 0.0f;
        const float r = std::sqrt(dx * dx + dy * dy);
        if (r > outerR || r < innerR) return Outside;
        // Angle measured clockwise from the top (12 o'clock), then offset by start.
        float a = std::atan2(dx, -dy) * Mathf::Rad2Deg;   // 0 at top, +clockwise
        if (!clockwise) a = -a;
        a -= startAngle;
        a = std::fmod(a, 360.0f); if (a < 0.0f) a += 360.0f;
        return (a <= Mathf::Clamp01(value) * 360.0f) ? Fill : Track;
    }
};

} // namespace okay
