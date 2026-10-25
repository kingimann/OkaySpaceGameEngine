#pragma once
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Collider3D.hpp"

namespace okay {

/// Stair step-up shared by the character controllers: when a GROUNDED, moving
/// body is blocked by a LOW obstacle (a stair, a curb), lift it onto the step
/// instead of letting it grind against the face. Three probes decide it:
/// a knee-height ray must hit (something is in the way), the same ray at
/// step-offset height must be CLEAR (it's a step, not a wall), and a down-ray
/// past the lip must find the step top within stepOffset of the feet.
inline void TryStepUp(Scene& sc, GameObject* go, Rigidbody3D* rb, const Vec3& dir,
                      bool grounded, bool moving, float stepOffset) {
    if (!grounded || !moving || stepOffset <= 0.0f || !rb || !go || !go->transform) return;
    Vec3 pos = go->transform->Position();
    const float reach = 0.45f;                      // capsule radius + skin
    Vec3 fw{dir.x, 0.0f, dir.z};
    float fl = std::sqrt(fw.x * fw.x + fw.z * fw.z);
    if (fl < 1e-4f) return;
    fw.x /= fl; fw.z /= fl;
    RaycastHit3D low = sc.physics3D().Raycast(sc, {pos.x, pos.y + 0.05f, pos.z}, fw, reach, go);
    if (!low.hit) return;                           // nothing in the way
    RaycastHit3D high = sc.physics3D().Raycast(sc, {pos.x, pos.y + stepOffset + 0.02f, pos.z},
                                               fw, reach + 0.10f, go);
    if (high.hit) return;                           // taller than a step -> a real wall
    Vec3 overO{pos.x + fw.x * (low.distance + 0.12f), pos.y + stepOffset + 0.02f,
               pos.z + fw.z * (low.distance + 0.12f)};
    RaycastHit3D top = sc.physics3D().Raycast(sc, overO, {0.0f, -1.0f, 0.0f}, stepOffset + 0.04f, go);
    if (!top.hit) return;
    float rise = top.point.y - pos.y;
    if (rise <= 0.01f || rise > stepOffset) return;
    // Climb: pop the body onto the step with a small forward nudge past the lip.
    go->transform->SetPosition({pos.x + fw.x * 0.06f, top.point.y + 0.02f, pos.z + fw.z * 0.06f});
    if (rb->velocity.y < 0.0f) rb->velocity.y = 0.0f;
}


/// Moving-platform support shared by the controllers: remember what the body
/// stands on and apply the platform's frame-to-frame translation, so players
/// RIDE elevators and moving props instead of sliding off them. Static ground
/// has a zero delta, so tracking it costs nothing.
struct PlatformRide { GameObject* platform = nullptr; Vec3 lastPos{0.0f, 0.0f, 0.0f}; };
inline void RideMovingPlatform(Scene& sc, GameObject* go, PlatformRide& st, bool grounded) {
    if (!go || !go->transform) { st.platform = nullptr; return; }
    GameObject* plat = nullptr;
    if (grounded) {
        Vec3 pos = go->transform->Position();
        RaycastHit3D hit = sc.physics3D().Raycast(sc, {pos.x, pos.y + 0.1f, pos.z},
                                                  {0.0f, -1.0f, 0.0f}, 1.4f, go);
        if (hit.hit && hit.gameObject && hit.gameObject->transform) plat = hit.gameObject;
    }
    if (plat && plat == st.platform) {
        Vec3 cur = plat->transform->Position();
        Vec3 d{cur.x - st.lastPos.x, cur.y - st.lastPos.y, cur.z - st.lastPos.z};
        if (d.x != 0.0f || d.y != 0.0f || d.z != 0.0f)
            go->transform->SetPosition(go->transform->Position() + d);
        st.lastPos = cur;
    } else if (plat) {
        st.platform = plat;
        st.lastPos = plat->transform->Position();
    } else {
        st.platform = nullptr;
    }
}

/// Stance body shared by the controllers: crouching and lying prone really make
/// the player shorter — the collider shrinks to the stance height (feet stay
/// where they are), so you can crawl under low cover — and standing back up is
/// refused while something is overhead.
struct StanceBody {
    bool  captured = false;
    float standH = 1.8f;     ///< the authored (standing) collider height
    float feetY = 0.0f;      ///< collider bottom, local to the object
    float curH = -1.0f;
};
/// The player's box / capsule collider, or null.
inline Collider3D* StanceCollider(GameObject* go) {
    if (!go) return nullptr;
    if (auto* b = go->GetComponent<BoxCollider3D>()) return b;
    if (auto* c = go->GetComponent<CapsuleCollider3D>()) return c;
    return nullptr;
}
inline void CaptureStance(GameObject* go, StanceBody& st) {
    if (st.captured) return;
    Collider3D* c = StanceCollider(go);
    if (!c) return;
    if (auto* b = dynamic_cast<BoxCollider3D*>(c)) st.standH = b->size.y;
    else if (auto* k = dynamic_cast<CapsuleCollider3D*>(c)) st.standH = k->height;
    st.feetY = c->offset.y - st.standH * 0.5f;
    st.curH = st.standH;
    st.captured = true;
}
/// Is there room for the body to be `height` tall where it stands now?
inline bool StanceHasRoom(Scene& sc, GameObject* go, const StanceBody& st, float height) {
    Collider3D* c = StanceCollider(go);
    if (!c || !go->transform || !st.captured || height <= st.curH + 1e-3f) return true;
    Vec3 s = go->transform->LossyScale();
    float half = 0.25f;
    if (auto* b = dynamic_cast<BoxCollider3D*>(c)) half = 0.5f * std::fmin(std::fabs(b->size.x * s.x), std::fabs(b->size.z * s.z));
    else if (auto* k = dynamic_cast<CapsuleCollider3D*>(c)) half = k->radius * std::fabs(s.x);
    half = std::fmax(0.05f, half * 0.9f);
    // Only the part ABOVE the current body needs to be clear.
    float lo = (st.feetY + st.curH) * std::fabs(s.y), hi = (st.feetY + height) * std::fabs(s.y);
    Vec3 base = go->transform->Position();
    Vec3 centre{base.x, base.y + 0.5f * (lo + hi), base.z};
    for (Collider3D* o : sc.physics3D().OverlapBox(sc, centre, {half, 0.5f * (hi - lo), half}, Quat::Identity,
                                                   Physics3D::kAllLayers, false, go))
        if (o && o->gameObject != go) return false;
    return true;
}
inline void ApplyStanceHeight(GameObject* go, StanceBody& st, float height) {
    Collider3D* c = StanceCollider(go);
    if (!c || !st.captured || std::fabs(height - st.curH) < 1e-4f) return;
    if (auto* b = dynamic_cast<BoxCollider3D*>(c)) b->size.y = height;
    else if (auto* k = dynamic_cast<CapsuleCollider3D*>(c)) k->height = std::fmax(height, 2.0f * k->radius);
    c->offset.y = st.feetY + height * 0.5f;
    st.curH = height;
}

} // namespace okay
