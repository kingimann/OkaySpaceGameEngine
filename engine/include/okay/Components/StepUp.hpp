#pragma once
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"

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

} // namespace okay
