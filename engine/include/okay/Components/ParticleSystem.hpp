#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Render/Color.hpp"
#include <vector>
#include <cmath>

namespace okay {

/// A billboarded particle emitter — a compact but feature-rich take on Unity's
/// ParticleSystem. Spawns particles from a configurable emission SHAPE and
/// integrates them with velocity, gravity, drag, colour-over-lifetime,
/// size-over-lifetime, lifetime/size/speed randomness, timed bursts, and a
/// looping duration. Deterministic for a given seed. Particles are drawn as
/// small world-space quads (position + size + colour), so the renderer only
/// needs those three fields and nothing here breaks the existing draw path.
class ParticleSystem : public Behaviour {
public:
    /// Where new particles spawn and which way they're flung.
    ///   Point  — all from the emitter origin (classic).
    ///   Circle — on a ring of `shapeRadius`, pushed outward.
    ///   Sphere — filled disc of `shapeRadius`, pushed outward (2D billboard).
    ///   Box    — inside a `boxSize` rectangle, using the start velocity.
    ///   Cone   — a `shapeAngle`-degree spray around the start velocity.
    ///   Edge   — along a horizontal line of width `shapeRadius`*2.
    enum class Shape { Point, Circle, Sphere, Box, Cone, Edge };

    struct Particle {
        Vec3  position;
        Vec2  velocity;
        float life = 0.0f;     // remaining seconds
        float maxLife = 1.0f;
        float size = 0.25f;    // current (rendered) size
        float size0 = 0.25f;   // size at birth (for size-over-life lerp)
        Color color = Color::White;
        bool  alive = false;
    };

    // ---- Emission ----
    float emissionRate = 20.0f;   // particles per second
    int   maxParticles = 300;
    bool  playing = true;

    // ---- Duration / looping ----
    float duration = 0.0f;        // seconds of emission per cycle (0 = forever)
    bool  loop = true;            // restart the cycle (and refire bursts) at `duration`

    // ---- Burst (one shot of N particles at `burstTime` into each cycle) ----
    int   burstCount = 0;         // 0 = no burst
    float burstTime  = 0.0f;      // seconds into the cycle when the burst fires

    // ---- Emission shape ----
    Shape shape = Shape::Point;
    float shapeRadius = 1.0f;     // Circle/Sphere/Edge extent
    float shapeAngle  = 25.0f;    // Cone half-angle, degrees
    Vec2  boxSize{1.0f, 1.0f};    // Box extents

    // ---- Per-particle start values ----
    float startLifetime = 1.5f;
    float startLifetimeRandom = 0.0f;   // +/- seconds
    float startSize = 0.25f;
    float startSizeRandom = 0.0f;       // +/- size units
    Color startColor = Color::White;
    Vec2  startVelocity{0.0f, 3.0f};
    float velocityRandom = 1.5f;        // +/- added to each velocity component
    float speedRandom = 0.0f;           // +/- scale on the launch speed (shapes)

    // ---- Over-lifetime ----
    Color endColor = Color::White;
    bool  colorOverLife = false;        // lerp startColor -> endColor over life
    float endSize = 0.0f;               // size at death
    bool  sizeOverLife = false;         // lerp size0 -> endSize over life
    bool  fadeOverLife = true;          // fade alpha out as the particle dies

    // ---- Forces ----
    Vec2  gravity{0.0f, -2.0f};
    float gravityModifier = 1.0f;       // scales gravity (Unity-style)
    float damping = 0.0f;               // linear drag per second (0 = none)

    std::uint64_t seed = 1234567u;

    /// Clamp the pool size so a malformed/hand-edited scene (negative or absurd
    /// maxParticles) can't trigger length_error / bad_alloc.
    int SafeMax() const { return maxParticles < 0 ? 0 : (maxParticles > 100000 ? 100000 : maxParticles); }

    void Awake() override { m_rng.Seed(seed); m_particles.assign(SafeMax(), Particle{}); m_age = 0.0f; m_burstFired = false; }

    /// Begin/stop emitting (existing particles keep living when stopped).
    void Play()  { playing = true; }
    void Stop()  { playing = false; }
    /// Clear all particles and restart the cycle from zero.
    void Restart() { for (Particle& p : m_particles) p.alive = false; m_age = 0.0f; m_burstFired = false; m_accum = 0.0f; }

