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

    bool IsHovered() const { return m_hover; }

    bool Contains(const Vec2& p) const {
        Vec2 o = ResolveAnchor(anchor, position, size);
        return p.x >= o.x && p.y >= o.y &&
               p.x <= o.x + size.x && p.y <= o.y + size.y;
    }

    void Update(float) override {
        Vec2 m = Input::MousePosition();
        m_hover = Contains(m);
        if (m_hover && Input::GetMouseButtonDown(0)) {
            on = !on;
            if (gameObject)
                if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                    if (sc->VM()) sc->VM()->CallEvent("on_toggle");
        }
    }

private:
    bool m_hover = false;
};

} // namespace okay
