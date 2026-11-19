#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Environment/Wind.hpp"
#include "okay/Environment/WindField.hpp"
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

/// A particle emitter. One component, one pool, one simulation: emission shapes and rates, sub-emitters,
/// motion modules (forces, noise, orbit, vortex), collision, billboards / streaks / ribbons / cubes and
/// one-click presets. Deterministic for a given seed and frame-rate independent (the emission, the motion and
/// the colour / size curves depend on time, not on how many frames it was cut into).
///
/// Storage is a pooled structure of arrays (one flat float block, `maxParticles` slots, no allocation while it
/// runs); live particles are packed at the front, so a pass costs what is alive, not what the cap is.
/// The simulation lives in engine/src/Components/ParticleSystem.cpp, the presets in ParticlePresets.cpp, and the
/// drawing (sorting, billboard modes, sheets, ribbons, culling) in engine/src/Render/ParticleDraw.cpp.
/// Written from general particle-effect techniques (value noise, Rodrigues rotation, trapezoid integration,
/// cone / sphere / mesh-area sampling), not from any other engine.
class ParticleSystem : public Behaviour {
public:
    /// Where new particles spawn and which way they are flung.
    ///   Point      - all from the emitter origin.
    ///   Circle     - on a flat ring of `shapeRadius` (XZ plane), pushed outward.
    ///   Sphere     - a ball (or only its skin with `shapeSurfaceOnly`), pushed outward.
    ///   Box        - inside (or only on the faces of) a `boxSize` box, using the start velocity.
    ///   Cone       - a `shapeAngle`-degree spray around the start velocity.
    ///   Edge       - along a line of width `shapeRadius`*2.
    ///   Hemisphere - like Sphere, but only the upper half (a fountain, a dome).
    ///   MeshSurface- on the skin of the Mesh Renderer on the same object, pushed along the surface normal.
    /// (New values go at the end: they are saved as numbers.)
    enum class Shape { Point, Circle, Sphere, Box, Cone, Edge, Hemisphere, MeshSurface };

    /// How a particle is drawn. Billboard = faces the camera. Stretch = a streak along the velocity.
    /// Horizontal = lies flat (ripples, ground glow). Vertical = stands upright and turns to face the camera
    /// sideways (flames, grass wisps). Mesh = a small shaded 3D cube (debris, confetti blocks).
    enum class RenderMode { Billboard, Stretch, Horizontal, Vertical, Mesh };
    enum class BlendMode { Alpha, Additive, Premultiplied };
    /// What is drawn in front: Distance = the nearer one (default), OldestFirst / YoungestFirst = by age, None = birth order.
    enum class SortMode { Distance, OldestFirst, YoungestFirst, None };
    /// How the picture sheet (flipbook) is stepped: over the particle's life, a random frame, or looping.
    enum class SheetMode { OverLife, Random, Loop };
    /// What happens when the pool is full: DropNew = new ones are skipped, ReplaceOldest = an old one makes room.
    enum class CapPolicy { DropNew, ReplaceOldest };
    enum class SimulationSpace { World, Local };

    /// A copy of one particle (the pool itself is stored as arrays).
    struct Particle {
        Vec3  position;
        Vec3  velocity;
        float life = 0.0f;     // remaining seconds
        float maxLife = 1.0f;
        float size = 0.25f;    // current (rendered) size
        float size0 = 0.25f;   // size at birth
        float rotation = 0.0f; // degrees
        float angularVel = 0.0f;  // degrees/second
        Color color = Color::White;
        Color color0 = Color::White;
        float random = 0.0f;   // a fixed 0..1 value per particle (picks its sheet frame, its tilt)
        bool  alive = false;
    };

    /// One timed burst of the extra list.
    struct Burst { float time = 0.0f; int count = 0; };
    static constexpr int kMaxBursts = 4;
    static constexpr int kMaxTrailPoints = 24;

    // ---- Emission ----
    float emissionRate = 20.0f;   // particles per second
    int   maxParticles = 300;
    bool  playing = true;
    /// Particles per world unit the emitter moves (a dust trail behind a walker). 0 = off.
    float emitPerDistance = 0.0f;
    /// Start the scene with the effect already running (a campfire that is burning from the first frame).
    bool  prewarm = false;
    CapPolicy capPolicy = CapPolicy::DropNew;

    // ---- Duration / looping ----
    float duration = 0.0f;        // seconds of emission per cycle (0 = forever)
    bool  loop = true;            // restart the cycle (and refire bursts) at `duration`

