#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Physics/ForceMode.hpp"

namespace okay {

/// 2D physics body, modeled after Unity's Rigidbody2D. Dynamic bodies are moved
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

    // ---- Angular dynamics (rotation about Z) ----
    /// Spin rate in DEGREES per second (Unity's Rigidbody2D.angularVelocity).
    float    angularVelocity = 0.0f;
    /// Angular damping per second (bleeds off spin like `drag` does for velocity).
    float    angularDrag = 0.05f;
    /// Lock Z rotation. Defaults TRUE so bodies behave exactly as before (no spin)
    /// until you opt in — uncheck it to let torque and off-center hits rotate the
    /// body (boxes tip and tumble, wheels spin, etc.).
    bool     freezeRotation = true;

    /// Apply a continuous force (integrated over the next step, scaled by mass).
    void AddForce(const Vec2& force) { m_forceAccum += force; }
    /// Apply a force with an explicit Unity-style ForceMode.
    void AddForce(const Vec2& force, ForceMode mode) {
        switch (mode) {
            case ForceMode::Force:          m_forceAccum += force; break;
            case ForceMode::Acceleration:   m_forceAccum += force * mass; break;
            case ForceMode::Impulse:        velocity += force * InvMass(); break;
            case ForceMode::VelocityChange: velocity += force; break;
        }
    }
    /// Apply an instantaneous change in momentum (immediate velocity change).
    void AddImpulse(const Vec2& impulse) { velocity += impulse * InvMass(); }

    /// Apply a torque about Z (continuous, integrated next step, scaled by inertia).
    void AddTorque(float torque) { m_torqueAccum += torque; }

    /// Apply a force at a world point, producing both linear force and a torque
    /// from the lever arm (Unity's AddForceAtPosition). Off-center hits spin the body.
    void AddForceAtPosition(const Vec2& force, const Vec2& point) {
        m_forceAccum += force;
        Vec3 c = transform ? transform->Position() : Vec3::Zero;
        Vec2 r{point.x - c.x, point.y - c.y};
        m_torqueAccum += r.x * force.y - r.y * force.x;   // 2D cross product
    }

    /// 1/mass for dynamic bodies, 0 for kinematic/static (treated as infinite).
    float InvMass() const {
        return (bodyType == BodyType::Dynamic && mass > 0.0f) ? 1.0f / mass : 0.0f;
    }

private:
    friend class Physics2D;
    Vec2  m_forceAccum  = Vec2::Zero;
    float m_torqueAccum = 0.0f;
    Vec2  ConsumeForce()  { Vec2 f = m_forceAccum; m_forceAccum = Vec2::Zero; return f; }
    float ConsumeTorque() { float t = m_torqueAccum; m_torqueAccum = 0.0f; return t; }
};

} // namespace okay
