#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Render/Color.hpp"
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>

namespace okay {

/// A colour gradient of up to `kMaxStops` (time, colour) stops over a 0..1
/// range, linearly blended (no separate alpha track).
/// `count == 0` means "unused" (callers fall back to their start/end colours).
/// Stops may be stored in any order: Evaluate brackets `t` by scanning them,
/// so editing a stop's time never needs a re-sort.
struct ParticleGradient {
    static constexpr int kMaxStops = 8;
    struct Stop { float time = 0.0f; Color color = Color::White; };
    Stop stops[kMaxStops];
    int  count = 0;

    bool Active() const { return count > 0; }
    void Clear() { count = 0; }
    /// Two stops: `a` at birth, `b` at death (what the old start/end fields mean).
    void SetTwo(const Color& a, const Color& b) { count = 2; stops[0] = {0.0f, a}; stops[1] = {1.0f, b}; }
    /// Append a stop (ignored when full); returns its index or -1.
    int Add(float time, const Color& c) {
        if (count >= kMaxStops) return -1;
        stops[count] = {Mathf::Clamp01(time), c};
        return count++;
    }
    void Remove(int i) {
        if (i < 0 || i >= count) return;
        for (int k = i; k + 1 < count; ++k) stops[k] = stops[k + 1];
        --count;
    }

    Color Evaluate(float t) const {
        if (count <= 0) return Color::White;
        // Nearest stop at-or-before t and at-or-after t (order-independent).
        int lo = -1, hi = -1;
        for (int i = 0; i < count && i < kMaxStops; ++i) {
            const float st = stops[i].time;
            if (st <= t && (lo < 0 || st > stops[lo].time)) lo = i;
            if (st >= t && (hi < 0 || st < stops[hi].time)) hi = i;
        }
        if (lo < 0) return stops[hi].color;   // before the first stop
        if (hi < 0) return stops[lo].color;   // after the last stop
        const float span = stops[hi].time - stops[lo].time;
        if (span <= 1e-6f) return stops[lo].color;
        return Color::Lerp(stops[lo].color, stops[hi].color, (t - stops[lo].time) / span);
    }
};

/// A curve of up to `kMaxKeys` (time, value) keys over a 0..1 range, blended
/// linearly or with a smoothstep ease between neighbouring keys. Used as a
/// MULTIPLIER over a particle's life (size x start size, speed x velocity).
/// `count == 0` means "unused". Keys may be stored in any order.
struct ParticleCurve {
    static constexpr int kMaxKeys = 8;
    struct Key { float time = 0.0f; float value = 1.0f; };
    Key  keys[kMaxKeys];
    int  count = 0;
    bool smooth = false;   // false = straight lines, true = eased (S-shaped) segments

    bool Active() const { return count > 0; }
    void Clear() { count = 0; }
    /// A straight line from `a` at birth to `b` at death.
    void SetLinear(float a, float b) { count = 2; keys[0] = {0.0f, a}; keys[1] = {1.0f, b}; }
    int Add(float time, float value) {
        if (count >= kMaxKeys) return -1;
        keys[count] = {Mathf::Clamp01(time), value};
        return count++;
    }
    void Remove(int i) {
        if (i < 0 || i >= count) return;
        for (int k = i; k + 1 < count; ++k) keys[k] = keys[k + 1];
        --count;
    }

