#pragma once
// ---------------------------------------------------------------------------
// LedgeHands - pure maths for "hands on the ledge" while a body hangs.
// Written from the problem itself (two hands on the top edge of a wall, spread
// along the edge, kept inside the arm's reach); no other project's code.
//   LedgeFromLip  : the grab edge point and the wall normal, from the lip the
//                   traverse probe keeps (a stand-on-top point behind the edge).
//   LedgeHandTargets : left / right hand points on the edge.
//   ClampToReach  : pull a target toward its shoulder so a straight arm can reach.
// Blending in and out is IKFade (LimbIK.fadeTime); nothing here has state.
// ---------------------------------------------------------------------------
#include "okay/Math/Vec3.hpp"
#include <cmath>

namespace okay {

struct LedgeGrip {
    bool valid = false;
    Vec3 edge{};      ///< point on the top edge, straight in front of the body
    Vec3 normal{};    ///< horizontal, points from the wall toward the body
};

/// `body` is where the hanging body stands, `lip` the stand-on-top point beyond the
/// edge. The body hangs `setback` metres out from the edge; the edge keeps the lip's height.
inline LedgeGrip LedgeFromLip(const Vec3& body, const Vec3& lip, float setback) {
    LedgeGrip g;
    Vec3 d{lip.x - body.x, 0.0f, lip.z - body.z};
    const float len = std::sqrt(d.x * d.x + d.z * d.z);
    if (!(len > 1e-4f) || !std::isfinite(len) || !std::isfinite(lip.y) || setback < 0.0f) return g;
    d = d * (1.0f / len);
    g.edge = Vec3{body.x + d.x * setback, lip.y, body.z + d.z * setback};
    g.normal = Vec3{-d.x, 0.0f, -d.z};
    g.valid = true;
    return g;
}

/// Hands `spread` metres either side of the edge point, `overEdge` metres onto the top
/// surface (into the wall). Left and right are as seen by someone facing the wall.
inline bool LedgeHandTargets(const LedgeGrip& g, float spread, float overEdge, Vec3& left, Vec3& right) {
    if (!g.valid) return false;
    const Vec3 in{-g.normal.x, 0.0f, -g.normal.z};            // into the wall
    const Vec3 r{-in.z, 0.0f, in.x};                          // facing `in`, the right-hand side (y up)
    const Vec3 c = g.edge + in * overEdge;
    left = c - r * spread;
    right = c + r * spread;
    return true;
}

/// If `target` is farther than `maxReach` from `shoulder`, move it onto that sphere.
inline Vec3 ClampToReach(const Vec3& shoulder, const Vec3& target, float maxReach) {
    const Vec3 d = target - shoulder;
    const float len = d.Magnitude();
    if (!(len > maxReach) || len < 1e-6f) return target;
    return shoulder + d * (maxReach / len);
}

} // namespace okay
