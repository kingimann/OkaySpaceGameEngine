#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/UIWorldContext.hpp"   // UIToLocal
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Core/Localization.hpp"
#include <string>
#include <vector>

namespace okay {

/// A screen-space checkbox: a square box (toggled fill) plus a text label to its
/// right (mute, fullscreen, "show hints"…). Clicking flips `on` and calls the
/// sibling ScriptComponent's on_toggle() handler. Position/size are window
/// pixels (origin top-left); `size` is the box, the label extends past it.
class UIToggle final : public Behaviour {
public:
    Vec2  position{20.0f, 20.0f};
    Vec2  size{28.0f, 28.0f};
    bool  on = false;
    std::string label = "Toggle";
    /// Localized: a string-table key whose text replaces `label` in the current language.
    LocalizedText loc;
    Color boxColor   = Color::FromBytes(41, 46, 59);
    Color checkColor = Color::FromBytes(66, 150, 250);
    Color textColor  = Color::White;
    UIAnchor anchor = UIAnchor::TopLeft;
    float cornerRadius = 3.0f;                        // rounded box corners
    /// Checkbox (inset tick) or a sliding on/off Switch (pill + knob).
    /// Radio: a round button that belongs to a group; picking one turns the others in its group off,
    /// and picking the one that is already on does nothing (a group always has an answer).
    enum class Style { Checkbox, Switch, Radio };
    Style style = Style::Checkbox;
    /// Radio buttons with the same non-empty group name are one group wherever they sit in the scene;
    /// an empty name groups the radio buttons that share a parent object.
    std::string radioGroup;
    Color knobColor = Color::FromBytes(235, 235, 240);  // switch knob

    bool IsHovered() const { return m_hover; }

    // Keyboard/gamepad menu focus (driven by NavigateUI).
    bool focusable = true;
    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }
    /// Flip the toggle and fire on_toggle — used by activation (Enter/A).
    void Toggle() {
        if (style == Style::Radio) {          // radio: only ever turns ON, and clears its group
            if (on) return;
            on = true;
            ClearGroupMates();
        } else {
            on = !on;
        }
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_toggle");
    }

    /// A screen point mapped into this widget's own frame (where it is really drawn:
    /// inside its UI parent, at the Canvas scale). See UIToLocal.
    Vec2 ToLocal(const Vec2& p) const {
        Vec2 o, sz;
        return UIControlPoint(gameObject, anchor, position, size, p, o, sz);
    }
    bool Contains(const Vec2& sp) const {
        Vec2 o, sz, p = UIControlPoint(gameObject, anchor, position, size, sp, o, sz);
        return p.x >= o.x && p.y >= o.y &&
               p.x <= o.x + sz.x && p.y <= o.y + sz.y;
    }

    bool interactable = true;   // when false: greyed out, ignores clicks
    /// Switch-knob animation speed (units/sec); 0 = snap. The renderer reads
    /// AnimT() (0=off..1=on) so the knob glides and the track cross-fades.
    float animSpeed = 12.0f;

    void Update(float dt) override {
        if (interactable) {
            Vec2 m = Input::MousePosition();
            m_hover = Contains(m);
            if (m_hover && Input::GetMouseButtonDown(0)) {
                if (Input::PointerIsTouch()) m_tapArmed = true;   // a finger flips it on lift, unless it drags
                else Toggle();
            } else if (m_tapArmed) {
                if (Input::TouchDragging() || !Input::PointerIsTouch()) m_tapArmed = false;
                else if (Input::GetMouseButtonUp(0)) { m_tapArmed = false; if (m_hover) Toggle(); }
            }
        } else {
            m_hover = m_tapArmed = false;
        }
        float target = on ? 1.0f : 0.0f;          // ease the knob toward its state
        if (animSpeed <= 0.0f) { m_anim = target; return; }
        float k = animSpeed * dt; if (k > 1.0f) k = 1.0f;
        m_anim += (target - m_anim) * k;
    }

    /// Smoothed 0..1 on-ness for the renderer (knob slide + track cross-fade).
    float AnimT() const { return m_anim; }

    /// The radio buttons in this one's group (this one included). Empty for other styles.
    std::vector<UIToggle*> RadioGroupMembers() const {
        std::vector<UIToggle*> out;
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (style != Style::Radio || !sc) return out;
        Transform* parent = gameObject->transform ? gameObject->transform->Parent() : nullptr;
        for (auto* t : sc->FindObjectsOfType<UIToggle>()) {
            if (!t || t->style != Style::Radio || t->radioGroup != radioGroup) continue;
            if (radioGroup.empty() && (!t->gameObject || !t->gameObject->transform || t->gameObject->transform->Parent() != parent)) continue;
            out.push_back(t);
        }
        return out;
    }
    /// Make this radio button the one that is on (same as a click). No-op for other styles.
    void Select() { if (style == Style::Radio) Toggle(); }
    /// The group's selected button, or nullptr.
    UIToggle* RadioSelected() const {
        for (auto* t : RadioGroupMembers()) if (t->on) return t;
        return nullptr;
    }

private:
    void ClearGroupMates() {
        for (auto* t : RadioGroupMembers()) if (t != this) t->on = false;
    }
    bool m_hover = false;
    bool m_tapArmed = false;   // a finger went down on it; flips when lifted without dragging
    bool m_focused = false;
    float m_anim = 0.0f;
};

} // namespace okay
