#pragma once
#include "okay/Physics/BodyPose.hpp"
#include "okay/Physics/PoseHistory.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Physics/ForceMode.hpp"
#include <cmath>

namespace okay {

/// 2D physics body, modeled after Rigidbody2D. Dynamic bodies are moved
/// by the Physics2D integrator (gravity, forces, velocity); Kinematic bodies
/// move only by their velocity; Static bodies don't move and act as immovable
/// colliders.
class Rigidbody2D : public Component {
public:
    enum class BodyType { Dynamic, Kinematic, Static };

    BodyType bodyType   = BodyType::Dynamic;
    Vec2     velocity   = Vec2::Zero;
    float    gravityScale = 1.0f;
    float    mass       = 1.0f;
    float    drag       = 0.0f;     // linear damping per second
    float    bounciness = 0.0f;     // restitution [0,1]
    float    friction   = 0.4f;     // Coulomb friction coefficient (0 = ice)
    /// Continuous collision detection: sweep the body along each step's motion so a
    /// fast object (bullet, thrown ball) stops at a thin wall instead of jumping
    /// past it between two frames. Opt-in (a circle cast per fast step).
    bool     continuousCollision = false;

    // ---- Angular dynamics (rotation about Z) ----
    /// Spin rate in DEGREES per second.
    float    angularVelocity = 0.0f;
    /// Angular damping per second (bleeds off spin like `drag` does for velocity).
    float    angularDrag = 0.05f;
    /// Lock Z rotation. Defaults TRUE so bodies behave exactly as before (no spin)
    /// until you opt in — uncheck it to let torque and off-center hits rotate the
    /// body (boxes tip and tumble, wheels spin, etc.).
    bool     freezeRotation = true;

    // ---- Sleeping ----
    /// May this body fall asleep once it comes to rest? A sleeping body is skipped
    /// by the integrator and solver (no CPU cost, no jitter) until something wakes
    /// it: a collision from a moving body, an applied force/impulse, or a directly
    /// set velocity. Turn off for bodies that must stay perfectly live.
    bool     allowSleep = true;
    /// Runtime: true while asleep. Not authored — set/cleared by Physics2D.
    bool     sleeping   = false;
    /// Runtime: poses for render interpolation (Scene::interpolatePhysics).
    PoseHistory poseHistory;
    /// Runtime: the parent this body was carried by last step. Not authored.
    ParentSample parentCarry;
    /// Force this body awake (call after teleporting or externally moving it).
    void WakeUp() { sleeping = false; m_sleepTimer = 0.0f; }

    // The velocity, force, impulse and gravity-scale calls below never store a NaN or infinite number (issue #888,
    // finding 31), exactly as on Rigidbody3D: such a call changes nothing (the body is not woken either) and the
    // first one on each body logs one warning that names the object. Finite values give what they gave before.
    // (Writing the public `velocity` / `gravityScale` fields directly is not checked.)

    /// velocity = v, unless v is NaN or infinite. True when stored. It does not wake the body (call WakeUp()).
    bool SetVelocity(const Vec2& v) {
        if (!AcceptFinite(v, "velocity")) return false;
        velocity = v;
        return true;
    }
    /// gravityScale = s, unless s is NaN or infinite. True when stored.
    bool SetGravityScale(float s) {
        if (!AcceptFinite(Vec2{s, 0.0f}, "gravity scale")) return false;
        gravityScale = s;
        return true;
    }

    /// Apply a continuous force (integrated over the next step, scaled by mass).
    void AddForce(const Vec2& force) {
        const Vec2 f = m_forceAccum + force;
        if (!AcceptFinite(f, "force")) return;
        m_forceAccum = f;
        WakeUp();
    }
    /// Apply a force with an explicit ForceMode.
    void AddForce(const Vec2& force, ForceMode mode) {
        switch (mode) {
            case ForceMode::Force:
            case ForceMode::Acceleration: {
                const Vec2 f = m_forceAccum + (mode == ForceMode::Force ? force : force * mass);
                if (!AcceptFinite(f, "force")) return;
                m_forceAccum = f;
                break;
            }
            case ForceMode::Impulse:
            case ForceMode::VelocityChange: {
                const Vec2 v = velocity + (mode == ForceMode::Impulse ? force * InvMass() : force);
                if (!AcceptFinite(v, "velocity")) return;
                velocity = v;
                break;
            }
        }
        WakeUp();
    }
    /// Apply an instantaneous change in momentum (immediate velocity change).
    void AddImpulse(const Vec2& impulse) {
        const Vec2 v = velocity + impulse * InvMass();
        if (!AcceptFinite(v, "velocity")) return;
        velocity = v;
        WakeUp();
    }

