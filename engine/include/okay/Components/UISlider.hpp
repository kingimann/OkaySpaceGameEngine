#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Input/Input.hpp"

namespace okay {

/// A screen-space horizontal slider the player drags to pick a value in
/// [minValue, maxValue] (volume, sensitivity, difficulty…). Position/size are
/// window pixels (origin top-left). While dragging it updates `value` and calls
/// the sibling ScriptComponent's on_change() handler each frame the value moves.
class UISlider : public Behaviour {
public:
    Vec2  position{20.0f, 20.0f};
    Vec2  size{200.0f, 24.0f};
    float value = 0.5f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    Color background = Color::FromBytes(40, 40, 50);
    Color fill       = Color::FromBytes(90, 140, 220);
    Color knob       = Color::FromBytes(230, 230, 240);
    UIAnchor anchor = UIAnchor::TopLeft;
    // Customization: rounded track, knob width (as a fraction of height), and an
    // optional value readout drawn to the right of the track.
    float cornerRadius = 3.0f;
    float knobSize = 0.6f;                           // knob width = size.y * this
    bool  showValue = false;
    Color textColor = Color::White;

    /// 0..1 position of the handle along the track (for rendering).
    float Fraction() const {
        float span = maxValue - minValue;
        return span > 0.0f ? Mathf::Clamp01((value - minValue) / span) : 0.0f;
    }
    void SetValue(float v) { value = Mathf::Clamp(v, minValue, maxValue); }
    bool IsDragging() const { return m_dragging; }

    bool Contains(const Vec2& p) const {
        Vec2 o = ResolveAnchor(anchor, position, size);
        return p.x >= o.x && p.y >= o.y &&
               p.x <= o.x + size.x && p.y <= o.y + size.y;
    }

    void Update(float) override {
        Vec2 m = Input::MousePosition();
        if (Input::GetMouseButtonDown(0) && Contains(m)) m_dragging = true;
        if (!Input::GetMouseButton(0)) m_dragging = false;
        if (m_dragging && size.x > 0.0f) {
            Vec2 o = ResolveAnchor(anchor, position, size);
            float frac = Mathf::Clamp01((m.x - o.x) / size.x);
            float nv = minValue + frac * (maxValue - minValue);
            if (nv != value) {
                value = nv;
                if (gameObject)
                    if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                        if (sc->VM()) sc->VM()->CallEvent("on_change");
            }
        }
    }

private:
    bool m_dragging = false;
};

} // namespace okay
