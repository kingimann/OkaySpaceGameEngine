// Joints of okay::phys::World: one general joint (per-axis lock / limit / free, motors, springs, a cone limit, an
// orientation drive and a position drive) that the Fixed, Ball, Hinge, Slider, ConeTwist, Distance and SixDof types
// are presets of.
//
// Written from:
//  * E. Catto, "Iterative Dynamics with Temporal Coherence" (2005) / "Modeling and Solving Constraints" (GDC 2009): a
//    joint is a set of scalar velocity rows J v + b = 0 (or >= 0 for limits), each with effective mass 1/(J M^-1 J^T),
//    solved one after another with the accumulated impulse clamped; the point constraint as one 3x3 block;
//  * E. Catto, "Soft Constraints: Reinventing the Spring" (GDC 2011): springs and drives from a stiffness k and damping c
//    (or a frequency and damping ratio): gamma = 1 / (dt (c + dt k)), beta = dt k / (c + dt k);
//  * the swing-twist split of a rotation for twist and cone limits (rotation about the joint X axis, then a swing about
//    an axis in the YZ plane), with the swing measured as a rotation vector (F. S. Grassia, "Practical parameterization
//    of rotations using the exponential map", J. Graphics Tools 1998);
//  * K. Erleben, PhD thesis (2004): the position pass that projects joint errors away after the velocity solve.
// No other engine's code was read or used.
#include "WorldInternal.hpp"

#include <algorithm>
#include <cmath>

