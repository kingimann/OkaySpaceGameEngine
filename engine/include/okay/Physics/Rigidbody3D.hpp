#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"
#include <cmath>

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
    /// Coulomb friction coefficient [0..~1]. Contacts lose tangential speed up to
    /// `friction` x the normal impulse, so boxes slow, stack and rest on slopes instead
    /// of sliding forever. Combined between the two bodies (geometric mean). 0 = ice.
    float    friction     = 0.5f;
    /// Terminal fall speed (world units/s). The downward velocity is clamped to this
    /// so a long fall can't accelerate without bound (more stable, no tunnelling
    /// through thin floors at high frame-step). 0 = no clamp.
    float    maxFallSpeed = 0.0f;
    /// Freeze movement on selected world axes (e.g. lock Y for a top-down game).
    bool     freezeX = false, freezeY = false, freezeZ = false;

    /// True for the frames this body is resting on heightmap Terrain. Set by
    /// Physics3D's terrain ground-follow (heightmap terrain has no polygon
    /// collider, so it produces no collision contacts). Controllers read it as a
    /// ground signal so you can jump, refill jumps, etc. while standing on terrain.
    bool     groundedOnTerrain = false;

    /// World position at the end of the previous physics step — used for swept
    /// (continuous) voxel collision so a fast-moving body can't tunnel through a
    /// thin floor/wall in one big step (e.g. at a low frame rate). Maintained by
    /// Physics3D; `hasPrevPos` is false until the first step records it.
    Vec3     prevPos = Vec3::Zero;
    bool     hasPrevPos = false;

    /// Apply a continuous force (integrated over the next step, scaled by mass).
    void AddForce(const Vec3& force) { m_forceAccum = m_forceAccum + force; }
    /// Apply an instantaneous change in momentum (immediate velocity change).
    void AddImpulse(const Vec3& impulse) { velocity = velocity + impulse * InvMass(); }

    /// Unity-style explosion: shove this body away from `center` with `force`, falling
    /// off linearly to zero at `radius` (bodies past the radius are untouched).
    /// `upModifier` biases the push upward (so debris pops up, not just outward).
    /// No effect on kinematic/static bodies (infinite mass).
    void AddExplosionForce(float force, const Vec3& center, float radius, float upModifier = 0.0f) {
        if (!gameObject || !gameObject->transform || radius <= 0.0f) return;
        Vec3 p = gameObject->transform->Position();
        Vec3 d{p.x - center.x, p.y - center.y, p.z - center.z};
        float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (dist > radius) return;
        float falloff = 1.0f - dist / radius;
        Vec3 dir = dist > 1e-5f ? Vec3{d.x / dist, d.y / dist, d.z / dist} : Vec3{0.0f, 1.0f, 0.0f};
        dir.y += upModifier;
        AddImpulse(dir * (force * falloff));
    }

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
