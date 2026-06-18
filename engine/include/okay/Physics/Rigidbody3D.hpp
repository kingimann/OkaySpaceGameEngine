#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Math/Vec3.hpp"

namespace okay {

/// 3D physics body, modeled after Unity's Rigidbody. Dynamic bodies are moved
/// by the Physics3D integrator (gravity, forces, velocity); Kinematic bodies
/// move only by their velocity; Static bodies don't move and act as immovable
/// colliders.
class Rigidbody3D : public Component {
public:
    enum class BodyType { Dynamic, Kinematic, Static };

    BodyType bodyType     = BodyType::Dynamic;
    Vec3     velocity     = Vec3::Zero;
    float    gravityScale = 1.0f;
    float    mass         = 1.0f;
    float    drag         = 0.0f;     // linear damping per second
    float    bounciness   = 0.0f;     // restitution [0,1]
    /// Freeze movement on selected world axes (e.g. lock Y for a top-down game).
    bool     freezeX = false, freezeY = false, freezeZ = false;

    /// Apply a continuous force (integrated over the next step, scaled by mass).
    void AddForce(const Vec3& force) { m_forceAccum = m_forceAccum + force; }
    /// Apply an instantaneous change in momentum (immediate velocity change).
    void AddImpulse(const Vec3& impulse) { velocity = velocity + impulse * InvMass(); }

    /// 1/mass for dynamic bodies, 0 for kinematic/static (treated as infinite).
    float InvMass() const {
        return (bodyType == BodyType::Dynamic && mass > 0.0f) ? 1.0f / mass : 0.0f;
    }

private:
    friend class Physics3D;
    Vec3 m_forceAccum = Vec3::Zero;
    Vec3 ConsumeForce() { Vec3 f = m_forceAccum; m_forceAccum = Vec3::Zero; return f; }
};

} // namespace okay