    // ---- Bursts: the first one (burstCount / burstTime) plus up to kMaxBursts more ----
    int   burstCount = 0;         // 0 = no burst
    float burstTime  = 0.0f;      // seconds into the cycle when the burst fires
    Burst extraBursts[kMaxBursts];
    int   extraBurstCount = 0;

    // ---- Emission shape ----
    Shape shape = Shape::Point;
    float shapeRadius = 1.0f;
    float shapeAngle  = 25.0f;    // Cone half-angle, degrees
    Vec3  boxSize{1.0f, 1.0f, 1.0f};
    bool  shapeSurfaceOnly = false;   // Sphere / Hemisphere / Box: only the skin

    // ---- Per-particle start values ----
    float startLifetime = 1.5f;
    float startLifetimeRandom = 0.0f;   // +/- seconds
    float startSize = 0.25f;
    float startSizeRandom = 0.0f;       // +/- size units
    Color startColor = Color::White;
    Color startColor2 = Color::White;   // with randomStartColor: each particle picks a colour between the two
    bool  randomStartColor = false;
    bool  randomHue = false;            // each particle gets any colour (a random hue, the start colour's strength)
    Vec3  startVelocity{0.0f, 3.0f, 0.0f};
    /// Start values that change while the effect plays: multipliers for the size, speed and lifetime of the particles being
    /// born, read at how far through the emitter's round (`duration`, or `startCurvePeriod` seconds when it runs forever) it is.
    ParticleCurve startSizeCurve, startSpeedCurve, startLifetimeCurve;
    float startCurvePeriod = 4.0f;
    float velocityRandom = 1.5f;        // +/- added to each velocity component
    float speedRandom = 0.0f;           // +/- scale on the launch speed

    // ---- Appearance ----
    std::string texture;                // empty = a soft round puff
    float startRotation = 0.0f;
    float startRotationRandom = 0.0f;
    float rotationSpeed = 0.0f;
    float rotationSpeedRandom = 0.0f;
    RenderMode renderMode = RenderMode::Billboard;
    float stretchScale = 0.15f;         // length added per unit of speed
    BlendMode blendMode = BlendMode::Alpha;
    SortMode  sortMode = SortMode::Distance;
    /// Picture sheet: `sheetColumns` x `sheetRows` frames in the texture. 1 x 1 = a single picture.
    int   sheetColumns = 1;
    int   sheetRows = 1;
    SheetMode sheetMode = SheetMode::OverLife;
    float sheetCycles = 1.0f;           // OverLife: times the sheet plays over a life. Loop: frames per second / frame count
    bool  sheetBlendFrames = true;      // fade between neighbouring frames
    /// Soft edges where the particle meets a solid surface (needs the depth picture; 0 = off). World units.
    float softDistance = 0.0f;
    /// Lit by the scene's lights (one sample per effect, at its origin). Off = the colour as set.
    bool  lit = false;
    /// Ribbon trail behind each particle: number of points (0 = off, up to kMaxTrailPoints).
    int   trailLength = 0;
    float trailInterval = 0.03f;        // seconds between trail points
    float trailWidth = 0.6f;            // ribbon width, as a fraction of the particle size
    float trailFade = 1.0f;             // 1 = fades to nothing at the tail

    // ---- Over-lifetime ----
    Color endColor = Color::White;
    bool  colorOverLife = false;
    ParticleGradient colorGradient;
    float endSize = 0.0f;
    bool  sizeOverLife = false;
    ParticleCurve sizeCurve;
    bool  fadeOverLife = true;
    bool  speedOverLife = false;
    ParticleCurve speedCurve;
    /// Spin multiplier over life (x the particle's own spin).
    bool  rotationOverLife = false;
    ParticleCurve rotationCurve;
    /// Linear velocity over life: extra drift (units/second, world axes) and radial drift away from the
    /// emitter (negative = inward). `velocityCurve` scales both over life (empty = constant).
    Vec3  linearVelocity{0.0f, 0.0f, 0.0f};
    float radialVelocity = 0.0f;
    ParticleCurve velocityCurve;
    /// A steady push (units/second squared, world axes) scaled over life by `forceCurve`; and the gravity scale over life.
    Vec3  force{0.0f, 0.0f, 0.0f};
    ParticleCurve forceCurve;
    ParticleCurve gravityCurve;

    // ---- Forces ----
    Vec3  gravity{0.0f, -2.0f, 0.0f};
    float gravityModifier = 1.0f;
    float damping = 0.0f;               // drag per second (exponential: the same at any frame rate)
    /// Affected by wind: how much the scene's wind (WindVector(): weather + gusts) carries these particles.
    /// 1 = they drift at the wind's speed, 0 = ignore it. (The wind system reads this field; the one place it is
    /// applied is ParticleSystem::WindAt in ParticleSystem.cpp.)
    float windInfluence = 1.0f;

