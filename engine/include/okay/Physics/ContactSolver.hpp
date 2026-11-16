#pragma once
// The Improved contact solver for 3D rigid bodies: repeated impulse passes over every contact point until the bodies stop
// pushing into each other, starting each step from the pushes of the last one.
//
// Written from the general method known as sequential impulses (projected Gauss-Seidel on the contact constraints):
//   * for every contact point a normal row (the relative speed along the normal must not be negative, so the push is
//     never negative: its running total is clamped at zero) and two friction rows (their running totals are clamped by the
//     friction coefficient times the normal push, which is what makes friction hold);
//   * the rows are visited again and again; each visit changes the body speeds a little and the running totals converge;
//   * warm starting: the pushes found last step are applied before the first pass, so a stack at rest starts the step
//     already balanced;
//   * restitution comes from the approach speed before the solve; a contact that is still apart (inside the skin) may close
//     at most the gap in one step, which is how a body is stopped one step before it hits;
//   * penetration is removed in a separate pass that moves bodies directly (no speed is added, so no energy);
//   * rolling resistance is one more row per touching pair that limits the turning speed about the contact.
// Nothing is copied from another engine. The maths is checked in tests/test_physics_deep_solver.cpp.
// Header only, no scene types.
#include "okay/Math/Mat3.hpp"
#include "okay/Math/Vec3.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace okay {
namespace contactsolver {

inline Mat3 ZeroMat() { Mat3 z; for (int i = 0; i < 9; ++i) z.m[i] = 0.0f; return z; }

struct Body {
    Vec3 pos{0, 0, 0};            // centre of mass, world
    Vec3 v{0, 0, 0}, w{0, 0, 0};  // linear and angular speed, world
    float invMass = 0.0f;         // 0 = does not move (static, or kinematic: it still has v and w)
    Mat3 invI = ZeroMat();        // world inverse inertia (all zero: cannot turn)
    Vec3 dp{0, 0, 0};             // position change made by SolvePositions
};

struct Point {
    Vec3 pos{0, 0, 0};            // world contact point
    float depth = 0.0f;           // > 0 overlap, < 0 gap (inside the skin)
    float jn = 0.0f, jt1 = 0.0f, jt2 = 0.0f;   // running pushes: in = last step's (warm start), out = this step's
    // scratch, filled by Prepare
    Vec3 rA{0, 0, 0}, rB{0, 0, 0};
    float nMass = 0.0f, t1Mass = 0.0f, t2Mass = 0.0f, target = 0.0f;
};

struct Manifold {
    int a = 0, b = 0;             // body indices
    Vec3 n{0, 1, 0};              // unit, from A toward B
    float friction = 0.5f;        // combined coefficient
    float restitution = 0.0f;     // combined bounciness
    float rolling = 0.0f;         // rolling resistance (a length): turning torque <= rolling * total normal push
    int count = 0;
    Point pts[4];
    Vec3 rollImpulse{0, 0, 0};    // running rolling push (in = warm start)
    Vec3 t1{0, 0, 0}, t2{0, 0, 0};   // scratch
    float sA = 1.0f, sB = 1.0f;      // scratch: how much more strongly body A / B answers a push here (mass ratio limit)
};

struct Settings {
    int velocityIterations = 10;
    int positionIterations = 3;
    float slop = 0.005f;                 // overlap that is left alone (smaller values make a stack buzz)
    float beta = 0.8f;                   // share of the remaining overlap removed per position pass
    float maxCorrection = 0.15f;         // most one pass moves a body apart (metres)
    float restitutionThreshold = 0.5f;   // slower approaches do not bounce
    float warmStart = 1.0f;              // 0 switches warm starting off (tests)
    float maxMassRatio = 8.0f;           // a body more than this many times heavier than the one it touches answers pushes as if it were
                                         // only this much heavier (a 100 kg crate on a 1 kg box would otherwise sink through it);
                                         // 0 switches the limit off. Applies to contacts that are more up-down than sideways.
    bool alternateOrder = true;          // every other pass visits the pairs in reverse (spreads a push both ways along a stack)
};

inline Vec3 PointVelocity(const Body& b, const Vec3& r) { return b.v + Vec3::Cross(b.w, r); }

inline void ApplyImpulse(Body& A, Body& B, const Vec3& rA, const Vec3& rB, const Vec3& P, float sA = 1.0f, float sB = 1.0f) {
    A.v = A.v - P * (A.invMass * sA);
    A.w = A.w - (A.invI * Vec3::Cross(rA, P)) * sA;
    B.v = B.v + P * (B.invMass * sB);
    B.w = B.w + (B.invI * Vec3::Cross(rB, P)) * sB;
}

inline float EffectiveMass(const Body& A, const Body& B, const Vec3& rA, const Vec3& rB, const Vec3& dir, float sA = 1.0f, float sB = 1.0f) {
    Vec3 ra = Vec3::Cross(rA, dir), rb = Vec3::Cross(rB, dir);
    float k = (A.invMass + Vec3::Dot(ra, A.invI * ra)) * sA + (B.invMass + Vec3::Dot(rb, B.invI * rb)) * sB;
    return k > 1e-12f ? 1.0f / k : 0.0f;
}

/// Work out the masses, tangent directions and targets, and apply the warm start.
inline void Prepare(std::vector<Body>& bodies, std::vector<Manifold>& ms, float dt, const Settings& s) {
    for (Manifold& m : ms) {
        Body& A = bodies[(std::size_t)m.a];
        Body& B = bodies[(std::size_t)m.b];
        m.sA = m.sB = 1.0f;
        if (s.maxMassRatio > 0.0f && A.invMass > 0.0f && B.invMass > 0.0f && std::fabs(m.n.y) > 0.5f) {   // (only where one body sits on the other: a sideways push keeps the true masses)
            const float ratio = A.invMass > B.invMass ? A.invMass / B.invMass : B.invMass / A.invMass;   // heavier : lighter
            if (ratio > s.maxMassRatio) (A.invMass > B.invMass ? m.sB : m.sA) = ratio / s.maxMassRatio;
        }
        const Vec3 helper = std::fabs(m.n.x) > 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        m.t1 = Vec3::Cross(m.n, helper).Normalized();
        m.t2 = Vec3::Cross(m.n, m.t1);
        for (int i = 0; i < m.count; ++i) {
            Point& p = m.pts[i];
            p.rA = p.pos - A.pos; p.rB = p.pos - B.pos;
            p.nMass = EffectiveMass(A, B, p.rA, p.rB, m.n, m.sA, m.sB);
            p.t1Mass = EffectiveMass(A, B, p.rA, p.rB, m.t1, m.sA, m.sB);
            p.t2Mass = EffectiveMass(A, B, p.rA, p.rB, m.t2, m.sA, m.sB);
            const float vn = Vec3::Dot(PointVelocity(B, p.rB) - PointVelocity(A, p.rA), m.n);
            if (p.depth < 0.0f) {
                // May close the gap, no more. A bouncy body that would arrive this step bounces now (at most a step early),
                // otherwise the gap clamp would swallow its speed before it ever touched.
                p.target = p.depth / dt;
                if (m.restitution > 0.0f && vn < -s.restitutionThreshold && -p.depth < -vn * dt) p.target = -m.restitution * vn;
            }
            else p.target = (vn < -s.restitutionThreshold) ? -m.restitution * vn : 0.0f;
            if (s.warmStart > 0.0f) {
                Vec3 P = (m.n * p.jn + m.t1 * p.jt1 + m.t2 * p.jt2) * s.warmStart;
                ApplyImpulse(A, B, p.rA, p.rB, P, m.sA, m.sB);
            } else { p.jn = p.jt1 = p.jt2 = 0.0f; }
        }
        if (s.warmStart <= 0.0f) m.rollImpulse = Vec3{0, 0, 0};
        else if (m.rolling > 0.0f && m.count > 0) {
            A.w = A.w - (A.invI * (m.rollImpulse * s.warmStart)) * m.sA;
            B.w = B.w + (B.invI * (m.rollImpulse * s.warmStart)) * m.sB;
        }
    }
}

inline void SolveVelocities(std::vector<Body>& bodies, std::vector<Manifold>& ms, const Settings& s) {
    const int count = (int)ms.size();
    for (int it = 0; it < s.velocityIterations; ++it) {
        const bool reverse = s.alternateOrder && (it & 1);
        for (int mi = 0; mi < count; ++mi) {
            Manifold& m = ms[(std::size_t)(reverse ? count - 1 - mi : mi)];
            Body& A = bodies[(std::size_t)m.a];
            Body& B = bodies[(std::size_t)m.b];
            // Friction first, so keeping the bodies apart wins when the two disagree.
            for (int i = 0; i < m.count; ++i) {
                Point& p = m.pts[i];
                const float maxF = m.friction * p.jn;
                {
                    Vec3 vrel = PointVelocity(B, p.rB) - PointVelocity(A, p.rA);
                    float l = -Vec3::Dot(vrel, m.t1) * p.t1Mass;
                    float nj = std::max(-maxF, std::min(maxF, p.jt1 + l));
                    l = nj - p.jt1; p.jt1 = nj;
                    ApplyImpulse(A, B, p.rA, p.rB, m.t1 * l, m.sA, m.sB);
                }
                {
                    Vec3 vrel = PointVelocity(B, p.rB) - PointVelocity(A, p.rA);
                    float l = -Vec3::Dot(vrel, m.t2) * p.t2Mass;
                    float nj = std::max(-maxF, std::min(maxF, p.jt2 + l));
                    l = nj - p.jt2; p.jt2 = nj;
                    ApplyImpulse(A, B, p.rA, p.rB, m.t2 * l, m.sA, m.sB);
                }
            }
            if (m.rolling > 0.0f && m.count > 0) {
                float total = 0.0f;
                for (int i = 0; i < m.count; ++i) total += m.pts[i].jn;
                const float maxL = m.rolling * total;
                Vec3 dw = B.w - A.w;
                dw = dw - m.n * Vec3::Dot(dw, m.n);                  // only the turning that is not about the normal
                Mat3 K = A.invI * m.sA + B.invI * m.sB;
                Vec3 l = K.Inverse() * (dw * -1.0f);
                l = l - m.n * Vec3::Dot(l, m.n);
                Vec3 ni = m.rollImpulse + l;
                float len = ni.Magnitude();
                if (len > maxL) ni = len > 1e-12f ? ni * (maxL / len) : Vec3{0, 0, 0};
                l = ni - m.rollImpulse; m.rollImpulse = ni;
                A.w = A.w - (A.invI * l) * m.sA;
                B.w = B.w + (B.invI * l) * m.sB;
            }
            for (int i = 0; i < m.count; ++i) {
                Point& p = m.pts[i];
                Vec3 vrel = PointVelocity(B, p.rB) - PointVelocity(A, p.rA);
                float vn = Vec3::Dot(vrel, m.n);
                float l = (p.target - vn) * p.nMass;
                float nj = std::max(0.0f, p.jn + l);                 // pushes only, never pulls
                l = nj - p.jn; p.jn = nj;
                ApplyImpulse(A, B, p.rA, p.rB, m.n * l, m.sA, m.sB);
            }
        }
    }
}

/// Move overlapping bodies apart directly (translation only, so no speed is added). Results are in Body::dp.
inline void SolvePositions(std::vector<Body>& bodies, std::vector<Manifold>& ms, const Settings& s) {
    for (int it = 0; it < s.positionIterations; ++it) {
        for (Manifold& m : ms) {
            Body& A = bodies[(std::size_t)m.a];
            Body& B = bodies[(std::size_t)m.b];
            const float imA = A.invMass * m.sA, imB = B.invMass * m.sB;
            const float im = imA + imB;
            if (im <= 0.0f) continue;
            for (int i = 0; i < m.count; ++i) {
                const Point& p = m.pts[i];
                float pen = p.depth - Vec3::Dot(m.n, B.dp - A.dp);
                float c = std::min(s.maxCorrection, s.beta * (pen - s.slop));
                if (c <= 0.0f) continue;
                Vec3 P = m.n * (c / im);
                A.dp = A.dp - P * imA;
                B.dp = B.dp + P * imB;
            }
        }
    }
}

} // namespace contactsolver
} // namespace okay
