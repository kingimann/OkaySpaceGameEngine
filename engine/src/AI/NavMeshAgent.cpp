#include "okay/Components/NavMeshAgent.hpp"

#include "okay/AI/NavMesh.hpp"
#include "okay/AI/Navigation.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ClickToMoveController.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/NavMeshObstacle.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"

#include <algorithm>
#include <cmath>

namespace okay {

namespace {
inline float Dist2D(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}
inline float Len2D(const Vec3& v) { return std::sqrt(v.x * v.x + v.z * v.z); }
inline Vec3 Flat(const Vec3& v) { return Vec3{v.x, 0.0f, v.z}; }
// Seconds until two discs touch, given the other's position / velocity relative to
// us; a large value when they never do, 0 when they already overlap.
float TimeToCollision(const Vec3& pRel, const Vec3& vRel, float R) {
    const float c = pRel.x * pRel.x + pRel.z * pRel.z - R * R;
    if (c < 0.0f) return 0.0f;
    const float a = vRel.x * vRel.x + vRel.z * vRel.z;
    if (a < 1e-6f) return 1e9f;
    const float b = -2.0f * (pRel.x * vRel.x + pRel.z * vRel.z);
    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f) return 1e9f;
    const float t = (-b - std::sqrt(disc)) / (2.0f * a);
    return t < 0.0f ? 1e9f : t;
}
const std::vector<Vec3>& EmptyPath() { static const std::vector<Vec3> e; return e; }
} // namespace

NPCController* NavMeshAgent::Npc() const { return gameObject ? gameObject->GetComponent<NPCController>() : nullptr; }
ClickToMoveController* NavMeshAgent::Ctm() const { return gameObject ? gameObject->GetComponent<ClickToMoveController>() : nullptr; }

void NavMeshAgent::SetDestination(const Vec3& worldPoint) {
    m_dest = worldPoint;
    m_hasDest = true; m_stopped = false; m_arrived = false;
    m_path.clear(); m_corner = 0; m_repath = 0.0f; m_stuckT = 0.0f; m_stuckCount = 0;
    if (NPCController* n = Npc()) { n->CommandGoTo(worldPoint); m_drivenHadDest = true; }
    else if (ClickToMoveController* c = Ctm()) { c->MoveTo(worldPoint); m_drivenHadDest = true; }
}

void NavMeshAgent::Stop() {
    m_stopped = true;
    if (NPCController* n = Npc()) n->CancelCommand();
    else if (ClickToMoveController* c = Ctm()) c->ClearDestination();
}

void NavMeshAgent::Resume() {
    if (!m_stopped) return;
    m_stopped = false;
    if (m_hasDest) SetDestination(m_dest);
}

void NavMeshAgent::Warp(const Vec3& worldPoint) {
    if (transform) transform->SetPosition(worldPoint);
    m_path.clear(); m_corner = 0; m_repath = 0.0f; m_velocity = Vec3{0, 0, 0};
    m_lastPos = worldPoint;
}

const std::vector<Vec3>& NavMeshAgent::Path() const {
    if (NPCController* n = Npc()) return n->CurrentPath();
    if (ClickToMoveController* c = Ctm()) return c->CurrentPath();
    return m_hasDest ? m_path : EmptyPath();
}

int NavMeshAgent::CurrentCorner() const {
    if (NPCController* n = Npc()) return n->CurrentPathIndex();
    if (ClickToMoveController* c = Ctm()) return c->CurrentPathIndex();
    return m_corner;
}

bool NavMeshAgent::CurrentCornerPoint(Vec3& out) const {
    const std::vector<Vec3>& p = Path();
    const int i = CurrentCorner();
    if (p.empty()) { if (m_hasDest) { out = m_dest; return true; } return false; }
    out = p[(std::size_t)std::clamp(i, 0, (int)p.size() - 1)];
    return true;
}

void NavMeshAgent::Start() {
    if (transform) m_lastPos = transform->Position();
}

bool NavMeshAgent::Replan(Scene& scene, const Vec3& pos) {
    m_repath = repathInterval;
    m_corner = 0;
    if (!Navigation::FindPath(scene, pos, m_dest, m_path, radius, gameObject)) { m_path.clear(); return false; }
    return true;
}

