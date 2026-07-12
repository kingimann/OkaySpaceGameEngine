#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Components/ActionList.hpp"   // Broadcast to ActionLists
#include <string>
#include <cstdint>
#include <cmath>

namespace okay {

/// Spawns copies of a template object over time — enemy waves, loot, pickups.
/// Name a `templateName` object in the scene to clone (deactivated at start so
/// it's just a blueprint), or set `prefabPath` to a .okayprefab file. Copies
/// land within `spawnRadius` of the spawner, at most `maxAlive` of its own
/// spawns alive at once, stopping after `totalToSpawn` (0 = endless). Each
/// spawn is tagged so the live count stays accurate as they die.
///
/// Waves: `count` spawns per wave (one every `interval` seconds), a `waveDelay`
/// pause between waves, and `waves` limits the run (0 = endless). The defaults
/// (count 1, no delay) reproduce the classic steady drip. Broadcasts
/// `spawner_spawn` / `spawner_wave` / `spawner_done` for ActionLists; scripts
/// drive it with `spawner_start(name)` / `spawner_stop(name)` /
/// `spawner_alive(name)`, and `autoStart` off arms it for a scripted start.
class Spawner : public Behaviour {
public:
    std::string templateName;       // object to clone (takes priority)
    std::string prefabPath;         // or a .okayprefab file when no template is set
    float interval = 3.0f;          // seconds between spawns
    int   maxAlive = 5;             // cap on live spawns from this spawner (0 = unlimited)
    int   totalToSpawn = 0;         // lifetime cap (0 = endless)
    float spawnRadius = 6.0f;       // spawns land within this radius of the spawner
    float startDelay = 1.0f;        // wait before the first spawn
    bool  deactivateTemplate = true;// hide the blueprint object on start
    int   count = 1;                // spawns per wave
    int   waves = 0;                // waves to run (0 = endless)
    float waveDelay = 0.0f;         // pause between waves (0 = just `interval`)
    bool  autoStart = true;         // begin at scene start (off = spawner_start / StartWaves)

    int  Spawned() const { return m_total; }
    void StartWaves() { m_running = true; }
    void StopWaves()  { m_running = false; }
    bool Running()   const { return m_running; }
    int  WavesDone() const { return m_wavesDone; }

    void Start() override {
        m_timer = startDelay;
        m_running = autoStart;
        m_left = count > 0 ? count : 1;
        m_wavesDone = 0; m_total = 0;
        m_tag = "spawn:" + (gameObject ? gameObject->name : std::string("?"));
        Vec3 p = transform ? transform->Position() : Vec3{0, 0, 0};
        m_seed = (uint32_t)(std::fabs(p.x) * 73856093.0f + std::fabs(p.z) * 19349663.0f) + 7u;
        if (deactivateTemplate) if (GameObject* t = Template()) if (t != gameObject) t->active = false;
    }

    void Update(float dt) override {
        if (dt <= 0.0f || !m_running) return;
        if (waves > 0 && m_wavesDone >= waves) return;
        if (totalToSpawn > 0 && m_total >= totalToSpawn) return;
        m_timer -= dt;
        if (m_timer > 0.0f) return;
        if (maxAlive > 0 && AliveCount() >= maxAlive) { m_timer = 0.25f; return; }  // wait for room
        if (!SpawnOne()) { m_timer = interval > 0.05f ? interval : 1.0f; return; }  // no template/prefab: retry
        if (--m_left <= 0) {                        // wave finished
            ++m_wavesDone;
            Broadcast("spawner_wave");
            if (waves > 0 && m_wavesDone >= waves) {
                m_running = false;
                Broadcast("spawner_done");
                return;
            }
            m_left = count > 0 ? count : 1;
            m_timer = waveDelay > 0.0f ? waveDelay : interval;
        } else {
            m_timer = interval;
        }
    }

    /// Spawn one copy now (exposed for buttons/scripts/testing). Returns it or null.
    GameObject* SpawnOne() {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s) return nullptr;
        GameObject* clone = nullptr;
        if (!templateName.empty()) {
            GameObject* t = Template();
            if (t && t != gameObject) clone = SceneSerializer::Instantiate(*s, *t);
        } else if (!prefabPath.empty()) {
            clone = SceneSerializer::InstantiateFromFile(*s, prefabPath, nullptr);
        }
        if (!clone) return nullptr;
        clone->active = true;
        clone->tag = m_tag;
        if (clone->transform && transform) {
            float a = Rand() * 6.2831853f, r = Rand() * spawnRadius;
            Vec3 c = transform->Position();
            clone->transform->SetPosition({c.x + std::cos(a) * r, c.y, c.z + std::sin(a) * r});
        }
        ++m_total;
        Broadcast("spawner_spawn");
        return clone;
    }

    int AliveCount() const {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s) return 0;
        int n = 0;
        for (const auto& go : s->Objects()) if (go->active && go->tag == m_tag) ++n;
        return n;
    }

private:
    GameObject* Template() const {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        return (s && !templateName.empty()) ? s->Find(templateName) : nullptr;
    }
    void Broadcast(const std::string& msg) {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (s) for (ActionList* al : s->FindObjectsOfType<ActionList>()) al->ReceiveMessage(msg);
    }
    float Rand() {
        m_seed = m_seed * 1664525u + 1013904223u;
        return (float)((m_seed >> 8) & 0xFFFFFF) / (float)0x1000000;
    }
    float m_timer = 0.0f;
    int   m_total = 0;
    bool  m_running = true;
    int   m_left = 1, m_wavesDone = 0;
    std::string m_tag;
    uint32_t m_seed = 7u;
};

} // namespace okay
