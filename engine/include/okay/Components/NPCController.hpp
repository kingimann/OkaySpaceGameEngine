#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/SurvivalAfflictions.hpp"   // DamageHealthOn
#include "okay/Net/NetOwnership.hpp"                  // IsLocallyControlled (MP-safe AI)
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>

namespace okay {

/// A steering + perception NPC brain for 3D games — wildlife, enemies, guards,
/// villagers. You pick a BASE behavior; a runtime state machine layers perception
/// (sight cone, line-of-sight, hearing) and combat on top, so a guard can patrol,
/// spot the player, give chase, lose them, search the last-known spot, then walk
/// home and resume its route — all without scripting.
///
/// Base behaviors:
///  - **Idle**   — stand at the spawn point (still a sentry if `aggressive`).
///  - **Wander** — roam randomly around the spawn, pausing between hops.
///  - **Patrol** — walk a list of `waypoints` (loop or ping-pong), waiting at each.
///  - **Guard**  — hold the spawn point and watch; chase intruders if `aggressive`.
///  - **Follow** — approach the target, stopping at `attackRange` (a companion/pet).
///  - **Flee**   — run from the target whenever it's perceived (prey).
///  - **Chase**  — hunt and attack the target (a predator/enemy).
///
/// Moves a sibling Rigidbody3D (or the Transform), turns smoothly toward its heading,
/// optionally drives a sibling Character's animation (idle/walk/run) and head-look,
/// and broadcasts messages (`npc_alert`, `npc_lost`, `npc_search`, `npc_waypoint`,
/// `npc_attack`, `npc_flee`, `npc_died`) ActionLists can react to.
class NPCController : public Behaviour {
public:
    enum class Behavior { Idle, Wander, Follow, Flee, Chase, Patrol, Guard };
    int   behavior = (int)Behavior::Wander;
    std::string targetName = "Player";

    // ---- Movement ----
    float moveSpeed = 2.5f;        ///< walk speed (wander / patrol / follow)
    float runSpeed  = 4.5f;        ///< run speed (chase / flee / search)
    float turnSpeed = 360.0f;      ///< how fast it rotates toward its heading (deg/sec; <=0 = snap)
    float acceleration = 14.0f;    ///< how quickly velocity eases to the target (per sec; <=0 = instant)
    float stopDistance = 0.0f;     ///< extra buffer added to the arrival distance
    bool  faceMovement = true;     ///< turn to face the direction of travel

    // ---- Perception ----
    float sightRange   = 8.0f;     ///< how far it can see the target
    float fieldOfView  = 200.0f;   ///< vision cone width in degrees (360 = sees all around)
    bool  lineOfSight  = false;    ///< require a clear physics raycast (walls block sight)
    float eyeHeight    = 1.2f;     ///< eye height above the origin for the sight ray
    float hearingRange = 0.0f;     ///< hears (and investigates) the target within this radius (0 = deaf)
    float detectionTime = 0.0f;    ///< seconds of continuous sight before it reacts (0 = instant; >0 = stealth meter)
    float loseSightTime = 3.0f;    ///< keep hunting this long after losing sight, then search
    float searchTime    = 4.0f;    ///< how long to investigate the last-known spot before giving up

    // ---- Aggression / leash ----
    bool  aggressive = false;      ///< Idle/Wander/Patrol/Guard NPCs chase a perceived target
    bool  provokable = true;       ///< becomes hostile after taking damage
    bool  returnsHome = true;      ///< walk back to the spawn after losing the target
    float leashRange  = 0.0f;      ///< give up the chase past this distance from home (0 = unlimited)

    // ---- Wander / patrol ----
    float wanderRadius = 6.0f;     ///< how far it roams from its spawn (Wander)
    float wanderPause  = 0.0f;     ///< seconds to pause on reaching a wander point
    std::vector<Vec3> waypoints;   ///< patrol route (world points); needs >= 1 for Patrol
    bool  patrolLoop = true;       ///< true = loop A->B->C->A; false = ping-pong A->B->C->B->A
    float waypointWait = 1.0f;     ///< seconds to wait at each waypoint

