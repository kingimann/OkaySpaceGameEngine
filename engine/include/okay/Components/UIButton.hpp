#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/NativeUIActions.hpp"
#include "okay/Components/Consumables.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Components/UIWorldContext.hpp"
#include "okay/Components/WorldSpaceUI.hpp"
#include "okay/Components/Canvas.hpp"
#include "okay/Scene/Transform.hpp"
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
    // Assignable click action (Unity-style OnClick): when clicked, call a named
    // public script function. `clickTarget` is the object whose script to call
    // (blank = this button's own object); `clickFunction` is the function name
    // (blank = none). This runs in addition to a sibling script's on_click().
    std::string clickTarget;
    std::string clickFunction;
    // Amount passed to a native On Click method (e.g. Eat 25, Drink 30). Native
    // verbs take this; script CallEvent ignores it (it takes no args).
    float clickArg = 0.0f;
    Color color = Color::FromBytes(60, 90, 150);
    Color hoverColor = Color::FromBytes(80, 120, 200);
    Color pressedColor = Color::FromBytes(45, 70, 120);  // shown while held down
    Color disabledColor = Color::FromBytes(70, 70, 78);  // shown when !interactable
    Color textColor = Color::White;
    UIAnchor anchor = UIAnchor::TopLeft;
    // Silhouette: rounded (default), hard rectangle, circle (icon button) or pill.
    UIShape shape = UIShape::Rounded;
    // Customization: rounded corners, label size, and an optional border.
    float cornerRadius = 4.0f;
    float fontScale = 2.0f;                        // text pixel size multiplier
    float borderWidth = 0.0f;                      // 0 = no border
    Color borderColor = Color::FromBytes(255, 255, 255, 80);
    // Optional drop shadow (lifts the button off the background — clean-UI style).
    bool  shadow = false;
    Color shadowColor = Color::FromBytes(0, 0, 0, 110);
    Vec2  shadowOffset{0.0f, 3.0f};
    float shadowSoftness = 0.0f;   // 0 = crisp; higher = a soft blurred shadow
    /// Grow effect when hovered/focused: 1 = none, e.g. 1.1 = 10% bigger. A
    /// lightweight Unity-style "transition" for tactile menus.
    float hoverScale = 1.0f;
    /// When false the button is greyed out and ignores hover/click (e.g. a
    /// "Continue" entry with no save). Scripts toggle it via set_interactable().
    bool interactable = true;
    /// Whether keyboard/gamepad menu navigation can focus this button.
    bool focusable = true;
    /// Text color while hovered/focused (defaults to the normal text color).
    Color hoverTextColor = Color::White;
    /// Color-transition speed (units/sec) for hover/press; 0 = instant (Unity's
    /// ColorTween). The renderer should use DisplayColor().
    float transitionSpeed = 0.0f;
    /// Toggle button: a click flips `isOn` and the button stays in its pressed
    /// color while on (radio/check style). on_click() still fires each click.
    bool toggleMode = false;
    bool isOn = false;
    /// Pixels the label/icon shift down while pressed (tactile "push"). 0 = none.
    float pressOffset = 1.0f;
    /// Draw the icon on the right of the label instead of the left.
    bool iconRight = false;
    /// Auto-repeat on_click() while held (steppers, +/- spinners). After an
    /// initial `repeatDelay`, fires every `repeatInterval` seconds.
    bool  holdRepeat = false;
    float repeatDelay = 0.4f;
    float repeatInterval = 0.12f;

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
        if (toggleMode) isOn = !isOn;
        m_clicked = true;
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_click");
    }

    bool IsOn() const { return isOn; }

    /// The target color for the current state.
    Color CurrentColor() const {
        if (!interactable)            return disabledColor;
        if (m_pressed || (toggleMode && isOn)) return pressedColor;
        if (m_hover || m_focused)     return hoverColor;
        return color;
    }
    /// The color the renderer should draw: smoothly eased toward CurrentColor()
    /// when transitionSpeed > 0, else the state color directly.
    Color DisplayColor() const { return transitionSpeed > 0.0f ? m_display : CurrentColor(); }
    /// Text color for the current state (hover/focus uses hoverTextColor).
    Color CurrentTextColor() const { return (m_hover || m_focused) ? hoverTextColor : textColor; }
    /// Downward push for the label/icon while pressed (or toggled on), in px.
    float PressShift() const { return (m_pressed || (toggleMode && isOn)) ? pressOffset : 0.0f; }

    /// True when this button is part of in-world (3D) UI — its own object or an
    /// ancestor carries WorldSpaceUI, or it lives under a world-space Canvas. Only
    /// then do we hit-test through the camera projector; ordinary screen-space
    /// buttons keep their exact silhouette test.
    bool IsWorldSpaceUI() const {
        for (Transform* t = gameObject ? gameObject->transform : nullptr; t; t = t->Parent()) {
            if (!t->gameObject) continue;
            if (t->gameObject->GetComponent<WorldSpaceUI>()) return true;
            if (auto* cv = t->gameObject->GetComponent<Canvas>()) if (cv->worldSpace) return true;
        }
        return false;
    }

    bool Contains(const Vec2& p) const {
        // World-space button (own 3D object / world canvas): hit-test where it
        // actually projects on screen. The renderer supplies rectOf() so this
        // header needn't pull in GetUIScreenRect. The projected silhouette is a
        // quad, so an axis-aligned rect test is the right approximation here.
        if (UIWorld().active && UIWorld().rectOf && gameObject && IsWorldSpaceUI()) {
            Vec2 o, sz;
            if (!UIWorld().rectOf(gameObject, o, sz)) return false;  // behind camera
            return p.x >= o.x && p.x <= o.x + sz.x && p.y >= o.y && p.y <= o.y + sz.y;
        }
        Vec2 o = ResolveAnchor(anchor, position, size);
        // Hit-test the actual silhouette so corners of a rounded/circle/pill button
        // aren't clickable where there's no pixel.
        return UIShapeContains(shape, size.x, size.y, cornerRadius, p.x - o.x, p.y - o.y);
    }

    void Update(float dt) override {
        if (!interactable) { m_hover = m_pressed = m_clicked = false; EaseColor(dt); return; }
        Vec2 m = Input::MousePosition();
        m_hover = Contains(m);
        m_pressed = m_hover && Input::GetMouseButton(0);
        m_clicked = false;
        if (m_hover && Input::GetMouseButtonDown(0)) {       // fresh click
            if (toggleMode) isOn = !isOn;
            m_clicked = true;
            m_repeat = repeatDelay;
            Fire();
        } else if (holdRepeat && m_pressed) {                // auto-repeat while held
            m_repeat -= dt;
            if (m_repeat <= 0.0f) { m_repeat = repeatInterval; m_clicked = true; Fire(); }
        }
        EaseColor(dt);
    }

