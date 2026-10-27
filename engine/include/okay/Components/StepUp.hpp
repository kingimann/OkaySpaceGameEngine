#pragma once
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Components/Stance.hpp"
#include <cmath>

namespace okay {

/// The lowest rise the controllers treat as a step. Anything under half a
/// centimetre is a seam in the floor; a door sill or a rug lip is a step.
constexpr float kMinStepHeight = 0.005f;

/// Near-zero vertical speed is not ground on its own: the apex of a jump passes
/// through it. A real contact (or terrain) is ground. The speed heuristic still
/// counts until the body has left the ground, so a resting body with no fresh
/// contact can still jump. A jump, or a real fall, sticks until a contact or
/// until the body has fallen and then settled.
struct GroundLatch {
    bool left = false;   ///< jumped or fell; the |vy| heuristic is off
    bool fell = false;   ///< vy has been a real fall since leaving
};
inline bool ResolveGrounded(GroundLatch& g, bool contact, float vy) {
    if (contact) { g.left = false; g.fell = false; }
    else if (vy < -0.5f) { g.left = true; g.fell = true; }
    else if (g.left && g.fell && std::fabs(vy) < 0.5f) { g.left = false; g.fell = false; }
    return contact || (std::fabs(vy) < 0.5f && !g.left);
}

/// Near-zero vertical speed is not ground on its own: the apex of a jump passes
/// through it. A real contact (or terrain) is ground. The speed heuristic still
/// counts until the body has left the ground, so a resting body with no fresh
/// contact can still jump. A jump, or a real fall, sticks until a contact or
/// until the body has fallen and then settled.
struct GroundLatch {
    bool left = false;   ///< jumped or fell; the |vy| heuristic is off
    bool fell = false;   ///< vy has been a real fall since leaving
};
inline bool ResolveGrounded(GroundLatch& g, bool contact, float vy) {
    if (contact) { g.left = false; g.fell = false; }
    else if (vy < -0.5f) { g.left = true; g.fell = true; }
    else if (g.left && g.fell && std::fabs(vy) < 0.5f) { g.left = false; g.fell = false; }
    return contact || (std::fabs(vy) < 0.5f && !g.left);
}

/// Stair step-up shared by the character controllers: when a GROUNDED, moving
/// body is blocked by a LOW obstacle (a stair, a curb), lift it onto the step
/// instead of letting it grind against the face. Three probes decide it:
/// a ray just off the floor must hit a wall-like face (something is in the way; a
/// ramp or a hill ahead is walkable ground, not a step), the same ray at
/// step-offset height must be CLEAR (it's a step, not a wall), and a down-ray
/// past the lip must find the step top between minStepHeight and stepOffset
/// above the feet. Only solid colliders count (no triggers), and never the
/// body's own parts. It lifts only: the body's own speed carries it over the lip.
inline void TryStepUp(Scene& sc, GameObject* go, Rigidbody3D* rb, const Vec3& dir,
                      bool grounded, bool moving, float stepOffset, float minStepHeight = kMinStepHeight) {
    if (!grounded || !moving || stepOffset <= 0.0f || !rb || !go || !go->transform) return;
    Vec3 pos = go->transform->Position();
    const float reach = 0.45f;                      // capsule radius + skin
    Vec3 fw{dir.x, 0.0f, dir.z};
    float fl = std::sqrt(fw.x * fw.x + fw.z * fw.z);
    if (fl < 1e-4f) return;
    fw.x /= fl; fw.z /= fl;
    Physics3D::IgnoreHierarchy own(go);
    auto& ph = sc.physics3D();
    // Low enough to meet a 1 cm sill. A walkable slope still has a steep
    // normal and is rejected below; a rise under kMinStepHeight is a seam.
    RaycastHit3D low = ph.Raycast(sc, {pos.x, pos.y + 0.005f, pos.z}, fw, reach, go, Physics3D::kAllLayers, false);
    if (!low.hit) return;                           // nothing in the way
    if (std::fabs(low.normal.y) >= 0.3f) return;    // a slope or a seam ahead, not a step's face
    RaycastHit3D high = ph.Raycast(sc, {pos.x, pos.y + stepOffset + 0.02f, pos.z},
                                   fw, reach + 0.10f, go, Physics3D::kAllLayers, false);
    if (high.hit) return;                           // taller than a step -> a real wall
    Vec3 overO{pos.x + fw.x * (low.distance + 0.12f), pos.y + stepOffset + 0.02f,
               pos.z + fw.z * (low.distance + 0.12f)};
    RaycastHit3D top = ph.Raycast(sc, overO, {0.0f, -1.0f, 0.0f}, stepOffset + 0.04f, go, Physics3D::kAllLayers, false);
    if (!top.hit) return;
    float rise = top.point.y - pos.y;
    if (rise < minStepHeight || rise > stepOffset) return;
    // Climb: lift the body onto the step's height (no forward push: its own
    // velocity takes it over the lip, at any frame rate).
    go->transform->SetPosition({pos.x, top.point.y + 0.02f, pos.z});
    if (rb->velocity.y < 0.0f) rb->velocity.y = 0.0f;
}


/// Moving-platform support shared by the controllers: remember what the body
/// stands on and apply the platform's frame-to-frame translation, so players
/// RIDE elevators and moving props instead of sliding off them. Static ground
/// has a zero delta, so tracking it costs nothing. Only solid, non-dynamic
/// things are ridden: a trigger or a crate the player is pushing never is.
struct PlatformRide {
    GameObject* platform = nullptr;
    Vec3 lastPos{0.0f, 0.0f, 0.0f};
    Vec3 applied{0.0f, 0.0f, 0.0f};   ///< translation added to the rider this call
};
inline void RideMovingPlatform(Scene& sc, GameObject* go, PlatformRide& st, bool grounded) {
    st.applied = {0.0f, 0.0f, 0.0f};
    if (!go || !go->transform) { st.platform = nullptr; return; }
    GameObject* plat = nullptr;
    if (grounded) {
        Vec3 pos = go->transform->Position();
        Physics3D::IgnoreHierarchy own(go);
        RaycastHit3D hit = sc.physics3D().Raycast(sc, {pos.x, pos.y + 0.1f, pos.z},
                                                  {0.0f, -1.0f, 0.0f}, 1.4f, go, Physics3D::kAllLayers, false);
        if (hit.hit && hit.gameObject && hit.gameObject->transform) {
            const auto* prb = hit.gameObject->GetComponent<Rigidbody3D>();
            if (!prb || prb->bodyType != Rigidbody3D::BodyType::Dynamic) plat = hit.gameObject;
        }
    }
    if (plat && plat == st.platform) {
        Vec3 cur = plat->transform->Position();
        Vec3 d{cur.x - st.lastPos.x, cur.y - st.lastPos.y, cur.z - st.lastPos.z};
        if (d.x != 0.0f || d.y != 0.0f || d.z != 0.0f) {
            go->transform->SetPosition(go->transform->Position() + d);
            st.applied = d;
        }
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
    // A capsule can't be shorter than its two end caps; centre the height it
    // really has so its bottom stays on the feet.
    float h = height;
    if (auto* b = dynamic_cast<BoxCollider3D*>(c)) b->size.y = h;
    else if (auto* k = dynamic_cast<CapsuleCollider3D*>(c)) { h = std::fmax(height, 2.0f * k->radius); k->height = h; }
    c->offset.y = st.feetY + h * 0.5f;
    st.curH = h;
}
/// The stance (0 stand, 1 crouch, 2 prone) the body may take when `want` is
/// asked for while it is `cur`: rising needs headroom for the target height,
/// and from prone a crouch will do if only that fits. `crouchFrac` /
/// `proneFrac` are the stance heights as fractions of standing.
inline int SettleStance(GameObject* go, StanceBody& st, int cur, int want, float crouchFrac, float proneFrac) {
    Scene* sc = go ? go->scene() : nullptr;
    CaptureStance(go, st);
    if (!sc || !st.captured) return want;
    auto height = [&](int s) { return st.standH * (s == 2 ? proneFrac : (s == 1 ? crouchFrac : 1.0f)); };
    if (height(want) <= st.curH + 1e-3f || StanceHasRoom(*sc, go, st, height(want))) return want;
    if (want == 0 && cur == 2 && StanceHasRoom(*sc, go, st, height(1))) return 1;
    return cur;
}
/// Ease the stance level toward `stance` and size the collider for it (feet
/// fixed; only the collider moves, never the body).
inline void EaseStance(GameObject* go, StanceBody& st, float& level, int stance, float dt, const StanceTimes& times,
                       bool resize, float crouchFrac, float proneFrac) {
    level = StepStanceLevel(level, stance, dt, times);
    if (!resize || !go) return;
    CaptureStance(go, st);
    ApplyStanceHeight(go, st, st.standH * StanceValue(level, 1.0f, crouchFrac, proneFrac));
}

} // namespace okay
