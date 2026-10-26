#include "okay/Components/UIKeybindField.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIModal.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Scene/GameObject.hpp"

namespace okay {

void UIKeybindField::Start() {
    if (!prefKey.empty() && Prefs::Has(prefKey)) {
        const int k = Prefs::GetInt(prefKey, (int)(unsigned char)key);
        if (k > 0 && k < 256) key = (char)k;
    }
}

std::string UIKeybindField::KeyLabel() const { return Input::KeyName(key); }

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
        if (Input::GetKeyDown('\b')) { Bind(defaultKey); return; }              // Backspace: default
        if (const char k = Input::FirstKeyDown()) Bind(k);
        else if (Input::GetMouseButtonDown(0) && !m_hover) m_listening = false; // clicked elsewhere
        return;
    }
    if (m_hover && Input::GetMouseButtonDown(0)) Listen();
}

} // namespace okay
