#pragma once
// The engine's math types to the physics core's and back (Vec3 <-> phys::V3, Quat <-> phys::Q). One copy for every
// engine-side user of the core (the scene bridge Physics3DEngine.inl, the ragdoll's CoreRagdoll).
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Physics/Core/PMath.hpp"

namespace okay {
namespace phys {
namespace conv {
inline V3 P3(const Vec3& v) { return {v.x, v.y, v.z}; }
inline Vec3 E3(const V3& v) { return {v.x, v.y, v.z}; }
inline Q PQ(const Quat& q) { return NormalizeQ(Q{q.x, q.y, q.z, q.w}); }
inline Quat EQ(const Q& q) { return Quat{q.x, q.y, q.z, q.w}; }
} // namespace conv
} // namespace phys
} // namespace okay
