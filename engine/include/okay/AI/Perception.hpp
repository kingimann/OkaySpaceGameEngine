#pragma once
#include "okay/Math/Vec3.hpp"

namespace okay {

class Scene;
class GameObject;

/// The numbers for one sight and hearing check. The NPC Controller and a
/// behavior tree's Perception node each keep their own copy of these; the
/// check itself is `Sense`.
struct SenseSettings {
    float sightRange  = 8.0f;    ///< horizontal distance (X/Z) at which the target can be seen
    float fieldOfView = 200.0f;  ///< full cone width in degrees (360 sees all around)
    bool  lineOfSight = false;   ///< a solid collider between the eyes blocks sight
    float eyeHeight   = 1.2f;    ///< eyes above each object's origin, for the sight ray
    float hearingRange = 0.0f;   ///< notices the target inside this radius, cone and walls ignored (0 = deaf)
};

/// What `Sense` found. `canSee` is range, cone and (when asked) a clear line.
/// `heard` ignores the cone and walls. `noticed` is either.
struct SenseResult {
    float distance  = 0.0f;   ///< horizontal distance (X/Z)
    bool  inRange   = false;
    bool  inCone    = false;
    bool  lineClear = true;   ///< true when line of sight is off, or the ray is clear
    bool  canSee    = false;
    bool  heard     = false;
    bool  noticed   = false;
};

/// Horizontal distance on X/Z. Sight range, the cone and hearing all use this,
/// so a target on a ledge is not "far" just because it is above you.
float HorizontalDistance(const Vec3& a, const Vec3& b);

/// True when `targetPosition` lies inside a cone of `fieldOfViewDegrees` (full
/// width) around the horizontal part of `forward`. A cone of 359 degrees or
/// more sees all around. Standing on the target, or facing straight up or
/// down, counts as inside.
bool InSightCone(const Vec3& origin, const Vec3& forward, const Vec3& targetPosition,
                 float fieldOfViewDegrees);

/// True when no solid collider stands between the two eye points. Trigger
/// volumes do not count (they are not walls). A hit on `target`, or on one of
/// its children, does not count either, and neither does a hit in the last
/// 0.3 units of the ray, so the quarry's own body is not a wall. `self` is
/// skipped. With no scene there is nothing to block the ray.
bool SightLineClear(Scene* scene, const Vec3& from, const Vec3& targetPosition,
                    GameObject* self, GameObject* target, float eyeHeight);

/// One sight and hearing check. `forward` is the facing the caller already
/// uses: an NPC Controller passes its local facing (that is what it turns),
/// a behavior tree passes the object's world facing. Given the same origin,
/// facing and settings, every caller gets the same answer.
SenseResult Sense(Scene* scene, const Vec3& origin, const Vec3& forward,
                  const Vec3& targetPosition, GameObject* self, GameObject* target,
                  const SenseSettings& settings);

} // namespace okay
