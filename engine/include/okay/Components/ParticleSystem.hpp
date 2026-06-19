#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Render/Color.hpp"
#include <vector>

namespace okay {

/// A simple 2D particle emitter. Spawns particles at the GameObject's position
/// and integrates them with velocity, gravity, fading, and lifetime — a compact
/// version of Unity's ParticleSystem. Deterministic for a given seed.
class ParticleSystem : public Behaviour {
public:
    struct Particle {
        Vec3  position;
        Vec2  velocity;
        float life = 0.0f;     // remaining seconds
        float maxLife = 1.0f;
        float size = 0.25f;
        Color color = Color::White;
        bool  alive = false;
    };

    // Emission
    float emissionRate = 20.0f;   // particles per second
    int   maxParticles = 300;
    bool  playing = true;

    // Per-particle start values
    float startLifetime = 1.5f;
    float startSize = 0.25f;
    Color startColor = Color::White;
    Vec2  startVelocity{0.0f, 3.0f};
    float velocityRandom = 1.5f;   // +/- added to each velocity component
    Vec2  gravity{0.0f, -2.0f};
    bool  fadeOverLife = true;

    std::uint64_t seed = 1234567u;

    /// Clamp the pool size so a malformed/hand-edited scene (negative or absurd
    /// maxParticles) can't trigger length_error / bad_alloc.
    int SafeMax() const { return maxParticles < 0 ? 0 : (maxParticles > 100000 ? 100000 : maxParticles); }

    void Awake() override { m_rng.Seed(seed); m_particles.assign(SafeMax(), Particle{}); }

    void Update(float dt) override {
        if (m_particles.empty()) m_particles.assign(SafeMax(), Particle{});

        // Emit.
        if (playing) {
            m_accum += emissionRate * dt;
            while (m_accum >= 1.0f) {
                m_accum -= 1.0f;
                Emit();
            }
        }
        // Integrate.
        for (Particle& p : m_particles) {
            if (!p.alive) continue;
            p.life -= dt;
            if (p.life <= 0.0f) { p.alive = false; continue; }
            p.velocity += gravity * dt;
            p.position += Vec3{p.velocity * dt};
            if (fadeOverLife) p.color.a = Mathf::Clamp01(p.life / p.maxLife);
        }
    }

    int AliveCount() const {
        int n = 0;
        for (const Particle& p : m_particles) if (p.alive) ++n;
        return n;
    }
    const std::vector<Particle>& Particles() const { return m_particles; }

    /// Spawn a burst of `count` particles immediately.
    void Emit(int count) { for (int i = 0; i < count; ++i) Emit(); }

private:
    void Emit() {
        // Find a free slot (reuse dead particles).
        for (Particle& p : m_particles) {
            if (p.alive) continue;
            p.alive = true;
            p.maxLife = p.life = startLifetime;
            p.size = startSize;
            p.color = startColor;
            p.position = transform ? transform->Position() : Vec3::Zero;
            p.velocity = startVelocity + Vec2{m_rng.Range(-velocityRandom, velocityRandom),
                                              m_rng.Range(-velocityRandom, velocityRandom)};
            return;
        }
    }

    std::vector<Particle> m_particles;
    float m_accum = 0.0f;
    Random m_rng;
};

} // namespace okay