    // ---- Combat ----
    float attackRange  = 1.4f;     ///< bite / stop distance
    float attackDamage = 8.0f;     ///< HP per hit (Chase / provoked)
    float attackInterval = 1.0f;   ///< seconds between hits
    float attackWindup = 0.0f;     ///< telegraph delay before a hit lands (0 = instant)
    float fleeHealthPct = 0.0f;    ///< flee when health falls to this fraction of max (0 = never)

    // ---- Group ----
    float separationRadius = 0.0f; ///< steer away from other NPCs within this radius (0 = off)

    // ---- Presentation (sibling Character, if any) ----
    bool  driveAnimation = true;   ///< set the Character's anim to idle/walk/run from movement
    bool  lookAtTarget   = true;   ///< head-track the target while alert (uses Character look-at)

    // ---- Combat target (so the player can fight back) ----
    float maxHealth = 30.0f, health = 30.0f;
    bool  invulnerable = false;

    bool IsDead() const { return m_dead; }
    /// Current runtime AI state, for HUD / debugging / scripts.
    int  StateId() const { return (int)m_state; }
    const char* StateName() const {
        static const char* n[] = {"Idle","Wander","Patrol","Follow","Flee","Chase","Search","Return"};
        int i = (int)m_state; return (i >= 0 && i < 8) ? n[i] : "?";
    }
    bool IsAlerted() const { return m_state == State::Chase || m_state == State::Search; }

    /// Take damage; provokes (if allowed), and on death broadcasts `npc_died`, plays a
    /// sibling AudioSource and removes the object. Call from a weapon or trap.
    void Damage(float amount) {
        if (m_dead || invulnerable || amount <= 0.0f) return;
        health -= amount;
        if (provokable) { m_provoked = true; m_sightLost = 0.0f; if (GameObject* t = Target()) m_lastKnown = TP(t); }
        if (health <= 0.0f) {
            health = 0.0f; m_dead = true;
            Broadcast("npc_died");
            if (gameObject) {
                if (auto* au = gameObject->GetComponent<AudioSource>()) au->Play();
                if (gameObject->scene()) gameObject->scene()->Destroy(gameObject);
                else gameObject->active = false;
            }
        }
    }

    void Start() override {
        health = maxHealth; m_dead = false; m_provoked = false;
        if (transform) m_home = transform->Position();
        m_seed = (uint32_t)(std::fabs(m_home.x) * 73856093.0f + std::fabs(m_home.z) * 19349663.0f) + 1u;
        m_state = State::Idle; m_wp = 0; m_wpDir = 1;
        PickWander();
    }

