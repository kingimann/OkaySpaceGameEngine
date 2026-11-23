// RagdollPhysics - our own small articulated rigid-body solver for the
// Ragdoll component. Written from the method in Mueller et al., "Detailed Rigid
// Body Simulation with Extended Position Based Dynamics" (SCA 2020): each
// frame is cut into substeps; each substep predicts the poses, applies one
// pass of positional / angular corrections (joints, limits, drives, contacts),
// derives the velocities from the pose change, then runs a velocity pass for
// damping. Compliance (1 / stiffness) turns a joint drive into a PD spring.
#include "okay/Animation/RagdollPhysics.hpp"
#include "okay/Math/JointLimit.hpp"   // TwistAbout: the swing / twist split shared with the IK limits
#include "okay/Math/Mathf.hpp"
#include "okay/Physics/ShapeKernel3D.hpp"   // LocalShape3D::Ray: the shared ray test
#include <cmath>

namespace okay {
namespace dyn {

namespace {
bool RpFinite(float f) { return std::isfinite(f); }
bool RpFinite(const Vec3& v) { return RpFinite(v.x) && RpFinite(v.y) && RpFinite(v.z); }
bool RpFinite(const Quat& q) { return RpFinite(q.x) && RpFinite(q.y) && RpFinite(q.z) && RpFinite(q.w); }

// q + 0.5 * (rot, 0) * q, renormalised: turn q by the small rotation vector `rot`.
Quat Turn(const Quat& q, const Vec3& rot, float sign = 1.0f) {
    const Quat r{rot.x * sign, rot.y * sign, rot.z * sign, 0.0f};
    const Quat d = r * q;
    Quat o{q.x + 0.5f * d.x, q.y + 0.5f * d.y, q.z + 0.5f * d.z, q.w + 0.5f * d.w};
    const float m = o.Magnitude();
    return m > 1e-12f ? Quat{o.x / m, o.y / m, o.z / m, o.w / m} : q;
}

// Rotation vector (axis * angle, radians) of a unit quaternion, shortest way.
Vec3 RotVec(Quat q) {
    if (q.w < 0.0f) q = Quat{-q.x, -q.y, -q.z, -q.w};
    const Vec3 v{q.x, q.y, q.z};
    const float s = v.Magnitude();
    if (s < 1e-9f) return v * 2.0f;
    const float ang = 2.0f * std::atan2(s, q.w);
    return v * (ang / s);
}

float WrapPi(float a) {
    while (a > Mathf::PI) a -= 2.0f * Mathf::PI;
    while (a < -Mathf::PI) a += 2.0f * Mathf::PI;
    return a;
}
} // namespace

// ---- SimpleWorld --------------------------------------------------------------
bool SimpleWorld::Cast(const Vec3& from, const Vec3& to, Vec3& point, Vec3& normal) const {
    float best = 2.0f;
    const Vec3 d = to - from;
    if (hasGround && from.y >= groundY && to.y <= groundY && d.y < 0.0f) {
        const float t = (groundY - from.y) / d.y;
        if (t >= 0.0f && t <= 1.0f) { best = t; normal = Vec3::Up; }
    }
    for (const Box& b : boxes) {
        // The shared ray-box test (Physics/ShapeKernel3D); a segment that starts inside never hits.
        LocalShape3D box;
        box.kind = LocalShape3D::Kind::Box;
        box.half = (b.max - b.min) * 0.5f;
        float t; Vec3 n;
        if (!box.Ray(from - (b.min + b.max) * 0.5f, d, 1.0f, t, n) || t >= best) continue;
        best = t;
        normal = n;
    }
    if (best > 1.0f) return false;
    point = from + d * best;
    return true;
}

// ---- XpbdRagdoll --------------------------------------------------------------
void XpbdRagdoll::Clear() { m_bodies.clear(); m_joints.clear(); m_resets = 0; }

int XpbdRagdoll::AddBody(const BodyDesc& d) {
    Body b;
    b.x = b.px = RpFinite(d.position) ? d.position : Vec3::Zero;
    b.q = b.pq = RpFinite(d.rotation) && d.rotation.Magnitude() > 1e-6f ? d.rotation.Normalized() : Quat::Identity;
    b.mass = RpFinite(d.mass) && d.mass > 1e-3f ? d.mass : 1e-3f;
    b.invMass = 1.0f / b.mass;
    for (int a = 0; a < 3; ++a) {
        const float I = RpFinite(d.inertia[a]) && d.inertia[a] > 1e-6f ? d.inertia[a] : 1e-6f;
        b.invI[a] = 1.0f / I;
    }
    b.friction = RpFinite(d.friction) ? Mathf::Clamp(d.friction, 0.0f, 2.0f) : 0.7f;
    for (const ContactSphere& s : d.spheres) {
        if (!RpFinite(s.local) || !RpFinite(s.radius) || s.radius <= 0.0f) continue;
        Sphere sp; sp.local = s.local; sp.radius = s.radius;
        b.spheres.push_back(sp);
    }
    b.good = {b.x, b.q, Vec3::Zero, Vec3::Zero};
    m_bodies.push_back(b);
    return (int)m_bodies.size() - 1;
}

int XpbdRagdoll::AddJoint(const JointDesc& d) {
    const int n = (int)m_bodies.size();
    if (d.parent < 0 || d.child < 0 || d.parent >= n || d.child >= n || d.parent == d.child) return -1;
    const Body& P = m_bodies[d.parent];
    const Body& C = m_bodies[d.child];
    Joint j;
    j.p = d.parent; j.c = d.child;
    const Vec3 anchor = RpFinite(d.anchor) ? d.anchor : C.x;
    j.rp = P.q.Inverse() * (anchor - P.x);
    j.rc = C.q.Inverse() * (anchor - C.x);
    j.rest = (P.q.Inverse() * C.q).Normalized();
    j.axisInParent = P.q.Inverse() * (C.q * Vec3::Up);
    j.hinge = d.hinge;
    j.swing = RpFinite(d.swing) ? Mathf::Clamp(d.swing, 0.0f, Mathf::PI) : 1.0f;
    j.twist = RpFinite(d.twist) ? Mathf::Clamp(d.twist, 0.0f, Mathf::PI) : 0.5f;
    Vec3 ax = RpFinite(d.hingeAxis) && d.hingeAxis.SqrMagnitude() > 1e-8f ? d.hingeAxis.Normalized() : Vec3::Right;
    j.hingeP = P.q.Inverse() * ax;
    j.hingeC = C.q.Inverse() * ax;
    j.refP = j.axisInParent;
    j.hingeMin = RpFinite(d.hingeMin) ? d.hingeMin : 0.0f;
    j.hingeMax = RpFinite(d.hingeMax) ? d.hingeMax : 2.0f;
    if (j.hingeMax < j.hingeMin) { const float t = j.hingeMin; j.hingeMin = j.hingeMax; j.hingeMax = t; }
    m_joints.push_back(j);
    return (int)m_joints.size() - 1;
}

void XpbdRagdoll::SetDrive(int joint, const JointDrive& d) {
    if (joint < 0 || joint >= (int)m_joints.size()) return;
    JointDrive& j = m_joints[joint].drive;
    j.useDrive = d.useDrive;
    if (RpFinite(d.target) && d.target.Magnitude() > 1e-6f) j.target = d.target.Normalized();
    auto clampF = [](float v, float hi, float fallback) { return RpFinite(v) ? Mathf::Clamp(v, 0.0f, hi) : fallback; };
    j.stiffness = clampF(d.stiffness, 1e7f, 0.0f);
    j.damping = clampF(d.damping, 1e7f, 0.0f);
    j.frequency = clampF(d.frequency, 1000.0f, 0.0f);
    j.dampingRatio = clampF(d.dampingRatio, 10.0f, 1.0f);
    j.maxTorque = clampF(d.maxTorque, 1e7f, 0.0f);
}

void XpbdRagdoll::SetState(int body, const BodyState& s) {
    if (body < 0 || body >= (int)m_bodies.size()) return;
    Body& b = m_bodies[body];
    if (RpFinite(s.position)) b.x = s.position;
    if (RpFinite(s.rotation) && s.rotation.Magnitude() > 1e-6f) b.q = s.rotation.Normalized();
    b.v = RpFinite(s.velocity) ? Vec3::ClampMagnitude(s.velocity, maxSpeed) : Vec3::Zero;
    b.w = RpFinite(s.angularVelocity) ? Vec3::ClampMagnitude(s.angularVelocity, maxSpin) : Vec3::Zero;
    b.px = b.x; b.pq = b.q;
    for (Sphere& sp : b.spheres) { sp.hasPlane = false; sp.touching = false; }
    b.good = {b.x, b.q, b.v, b.w};
}

BodyState XpbdRagdoll::State(int body) const {
    if (body < 0 || body >= (int)m_bodies.size()) return {Vec3::Zero, Quat::Identity, Vec3::Zero, Vec3::Zero};
    const Body& b = m_bodies[body];
    return {b.x, b.q, b.v, b.w};
}

float XpbdRagdoll::Mass(int body) const {
    return body >= 0 && body < (int)m_bodies.size() ? m_bodies[body].mass : 0.0f;
}

Vec3 XpbdRagdoll::InvIWorld(const Body& b, const Vec3& v) const {
    Vec3 l = b.q.Inverse() * v;
    l = Vec3{l.x * b.invI.x, l.y * b.invI.y, l.z * b.invI.z};
    return b.q * l;
}

float XpbdRagdoll::GenInvMass(const Body* b, const Vec3& r, const Vec3& n) const {
    if (!b) return 0.0f;
    const Vec3 rn = Vec3::Cross(r, n);
    return b->invMass + Vec3::Dot(rn, InvIWorld(*b, rn));
}

float XpbdRagdoll::GenInvInertia(const Body* b, const Vec3& n) const {
    return b ? Vec3::Dot(n, InvIWorld(*b, n)) : 0.0f;
}

void XpbdRagdoll::Positional(Body* a, const Vec3& ra, Body* b, const Vec3& rb, const Vec3& delta, float alpha, float h) {
    const float c = delta.Magnitude();
    if (c < 1e-9f) return;
    const Vec3 n = delta * (1.0f / c);
    const float w = GenInvMass(a, ra, n) + GenInvMass(b, rb, n) + alpha / (h * h);
    if (w < 1e-12f) return;
    const Vec3 P = n * (c / w);
    if (a) { a->x += P * a->invMass; a->q = Turn(a->q, InvIWorld(*a, Vec3::Cross(ra, P))); }
    if (b) { b->x -= P * b->invMass; b->q = Turn(b->q, InvIWorld(*b, Vec3::Cross(rb, P)), -1.0f); }
}

void XpbdRagdoll::Angular(Body* a, Body* b, const Vec3& rot, float alpha, float h, float maxTorque) {
    const float th = rot.Magnitude();
    if (th < 1e-9f) return;
    const Vec3 n = rot * (1.0f / th);
    const float w = GenInvInertia(a, n) + GenInvInertia(b, n) + alpha / (h * h);
    if (w < 1e-12f) return;
    float lambda = th / w;                          // angular impulse of this correction (N m s)
    // XPBD multipliers are positional: torque = lambda / h^2 (Mueller et al. 2020, section 3.3).
    if (maxTorque > 0.0f) lambda = Mathf::Min(lambda, maxTorque * h * h);
    const Vec3 L = n * lambda;
    if (a) a->q = Turn(a->q, InvIWorld(*a, L));
    if (b) b->q = Turn(b->q, InvIWorld(*b, L), -1.0f);
}

void XpbdRagdoll::AngularVelocity(Body* a, Body* b, const Vec3& dw) {
    const float m = dw.Magnitude();
    if (m < 1e-9f) return;
    const Vec3 n = dw * (1.0f / m);
    const float w = GenInvInertia(a, n) + GenInvInertia(b, n);
    if (w < 1e-12f) return;
    const Vec3 L = n * (m / w);
    if (a) a->w += InvIWorld(*a, L);
    if (b) b->w -= InvIWorld(*b, L);
}

void XpbdRagdoll::ApplyImpulse(int body, const Vec3& impulse, const Vec3& atPoint) {
    if (body < 0 || body >= (int)m_bodies.size() || !RpFinite(impulse)) return;
    Body& b = m_bodies[body];
    const Vec3 J = Vec3::ClampMagnitude(impulse, b.mass * maxSpeed);
    const Vec3 r = RpFinite(atPoint) ? Vec3::ClampMagnitude(atPoint - b.x, 2.0f) : Vec3::Zero;
    b.v = Vec3::ClampMagnitude(b.v + J * b.invMass, maxSpeed);
    b.w = Vec3::ClampMagnitude(b.w + InvIWorld(b, Vec3::Cross(r, J)), maxSpin);
}

void XpbdRagdoll::AddForce(int body, const Vec3& f) {
    if (body < 0 || body >= (int)m_bodies.size() || !RpFinite(f)) return;
    m_bodies[body].force += f;
}

void XpbdRagdoll::AddTorque(int body, const Vec3& t) {
    if (body < 0 || body >= (int)m_bodies.size() || !RpFinite(t)) return;
    m_bodies[body].torque += t;
}

void XpbdRagdoll::SetPin(int body, const Vec3& localPoint, const Vec3& worldPoint, float stiffness) {
    if (body < 0 || body >= (int)m_bodies.size() || !RpFinite(localPoint) || !RpFinite(worldPoint) || !RpFinite(stiffness)) return;
    Body& b = m_bodies[body];
    b.pinned = stiffness > 0.0f;
    b.pinLocal = localPoint; b.pinWorld = worldPoint; b.pinK = Mathf::Min(stiffness, 1e7f);
}

void XpbdRagdoll::ClearPin(int body) {
    if (body >= 0 && body < (int)m_bodies.size()) m_bodies[body].pinned = false;
}

void XpbdRagdoll::SetHelper(int body, const Vec3& pos, float posK, const Quat& rot, float rotK, float damping) {
    if (body < 0 || body >= (int)m_bodies.size()) return;
    Body& b = m_bodies[body];
    b.helpXK = RpFinite(pos) && RpFinite(posK) ? Mathf::Clamp(posK, 0.0f, 1e7f) : 0.0f;
    b.helpQK = RpFinite(rot) && rot.Magnitude() > 1e-6f && RpFinite(rotK) ? Mathf::Clamp(rotK, 0.0f, 1e7f) : 0.0f;
    b.helpX = RpFinite(pos) ? pos : b.x;
    b.helpQ = b.helpQK > 0.0f ? rot.Normalized() : Quat::Identity;
    b.helpDamp = RpFinite(damping) ? Mathf::Clamp(damping, 0.0f, 1000.0f) : 0.0f;
}

void XpbdRagdoll::ClearHelpers() {
    for (Body& b : m_bodies) { b.helpXK = 0.0f; b.helpQK = 0.0f; b.helpDamp = 0.0f; }
}

void XpbdRagdoll::FindContacts(float dt, const ContactWorld* world) {
    const float skin = 0.02f;
    for (Body& b : m_bodies) {
        for (Sphere& s : b.spheres) {
            s.touching = false;
            const Vec3 r = b.q * s.local;
            const Vec3 c0 = b.x + r;
            if (!world) { s.hasPlane = false; continue; }
            const Vec3 vel = b.v + Vec3::Cross(b.w, r);
            Vec3 move = vel * dt;
            const float len = move.Magnitude();
            const Vec3 dir = len > 1e-5f ? move * (1.0f / len) : Vec3::Down;
            Vec3 p, n;
            bool hit = world->Cast(c0 - dir * (0.5f * s.radius), c0 + move + dir * (s.radius + skin), p, n);
            if (!hit) hit = world->Cast(c0 + Vec3::Up * s.radius, c0 + Vec3::Down * (s.radius + skin + 0.02f), p, n);
            // A deep overlap starts both casts inside the collider, which then miss:
            // look again from well above. Nothing under it = no surface (the old
            // code kept the last plane, an invisible floor up to 1 m past an edge).
            if (!hit) hit = world->Cast(c0 + Vec3::Up * (s.radius + 0.6f), c0 + Vec3::Down * (s.radius + skin), p, n);
            if (hit && RpFinite(p) && RpFinite(n) && n.SqrMagnitude() > 1e-8f) {
                s.hasPlane = true; s.planePoint = p; s.planeNormal = n.Normalized();
            } else {
                s.hasPlane = false;
            }
        }
    }
}

float XpbdRagdoll::DriveStiffness(const Joint& j, const Body* P, const Body* C) const {
    if (!j.drive.useDrive) return 0.0f;
    if (j.drive.stiffness > 0.0f) return j.drive.stiffness;
    if (j.drive.frequency <= 0.0f) return 0.0f;
    // From a frequency: k = I (2 pi f)^2 with I the joint's inertia about the
    // child's long axis' normal (both bodies, as the solver sees them).
    const Vec3 n = (C->q * Vec3::Right).Normalized();
    const float w = GenInvInertia(C, n) + GenInvInertia(P, n);
    const float om = 2.0f * Mathf::PI * j.drive.frequency;
    return w > 1e-9f ? om * om / w : 0.0f;
}

void XpbdRagdoll::SolveJoint(Joint& j, float h) {
    Body* P = &m_bodies[j.p];
    Body* C = &m_bodies[j.c];
    // Drive first, so the hard constraints below have the last word.
    const float k = DriveStiffness(j, P, C);
    if (k > 0.0f) {
        const Quat want = (P->q * j.rest * j.drive.target).Normalized();
        Angular(C, P, RotVec(want * C->q.Inverse()), 1.0f / k, h, j.drive.maxTorque);
    }
    if (j.hinge) {
        const Vec3 ap = P->q * j.hingeP, ac = C->q * j.hingeC;
        Angular(C, P, Vec3::Cross(ac, ap), 0.0f, h);
        const Vec3 axis = (P->q * j.hingeP).Normalized();
        const Vec3 u = Vec3::ProjectOnPlane(P->q * j.refP, axis);
        const Vec3 v = Vec3::ProjectOnPlane(C->q * Vec3::Up, axis);
        if (u.SqrMagnitude() > 1e-10f && v.SqrMagnitude() > 1e-10f) {
            const float phi = std::atan2(Vec3::Dot(Vec3::Cross(u, v), axis), Vec3::Dot(u, v));
            const float lim = Mathf::Clamp(phi, j.hingeMin, j.hingeMax);
            if (lim != phi) Angular(C, P, axis * (lim - phi), 0.0f, h);
        }
    } else {
        const Vec3 u = P->q * j.axisInParent, v = C->q * Vec3::Up;
        const float cs = Mathf::Clamp(Vec3::Dot(u, v), -1.0f, 1.0f);
        const float ang = std::acos(cs);
        if (ang > j.swing) {
            Vec3 n = Vec3::Cross(v, u);
            if (n.SqrMagnitude() > 1e-12f) Angular(C, P, n.Normalized() * (ang - j.swing), 0.0f, h);
        }
        Quat d = j.rest.Inverse() * (P->q.Inverse() * C->q);
        if (d.w < 0.0f) d = Quat{-d.x, -d.y, -d.z, -d.w};
        float twDeg = 0.0f;
        TwistAbout(d, Vec3::Up, twDeg);
        const float tw = WrapPi(twDeg * Mathf::Deg2Rad);
        const float lim = Mathf::Clamp(tw, -j.twist, j.twist);
        if (lim != tw) Angular(C, P, (C->q * Vec3::Up) * (lim - tw), 0.0f, h);
    }
    // The anchor points meet (hard).
    const Vec3 ra = P->q * j.rp, rc = C->q * j.rc;
    Positional(P, ra, C, rc, (C->x + rc) - (P->x + ra), 0.0f, h);
}

void XpbdRagdoll::SolveContacts(float h) {
    for (Body& b : m_bodies) {
        for (Sphere& s : b.spheres) {
            if (!s.hasPlane) continue;
            const Vec3 r = b.q * s.local;
            const float d = Vec3::Dot(b.x + r - s.planePoint, s.planeNormal) - s.radius;
            if (d > 0.005f) continue;
            s.touching = true;
            if (d >= 0.0f) continue;
            const float wn = GenInvMass(&b, r, s.planeNormal);
            if (wn < 1e-12f) continue;
            const float lamN = -d / wn;
            Positional(&b, r, nullptr, Vec3::Zero, s.planeNormal * (-d), 0.0f, h);
            // Friction (static, capped at mu * normal): undo the sliding of the
            // contact point during this substep.
            const Vec3 r2 = b.q * s.local;
            const Vec3 prev = b.px + b.pq * s.local;
            const Vec3 slide = Vec3::ProjectOnPlane((b.x + r2) - prev, s.planeNormal);
            const float sl = slide.Magnitude();
            if (sl < 1e-9f) continue;
            const Vec3 t = slide * (1.0f / sl);
            const float wt = GenInvMass(&b, r2, t);
            if (wt < 1e-12f) continue;
            const float lamT = sl / wt;
            const float cap = b.friction * lamN;
            const float k = lamT <= cap ? 1.0f : cap / lamT;
            Positional(&b, r2, nullptr, Vec3::Zero, slide * (-k), 0.0f, h);
        }
    }
}

void XpbdRagdoll::Sanitise() {
    bool ok = true;
    for (const Body& b : m_bodies)
        if (!RpFinite(b.x) || !RpFinite(b.q) || !RpFinite(b.v) || !RpFinite(b.w)) { ok = false; break; }
    if (!ok) {
        ++m_resets;
        for (Body& b : m_bodies) {
            b.x = b.px = b.good.position; b.q = b.pq = b.good.rotation;
            b.v = Vec3::Zero; b.w = Vec3::Zero;
            for (Sphere& s : b.spheres) s.hasPlane = false;
        }
        return;
    }
    for (Body& b : m_bodies) b.good = {b.x, b.q, b.v, b.w};
}

void XpbdRagdoll::Step(float dt, const ContactWorld* world) {
    if (m_bodies.empty() || !RpFinite(dt) || dt <= 0.0f) return;
    dt = Mathf::Min(dt, 0.1f);
    FindContacts(dt, world);
    const int n = substeps < 1 ? 1 : (substeps > 64 ? 64 : substeps);
    const float h = dt / (float)n;
    const Vec3 g = RpFinite(gravity) ? gravity : Vec3::Zero;
    for (int sub = 0; sub < n; ++sub) {
        for (Body& b : m_bodies) {
            b.px = b.x; b.pq = b.q;
            b.v += (g + b.force * b.invMass) * h;
            b.x += b.v * h;
            b.w += InvIWorld(b, b.torque) * h;
            b.q = Turn(b.q, b.w * h);
        }
        for (Joint& j : m_joints) SolveJoint(j, h);
        for (Body& b : m_bodies) {
            if (!b.pinned) continue;
            const Vec3 r = b.q * b.pinLocal;
            Positional(&b, r, nullptr, Vec3::Zero, b.pinWorld - (b.x + r), 1.0f / b.pinK, h);
        }
        for (Body& b : m_bodies) {
            if (b.helpXK > 0.0f) Positional(&b, Vec3::Zero, nullptr, Vec3::Zero, b.helpX - b.x, 1.0f / b.helpXK, h);
            if (b.helpQK > 0.0f) Angular(&b, nullptr, RotVec(b.helpQ * b.q.Inverse()), 1.0f / b.helpQK, h);
        }
        SolveContacts(h);
        for (Body& b : m_bodies) {
            b.v = Vec3::ClampMagnitude((b.x - b.px) * (1.0f / h), maxSpeed);
            Quat dq = b.q * b.pq.Inverse();
            if (dq.w < 0.0f) dq = Quat{-dq.x, -dq.y, -dq.z, -dq.w};
            b.w = Vec3::ClampMagnitude(Vec3{dq.x, dq.y, dq.z} * (2.0f / h), maxSpin);
        }
        // Velocity pass: drive damping (the D of the PD) and joint friction.
        for (Joint& j : m_joints) {
            Body* P = &m_bodies[j.p];
            Body* C = &m_bodies[j.c];
            const Vec3 rel = C->w - P->w;
            const float m = rel.Magnitude();
            if (m < 1e-9f) continue;
            const Vec3 nn = rel * (1.0f / m);
            const float wsum = GenInvInertia(C, nn) + GenInvInertia(P, nn);
            if (wsum < 1e-12f) continue;
            float f = Mathf::Min(1.0f, m_jointFriction * h);
            const float k = DriveStiffness(j, P, C);
            if (j.drive.useDrive && (k > 0.0f || j.drive.damping > 0.0f)) {
                const float d = j.drive.damping > 0.0f ? j.drive.damping : 2.0f * j.drive.dampingRatio * std::sqrt(k / wsum);
                f = Mathf::Min(1.0f, f + d * h * wsum);
            }
            if (f > 0.0f) {
                Vec3 dw = rel * (-f);
                // The drive's damping counts toward its torque cap too (Joint3D: never more than driveMaxTorque).
                if (j.drive.useDrive && j.drive.maxTorque > 0.0f) {
                    const float impulse = dw.Magnitude() / wsum, cap = j.drive.maxTorque * h;
                    if (impulse > cap) dw = dw * (cap / impulse);
                }
                AngularVelocity(C, P, dw);
            }
        }
        for (Body& b : m_bodies) {
            if (b.helpDamp <= 0.0f) continue;
            const float k = 1.0f - Mathf::Min(1.0f, b.helpDamp * h);
            if (b.helpXK > 0.0f) b.v = b.v * k;
            if (b.helpQK > 0.0f) b.w = b.w * k;
        }
        // Resting contacts lose their speed into the surface.
        for (Body& b : m_bodies) {
            for (Sphere& s : b.spheres) {
                if (!s.touching) continue;
                const Vec3 r = b.q * s.local;
                const Vec3 vp = b.v + Vec3::Cross(b.w, r);
                const float vn = Vec3::Dot(vp, s.planeNormal);
                if (vn >= 0.0f) continue;
                const float w = GenInvMass(&b, r, s.planeNormal);
                if (w < 1e-12f) continue;
                const Vec3 J = s.planeNormal * (-vn / w);
                b.v += J * b.invMass;
                b.w += InvIWorld(b, Vec3::Cross(r, J));
            }
        }
    }
    for (Body& b : m_bodies) { b.force = Vec3::Zero; b.torque = Vec3::Zero; }
    Sanitise();
}

bool XpbdRagdoll::BodyTouching(int body) const {
    if (body < 0 || body >= (int)m_bodies.size()) return false;
    for (const Sphere& s : m_bodies[body].spheres) if (s.touching) return true;
    return false;
}

int XpbdRagdoll::SphereCount(int body) const {
    return body >= 0 && body < (int)m_bodies.size() ? (int)m_bodies[body].spheres.size() : 0;
}

bool XpbdRagdoll::SphereTouching(int body, int sphere, Vec3* worldCentre, Vec3* normal) const {
    if (body < 0 || body >= (int)m_bodies.size()) return false;
    const Body& b = m_bodies[body];
    if (sphere < 0 || sphere >= (int)b.spheres.size()) return false;
    if (worldCentre) *worldCentre = b.x + b.q * b.spheres[sphere].local;
    if (normal) *normal = b.spheres[sphere].hasPlane ? b.spheres[sphere].planeNormal : Vec3::Up;
    return b.spheres[sphere].touching;
}

float XpbdRagdoll::JointGap(int joint) const {
    if (joint < 0 || joint >= (int)m_joints.size()) return 0.0f;
    const Joint& j = m_joints[joint];
    const Body& P = m_bodies[j.p];
    const Body& C = m_bodies[j.c];
    return Vec3::Distance(P.x + P.q * j.rp, C.x + C.q * j.rc);
}

} // namespace dyn
} // namespace okay
