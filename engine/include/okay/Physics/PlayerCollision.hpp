#pragma once
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Math/Mathf.hpp"

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

} // namespace okay
