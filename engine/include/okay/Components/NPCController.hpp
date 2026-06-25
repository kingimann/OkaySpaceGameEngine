#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Components/SurvivalAfflictions.hpp"   // DamageHealthOn
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include <string>
#include <cstdint>
#include <cmath>

namespace okay {

/// A simple steering NPC for 3D games — wildlife, enemies, villagers. Picks a
/// behavior toward a named target (default "Player") and moves a sibling Rigidbody3D
/// (or the Transform) accordingly, turning to face its heading:
///  - **Idle**   — stand still.
///  - **Wander** — roam randomly around its spawn point.
///  - **Follow** — approach the target, stopping at `attackRange` (a companion/pet).
///  - **Flee**   — run from the target when it's within `sightRange` (prey).
///  - **Chase**  — hunt the target within `sightRange` and bite for `attackDamage`
///    every `attackInterval` while within `attackRange` (a predator/enemy). Out of
///    sight it falls back to wandering. Broadcasts `npc_attack` on each hit.
class NPCController : public Behaviour {
public:
    enum class Behavior { Idle, Wander, Follow, Flee, Chase };
    int   behavior = (int)Behavior::Wander;
    float moveSpeed = 2.5f;
    std::string targetName = "Player";
    float sightRange   = 8.0f;     // range to start chasing / fleeing
    float wanderRadius = 6.0f;     // how far it roams from its spawn
    float attackRange  = 1.4f;     // bite / stop distance
    float attackDamage = 8.0f;     // HP per bite (Chase)
    float attackInterval = 1.0f;   // seconds between bites
    bool  faceMovement = true;     // turn to face the direction of travel

    // ---- Combat target (so the player can fight back) ----
    float maxHealth = 30.0f, health = 30.0f;
    bool  invulnerable = false;

    bool IsDead() const { return m_dead; }
    /// Take damage; on death broadcasts `npc_died`, plays a sibling AudioSource and
    /// removes the object. Call from a player weapon (MeleeAttacker) or a trap.
    void Damage(float amount) {
        if (m_dead || invulnerable || amount <= 0.0f) return;
        health -= amount;
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
        health = maxHealth; m_dead = false;
        if (transform) m_home = transform->Position();
        m_seed = (uint32_t)(std::fabs(m_home.x) * 73856093.0f + std::fabs(m_home.z) * 19349663.0f) + 1u;
        PickWander();
    }

    void Update(float dt) override {
        if (!transform || dt <= 0.0f || m_dead) return;
        Vec3 pos = transform->Position();
        Behavior b = (Behavior)behavior;
        if (m_atkTimer > 0.0f) m_atkTimer -= dt;

        GameObject* tgt = Target();
        Vec3 tp = tgt && tgt->transform ? tgt->transform->Position() : pos;
        float distT = tgt ? Dist2D(pos, tp) : 1e9f;

        Vec3 dir{0, 0, 0}; bool move = false;
        if (b == Behavior::Chase && tgt && distT <= sightRange) {
            if (distT > attackRange) { dir = Dir(pos, tp); move = true; }
            else Attack(tgt);
        } else if (b == Behavior::Follow && tgt && distT <= sightRange && distT > attackRange) {
            dir = Dir(pos, tp); move = true;
        } else if (b == Behavior::Flee && tgt && distT <= sightRange) {
            dir = Dir(tp, pos); move = true;
        } else if (b != Behavior::Idle) {
            if (Dist2D(pos, m_wander) < 0.6f) PickWander();
            dir = Dir(pos, m_wander); move = true;
        }

        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr;
        if (move) {
            if (rb) { rb->velocity.x = dir.x * moveSpeed; rb->velocity.z = dir.z * moveSpeed; }
            else transform->Translate(Vec3{dir.x * moveSpeed * dt, 0.0f, dir.z * moveSpeed * dt});
            if (faceMovement && (dir.x != 0.0f || dir.z != 0.0f))
                transform->localRotation = Quat::LookRotation(Vec3{dir.x, 0.0f, dir.z});
        } else if (rb) { rb->velocity.x = 0.0f; rb->velocity.z = 0.0f; }
    }

private:
    GameObject* Target() const {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        return s ? s->Find(targetName) : nullptr;
    }
    void Attack(GameObject* tgt) {
        if (m_atkTimer > 0.0f) return;
        DamageHealthOn(tgt, attackDamage);
        m_atkTimer = attackInterval;
        Broadcast("npc_attack");
    }
    void Broadcast(const std::string& msg) {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (s) for (ActionList* al : s->FindObjectsOfType<ActionList>()) al->ReceiveMessage(msg);
    }
    void PickWander() {
        float a = Rand() * 6.2831853f;
        float r = Rand() * wanderRadius;
        m_wander = {m_home.x + std::cos(a) * r, m_home.y, m_home.z + std::sin(a) * r};
    }
    float Rand() {                       // deterministic per-NPC LCG, 0..1
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
    Vec3 m_home{0, 0, 0}, m_wander{0, 0, 0};
    float m_atkTimer = 0.0f;
    bool m_dead = false;
    uint32_t m_seed = 1u;
};

} // namespace okay
