#pragma once
#include "okay/AI/NavMesh.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

/// An off-mesh link: a jump, a drop or a ladder between two points that are not
/// joined by walkable ground. NavMeshAgents plan through it like an extra edge of the
/// mesh (distance * `cost` * the link area's cost), walk to the start point, cross to
/// the end point over time (see Traverse), then carry on. The points are in this
/// object's local space and must lie on the baked mesh (within about 1.5 m). A disabled
/// component, or an inactive object, is not used; an agent whose area mask lacks the
/// link's `area` ignores it.
class NavMeshLink : public Component {
public:
    enum class Type { Jump = 0, Drop = 1, Ladder = 2 };

    Type  type = Type::Jump;
    Vec3  startPoint{-1.0f, 0.0f, 0.0f};   ///< local space
    Vec3  endPoint{1.0f, 0.0f, 0.0f};      ///< local space
    bool  bidirectional = true;            ///< false: only start -> end
    float cost = 1.0f;                     ///< multiplier on the crossing distance (on top of the area cost)
    int   area = NavArea::Walkable;        ///< NavArea 0..31, checked against the agent's mask
    float speed = 0.0f;                    ///< crossing speed, units per second (0 = the agent's own speed)
    float arcHeight = 1.0f;                ///< Jump: how high the arc peaks above the straight line

    Vec3 WorldStart() const { return ToWorld(startPoint); }
    Vec3 WorldEnd() const { return ToWorld(endPoint); }
    /// Is this link in play (component on, object active)?
    bool Usable() const { return enabled && gameObject && gameObject->active; }

    /// Where an agent is `t` (0..1) of the way across from `a` to `b`: a Jump follows a
    /// parabola, a Drop falls (slow first, then accelerating), a Ladder climbs in a straight line.
    Vec3 PointAt(const Vec3& a, const Vec3& b, float t) const {
        t = std::clamp(t, 0.0f, 1.0f);
        Vec3 p = a + (b - a) * t;
        if (type == Type::Jump) p.y += arcHeight * 4.0f * t * (1.0f - t);
        else if (type == Type::Drop) p.y = a.y + (b.y - a.y) * t * t;
        return p;
    }
    /// Seconds the crossing from `a` to `b` takes for an agent whose own speed is `agentSpeed`.
    float Duration(const Vec3& a, const Vec3& b, float agentSpeed) const {
        const float v = speed > 0.0f ? speed : agentSpeed;
        const float d = (b - a).Magnitude();
        return std::max(d / std::max(v, 0.01f), 0.05f);
    }

private:
    Vec3 ToWorld(const Vec3& local) const {
        if (!transform) return local;
        return transform->Position() + transform->Rotation() * (local * transform->LossyScale());
    }
};

} // namespace okay
