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

/// A screen-space clickable button (the core of in-game UI: menus, HUD buttons).
/// Position and size are in window pixels (origin top-left). When clicked it
/// calls the sibling ScriptComponent's on_click() handler, and exposes hover /
/// clicked state for the renderer and gameplay code.
class UIButton : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{160.0f, 48.0f};
    std::string label = "Button";
    Color color = Color::FromBytes(60, 90, 150);
    Color hoverColor = Color::FromBytes(80, 120, 200);
    Color textColor = Color::White;
    UIAnchor anchor = UIAnchor::TopLeft;

    bool IsHovered() const { return m_hover; }
    /// True only on the frame the button was clicked.
    bool WasClicked() const { return m_clicked; }

    bool Contains(const Vec2& p) const {
        Vec2 o = ResolveAnchor(anchor, position, size);
        return p.x >= o.x && p.y >= o.y &&
               p.x <= o.x + size.x && p.y <= o.y + size.y;
    }

    void Update(float) override {
        Vec2 m = Input::MousePosition();
        m_hover = Contains(m);
        m_clicked = m_hover && Input::GetMouseButtonDown(0);
        if (m_clicked && gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_click");
    }

private:
    bool m_hover = false;
    bool m_clicked = false;
};

} // namespace okay
