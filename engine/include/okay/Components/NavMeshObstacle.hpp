#pragma once
#include "okay/AI/NavMesh.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

/// A box or capsule NavMeshAgents go around (a cart, a closing door, a crowd barrier that slides).
///
/// Always: agents avoid it locally, so it can move every frame (it is never baked).
///
/// With `carve` on, it also cuts a hole in the navigation mesh while it stands still, so the PATH
/// SEARCH goes around it (a parked cart across a corridor makes agents take the other way, or find no
/// way when none is left) instead of walking into it and wedging. Carving happens at run time, only
/// while the obstacle is enabled and active:
///   - `carveOnlyWhenStationary` (default): the hole exists once the obstacle has not moved for
///     `timeToStationary` seconds, and is filled again the moment it moves. A moving obstacle is
///     only avoided locally, so a patrolling cart does not rebuild the mesh every frame.
///   - off: the hole follows the obstacle, rebuilt whenever it has moved more than `moveThreshold`.
/// Agents already walking replan as soon as the hole appears, moves or goes away.
class NavMeshObstacle : public Behaviour {
public:
    enum class Shape { Box = 0, Capsule = 1 };

    Shape shape = Shape::Box;
    Vec3 size{1.0f, 1.0f, 1.0f};    ///< Box: the box, in local units (scaled by the Transform)
    Vec3 center{0.0f, 0.0f, 0.0f};  ///< offset from the Transform, in local units
    float capsuleRadius = 0.5f;     ///< Capsule: radius, in local units (scaled by the Transform's X/Z)
    float capsuleHeight = 2.0f;     ///< Capsule: total height including both caps, in local units (scaled by Y)
    bool  carve = false;                    ///< cut a hole in the navigation mesh
    bool  carveOnlyWhenStationary = true;   ///< carve only after it has stopped moving
    float timeToStationary = 0.5f;          ///< seconds without moving before it counts as stationary
    float moveThreshold = 0.1f;             ///< not stationary-only: re-carve after moving this far

    Vec3 WorldCenter() const {
        if (!transform) return center;
        return transform->Position() + transform->Rotation() * (center * transform->LossyScale());
    }
    /// Radius of the circle around the footprint (what agents keep clear of).
    float Radius() const {
        Vec3 s = transform ? transform->LossyScale() : Vec3::One;
        if (shape == Shape::Capsule) return std::fabs(capsuleRadius) * std::max(std::fabs(s.x), std::fabs(s.z));
        float hx = std::fabs(size.x * s.x) * 0.5f, hz = std::fabs(size.z * s.z) * 0.5f;
        return std::sqrt(hx * hx + hz * hz);
    }
    /// How the obstacle moved last frame (world units per second), for avoidance.
    Vec3 Velocity() const { return m_velocity; }

    /// The solid this obstacle fills right now: a box's footprint is the hull of its eight corners, a
    /// capsule's a circle. `minY`/`maxY` are its extent in height.
    NavCarveVolume LiveVolume() const {
        NavCarveVolume v;
        const Vec3 c = WorldCenter();
        const Vec3 s = transform ? transform->LossyScale() : Vec3::One;
        if (shape == Shape::Capsule) {
            const float r = std::fabs(capsuleRadius) * std::max(std::fabs(s.x), std::fabs(s.z));
            const float h = std::max(std::fabs(capsuleHeight) * std::fabs(s.y), 2.0f * r);
            v.SetCircle(c.x, c.z, r);
            v.minY = c.y - h * 0.5f; v.maxY = c.y + h * 0.5f;
            return v;
        }
        float xs[8], zs[8];
        float lo = 1e30f, hi = -1e30f;
        int n = 0;
        const Quat q = transform ? transform->Rotation() : Quat::Identity;
        for (int i = 0; i < 8; ++i) {
            const Vec3 corner{(i & 1 ? 0.5f : -0.5f) * size.x * s.x, (i & 2 ? 0.5f : -0.5f) * size.y * s.y,
                              (i & 4 ? 0.5f : -0.5f) * size.z * s.z};
            const Vec3 w = c + q * corner;
            xs[n] = w.x; zs[n] = w.z; ++n;
            lo = std::min(lo, w.y); hi = std::max(hi, w.y);
        }
        v.SetHull(xs, zs, n);
        v.minY = lo; v.maxY = hi;
        return v;
    }
    /// True while this obstacle has a hole in the navigation mesh (carve on, and the carve condition holds).
    bool Carving() const { return carve && m_carving; }
    /// The solid the mesh was carved with (where it stood when the hole was cut). Valid while Carving().
    const NavCarveVolume& CarvedVolume() const { return m_carved; }
    /// Seconds it has been still (stationary-only mode).
    float StationaryTime() const { return m_still; }

    void Start() override {
        m_last = LiveVolume(); m_lastCenter = WorldCenter(); m_haveLast = true;
        m_still = 0.0f; m_carving = false;
    }
    void Update(float dt) override {
        const Vec3 c = WorldCenter();
        if (m_haveLast && dt > 1e-6f) m_velocity = (c - m_lastCenter) * (1.0f / dt);
        m_lastCenter = c;
        const NavCarveVolume cur = LiveVolume();
        const float frameMove = m_haveLast ? cur.Delta(m_last) : 0.0f;
        m_last = cur; m_haveLast = true;
        if (!carve) { m_carving = false; m_still = 0.0f; return; }
        if (carveOnlyWhenStationary) {
            if (frameMove > 1e-3f) { m_still = 0.0f; m_carving = false; return; }
            m_still += dt;
            if (!m_carving && m_still >= timeToStationary) { m_carving = true; m_carved = cur; }
        } else if (!m_carving || cur.Delta(m_carved) > moveThreshold) {
            m_carving = true; m_carved = cur;
        }
    }

private:
    Vec3 m_velocity{0, 0, 0}, m_lastCenter{0, 0, 0};
    NavCarveVolume m_last, m_carved;
    bool m_haveLast = false, m_carving = false;
    float m_still = 0.0f;
};

} // namespace okay