    // ---- Motion ----
    SimulationSpace simulationSpace = SimulationSpace::World;
    float inheritVelocity = 0.0f;
    float noiseStrength = 0.0f;
    float noiseFrequency = 1.0f;
    float noiseScroll = 0.5f;
    int   noiseOctaves = 1;             // 1..3 layers of finer noise on top
    float orbitSpeed = 0.0f;
    Vec3  orbitAxis{0.0f, 1.0f, 0.0f};
    float vortexStrength = 0.0f;
    float vortexPull = 0.0f;

    // ---- Collision ----
    /// Flat ground at world height `collisionY` (cheap: one compare per particle).
    bool  collision = false;
    float collisionY = 0.0f;
    float bounce = 0.5f;
    float collisionFriction = 0.1f;
    float collisionLifeLoss = 0.0f;
    bool  dieOnCollision = false;       // the particle ends at its first hit (ground or world)
    /// Hit the colliders of the scene (a ray from where the particle was to where it is, through the physics
    /// world). `collisionRaysPerFrame` is the most rays one frame may use; the others are checked next frame.
    bool  worldCollision = false;
    int   collisionRaysPerFrame = 64;
    /// The same for a 2D game: the particle's X and Y against the 2D colliders (box, circle, capsule, edge, polygon); Z is left alone.
    bool  worldCollision2D = false;

    // ---- Sub-emitters: names of CHILD objects that carry their own Particle System ----
    std::string subEmitterBirth;        // a few sparks at the place each particle is born
    std::string subEmitterDeath;        // a puff where each particle ends
    std::string subEmitterCollision;    // a splash where each particle hits something
    int subBirthCount = 1, subDeathCount = 3, subCollisionCount = 3;

    // ---- Speed and size of the work ----
    /// Fewer particles far away: between `lodNear` and `lodFar` metres the emission is scaled down to `lodMinimum`
    /// (0 = no LOD). Fed by the renderer each frame (SetViewDistance).
    float lodNear = 0.0f, lodFar = 0.0f, lodMinimum = 0.25f;
    /// Keep simulating while off screen (true = always; false = pause when the renderer reports it culled).
    bool  simulateOffscreen = true;
    /// Hand the work of a very big pool to the job system above this many live particles (0 = never).
    int   parallelThreshold = 4096;

    std::uint64_t seed = 1234567u;

    int SafeMax() const { return maxParticles < 0 ? 0 : (maxParticles > 100000 ? 100000 : maxParticles); }
    int SafeTrail() const { return trailLength < 0 ? 0 : (trailLength > kMaxTrailPoints ? kMaxTrailPoints : trailLength); }

    void Awake() override;
    void OnPoolReuse() override { if (!m_awake) return; Restart(); m_hasLast = false; playing = m_playOnReuse; }
    void Update(float dt) override;

    void Play()  { playing = true; }
    void Stop()  { playing = false; }
    /// Clear all particles and restart the cycle from zero (with `prewarm`, already running).
    void Restart();
    /// Run the effect forward `seconds` right now (a preview that is already going, a prewarm).
    void Simulate(float seconds, float step = 1.0f / 30.0f);

    // ---- Presets ----
    enum class Preset { Fire, Smoke, Sparks, Explosion, Rain, Snow, Magic, Dust,
                        Campfire, Steam, Leaves, Confetti, HitSpark, MuzzleFlash, Fireflies, Portal, WaterfallMist, Count };
    static const char* PresetName(Preset p);
    /// One plain sentence about what the preset looks like (for menus and tooltips).
    static const char* PresetHint(Preset p);
    void ApplyPreset(Preset which);

    // ---- Reading the pool ----
    int AliveCount() const { return m_n; }
    /// The live particles as copies (slow: for tests, tools and 2D drawing). The renderers read the arrays directly.
    std::vector<Particle> Particles() const;
    Particle Get(int i) const;
    void Set(int i, const Particle& p);
    template <class F> void ForEach(F&& f) const { for (int i = 0; i < m_n; ++i) f(Get(i)); }

    /// Spawn `count` particles now at the emitter.
    void Emit(int count) { EmitAt(transform ? transform->Position() : Vec3::Zero, count, Vec3::Zero); }
    /// Spawn `count` particles now at `where`; `extraVelocity` is added (scaled by inheritVelocity).
    void EmitAt(const Vec3& where, int count, const Vec3& extraVelocity);

