#pragma once
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

// How far a sideways push may be turned into a lift on a slope (a body 1 cm into a ramp is lifted out, never slid down it).
constexpr float kSeatLift = 0.35f;

// A sideways push from a walkable floor or a low riser is turned into a step up, so a body
// resting in a slope is lifted straight out (no downhill creep) and a kerb no taller than
// `stepUp` is walked onto. A wall that still blocks at that height keeps the raw push, and so does a
// ledge taller than `stepUp`: the body is never left hanging on its corner above the floor.
// `bottom` is the distance from `center` to the feet, `radius` the capsule radius.
template <class F>
Vec3 SeatUpright(Scene& scene, GameObject* self, const Vec3& center, float bottom, float radius, float stepUp, F resolveAt) {
    Vec3 fixed = resolveAt(center);
    Vec3 d = fixed - center;
    if (d.x * d.x + d.z * d.z <= 1e-8f) return fixed;
    Vec3 raised = center + Vec3{0.0f, kSeatLift, 0.0f};
    Vec3 rf = resolveAt(raised);
    Vec3 rd = rf - raised;
    if (rd.x * rd.x + rd.z * rd.z > 2.5e-3f) return fixed;
    RaycastHit3D g = scene.physics3D().Raycast(scene, rf, Vec3{0.0f, -1.0f, 0.0f},
                                               kSeatLift + bottom + 0.2f, self, Physics3D::kAllLayers, false);
    if (!g.hit || g.normal.y < 0.35f) return fixed;
    float drop = g.distance - bottom;
    if (drop < -0.05f || drop > kSeatLift + 0.15f) return fixed;
    Vec3 landed{center.x, rf.y - drop, center.z};
    bool settled = false;
    for (int k = 0; k < 5 && !settled; ++k) {
        Vec3 check = resolveAt(landed);
        float cx = check.x - landed.x, cy = check.y - landed.y, cz = check.z - landed.z;
        if (cx * cx + cz * cz < 1e-6f && cy < 0.001f && cy > -0.001f) { settled = true; break; }
        float up = std::sqrt(cx * cx + cy * cy + cz * cz);
        if (!(up > 1e-5f)) break;
        landed.x = center.x;
        landed.y += up;
        landed.z = center.z;
    }
    // What is the body now standing on? Look at the surface in front of it, on the side the push came from. The slope
    // the body was pushed out of is walked up. Any other top (flat, a little tilted, round, uneven) is a ledge and is
    // only climbed when it is no taller than a step, from where the feet were: a body is never lifted onto the corner
    // of a ledge or crate that is higher than that, and left hanging there.
    const float hx = -d.x, hz = -d.z;
    const float hl = std::sqrt(hx * hx + hz * hz);
    const float rise = landed.y - center.y;
    if (rise > kSeatLift + 0.01f) return fixed;
    const float feetY = landed.y - bottom;
    Vec3 probe{landed.x + (hl > 1e-6f ? hx / hl : 0.0f) * radius * 1.3f, feetY + 0.9f,
               landed.z + (hl > 1e-6f ? hz / hl : 0.0f) * radius * 1.3f};
    RaycastHit3D top = scene.physics3D().Raycast(scene, probe, Vec3{0.0f, -1.0f, 0.0f}, 1.3f, self,
                                                 Physics3D::kAllLayers, false);
    if (!top.hit || top.normal.y < 0.35f) return fixed;
    Vec3 tn = top.normal;
    const float tl = tn.Magnitude();
    if (!(tl > 1e-6f)) return fixed;
    tn = tn * (1.0f / tl);
    // The same slope that pushed? Then the push points the way the top faces, and the round foot was no deeper under
    // the top's plane than that push explains. A ledge pushes from its side, with the foot far under its top.
    const float feet = center.y - bottom;
    const float pushed = d.Magnitude();
    const float under = radius - Vec3::Dot(Vec3{center.x, feet + radius, center.z} - top.point, tn);
    const bool sameSlope = Vec3::Dot(d, tn) > 0.9f * pushed && under <= pushed * 1.5f + 0.03f;
    // A ledge: how far its top is above the feet, less what the top's own tilt adds between the body and the probe.
    const float tilt = radius * 1.3f * std::sqrt(Mathf::Max(0.0f, 1.0f - tn.y * tn.y)) / tn.y;
    if (!sameSlope && top.point.y - feet > stepUp + 0.02f + tilt) return fixed;   // a top taller than a step
    return landed;
}