private:
    void Fire() {
        // The sibling script's on_click() (back-compat / code-first buttons).
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_click");
        // The inspector-assigned action: a named function on a chosen object. Native
        // gameplay verbs (Eat/Drink/Damage/…) dispatch directly; otherwise fall back
        // to a script's public function so custom handlers still work.
        if (!clickFunction.empty() && gameObject) {
            GameObject* t = gameObject;
            if (!clickTarget.empty() && gameObject->scene())
                if (GameObject* g = gameObject->scene()->Find(clickTarget)) t = g;
            bool handled = false;
            if (t && clickFunction == "UseItem")   // Consumables (dispatched here to avoid a header cycle)
                if (auto* cons = t->GetComponent<Consumables>()) { cons->UseIndex((int)clickArg); handled = true; }
            if (t && !handled && !InvokeNativeUIAction(t, clickFunction, clickArg))
                if (auto* tsc = t->GetComponent<ScriptComponent>())
                    if (tsc->VM()) tsc->VM()->CallEvent(clickFunction);
        }
    }
    void EaseColor(float dt) {
        Color t = CurrentColor();
        if (transitionSpeed <= 0.0f) { m_display = t; return; }
        float k = transitionSpeed * dt; if (k > 1.0f) k = 1.0f;
        m_display.r += (t.r - m_display.r) * k;
        m_display.g += (t.g - m_display.g) * k;
        m_display.b += (t.b - m_display.b) * k;
        m_display.a += (t.a - m_display.a) * k;
    }

    bool m_hover = false;
    bool m_pressed = false;
    bool m_clicked = false;
    bool m_focused = false;
    float m_repeat = 0.0f;
    Color m_display = Color::FromBytes(60, 90, 150);
};

} // namespace okay