    void Update(float dt) override {
        if (m_particles.empty()) m_particles.assign(SafeMax(), Particle{});

        // Advance the cycle clock and decide whether we're still emitting.
        bool emitting = playing;
        if (playing) {
            m_age += dt;
            if (duration > 0.0f) {
                if (m_age >= duration) {
                    if (loop) { m_age = std::fmod(m_age, duration); m_burstFired = false; }
                    else      { emitting = false; }
                }
            }
            // Timed burst (once per cycle).
            if (emitting && burstCount > 0 && !m_burstFired && m_age >= burstTime) {
                Emit(burstCount);
                m_burstFired = true;
            }
        }

        // Steady-rate emission.
        if (emitting && emissionRate > 0.0f) {
            m_accum += emissionRate * dt;
            while (m_accum >= 1.0f) { m_accum -= 1.0f; Emit(); }
        }

        // Integrate.
        const float drag = damping > 0.0f ? Mathf::Clamp01(1.0f - damping * dt) : 1.0f;
        for (Particle& p : m_particles) {
            if (!p.alive) continue;
            p.life -= dt;
            if (p.life <= 0.0f) { p.alive = false; continue; }
            p.velocity += gravity * (gravityModifier * dt);
            if (drag != 1.0f) p.velocity *= drag;
            p.position += Vec3{p.velocity * dt};
            float t = 1.0f - Mathf::Clamp01(p.life / p.maxLife);   // 0 at birth -> 1 at death
            if (sizeOverLife) p.size = p.size0 + (endSize - p.size0) * t;
            if (colorOverLife) {
                p.color.r = startColor.r + (endColor.r - startColor.r) * t;
                p.color.g = startColor.g + (endColor.g - startColor.g) * t;
                p.color.b = startColor.b + (endColor.b - startColor.b) * t;
                p.color.a = startColor.a + (endColor.a - startColor.a) * t;
            }
            if (fadeOverLife) p.color.a *= Mathf::Clamp01(p.life / p.maxLife);
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
            p.maxLife = p.life = startLifetime + (startLifetimeRandom > 0.0f
                                  ? m_rng.Range(-startLifetimeRandom, startLifetimeRandom) : 0.0f);
            if (p.life < 0.01f) p.life = p.maxLife = 0.01f;
            p.size0 = p.size = startSize + (startSizeRandom > 0.0f
                               ? m_rng.Range(-startSizeRandom, startSizeRandom) : 0.0f);
            if (p.size0 < 0.0f) p.size0 = p.size = 0.0f;
            p.color = startColor;

            Vec3 origin = transform ? transform->Position() : Vec3::Zero;
            Vec2 off{0.0f, 0.0f};
            Vec2 vel = startVelocity;
            switch (shape) {
                case Shape::Point: break;
                case Shape::Circle: {
                    float a = m_rng.Range(0.0f, 6.2831853f);
                    Vec2 dir{std::cos(a), std::sin(a)};
                    off = dir * shapeRadius;
                    vel = dir * VLen(startVelocity);
                } break;
                case Shape::Sphere: {
                    float a = m_rng.Range(0.0f, 6.2831853f);
                    float r = shapeRadius * std::sqrt(m_rng.Range(0.0f, 1.0f));
                    Vec2 dir{std::cos(a), std::sin(a)};
                    off = dir * r;
                    vel = dir * VLen(startVelocity);
                } break;
                case Shape::Box: {
                    off = Vec2{m_rng.Range(-boxSize.x * 0.5f, boxSize.x * 0.5f),
                               m_rng.Range(-boxSize.y * 0.5f, boxSize.y * 0.5f)};
                } break;
                case Shape::Cone: {
                    float base = std::atan2(startVelocity.y, startVelocity.x);
                    float half = shapeAngle * 0.0174532925f;
                    float a = base + m_rng.Range(-half, half);
                    float spd = VLen(startVelocity);
                    vel = Vec2{std::cos(a) * spd, std::sin(a) * spd};
                } break;
                case Shape::Edge: {
                    off = Vec2{m_rng.Range(-shapeRadius, shapeRadius), 0.0f};
                } break;
            }
            if (speedRandom > 0.0f) vel *= (1.0f + m_rng.Range(-speedRandom, speedRandom));
            vel += Vec2{velocityRandom > 0.0f ? m_rng.Range(-velocityRandom, velocityRandom) : 0.0f,
                        velocityRandom > 0.0f ? m_rng.Range(-velocityRandom, velocityRandom) : 0.0f};

            p.position = origin + Vec3{off};
            p.velocity = vel;
            return;
        }
    }

    static float VLen(const Vec2& v) { float l = std::sqrt(v.x * v.x + v.y * v.y); return l; }

    std::vector<Particle> m_particles;
    float m_accum = 0.0f;
    float m_age = 0.0f;        // seconds into the current cycle
    bool  m_burstFired = false;
    Random m_rng;
};

} // namespace okay