    // ---- Scripts: change the look while it runs ----
    void SetRate(float perSecond) { emissionRate = perSecond < 0.0f ? 0.0f : perSecond; }
    void SetColor(const Color& c) { startColor = c; startColor2 = c; endColor = c; colorGradient.Clear(); colorOverLife = false; }

    // ---- Renderer hints ----
    /// The renderer tells the system how far the camera is and whether it was on screen (for LOD and culling).
    void SetViewDistance(float metres) { m_viewDistance = metres; }
    void SetCulled(bool culled) { m_culled = culled; }
    bool Culled() const { return m_culled; }
    float LodFactor() const;
    /// A box around the live particles (valid when AliveCount() > 0).
    Vec3 BoundsMin() const { return m_bmin; }
    Vec3 BoundsMax() const { return m_bmax; }
    /// Memory held by the pool, in bytes.
    std::size_t MemoryBytes() const { return (m_f.capacity() + m_trail.capacity() * 1) * sizeof(float) + m_trailN.capacity(); }
    /// Rays used by the last Update (world collision).
    int LastRays() const { return m_lastRays; }

    // ---- Pool channels (read by the renderers) ----
    enum Ch { PX, PY, PZ, VX, VY, VZ, LIFE, MAXLIFE, SIZE0, SIZE, ROT, ANG, R0, G0, B0, A0, R, G, B, A, RND, OX, OY, OZ, TRT, kChannels };
    const float* Channel(Ch c) const { return m_f.empty() ? nullptr : m_f.data() + (std::size_t)c * m_cap; }
    /// 0 at birth, 1 at death.
    float Age01(int i) const { const float* l = Channel(LIFE); const float* m = Channel(MAXLIFE); return 1.0f - Mathf::Clamp01(l[i] / m[i]); }
    /// The trail points of particle i, newest first (up to SafeTrail()); returns how many.
    int TrailPoints(int i, Vec3* out) const;

private:
    struct Ctx;
    struct Event { int kind; Vec3 pos; Vec3 vel; };
    float& F(Ch c, int i) { return m_f[(std::size_t)c * m_cap + i]; }
    float F(Ch c, int i) const { return m_f[(std::size_t)c * m_cap + i]; }
    void EnsurePool();
    void Tick(float dt, const Vec3* originOverride = nullptr, const Quat* rotOverride = nullptr);
    void StepRange(int begin, int end, float dt, const Ctx& c);
    void SpawnOne(const Vec3& origin, const Vec3& extraVel, float age, float dt, const Ctx& c, bool event);
    int  AllocSlot();
    void Remove(int i);
    void WorldCollide(float dt);
    void FlushEvents();
    ParticleSystem* FindSub(const std::string& name) const;
    Vec3 WindAt(const Vec3& position, const Vec3& emitterOrigin) const;   // the wind hook (windInfluence)
    float Hash01(std::uint64_t k) const;
    void SampleMesh(Vec3& pos, Vec3& normal);
    static float Noise3(float x, float y, float z);
    static float Hash3(int x, int y, int z);
    Vec3 RandUnitVec();

    std::vector<float> m_f;          // kChannels * m_cap floats
    std::vector<float> m_trail;      // m_cap * SafeTrail * 3
    std::vector<unsigned char> m_trailN;
    std::vector<unsigned char> m_hit;   // 1 = hit something this step (collision events)
    int m_cap = 0, m_trailCap = 0, m_n = 0;
    bool m_playOnReuse = true, m_awake = false;
    float m_accum = 0.0f, m_distAccum = 0.0f;
    float m_age = 0.0f;
    bool  m_burstFired = false;
    unsigned m_extraFired = 0;
    Random m_rng;
    std::uint64_t m_emitCount = 0;
    float m_time = 0.0f;
    bool  m_hasLast = false;
    Vec3  m_lastOrigin{0.0f, 0.0f, 0.0f};
    Quat  m_lastRot = Quat::Identity;
    Vec3  m_emitterVel{0.0f, 0.0f, 0.0f};
    Vec3  m_bmin{0, 0, 0}, m_bmax{0, 0, 0};
    float m_viewDistance = 0.0f;
    bool  m_culled = false;
    int   m_lastRays = 0, m_rayCursor = 0;
    std::vector<Event> m_events;
    std::vector<float> m_meshAreas; const void* m_meshKey = nullptr; std::size_t m_meshTris = 0;
    int m_depth = 0;                 // > 0 while ticking (guards re-entry from sub-emitters)
};

} // namespace okay
