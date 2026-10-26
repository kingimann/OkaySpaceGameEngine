#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Math/IKMath.hpp"
#include <vector>

namespace okay {

/// CCD (Cyclic Coordinate Descent) — the other classic full-chain IK solver. Each
/// iteration sweeps from the joint nearest the tip back to the root, rotating each
/// joint so the tip swings toward the target. Because every step is a rigid
/// rotation about a joint, bone lengths are preserved exactly and the root never
/// moves. Compared to FABRIK it tends to bend the chain from the tip end first —
/// handy when you want the wrist/ankle to do the reaching before the shoulder/hip.
/// Solves in-place on `joints` (root .. tip).
///
/// A root or target that isn't finite, or a target sitting on the root, leaves
/// `joints` exactly as they are.
inline void SolveCCD(std::vector<Vec3>& joints, const Vec3& target,
                     int iterations = 10, float tolerance = 1e-3f) {
    const int n = static_cast<int>(joints.size());
    if (n < 2) return;
    if (!Finite(joints[0]) || !Finite(target) || (target - joints[0]).SqrMagnitude() < 1e-12f) return;
    for (int it = 0; it < iterations; ++it) {
        if ((joints[n - 1] - target).Magnitude() < tolerance) break;
        for (int i = n - 2; i >= 0; --i) {
            Vec3 toEnd = joints[n - 1] - joints[i];
            Vec3 toTgt = target - joints[i];
            if (toEnd.SqrMagnitude() < 1e-10f || toTgt.SqrMagnitude() < 1e-10f) continue;
            Quat rot = Quat::FromToRotation(toEnd.Normalized(), toTgt.Normalized());
            // Rotate every joint past i rigidly about joint i (keeps bone lengths).
            for (int j = i + 1; j < n; ++j)
                joints[j] = joints[i] + rot * (joints[j] - joints[i]);
        }
    }
}

} // namespace okay