Vec3 NavMeshAgent::Avoid(Scene& scene, const Vec3& pos, const Vec3& desired) const {
    if (avoidanceRange <= 0.0f) return desired;
    struct Neighbour { Vec3 p, v; float r, share; };
    Neighbour nb[16]; int count = 0;
    const float range = avoidanceRange + radius;
    for (NavMeshAgent* a : Navigation::Agents(scene)) {
        if (a == this || !a || !a->transform || !a->gameObject || !a->gameObject->active || !a->enabled) continue;
        const Vec3 ap = a->transform->Position();
        if (Dist2D(pos, ap) > range + a->radius) continue;
        const float share = a->avoidancePriority == avoidancePriority ? 0.5f
                          : (avoidancePriority > a->avoidancePriority ? 0.8f : 0.2f);
        nb[count++] = {ap, Flat(a->Velocity()), a->radius, share};
        if (count == 16) break;
    }
    for (NavMeshObstacle* o : Navigation::Obstacles(scene)) {
        if (count == 16) break;
        if (!o || !o->transform || !o->gameObject || !o->gameObject->active || !o->enabled) continue;
        const Vec3 op = o->WorldCenter();
        const float r = o->Radius();
        if (Dist2D(pos, op) > range + r) continue;
        nb[count++] = {op, Flat(o->Velocity()), r, 1.0f};
    }
    if (count == 0) return desired;
    // Already overlapping something: push straight out of it.
    Vec3 push{0, 0, 0};
    for (int i = 0; i < count; ++i) {
        const float d = Dist2D(pos, nb[i].p), R = radius + nb[i].r;
        if (d < R && d > 1e-4f) push += Flat(pos - nb[i].p) * ((R - d) / d) * 4.0f;
    }
    // Try the wanted velocity and a fan of alternatives; keep the one that stays
    // closest to it while not running into anyone soon. Each neighbour is assumed
    // to take its share of the dodge (reciprocal), so two agents meeting head-on
    // each step aside a little instead of both swerving hard.
    static const float kAngles[] = {0.0f, 20.0f, -20.0f, 40.0f, -40.0f, 60.0f, -60.0f, 90.0f, -90.0f, 120.0f, -120.0f};
    static const float kScales[] = {1.0f, 0.5f};
    const float want = Len2D(desired);
    Vec3 best = desired; float bestPenalty = 1e30f;
    auto evaluate = [&](const Vec3& c) {
        float penalty = Dist2D(c, desired);
        for (int i = 0; i < count; ++i) {
            const Vec3 eff = Flat(m_velocity) + (c - Flat(m_velocity)) * (1.0f / nb[i].share);
            const float t = TimeToCollision(Flat(nb[i].p - pos), eff - nb[i].v, radius + nb[i].r);
            if (t < 3.0f) penalty += 2.0f / std::max(t, 0.05f);
        }
        if (penalty < bestPenalty) { bestPenalty = penalty; best = c; }
    };
    if (want < 1e-4f) evaluate(desired);
    else {
        const float baseA = std::atan2(desired.z, desired.x);
        for (float s : kScales)
            for (float a : kAngles) {
                const float ang = baseA + a * 3.14159265f / 180.0f;
                evaluate(Vec3{std::cos(ang) * want * s, 0.0f, std::sin(ang) * want * s});
            }
        evaluate(Vec3{0, 0, 0});
    }
    return best + push;
}

void NavMeshAgent::Move(Scene& scene, const Vec3& pos, const Vec3& vel, float dt) {
    Rigidbody3D* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr;
    if (rb && rb->bodyType != Rigidbody3D::BodyType::Static) {
        rb->velocity.x = vel.x; rb->velocity.z = vel.z;
        if (vel.x != 0.0f || vel.z != 0.0f) rb->WakeUp();
        return;
    }
    if (!transform) return;
    Vec3 np = pos + Vec3{vel.x * dt, 0.0f, vel.z * dt};
    if (const NavMesh* mesh = Navigation::Mesh(scene)) {
        const Vec3 feet{np.x, np.y - baseOffset, np.z};
        np.y = mesh->SampleHeight(feet, feet.y, 1.0f) + baseOffset;
    }
    transform->SetPosition(np);
}

void NavMeshAgent::Face(const Vec3& vel, float dt) {
    if (!faceMovement || !transform || vel.x * vel.x + vel.z * vel.z < 0.01f) return;
    const Quat want = Quat::LookRotation(Vec3{vel.x, 0.0f, vel.z});
    if (angularSpeed <= 0.0f) { transform->localRotation = want; return; }
    float t = (angularSpeed * dt) / 180.0f;
    if (t > 1.0f) t = 1.0f;
    transform->localRotation = Quat::Slerp(transform->localRotation, want, t);
}

void NavMeshAgent::Animate(float dt) {
    if (!driveAnimation || !gameObject || !transform) return;
    Character* ch = gameObject->GetComponent<Character>();
    if (!ch)
        for (Transform* c : transform->Children())
            if (c && c->gameObject) if ((ch = c->gameObject->GetComponent<Character>())) break;
    if (!ch) return;
    Character::Locomotion lo;
    lo.velocity = m_velocity;
    lo.facing = transform->localRotation * Vec3{0, 0, -1};
    lo.onGround = true;
    lo.walkSpeed = speed; lo.runSpeed = speed * 2.0f;
    lo.dt = dt;
    ch->DriveLocomotion(lo);
}

