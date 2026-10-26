#pragma once
#include <cmath>
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Math/CCD.hpp"
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

    // Target out of reach: stretch the chain straight toward it (a target on the
    // root of a zero-length chain has no direction: leave it).
    const float reach = (target - root).Magnitude();
    if (reach >= total) {
        if (reach < 1e-6f) return;
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

/// Bend the interior joints of a solved chain toward a pole point (the knee /
/// elbow / tentacle-curl direction). Each interior joint is rotated about the
/// line through its neighbours, so every bone length is preserved.
inline void ApplyFabrikPole(std::vector<Vec3>& joints, const Vec3& pole) {
    const int n = static_cast<int>(joints.size());
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

/// One joint's limits for the chain solvers: how far the next bone may swing
/// away from the line of this one (a cone), and optionally a hinge, so it bends
/// only about one axis (a knee, an elbow, a finger). The solvers turn bones by
/// the shortest arc, so they add no twist of their own to limit.
struct JointLimit {
    float maxSwing = 180.0f;   ///< degrees the next bone may bend away from this one (180 = free)
    bool  hinge = false;       ///< bend only about `axis`
    Vec3  axis{1, 0, 0};       ///< the hinge axis, in the joint bone's own (local) space
};

/// Keep joint `i` (0 < i < n-1) within a swing cone of `maxRad` and, with a
/// `hinge` axis (world), in the plane across it. An over-bent next bone is
/// swung back and the rest of the chain follows rigidly; lengths are kept.
inline void LimitJoint(std::vector<Vec3>& joints, const std::vector<float>& len, int i,
                       float maxRad, const Vec3* hinge) {
    const int n = static_cast<int>(joints.size());
    Vec3 prev = joints[i] - joints[i - 1], next = joints[i + 1] - joints[i];
    const float lp = prev.Magnitude(), ln = next.Magnitude();
    if (lp < 1e-6f || ln < 1e-6f) return;
    prev = prev * (1.0f / lp); next = next * (1.0f / ln);
    Vec3 dir = next;
    if (hinge && hinge->SqrMagnitude() > 1e-8f) {   // drop the part of the bend off the hinge's plane
        const Vec3 ax = hinge->Normalized();
        Vec3 onPlane = dir - ax * Vec3::Dot(dir, ax);
        if (onPlane.SqrMagnitude() > 1e-8f) dir = onPlane.Normalized();
    }
    float c = Vec3::Dot(prev, dir); c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
    if (std::acos(c) > maxRad) {   // swing back within the prev/dir plane to exactly maxRad
        Vec3 perp = dir - prev * c;
        const float pl = perp.Magnitude();
        dir = pl > 1e-6f ? prev * std::cos(maxRad) + perp * (std::sin(maxRad) / pl) : prev;
    }
    if ((dir - next).SqrMagnitude() < 1e-12f) return;
    const Vec3 delta = joints[i] + dir * len[i] - joints[i + 1];
    for (int k = i + 1; k < n; ++k) joints[k] = joints[k] + delta;   // the rest of the chain follows rigidly
}

/// Keep the bend at each interior joint within `maxBendDeg` (the angle between a
/// bone and the next one): over-bent segments are swung back toward straight and
/// the rest of the chain follows. Lengths are preserved.
inline void ApplyFabrikBendLimit(std::vector<Vec3>& joints, const std::vector<float>& len, float maxBendDeg) {
    const int n = static_cast<int>(joints.size());
    if (maxBendDeg <= 0.0f || maxBendDeg >= 180.0f) return;
    for (int i = 1; i < n - 1; ++i) LimitJoint(joints, len, i, maxBendDeg * 0.01745329f, nullptr);
}

/// Per-joint limits on a solved chain. `limits[i]` is joint i's (root .. tip);
/// a hinge axis turns with the bone before it, from where `rest` (the chain's
/// animated pose, in which `restRot[i]` was joint i's world rotation) had it.
inline void ApplyJointLimits(std::vector<Vec3>& joints, const std::vector<float>& len,
                             const std::vector<JointLimit>& limits,
                             const std::vector<Vec3>& rest, const std::vector<Quat>& restRot) {
    const int n = static_cast<int>(joints.size());
    for (int i = 1; i < n - 1 && i < static_cast<int>(limits.size()); ++i) {
        const JointLimit& l = limits[i];
        if (!l.hinge && l.maxSwing >= 180.0f) continue;
        Vec3 axis;
        if (l.hinge && i < static_cast<int>(rest.size()) && i < static_cast<int>(restRot.size()))
            axis = Quat::FromToRotation(rest[i] - rest[i - 1], joints[i] - joints[i - 1]) * (restRot[i] * l.axis);
        LimitJoint(joints, len, i, std::fmax(0.0f, std::fmin(l.maxSwing, 180.0f)) * 0.01745329f, l.hinge ? &axis : nullptr);
    }
}

/// What a chain solve keeps to: a pole point, one bend cap for every joint
/// (0 = none) and per-joint limits (with the animated pose their hinge axes
/// were read in, see ApplyJointLimits). Every part is optional.
struct ChainConstraints {
    const Vec3* pole = nullptr;
    float maxBendDeg = 0.0f;
    const std::vector<JointLimit>* limits = nullptr;
    const std::vector<Vec3>* rest = nullptr;
    const std::vector<Quat>* restRot = nullptr;
};

/// FABRIK or CCD under constraints: each iteration solves one pass, then bends
/// toward the pole and applies the limits. Returns the remaining distance from
/// the tip to the target.
inline float SolveChainConstrained(std::vector<Vec3>& joints, const std::vector<float>& len,
                                   const Vec3& target, int iterations, float tolerance,
                                   const ChainConstraints& c, bool ccd = false) {
    const int n = static_cast<int>(joints.size());
    if (n < 2 || static_cast<int>(len.size()) < n - 1) return 0.0f;
    // A pole decides the bend plane up front, so the solve starts from the right side.
    if (c.pole) ApplyFabrikPole(joints, *c.pole);
    for (int it = 0; it < (iterations < 1 ? 1 : iterations); ++it) {
        if (ccd) SolveCCD(joints, target, 1, tolerance);
        else     SolveFabrik(joints, len, target, 1, tolerance);
        if (c.pole) ApplyFabrikPole(joints, *c.pole);
        ApplyFabrikBendLimit(joints, len, c.maxBendDeg);
        if (c.limits && c.rest && c.restRot) ApplyJointLimits(joints, len, *c.limits, *c.rest, *c.restRot);
        if ((joints[n - 1] - target).Magnitude() < tolerance) break;
    }
    return (joints[n - 1] - target).Magnitude();
}

/// FABRIK with a pole target and joint bend limits. `pole` may be null.
/// Returns the remaining distance from the tip to the target.
inline float SolveFabrikConstrained(std::vector<Vec3>& joints, const std::vector<float>& len,
                                    const Vec3& target, int iterations, float tolerance,
                                    const Vec3* pole, float maxBendDeg) {
    ChainConstraints c;
    c.pole = pole; c.maxBendDeg = maxBendDeg;
    return SolveChainConstrained(joints, len, target, iterations, tolerance, c);
}

} // namespace okay