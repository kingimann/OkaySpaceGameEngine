#include "okay/Components/UIDialogueBox.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Core/Utf8.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>

namespace okay {

namespace {
GameObject* FindDescendant(Transform* root, const std::string& name) {
    if (!root) return nullptr;
    for (Transform* c : root->Children()) {
        if (!c || !c->gameObject) continue;
        if (c->gameObject->name == name) return c->gameObject;
        if (GameObject* g = FindDescendant(c, name)) return g;
    }
    return nullptr;
}
void SetText(GameObject* go, const std::string& s) {
    if (!go) return;
    if (auto* tr = go->GetComponent<TextRenderer>()) tr->text = s;
    else if (auto* b = go->GetComponent<UIButton>()) b->label = s;
}
}

GameObject* UIDialogueBox::Part(const char* name) const {
    return gameObject ? FindDescendant(gameObject->transform, name) : nullptr;
}

void UIDialogueBox::Fire(const char* fn) {
    if (gameObject)
        if (auto* sc = gameObject->GetComponent<ScriptComponent>())
            if (sc->VM()) sc->VM()->CallEvent(fn);
}

void UIDialogueBox::Start() {
    if (startHidden) {
        if (GameObject* box = Part("Box")) box->active = false;
        m_open = false;
    }
}

void UIDialogueBox::Show(const std::string& speaker, const std::string& text,
                         const std::vector<std::string>& choices) {
    m_speaker = speaker;
    m_text = text;
    m_choices.assign(choices.begin(), choices.begin() + (std::ptrdiff_t)std::min<std::size_t>(choices.size(), 4));
    m_open = true;
    ++m_serial;
    m_justShown = true;
    m_shown = 0.0f;
    m_complete = charsPerSecond <= 0.0f;
    if (GameObject* box = Part("Box")) box->active = true;
    if (gameObject) gameObject->active = true;
    SetText(Part("Speaker"), speaker);
    if (GameObject* body = Part("Body"))
        if (auto* tr = body->GetComponent<TextRenderer>()) {
            tr->text = text;
            tr->typeSpeed = 0.0f;                       // this box drives the reveal itself
            tr->visibleChars = m_complete ? -1 : 0;
        }
    Refresh();
    Fire("on_dialogue_line");
}

void UIDialogueBox::Refresh() {
    GameObject* next = Part("Next");
    if (next) next->active = m_open && m_complete && m_choices.empty();
    for (int i = 0; i < 4; ++i) {
        const std::string nm = "Choice" + std::to_string(i + 1);
        GameObject* c = Part(nm.c_str());
        if (!c) continue;
        const bool on = m_open && m_complete && i < (int)m_choices.size();
        c->active = on;
        if (on) {
            // The label is the button's own text, or a text child called "Label".
            if (auto* b = c->GetComponent<UIButton>()) {
                b->label = m_choices[(std::size_t)i];
                b->clickTarget = gameObject ? gameObject->name : std::string();
                b->clickFunction = "Choose";
                b->clickArg = (float)i;
            }
            if (GameObject* lab = FindDescendant(c->transform, "Label")) SetText(lab, m_choices[(std::size_t)i]);
        }
    }
}

void UIDialogueBox::Advance() {
    if (!m_open) return;
    if (!m_complete) {                                  // first press finishes the line
        m_complete = true;
        if (GameObject* body = Part("Body"))
            if (auto* tr = body->GetComponent<TextRenderer>()) tr->visibleChars = -1;
        Refresh();
        return;
    }
    if (!m_choices.empty()) return;                     // a choice has to be picked
    Fire("on_dialogue_done");
    if (hideWhenDone) Close();
}

bool UIDialogueBox::Choose(int index) {
    if (!m_open || !m_complete || index < 0 || index >= (int)m_choices.size()) return false;
    m_lastChoice = index;
    const unsigned serial = m_serial;
    Fire("on_dialogue_choice");
    if (m_serial == serial && hideWhenDone) Close();    // the script did not show a follow-up line
    return true;
}

void UIDialogueBox::Close() {
    m_open = false;
    m_choices.clear();
    Refresh();
    if (GameObject* box = Part("Box")) box->active = false;
}

void UIDialogueBox::Update(float dt) {
    if (!m_open) return;
    const bool fresh = m_justShown;      // input that opened the line must not also skip it
    m_justShown = false;
    if (!m_complete) {
        m_shown += charsPerSecond * dt;
        const int total = (int)utf8::Length(m_text);
        if ((int)m_shown >= total) {
            m_complete = true;
            if (GameObject* body = Part("Body"))
                if (auto* tr = body->GetComponent<TextRenderer>()) tr->visibleChars = -1;
            Refresh();
        } else if (GameObject* body = Part("Body")) {
            if (auto* tr = body->GetComponent<TextRenderer>()) tr->visibleChars = (int)m_shown;
        }
    }
    if (advanceOnClick && m_open && !fresh) {
        const bool key = Input::GetKeyDown(' ') || Input::GetKeyDown('\r') || Input::GetKeyDown('\n');
        bool click = false;
        if (Input::GetMouseButtonDown(0)) {
            if (GameObject* box = Part("Box")) click = UIScreenContains(box, Input::MousePosition(), UICanvas::Width(), UICanvas::Height());
        }
        // A choice button takes its own click; only the box itself advances.
        if (click && !m_choices.empty() && m_complete) click = false;
        if (key || click) Advance();
    }
}

} // namespace okay
