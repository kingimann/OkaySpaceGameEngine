#include "okay/Components/UIContextMenu.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/UIModal.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"

namespace okay {

void UIContextMenu::OpenAt(const Vec2& at, float screenW, float screenH) {
    const Vec2 sz = Size();
    m_at = at;
    if (screenW > 0.0f && m_at.x + sz.x > screenW) m_at.x = screenW - sz.x;   // keep it on screen
    if (screenH > 0.0f && m_at.y + sz.y > screenH) m_at.y = screenH - sz.y;
    if (m_at.x < 0.0f) m_at.x = 0.0f;
    if (m_at.y < 0.0f) m_at.y = 0.0f;
    m_open = true;
    m_hover = -1;
    m_openedFrame = 1;
}

int UIContextMenu::ItemAt(const Vec2& p) const {
    if (!m_open || items.empty() || itemHeight <= 0.0f) return -1;
    if (p.x < m_at.x || p.x > m_at.x + width || p.y < m_at.y) return -1;
    const int i = (int)((p.y - m_at.y) / itemHeight);
    if (i < 0 || i >= (int)items.size() || items[i] == "-") return -1;
    return i;
}

UIContextMenu* UIContextMenu::OpenMenuAt(GameObject* anyInScene, const Vec2& p) {
    if (!anyInScene || !anyInScene->scene()) return nullptr;
    for (const auto& up : anyInScene->scene()->Objects())
        if (auto* m = up->GetComponent<UIContextMenu>())
            if (m->m_open) {
                const Vec2 o = m->m_at, s = m->Size();
                if (p.x >= o.x && p.y >= o.y && p.x <= o.x + s.x && p.y <= o.y + s.y) return m;
            }
    return nullptr;
}

void UIContextMenu::Pick(int index) {
    const std::string item = items[index];
    Close();
    if (onSelect) onSelect(index, item);
    if (gameObject)
        if (auto* sc = gameObject->GetComponent<ScriptComponent>())
            if (sc->VM()) {
                Prefs::SetInt("ui_context_index", index);
                Prefs::SetString("ui_context_item", item);
                sc->VM()->CallEvent("on_context_select");
            }
}

void UIContextMenu::Update(float) {
    if (!gameObject || UIModal::Blocks(gameObject)) { if (m_open) Close(); return; }
    const Vec2 mouse = Input::MousePosition();
    const float sw = UIWorld().screenW > 0.0f ? UIWorld().screenW : UICanvas::Width();
    const float sh = UIWorld().screenH > 0.0f ? UIWorld().screenH : UICanvas::Height();
    if (m_open) {
        m_hover = ItemAt(mouse);
        if (m_openedFrame > 0) { --m_openedFrame; return; }
        if (Input::GetKeyDown((char)27)) { Close(); return; }
        if (Input::GetMouseButtonDown(0) || Input::GetMouseButtonDown(1)) {
            if (m_hover >= 0 && Input::GetMouseButtonDown(0)) Pick(m_hover);
            else if (m_hover < 0) Close();
        }
        return;
    }
    if (!Input::GetMouseButtonDown(1) || items.empty()) return;
    // Is the pointer on this menu's widget (as drawn: inside its parents and clips)?
    auto onWidget = [&](GameObject* g) {
        Vec2 o, sz;
        return UIWorld().rectOf && UIWorld().rectOf(g, o, sz) &&
               mouse.x >= o.x && mouse.y >= o.y && mouse.x <= o.x + sz.x && mouse.y <= o.y + sz.y &&
               (!UIWorld().clipContains || UIWorld().clipContains(g, mouse));
    };
    bool over = false;
    if (!anywhere) {
        over = onWidget(gameObject);
    } else if (gameObject->scene()) {   // a fallback menu: only where no widget menu claims the click
        over = true;
        for (const auto& up : gameObject->scene()->Objects())
            if (auto* m = up->GetComponent<UIContextMenu>())
                if (m != this && !m->anywhere && up->active && onWidget(up.get())) { over = false; break; }
    }
    if (over) OpenAt(mouse, sw, sh);
}

} // namespace okay
