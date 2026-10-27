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
///
/// With `inputAction` set it rebinds that InputActions action instead ("Jump",
/// "Fire"): it shows the action's current binding, `gamepad` picks whether it
/// listens for a key / mouse button or a controller button, and for a four-key
/// direction (Move) `part` 0..3 picks the up / down / left / right key. The new
/// binding is saved to the player's prefs; Backspace puts the action's defaults back.
class UIKeybindField final : public Behaviour {
public:
    Vec2  position{20.0f, 20.0f};
    Vec2  size{260.0f, 34.0f};
    UIAnchor anchor = UIAnchor::TopLeft;
    std::string action = "Jump";
    char  key = ' ';
    char  defaultKey = ' ';
    std::string prefKey;             // Prefs name to persist the binding under ("" = don't)
    std::string inputAction;         // an InputActions action to rebind ("" = use `key`)
    bool  gamepad = false;           // with inputAction: bind a controller button
    int   part = -1;                 // with inputAction: 0..3 = one key of a four-key direction
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
    /// The shown key text ("Space", "W", "RT", ...).
    std::string KeyLabel() const;
    /// What the field shows while waiting: "Press a key..." / "Press a button...".
    const char* ListenPrompt() const { return !inputAction.empty() && gamepad ? "Press a button..." : "Press a key..."; }
    /// Bind an InputActions binding ("key:e", "pad:rt", "mouse:right").
    void BindAction(const std::string& binding);

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
