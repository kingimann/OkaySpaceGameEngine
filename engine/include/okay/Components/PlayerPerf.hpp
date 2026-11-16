#pragma once
// ---------------------------------------------------------------------------
// PlayerPerf: the small pieces that keep the player controllers cheap and honest every frame.
//
//  * OldPlayerPath()      OKAY_OLD_PLAYER=1 (or SetOldPlayerPath(true)) brings back the controllers' earlier
//                         per-frame work and the earlier body behaviour, for comparing a change side by side.
//  * SceneStamp           the scene's change counters (the same three the physics query cache keys on). A lookup
//                         that was done while they had these values is still right while they stay.
//  * SceneHas<T>          is there any T in the scene, asked once per scene change instead of once per call.
//  * PlayerLookups        the components one controller reads every frame (its body, its camera child, its
//                         Character, its Player View), found again only when the scene changed.
//
// Everything here reads the scene only. Nothing changes what a controller decides: the same queries give the
// same answers, they are asked less often. Written from scratch for this engine; no outside code.
// ---------------------------------------------------------------------------
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Water.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace okay {

class Camera;
class Character;
class PlayerView;
class Rigidbody3D;

/// OKAY_OLD_PLAYER=1 in the environment (read once) turns the old path on for the whole process.
inline bool& OldPlayerPathSlot() {
    static bool on = [] { const char* e = std::getenv("OKAY_OLD_PLAYER"); return e && *e && *e != '0'; }();
    return on;
}
inline bool OldPlayerPath() { return OldPlayerPathSlot(); }
inline void SetOldPlayerPath(bool on) { OldPlayerPathSlot() = on; }

/// The scene's change counters. A cached answer taken at one stamp is right until the stamp moves: a
/// component added or removed, an object created, destroyed or re-parented all move it.
struct SceneStamp {
    std::uint64_t uid = 0, structure = 0, components = 0;
    bool Current(const Scene& s) const {
        return uid == s.Uid() && structure == s.StructureVersion() && components == s.ComponentVersion();
    }
    void Take(const Scene& s) { uid = s.Uid(); structure = s.StructureVersion(); components = s.ComponentVersion(); }
};

/// The water volumes of the scene, found once per scene change. Main thread only, like every scene call.
inline const std::vector<Water*>& SceneWaters(const Scene& sc) {
    static SceneStamp stamp;
    static std::vector<Water*> list;
    if (!stamp.Current(sc)) { list = sc.FindObjectsOfType<Water>(); stamp.Take(sc); }
    return list;
}

inline const std::vector<Collider3D*>& SceneColliders(const Scene& sc);

/// Trigger boxes on objects tagged "Climbable": ladders and climb volumes. Found once per scene change.
inline const std::vector<Collider3D*>& SceneClimbables(const Scene& sc) {
    static SceneStamp stamp;
    static std::vector<Collider3D*> list;
    if (!stamp.Current(sc)) {
        list.clear();
        for (Collider3D* c : SceneColliders(sc))
            if (c && c->isTrigger && c->gameObject && c->gameObject->tag == "Climbable") list.push_back(c);
        stamp.Take(sc);
    }
    return list;
}
/// Is the world point inside a climb volume (a box trigger tagged "Climbable")?
inline bool InClimbVolume(const Scene& sc, const Vec3& p) {
    for (const Collider3D* c : SceneClimbables(sc)) {
        if (!c->enabled || !c->gameObject->ActiveInHierarchy() || !c->transform) continue;
        const auto* b = dynamic_cast<const BoxCollider3D*>(c);
        if (!b) continue;
        const Vec3 l = c->transform->InverseTransformPoint(p);
        const Vec3 h = b->size * 0.5f;
        if (std::fabs(l.x - c->offset.x) <= h.x && std::fabs(l.y - c->offset.y) <= h.y && std::fabs(l.z - c->offset.z) <= h.z) return true;
    }
    return false;
}

/// Water::ProbeAt, without searching the whole scene each call (the old path does, with OKAY_OLD_PLAYER=1).
inline Water::Probe ProbeWater(const Scene* sc, const Vec3& feet, float bodyHeight) {
    if (!sc) return Water::Probe{};
    if (OldPlayerPath()) return Water::ProbeAt(sc, feet, bodyHeight);
    return Water::ProbeIn(SceneWaters(*sc), feet, bodyHeight);
}

/// True when the scene has a heightmap Terrain (a ray can hit one, an overlap box cannot).
inline bool SceneHasTerrain(const Scene& sc) {
    static SceneStamp stamp;
    static bool any = false;
    if (!stamp.Current(sc)) { any = sc.FindObjectOfType<Terrain>() != nullptr; stamp.Take(sc); }
    return any;
}

/// Every Collider3D of the scene, found once per scene change (the physics queries keep the same kind of list).
inline const std::vector<Collider3D*>& SceneColliders(const Scene& sc) {
    static SceneStamp stamp;
    static std::vector<Collider3D*> list;
    if (!stamp.Current(sc)) { list = sc.FindObjectsOfType<Collider3D>(); stamp.Take(sc); }
    return list;
}

/// Could a solid collider (alive, not a trigger, not part of `self`) reach into the world box [mn, mx]?
/// A bounds test over the scene's collider list, so false is certain (no overlap query can find anything
/// there) and true only means "look closer". A collider whose bounds are not numbers counts as near, so a
/// strange one never hides behind this test.
inline bool AnySolidNear(const Scene& sc, const GameObject* self, const Vec3& mn, const Vec3& mx) {
    for (const Collider3D* c : SceneColliders(sc)) {
        if (!c || !c->enabled || c->isTrigger || !c->gameObject || !c->gameObject->ActiveInHierarchy()) continue;
        if (self && c->gameObject->IsSelfOrDescendantOf(self)) continue;
        Vec3 lo, hi;
        c->WorldAABB(lo, hi);
        const bool finite = std::isfinite(lo.x + lo.y + lo.z + hi.x + hi.y + hi.z);
        if (!finite) return true;
        if (hi.x < mn.x || lo.x > mx.x || hi.y < mn.y || lo.y > mx.y || hi.z < mn.z || lo.z > mx.z) continue;
        return true;
    }
    return false;
}

/// What a controller learns about the ground from the contacts the physics step reports. The contact normal
/// points from the body toward the other collider, so the ground under the feet has a normal that points down
/// (y < -0.5), whatever the other object's pivot is, and the walkable surface normal is its opposite. Only the
/// flattest ground contact of a frame counts (a body standing at the foot of a ramp touches the floor too).
struct GroundSense {
    Vec3  normal{0.0f, 1.0f, 0.0f};   ///< surface normal of the ground, pointing up
    float age = 1e9f;                 ///< seconds since a ground contact was reported
    /// Call from OnCollisionEnter/Stay3D. True when the contact is ground.
    bool Note(const Vec3& contactNormal) {
        if (contactNormal.y > -0.5f) return false;
        const Vec3 n = contactNormal * -1.0f;
        if (!m_have || n.y > m_pending.y) { m_pending = n; m_have = true; }
        return true;
    }
    /// Once per Simulate: take what the physics reported since the last call, or let the old normal age.
    void Step(float dt) {
        if (m_have) { normal = m_pending; age = 0.0f; m_have = false; }
        else age += dt;
    }
    bool Fresh() const { return age < 0.12f; }
    /// Degrees from flat of the ground the body is on (0 when unknown).
    float SlopeDegrees() const {
        if (!Fresh()) return 0.0f;
        const float y = normal.y < 0.0f ? 0.0f : (normal.y > 1.0f ? 1.0f : normal.y);
        return std::acos(y) * 57.29578f;
    }
private:
    Vec3 m_pending{0.0f, 1.0f, 0.0f};
    bool m_have = false;
};

/// The wall a body is pressed against (a horizontal contact normal), so that pushing into it asks for the slide
/// along it. Without this the steering that drops the old heading cancelled the slide every frame: a body
/// walking at a wall at 30 degrees crept along it at a third of the speed a frictionless slide gives.
/// A thing the body can push out of its way: another physics body that is not fixed (a crate, a ball). The contact solver
/// answers a push into one (it gives way, or it does not), so it is not a wall to slide along. Treating it as a wall removed
/// the push from the wish every frame the two touched, and a walker pushing a light crate head-on lost a fifth of its speed
/// to a stutter (push, lose the contact, push again).
inline bool IsPushableBody(const GameObject* other) {
    if (!other) return false;
    const auto* rb = other->GetComponent<Rigidbody3D>();
    return rb && rb->bodyType == Rigidbody3D::BodyType::Dynamic;
}

struct WallSense {
    void Note(const Vec3& contactNormal) {   // the contact normal points from the body toward the wall
        const float l = std::sqrt(contactNormal.x * contactNormal.x + contactNormal.z * contactNormal.z);
        if (l < 1e-4f) return;
        m_n = Vec3{-contactNormal.x / l, 0.0f, -contactNormal.z / l};   // out of the wall, toward the body
        m_have = true;
    }
    void Step(float dt) { if (m_have) { m_age = 0.0f; m_have = false; m_cur = m_n; } else m_age += dt; }
    /// Remove the part of the wish that pushes into the wall (fresh contact only).
    void SlideAlong(float& wx, float& wz) const {
        if (m_age > 0.1f) return;
        const float into = wx * m_cur.x + wz * m_cur.z;
        if (into >= 0.0f) return;
        wx -= m_cur.x * into; wz -= m_cur.z * into;
    }
private:
    Vec3 m_n{0, 0, 0}, m_cur{0, 0, 0};
    float m_age = 1e9f;
    bool m_have = false;
};

/// What a controller saw and did on its last step, for the on-screen player numbers (see PlayerDebug.hpp).
struct PlayerDebugState {
    bool  valid = false;
    bool  grounded = false;
    bool  moving = false, running = false, swimming = false, snapped = false;
    int   stance = 0;
    float speed = 0.0f;               ///< horizontal speed, m/s
    float slopeDeg = 0.0f;            ///< slope of the ground under the feet, degrees
    float slopeLimit = 0.0f;
    Vec3  velocity{0.0f, 0.0f, 0.0f};
    Vec3  groundNormal{0.0f, 1.0f, 0.0f};
    const GameObject* platform = nullptr;   ///< the thing stood on (floor, platform), if one was found
    bool  platformMoves = false;
    bool  held = false;               ///< held in place on a slope
    bool  terrainGround = false;
    bool  climbing = false;
    float lastLandSpeed = 0.0f;       ///< speed of the last landing, m/s
    int   hardLandings = 0;           ///< landings at or over the hard-landing speed
};

} // namespace okay
