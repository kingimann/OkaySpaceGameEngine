#pragma once
#include <cmath>
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Math/IKMath.hpp"
#include <vector>

namespace okay {

/// FABRIK (Forward-And-Backward-Reaching IK) — a full-body / long-chain solver.
/// Iteratively drags a chain of joint positions so the tip reaches `target` while
/// keeping the root pinned and every bone length fixed. Handles arbitrarily long
/// chains (spider legs, tentacles, spines, two-hands-on-a-wall) where the closed-
/// form two-bone solver can't. Solves in-place on `joints` (root .. tip); `len`
/// holds the n-1 segment lengths.
///
/// A root or target that isn't finite, a chain with no length, or a target
/// sitting on the root leaves `joints` exactly as they are.
inline void SolveFabrik(std::vector<Vec3>& joints, const std::vector<float>& len,
                        const Vec3& target, int iterations = 10, float tolerance = 1e-3f) {
    const int n = static_cast<int>(joints.size());
    if (n < 2 || static_cast<int>(len.size()) < n - 1) return;

    const Vec3 root = joints[0];
    if (!Finite(root) || !Finite(target)) return;
    float total = 0.0f;
    for (int i = 0; i < n - 1; ++i) total += len[i];
    const Vec3 toTarget = target - root;
    const float dist = toTarget.Magnitude();
    if (!(total > 1e-9f) || !(dist > 1e-6f)) return;
    // Wherever two joints coincide, the direction the chain already points stands in.
    const Vec3 chainDir = SafeNormalize(joints[n - 1] - root, Vec3::Down);

    // Target out of reach: stretch the chain straight toward it.
    if (dist >= total) {
        const Vec3 dir = SafeNormalize(toTarget, chainDir);
        for (int i = 1; i < n; ++i) joints[i] = joints[i - 1] + dir * len[i - 1];
        return;
    }

    for (int it = 0; it < iterations; ++it) {
        if ((joints[n - 1] - target).Magnitude() < tolerance) break;
        // Backward reach: pin the tip to the target, walk to the root.
        joints[n - 1] = target;
        Vec3 last = chainDir * -1.0f;
        for (int i = n - 2; i >= 0; --i) {
            const Vec3 d = SafeNormalize(joints[i] - joints[i + 1], last);
            joints[i] = joints[i + 1] + d * len[i];
            last = d;
        }
        // Forward reach: pin the root back, walk to the tip.
        joints[0] = root;
        last = chainDir;
        for (int i = 1; i < n; ++i) {
            const Vec3 d = SafeNormalize(joints[i] - joints[i - 1], last);
            joints[i] = joints[i - 1] + d * len[i - 1];
            last = d;
        }
    }
}

/// Bend the interior joints of a solved chain toward a pole point (the knee /
/// elbow / tentacle-curl direction). Each interior joint is rotated about the
/// line through its neighbours, so every bone length is preserved.
inline void ApplyFabrikPole(std::vector<Vec3>& joints, const Vec3& pole) {
    const int n = static_cast<int>(joints.size());
    if (!Finite(pole)) return;
    for (int i = 1; i < n - 1; ++i) {
        const Vec3 a = joints[i - 1], b = joints[i + 1];
        Vec3 axis = b - a;
        const float axLen = axis.Magnitude();
        if (axLen < 1e-6f) continue;
        axis = axis * (1.0f / axLen);
        // Project the joint and the pole onto the plane through `a` normal to the axis.
        auto proj = [&](const Vec3& p) { Vec3 d = p - a; return d - axis * Vec3::Dot(d, axis); };
        Vec3 pj = proj(joints[i]), pp = proj(pole);
        const float lj = pj.Magnitude(), lp = pp.Magnitude();
        if (lj < 1e-6f || lp < 1e-6f) continue;
        pj = pj * (1.0f / lj); pp = pp * (1.0f / lp);
        float c = Vec3::Dot(pj, pp); c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
        float ang = std::acos(c);
        if (Vec3::Dot(Vec3::Cross(pj, pp), axis) < 0.0f) ang = -ang;
        // Rodrigues rotation of (joint - a) about `axis` by `ang`.
        const Vec3 v = joints[i] - a;
        const float cs = std::cos(ang), sn = std::sin(ang);
        joints[i] = a + v * cs + Vec3::Cross(axis, v) * sn + axis * (Vec3::Dot(axis, v) * (1.0f - cs));
    }
}

/// Keep the bend at each interior joint within `maxBendDeg` (the angle between a
/// bone and the next one): over-bent segments are swung back toward straight and
/// the rest of the chain follows. Lengths are preserved.
inline void ApplyFabrikBendLimit(std::vector<Vec3>& joints, const std::vector<float>& len, float maxBendDeg) {
    const int n = static_cast<int>(joints.size());
    if (maxBendDeg <= 0.0f || maxBendDeg >= 180.0f) return;
    const float maxRad = maxBendDeg * 0.01745329f;
    for (int i = 1; i < n - 1; ++i) {
        Vec3 prev = joints[i] - joints[i - 1], next = joints[i + 1] - joints[i];
        const float lp = prev.Magnitude(), ln = next.Magnitude();
        if (lp < 1e-6f || ln < 1e-6f) continue;
        prev = prev * (1.0f / lp); next = next * (1.0f / ln);
        float c = Vec3::Dot(prev, next); c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
        const float ang = std::acos(c);
        if (ang <= maxRad) continue;
        // Swing `next` within the prev/next plane to exactly maxRad from prev.
        Vec3 perp = next - prev * c;
        const float pl = perp.Magnitude();
        Vec3 dir = pl > 1e-6f ? prev * std::cos(maxRad) + perp * (std::sin(maxRad) / pl) : prev;
        const Vec3 delta = joints[i] + dir * len[i] - joints[i + 1];
        for (int k = i + 1; k < n; ++k) joints[k] = joints[k] + delta;   // the rest of the chain follows rigidly
    }
}

/// FABRIK with a pole target and joint bend limits. `pole` may be null.
/// Returns the remaining distance from the tip to the target (0 when nothing
/// could be solved: a broken target or root, or a target on the root).
inline float SolveFabrikConstrained(std::vector<Vec3>& joints, const std::vector<float>& len,
                                    const Vec3& target, int iterations, float tolerance,
                                    const Vec3* pole, float maxBendDeg) {
    const int n = static_cast<int>(joints.size());
    if (n < 2 || static_cast<int>(len.size()) < n - 1) return 0.0f;
    if (!Finite(joints[0]) || !Finite(target) || (target - joints[0]).SqrMagnitude() < 1e-12f) return 0.0f;
    float total = 0.0f;
    for (int i = 0; i < n - 1; ++i) total += len[i];
    if (!(total > 1e-9f)) return 0.0f;
    // A pole decides the bend plane up front, so FABRIK starts from the right side.
    if (pole) ApplyFabrikPole(joints, *pole);
    for (int it = 0; it < (iterations < 1 ? 1 : iterations); ++it) {
        SolveFabrik(joints, len, target, 1, tolerance);
        if (pole) ApplyFabrikPole(joints, *pole);
        ApplyFabrikBendLimit(joints, len, maxBendDeg);
        if ((joints[n - 1] - target).Magnitude() < tolerance) break;
    }
    return (joints[n - 1] - target).Magnitude();
}

} // namespace okay
