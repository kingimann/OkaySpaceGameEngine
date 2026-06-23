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
    float radius;
    Vec3 center;
    if (auto* bc = self->GetComponent<BoxCollider3D>()) {
        Vec3 h = bc->HalfExtents();
        radius = Mathf::Max(0.05f, Mathf::Min(h.x, h.z));
        center = bc->WorldCenter();
    } else {
        radius = 0.4f;
        center = self->transform->Position() + Vec3{0.0f, 0.9f, 0.0f};
    }
    Vec3 fixed = scene.physics3D().ResolveSphere(scene, center, radius, self);
    Vec3 delta = fixed - center;
    if (delta.SqrMagnitude() > 1e-10f) self->transform->Translate(delta);
}

} // namespace okay
