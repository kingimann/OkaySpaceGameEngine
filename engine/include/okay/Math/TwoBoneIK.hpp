#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mathf.hpp"

namespace okay {

/// Analytic two-bone IK (hip -> knee -> foot, or shoulder -> elbow -> hand).
/// Given the root position, the two bone lengths and a world-space target, solves
/// the bent-joint position so the chain reaches the target. `pole` hints the bend
/// direction (e.g. forward for a knee, so it bends the right way). The target is
/// clamped to the reachable range, so the chain never stretches or inverts.
///
/// This is the math Unity's IK / "foot IK" hides — a closed-form law-of-cosines
/// solve, no iteration. Returns the mid-joint and the (clamped) end position.
inline void SolveTwoBoneIK(const Vec3& root, float upperLen, float lowerLen,
                           const Vec3& target, const Vec3& pole,
                           Vec3& outMid, Vec3& outEnd) {
    Vec3 toTarget = target - root;
    float dist = toTarget.Magnitude();
    float maxReach = upperLen + lowerLen;
    float minReach = Mathf::Abs(upperLen - lowerLen);
    // Clamp into the reachable shell (leave a hair of slack so it never fully locks).
    float clamped = Mathf::Clamp(dist, minReach + 1e-4f, maxReach - 1e-4f);
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

} // namespace okay
