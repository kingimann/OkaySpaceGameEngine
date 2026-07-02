#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mathf.hpp"
#include <vector>

namespace okay {

/// FABRIK (Forward-And-Backward-Reaching IK) — a full-body / long-chain solver.
/// Iteratively drags a chain of joint positions so the tip reaches `target` while
/// keeping the root pinned and every bone length fixed. Handles arbitrarily long
/// chains (spider legs, tentacles, spines, two-hands-on-a-wall) where the closed-
/// form two-bone solver can't. Solves in-place on `joints` (root .. tip); `len`
/// holds the n-1 segment lengths.
inline void SolveFabrik(std::vector<Vec3>& joints, const std::vector<float>& len,
                        const Vec3& target, int iterations = 10, float tolerance = 1e-3f) {
    const int n = static_cast<int>(joints.size());
    if (n < 2 || static_cast<int>(len.size()) < n - 1) return;

    const Vec3 root = joints[0];
    float total = 0.0f;
    for (int i = 0; i < n - 1; ++i) total += len[i];

    // Target out of reach: stretch the chain straight toward it.
    if ((target - root).Magnitude() >= total) {
        Vec3 dir = (target - root).Normalized();
        for (int i = 1; i < n; ++i) joints[i] = joints[i - 1] + dir * len[i - 1];
        return;
    }

    for (int it = 0; it < iterations; ++it) {
        if ((joints[n - 1] - target).Magnitude() < tolerance) break;
        // Backward reach: pin the tip to the target, walk to the root.
        joints[n - 1] = target;
        for (int i = n - 2; i >= 0; --i) {
            Vec3 d = (joints[i] - joints[i + 1]).Normalized();
            joints[i] = joints[i + 1] + d * len[i];
        }
        // Forward reach: pin the root back, walk to the tip.
        joints[0] = root;
        for (int i = 1; i < n; ++i) {
            Vec3 d = (joints[i] - joints[i - 1]).Normalized();
            joints[i] = joints[i - 1] + d * len[i - 1];
        }
    }
}

} // namespace okay
