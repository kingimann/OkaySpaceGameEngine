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

/// A screen-space text input box (Unity's InputField). Click it to focus, then
/// type — printable keys append, Backspace deletes, Enter submits (calls the
/// sibling ScriptComponent's on_submit()). Shows `placeholder` when empty.
class UIInputField : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{220.0f, 40.0f};
    UIAnchor anchor = UIAnchor::TopLeft;
    Color color            = Color::FromBytes(40, 44, 54);
    Color focusColor       = Color::FromBytes(55, 62, 78);
    Color textColor        = Color::White;
    Color placeholderColor = Color::FromBytes(140, 144, 150);
    std::string text;
    std::string placeholder = "Type...";
    int  maxLength = 64;
    bool focused = false;

    bool Contains(const Vec2& p) const {
        Vec2 o = ResolveAnchor(anchor, position, size);
        return p.x >= o.x && p.y >= o.y && p.x <= o.x + size.x && p.y <= o.y + size.y;
    }

    Color CurrentColor() const { return focused ? focusColor : color; }

    void Update(float) override {
        if (Input::GetMouseButtonDown(0)) focused = Contains(Input::MousePosition());
        if (!focused) return;
        // Input keys are reported lowercase, so skip 'A'..'Z' to avoid typing a
        // character twice (the lowercase letter already covers it).
        for (char c = 32; c < 127; ++c) {
            if (c >= 'A' && c <= 'Z') continue;
            if (Input::GetKeyDown(c) && (int)text.size() < maxLength) text += c;
        }
        if (Input::GetKeyDown((char)8) || Input::GetKeyDown((char)127)) {
            if (!text.empty()) text.pop_back();
        }
        if (Input::GetKeyDown('\r') || Input::GetKeyDown('\n')) {
            focused = false;
            if (gameObject)
                if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                    if (sc->VM()) sc->VM()->CallEvent("on_submit");
        }
    }
};

} // namespace okay
