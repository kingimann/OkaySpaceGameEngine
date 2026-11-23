#include "okay/Input/PadProfiles.hpp"
#include "okay/Components/UIKeybindField.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIModal.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Input/InputRuntime.hpp"
#include "okay/Scene/GameObject.hpp"

namespace okay {

namespace {
// "ia:Jump" / "ia:Jump@1": an action of a local player's input asset.
bool ParseTyped(const std::string& s, std::string& name, int& player) {
    if (s.rfind("ia:", 0) != 0) return false;
    name = s.substr(3); player = 0;
    const std::size_t at = name.find('@');
    if (at != std::string::npos) { player = std::atoi(name.substr(at + 1).c_str()); name.resize(at); }
    return !name.empty();
}
// The first binding of the keyboard / mouse group, or of the controller group.
int TypedBinding(const ActionSet& set, const ActionDef& a, bool gamepad) {
    for (std::size_t i = 0; i < a.bindings.size(); ++i) {
        const std::string& g = a.bindings[i].group;
        if (g.empty()) return (int)i;
        for (const ControlScheme& c : set.Schemes()) if (c.name == g && (gamepad ? c.gamepad : (c.keyboard || c.mouse))) return (int)i;
    }
    return -1;
}
}

void UIKeybindField::Start() {
    { std::string tn; int tp = 0; if (ParseTyped(inputAction, tn, tp)) InputRuntime::Set(tp); }   // a typed field needs its player (the first read joins one)
    if (!prefKey.empty() && Prefs::Has(prefKey)) {
        const int k = Prefs::GetInt(prefKey, (int)(unsigned char)key);
        if (k > 0 && k < 256) key = (char)k;
    }
}

std::string UIKeybindField::KeyLabel() const {
    if (inputAction.empty()) return Input::KeyName(key);
    std::string tn; int tp = 0;
    if (ParseTyped(inputAction, tn, tp)) {
        const ActionSet* set = InputRuntime::Players().Player(tp);
        const ActionDef* a = set ? set->Find(tn) : nullptr;
        const int bi = a ? TypedBinding(*set, *a, gamepad) : -1;
        const int pi = part < 0 ? 0 : part;
        if (bi < 0 || pi >= (int)a->bindings[(std::size_t)bi].parts.size()) return "None";
        return PadProfiles::BindingLabel(InputBinding::Parse(a->bindings[(std::size_t)bi].parts[(std::size_t)pi]));
    }
    const InputBinding* b = InputActions::Current(inputAction, gamepad);
    if (!b) return "None";
    if (part >= 0 && part < 4 && b->kind == InputBinding::Kind::Keys4) return Input::KeyName(b->keys[part]);
    return b->kind == InputBinding::Kind::PadButton ? PadProfiles::BindingLabel(*b) : b->Label();
}

void UIKeybindField::BindAction(const std::string& binding) {
    m_listening = false;
    const std::string before = KeyLabel();
    std::string tn; int tp = 0;
    if (ParseTyped(inputAction, tn, tp)) {
        ActionSet* set = InputRuntime::Set(tp);
        const ActionDef* a = set ? set->Find(tn) : nullptr;
        const int bi = a ? TypedBinding(*set, *a, gamepad) : -1;
        if (bi < 0 || !InputRuntime::Rebind(tp, tn, bi, part < 0 ? 0 : part, InputBinding::Parse(binding).ToString())) return;   // the canonical text ("key:j"); player 0's reaches InputActions too
        InputRuntime::SaveOverrides(tp);
    } else if (!InputActions::Rebind(inputAction, binding, part)) return;
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
            std::string tn; int tp = 0;
            const bool typed = ParseTyped(inputAction, tn, tp);
            if (Input::GetKeyDown('\b')) {
                if (!typed) InputActions::Reset(inputAction);
                else if (ActionSet* set = InputRuntime::Set(tp)) {
                    if (const ActionDef* a = set->Find(tn)) InputRuntime::ResetBinding(tp, tn, TypedBinding(*set, *a, gamepad));
                    InputRuntime::SaveOverrides(tp);
                }
                m_listening = false; return;
            }
            if (gamepad) {
                const int pad = typed ? InputRuntime::Players().PadOf(tp) : -1;
                if (const int b = Input::FirstGamepadButtonDown(pad >= 0 ? pad : 0); b >= 0) BindAction("pad:" + Input::GamepadButtonKey(b));
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