    float Evaluate(float t) const {
        if (count <= 0) return 1.0f;
        int lo = -1, hi = -1;
        for (int i = 0; i < count && i < kMaxKeys; ++i) {
            const float kt = keys[i].time;
            if (kt <= t && (lo < 0 || kt > keys[lo].time)) lo = i;
            if (kt >= t && (hi < 0 || kt < keys[hi].time)) hi = i;
        }
        if (lo < 0) return keys[hi].value;
        if (hi < 0) return keys[lo].value;
        const float span = keys[hi].time - keys[lo].time;
        if (span <= 1e-6f) return keys[lo].value;
        float u = (t - keys[lo].time) / span;
        if (smooth) u = u * u * (3.0f - 2.0f * u);
        return keys[lo].value + (keys[hi].value - keys[lo].value) * u;
    }
    /// Largest key value (for scaling a plot); at least 1.
    float MaxValue() const {
        float m = 1.0f;
        for (int i = 0; i < count && i < kMaxKeys; ++i) if (keys[i].value > m) m = keys[i].value;
        return m;
    }
};

/// A billboarded particle emitter — compact but feature-rich, modelled on a classic
/// ParticleSystem. Spawns particles from a configurable emission SHAPE and
/// integrates them with velocity, gravity, drag, colour-over-lifetime,
/// size-over-lifetime, lifetime/size/speed randomness, timed bursts, and a
/// looping duration, plus multi-stop colour gradients, size/speed curves,
/// turbulence noise, orbit/vortex swirl, inherited emitter velocity, world vs
/// local simulation space and one-call presets (ApplyPreset).
/// Deterministic for a given seed. Particles are drawn as
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
        Vec3  velocity;        // full 3D velocity (particles move through space, not a flat plane)
        float life = 0.0f;     // remaining seconds
        float maxLife = 1.0f;
        float size = 0.25f;    // current (rendered) size
        float size0 = 0.25f;   // size at birth (for size-over-life lerp)
        float rotation = 0.0f; // billboard spin, degrees
        float angularVel = 0.0f;  // degrees/second
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
    Vec3  boxSize{1.0f, 1.0f, 1.0f};   // Box extents (3D)

    // ---- Per-particle start values ----
    float startLifetime = 1.5f;
    float startLifetimeRandom = 0.0f;   // +/- seconds
    float startSize = 0.25f;
    float startSizeRandom = 0.0f;       // +/- size units
    Color startColor = Color::White;
    Vec3  startVelocity{0.0f, 3.0f, 0.0f};   // launch velocity (3D)
    float velocityRandom = 1.5f;        // +/- added to each velocity component
    float speedRandom = 0.0f;           // +/- scale on the launch speed (shapes)

    // ---- Appearance ----
    /// Optional sprite texture for each particle (smoke/spark/flare/glow PNG),
    /// drawn as a camera-facing billboard tinted by the particle colour. Empty =
    /// a soft round puff. Build Game bundles the file alongside the exe.
    std::string texture;
    /// Per-particle billboard spin,
    /// degrees and degrees-per-second, each with a +/- randomiser.
    float startRotation = 0.0f;
    float startRotationRandom = 0.0f;
    float rotationSpeed = 0.0f;
    float rotationSpeedRandom = 0.0f;

    /// How particles face the camera. Billboard = an upright camera-facing quad
    /// (the default). Stretch = elongated along the velocity (Stretched
    /// Billboard) for rain, sparks, tracers and speed lines; `stretchScale` sets
    /// how much speed lengthens the streak.
    enum class RenderMode { Billboard, Stretch };
    RenderMode renderMode = RenderMode::Billboard;
    float stretchScale = 0.15f;   // length added per unit of speed

    // ---- Over-lifetime ----
    Color endColor = Color::White;
    bool  colorOverLife = false;        // lerp startColor -> endColor over life
    /// Multi-stop colour over life. When it has stops (and colorOverLife is on)
    /// it REPLACES the startColor -> endColor lerp; empty = the old behaviour.
    ParticleGradient colorGradient;
    float endSize = 0.0f;               // size at death
    bool  sizeOverLife = false;         // lerp size0 -> endSize over life
    /// Size multiplier over life (x the particle's start size). When it has keys
    /// (and sizeOverLife is on) it REPLACES the size0 -> endSize lerp.
    ParticleCurve sizeCurve;
    bool  fadeOverLife = true;          // fade alpha out as the particle dies
    /// Speed multiplier over life: scales how far each particle moves per frame
    /// (1 = unchanged), e.g. a fast launch that slows to a hover.
    bool  speedOverLife = false;
    ParticleCurve speedCurve;

    // ---- Forces ----
    Vec3  gravity{0.0f, -2.0f, 0.0f};   // 3D gravity/wind acceleration
    float gravityModifier = 1.0f;       // scales gravity
    float damping = 0.0f;               // linear drag per second (0 = none)