    void Update(float dt) override {
        if (!transform || dt <= 0.0f || m_dead) return;
        if (!IsLocallyControlled(gameObject)) return;   // MP: only the authority runs AI; others follow via NetworkSync
        Vec3 pos = transform->Position();
        if (m_atkTimer > 0.0f) m_atkTimer -= dt;
        if (m_windup  > 0.0f) m_windup  -= dt;
        if (m_wait    > 0.0f) m_wait    -= dt;

        GameObject* tgt = Target();
        Vec3 tp = tgt ? TP(tgt) : pos;
        float distT = tgt ? Dist2D(pos, tp) : 1e9f;

        // ---- Perception ----------------------------------------------------
        bool canSee = tgt && distT <= sightRange && InFov(pos, tp) && (!lineOfSight || LosClear(pos, tp, distT));
        bool heard  = tgt && hearingRange > 0.0f && distT <= hearingRange;
        if (canSee) { m_detect = std::fmin(m_detect + dt, detectionTime + 0.001f); m_lastKnown = tp; m_sightLost = 0.0f; }
        else        { m_detect = std::fmax(m_detect - dt * 0.6f, 0.0f); m_sightLost += dt; }
        bool acquired = canSee && (detectionTime <= 0.0f || m_detect >= detectionTime);
        bool aware    = acquired || heard || (m_provoked && distT <= sightRange * 1.5f);

        // ---- Decide the AI state ------------------------------------------
        Behavior base = (Behavior)behavior;
        bool lowHealth = fleeHealthPct > 0.0f && health <= maxHealth * fleeHealthPct;
        bool fleer     = base == Behavior::Flee || lowHealth;
        // Flee is sticky: once spooked, keep running even after turning your back on the
        // threat (so the sight cone losing the target doesn't instantly calm the NPC) —
        // until the lose-sight grace lapses.
        bool wantFlee  = fleer && (aware || (m_state == State::Flee && m_sightLost < loseSightTime + searchTime));
        bool hunts     = base == Behavior::Chase || m_provoked ||
                         (aggressive && (base == Behavior::Idle || base == Behavior::Wander ||
                                         base == Behavior::Patrol || base == Behavior::Guard));
        bool leashed   = leashRange > 0.0f && Dist2D(pos, m_home) > leashRange;

        State prev = m_state;
        if (wantFlee) {
            m_state = State::Flee;
        } else if (hunts && !leashed && (aware || (IsAlerted() && m_sightLost < loseSightTime))) {
            m_state = canSee ? State::Chase
                             : (m_sightLost < loseSightTime ? State::Chase : State::Search);
        } else if (hunts && (m_state == State::Chase || m_state == State::Search)) {
            // Lost them (or leashed): investigate the last spot, then give up.
            if (!leashed && m_sightLost < loseSightTime + searchTime) m_state = State::Search;
            else m_state = returnsHome ? State::Return : BaseState(base);
        } else if (base == Behavior::Follow && tgt && distT <= sightRange) {
            m_state = State::Follow;
        } else if (m_state == State::Return) {
            if (Dist2D(pos, m_home) < 1.0f) { m_state = BaseState(base); Broadcast("npc_lost"); }
        } else {
            m_state = BaseState(base);
        }
        if (m_state != prev) OnEnterState(prev);

        // ---- Act on the state ---------------------------------------------
        Vec3 goal = pos; bool move = false; float speed = moveSpeed; float arrive = 0.4f;
        switch (m_state) {
            case State::Chase:
                if (distT > attackRange + stopDistance) { goal = canSee ? tp : m_lastKnown; move = true; speed = runSpeed; }
                else { FaceToward(Dir(pos, tp), dt); Attack(tgt); }
                break;
            case State::Search:
                goal = m_lastKnown; speed = runSpeed; arrive = 0.8f;
                if (Dist2D(pos, goal) > arrive) move = true;
                break;
            case State::Flee: {
                Vec3 away = Dir(tp, pos);
                goal = {pos.x + away.x * 4.0f, pos.y, pos.z + away.z * 4.0f};
                move = (away.x != 0.0f || away.z != 0.0f); speed = runSpeed;
                break;
            }
            case State::Follow:
                if (distT > attackRange + stopDistance) { goal = tp; move = true; }
                else FaceToward(Dir(pos, tp), dt);
                break;
            case State::Patrol: {
                Vec3 wp = CurrentWaypoint();
                if (Dist2D(pos, wp) < 0.6f + stopDistance) {
                    if (m_wait <= 0.0f) { AdvanceWaypoint(); m_wait = waypointWait; Broadcast("npc_waypoint"); }
                } else { goal = wp; move = true; }
                break;
            }
            case State::Wander:
                if (Dist2D(pos, m_wander) < 0.6f) {
                    if (m_wait <= 0.0f) { PickWander(); m_wait = wanderPause; }
                } else { goal = m_wander; move = true; }
                break;
            case State::Return:
                goal = m_home; move = Dist2D(pos, m_home) > 1.0f; break;
            case State::Idle:
            default:
                break;
        }

        // ---- Steering + movement ------------------------------------------
        Vec3 dir = move ? Dir(pos, goal) : Vec3{0, 0, 0};
        if (move && separationRadius > 0.0f) dir = Steer(dir, pos);
        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr;
        float vx = dir.x * speed, vz = dir.z * speed;
        if (!move) { vx = 0.0f; vz = 0.0f; }
        if (rb) {
            if (acceleration > 0.0f) {
                float k = 1.0f - std::exp(-acceleration * dt);
                rb->velocity.x += (vx - rb->velocity.x) * k;
                rb->velocity.z += (vz - rb->velocity.z) * k;
            } else { rb->velocity.x = vx; rb->velocity.z = vz; }
        } else if (move) {
            transform->Translate(Vec3{vx * dt, 0.0f, vz * dt});
        }
        if (faceMovement && move && (dir.x != 0.0f || dir.z != 0.0f)) FaceToward(dir, dt);

        // ---- Presentation (sibling Character) -----------------------------
        DriveCharacter(rb, move, speed, tgt, canSee || acquired);
    }

private:
    enum class State { Idle, Wander, Patrol, Follow, Flee, Chase, Search, Return };

