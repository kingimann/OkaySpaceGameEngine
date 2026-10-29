#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Math/Vec3.hpp"
#include <vector>

namespace okay {

class Scene;
class NPCController;
class ClickToMoveController;

/// Moves its object along the scene's navigation (the baked NavMesh, or the grid
/// when there is none): SetDestination() plans a path, the agent follows the
/// corners at `speed`, steers around other agents and NavMeshObstacles
/// (reciprocal velocity sampling), replans when stuck or on a timer, and stops at
/// `stoppingDistance`. Moves a sibling Rigidbody3D by velocity, else the Transform
/// (projected to the mesh height). On an object with an NPCController or
/// ClickToMoveController it hands the destination to that controller instead and
/// only reports on it. Broadcasts `nav_arrived` to Action Lists on arrival.
class NavMeshAgent : public Behaviour {
public:
    /// Before the animation components, so they pose this frame's movement.
    int ExecutionOrder() const override { return -10; }

    float radius = 0.4f;            ///< body radius for local avoidance
    float height = 1.8f;            ///< body height (informational; the bake sets the clearance)
    float speed = 3.5f;             ///< top speed, units per second
    float acceleration = 8.0f;      ///< how quickly the velocity eases to the target (per sec; <= 0 = instant)
    float angularSpeed = 360.0f;    ///< turn rate toward travel, degrees per second (<= 0 = snap)
    float stoppingDistance = 0.2f;  ///< done when this close to the destination
    bool  autoRepath = true;        ///< replan every `repathInterval` while moving
    float repathInterval = 0.5f;
    int   avoidancePriority = 50;   ///< 0 = others get out of its way .. 99 = it yields to everyone
    float avoidanceRange = 3.0f;    ///< how far it looks for agents and obstacles to avoid (0 = off)
    bool  autoBraking = true;       ///< slow down over the last stretch instead of stopping dead
    bool  faceMovement = true;      ///< turn to face the direction of travel
    bool  driveAnimation = true;    ///< drive a sibling Character's idle / walk / run
    float baseOffset = 0.0f;        ///< the object's origin above its feet (a cube's centre: half its height)

    void SetDestination(const Vec3& worldPoint);
    /// Stop in place and keep the destination (Resume() continues).
    void Stop();
    void Resume();
    /// A destination is being pursued.
    bool HasPath() const { return m_hasDest; }
    /// Path length left to the destination (0 when there is none).
    float RemainingDistance() const { return m_hasDest ? m_remaining : 0.0f; }
    bool IsStopped() const { return m_stopped; }
    /// The last SetDestination was reached (cleared by the next one).
    bool Arrived() const { return m_arrived; }
    Vec3 Velocity() const { return m_velocity; }
    Vec3 Destination() const { return m_dest; }
    /// Teleport without planning.
    void Warp(const Vec3& worldPoint);

    /// Debug / editor: the corners being followed and the one steered at now.
    const std::vector<Vec3>& Path() const;
    int CurrentCorner() const;
    bool CurrentCornerPoint(Vec3& out) const;

    void Start() override;
    void Update(float dt) override;

private:
    bool Replan(Scene& scene, const Vec3& pos);
    Vec3 Avoid(Scene& scene, const Vec3& pos, const Vec3& desired) const;
    void Move(Scene& scene, const Vec3& pos, const Vec3& vel, float dt);
    void Face(const Vec3& vel, float dt);
    void Animate(float dt);
    void Broadcast(Scene& scene, const char* msg);
    NPCController* Npc() const;
    ClickToMoveController* Ctm() const;

    std::vector<Vec3> m_path;
    int   m_corner = 0;
    Vec3  m_dest{0, 0, 0};
    bool  m_hasDest = false, m_stopped = false, m_arrived = false;
    Vec3  m_velocity{0, 0, 0};
    float m_remaining = 0.0f;
    float m_repath = 0.0f;
    Vec3  m_lastPos{0, 0, 0};
    float m_stuckT = 0.0f;
    int   m_stuckCount = 0;
    bool  m_drivenHadDest = false;
};

} // namespace okay
