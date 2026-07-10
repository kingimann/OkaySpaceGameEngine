#pragma once
// No-code gameplay mechanics — add-and-go Behaviours that wire the most common
// beginner mechanics (pickups, hazards, teleporters, trigger events) with zero
// scripting. Each hooks the engine's trigger/collision messages and drives the
// existing systems (HealthStat for damage/heal, ActionList variables for score,
// which the prebuilt HUD's `Score: {score}` already binds to). Put one on an
// object that has a Collider (2D or 3D) — a trigger collider for overlap-style
// pickups/zones, a solid collider for touch-style hazards.
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics2D.hpp"        // Collision2D
#include "okay/Physics/Physics3D.hpp"        // Collision3D
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/SurvivalAfflictions.hpp"   // DamageHealthOn / HealOn
#include "okay/Components/ActionList.hpp"            // ActionList::Vars() (score, flags)
#include "okay/Scene/SceneSerializer.hpp"            // Spawner: clone / prefab instantiate
#include "okay/Core/Game.hpp"
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace okay {

/// Shared "who can trigger me" test. An empty filter matches anything with a
/// collider; otherwise the other object must carry the tag, or contain the filter
/// text in its name (so `filter="Player"` matches a "Player" tag or a "Player 1"
/// object without forcing the user to set tags).
inline bool NoCodeMatches(GameObject* other, const std::string& filter) {
    if (!other) return false;
    if (filter.empty()) return true;
    if (other->tag == filter) return true;
    return other->name.find(filter) != std::string::npos;
}

/// Collectible / pickup — a coin, gem, or power-up. When a matching body touches
/// it, add `points` to a score variable (default "score", which the prebuilt HUD
/// shows via `Score: {score}`), optionally heal the collector, then disappear:
/// destroyed outright, or hidden and brought back after `respawnDelay`. Drop a HUD
/// + a handful of these and you have a working coin game with no code.
class Collectible : public Behaviour {
public:
    std::string collectorTag;         // who can collect (tag or name text); empty = anyone
    std::string scoreVar = "score";   // variable to add to (drives the HUD)
    float points       = 1.0f;        // added to scoreVar per pickup
    float heal         = 0.0f;        // also heal the collector this much (needs a HealthStat)
    bool  respawn      = false;       // reappear instead of vanishing for good
    float respawnDelay = 5.0f;

    void OnTriggerEnter2D(Collider2D* o) override { if (o) Collect(o->gameObject); }
    void OnTriggerEnter3D(Collider3D* o) override { if (o) Collect(o->gameObject); }
    void OnCollisionEnter2D(const Collision2D& c) override { Collect(c.gameObject); }
    void OnCollisionEnter3D(const Collision3D& c) override { Collect(c.gameObject); }

    /// Exposed so pickups can be driven/tested from code.
    void Collect(GameObject* who) {
        if (m_taken || !who || who == gameObject) return;
        if (!NoCodeMatches(who, collectorTag)) return;
        ActionList::Vars()[scoreVar] += points;
        if (heal > 0.0f) HealOn(who, heal);
        if (respawn) { m_taken = true; m_timer = respawnDelay; SetVisible(false); }
        else if (gameObject && gameObject->scene()) gameObject->scene()->Destroy(gameObject);
    }

    void Update(float dt) override {
        if (!m_taken) return;                      // only ticks while hidden (respawn mode)
        m_timer -= dt;
        if (m_timer <= 0.0f) { m_taken = false; SetVisible(true); }
    }

private:
    // Hide/show by toggling the sibling renderer + collider; the object stays active
    // so this component keeps ticking the respawn timer (an inactive object gets no Update).
    void SetVisible(bool on) {
        if (!gameObject) return;
        if (auto* r = gameObject->GetComponent<SpriteRenderer>()) r->enabled = on;
        if (auto* r = gameObject->GetComponent<MeshRenderer>())   r->enabled = on;
        if (auto* c = gameObject->GetComponent<Collider2D>())     c->enabled = on;
        if (auto* c = gameObject->GetComponent<Collider3D>())     c->enabled = on;
    }
    bool  m_taken = false;
    float m_timer = 0.0f;
};

/// Damage-on-touch — spikes, lava, a saw blade, an enemy, or a projectile. Hurts
/// whatever touches it (via its HealthStat/SurvivalStats), re-applying every
/// `interval` seconds so standing in it keeps hurting; set `once` for a single hit.
/// Optional knockback pushes a Rigidbody toucher away, and `destroySelf` turns it
/// into a one-shot projectile that pops on impact.
class DamageOnTouch : public Behaviour {
public:
    std::string targetTag;        // who takes damage (tag or name text); empty = anyone
    float damage      = 10.0f;
    float interval    = 0.5f;     // seconds between repeat hits while overlapping (0 = every frame)
    bool  once        = false;    // hit only on first contact, then never again
    float knockback   = 0.0f;     // impulse away from this object (Rigidbody toucher)
    bool  destroySelf = false;    // remove this object after it lands a hit (projectile)

    void OnTriggerEnter2D(Collider2D* o) override { if (o) Hit(o->gameObject); }
    void OnTriggerStay2D(Collider2D* o)  override { if (o) Hit(o->gameObject); }
    void OnTriggerEnter3D(Collider3D* o) override { if (o) Hit(o->gameObject); }
    void OnTriggerStay3D(Collider3D* o)  override { if (o) Hit(o->gameObject); }
    void OnCollisionEnter3D(const Collision3D& c) override { Hit(c.gameObject); }
    void OnCollisionStay3D(const Collision3D& c)  override { Hit(c.gameObject); }
    void OnCollisionEnter2D(const Collision2D& c) override { Hit(c.gameObject); }
    void OnCollisionStay2D(const Collision2D& c)  override { Hit(c.gameObject); }

    void Update(float dt) override { if (m_cooldown > 0.0f) m_cooldown -= dt; }

    void Hit(GameObject* who) {
        if (!who || who == gameObject) return;
        if (!NoCodeMatches(who, targetTag)) return;
        if (once && m_hitOnce) return;
        if (m_cooldown > 0.0f) return;
        DamageHealthOn(who, damage);
        if (knockback > 0.0f) ApplyKnockback(who);
        m_hitOnce = true;
        m_cooldown = interval;
        if (destroySelf && gameObject && gameObject->scene()) gameObject->scene()->Destroy(gameObject);
    }

private:
    void ApplyKnockback(GameObject* who) {
        if (!who || !who->transform || !gameObject || !gameObject->transform) return;
        Vec3 dir = who->transform->Position() - gameObject->transform->Position();
        if (auto* rb = who->GetComponent<Rigidbody3D>()) {
            Vec3 d = dir; if (d.SqrMagnitude() > 1e-6f) d = d.Normalized(); else d = Vec3{0, 1, 0};
            rb->velocity += d * knockback;
        }
        if (auto* rb = who->GetComponent<Rigidbody2D>()) {
            Vec2 d{dir.x, dir.y}; float m = d.Magnitude();
            d = m > 1e-6f ? Vec2{d.x / m, d.y / m} : Vec2{0, 1};
            rb->velocity += d * knockback;
        }
    }
    bool  m_hitOnce  = false;
    float m_cooldown = 0.0f;
};

/// Teleporter — a door or portal. When a matching body enters, move it to a
/// destination: a named target object's position if `targetName` is set, else the
/// fixed `destination`. A short `cooldown` stops the pair of linked pads from
/// bouncing the player back and forth forever.
class Teleporter : public Behaviour {
public:
    std::string triggerTag;        // who teleports (tag or name text); empty = anyone
    std::string targetName;        // teleport to this object's position (empty = use destination)
    Vec3  destination{0, 0, 0};    // fixed target when targetName is empty
    float cooldown = 0.5f;         // seconds a just-teleported body is ignored

    void OnTriggerEnter2D(Collider2D* o) override { if (o) Warp(o->gameObject); }
    void OnTriggerEnter3D(Collider3D* o) override { if (o) Warp(o->gameObject); }

    void Update(float dt) override { if (m_cooldown > 0.0f) m_cooldown -= dt; }

    void Warp(GameObject* who) {
        if (!who || who == gameObject || m_cooldown > 0.0f) return;
        if (!NoCodeMatches(who, triggerTag) || !who->transform) return;
        Vec3 dest = destination;
        if (!targetName.empty() && gameObject && gameObject->scene())
            if (GameObject* t = gameObject->scene()->Find(targetName); t && t->transform)
                dest = t->transform->Position();
        who->transform->SetPosition(dest);
        m_cooldown = cooldown;
    }

private:
    float m_cooldown = 0.0f;
};

/// Jump pad / launcher — put it on a trigger collider; anything (or a tagged
/// body) that enters is flung along the pad's launch direction. The classic
/// platformer bounce pad, booster ring or trampoline, with zero scripting.
class JumpPad : public Behaviour {
public:
    std::string triggerTag;        ///< who launches (tag or name text); empty = anyone
    float force = 12.0f;           ///< launch speed (world units/s)
    bool  useObjectUp = true;      ///< launch along this object's local +Y (tilt the pad to aim); off = straight up
    float forwardBoost = 0.0f;     ///< extra speed added along the body's CURRENT travel direction
    float cooldown = 0.2f;         ///< seconds a just-launched body is ignored (no double-fire)

    void OnTriggerEnter3D(Collider3D* o) override { if (o) Launch(o->gameObject); }
    void OnCollisionEnter3D(const Collision3D& c) override { Launch(c.gameObject); }

    void Update(float dt) override { if (m_cool > 0.0f) m_cool -= dt; }

    void Launch(GameObject* who) {
        if (!who || who == gameObject || m_cool > 0.0f) return;
        if (!NoCodeMatches(who, triggerTag)) return;
        auto* rb = who->GetComponent<Rigidbody3D>();
        if (!rb) return;
        Vec3 dir = (useObjectUp && transform) ? transform->Rotation() * Vec3{0, 1, 0} : Vec3{0, 1, 0};
        float dl = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        if (dl > 1e-4f) { dir.x /= dl; dir.y /= dl; dir.z /= dl; }
        rb->velocity = dir * force;
        if (forwardBoost != 0.0f) {
            Vec3 v{rb->velocity.x, 0.0f, rb->velocity.z};
            float hl = std::sqrt(v.x * v.x + v.z * v.z);
            if (hl > 1e-3f) { rb->velocity.x += v.x / hl * forwardBoost; rb->velocity.z += v.z / hl * forwardBoost; }
        }
        rb->WakeUp();
        m_cool = cooldown;
    }

private:
    float m_cool = 0.0f;
};

/// Spawner — waves of enemies, pickups or props with zero scripting. Point it at
/// a template object in the scene (built and tuned in place, hidden at play
/// start) or a .okayprefab file, and it spawns copies around itself: `count` per
/// wave, one every `interval` seconds, pausing `waveDelay` between waves, with a
/// live cap (`maxAlive`) so endless spawners can't flood the scene. Broadcasts
/// `spawner_spawn` / `spawner_wave` / `spawner_done` for ActionLists, and
/// scripts can drive it with `spawner_start(name)` / `spawner_stop(name)`.
class Spawner : public Behaviour {
public:
    std::string templateName;      ///< scene object to clone (takes priority over the prefab)
    std::string prefabPath;        ///< .okayprefab to instantiate when no template is set
    int   count     = 3;           ///< objects per wave
    float interval  = 1.0f;        ///< seconds between spawns within a wave
    int   waves     = 0;           ///< how many waves to run (0 = endless)
    float waveDelay = 4.0f;        ///< pause between waves
    float radius    = 3.0f;        ///< spawn scatter radius around this object (flat XZ disc)
    int   maxAlive  = 8;           ///< cap on live spawned objects (0 = unlimited)
    bool  autoStart = true;        ///< begin as soon as the game starts
    bool  hideTemplate = true;     ///< deactivate the template object itself in play

    void StartWaves() { m_running = true; }
    void StopWaves()  { m_running = false; }
    bool Running()   const { return m_running; }
    int  WavesDone() const { return m_wavesDone; }
    /// Live objects this spawner created (destroyed ones are pruned).
    int AliveCount() {
        Prune();
        return (int)m_spawned.size();
    }

    void Start() override {
        m_running = autoStart;
        m_left = count; m_timer = 0.0f; m_wavesDone = 0;
        m_spawned.clear();
        if (transform) {
            Vec3 p = transform->Position();
            m_seed = (uint32_t)(std::fabs(p.x) * 73856093.0f + std::fabs(p.z) * 19349663.0f) + 7u;
        }
        if (hideTemplate && !templateName.empty())
            if (GameObject* t = Template())
                if (t != gameObject) t->active = false;
    }

    void Update(float dt) override {
        if (!m_running || !gameObject || !gameObject->scene()) return;
        if (waves > 0 && m_wavesDone >= waves) return;
        m_timer -= dt;
        if (m_timer > 0.0f) return;
        if (maxAlive > 0 && AliveCount() >= maxAlive) { m_timer = 0.25f; return; }   // wait for room
        if (!SpawnOne()) { m_timer = 1.0f; return; }   // missing template/prefab: retry slowly
        if (--m_left <= 0) {
            ++m_wavesDone;
            Broadcast("spawner_wave");
            if (waves > 0 && m_wavesDone >= waves) {
                m_running = false;
                Broadcast("spawner_done");
                return;
            }
            m_left = count; m_timer = waveDelay;
        } else {
            m_timer = interval;
        }
    }

private:
    GameObject* Template() const {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        return (s && !templateName.empty()) ? s->Find(templateName) : nullptr;
    }

    bool SpawnOne() {
        Scene& sc = *gameObject->scene();
        GameObject* go = nullptr;
        if (!templateName.empty()) {
            if (GameObject* t = Template())
                if (t != gameObject) go = SceneSerializer::Instantiate(sc, *t);
        } else if (!prefabPath.empty()) {
            go = SceneSerializer::InstantiateFromFile(sc, prefabPath, nullptr);
        }
        if (!go) return false;
        go->active = true;                       // the template may be hidden
        if (go->transform && transform) {
            float a = Rand() * 6.2831853f;
            float r = std::sqrt(Rand()) * std::fmax(radius, 0.0f);    // even disc scatter
            Vec3 p = transform->Position();
            go->transform->SetPosition({p.x + std::cos(a) * r, p.y, p.z + std::sin(a) * r});
        }
        m_spawned.push_back({go, 2});   // grace: adoption into the scene is deferred a frame
        Broadcast("spawner_spawn");
        return true;
    }

    // Drop pointers to objects no longer in the scene. Membership is checked by
    // pointer identity against the scene's object list — spawned objects are never
    // dereferenced after they die. Fresh spawns get a 2-tick grace period because
    // new objects are adopted into the scene list a frame after Instantiate.
    void Prune() {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s) { m_spawned.clear(); return; }
        std::vector<std::pair<GameObject*, int>> live;
        live.reserve(m_spawned.size());
        for (auto& e : m_spawned) {
            bool found = false;
            for (const auto& o : s->Objects())
                if (o.get() == e.first) { found = true; break; }
            if (found) live.push_back({e.first, 0});
            else if (e.second > 0) live.push_back({e.first, e.second - 1});
        }
        m_spawned.swap(live);
    }

    void Broadcast(const std::string& msg) {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (s) for (ActionList* al : s->FindObjectsOfType<ActionList>()) al->ReceiveMessage(msg);
    }

    float Rand() {
        m_seed = m_seed * 1664525u + 1013904223u;
        return (float)((m_seed >> 8) & 0xFFFFFF) / (float)0x1000000;
    }

    // What we created: pointer identity + adoption-grace ticks (see Prune).
    std::vector<std::pair<GameObject*, int>> m_spawned;
    bool  m_running = false;
    int   m_left = 0, m_wavesDone = 0;
    float m_timer = 0.0f;
    uint32_t m_seed = 7u;
};

