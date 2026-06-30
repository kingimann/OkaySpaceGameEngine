#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Math/Vec2.hpp"

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

    /// Apply a continuous force (integrated over the next step, scaled by mass).
    void AddForce(const Vec2& force) { m_forceAccum += force; }
    /// Apply an instantaneous change in momentum (immediate velocity change).
    void AddImpulse(const Vec2& impulse) { velocity += impulse * InvMass(); }

    /// 1/mass for dynamic bodies, 0 for kinematic/static (treated as infinite).
    float InvMass() const {
        return (bodyType == BodyType::Dynamic && mass > 0.0f) ? 1.0f / mass : 0.0f;
    }

private:
    friend class Physics2D;
    Vec2 m_forceAccum = Vec2::Zero;
    Vec2 ConsumeForce() { Vec2 f = m_forceAccum; m_forceAccum = Vec2::Zero; return f; }
};

} // namespace okay