namespace okay {
namespace phys {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kInf = 1e30f;
constexpr float kMaxAngularCorrection = 0.5f;   // radians per position iteration (a pose shoved past a stop comes back in a step)

struct Frames {
    V3 pA, pB, rA, rB, d;     // anchors, arms from the centres of mass, d = pB - pA
    Q RA, RB, rel;            // joint frames (world) and B's frame in A's
    V3 ax[3], bx0;            // A's joint axes (world), B's X axis (world)
    float twist = 0, sy = 0, sz = 0;
};

float Wrap(float a) {
    while (a > kPi) a -= 2.0f * kPi;
    while (a < -kPi) a += 2.0f * kPi;
    return a;
}

void SwingTwist(const Q& rel, float& twist, float& sy, float& sz) {
    Q qt{rel.x, 0.0f, 0.0f, rel.w};
    const float l = std::sqrt(qt.x * qt.x + qt.w * qt.w);
    if (l < 1e-9f) { qt = Q{}; twist = kPi; }
    else {
        qt = Q{qt.x / l, 0, 0, qt.w / l};
        if (qt.w < 0.0f) qt = Q{-qt.x, 0, 0, -qt.w};
        twist = Wrap(2.0f * std::atan2(qt.x, qt.w));
    }
    const Q qs = rel * Conj(qt);
    const V3 s = ToRotationVector(qs);
    sy = s.y;
    sz = s.z;
}

Frames MakeFrames(const SolverBody& A, const SolverBody& B, const JointDef& d) {
    Frames f;
    const Xf xa{A.com - Rotate(A.q, A.localCom), A.q};
    const Xf xb{B.com - Rotate(B.q, B.localCom), B.q};
    f.pA = xa.Apply(d.frameA.p);
    f.pB = xb.Apply(d.frameB.p);
    f.rA = f.pA - A.com;
    f.rB = f.pB - B.com;
    f.d = f.pB - f.pA;
    f.RA = NormalizeQ(A.q * d.frameA.q);
    f.RB = NormalizeQ(B.q * d.frameB.q);
    f.rel = NormalizeQ(Conj(f.RA) * f.RB);
    f.ax[0] = Rotate(f.RA, V3{1, 0, 0});
    f.ax[1] = Rotate(f.RA, V3{0, 1, 0});
    f.ax[2] = Rotate(f.RA, V3{0, 0, 1});
    f.bx0 = Rotate(f.RB, V3{1, 0, 0});
    SwingTwist(f.rel, f.twist, f.sy, f.sz);
    return f;
}

float RowK(const SolverBody& A, const SolverBody& B, const JointRow& r) {
    float k = 0.0f;
    if (A.dynamic) k += Dot(r.lin, Mul(A.invMassAxis, r.lin)) + Dot(r.angA, A.invI * r.angA);
    if (B.dynamic) k += Dot(r.lin, Mul(B.invMassAxis, r.lin)) + Dot(r.angB, B.invI * r.angB);
    return k;
}

void ApplyRow(SolverBody& A, SolverBody& B, const JointRow& r, float l) {
    if (A.dynamic) { A.v -= Mul(A.invMassAxis, r.lin) * l; A.w -= A.invI * r.angA * l; }
    if (B.dynamic) { B.v += Mul(B.invMassAxis, r.lin) * l; B.w += B.invI * r.angB * l; }
}

void Soft(float k, float c, float dt, float& gamma, float& beta) {
    const float den = c + dt * k;
    if (!(den > 0.0f)) { gamma = 0.0f; beta = 0.0f; return; }
    gamma = 1.0f / (dt * den);
    beta = dt * k / den;
}

/// Adds a row; returns it (or nullptr when full or it has no effective mass).
JointRow* AddRow(JointSolver& j, const SolverBody& A, const SolverBody& B, const V3& lin, const V3& angA, const V3& angB,
                 float* acc, bool angular) {
    if (j.rowCount >= 24) return nullptr;
    JointRow& r = j.rows[j.rowCount];
    r = JointRow{};
    r.lin = lin; r.angA = angA; r.angB = angB; r.acc = acc; r.angular = angular;
    const float k = RowK(A, B, r);
    if (!(k > 1e-12f)) return nullptr;
    r.mass = 1.0f / k;
    ++j.rowCount;
    return &r;
}
void MakeSoft(JointRow* r, float k, float c, float C, float dt) {
    if (!r) return;
    float gamma, beta;
    Soft(k, c, dt, gamma, beta);
    const float keff = 1.0f / r->mass;
    r->gamma = gamma;
    r->mass = 1.0f / (keff + gamma);
    r->bias += beta / dt * C;
}
/// Spring constants from a frequency (Hz) and damping ratio for a row of effective mass m.
void FreqToKC(float m, float hz, float zeta, float& k, float& c) {
    const float om = 2.0f * kPi * hz;
    k = m * om * om;
    c = 2.0f * m * zeta * om;
}
} // namespace

V3 JointAnglesOf(const Q& qa, const Q& qb, const JointDef& d) {
    const Q RA = NormalizeQ(qa * d.frameA.q), RB = NormalizeQ(qb * d.frameB.q);
    float t, y, z;
    SwingTwist(NormalizeQ(Conj(RA) * RB), t, y, z);
    return V3{t, y, z};
}

void JointsPrepare(World::Impl& w, const IslandWork& island, std::vector<SolverBody>& sb,
                   const std::function<int(BodyId)>& slotOf, std::vector<JointSolver>& out, const SolveSettings& s) {
    const float dt = s.dt;
    out.clear();
    out.reserve(island.joints.size());
    for (JointId id : island.joints) {
        JointData& jd = w.joints[id];
        const JointDef& d = jd.def;
        JointSolver j;
        j.id = id;
        j.a = slotOf(d.bodyA);
        j.b = slotOf(d.bodyB);
        const SolverBody& A = sb[(std::size_t)j.a];
        const SolverBody& B = sb[(std::size_t)j.b];
        const Frames f = MakeFrames(A, B, d);
        j.rA = f.rA;
        j.rB = f.rB;
        if (!s.warmStarting) {
            jd.linImpulse = V3{}; jd.coneImpulse = 0; jd.driveImpulse = V3{}; jd.linDriveImpulse = V3{}; jd.distImpulse = 0;
            for (int k = 0; k < 3; ++k) {
                jd.linAxisImpulse[k] = jd.linMotorImpulse[k] = jd.angAxisImpulse[k] = jd.angMotorImpulse[k] = 0;
                jd.linLowImpulse[k] = jd.linHighImpulse[k] = jd.angLowImpulse[k] = jd.angHighImpulse[k] = 0;
            }
        }

        // ---- linear part ----
        if (d.type == JointType::Distance) {
            const float L = Len(f.d);
            const V3 n = L > 1e-6f ? f.d * (1.0f / L) : f.ax[0];
            const V3 ca = Cross(f.rA, n), cb = Cross(f.rB, n);
            const bool spring = d.distanceSpring > 0.0f || d.distanceStiffness > 0.0f;
            const bool rod = d.minLength >= d.maxLength && !spring;
            if (rod) {
                AddRow(j, A, B, n, ca, cb, &jd.distImpulse, false);
            } else {
                if (spring) {
                    JointRow* r = AddRow(j, A, B, n, ca, cb, &jd.distImpulse, false);
                    if (r) {
                        float k = d.distanceStiffness, c = d.distanceDamper;
                        if (d.distanceSpring > 0.0f) FreqToKC(r->mass, d.distanceSpring, d.distanceDamping, k, c);
                        MakeSoft(r, k, c, L - d.maxLength, dt);
                    }
                } else if (JointRow* r = AddRow(j, A, B, -n, -ca, -cb, &jd.linHighImpulse[0], false)) {   // L <= max
                    r->lo = 0.0f; r->bias = std::max(d.maxLength - L, 0.0f) / dt;
                }
                if (d.minLength > 0.0f) {
                    if (JointRow* r = AddRow(j, A, B, n, ca, cb, &jd.linLowImpulse[0], false)) {   // L >= min
                        r->lo = 0.0f; r->bias = std::max(L - d.minLength, 0.0f) / dt;
                    }
                }
            }
        } else {
            bool allLocked = !d.linearDrive;
            for (int k = 0; k < 3; ++k)
                if (d.linear[k].mode != AxisMode::Locked || d.linear[k].springFrequency > 0.0f || d.linear[k].motor) allLocked = false;
            if (allLocked) {
                j.point = true;
                j.pointAcc = &jd.linImpulse;
                M33 K = M33::Zero();
                for (int k = 0; k < 3; ++k) K.m[k][k] = A.invMassAxis[k] + B.invMassAxis[k];
                if (A.dynamic) { const M33 S = Skew(f.rA); K = K + (S * A.invI * S) * -1.0f; }
                if (B.dynamic) { const M33 S = Skew(f.rB); K = K + (S * B.invI * S) * -1.0f; }
                j.pointInvK = K.Inverse();
            } else {
                const V3 armA = f.rA + f.d;   // B's anchor seen from A's centre (so a slider's arm grows with travel)
                for (int k = 0; k < 3; ++k) {
                    const JointAxis& ax = d.linear[k];
                    const V3 n = f.ax[k];
                    const V3 ca = Cross(armA, n), cb = Cross(f.rB, n);
                    const float x = Dot(f.d, n);
                    if (ax.springFrequency > 0.0f) {
                        JointRow* r = AddRow(j, A, B, n, ca, cb, &jd.linAxisImpulse[k], false);
                        if (r) { float kk, c; FreqToKC(r->mass, ax.springFrequency, ax.springDamping, kk, c); MakeSoft(r, kk, c, x - ax.springTarget, dt); }
                    } else if (ax.mode == AxisMode::Locked) {
                        AddRow(j, A, B, n, ca, cb, &jd.linAxisImpulse[k], false);
                    }
                    if (ax.mode == AxisMode::Limited) {
                        if (JointRow* r = AddRow(j, A, B, n, ca, cb, &jd.linLowImpulse[k], false)) { r->lo = 0; r->bias = std::max(x - ax.lower, 0.0f) / dt; }
                        if (JointRow* r = AddRow(j, A, B, -n, -ca, -cb, &jd.linHighImpulse[k], false)) { r->lo = 0; r->bias = std::max(ax.upper - x, 0.0f) / dt; }
                    }
                    if (ax.motor && ax.mode != AxisMode::Locked) {
                        if (JointRow* r = AddRow(j, A, B, n, ca, cb, &jd.linMotorImpulse[k], false)) {
                            r->bias = -ax.motorSpeed;
                            r->lo = -ax.motorMaxForce * dt; r->hi = ax.motorMaxForce * dt;
                        }
                    }
                }
                if (d.linearDrive) {   // soft pull of B's anchor toward the target point in A's frame
                    const V3 T = f.pA + Rotate(f.RA, d.linearTarget);
                    const V3 armT = T - A.com;
                    for (int k = 0; k < 3; ++k) {
                        const V3 n = f.ax[k];
                        JointRow* r = AddRow(j, A, B, n, Cross(armT, n), Cross(f.rB, n), &jd.linDriveImpulse[k], false);
                        if (!r) continue;
                        float kk, c; FreqToKC(r->mass, d.linearFrequency, d.linearDampingRatio, kk, c);
                        MakeSoft(r, kk, c, Dot(f.pB - T, n), dt);
                        r->lo = -d.linearMaxForce * dt; r->hi = d.linearMaxForce * dt;
                    }
                }
            }
        }

        // ---- angular part ----
        if (d.type != JointType::Distance) {
            const V3 twistAxis = Normalize(f.ax[0] + f.bx0, f.ax[0]);
            const bool allLocked = d.angular[0].mode == AxisMode::Locked && d.angular[1].mode == AxisMode::Locked &&
                                   d.angular[2].mode == AxisMode::Locked;
            const V3 rv = ToRotationVector(f.rel);   // (fixed joints: the whole relative rotation)
            const float value[3] = {f.twist, f.sy, f.sz};
            for (int k = 0; k < 3; ++k) {
                const JointAxis& ax = d.angular[k];
                const V3 n = (k == 0 && !allLocked) ? twistAxis : f.ax[k];
                const float v = allLocked ? rv[k] : value[k];
                if (ax.springFrequency > 0.0f) {
                    JointRow* r = AddRow(j, A, B, V3{}, n, n, &jd.angAxisImpulse[k], true);
                    if (r) { float kk, c; FreqToKC(r->mass, ax.springFrequency, ax.springDamping, kk, c); MakeSoft(r, kk, c, Wrap(v - ax.springTarget), dt); }
                } else if (ax.mode == AxisMode::Locked) {
                    AddRow(j, A, B, V3{}, n, n, &jd.angAxisImpulse[k], true);
                }
                if (ax.mode == AxisMode::Limited && !(d.useCone && k > 0)) {
                    if (JointRow* r = AddRow(j, A, B, V3{}, n, n, &jd.angLowImpulse[k], true)) { r->lo = 0; r->bias = std::max(v - ax.lower, 0.0f) / dt; }
                    if (JointRow* r = AddRow(j, A, B, V3{}, -n, -n, &jd.angHighImpulse[k], true)) { r->lo = 0; r->bias = std::max(ax.upper - v, 0.0f) / dt; }
                }
                if (ax.motor && ax.mode != AxisMode::Locked) {
                    if (JointRow* r = AddRow(j, A, B, V3{}, n, n, &jd.angMotorImpulse[k], true)) {
                        r->bias = -ax.motorSpeed;
                        r->lo = -ax.motorMaxForce * dt; r->hi = ax.motorMaxForce * dt;
                    }
                }
            }
            if (d.useCone) {   // elliptical swing cone: |swing| <= the cone's radius in the swing's direction
                const float sw = std::sqrt(f.sy * f.sy + f.sz * f.sz);
                if (sw > 1e-6f) {
                    const float cy = f.sy / sw, cz = f.sz / sw;
                    const float s1 = std::max(d.swingSpan1, 1e-3f), s2 = std::max(d.swingSpan2, 1e-3f);
                    const float limit = 1.0f / std::sqrt(cy * cy / (s1 * s1) + cz * cz / (s2 * s2));
                    const V3 n = Rotate(f.RA, V3{0.0f, cy, cz});
                    if (JointRow* r = AddRow(j, A, B, V3{}, -n, -n, &jd.coneImpulse, true)) { r->lo = 0; r->bias = std::max(limit - sw, 0.0f) / dt; }
                } else {
                    jd.coneImpulse = 0.0f;
                }
            }
            if (d.drive) {   // orientation drive toward driveTarget
                const V3 delta = Rotate(f.RA, ToRotationVector(NormalizeQ(d.driveTarget * Conj(f.rel))));
                const V3 tv = Rotate(f.RA, d.driveTargetVelocity);
                // A frequency is turned into a stiffness with the inertia each body has about the joint anchor (the
                // body swings about the anchor, not its centre), so "4 Hz" means 4 Hz for a limb on its joint.
                M33 invPivot = M33::Zero();
                auto addPivot = [&](const SolverBody& b, const V3& r) {
                    if (!b.dynamic || b.invMass <= 0.0f) return;
                    const M33 I = b.invI.Inverse();
                    M33 pa = M33::Zero();
                    const float rr = LenSq(r), m = 1.0f / b.invMass;
                    for (int x = 0; x < 3; ++x) for (int y = 0; y < 3; ++y) pa.m[x][y] = m * ((x == y ? rr : 0.0f) - r[x] * r[y]);
                    invPivot = invPivot + (I + pa).Inverse();
                };
                if (j.point) { addPivot(A, f.rA); addPivot(B, f.rB); }
                for (int k = 0; k < 3; ++k) {
                    const V3 n = f.ax[k];
                    JointRow* r = AddRow(j, A, B, V3{}, n, n, &jd.driveImpulse[k], true);
                    if (!r) continue;
                    float kk = d.driveStiffness, c = d.driveDamping;
                    if (d.driveFrequency > 0.0f) {
                        const float kp = Dot(n, invPivot * n);
                        FreqToKC(j.point && kp > 1e-12f ? 1.0f / kp : r->mass, d.driveFrequency, d.driveDampingRatio, kk, c);
                    }
                    if (kk <= 0.0f && c <= 0.0f) { --j.rowCount; continue; }
                    r->bias = -Dot(tv, n);
                    MakeSoft(r, kk, c, -Dot(delta, n), dt);
                    r->lo = -d.driveMaxTorque * dt; r->hi = d.driveMaxTorque * dt;
                }
            }
        }
        out.push_back(j);
    }
}

void JointsWarmStart(std::vector<JointSolver>& js, std::vector<SolverBody>& sb) {
    // (the accumulated impulses live in JointData; rows point at them)
    for (JointSolver& j : js) {
        SolverBody& A = sb[(std::size_t)j.a];
        SolverBody& B = sb[(std::size_t)j.b];
        for (int r = 0; r < j.rowCount; ++r) ApplyRow(A, B, j.rows[r], *j.rows[r].acc);
        if (j.point) {
            const V3 l = *j.pointAcc;
            if (A.dynamic) { A.v -= Mul(A.invMassAxis, l); A.w -= A.invI * Cross(j.rA, l); }
            if (B.dynamic) { B.v += Mul(B.invMassAxis, l); B.w += B.invI * Cross(j.rB, l); }
        }
    }
}

namespace {
// point-block impulse storage is in JointData; JointSolver keeps no pointer to it, so pass it in
void SolvePoint(JointSolver& j, SolverBody& A, SolverBody& B, V3& acc) {
    const V3 cdot = VelAt(B, j.rB) - VelAt(A, j.rA);
    const V3 l = (j.pointInvK * cdot) * -1.0f;
    acc += l;
    if (A.dynamic) { A.v -= Mul(A.invMassAxis, l); A.w -= A.invI * Cross(j.rA, l); }
    if (B.dynamic) { B.v += Mul(B.invMassAxis, l); B.w += B.invI * Cross(j.rB, l); }
}
} // namespace

void JointsSolveVelocity(std::vector<JointSolver>& js, std::vector<SolverBody>& sb, const SolveSettings& s) {
    (void)s;
    for (JointSolver& j : js) {
        SolverBody& A = sb[(std::size_t)j.a];
        SolverBody& B = sb[(std::size_t)j.b];
        for (int r = 0; r < j.rowCount; ++r) {
            JointRow& row = j.rows[r];
            const float jv = Dot(row.lin, B.v - A.v) + Dot(row.angB, B.w) - Dot(row.angA, A.w);
            float l = -row.mass * (jv + row.bias + row.gamma * *row.acc);
            const float o = *row.acc;
            const float nw = std::min(std::max(o + l, row.lo), row.hi);
            *row.acc = nw;
            l = nw - o;
            ApplyRow(A, B, row, l);
        }
        if (j.point) SolvePoint(j, A, B, *j.pointAcc);   // the anchor last: it matters most
    }
}

void JointsSolvePosition(World::Impl& w, std::vector<JointSolver>& js, std::vector<SolverBody>& sb, const SolveSettings& s) {
    for (JointSolver& j : js) {
        const JointDef& d = w.joints[j.id].def;
        SolverBody& A = sb[(std::size_t)j.a];
        SolverBody& B = sb[(std::size_t)j.b];
        if (!A.dynamic && !B.dynamic) continue;
        auto moveLin = [&](const V3& P, const V3& rA, const V3& rB) {
            if (A.dynamic) { A.com -= Mul(A.invMassAxis, P); A.q = Integrate(A.q, (A.invI * Cross(rA, P)) * -1.0f, 1.0f); }
            if (B.dynamic) { B.com += Mul(B.invMassAxis, P); B.q = Integrate(B.q, B.invI * Cross(rB, P), 1.0f); }
        };
        auto turn = [&](const V3& axis, float err) {   // rotate B relative to A by -err about axis (split by inertia)
            JointRow r; r.angA = axis; r.angB = axis;
            const float k = RowK(A, B, r);
            if (!(k > 1e-12f)) return;
            const float e = std::min(std::max(err, -kMaxAngularCorrection), kMaxAngularCorrection);
            const float l = -e / k;
            if (A.dynamic) A.q = Integrate(A.q, (A.invI * axis) * -l, 1.0f);
            if (B.dynamic) B.q = Integrate(B.q, (B.invI * axis) * l, 1.0f);
        };
        auto pushAlong = [&](const V3& n, const V3& rA, const V3& rB, float err) {   // move B relative to A by -err along n
            JointRow r; r.lin = n; r.angA = Cross(rA, n); r.angB = Cross(rB, n);
            const float k = RowK(A, B, r);
            if (!(k > 1e-12f)) return;
            const float e = std::min(std::max(err, -s.maxCorrection), s.maxCorrection);
            moveLin(n * (-e / k), rA, rB);
        };

        Frames f = MakeFrames(A, B, d);
        if (d.type == JointType::Distance) {
            const float L = Len(f.d);
            if (L < 1e-6f) continue;
            const V3 n = f.d * (1.0f / L);
            if (d.distanceSpring > 0.0f || d.distanceStiffness > 0.0f) {
                if (d.minLength > 0.0f && L < d.minLength) pushAlong(n, f.rA, f.rB, L - d.minLength);
                continue;
            }
            if (L > d.maxLength) pushAlong(n, f.rA, f.rB, L - d.maxLength);
            else if (L < d.minLength) pushAlong(n, f.rA, f.rB, L - d.minLength);
            continue;
        }
        // linear
        if (j.point) {
            const V3 C = f.d;
            if (LenSq(C) > 1e-12f) {
                M33 K = M33::Zero();
                for (int k = 0; k < 3; ++k) K.m[k][k] = A.invMassAxis[k] + B.invMassAxis[k];
                if (A.dynamic) { const M33 S = Skew(f.rA); K = K + (S * A.invI * S) * -1.0f; }
                if (B.dynamic) { const M33 S = Skew(f.rB); K = K + (S * B.invI * S) * -1.0f; }
                V3 Cc = C;
                const float l = Len(Cc);
                if (l > s.maxCorrection) Cc *= s.maxCorrection / l;
                const V3 P = (K.Inverse() * Cc) * -1.0f;
                moveLin(P, f.rA, f.rB);
            }
        } else {
            const V3 armA = f.rA + f.d;
            for (int k = 0; k < 3; ++k) {
                const JointAxis& ax = d.linear[k];
                const float x = Dot(f.d, f.ax[k]);
                if (ax.mode == AxisMode::Locked && ax.springFrequency <= 0.0f) pushAlong(f.ax[k], armA, f.rB, x);
                else if (ax.mode == AxisMode::Limited) {
                    if (x < ax.lower) pushAlong(f.ax[k], armA, f.rB, x - ax.lower);
                    else if (x > ax.upper) pushAlong(f.ax[k], armA, f.rB, x - ax.upper);
                }
            }
        }
        // angular
        f = MakeFrames(A, B, d);
        const bool allLocked = d.angular[0].mode == AxisMode::Locked && d.angular[1].mode == AxisMode::Locked &&
                               d.angular[2].mode == AxisMode::Locked;
        if (allLocked && d.angular[0].springFrequency <= 0.0f && d.angular[1].springFrequency <= 0.0f &&
            d.angular[2].springFrequency <= 0.0f) {
            const V3 rv = Rotate(f.RA, ToRotationVector(f.rel));   // B's frame relative to A's, as a world rotation vector
            for (int k = 0; k < 3; ++k) turn(f.ax[k], Dot(rv, f.ax[k]));
            continue;
        }
        const V3 twistAxis = Normalize(f.ax[0] + f.bx0, f.ax[0]);
        const float value[3] = {f.twist, f.sy, f.sz};
        for (int k = 0; k < 3; ++k) {
            const JointAxis& ax = d.angular[k];
            if (ax.springFrequency > 0.0f) continue;
            const V3 n = k == 0 ? twistAxis : f.ax[k];
            if (ax.mode == AxisMode::Locked) turn(n, value[k]);
            else if (ax.mode == AxisMode::Limited && !(d.useCone && k > 0)) {
                if (value[k] < ax.lower) turn(n, value[k] - ax.lower);
                else if (value[k] > ax.upper) turn(n, value[k] - ax.upper);
            }
        }
        if (d.useCone) {
            f = MakeFrames(A, B, d);
            const float sw = std::sqrt(f.sy * f.sy + f.sz * f.sz);
            if (sw > 1e-6f) {
                const float cy = f.sy / sw, cz = f.sz / sw;
                const float s1 = std::max(d.swingSpan1, 1e-3f), s2 = std::max(d.swingSpan2, 1e-3f);
                const float limit = 1.0f / std::sqrt(cy * cy / (s1 * s1) + cz * cz / (s2 * s2));
                if (sw > limit) turn(Rotate(f.RA, V3{0.0f, cy, cz}), sw - limit);
            }
        }
    }
}

void JointsFinish(World::Impl& w, std::vector<JointSolver>& js, const SolveSettings& s) {
    const float invDt = 1.0f / s.dt;
    for (JointSolver& j : js) {
        JointData& jd = w.joints[j.id];
        V3 F, T;
        if (j.point) F += jd.linImpulse;
        for (int r = 0; r < j.rowCount; ++r) {
            const JointRow& row = j.rows[r];
            if (row.angular) T += row.angB * *row.acc;
            else F += row.lin * *row.acc;
        }
        jd.lastForce = F * invDt;
        jd.lastTorque = T * invDt;
        if (Len(jd.lastForce) > jd.def.breakForce || Len(jd.lastTorque) > jd.def.breakTorque) jd.broken = true;
    }
}

} // namespace phys
} // namespace okay
