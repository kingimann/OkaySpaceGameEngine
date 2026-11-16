#pragma once
// A small, ready-to-use source of rigid debris: every `interval` seconds it throws a burst of small chunks from its object
// (spread in a cone around the object's up direction), using DebrisPool. The chunks fly, land, settle, sleep and shrink away,
// and there is never more than `maxActive` of them. Put it on an empty object, press Play and watch the pile build and clear.
// For code that spawns debris at its own moments (a digger, an explosion) use DebrisPool directly.
#include "okay/Physics/DebrisPool.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <memory>

namespace okay {

class DebrisEmitter : public Behaviour {
public:
    int   count = 12;            ///< chunks per burst
    float interval = 1.5f;       ///< seconds between bursts (0 = one burst at the start, then none)
    float startDelay = 0.5f;     ///< seconds before the first burst
    float sizeMin = 0.12f;       ///< smallest chunk edge (metres)
    float sizeMax = 0.30f;       ///< largest chunk edge (metres)
    float speed = 5.0f;          ///< launch speed (metres per second)
    float spread = 0.6f;         ///< how wide the cone is (0 = straight up, 1 = a flat fan)
    Color color{0.72f, 0.55f, 0.38f, 1.0f};
    int   maxActive = 150;       ///< most chunks alive at once (the oldest is recycled)
    float lifetime = 12.0f;      ///< seconds a chunk may live
    float settledLife = 4.0f;    ///< seconds a chunk stays once it has landed and gone to sleep
    bool  collideWithEachOther = false;   ///< off: chunks only hit the world (much cheaper)

    /// The pool behind it (made when Play starts), for tests and tools.
    DebrisPool* pool() { return m_pool.get(); }
    int Bursts() const { return m_bursts; }

    void Update(float dt) override {
        if (!gameObject || !gameObject->scene() || !(dt > 0.0f)) return;
        Scene& scene = *gameObject->scene();
        if (!m_pool) {
            DebrisSettings s;
            s.maxActive = maxActive; s.lifetime = lifetime; s.settledLife = settledLife; s.collideWithEachOther = collideWithEachOther;
            m_pool = std::make_unique<DebrisPool>(scene, s);
            m_clock = -startDelay;
        }
        m_pool->settings().maxActive = maxActive;
        m_pool->settings().lifetime = lifetime;
        m_pool->settings().settledLife = settledLife;
        m_clock += dt;
        if (m_clock >= 0.0f && (m_bursts == 0 || (interval > 0.0f && m_clock >= m_nextAt))) {
            Burst();
            m_nextAt = m_clock + interval;
        }
        m_pool->Update(dt);
    }

    /// Throw one burst now.
    void Burst() {
        if (!m_pool || !gameObject || !gameObject->transform) return;
        const Vec3 origin = gameObject->transform->Position();
        const Quat rot = gameObject->transform->Rotation();
        for (int i = 0; i < count; ++i) {
            const float a = Rand() * 6.2831853f, r = Rand() * spread;
            Vec3 dir = Vec3{r * Mathf::Cos(a), 1.0f, r * Mathf::Sin(a)}.Normalized();
            dir = rot * dir;
            const float lo = std::min(sizeMin, sizeMax), hi = std::max(sizeMin, sizeMax);
            const float edge = lo + (hi - lo) * Rand();
            const float v = speed * (0.6f + 0.4f * Rand());
            m_pool->Spawn(origin + dir * 0.2f, Vec3{edge, edge * (0.7f + 0.3f * Rand()), edge}, dir * v, color,
                          Vec3{(Rand() - 0.5f) * 8.0f, (Rand() - 0.5f) * 8.0f, (Rand() - 0.5f) * 8.0f});
        }
        ++m_bursts;
    }

private:
    float Rand() { m_seed = m_seed * 1664525u + 1013904223u; return (float)((m_seed >> 8) & 0xFFFFFF) / 16777216.0f; }
    std::unique_ptr<DebrisPool> m_pool;
    float m_clock = 0.0f, m_nextAt = 0.0f;
    int m_bursts = 0;
    std::uint32_t m_seed = 12345u;
};

} // namespace okay
