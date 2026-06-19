#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Input/Input.hpp"
#include <string>

namespace okay {

/// A screen-space checkbox: a square box (toggled fill) plus a text label to its
/// right (mute, fullscreen, "show hints"…). Clicking flips `on` and calls the
/// sibling ScriptComponent's on_toggle() handler. Position/size are window
/// pixels (origin top-left); `size` is the box, the label extends past it.
class UIToggle : public Behaviour {
public:
    Vec2  position{20.0f, 20.0f};
    Vec2  size{28.0f, 28.0f};
    bool  on = false;
    std::string label = "Toggle";
    Color boxColor   = Color::FromBytes(40, 40, 50);
    Color checkColor = Color::FromBytes(90, 200, 110);
    Color textColor  = Color::White;
    UIAnchor anchor = UIAnchor::TopLeft;
    float cornerRadius = 3.0f;                        // rounded box corners
    /// Checkbox (inset tick) or a sliding on/off Switch (pill + knob).
    enum class Style { Checkbox, Switch };
    Style style = Style::Checkbox;
    Color knobColor = Color::FromBytes(235, 235, 240);  // switch knob

    bool IsHovered() const { return m_hover; }

    // Keyboard/gamepad menu focus (driven by NavigateUI).
    bool focusable = true;
    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }
    /// Flip the toggle and fire on_toggle — used by activation (Enter/A).
    void Toggle() {
        on = !on;
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_toggle");
    }

    bool Contains(const Vec2& p) const {
        Vec2 o = ResolveAnchor(anchor, position, size);
        return p.x >= o.x && p.y >= o.y &&
               p.x <= o.x + size.x && p.y <= o.y + size.y;
    }

    bool interactable = true;   // when false: greyed out, ignores clicks

    void Update(float) override {
        if (!interactable) { m_hover = false; return; }
        Vec2 m = Input::MousePosition();
        m_hover = Contains(m);
        if (m_hover && Input::GetMouseButtonDown(0)) Toggle();
    }

private:
    bool m_hover = false;
    bool m_focused = false;
};

} // namespace okay