    /// Apply a torque about Z (continuous, integrated next step, scaled by inertia).
    void AddTorque(float torque) { m_torqueAccum += torque; WakeUp(); }

    /// Apply a force at a world point, producing both linear force and a torque
    /// from the lever arm. Off-center hits spin the body.
    void AddForceAtPosition(const Vec2& force, const Vec2& point) {
        const Vec2 f = m_forceAccum + force;
        Vec3 c = transform ? transform->Position() : Vec3::Zero;
        Vec2 r{point.x - c.x, point.y - c.y};
        const float t = m_torqueAccum + (r.x * force.y - r.y * force.x);   // 2D cross product
        if (!AcceptFinite(f, "force") || !AcceptFinite(Vec2{t, 0.0f}, "torque")) return;
        m_forceAccum = f;
        m_torqueAccum = t;
        WakeUp();
    }

    /// Explosion: shove this body away from `center` with `force`, falling off
    /// linearly to zero at `radius` (bodies past the radius are untouched).
    /// `upModifier` biases the push upward (so debris pops up, not just outward).
    /// No effect on kinematic/static bodies (infinite mass). Same rule as
    /// Rigidbody3D::AddExplosionForce, in the XY plane.
    void AddExplosionForce(float force, const Vec2& center, float radius, float upModifier = 0.0f) {
        if (!gameObject || !gameObject->transform || radius <= 0.0f) return;
        Vec3 p = gameObject->transform->Position();
        Vec2 d{p.x - center.x, p.y - center.y};
        float dist = std::sqrt(d.x * d.x + d.y * d.y);
        if (dist > radius) return;
        float falloff = 1.0f - dist / radius;
        Vec2 dir = dist > 1e-5f ? Vec2{d.x / dist, d.y / dist} : Vec2{0.0f, 1.0f};
        dir.y += upModifier;
        AddImpulse(dir * (force * falloff));
    }

    /// Back to rest, as a fresh body starts: no velocity, no spin, no force or torque waiting for the next
    /// step, awake, and no render-interpolation history (so a reused body is not drawn sliding in from
    /// where it was last). Called by an ObjectPool when it hands the object out again.
    void OnPoolReuse() override {
        velocity = Vec2::Zero; angularVelocity = 0.0f;
        m_forceAccum = Vec2::Zero; m_torqueAccum = 0.0f;
        WakeUp();
        poseHistory = PoseHistory{};
        parentCarry = ParentSample{};
    }

    /// 1/mass for dynamic bodies, 0 for kinematic/static (treated as infinite).
    float InvMass() const {
        return (bodyType == BodyType::Dynamic && mass > 0.0f) ? 1.0f / mass : 0.0f;
    }

private:
    friend class Physics2D;
    Vec2  m_forceAccum  = Vec2::Zero;
    float m_torqueAccum = 0.0f;
    float m_sleepTimer  = 0.0f;   // seconds spent under the sleep thresholds
    bool  m_warnedNotFinite = false;
    // True when both axes of `v` are finite. Otherwise logs the warning above (once per body, naming `what`)
    // and answers false, so the caller keeps the old value. Physics2D.cpp; the check is okay::Finite.
    bool AcceptFinite(const Vec2& v, const char* what);
    Vec2  ConsumeForce()  { Vec2 f = m_forceAccum; m_forceAccum = Vec2::Zero; return f; }
    float ConsumeTorque() { float t = m_torqueAccum; m_torqueAccum = 0.0f; return t; }
};

} // namespace okay
