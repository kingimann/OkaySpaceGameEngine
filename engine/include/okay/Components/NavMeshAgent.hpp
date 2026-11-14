#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/AI/NavMesh.hpp"
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace okay {

class Scene;
class NPCController;
class ClickToMoveController;
class NavMeshLink;

/// Moves its object along the scene's navigation (the baked NavMesh, or the grid
/// when there is none): SetDestination() plans a path, the agent follows the
/// corners at `speed`, steers around other agents and NavMeshObstacles
/// (reciprocal velocity sampling), replans when stuck or on a timer, and stops at
/// `stoppingDistance`. Moves a sibling Rigidbody3D by velocity, else the Transform
/// (projected to the mesh height). On an object with an NPCController or
/// ClickToMoveController it hands the destination to that controller instead and
/// only reports on it (it does not use off-mesh links). Broadcasts `nav_arrived` to Action Lists on arrival.
///
/// Areas and links: the path search honours the surface's area costs and this agent's
/// `areaMask` (a forbidden area is never entered), and goes through the scene's enabled
/// NavMeshLinks whose area the mask allows. On a link the agent walks to the start point,
/// then crosses to the end point over the link's duration (`IsTraversingLink`), and
/// carries on. `onLinkStart` / `onLinkEnd` fire at the two ends of the crossing (hook an
/// animation on them); Action Lists get `nav_link_start` / `nav_link_end`.
///
/// Carving: a NavMeshObstacle with Carve on cuts a hole in the mesh; the agent replans at once when the
/// hole appears, moves or goes (and gives up, like for any unreachable goal, if no route is left).
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
    /// Areas this agent may enter (bit n = NavArea n). Default: all but NotWalkable.
    std::uint32_t areaMask = kNavDefaultAreaMask;

    /// Called as a link crossing begins / ends (the agent is at the start / end point).
    std::function<void(NavMeshAgent&, NavMeshLink&)> onLinkStart, onLinkEnd;
    /// A link crossing is under way.
    bool IsTraversingLink() const { return m_link != nullptr; }
    NavMeshLink* CurrentLink() const { return m_link; }
    /// How far across the current link the agent is, 0..1 (0 when not on one).
    float LinkProgress() const { return m_link && m_linkDur > 0.0f ? m_linkT / m_linkDur : 0.0f; }

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
    NavMeshLink* LinkInto(int corner) const { return corner >= 0 && corner < (int)m_pathLinks.size() ? m_pathLinks[(std::size_t)corner] : nullptr; }
    void BeginLink(Scene& scene, const Vec3& pos, NavMeshLink* link, const Vec3& landing);
    void UpdateLink(Scene& scene, float dt);
    Vec3 Avoid(Scene& scene, const Vec3& pos, const Vec3& desired) const;
    void Move(Scene& scene, const Vec3& pos, const Vec3& vel, float dt);
    void Face(const Vec3& vel, float dt);
    void Animate(float dt);
    void Broadcast(Scene& scene, const char* msg);
    NPCController* Npc() const;
    ClickToMoveController* Ctm() const;

    std::vector<Vec3> m_path;
    std::vector<NavMeshLink*> m_pathLinks;   // parallel to m_path: the link crossed by the leg that ends at that corner
    NavMeshLink* m_link = nullptr;           // the crossing in progress
    Vec3  m_linkFrom{0, 0, 0}, m_linkTo{0, 0, 0};
    float m_linkT = 0.0f, m_linkDur = 0.0f;
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
    std::uint32_t m_carveGen = 0;   // Navigation::CarveGeneration the path was planned under
};

} // namespace okay
