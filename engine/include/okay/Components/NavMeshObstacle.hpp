#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"
#include <cmath>

namespace okay {

/// A moving box NavMeshAgents steer around (a cart, a closing door, a crowd
/// barrier that slides). It is not baked into the navmesh; agents avoid it
/// locally, so it can move every frame. For walls that never move, bake instead.
class NavMeshObstacle : public Behaviour {
public:
    Vec3 size{1.0f, 1.0f, 1.0f};    ///< the box, in local units (scaled by the Transform)
    Vec3 center{0.0f, 0.0f, 0.0f};  ///< offset from the Transform, in local units

    Vec3 WorldCenter() const {
        if (!transform) return center;
        return transform->Position() + transform->Rotation() * (center * transform->LossyScale());
    }
    /// Radius of the circle around the box's footprint (what agents keep clear of).
    float Radius() const {
        Vec3 s = transform ? transform->LossyScale() : Vec3::One;
        float hx = std::fabs(size.x * s.x) * 0.5f, hz = std::fabs(size.z * s.z) * 0.5f;
        return std::sqrt(hx * hx + hz * hz);
    }
    /// How the obstacle moved last frame (world units per second), for avoidance.
    Vec3 Velocity() const { return m_velocity; }

    void Start() override { m_last = WorldCenter(); m_haveLast = true; }
    void Update(float dt) override {
        Vec3 c = WorldCenter();
        if (m_haveLast && dt > 1e-6f) m_velocity = (c - m_last) * (1.0f / dt);
        m_last = c; m_haveLast = true;
    }

private:
    Vec3 m_velocity{0, 0, 0}, m_last{0, 0, 0};
    bool m_haveLast = false;
};

} // namespace okay
