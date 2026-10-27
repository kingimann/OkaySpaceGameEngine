#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Components/UIWorldContext.hpp"   // UIToLocal
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include <string>

namespace okay {

/// A "press a key to bind" field for control settings: shows the action and its
/// key ("Jump  [Space]"). Click it (or activate it with Enter / gamepad A) and it
/// waits for a key: the next key pressed becomes the binding, Escape cancels and
/// Backspace restores `defaultKey`. With `prefKey` set the binding is loaded from
/// and saved to Prefs under that name, so it survives restarts. A change fires the
/// object's script on_keybind_change(), with Prefs "ui_keybind_action" and
/// "ui_keybind_key" (the key's name). Games read `key` (or the Prefs value).
class UIKeybindField final : public Behaviour {
public:
    Vec2  position{20.0f, 20.0f};
    Vec2  size{260.0f, 34.0f};
    UIAnchor anchor = UIAnchor::TopLeft;
    std::string action = "Jump";
    char  key = ' ';
    char  defaultKey = ' ';
    std::string prefKey;             // Prefs name to persist the binding under ("" = don't)
    float textSize = 2.0f;
    Color background = Color::FromBytes(41, 46, 59);
    Color keyColor   = Color::FromBytes(66, 150, 250);
    Color listenColor = Color::FromBytes(230, 160, 40);
    Color textColor  = Color::White;
    bool  interactable = true;
    bool  focusable = true;

    bool IsListening() const { return m_listening; }
    bool IsHovered() const { return m_hover; }
    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }
    /// Start waiting for a key (what a click or Enter / A does).
    void Listen() { if (interactable) { m_listening = true; m_skipFrame = true; } }
    /// Set the binding (saves to Prefs and fires on_keybind_change when it changes).
    void Bind(char k);
    /// The shown key text ("Space", "W", ...).
    std::string KeyLabel() const;

    Vec2 ToLocal(const Vec2& p) const { return UIToLocal(gameObject, ResolveAnchor(anchor, position, size), size, p); }
    bool Contains(const Vec2& sp) const {
        Vec2 o = ResolveAnchor(anchor, position, size), p = ToLocal(sp);
        return p.x >= o.x && p.y >= o.y && p.x <= o.x + size.x && p.y <= o.y + size.y;
    }

    void Start() override;
    void Update(float dt) override;

private:
    bool m_listening = false;
    bool m_skipFrame = false;   // the click / Enter that started listening isn't the new key
    bool m_hover = false;
    bool m_focused = false;
};

} // namespace okay
