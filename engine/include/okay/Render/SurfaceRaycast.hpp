#pragma once
// Ray casts against the triangles of the scene's mesh renderers: what the foliage brush, the decal
// projector and the measuring tool use to find "the surface under here". It needs no colliders. Objects
// are skipped when their world box misses the ray, so a scene of many props costs a few box tests plus the
// triangles of what the ray actually passes through.
#include "okay/Math/Vec3.hpp"
#include <functional>

namespace okay {

class Scene;
class GameObject;

struct SurfaceHit {
    Vec3 position;                 ///< world point on the surface
    Vec3 normal{0, 1, 0};          ///< world-space unit normal, facing the ray's origin
    float distance = 0.0f;
    GameObject* object = nullptr;
};

/// First surface hit by the ray (origin, unit direction) within `maxDistance`. `skip` (optional)
/// returns true for objects that must not count (the tool's own objects). Skinned meshes and objects with no
/// mesh are ignored; inactive objects too.
bool RaycastSurfaces(Scene& scene, const Vec3& origin, const Vec3& dir, float maxDistance, SurfaceHit& hit,
                     const std::function<bool(const GameObject&)>& skip = {});

} // namespace okay
