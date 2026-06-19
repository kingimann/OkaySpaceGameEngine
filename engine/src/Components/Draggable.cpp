#include "okay/Components/Draggable.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/UIAnchor.hpp"      // UICanvas::Width/Height (window size)
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Core/Prefs.hpp"

namespace okay {
namespace {

void Fire(GameObject* go, const char* fn) {
    if (!go) return;
    if (auto* sc = go->GetComponent<ScriptComponent>())
        if (sc->VM()) sc->VM()->CallEvent(fn);
}

// Screen pixels -> world XY, matching the player/editor 2D projection
// (sx = w/2 + (x-camX)*scale ; sy = h/2 - (y-camY)*scale).
Vec3 ScreenToWorld(const Vec2& m, const Vec3& camPos, float scale, float w, float h) {
    return Vec3{camPos.x + (m.x - w * 0.5f) / scale,
                camPos.y + (h * 0.5f - m.y) / scale, 0.0f};
}

// World-space half-extents of a sprite (size * lossy scale / 2).
bool SpriteHalf(GameObject* go, Vec3& center, Vec2& half) {
    auto* sr = go->GetComponent<SpriteRenderer>();
    if (!sr || !go->transform) return false;
    center = go->transform->Position();
    Vec3 ls = go->transform->LossyScale();
    half = {sr->size.x * ls.x * 0.5f, sr->size.y * ls.y * 0.5f};
    return true;
}

bool SpriteContains(GameObject* go, const Vec3& world) {
    Vec3 c; Vec2 hf;
    if (!SpriteHalf(go, c, hf)) return false;
    return world.x >= c.x - hf.x && world.x <= c.x + hf.x &&
           world.y >= c.y - hf.y && world.y <= c.y + hf.y;
}

// Topmost (highest sortOrder, then last in scene order) sprite under `world`,
// excluding `self`.
GameObject* PickOther(Scene& scene, GameObject* self, const Vec3& world) {
    GameObject* hit = nullptr; int best = -2147483647;
    for (const auto& up : scene.Objects()) {
        GameObject* go = up.get();
        if (go == self || !go->active) continue;
        auto* sr = go->GetComponent<SpriteRenderer>();
        if (!sr || !SpriteContains(go, world)) continue;
        if (sr->sortOrder >= best) { best = sr->sortOrder; hit = go; }
    }
    return hit;
}

GameObject* ValidTarget(GameObject* target, GameObject* self, bool anyTarget) {
    if (!target) return nullptr;
    auto* dz = target->GetComponent<DropZone>();
    if (!anyTarget && !dz) return nullptr;
    if (dz && !dz->acceptTag.empty() && dz->acceptTag != self->tag) return nullptr;
    return target;
}

} // namespace

void Draggable::Update(float) {
    if (!gameObject || !gameObject->scene() || !gameObject->transform) return;
    Scene& scene = *gameObject->scene();
    Camera* cam = scene.mainCamera;
    float w = UICanvas::Width(), h = UICanvas::Height();
    if (w < 1.0f || h < 1.0f) return;
    float ortho = (cam && cam->orthographicSize > 1e-3f) ? cam->orthographicSize : 5.0f;
    Vec3 camPos = (cam && cam->transform) ? cam->transform->Position() : Vec3::Zero;
    float scale = h / (2.0f * ortho);
    if (scale < 1e-6f) scale = 1.0f;
    Vec2 m = Input::MousePosition();
    Vec3 world = ScreenToWorld(m, camPos, scale, w, h);

    // Press over the sprite arms a potential drag.
    if (!m_dragging && !m_armed && Input::GetMouseButtonDown(0) && SpriteContains(gameObject, world)) {
        m_armed = true; m_press = m;
        m_start = gameObject->transform->Position();
        m_grab  = world - m_start;   // keep the cursor where it grabbed the item
    }
    if (m_armed && !m_dragging) {
        if (!Input::GetMouseButton(0)) { m_armed = false; return; }
        float dx = m.x - m_press.x, dy = m.y - m_press.y;
        float thr = dragThreshold * scale;   // threshold is in world units
        if (dx * dx + dy * dy >= thr * thr) {
            m_dragging = true; m_armed = false;
            if (bringToFront)
                if (auto* sr = gameObject->GetComponent<SpriteRenderer>()) {
                    m_savedOrder = sr->sortOrder; sr->sortOrder = 100000;
                }
            Fire(gameObject, "on_drag_start");
        } else {
            return;
        }
    }
    if (!m_dragging) return;

    if (Input::GetMouseButton(0)) {
        Vec3 target = world - m_grab;
        Vec3 p = gameObject->transform->Position();
        if (axis != Axis::Vertical)   p.x = target.x;
        if (axis != Axis::Horizontal) p.y = target.y;
        gameObject->transform->SetPosition(p);
        GameObject* hover = ValidTarget(PickOther(scene, gameObject, world), gameObject, anyTarget);
        for (const auto& up : scene.Objects())
            if (auto* dz = up->GetComponent<DropZone>()) dz->SetHovered(up.get() == hover);
        Fire(gameObject, "on_drag");
    } else {                                   // released: resolve a drop
        m_dragging = false;
        if (bringToFront)
            if (auto* sr = gameObject->GetComponent<SpriteRenderer>()) sr->sortOrder = m_savedOrder;
        for (const auto& up : scene.Objects())
            if (auto* dz = up->GetComponent<DropZone>()) dz->SetHovered(false);
        GameObject* target = ValidTarget(PickOther(scene, gameObject, world), gameObject, anyTarget);
        m_dropTarget = target;
        if (target) {
            if (snapToZone && target->transform)
                gameObject->transform->SetPosition(target->transform->Position());
            Prefs::SetString("drop_target", target->name);
            Prefs::SetString("drop_source", gameObject->name);
            Fire(gameObject, "on_drop");
            Fire(target, "on_receive");
        } else if (returnToStart) {
            gameObject->transform->SetPosition(m_start);
        }
    }
}

} // namespace okay
