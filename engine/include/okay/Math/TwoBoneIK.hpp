#pragma once
#include <cmath>
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mathf.hpp"

namespace okay {

/// Analytic two-bone IK (hip -> knee -> foot, or shoulder -> elbow -> hand).
/// Given the root position, the two bone lengths and a world-space target, solves
/// the bent-joint position so the chain reaches the target. `pole` hints the bend
/// direction (e.g. forward for a knee, so it bends the right way). The target is
/// clamped to the reachable range, so the chain never stretches or inverts.
///
/// This is the math IK / "foot IK" hides — a closed-form law-of-cosines
/// solve, no iteration. Returns the mid-joint and the (clamped) end position.
///
/// `minBendDeg`/`maxBendDeg` limit the interior angle at the mid joint (knee/elbow):
/// the defaults (0..180) impose no limit, but e.g. maxBendDeg=175 stops a knee from
/// hyperextending/locking straight, and a higher minBendDeg keeps a minimum bend.
/// When a limit binds the end stays short of the target (it can't reach without
/// breaking the limit), which is the correct, natural behavior.
inline void SolveTwoBoneIK(const Vec3& root, float upperLen, float lowerLen,
                           const Vec3& target, const Vec3& pole,
                           Vec3& outMid, Vec3& outEnd,
                           float minBendDeg = 0.0f, float maxBendDeg = 180.0f) {
    Vec3 toTarget = target - root;
    float dist = toTarget.Magnitude();
    float maxReach = upperLen + lowerLen;
    float minReach = Mathf::Abs(upperLen - lowerLen);
    // Bend-angle limits map to a reachable-distance window (law of cosines: a larger
    // interior angle = a straighter limb = a longer root->end distance).
    float lo = minReach + 1e-4f, hi = maxReach - 1e-4f;
    auto distForAngle = [&](float deg) {
        float c = Mathf::Cos(Mathf::Clamp(deg, 0.0f, 180.0f) * Mathf::Deg2Rad);
        float d2 = upperLen * upperLen + lowerLen * lowerLen - 2.0f * upperLen * lowerLen * c;
        return d2 > 0.0f ? Mathf::Sqrt(d2) : 0.0f;
    };
    if (minBendDeg > 0.0f)   lo = Mathf::Max(lo, distForAngle(minBendDeg));
    if (maxBendDeg < 180.0f) hi = Mathf::Min(hi, distForAngle(maxBendDeg));
    if (lo > hi) lo = hi;
    // Clamp into the reachable shell (leave a hair of slack so it never fully locks).
    float clamped = Mathf::Clamp(dist, lo, hi);
    Vec3 axis = dist > Mathf::Epsilon ? toTarget / dist : Vec3{0, -1, 0};
    Vec3 end = root + axis * clamped;

    // Distance along the root->end axis to the foot of the perpendicular from the
    // mid joint (law of cosines), and the perpendicular height of the mid joint.
    float d = (upperLen * upperLen - lowerLen * lowerLen + clamped * clamped) / (2.0f * clamped);
    float h2 = upperLen * upperLen - d * d;
    float h = h2 > 0.0f ? Mathf::Sqrt(h2) : 0.0f;

    // Bend direction: the part of `pole` perpendicular to the axis.
    Vec3 bend = pole - axis * Vec3::Dot(pole, axis);
    if (bend.SqrMagnitude() < 1e-8f) {
        // Degenerate pole (parallel to the limb): pick any perpendicular.
        bend = Vec3::Cross(axis, Vec3{0, 1, 0});
        if (bend.SqrMagnitude() < 1e-8f) bend = Vec3::Cross(axis, Vec3{1, 0, 0});
    }
    bend = bend.Normalized();

    outMid = root + axis * d + bend * h;
    outEnd = end;
}

/// Soft IK: as a target approaches full reach, ease the reached distance in
/// instead of snapping the limb straight (the "pop" at full extension). Returns
/// the softened root->target distance for a limb of total length `reach`;
/// `soft` is the easing band in world units (0 = off).
inline float SoftReachDistance(float dist, float reach, float soft) {
    if (soft <= 1e-5f) return dist;
    const float da = reach - soft;
    if (dist <= da) return dist;
    return da + soft * (1.0f - std::exp(-(dist - da) / soft));
}

/// SolveTwoBoneIK with soft reach: the target is pulled in along the limb axis
/// by SoftReachDistance before solving.
inline void SolveTwoBoneIKSoft(const Vec3& root, float upperLen, float lowerLen,
                               const Vec3& target, const Vec3& pole, float soft,
                               Vec3& outMid, Vec3& outEnd,
                               float minBendDeg = 0.0f, float maxBendDeg = 180.0f) {
    Vec3 d = target - root;
    float dist = d.Magnitude();
    Vec3 t = target;
    if (dist > 1e-6f) t = root + d * (SoftReachDistance(dist, upperLen + lowerLen, soft) / dist);
    SolveTwoBoneIK(root, upperLen, lowerLen, t, pole, outMid, outEnd, minBendDeg, maxBendDeg);
}

} // namespace okay