/// Trigger zone — the generic "when something enters here, do one thing" glue that
/// replaces a lot of one-off scripting. Put it on a trigger collider; on entry by a
/// matching body it runs its Action once (or every time). Win/Lose set the `won`/
/// `lost` variables (readable by visual scripts and UI binds) and pause the game.
class TriggerZone : public Behaviour {
public:
    enum class Action { SetVar, AddVar, ActivateTarget, DeactivateTarget, Win, Lose, Quit };
    int   action = (int)Action::SetVar;
    std::string triggerTag;        // who fires it (tag or name text); empty = anyone
    std::string varName = "flag";  // variable for SetVar/AddVar
    float amount = 1.0f;           // value to set / add
    std::string targetName;        // object to activate/deactivate
    bool  once = true;             // fire a single time (default) or on every entry

    void OnTriggerEnter2D(Collider2D* o) override { if (o) Fire(o->gameObject); }
    void OnTriggerEnter3D(Collider3D* o) override { if (o) Fire(o->gameObject); }
    void OnCollisionEnter2D(const Collision2D& c) override { Fire(c.gameObject); }
    void OnCollisionEnter3D(const Collision3D& c) override { Fire(c.gameObject); }

    void Fire(GameObject* who) {
        if (!who || who == gameObject) return;
        if (once && m_fired) return;
        if (!NoCodeMatches(who, triggerTag)) return;
        m_fired = true;
        switch ((Action)action) {
        case Action::SetVar: ActionList::Vars()[varName] = amount; break;
        case Action::AddVar: ActionList::Vars()[varName] += amount; break;
        case Action::ActivateTarget:   SetTarget(true);  break;
        case Action::DeactivateTarget: SetTarget(false); break;
        case Action::Win:  ActionList::Vars()["won"]  = 1.0f; Game::SetPaused(true); break;
        case Action::Lose: ActionList::Vars()["lost"] = 1.0f; Game::SetPaused(true); break;
        case Action::Quit: Game::RequestQuit(); break;
        }
    }

private:
    void SetTarget(bool on) {
        if (targetName.empty() || !gameObject || !gameObject->scene()) return;
        if (GameObject* t = gameObject->scene()->Find(targetName)) t->active = on;
    }
    bool m_fired = false;
};

} // namespace okay
