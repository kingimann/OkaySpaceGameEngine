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
    Color pressedColor = Color::FromBytes(45, 70, 120);  // shown while held down
    Color disabledColor = Color::FromBytes(70, 70, 78);  // shown when !interactable
    Color textColor = Color::White;
    UIAnchor anchor = UIAnchor::TopLeft;
    /// When false the button is greyed out and ignores hover/click (e.g. a
    /// "Continue" entry with no save). Scripts toggle it via set_interactable().
    bool interactable = true;

    bool IsHovered() const { return m_hover; }
    /// True while the mouse is held down over an interactable button.
    bool IsPressed() const { return m_pressed; }
    /// True only on the frame the button was clicked.
    bool WasClicked() const { return m_clicked; }

    /// The color the renderer should use for the current state.
    Color CurrentColor() const {
        if (!interactable) return disabledColor;
        if (m_pressed)     return pressedColor;
        if (m_hover)       return hoverColor;
        return color;
    }

    bool Contains(const Vec2& p) const {
        Vec2 o = ResolveAnchor(anchor, position, size);
        return p.x >= o.x && p.y >= o.y &&
               p.x <= o.x + size.x && p.y <= o.y + size.y;
    }

    void Update(float) override {
        if (!interactable) { m_hover = m_pressed = m_clicked = false; return; }
        Vec2 m = Input::MousePosition();
        m_hover = Contains(m);
        m_pressed = m_hover && Input::GetMouseButton(0);
        m_clicked = m_hover && Input::GetMouseButtonDown(0);
        if (m_clicked && gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_click");
    }

private:
    bool m_hover = false;
    bool m_pressed = false;
    bool m_clicked = false;
};

} // namespace okay