    static State BaseState(Behavior b) {
        switch (b) {
            case Behavior::Wander: return State::Wander;
            case Behavior::Patrol: return State::Patrol;
            case Behavior::Follow: return State::Follow;
            case Behavior::Flee:   return State::Wander;   // resting prey just wanders
            case Behavior::Chase:  return State::Wander;    // resting predator wanders
            case Behavior::Guard:
            case Behavior::Idle:
            default:               return State::Idle;
        }
    }
    void OnEnterState(State /*prev*/) {
        if (m_state == State::Chase)  Broadcast("npc_alert");
        else if (m_state == State::Search) Broadcast("npc_search");
        else if (m_state == State::Flee)   Broadcast("npc_flee");
    }

    GameObject* Target() const {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        return s ? s->Find(targetName) : nullptr;
    }
    Vec3 TP(GameObject* t) const { return t && t->transform ? t->transform->Position() : Vec3{0, 0, 0}; }

    bool InFov(const Vec3& pos, const Vec3& tp) const {
        if (fieldOfView >= 359.0f) return true;
        Vec3 to = Dir(pos, tp);
        if (to.x == 0.0f && to.z == 0.0f) return true;
        Vec3 fwd = transform->localRotation * Vec3{0, 0, 1};   // facing (faceMovement aligns +Z to heading)
        float fm = std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z);
        if (fm < 1e-5f) return true;
        float dot = (to.x * fwd.x + to.z * fwd.z) / fm;
        if (dot > 1.0f) dot = 1.0f;
        if (dot < -1.0f) dot = -1.0f;
        return std::acos(dot) * 57.29578f <= fieldOfView * 0.5f;
    }
    bool LosClear(const Vec3& pos, const Vec3& tp, float distT) const {
        if (!gameObject || !gameObject->scene()) return true;
        Vec3 o = {pos.x, pos.y + eyeHeight, pos.z};
        Vec3 e = {tp.x,  tp.y  + eyeHeight, tp.z};
        Vec3 d = {e.x - o.x, e.y - o.y, e.z - o.z};
        float m = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (m < 1e-4f) return true;
        d = {d.x / m, d.y / m, d.z / m};
        Physics3D phys;
        RaycastHit3D hit = phys.Raycast(*gameObject->scene(), o, d, m - 0.3f, gameObject);
        // A hit before the target means a wall is in the way (the target has no collider
        // in the path, or we'd ignore it — close enough for a sight check).
        return !hit || hit.distance >= distT - 0.3f;
    }

    void FaceToward(const Vec3& dir, float dt) {
        if (dir.x == 0.0f && dir.z == 0.0f) return;
        Quat want = Quat::LookRotation(Vec3{dir.x, 0.0f, dir.z});
        if (turnSpeed <= 0.0f) { transform->localRotation = want; return; }
        // Slerp toward the desired facing at turnSpeed deg/sec.
        float t = (turnSpeed * dt) / 180.0f;
        if (t > 1.0f) t = 1.0f;
        transform->localRotation = Quat::Slerp(transform->localRotation, want, t);
    }

