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

// Topmost UI widget under `p` (canvas pixels), excluding `self`: the one drawn over the others
// (canvas order, layer, hierarchy), the same order the renderer paints in.
GameObject* PickOther(Scene& scene, GameObject* self, const Vec2& p, float w, float h) {
    GameObject* hit = nullptr;
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (go == self || !go->active) continue;
        if (UIScreenContains(go, p, w, h) && (!hit || UIDrawsAbove(go, hit))) hit = go;
    }
    return hit;
}

// The widget a drag moves: this one, or (dragParent) its nearest UI ancestor.
GameObject* Mover(GameObject* self, bool dragParent) {
    if (!dragParent || !self || !self->transform) return self;
    for (Transform* t = self->transform->Parent(); t; t = t->Parent())
        if (t->gameObject && GetUIRect(t->gameObject).valid) return t->gameObject;
    return self;
}

// Is `target` a valid drop for a draggable `self` (tag/anyTarget rules)?
GameObject* ValidTarget(GameObject* target, GameObject* self, bool anyTarget) {
    if (!target) return nullptr;
    auto* dt = target->GetComponent<UIDropTarget>();
    if (!anyTarget && !dt) return nullptr;
    if (dt && !dt->acceptTag.empty() && dt->acceptTag != self->tag) return nullptr;
    return target;
}

} // namespace

void UIDraggable::Update(float) {
    if (!gameObject || !gameObject->scene()) return;
    GameObject* mover = Mover(gameObject, dragParent);
    UIRect r = GetUIRect(mover);
    if (!r.valid || !r.position) return;
    Scene& scene = *gameObject->scene();
    float w = UICanvas::Width(), h = UICanvas::Height();
    Vec2 m = Input::MousePosition();

    // Press over the widget arms a potential drag (it begins once the pointer
    // moves past dragThreshold — so a click that barely moves stays a click).
    if (!m_dragging && !m_armed && Input::GetMouseButtonDown(0) && UIScreenContains(gameObject, m, w, h)) {
        m_armed = true; m_press = m; m_prevMouse = m; m_start = *r.position;
    }
    if (m_armed && !m_dragging) {
        if (!Input::GetMouseButton(0)) { m_armed = false; return; }
        float dx = m.x - m_press.x, dy = m.y - m_press.y;
        if (dx * dx + dy * dy >= dragThreshold * dragThreshold) {
            m_dragging = true; m_armed = false; m_prevMouse = m_press;
            if (bringToFront && gameObject->scene()) gameObject->scene()->MoveToFront(mover);
            Fire(gameObject, "on_drag_start");
            // Leaving the slot it was sitting in: tell that slot (on_remove).
            if (m_dropTarget) {
                Prefs::SetString("ui_remove_item", gameObject->name);
                Prefs::SetString("ui_remove_target", m_dropTarget->name);
                Fire(m_dropTarget, "on_remove");
                m_dropTarget = nullptr;
            }
        } else {
            return;
        }
    }
    if (!m_dragging) return;

    if (Input::GetMouseButton(0)) {
        float s = UIScaleFor(mover, w, h);
        if (s < 1e-3f) s = 1.0f;
        if (axis != Axis::Vertical)   r.position->x += (m.x - m_prevMouse.x) / s;
        if (axis != Axis::Horizontal) r.position->y += (m.y - m_prevMouse.y) / s;
        if (keepOnScreen) {          // keep the whole widget on the screen (screen pixels -> design pixels)
            Vec2 o, sz;
            if (GetUIScreenRect(mover, w, h, o, sz) && sz.x > 0.0f && sz.y > 0.0f) {
                float dx = 0.0f, dy = 0.0f;
                if (o.x < 0.0f) dx = -o.x; else if (o.x + sz.x > w) dx = w - (o.x + sz.x);
                if (o.y < 0.0f) dy = -o.y; else if (o.y + sz.y > h) dy = h - (o.y + sz.y);
                if (!r.Stretches()) { r.position->x += dx / s; r.position->y += dy / s; }
            }
        }
        m_prevMouse = m;
        // Highlight the slot we're hovering (clear the rest). The raw topmost target
        // is highlighted whether or not the tag matches, so the slot can show a
        // reject tint for a wrong item; validity is recorded for the renderer.
        GameObject* raw = PickOther(scene, gameObject, m, w, h);
        for (const auto& up : scene.Objects())
            if (auto* dt = up->GetComponent<UIDropTarget>()) {
                bool over = (up.get() == raw);
                bool was = dt->IsHovered();
                dt->SetHovered(over);
                if (over) dt->SetValid(dt->acceptTag.empty() || dt->acceptTag == gameObject->tag);
                // Fire enter/exit on the slot as the item moves over it.
                if (over && !was) {
                    Prefs::SetString("ui_drag_item", gameObject->name);
                    Fire(up.get(), "on_drag_enter");
                } else if (!over && was) {
                    Fire(up.get(), "on_drag_exit");
                }
            }
        Fire(gameObject, "on_drag");
    } else {                                   // released: resolve a drop
        m_dragging = false;
        for (const auto& up : scene.Objects())                  // clear all highlights
            if (auto* dt = up->GetComponent<UIDropTarget>()) dt->SetHovered(false);
        GameObject* target = ValidTarget(PickOther(scene, gameObject, m, w, h), gameObject, anyTarget);
        m_dropTarget = target;
        if (target) {
            // Snap-to-slot: center this item in the slot (zero-script inventory).
            // Honored if the draggable wants it OR the target requests centering.
            auto* tdt = target->GetComponent<UIDropTarget>();
            if (snapToSlot || (tdt && tdt->snapToCenter)) {
                UIRect tr = GetUIRect(target);
                if (tr.valid && tr.position && r.anchorPtr) {
                    *r.anchorPtr = tr.anchor;
                    r.position->x = tr.position->x + (tr.size.x - r.size.x) * 0.5f;
                    r.position->y = tr.position->y + (tr.size.y - r.size.y) * 0.5f;
                }
            }
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
