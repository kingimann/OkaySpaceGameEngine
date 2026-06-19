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
    /// Optional icon image drawn inside the button, left of the label (PNG/JPG;
    /// bundled by Build Game). `iconSize` is its square size in pixels (0 hides
    /// it); the label shifts right to make room.
    std::string icon;
    float iconSize = 0.0f;
    Color color = Color::FromBytes(60, 90, 150);
    Color hoverColor = Color::FromBytes(80, 120, 200);
    Color pressedColor = Color::FromBytes(45, 70, 120);  // shown while held down
    Color disabledColor = Color::FromBytes(70, 70, 78);  // shown when !interactable
    Color textColor = Color::White;
    UIAnchor anchor = UIAnchor::TopLeft;
    // Customization: rounded corners, label size, and an optional border.
    float cornerRadius = 4.0f;
    float fontScale = 2.0f;                        // text pixel size multiplier
    float borderWidth = 0.0f;                      // 0 = no border
    Color borderColor = Color::FromBytes(255, 255, 255, 80);
    /// Grow effect when hovered/focused: 1 = none, e.g. 1.1 = 10% bigger. A
    /// lightweight Unity-style "transition" for tactile menus.
    float hoverScale = 1.0f;
    /// When false the button is greyed out and ignores hover/click (e.g. a
    /// "Continue" entry with no save). Scripts toggle it via set_interactable().
    bool interactable = true;
    /// Whether keyboard/gamepad menu navigation can focus this button.
    bool focusable = true;

    bool IsHovered() const { return m_hover; }
    /// True while the mouse is held down over an interactable button.
    bool IsPressed() const { return m_pressed; }
    /// True only on the frame the button was clicked.
    bool WasClicked() const { return m_clicked; }
    /// Whether menu navigation currently highlights this button.
    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }

    /// Activate as if clicked (keyboard/gamepad). Fires on_click and reports
    /// WasClicked() for this frame.
    void Press() {
        if (!interactable) return;
        m_clicked = true;
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_click");
    }

    /// The color the renderer should use for the current state.
    Color CurrentColor() const {
        if (!interactable)        return disabledColor;
        if (m_pressed)            return pressedColor;
        if (m_hover || m_focused) return hoverColor;
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
    bool m_focused = false;
};

} // namespace okay
