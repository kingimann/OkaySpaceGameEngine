#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Input/Input.hpp"

namespace okay {

/// A scrollable UI region (Unity's Scroll View). Its child UI widgets are
/// offset by `scroll` and clipped to the viewport rect, with a scrollbar on the
/// right. Put widgets as children of the scroll view's GameObject and set
/// `contentHeight` to the total height of that content; the mouse wheel (or a
/// drag of the scrollbar) moves through it.
class UIScrollView : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};      // viewport top-left (pixels, from anchor)
    Vec2 size{300.0f, 200.0f};        // viewport size
    UIAnchor anchor = UIAnchor::TopLeft;
    Color background = Color::FromBytes(24, 28, 38, 220);
    Color barColor   = Color::FromBytes(90, 100, 120, 255);
    float contentHeight = 400.0f;     // total height of the scrollable content
    float scroll = 0.0f;              // current vertical scroll offset (pixels)

    float ScrollMax() const { return Mathf::Max(0.0f, contentHeight - size.y); }
    void  ScrollBy(float delta) { scroll = Mathf::Clamp(scroll + delta, 0.0f, ScrollMax()); }
    void  SetScroll(float v)    { scroll = Mathf::Clamp(v, 0.0f, ScrollMax()); }
    /// 0..1 fraction scrolled (for a scrollbar thumb).
    float Fraction() const { float m = ScrollMax(); return m > 0.0f ? scroll / m : 0.0f; }
    /// Pixels scrolled per wheel notch.
    float wheelSpeed = 30.0f;

    bool Contains(const Vec2& p) const {
        Vec2 o = ResolveAnchor(anchor, position, size);
        return p.x >= o.x && p.y >= o.y && p.x <= o.x + size.x && p.y <= o.y + size.y;
    }

    // Wheel-scroll when the pointer is over the viewport (built game + play mode).
    void Update(float) override {
        float wheel = Input::MouseWheel();
        if (wheel != 0.0f && Contains(Input::MousePosition())) ScrollBy(-wheel * wheelSpeed);
    }
};

} // namespace okay
