#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Input/Input.hpp"

namespace okay {

/// A numeric stepper: a [-] button, the current value, and a [+] button, for
/// picking a quantity (lives, difficulty, count…). Clicking an end button nudges
/// `value` by `step`, clamped to [minValue, maxValue], and fires the sibling
/// ScriptComponent's on_change() handler. Position/size are window pixels.
class UIStepper : public Behaviour {
public:
    Vec2  position{20.0f, 20.0f};
    Vec2  size{160.0f, 32.0f};
    float value = 0.0f;
    float minValue = 0.0f;
    float maxValue = 10.0f;
    float step = 1.0f;
    bool  wholeNumbers = true;          // round to integers
    bool  wrap = false;                 // wrap past the ends instead of clamping
    Color background = Color::FromBytes(40, 40, 50);
    Color button     = Color::FromBytes(90, 140, 220);
    Color textColor  = Color::White;
    UIAnchor anchor = UIAnchor::TopLeft;
    UIShape shape = UIShape::Rounded;
    float cornerRadius = 5.0f;

    enum class Part { None, Minus, Plus };

    bool interactable = true;
    bool focusable = true;
    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }

    /// Width of each end button (a square based on the height).
    float ButtonWidth() const { return Mathf::Min(size.y, size.x * 0.4f); }

    /// Which part the local-space (anchored) point hits.
    Part PartAt(const Vec2& p) const {
        Vec2 o = ResolveAnchor(anchor, position, size);
        if (p.x < o.x || p.y < o.y || p.x > o.x + size.x || p.y > o.y + size.y) return Part::None;
        float bw = ButtonWidth();
        if (p.x <= o.x + bw)            return Part::Minus;
        if (p.x >= o.x + size.x - bw)   return Part::Plus;
        return Part::None;
    }

    bool Contains(const Vec2& p) const {
        Vec2 o = ResolveAnchor(anchor, position, size);
        return p.x >= o.x && p.y >= o.y && p.x <= o.x + size.x && p.y <= o.y + size.y;
    }

    void SetValue(float v) {
        float nv = v;
        float span = maxValue - minValue;
        if (wrap && span > 0.0f) {
            // Stepping past an end wraps to the other end (Easy<->Hard style).
            if (nv > maxValue)      nv = minValue;
            else if (nv < minValue) nv = maxValue;
        } else {
            nv = Mathf::Clamp(nv, minValue, maxValue);
        }
        if (wholeNumbers) nv = Mathf::Round(nv);
        if (nv == value) return;
        value = nv;
        Fire();
    }
    void Step(float dir) { SetValue(value + dir * step); }

    void Update(float) override {
        if (!interactable) return;
        if (Input::GetMouseButtonDown(0)) {
            Part part = PartAt(Input::MousePosition());
            if (part == Part::Minus) Step(-1.0f);
            else if (part == Part::Plus) Step(1.0f);
        }
    }

private:
    void Fire() {
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_change");
    }
    bool m_focused = false;
};

} // namespace okay
