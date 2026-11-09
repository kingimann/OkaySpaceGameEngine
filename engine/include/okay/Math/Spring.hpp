#pragma once
// Exact damped springs: move a value (and its velocity) toward a target the way a
// spring would, for any step length.
//
// Integrating a spring force each frame blows up when a frame is long (a stiff
// spring and one 0.3 s hitch is enough). These functions use the closed-form
// solution of the spring equation instead, so the result is the same whether you
// step once with dt = 2 or two hundred times with dt = 0.01, and a huge dt just
// lands on the target. Unlike a tween, a spring carries its velocity: change the
// target at any moment and the motion bends smoothly toward the new one.
//
//     float x = 0, v = 0;
//     Spring::Step(x, v, target, /*stiffness*/ 120.0f, /*damping*/ 14.0f, dt);   // rings a little
//     Spring::StepCritical(x, v, target, /*smoothTime*/ 0.25f, dt);               // no overshoot
//
//     Spring follow; follow.SetCritical(0.2f);        // or keep the state in one object
//     follow.target = 5.0f;  follow.Update(dt);  use(follow.position);
//
// `stiffness` is k in  x'' = -k (x - target) - c x'  (1/s^2: the spring rings at about
// sqrt(k) radians per second) and `damping` is c (1/s). damping = 2 sqrt(k) is critical
// (the quickest approach without overshoot); less rings, more is sluggish. A stiffness
// of zero or less leaves a pure friction glide. A step that is zero, negative, NaN or
// infinite changes nothing; a non-finite value snaps to the target.
//
// The Step functions are pure: deterministic, no state, no allocation, safe from any thread.
#include "okay/Math/Mathf.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include <cmath>

namespace okay {

struct Spring {
    // ---- A spring you keep: value, velocity, target and its two constants ----
    float position = 0.0f, velocity = 0.0f, target = 0.0f;
    float stiffness = 120.0f, damping = 14.0f;

    void Update(float dt) { Step(position, velocity, target, stiffness, damping, dt); }
    /// Jump to `value` with no motion.
    void Snap(float value) { position = target = value; velocity = 0.0f; }
    /// Within `eps` of the target and slower than `eps`.
    bool AtRest(float eps = 1e-3f) const { return std::fabs(position - target) <= eps && std::fabs(velocity) <= eps; }
    /// Critically damped (no overshoot) arriving in about `smoothTime` seconds.
    void SetCritical(float smoothTime) {
        const float w = 2.0f / Floor(smoothTime);
        stiffness = w * w;
        damping = 2.0f * w;
    }

    // ---- Stateless steps on values you own ----

    /// Move `pos` / `vel` toward `target` by `dt` seconds.
    static void Step(float& pos, float& vel, float target, float stiffness, float damping, float dt) {
        if (!(dt > 0.0f) || !std::isfinite(dt)) return;
        if (!std::isfinite(target)) return;                              // nothing to chase
        if (!std::isfinite(pos) || !std::isfinite(vel)) { pos = target; vel = 0.0f; return; }
        const double c = std::isfinite(damping) && damping > 0.0f ? (double)damping : 0.0;
        const double d0 = (double)pos - (double)target;
        const double v0 = (double)vel;
        const double t = (double)dt;
        double d = 0.0, v = 0.0;
        if (!(stiffness > 1e-9f) || !std::isfinite(stiffness)) {
            // No spring, only friction: the velocity decays as exp(-c t) and the value coasts.
            if (c < 1e-9) { d = d0 + v0 * t; v = v0; }
            else { const double e = std::exp(-c * t); d = d0 + v0 * (1.0 - e) / c; v = v0 * e; }
        } else {
            const double w0 = std::sqrt((double)stiffness);
            const double zeta = c / (2.0 * w0);
            const double a = zeta * w0;                                  // decay rate
            if (std::fabs(zeta - 1.0) < 1e-4) {                          // critically damped
                const double e = std::exp(-a * t);
                const double b = v0 + a * d0;
                d = e * (d0 + b * t);
                v = e * (v0 - a * b * t);
            } else if (zeta < 1.0) {                                     // rings: a damped cosine
                const double wd = w0 * std::sqrt(1.0 - zeta * zeta);
                const double e = std::exp(-a * t), cs = std::cos(wd * t), sn = std::sin(wd * t);
                d = e * (d0 * cs + ((v0 + a * d0) / wd) * sn);
                v = e * (v0 * cs - ((a * v0 + w0 * w0 * d0) / wd) * sn);
            } else {                                                     // overdamped: two decays
                const double b = w0 * std::sqrt(zeta * zeta - 1.0);      // b < a, so both exponents fall
                const double e1 = std::exp(-(a - b) * t), e2 = std::exp(-(a + b) * t);
                const double ch = 0.5 * (e1 + e2), sh = 0.5 * (e1 - e2); // e^-at cosh(bt), e^-at sinh(bt)
                d = d0 * ch + ((v0 + a * d0) / b) * sh;
                v = v0 * ch - ((a * v0 + w0 * w0 * d0) / b) * sh;
            }
        }
        pos = (float)(d + (double)target);
        vel = (float)v;
    }

