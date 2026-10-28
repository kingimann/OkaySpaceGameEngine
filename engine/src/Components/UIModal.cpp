#include "okay/Components/UIModal.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIToast.hpp"   // grok/ui-toast: don't restyle a live toast
#include "okay/UI/UIShape.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <vector>

namespace okay {

namespace {
std::vector<UIModal*>& Registry() { static std::vector<UIModal*> r; return r; }
std::uint64_t g_nextOrder = 0;
} // namespace

UIModal::UIModal() { Registry().push_back(this); }
UIModal::~UIModal() {
    auto& r = Registry();
    r.erase(std::remove(r.begin(), r.end(), this), r.end());
}

bool UIModal::IsOpen() const { return gameObject && gameObject->active && enabled && !UIHidden(gameObject); }

void UIModal::Open() {
    if (gameObject) gameObject->active = true;
    if (m_order == 0) m_order = ++g_nextOrder;
}

void UIModal::Close() {
    if (!gameObject || !gameObject->active) return;
    gameObject->active = false;
    m_order = 0;
    if (auto* sc = gameObject->GetComponent<ScriptComponent>())
        if (sc->VM()) sc->VM()->CallEvent("on_modal_close");
}

void UIModal::RefreshOpenOrder() {
    for (UIModal* m : Registry()) {
        if (!m->IsOpen()) m->m_order = 0;
        else if (m->m_order == 0) m->m_order = ++g_nextOrder;
    }
}

UIModal* UIModal::Top(const Scene* scene) {
    UIModal* top = nullptr;
    for (UIModal* m : Registry())
        if (m->m_order != 0 && m->gameObject && m->gameObject->scene() == scene && m->IsOpen() &&
            (!top || m->m_order > top->m_order))
            top = m;
    return top;
}

bool UIModal::Blocks(GameObject* go) {
    if (!go || Registry().empty()) return false;
    UIModal* top = Top(go->scene());
    return top && !go->IsSelfOrDescendantOf(top->gameObject);
}

void UIModal::ApplyChrome() {
    if (!gameObject || !gameObject->transform) return;
    std::vector<GameObject*> stack;
    for (Transform* t : gameObject->transform->Children())
        if (t && t->gameObject) stack.push_back(t->gameObject);
    for (std::size_t i = 0; i < stack.size(); ++i) {
        GameObject* c = stack[i];
        if (!c || c->GetComponent<UIToast>() || c->GetComponent<UIToastItem>()) continue;
        const std::string& n = c->name;
        if (n == "Card") {
            if (auto* p = c->GetComponent<UIPanel>()) {
                p->color = cardColor;
                p->cornerRadius = cardRadius;
                p->shape = cardRadius > 0.0f ? UIShape::Rounded : UIShape::Rectangle;
                p->borderWidth = 1.0f;
                p->borderColor = Color(titleColor.r, titleColor.g, titleColor.b, 0.45f);
            }
        } else if (n == "Title" || n == "Message") {
            if (auto* tr = c->GetComponent<TextRenderer>()) {
                tr->color = (n == "Title") ? titleColor : bodyColor;
                const std::string& text = (n == "Title") ? titleText : bodyText;
                if (!text.empty()) tr->text = text;
            }
        } else if (n == "ConfirmButton" || n == "CancelButton") {
            if (auto* b = c->GetComponent<UIButton>()) {
                const Color col = (n == "ConfirmButton") ? confirmColor : cancelColor;
                b->color = col;
                b->hoverColor = Color::Lerp(col, Color::White, 0.18f);
                b->pressedColor = Color::Lerp(col, Color::Black, 0.25f);
                b->textColor = Color::White;
                b->hoverTextColor = Color::White;
                const std::string& text = (n == "ConfirmButton") ? confirmText : cancelText;
                if (!text.empty()) b->label = text;
            }
        }
        if (c->transform)
            for (Transform* t : c->transform->Children())
                if (t && t->gameObject) stack.push_back(t->gameObject);
    }
}

void UIModal::Update(float) {
    if (styleCard && IsOpen()) ApplyChrome();
    if (!IsOpen() || Top(gameObject->scene()) != this) return;
    if (closeOnCancel && (Input::GetKeyDown((char)27) || Input::GetGamepadButtonDown(1))) { Close(); return; }
    if (closeOnClickOutside && Input::GetMouseButtonDown(0)) {
        Vec2 o, sz;
        const Vec2 m = Input::MousePosition();
        if (UIWorld().rectOf && UIWorld().rectOf(gameObject, o, sz) &&
            !(m.x >= o.x && m.y >= o.y && m.x <= o.x + sz.x && m.y <= o.y + sz.y))
            Close();
    }
}

} // namespace okay
