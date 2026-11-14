#include "okay/AI/Perception.hpp"

#include "okay/Physics/Physics3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"

#include <cmath>

namespace okay {

float HorizontalDistance(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

bool InSightCone(const Vec3& origin, const Vec3& forward, const Vec3& targetPosition,
                 float fieldOfViewDegrees) {
    if (fieldOfViewDegrees >= 359.0f) return true;
    const float dx = targetPosition.x - origin.x;
    const float dz = targetPosition.z - origin.z;
    const float len = std::sqrt(dx * dx + dz * dz);
    // On top of the target there is no direction to be outside the cone.
    if (len <= 1e-5f) return true;
    const float tx = dx / len, tz = dz / len;
    const float fm = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    if (fm < 1e-5f) return true;   // facing straight up or down: no horizontal cone
    float dot = (tx * forward.x + tz * forward.z) / fm;
    if (dot > 1.0f) dot = 1.0f;
    if (dot < -1.0f) dot = -1.0f;
    return std::acos(dot) * 57.29578f <= fieldOfViewDegrees * 0.5f;
}

bool SightLineClear(Scene* scene, const Vec3& from, const Vec3& targetPosition,
                    GameObject* self, GameObject* target, float eyeHeight) {
    if (!scene) return true;
    const Vec3 o{from.x, from.y + eyeHeight, from.z};
    const Vec3 e{targetPosition.x, targetPosition.y + eyeHeight, targetPosition.z};
    Vec3 d = e - o;
    const float m = d.Magnitude();
    if (m < 1e-4f) return true;
    d = d * (1.0f / m);
    // Triggers are volumes, not walls. The ray runs the full eye-to-eye length;
    // a hit in the last 0.3 units is the quarry's body, not a wall in the way.
    const RaycastHit3D hit = scene->physics3D().Raycast(
        *scene, o, d, m, self, Physics3D::kAllLayers, /*queryTriggers=*/false);
    if (!hit) return true;
    if (target && hit.gameObject &&
        (hit.gameObject == target || hit.gameObject->IsSelfOrDescendantOf(target)))
        return true;
    return hit.distance >= m - 0.3f;
}

SenseResult Sense(Scene* scene, const Vec3& origin, const Vec3& forward,
                  const Vec3& targetPosition, GameObject* self, GameObject* target,
                  const SenseSettings& settings) {
    SenseResult r;
    r.distance = HorizontalDistance(origin, targetPosition);
    r.inRange = r.distance <= settings.sightRange;
    r.inCone = InSightCone(origin, forward, targetPosition, settings.fieldOfView);
    if (settings.lineOfSight)
        r.lineClear = SightLineClear(scene, origin, targetPosition, self, target, settings.eyeHeight);
    r.canSee = r.inRange && r.inCone && r.lineClear;
    r.heard = settings.hearingRange > 0.0f && r.distance <= settings.hearingRange;
    r.noticed = r.canSee || r.heard;
    return r;
}

} // namespace okay
