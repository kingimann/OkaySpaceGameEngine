#pragma once
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

/// Keep a transform-moved player (no Rigidbody3D) from clipping through walls/floors:
/// approximate the body as a sphere and push it out of any solid geometry, then shift
/// the player by that correction. Uses the player's BoxCollider3D for the body size
/// when present, else a sensible default. Call right after moving the Transform.
inline void ResolvePlayerBody(Scene& scene, GameObject* self) {
    if (!self || !self->transform) return;
    auto shift = [&](const Vec3& from, const Vec3& fixed) {
        Vec3 delta = fixed - from;
        if (delta.SqrMagnitude() > 1e-10f) self->transform->Translate(delta);
    };
    // A real capsule (or a tall box, which is the usual character stand-in) is pushed out as a capsule,
    // so the head meets a ceiling and the feet meet a floor. A cube stays the old waist sphere.
    if (auto* cap = self->GetComponent<CapsuleCollider3D>()) {
        if (cap->shape() == Collider3D::Shape::Capsule) {
            Vec3 a, b;
            cap->Segment(a, b);
            Vec3 center = (a + b) * 0.5f;
            Vec3 axis = b - a;
            float half = axis.Magnitude() * 0.5f;
            shift(center, scene.physics3D().ResolveCapsule(scene, center, axis, half, cap->WorldRadius(), self));
            return;
        }
    }
    if (auto* bc = self->GetComponent<BoxCollider3D>()) {
        Vec3 h = bc->HalfExtents();
        float radius = Mathf::Max(0.05f, Mathf::Min(h.x, h.z));
        Vec3 center = bc->WorldCenter();
        float half = h.y - radius;
        if (half > 0.02f)
            shift(center, scene.physics3D().ResolveCapsule(scene, center, Vec3{0, 1, 0}, half, radius, self));
        else
            shift(center, scene.physics3D().ResolveSphere(scene, center, radius, self));
        return;
    }
    Vec3 center = self->transform->Position() + Vec3{0.0f, 0.9f, 0.0f};
    shift(center, scene.physics3D().ResolveSphere(scene, center, 0.4f, self));
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
        ResolvePlayerBody(scene, self);
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
    Vec3 feet = self->transform->Position();
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
            RaycastHit3D seat = scene.physics3D().Raycast(scene, pos + Vec3{0.0f, 0.4f, 0.0f}, {0.0f, -1.0f, 0.0f},
                                                          1.5f, self, Physics3D::kAllLayers, false);
            if (seat.hit && seat.distance > 0.05f)
                self->transform->SetPosition(self->transform->Position() + Vec3{0.0f, -(seat.distance - 0.05f), 0.0f});
        }
    }
}

} // namespace okay