    /// The same spring made critically damped, described by a time like Mathf::SmoothDamp's
    /// `smoothTime` (about how long it takes to arrive): no overshoot, any step length, and
    /// the velocity is kept, so retargeting is smooth. `smoothTime` has a floor of 0.0001.
    static void StepCritical(float& pos, float& vel, float target, float smoothTime, float dt) {
        const float w = 2.0f / Floor(smoothTime);
        Step(pos, vel, target, w * w, 2.0f * w, dt);
    }

    static void StepVec2(Vec2& pos, Vec2& vel, const Vec2& target, float stiffness, float damping, float dt) {
        Step(pos.x, vel.x, target.x, stiffness, damping, dt);
        Step(pos.y, vel.y, target.y, stiffness, damping, dt);
    }
    static void StepVec3(Vec3& pos, Vec3& vel, const Vec3& target, float stiffness, float damping, float dt) {
        Step(pos.x, vel.x, target.x, stiffness, damping, dt);
        Step(pos.y, vel.y, target.y, stiffness, damping, dt);
        Step(pos.z, vel.z, target.z, stiffness, damping, dt);
    }
    static void StepCriticalVec2(Vec2& pos, Vec2& vel, const Vec2& target, float smoothTime, float dt) {
        StepCritical(pos.x, vel.x, target.x, smoothTime, dt);
        StepCritical(pos.y, vel.y, target.y, smoothTime, dt);
    }
    static void StepCriticalVec3(Vec3& pos, Vec3& vel, const Vec3& target, float smoothTime, float dt) {
        StepCritical(pos.x, vel.x, target.x, smoothTime, dt);
        StepCritical(pos.y, vel.y, target.y, smoothTime, dt);
        StepCritical(pos.z, vel.z, target.z, smoothTime, dt);
    }

    /// An angle in degrees, taking the short way round (350 chasing 10 goes up through
    /// 360). The result is near the target, not wrapped into 0..360.
    static void StepAngle(float& degrees, float& velocity, float targetDegrees, float stiffness, float damping, float dt) {
        if (!std::isfinite(degrees) || !std::isfinite(targetDegrees)) { Step(degrees, velocity, targetDegrees, stiffness, damping, dt); return; }
        Step(degrees, velocity, degrees + Mathf::DeltaAngle(degrees, targetDegrees), stiffness, damping, dt);
    }

    /// A rotation chasing `target` along the shortest arc. `angularVelocity` is in radians
    /// per second about the world axes. The spring acts on the error angle, so ringing and
    /// critical damping behave as they do for a number.
    static void StepQuat(Quat& rot, Vec3& angularVelocity, const Quat& target, float stiffness, float damping, float dt) {
        if (!(dt > 0.0f) || !std::isfinite(dt)) return;
        const Quat tn = target.Normalized();
        // The turn from the target to the current rotation, as a vector: the "position" the spring acts on.
        Vec3 err = ToRotationVector(rot.Normalized() * tn.Inverse());
        Step(err.x, angularVelocity.x, 0.0f, stiffness, damping, dt);
        Step(err.y, angularVelocity.y, 0.0f, stiffness, damping, dt);
        Step(err.z, angularVelocity.z, 0.0f, stiffness, damping, dt);
        rot = (FromRotationVector(err) * tn).Normalized();
    }
    static void StepQuatCritical(Quat& rot, Vec3& angularVelocity, const Quat& target, float smoothTime, float dt) {
        const float w = 2.0f / Floor(smoothTime);
        StepQuat(rot, angularVelocity, target, w * w, 2.0f * w, dt);
    }

private:
    static float Floor(float smoothTime) { return smoothTime > 0.0001f ? smoothTime : 0.0001f; }
    // The rotation that turns by the rotation vector `r` (axis * radians).
    static Quat FromRotationVector(const Vec3& r) {
        const double len = std::sqrt((double)r.x * r.x + (double)r.y * r.y + (double)r.z * r.z);
        if (len < 1e-9) return Quat{r.x * 0.5f, r.y * 0.5f, r.z * 0.5f, 1.0f}.Normalized();
        const double half = 0.5 * len, k = std::sin(half) / len;
        return Quat{(float)(r.x * k), (float)(r.y * k), (float)(r.z * k), (float)std::cos(half)};
    }
    // The shortest rotation vector (axis * radians) of a unit quaternion.
    static Vec3 ToRotationVector(Quat q) {
        if (q.w < 0.0f) q = Quat{-q.x, -q.y, -q.z, -q.w};
        const double s = std::sqrt((double)q.x * q.x + (double)q.y * q.y + (double)q.z * q.z);
        if (s < 1e-9) return Vec3{q.x * 2.0f, q.y * 2.0f, q.z * 2.0f};
        const double k = 2.0 * std::atan2(s, (double)q.w) / s;
        return Vec3{(float)(q.x * k), (float)(q.y * k), (float)(q.z * k)};
    }
};

} // namespace okay
