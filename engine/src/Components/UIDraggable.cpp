#include "okay/Components/UIDraggable.hpp"
#include "okay/Components/UIElement.hpp"   // GetUIRect / UIScreenContains / UIScaleFor
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Core/Prefs.hpp"

namespace okay {
namespace {

void Fire(GameObject* go, const char* fn) {
    if (!go) return;
    if (auto* sc = go->GetComponent<ScriptComponent>())
        if (sc->VM()) sc->VM()->CallEvent(fn);
}

// Topmost UI widget under `p` (canvas pixels), excluding `self`.
GameObject* PickOther(Scene& scene, GameObject* self, const Vec2& p, float w, float h) {
    GameObject* hit = nullptr;
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (go == self || !go->active) continue;
        if (UIScreenContains(go, p, w, h)) hit = go;   // last match = topmost
    }
    return hit;
}

} // namespace

void UIDraggable::Update(float) {
    if (!gameObject || !gameObject->scene()) return;
    UIRect r = GetUIRect(gameObject);
    if (!r.valid || !r.position) return;
    Scene& scene = *gameObject->scene();
    float w = UICanvas::Width(), h = UICanvas::Height();
    Vec2 m = Input::MousePosition();

    if (!m_dragging && Input::GetMouseButtonDown(0) && UIScreenContains(gameObject, m, w, h)) {
        m_dragging = true;
        m_start = *r.position;
        m_prevMouse = m;
        Fire(gameObject, "on_drag_start");
        return;
    }
    if (!m_dragging) return;

    if (Input::GetMouseButton(0)) {
        float s = UIScaleFor(gameObject, w, h);
        if (s < 1e-3f) s = 1.0f;
        r.position->x += (m.x - m_prevMouse.x) / s;
        r.position->y += (m.y - m_prevMouse.y) / s;
        m_prevMouse = m;
        Fire(gameObject, "on_drag");
    } else {                                   // released: resolve a drop
        m_dragging = false;
        GameObject* target = PickOther(scene, gameObject, m, w, h);
        if (target && !anyTarget && !target->GetComponent<UIDropTarget>())
            target = nullptr;                  // only UIDropTargets count
        m_dropTarget = target;
        if (target) {
            Prefs::SetString("ui_drop_target", target->name);
            Prefs::SetString("ui_drop_source", gameObject->name);
            Fire(gameObject, "on_drop");
            Fire(target, "on_receive");
        } else if (returnToStart) {
            *r.position = m_start;
        }
    }
}

} // namespace okay