    // ---- Motion ----
    /// World = particles stay where they were born when the emitter moves (a
    /// smoke trail behind a rocket; the default). Local = live particles are
    /// carried along with the emitter's position and rotation (a torch flame).
    enum class SimulationSpace { World, Local };
    SimulationSpace simulationSpace = SimulationSpace::World;
    /// Fraction (0..1+) of the emitter's own velocity added to each new particle.
    float inheritVelocity = 0.0f;
    /// Turbulence: a smooth 3D noise field that pushes particles around.
    /// Strength is in units/second, frequency is noise cells per unit, and
    /// scroll animates the field over time. 0 strength = off.
    float noiseStrength = 0.0f;
    float noiseFrequency = 1.0f;
    float noiseScroll = 0.5f;
    /// Orbit: spins particles around `orbitAxis` through the emitter at
    /// `orbitSpeed` degrees/second (rings, auras, swirls).
    float orbitSpeed = 0.0f;
    Vec3  orbitAxis{0.0f, 1.0f, 0.0f};
    /// Vortex: a tangential acceleration around `orbitAxis` (a whirlwind) plus
    /// `vortexPull` towards the axis (negative pushes outward).
    float vortexStrength = 0.0f;
    float vortexPull = 0.0f;

    // ---- Collision ----
    /// Bounce particles off a horizontal ground plane at world height `collisionY`
    /// (sparks skittering off a floor, bouncing debris, rain splatter). Cheap — one
    /// plane test per particle, no colliders needed.
    bool  collision = false;
    float collisionY = 0.0f;            // world height of the plane
    float bounce = 0.5f;                // restitution: 0 = stick, 1 = perfect bounce
    float collisionFriction = 0.1f;     // 0..1 tangential speed lost per bounce
    float collisionLifeLoss = 0.0f;     // 0..1 fraction of remaining life lost per bounce

    std::uint64_t seed = 1234567u;

    /// Clamp the pool size so a malformed/hand-edited scene (negative or absurd
    /// maxParticles) can't trigger length_error / bad_alloc.
    int SafeMax() const { return maxParticles < 0 ? 0 : (maxParticles > 100000 ? 100000 : maxParticles); }

    void Awake() override { m_rng.Seed(seed); m_particles.assign(SafeMax(), Particle{}); m_age = 0.0f; m_burstFired = false; m_hasLast = false; }

    /// Begin/stop emitting (existing particles keep living when stopped).
    void Play()  { playing = true; }
    void Stop()  { playing = false; }
    /// Clear all particles and restart the cycle from zero.
    void Restart() { for (Particle& p : m_particles) p.alive = false; m_age = 0.0f; m_burstFired = false; m_accum = 0.0f; }

    // ---- Presets: one-call classic effects (every value stays editable after) ----
    enum class Preset { Fire, Smoke, Sparks, Explosion, Rain, Snow, Magic, Dust, Count };
    static const char* PresetName(Preset p) {
        static const char* kNames[] = {"Fire", "Smoke", "Sparks", "Explosion", "Rain", "Snow", "Magic", "Dust"};
        int i = (int)p;
        return (i >= 0 && i < (int)Preset::Count) ? kNames[i] : "?";
    }

