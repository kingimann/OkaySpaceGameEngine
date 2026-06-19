#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Input/Input.hpp"
#include <string>
#include <vector>

namespace okay {

/// A screen-space dropdown / option selector (Unity's Dropdown): a header box
/// showing the current choice; clicking it opens a list of options below, and
/// picking one sets `value` (the selected index) and calls the sibling
/// ScriptComponent's on_change() handler. Position/size are window pixels
/// (origin top-left); each open option is `size.y` tall.
class UIDropdown : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{180.0f, 32.0f};
    std::vector<std::string> options{"Option A", "Option B", "Option C"};
    int  value = 0;                                 // selected option index
    bool open  = false;                             // list expanded
    Color color       = Color::FromBytes(40, 44, 56);
    Color hoverColor  = Color::FromBytes(60, 70, 92);   // highlighted option
    Color listColor   = Color::FromBytes(28, 30, 40);   // open list background
    Color textColor   = Color::White;
    Color borderColor = Color::FromBytes(255, 255, 255, 60);
    UIAnchor anchor = UIAnchor::TopLeft;

    /// The currently selected option text (empty if none).
    const std::string& Selected() const {
        static const std::string empty;
        if (value >= 0 && value < (int)options.size()) return options[value];
        return empty;
    }

    /// Set the selection by index, firing on_change if it actually changed.
    void SetValue(int v) {
        if (v < 0 || v >= (int)options.size() || v == value) return;
        value = v;
        Fire();
    }

    bool IsHovered() const { return m_hover; }
    int  HoveredOption() const { return m_hoverOption; }   // -1 = none

    // Keyboard/gamepad menu focus (driven by NavigateUI).
    bool focusable = true;
    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }
    /// Move the selection by `dir` (-1/+1), clamped, firing on_change.
    void Cycle(int dir) {
        int v = value + dir;
        if (v < 0) v = 0;
        if (v > (int)options.size() - 1) v = (int)options.size() - 1;
        SetValue(v);
    }

    /// Header rectangle origin (resolved against the anchor).
    Vec2 Origin() const { return ResolveAnchor(anchor, position, size); }

    bool HeaderContains(const Vec2& p) const {
        Vec2 o = Origin();
        return p.x >= o.x && p.y >= o.y &&
               p.x <= o.x + size.x && p.y <= o.y + size.y;
    }

    /// Which open option (0..N-1) the point is over, or -1 (also -1 when closed).
    int OptionAt(const Vec2& p) const {
        if (!open) return -1;
        Vec2 o = Origin();
        if (p.x < o.x || p.x > o.x + size.x) return -1;
        float top = o.y + size.y;
        for (int i = 0; i < (int)options.size(); ++i) {
            float y = top + i * size.y;
            if (p.y >= y && p.y <= y + size.y) return i;
        }
        return -1;
    }

    bool interactable = true;   // when false: greyed out, ignores clicks

    void Update(float) override {
        if (!interactable) { m_hover = false; m_hoverOption = -1; open = false; return; }
        Vec2 m = Input::MousePosition();
        m_hover = HeaderContains(m);
        m_hoverOption = OptionAt(m);
        if (Input::GetMouseButtonDown(0)) {
            if (m_hover) {
                open = !open;                       // toggle the list
            } else if (open && m_hoverOption >= 0) {
                SetValue(m_hoverOption);            // pick an option
                open = false;
            } else if (open) {
                open = false;                       // click outside closes
            }
        }
    }

private:
    void Fire() {
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_change");
    }
    bool m_hover = false;
    bool m_focused = false;
    int  m_hoverOption = -1;
};

} // namespace okay
