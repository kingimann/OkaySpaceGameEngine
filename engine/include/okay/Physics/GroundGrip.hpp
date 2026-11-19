#pragma once
// How well the ground under a body grips, as a multiplier for a controller's accelerating and braking: 1 on ground with no
// Physics Material (or the Default one), small on Ice, above 1 on Rubber and Sticky. Characters and vehicles scale their
// acceleration, braking and sideways grip by it, so they slide on ice.
// Written from this engine's Physics3D::Raycast and the Physics Material numbers; the reference grip is the Default material's 0.5.
#include "okay/Components/PhysicsSurface.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>

namespace okay {

inline float GroundGripScale(GameObject* self) {
    if (!self || !self->scene() || !self->transform) return 1.0f;
    auto* col = self->GetComponent<Collider3D>();
    Vec3 mn, mx;
    Vec3 p = self->transform->Position();
    if (col) { col->WorldAABB(mn, mx); p = {p.x, mn.y + 0.1f, p.z}; }
    Scene& scene = *self->scene();
    RaycastHit3D h = scene.physics3D().Raycast(scene, p, {0, -1, 0}, 0.6f, self, Physics3D::kAllLayers, false);
    if (!h.hit || !h.gameObject) return 1.0f;
    const PhysicsMaterialData* m = SurfaceOf(h.gameObject);
    if (!m) return 1.0f;
    return std::max(0.03f, std::min(3.0f, m->dynamicFriction / 0.5f));
}

} // namespace okay
