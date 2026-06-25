#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <string>
#include <cstdint>
#include <cmath>

namespace okay {

/// Spawns copies of a template object over time — enemy waves, loot, particles.
/// Names a `templateName` object in the scene to clone (deactivated at start so it's
/// just a blueprint), then instantiates copies near the spawner every `interval`
/// seconds, keeping at most `maxAlive` of its own spawns alive at once and stopping
/// after `totalToSpawn` (0 = endless). Each spawn is tagged so the live count is
/// accurate even as they die.
class Spawner : public Behaviour {
public:
    std::string templateName;       // object to clone
    float interval = 3.0f;          // seconds between spawns
    int   maxAlive = 5;             // cap on live spawns from this spawner
    int   totalToSpawn = 0;         // lifetime cap (0 = endless)
    float spawnRadius = 6.0f;       // spawns land within this radius of the spawner
    float startDelay = 1.0f;        // wait before the first spawn
    bool  deactivateTemplate = true;// hide the blueprint object on start

    int Spawned() const { return m_total; }

    void Start() override {
        m_timer = startDelay;
        m_tag = "spawn:" + (gameObject ? gameObject->name : std::string("?"));
        Vec3 p = transform ? transform->Position() : Vec3{0, 0, 0};
        m_seed = (uint32_t)(std::fabs(p.x) * 73856093.0f + std::fabs(p.z) * 19349663.0f) + 7u;
        if (deactivateTemplate) if (GameObject* t = Template()) t->active = false;
    }

    void Update(float dt) override {
        if (dt <= 0.0f) return;
        m_timer -= dt;
        if (m_timer > 0.0f) return;
        m_timer = interval;
        if (totalToSpawn > 0 && m_total >= totalToSpawn) return;
        if (AliveCount() >= maxAlive) return;
        SpawnOne();
    }

    /// Spawn one copy now (exposed for buttons/scripts/testing). Returns it or null.
    GameObject* SpawnOne() {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        GameObject* t = Template();
        if (!s || !t) return nullptr;
        GameObject* clone = SceneSerializer::Instantiate(*s, *t);
        if (!clone) return nullptr;
        clone->active = true;
        clone->tag = m_tag;
        if (clone->transform && transform) {
            float a = Rand() * 6.2831853f, r = Rand() * spawnRadius;
            Vec3 c = transform->Position();
            clone->transform->SetPosition({c.x + std::cos(a) * r, c.y, c.z + std::sin(a) * r});
        }
        ++m_total;
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
        return s ? s->Find(templateName) : nullptr;
    }
    float Rand() {
        m_seed = m_seed * 1664525u + 1013904223u;
        return (float)((m_seed >> 8) & 0xFFFFFF) / (float)0x1000000;
    }
    float m_timer = 0.0f;
    int   m_total = 0;
    std::string m_tag;
    uint32_t m_seed = 7u;
};

} // namespace okay