    /// Overwrite the simulation settings with a ready-made look. The sprite
    /// texture, seed and play state are kept; everything else is reset first so
    /// presets don't inherit leftovers from each other. Designed to read well
    /// with the default soft puff (no texture). Live particles are cleared.
    void ApplyPreset(Preset which) {
        // Baseline (the component defaults, minus emission/look specifics).
        shape = Shape::Point; shapeRadius = 1.0f; shapeAngle = 25.0f; boxSize = {1.0f, 1.0f, 1.0f};
        burstCount = 0; burstTime = 0.0f; duration = 0.0f; loop = true;
        startLifetimeRandom = 0.0f; startSizeRandom = 0.0f; velocityRandom = 0.0f; speedRandom = 0.2f;
        startRotation = 0.0f; startRotationRandom = 0.0f; rotationSpeed = 0.0f; rotationSpeedRandom = 0.0f;
        renderMode = RenderMode::Billboard; stretchScale = 0.15f;
        colorOverLife = true; sizeOverLife = false; fadeOverLife = false; speedOverLife = false;
        colorGradient.Clear(); sizeCurve.Clear(); sizeCurve.smooth = true; speedCurve.Clear(); speedCurve.smooth = true;
        endSize = 0.0f; endColor = Color::White;
        gravity = {0.0f, -2.0f, 0.0f}; gravityModifier = 1.0f; damping = 0.0f;
        simulationSpace = SimulationSpace::World; inheritVelocity = 0.0f;
        noiseStrength = 0.0f; noiseFrequency = 1.0f; noiseScroll = 0.5f;
        orbitSpeed = 0.0f; orbitAxis = {0.0f, 1.0f, 0.0f}; vortexStrength = 0.0f; vortexPull = 0.0f;
        collision = false;
        auto& G = colorGradient;
        switch (which) {
            case Preset::Fire:   // rising tongues: white-hot core -> orange -> red smoke, flickering
                emissionRate = 60; maxParticles = 400;
                shape = Shape::Circle; shapeRadius = 0.25f;
                startLifetime = 0.9f; startLifetimeRandom = 0.3f;
                startSize = 0.45f; startSizeRandom = 0.15f;
                startVelocity = {0, 1.8f, 0}; velocityRandom = 0.4f;
                gravity = {0, 1.2f, 0};   // hot air rises
                G.Add(0.0f, {1.0f, 0.95f, 0.6f, 1.0f}); G.Add(0.25f, {1.0f, 0.65f, 0.15f, 1.0f});
                G.Add(0.6f, {0.9f, 0.2f, 0.05f, 0.8f}); G.Add(1.0f, {0.3f, 0.05f, 0.02f, 0.0f});
                startColor = G.stops[0].color;
                sizeOverLife = true; sizeCurve.Add(0.0f, 0.6f); sizeCurve.Add(0.2f, 1.0f); sizeCurve.Add(1.0f, 0.15f);
                noiseStrength = 0.6f; noiseFrequency = 1.5f; noiseScroll = 1.5f;
                break;
            case Preset::Smoke:  // slow gray puffs that grow, swirl and thin out
                emissionRate = 20; maxParticles = 300;
                shape = Shape::Circle; shapeRadius = 0.2f;
                startLifetime = 2.8f; startLifetimeRandom = 0.8f;
                startSize = 0.5f; startSizeRandom = 0.2f;
                startVelocity = {0, 1.2f, 0}; velocityRandom = 0.3f;
                gravity = {0, 0.3f, 0}; damping = 0.3f;
                G.Add(0.0f, {0.5f, 0.5f, 0.52f, 0.0f}); G.Add(0.15f, {0.45f, 0.45f, 0.48f, 0.6f});
                G.Add(1.0f, {0.25f, 0.25f, 0.28f, 0.0f});
                startColor = G.stops[1].color;
                sizeOverLife = true; sizeCurve.Add(0.0f, 0.5f); sizeCurve.Add(1.0f, 2.8f);
                noiseStrength = 0.8f; noiseFrequency = 0.6f; noiseScroll = 0.4f;
                rotationSpeed = 20.0f; startRotationRandom = 180.0f;
                break;
            case Preset::Sparks: // fast streaks that arc down under gravity and cool
                emissionRate = 90; maxParticles = 500;
                startLifetime = 0.6f; startLifetimeRandom = 0.2f;
                startSize = 0.08f; startSizeRandom = 0.03f;
                startVelocity = {0, 4.0f, 0}; velocityRandom = 3.5f;
                gravity = {0, -9.0f, 0}; damping = 0.5f;
                G.Add(0.0f, {1.0f, 1.0f, 0.85f, 1.0f}); G.Add(0.4f, {1.0f, 0.75f, 0.25f, 1.0f});
                G.Add(1.0f, {1.0f, 0.3f, 0.05f, 0.0f});
                startColor = G.stops[0].color;
                renderMode = RenderMode::Stretch;
                break;
            case Preset::Explosion: // one big burst every 2 s: flash -> fireball -> drifting smoke
                emissionRate = 0; maxParticles = 300;
                burstCount = 150; burstTime = 0.0f; duration = 2.0f; loop = true;
                shape = Shape::Sphere; shapeRadius = 0.3f;
                startLifetime = 1.1f; startLifetimeRandom = 0.4f;
                startSize = 0.5f; startSizeRandom = 0.2f;
                startVelocity = {0, 6.0f, 0}; speedRandom = 0.4f; velocityRandom = 0.5f;
                gravity = {0, 0.8f, 0}; damping = 2.5f;
                G.Add(0.0f, {1.0f, 1.0f, 0.85f, 1.0f}); G.Add(0.12f, {1.0f, 0.8f, 0.3f, 1.0f});
                G.Add(0.35f, {1.0f, 0.35f, 0.08f, 0.9f}); G.Add(0.7f, {0.3f, 0.28f, 0.27f, 0.6f});
                G.Add(1.0f, {0.15f, 0.15f, 0.15f, 0.0f});
                startColor = G.stops[0].color;
                sizeOverLife = true; sizeCurve.Add(0.0f, 0.4f); sizeCurve.Add(0.3f, 1.2f); sizeCurve.Add(1.0f, 1.8f);
                noiseStrength = 0.5f; noiseFrequency = 1.0f; noiseScroll = 0.8f;
                break;
            case Preset::Rain:   // fast streaks falling through a wide box
                emissionRate = 300; maxParticles = 2000;
                shape = Shape::Box; boxSize = {12, 0.1f, 12};
                startLifetime = 1.6f; startLifetimeRandom = 0.2f;
                startSize = 0.06f;
                startColor = {0.60f, 0.72f, 1.0f, 0.8f}; endColor = {0.60f, 0.72f, 1.0f, 0.4f};
                startVelocity = {0, -9.0f, 0}; velocityRandom = 0.4f;
                gravityModifier = 0.0f;
                renderMode = RenderMode::Stretch;
                break;
            case Preset::Snow:   // slow flakes drifting on a gentle breeze
                emissionRate = 80; maxParticles = 1500;
                shape = Shape::Box; boxSize = {12, 0.1f, 12};
                startLifetime = 6.0f; startLifetimeRandom = 2.0f;
                startSize = 0.09f; startSizeRandom = 0.04f;
                startColor = {1, 1, 1, 0.95f}; endColor = {1, 1, 1, 0.0f};
                startVelocity = {0, -0.8f, 0}; velocityRandom = 0.4f;
                gravityModifier = 0.0f; damping = 0.4f;
                noiseStrength = 0.5f; noiseFrequency = 0.8f; noiseScroll = 0.3f;
                break;
            case Preset::Magic:  // violet-to-cyan motes circling and twinkling around the emitter
                emissionRate = 40; maxParticles = 400;
                shape = Shape::Sphere; shapeRadius = 0.6f;
                startLifetime = 1.6f; startLifetimeRandom = 0.5f;
                startSize = 0.15f; startSizeRandom = 0.08f;
                startVelocity = {0, 0.6f, 0}; velocityRandom = 0.3f;
                gravityModifier = 0.0f; rotationSpeed = 90.0f;
                G.Add(0.0f, {0.55f, 0.40f, 1.0f, 1.0f}); G.Add(0.6f, {0.20f, 0.90f, 1.0f, 0.9f});
                G.Add(1.0f, {0.9f, 0.95f, 1.0f, 0.0f});
                startColor = G.stops[0].color;
                sizeOverLife = true; sizeCurve.Add(0.0f, 0.0f); sizeCurve.Add(0.2f, 1.0f); sizeCurve.Add(1.0f, 0.0f);
                orbitSpeed = 120.0f; vortexPull = 0.5f;
                noiseStrength = 0.3f; noiseFrequency = 1.2f; noiseScroll = 1.0f;
                break;
            case Preset::Dust:   // faint motes hanging in the air, drifting lazily
                emissionRate = 15; maxParticles = 200;
                shape = Shape::Box; boxSize = {4, 0.5f, 4};
                startLifetime = 4.0f; startLifetimeRandom = 1.5f;
                startSize = 0.06f; startSizeRandom = 0.03f;
                startVelocity = {0, 0.05f, 0}; velocityRandom = 0.1f;
                gravityModifier = 0.0f; damping = 0.5f;
                G.Add(0.0f, {0.8f, 0.72f, 0.55f, 0.0f}); G.Add(0.3f, {0.8f, 0.72f, 0.55f, 0.5f});
                G.Add(1.0f, {0.8f, 0.72f, 0.55f, 0.0f});
                startColor = G.stops[1].color;
                noiseStrength = 0.35f; noiseFrequency = 0.7f; noiseScroll = 0.2f;
                break;
            case Preset::Count: break;
        }
        // Keep the plain start/end fields in step with the gradient's ends, so
        // switching the gradient off later still gives a sensible two-colour fade.
        if (G.Active()) { endColor = G.Evaluate(1.0f); }
        if (sizeCurve.Active()) endSize = startSize * sizeCurve.Evaluate(1.0f);
        Restart();
    }

