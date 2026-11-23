#pragma once
// The way a Visual Script object faces: its own -Z axis.
//
// This is the engine's character and camera convention, not a new one: the Camera view looks down -Z,
// VirtualCamera::ViewForward is `rotation * (0,0,-1)`, and CharacterController3D, ThirdPersonController and
// FirstPersonController all read the facing as `rotation * (0,0,-1)`. The Look At and Rotate Towards nodes
// turn -Z toward their target. Transform::Forward() is the plain maths +Z axis and Transform::LookAt turns
// +Z, so nodes that mean "the way this object faces" use these helpers instead (until 2026-11-23 several
// nodes used +Z, and Look At followed by Shoot fired backwards).
#include "okay/Scene/Transform.hpp"

namespace okay::vs {

/// The world direction a rotation faces (its -Z).
inline Vec3 VsFacing(const Quat& worldRotation) { return worldRotation * Vec3::Back; }
/// The world direction this transform faces (its -Z); -Z itself for no transform.
inline Vec3 VsFacing(const Transform* t) { return t ? VsFacing(t->Rotation()) : Vec3::Back; }

/// Turn `t` so it faces the world point `target` (full 3D, world up as the hint). Nothing happens when the
/// target is at its position. Transform::LookAt turns +Z at a point, so it is aimed at the mirror point.
inline void VsFaceToward(Transform& t, const Vec3& target) {
    const Vec3 p = t.Position();
    t.LookAt(p * 2.0f - target);
}
/// Turn `t` about the world up axis only so it faces the side of `target` (level, no tilt).
inline void VsFaceTowardFlat(Transform& t, const Vec3& target) {
    const Vec3 p = t.Position();
    t.LookAtYaw(p * 2.0f - target);
}
/// Turn `t` about the world up axis only so it faces along the world direction `dir`.
inline void VsFaceAlongFlat(Transform& t, const Vec3& dir) { t.LookAtYaw(t.Position() - dir); }

} // namespace okay::vs