void NavMeshAgent::Broadcast(Scene& scene, const char* msg) {
    for (ActionList* al : scene.FindObjectsOfType<ActionList>()) al->ReceiveMessage(msg);
}

void NavMeshAgent::Update(float dt) {
    if (!transform || !gameObject || !gameObject->scene() || dt <= 0.0f) return;
    if (!IsLocallyControlled(gameObject)) return;   // MP: the authority moves it; others follow via NetworkSync
    Scene& scene = *gameObject->scene();
    const Vec3 pos = transform->Position();

    // Driven by an NPC / click-to-move controller: it moves the body; mirror its state.
    if (NPCController* n = Npc()) {
        const bool busy = n->Commanded();
        if (m_drivenHadDest && !busy && m_hasDest && !m_stopped) { m_arrived = true; m_hasDest = false; }
        m_drivenHadDest = busy;
        m_remaining = m_hasDest ? Dist2D(pos, m_dest) : 0.0f;
        m_velocity = dt > 0.0f ? (pos - m_lastPos) * (1.0f / dt) : Vec3{0, 0, 0};
        m_lastPos = pos;
        return;
    }
    if (ClickToMoveController* c = Ctm()) {
        const bool busy = c->HasDestination();
        if (m_drivenHadDest && !busy && m_hasDest && !m_stopped) { m_arrived = true; m_hasDest = false; }
        m_drivenHadDest = busy;
        m_remaining = m_hasDest ? Dist2D(pos, m_dest) : 0.0f;
        m_velocity = dt > 0.0f ? (pos - m_lastPos) * (1.0f / dt) : Vec3{0, 0, 0};
        m_lastPos = pos;
        return;
    }

    const float ease = acceleration > 0.0f ? 1.0f - std::exp(-acceleration * dt) : 1.0f;
    if (!m_hasDest || m_stopped) {
        m_velocity += (Vec3{0, 0, 0} - m_velocity) * ease;
        if (Len2D(m_velocity) < 0.02f) m_velocity = Vec3{0, 0, 0};
        Move(scene, pos, m_velocity, dt);
        Animate(dt);
        m_lastPos = pos;
        return;
    }

    // Plan, and replan on the timer or when wedged on a corner / another agent.
    m_repath -= dt;
    const bool wedged = Dist2D(pos, m_lastPos) < 0.01f && Len2D(m_velocity) > 0.05f;
    m_stuckT = wedged ? m_stuckT + dt : 0.0f;
    m_lastPos = pos;
    bool needPlan = m_path.empty() || (autoRepath && m_repath <= 0.0f);
    if (m_stuckT > 0.8f) {
        m_stuckT = 0.0f; needPlan = true;
        if (++m_stuckCount > 4) { m_hasDest = false; m_path.clear(); m_velocity = Vec3{0, 0, 0}; Move(scene, pos, m_velocity, dt); return; }
    }
    if (needPlan && !Replan(scene, pos)) {   // off the mesh / an island: nowhere to go
        m_hasDest = false; m_velocity = Vec3{0, 0, 0};
        Move(scene, pos, m_velocity, dt);
        Animate(dt);
        return;
    }
    const float reach = std::max(0.3f, radius * 0.75f);
    while (m_corner + 1 < (int)m_path.size() && Dist2D(pos, m_path[(std::size_t)m_corner]) < reach) ++m_corner;
    const Vec3 target = m_path.empty() ? m_dest : m_path[(std::size_t)m_corner];
    m_remaining = Dist2D(pos, target);
    for (std::size_t i = (std::size_t)m_corner; i + 1 < m_path.size(); ++i) m_remaining += Dist2D(m_path[i], m_path[i + 1]);

    if (m_remaining <= stoppingDistance) {   // there
        m_hasDest = false; m_arrived = true; m_path.clear(); m_corner = 0;
        m_velocity = Vec3{0, 0, 0};
        Move(scene, pos, m_velocity, dt);
        Animate(dt);
        Broadcast(scene, "nav_arrived");
        return;
    }
    Vec3 dir = Flat(target - pos);
    const float dl = Len2D(dir);
    dir = dl > 1e-5f ? dir * (1.0f / dl) : Vec3{0, 0, 0};
    float want = speed;
    if (autoBraking) want *= std::clamp((m_remaining - stoppingDistance) / 1.0f, 0.2f, 1.0f);
    if (want * dt > m_remaining) want = m_remaining / dt;   // never step past the destination
    Vec3 desired = dir * want;
    desired = Avoid(scene, pos, desired);
    m_velocity += (desired - m_velocity) * ease;
    m_velocity.y = 0.0f;
    Move(scene, pos, m_velocity, dt);
    Face(m_velocity, dt);
    Animate(dt);
}

} // namespace okay