/// Keep a transform-moved player (no Rigidbody3D) from clipping through walls, floors or ceilings.
/// A capsule, and a tall box (the usual character), is pushed out as a capsule: the head meets a
/// ceiling and the feet meet a floor. That push is the default. On a walkable slope the separation
/// is straight up, so standing still does not slide downhill, and a riser lower than a step is
/// walked over. `stepUp` is the tallest riser it climbs (MovePlayerBody passes its stepHeight, the first- and
/// third-person controllers their stepOffset), at most kSeatLift; a slope is always walked. A foot is round: a
/// kerb lower than the body's radius is still rolled over. A cube stays a sphere at the waist. Call right after
/// moving the Transform.
inline void ResolvePlayerBody(Scene& scene, GameObject* self, float stepUp = kSeatLift) {
    if (!self || !self->transform) return;
    auto shift = [&](const Vec3& from, const Vec3& fixed) {
        Vec3 delta = fixed - from;
        if (delta.SqrMagnitude() > 1e-10f) self->transform->Translate(delta);
    };
    if (auto* cap = self->GetComponent<CapsuleCollider3D>()) {
        if (cap->shape() == Collider3D::Shape::Capsule) {
            Vec3 a, b;
            cap->Segment(a, b);
            Vec3 center = (a + b) * 0.5f;
            Vec3 axis = b - a;
            float half = axis.Magnitude() * 0.5f;
            float radius = cap->WorldRadius();
            auto resolveAt = [&](const Vec3& c) {
                return scene.physics3D().ResolveCapsule(scene, c, axis, half, radius, self);
            };
            shift(center, SeatUpright(scene, self, center, half + radius, radius, stepUp, resolveAt));
            return;
        }
    }
    if (auto* bc = self->GetComponent<BoxCollider3D>()) {
        Vec3 h = bc->HalfExtents();
        float radius = Mathf::Max(0.05f, Mathf::Min(h.x, h.z));
        Vec3 center = bc->WorldCenter();
        float half = h.y - radius;
        if (half > 0.02f) {
            auto resolveAt = [&](const Vec3& c) {
                return scene.physics3D().ResolveCapsule(scene, c, Vec3{0, 1, 0}, half, radius, self);
            };
            shift(center, SeatUpright(scene, self, center, half + radius, radius, stepUp, resolveAt));
        } else {
            shift(center, scene.physics3D().ResolveSphere(scene, center, radius, self));
        }
        return;
    }
    Vec3 center = self->transform->Position() + Vec3{0.0f, 0.9f, 0.0f};
    shift(center, scene.physics3D().ResolveSphere(scene, center, 0.4f, self));
}

/// Where the body's feet are: the bottom of its collider under the pivot, or the pivot when it has no collider.
/// (A box is usually centred on its Transform, so the pivot is half a body above the floor.)
inline Vec3 FeetOf(GameObject* self) {
    Vec3 p = self->transform->Position();
    if (Collider3D* col = self->GetComponent<Collider3D>()) {
        Vec3 mn, mx;
        col->WorldAABB(mn, mx);
        if (std::isfinite(mn.y + mx.y + mn.x + mx.x + mn.z + mx.z)) return Vec3{(mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f};
    }
    return p;
}

/// Degrees between `normal` and straight up. A flat floor is 0. A wall is 90.
inline float GroundSlopeDegrees(const Vec3& normal) {
    float mag = normal.Magnitude();
    float up = mag > 1e-6f ? normal.y / mag : 1.0f;
    if (up > 1.0f) up = 1.0f;
    if (up < -1.0f) up = -1.0f;
    return std::acos(up) * (180.0f / 3.14159265f);
}

/// Move a transform-driven character by `delta`, then push it out of the world.
/// A ledge no taller than `stepHeight` is stepped onto instead of blocking the move.
/// Ground steeper than `maxSlopeDegrees` slides the body downhill. A cube or a capsule
/// body uses the same push-out as ResolvePlayerBody.
inline void MovePlayerBody(Scene& scene, GameObject* self, const Vec3& delta,
                           float stepHeight = 0.3f, float maxSlopeDegrees = 46.0f) {
    if (!self || !self->transform) return;
    const Vec3 start = self->transform->Position();
    auto place = [&](const Vec3& p) {
        self->transform->SetPosition(p);
        ResolvePlayerBody(scene, self, stepHeight);
        return self->transform->Position();
    };
    auto along = [](const Vec3& a, const Vec3& b) {
        Vec3 d{b.x - a.x, 0.0f, b.z - a.z};
        return d.Magnitude();
    };
    Vec3 landed = place(start + delta);
    const Vec3 wish{delta.x, 0.0f, delta.z};
    const float wishLen = wish.Magnitude();
    if (stepHeight > 0.02f && wishLen > 1e-4f && along(start, landed) < wishLen * 0.65f) {
        const Vec3 blocked = landed;
        Vec3 raised = place(start + Vec3{0.0f, stepHeight, 0.0f} + wish);
        Vec3 dropped = place(Vec3{raised.x, raised.y - stepHeight, raised.z});
        float rise = dropped.y - start.y;
        if (rise <= stepHeight + 0.05f && rise >= -0.08f && along(start, dropped) > along(start, blocked) + 0.02f)
            landed = dropped;
        else
            self->transform->SetPosition(blocked);
    }
    const Vec3 footOffset = FeetOf(self) - self->transform->Position();   // 0 for a pivot at the feet
    Vec3 feet = FeetOf(self);
    RaycastHit3D ground = scene.physics3D().Raycast(scene, feet + Vec3{0.0f, 0.2f, 0.0f}, {0.0f, -1.0f, 0.0f},
                                                    0.8f, self, Physics3D::kAllLayers, false);
    if (ground.hit && GroundSlopeDegrees(ground.normal) > maxSlopeDegrees) {
        Vec3 n = ground.normal;
        float mag = n.Magnitude();
        if (mag > 1e-6f) n = n * (1.0f / mag);
        Vec3 down{0.0f, -1.0f, 0.0f};
        Vec3 slide = down - n * Vec3::Dot(down, n);
        float sl = slide.Magnitude();
        if (sl > 1e-4f) {
            // Move along the slope, then sit back on it. A full push-out after a
            // move into the solid would cancel the slide.
            Vec3 pos = self->transform->Position() + slide * (0.5f / sl);
            self->transform->SetPosition(pos);
            RaycastHit3D seat = scene.physics3D().Raycast(scene, pos + footOffset + Vec3{0.0f, 0.4f, 0.0f}, {0.0f, -1.0f, 0.0f},
                                                          1.5f, self, Physics3D::kAllLayers, false);
            if (seat.hit && seat.distance > 0.05f)
                self->transform->SetPosition(self->transform->Position() + Vec3{0.0f, -(seat.distance - 0.05f), 0.0f});
        }
    }
}

} // namespace okay