    void Update(float dt) override {
        if (m_particles.empty()) m_particles.assign(SafeMax(), Particle{});

        // Track the emitter's motion: its velocity (for inheritVelocity) and, in
        // Local space, carry live particles along by this frame's move + turn.
        const Vec3 origin = transform ? transform->Position() : Vec3::Zero;
        const Quat rot = transform ? transform->Rotation() : Quat::Identity;
        m_time += dt;
        if (m_hasLast) {
            m_emitterVel = dt > 1e-6f ? (origin - m_lastOrigin) * (1.0f / dt) : Vec3::Zero;
            if (simulationSpace == SimulationSpace::Local) {
                const Quat dq = rot * m_lastRot.Inverse();
                for (Particle& p : m_particles) {
                    if (!p.alive) continue;
                    p.position = origin + dq * (p.position - m_lastOrigin);
                    p.velocity = dq * p.velocity;
                }
            }
        } else {
            m_emitterVel = Vec3::Zero;
        }
        m_lastOrigin = origin; m_lastRot = rot; m_hasLast = true;

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
        // Orbit / vortex axis (unit), rotated with the emitter in Local space.
        const bool swirl = orbitSpeed != 0.0f || vortexStrength != 0.0f || vortexPull != 0.0f;
        Vec3 axis{0.0f, 1.0f, 0.0f};
        if (swirl) {
            float al = VLen(orbitAxis);
            if (al > 1e-5f) axis = orbitAxis * (1.0f / al);
            if (simulationSpace == SimulationSpace::Local) axis = rot * axis;
        }
        const float orbitRad = orbitSpeed * 0.0174532925f * dt;
        const float orbitC = std::cos(orbitRad), orbitS = std::sin(orbitRad);
        const bool useSpeedCurve = speedOverLife && speedCurve.Active();
        for (Particle& p : m_particles) {
            if (!p.alive) continue;
            p.life -= dt;
            if (p.life <= 0.0f) { p.alive = false; continue; }
            p.velocity += gravity * (gravityModifier * dt);
            if (swirl && (vortexStrength != 0.0f || vortexPull != 0.0f)) {
                // Radial offset from the axis line through the emitter.
                Vec3 d = p.position - origin;
                Vec3 radial = d - axis * Vec3::Dot(d, axis);
                float rl = VLen(radial);
                if (rl > 1e-5f) {
                    Vec3 rn = radial * (1.0f / rl);
                    p.velocity += (Vec3::Cross(axis, rn) * vortexStrength - rn * vortexPull) * dt;
                }
            }
            if (drag != 1.0f) p.velocity *= drag;
            if (useSpeedCurve) {
                float tl = 1.0f - Mathf::Clamp01(p.life / p.maxLife);
                p.position += p.velocity * (speedCurve.Evaluate(tl) * dt);
            } else {
                p.position += p.velocity * dt;
            }
            if (noiseStrength != 0.0f) {
                // Turbulence moves the particle directly (it doesn't build up speed),
                // so it stays a gentle wobble at any strength.
                Vec3 q = p.position * noiseFrequency;
                float s = m_time * noiseScroll;
                Vec3 n{Noise3(q.x + s, q.y, q.z),
                       Noise3(q.x + 31.4f, q.y + s, q.z + 17.9f),
                       Noise3(q.x - 23.1f, q.y + 47.2f, q.z + s)};
                p.position += n * (noiseStrength * dt);
            }
            if (orbitSpeed != 0.0f) {
                // Rodrigues rotation of the offset about the axis (keeps the radius).
                Vec3 d = p.position - origin;
                d = d * orbitC + Vec3::Cross(axis, d) * orbitS + axis * (Vec3::Dot(axis, d) * (1.0f - orbitC));
                p.position = origin + d;
            }
            p.rotation += p.angularVel * dt;
            // Ground-plane collision: reflect downward motion, lose tangential speed
            // to friction and (optionally) some life on each impact.
            if (collision && p.position.y < collisionY && p.velocity.y < 0.0f) {
                p.position.y = collisionY;
                p.velocity.y = -p.velocity.y * bounce;
                float keep = 1.0f - Mathf::Clamp01(collisionFriction);
                p.velocity.x *= keep; p.velocity.z *= keep;
                if (collisionLifeLoss > 0.0f) p.life -= p.maxLife * Mathf::Clamp01(collisionLifeLoss);
            }
            float t = 1.0f - Mathf::Clamp01(p.life / p.maxLife);   // 0 at birth -> 1 at death
            if (sizeOverLife) {
                if (sizeCurve.Active()) p.size = p.size0 * sizeCurve.Evaluate(t);
                else                    p.size = p.size0 + (endSize - p.size0) * t;
            }
            if (colorOverLife && colorGradient.Active()) {
                p.color = colorGradient.Evaluate(t);
            } else if (colorOverLife) {
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
            p.rotation = startRotation + (startRotationRandom > 0.0f
                         ? m_rng.Range(-startRotationRandom, startRotationRandom) : 0.0f);
            p.angularVel = rotationSpeed + (rotationSpeedRandom > 0.0f
                           ? m_rng.Range(-rotationSpeedRandom, rotationSpeedRandom) : 0.0f);
            p.color = startColor;

            Vec3 origin = transform ? transform->Position() : Vec3::Zero;
            Vec3 off{0.0f, 0.0f, 0.0f};
            Vec3 vel = startVelocity;
            const float spd = VLen(startVelocity);
            switch (shape) {
                case Shape::Point: break;
                case Shape::Circle: {
                    // Ring laid flat on the ground (XZ plane), flung outward — reads
                    // as a 3D ripple/shockwave from any camera angle.
                    float a = m_rng.Range(0.0f, 6.2831853f);
                    Vec3 dir{std::cos(a), 0.0f, std::sin(a)};
                    off = dir * shapeRadius;
                    vel = dir * spd + Vec3{0.0f, startVelocity.y, 0.0f};
                } break;
                case Shape::Sphere: {
                    // Filled 3D ball, pushed radially outward (a real burst, not a disc).
                    Vec3 dir = RandUnitVec();
                    float r = shapeRadius * std::cbrt(m_rng.Range(0.0f, 1.0f));
                    off = dir * r;
                    vel = dir * spd;
                } break;
                case Shape::Box: {
                    off = Vec3{m_rng.Range(-boxSize.x * 0.5f, boxSize.x * 0.5f),
                               m_rng.Range(-boxSize.y * 0.5f, boxSize.y * 0.5f),
                               m_rng.Range(-boxSize.z * 0.5f, boxSize.z * 0.5f)};
                } break;
                case Shape::Cone: {
                    // Spray within a 3D cone around the start-velocity axis.
                    Vec3 axis = spd > 1e-5f ? startVelocity * (1.0f / spd) : Vec3{0, 1, 0};
                    float half = shapeAngle * 0.0174532925f;
                    float cosT = std::cos(half);
                    float ct = m_rng.Range(cosT, 1.0f);            // cosine of the polar angle
                    float st = std::sqrt(std::fmax(0.0f, 1.0f - ct * ct));
                    float phi = m_rng.Range(0.0f, 6.2831853f);
                    // Build an orthonormal frame around the cone axis.
                    Vec3 up = std::fabs(axis.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
                    Vec3 t1 = Vec3::Cross(up, axis); { float l = VLen(t1); t1 = l > 1e-5f ? t1 * (1.0f / l) : Vec3{1, 0, 0}; }
                    Vec3 t2 = Vec3::Cross(axis, t1);
                    Vec3 dir = axis * ct + (t1 * std::cos(phi) + t2 * std::sin(phi)) * st;
                    vel = dir * (spd > 1e-5f ? spd : 1.0f);
                } break;
                case Shape::Edge: {
                    off = Vec3{m_rng.Range(-shapeRadius, shapeRadius), 0.0f, 0.0f};
                } break;
            }
            if (speedRandom > 0.0f) vel *= (1.0f + m_rng.Range(-speedRandom, speedRandom));
            if (velocityRandom > 0.0f)
                vel += Vec3{m_rng.Range(-velocityRandom, velocityRandom),
                            m_rng.Range(-velocityRandom, velocityRandom),
                            m_rng.Range(-velocityRandom, velocityRandom)};
            if (inheritVelocity != 0.0f) vel += m_emitterVel * inheritVelocity;

            p.position = origin + off;
            p.velocity = vel;
            return;
        }
    }

    static float VLen(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

    /// A uniformly-distributed unit vector on the sphere (for 3D burst directions).
    Vec3 RandUnitVec() {
        float z = m_rng.Range(-1.0f, 1.0f);
        float a = m_rng.Range(0.0f, 6.2831853f);
        float r = std::sqrt(std::fmax(0.0f, 1.0f - z * z));
        return Vec3{r * std::cos(a), r * std::sin(a), z};
    }

    /// Cheap deterministic 3D value noise (hashed lattice, smoothstep blend), ~[-1,1].
    static float Hash3(int x, int y, int z) {
        std::uint32_t h = (std::uint32_t)x * 374761393u + (std::uint32_t)y * 668265263u + (std::uint32_t)z * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float)((h ^ (h >> 16)) & 0xffffu) * (2.0f / 65535.0f) - 1.0f;
    }
    static float Noise3(float x, float y, float z) {
        const float fx0 = std::floor(x), fy0 = std::floor(y), fz0 = std::floor(z);
        const int xi = (int)fx0, yi = (int)fy0, zi = (int)fz0;
        auto sm = [](float t) { return t * t * (3.0f - 2.0f * t); };
        const float fx = sm(x - fx0), fy = sm(y - fy0), fz = sm(z - fz0);
        auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
        float c00 = lerp(Hash3(xi, yi, zi),         Hash3(xi + 1, yi, zi), fx);
        float c10 = lerp(Hash3(xi, yi + 1, zi),     Hash3(xi + 1, yi + 1, zi), fx);
        float c01 = lerp(Hash3(xi, yi, zi + 1),     Hash3(xi + 1, yi, zi + 1), fx);
        float c11 = lerp(Hash3(xi, yi + 1, zi + 1), Hash3(xi + 1, yi + 1, zi + 1), fx);
        return lerp(lerp(c00, c10, fy), lerp(c01, c11, fy), fz);
    }

    std::vector<Particle> m_particles;
    float m_accum = 0.0f;
    float m_age = 0.0f;        // seconds into the current cycle
    bool  m_burstFired = false;
    Random m_rng;
    // Emitter motion tracking (inheritVelocity + Local space).
    float m_time = 0.0f;       // total simulated seconds (scrolls the noise field)
    bool  m_hasLast = false;
    Vec3  m_lastOrigin{0.0f, 0.0f, 0.0f};
    Quat  m_lastRot = Quat::Identity;
    Vec3  m_emitterVel{0.0f, 0.0f, 0.0f};
};

} // namespace okay
