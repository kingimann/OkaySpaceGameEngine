#include "okay/Components/UIKeybindField.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIModal.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Scene/GameObject.hpp"

namespace okay {

void UIKeybindField::Start() {
    if (!prefKey.empty() && Prefs::Has(prefKey)) {
        const int k = Prefs::GetInt(prefKey, (int)(unsigned char)key);
        if (k > 0 && k < 256) key = (char)k;
    }
}

std::string UIKeybindField::KeyLabel() const {
    if (inputAction.empty()) return Input::KeyName(key);
    const InputBinding* b = InputActions::Current(inputAction, gamepad);
    if (!b) return "None";
    if (part >= 0 && part < 4 && b->kind == InputBinding::Kind::Keys4) return Input::KeyName(b->keys[part]);
    return b->Label();
}

void UIKeybindField::BindAction(const std::string& binding) {
    m_listening = false;
    const std::string before = KeyLabel();
    if (!InputActions::Rebind(inputAction, binding, part)) return;
    if (KeyLabel() == before) return;
    if (gameObject)
        if (auto* sc = gameObject->GetComponent<ScriptComponent>())
            if (sc->VM()) {
                Prefs::SetString("ui_keybind_action", inputAction);
                Prefs::SetString("ui_keybind_key", KeyLabel());
                sc->VM()->CallEvent("on_keybind_change");
            }
}

void UIKeybindField::Bind(char k) {
    m_listening = false;
    if (k == key) return;
    key = k;
    if (!prefKey.empty()) Prefs::SetInt(prefKey, (int)(unsigned char)k);
    if (gameObject)
        if (auto* sc = gameObject->GetComponent<ScriptComponent>())
            if (sc->VM()) {
                Prefs::SetString("ui_keybind_action", action);
                Prefs::SetString("ui_keybind_key", Input::KeyName(k));
                sc->VM()->CallEvent("on_keybind_change");
            }
}

void UIKeybindField::Update(float) {
    if (!interactable || UIModal::Blocks(gameObject)) { m_hover = false; m_listening = false; return; }
    m_hover = Contains(Input::MousePosition());
    if (m_listening) {
        if (m_skipFrame) { m_skipFrame = false; return; }
        if (Input::GetKeyDown((char)27)) { m_listening = false; return; }       // Escape: keep the old key
        if (!inputAction.empty()) {
            if (Input::GetKeyDown('\b')) { InputActions::Reset(inputAction); m_listening = false; return; }
            if (gamepad) {
                if (const int b = Input::FirstGamepadButtonDown(); b >= 0) BindAction("pad:" + Input::GamepadButtonKey(b));
            } else if (const char k = Input::FirstKeyDown()) {
                BindAction("key:" + Input::KeyName(k));
            } else if (part < 0 && Input::GetMouseButtonDown(1)) {
                BindAction("mouse:right");
            } else if (part < 0 && Input::GetMouseButtonDown(2)) {
                BindAction("mouse:middle");
            } else if (Input::GetMouseButtonDown(0) && !m_hover) {
                m_listening = false;
            }
            return;
        }
        if (Input::GetKeyDown('\b')) { Bind(defaultKey); return; }              // Backspace: default
        if (const char k = Input::FirstKeyDown()) Bind(k);
        else if (Input::GetMouseButtonDown(0) && !m_hover) m_listening = false; // clicked elsewhere
        return;
    }
    if (m_hover && Input::GetMouseButtonDown(0)) Listen();
}

} // namespace okay