    Vec3 Steer(Vec3 dir, const Vec3& pos) const {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s) return dir;
        Vec3 push{0, 0, 0};
        for (NPCController* o : s->FindObjectsOfType<NPCController>()) {
            if (o == this || !o->transform) continue;
            Vec3 op = o->transform->Position();
            float dx = pos.x - op.x, dz = pos.z - op.z;
            float d = std::sqrt(dx * dx + dz * dz);
            if (d > 1e-3f && d < separationRadius) { float w = (separationRadius - d) / separationRadius; push.x += dx / d * w; push.z += dz / d * w; }
        }
        Vec3 r{dir.x + push.x, 0.0f, dir.z + push.z};
        float m = std::sqrt(r.x * r.x + r.z * r.z);
        return m > 1e-5f ? Vec3{r.x / m, 0.0f, r.z / m} : dir;
    }

    void DriveCharacter(Rigidbody3D* rb, bool move, float speed, GameObject* tgt, bool see) {
        if (!gameObject) return;
        auto* ch = gameObject->GetComponent<Character>();
        if (!ch) return;
        if (driveAnimation) {
            bool moving = move;
            if (rb) { float v = std::sqrt(rb->velocity.x * rb->velocity.x + rb->velocity.z * rb->velocity.z); moving = v > 0.2f; }
            ch->anim = moving ? (speed >= runSpeed - 0.01f ? 3 : 2) : 1;
        }
        if (lookAtTarget) {
            if (tgt && see && IsAlerted()) { ch->lookAtTarget = targetName; }
            else if (ch->lookAtTarget == targetName) { ch->lookAtTarget.clear(); ch->lookYaw = 0.0f; ch->lookPitch = 0.0f; }
        }
    }

    void Attack(GameObject* tgt) {
        if (m_atkTimer > 0.0f || !tgt) return;
        if (attackWindup > 0.0f && m_windup <= 0.0f && !m_winding) { m_windup = attackWindup; m_winding = true; return; }
        if (m_winding && m_windup > 0.0f) return;   // still telegraphing
        DamageHealthOn(tgt, attackDamage);
        m_atkTimer = attackInterval; m_winding = false;
        Broadcast("npc_attack");
    }
    void Broadcast(const std::string& msg) {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (s) for (ActionList* al : s->FindObjectsOfType<ActionList>()) al->ReceiveMessage(msg);
    }

    Vec3 CurrentWaypoint() const {
        if (waypoints.empty()) return m_home;
        int i = m_wp; if (i < 0) i = 0; if (i >= (int)waypoints.size()) i = (int)waypoints.size() - 1;
        return waypoints[i];
    }
    void AdvanceWaypoint() {
        if (waypoints.size() <= 1) { m_wp = 0; return; }
        if (patrolLoop) { m_wp = (m_wp + 1) % (int)waypoints.size(); return; }
        m_wp += m_wpDir;                                  // ping-pong
        if (m_wp >= (int)waypoints.size()) { m_wp = (int)waypoints.size() - 2; m_wpDir = -1; }
        else if (m_wp < 0) { m_wp = 1; m_wpDir = 1; }
    }
    void PickWander() {
        float a = Rand() * 6.2831853f;
        float r = Rand() * wanderRadius;
        m_wander = {m_home.x + std::cos(a) * r, m_home.y, m_home.z + std::sin(a) * r};
    }
    float Rand() {
        m_seed = m_seed * 1664525u + 1013904223u;
        return (float)((m_seed >> 8) & 0xFFFFFF) / (float)0x1000000;
    }
    static float Dist2D(const Vec3& a, const Vec3& b) {
        float dx = a.x - b.x, dz = a.z - b.z; return std::sqrt(dx * dx + dz * dz);
    }
    static Vec3 Dir(const Vec3& from, const Vec3& to) {
        Vec3 d{to.x - from.x, 0.0f, to.z - from.z};
        float m = std::sqrt(d.x * d.x + d.z * d.z);
        return m > 1e-5f ? Vec3{d.x / m, 0.0f, d.z / m} : Vec3{0, 0, 0};
    }

    Vec3 m_home{0, 0, 0}, m_wander{0, 0, 0}, m_lastKnown{0, 0, 0};
    State m_state = State::Idle;
    int  m_wp = 0, m_wpDir = 1;
    float m_atkTimer = 0.0f, m_windup = 0.0f, m_wait = 0.0f;
    float m_detect = 0.0f, m_sightLost = 1e9f;
    bool m_dead = false, m_provoked = false, m_winding = false;
    uint32_t m_seed = 1u;
};

} // namespace okay
