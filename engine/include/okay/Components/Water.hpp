#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/MeshNormals.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Environment/Wind.hpp"
#include "okay/Environment/WaterWaves.hpp"
#include "okay/Environment/WaterShading.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/Camera.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace okay {

/// A real, living water surface — not just a flat blue plane. It builds a
/// subdivided mesh and animates it every frame with crossing sine waves (so the
/// surface actually rolls), with a reflective, specular, semi-transparent water
/// material: the sky reflects in it, the sun glints off the crests, and the UVs
/// scroll like a current. Drop it on an object at your water level and the whole
/// lake/ocean comes alive. Pairs naturally with Terrain / VoxelTerrain.
class Water : public Behaviour {
public:
    float size       = 100.0f;  ///< plane edge length (world units), centred on the object
    int   resolution = 48;      ///< grid subdivisions per side (more = smoother waves)
    float waveHeight = 0.35f;    ///< crest-to-trough amplitude
    float waveLength = 9.0f;     ///< distance between crests (world units)
    float waveSpeed  = 1.1f;     ///< how fast the waves travel
    Color color      = Color::FromBytes(40, 110, 165, 165);  ///< tint (alpha = opacity)
    float opacity    = 0.72f;    ///< 0 clear .. 1 opaque (GPU renderers blend by this)
    float reflectivity = 0.55f;  ///< how much sky reflects in the surface
    float specular   = 0.85f;    ///< sun-glint strength
    float shininess  = 80.0f;    ///< glint tightness
    Vec2  flow       = {0.025f, 0.018f};  ///< surface UV scroll (a current)
    std::string texture;         ///< optional water/normal-ripple texture
    /// How deep the volume is, in metres straight down from the waved surface.
    /// 0 keeps the old visual plane: nothing swims and nothing floats.
    float depth = 0.0f;
    /// Mass per cubic metre in the same units as Rigidbody3D::mass. A 1 m cube
    /// of mass 1 is neutral; lighter floats, heavier sinks.
    float fluidDensity = 1.0f;
    /// How fast a submerged prop's velocity fades (1/s, scaled by how much of it is under).
    float linearDrag = 4.0f;

    /// What the waves look like and how they move.
    ///   Classic - the original crossing sine waves (kept so old scenes look the same).
    ///   Ocean   - a few travelling waves with sharp crests and long swells (Environment/WaterWaves.hpp).
    ///   Calm    - the same waves, low and gentle: a lake.
    /// Ocean and Calm have an exact height, slope and flow at every point (SurfaceAt) that the boat and the
    /// floating objects use, and the drawn mesh is built from the very same formula.
    enum class WaveStyle { Classic, Ocean, Calm };
    WaveStyle style = WaveStyle::Classic;
    float waveDirection = 0.0f;   ///< degrees from +X towards +Z: which way the main swell runs (Ocean and Calm)
    float waveSteepness = 0.5f;   ///< 0 round, smooth .. 1 sharp crests (Ocean; Calm uses a third of it)
    int   waveCount = 4;          ///< how many waves are added together, 1 to 8 (Ocean; Calm uses at most 2)
    int   waveSeed = 7;           ///< the same seed always gives the same sea
    Vec2  current{0.0f, 0.0f};    ///< a steady stream in the water, metres per second along the object's X and Z

    // ---- How it LOOKS (docs/water_visuals.md). Each is off or neutral in an old scene, so old scenes look the same. ----
    /// Sea state shortcut: 0 Custom (your own wave numbers), 1 Calm, 2 Light, 3 Rough, 4 Storm. SetSeaState() writes the
    /// wave numbers; this only remembers which one was picked.
    int   seaState = 0;
    bool  realisticLook = false;  ///< depth colour, foam, crest glow and ripples, painted per vertex (every renderer shows it)
    Color shallowColor = Color::FromBytes(70, 190, 185);   ///< colour of shallow water (turquoise)
    Color deepColor    = Color::FromBytes(8, 42, 100);     ///< colour of deep water (blue)
    float clarity      = 6.0f;    ///< metres of depth before the water looks deep (small = murky)
    float assumedDepth = 12.0f;   ///< depth used where there is no Terrain underneath and Depth is 0
    float foamAmount   = 0.5f;    ///< white caps on crests and steep waves, 0 none .. 1 a gale
    float shoreFoamWidth = 1.2f;  ///< metres of foam where the water meets the ground (0 = none)
    Color foamColor    = Color::FromBytes(240, 247, 252);
    float scatter      = 0.4f;    ///< green-blue glow in crests when the sun is behind them
    float ripples      = 0.0f;    ///< small ripples that never stop, even on a calm lake (0 none .. 1 strong)
    float rippleSize   = 1.0f;    ///< size of the ripples (metres per feature, 1 = normal)
    bool  lod          = false;   ///< rings of squares: fine near the camera, coarse far away (Detail = cells per ring side)
    int   lodRings     = 4;       ///< how many rings (each twice as coarse as the one inside it)
    bool  infinite     = false;   ///< with rings: the sea follows the camera past Size (no edge)
    bool  interactive  = false;   ///< things that fall in or move through make rings and splashes
    bool  underwaterEffect = false; ///< blue-green fog when the camera is below the surface
    bool  bubbles      = false;   ///< bubbles rise around the camera under the surface

    /// Everything about the surface above or below one world point.
    struct Surface {
        float height = 0.0f;                 ///< world height of the surface at that x/z
        float floorY = -1.0e9f;              ///< world height of the bottom of the volume (very low when depth is 0)
        Vec3  normal{0.0f, 1.0f, 0.0f};      ///< unit surface normal (world)
        Vec3  velocity{0.0f, 0.0f, 0.0f};    ///< how fast the water itself moves there (world, m/s): waves plus current
    };

    /// Where a swimmer is relative to the water volumes in the scene.
    struct Probe {
        bool  inWater = false;     ///< the body overlaps a volume
        float surfaceY = 0.0f;     ///< world height of the surface at the feet
        float feetDepth = 0.0f;    ///< surfaceY - feet.y (negative when the feet are above it)
    };

    void Start() override { Apply(); }

    void Update(float dt) override {
        // The scene's wind (weather) quickens the swell and adds a wave running
        // downwind; no wind leaves the surface exactly as authored.
        const Vec3 wind = WindVector();
        const float ws = std::sqrt(wind.x * wind.x + wind.z * wind.z);
        m_windStrength = ws;
        if (ws > 1e-4f) { m_windDir = {wind.x / ws, wind.z / ws}; }
        m_time += dt * waveSpeed * (1.0f + 0.1f * ws);
        if (dt > 0.0f) UpdateLook(dt);
        Animate();
        if (dt > 0.0f && depth > 0.0f) BuoyBodies();
    }

    /// World height of the waved surface at a world x/z, and the floor of the
    /// volume `depth` metres below it. False when `world` is outside the square
    /// (grown by `margin` metres, so a body centred just past the edge still counts).
    bool Column(const Vec3& world, float& surfaceY, float& floorY, float margin = 0.0f) const {
        if (!transform || depth <= 0.0f || size <= 0.0f) return false;
        const Vec3 local = transform->InverseTransformPoint(world);
        const float half = size * 0.5f + std::max(0.0f, margin);
        if (std::fabs(local.x) > half || std::fabs(local.z) > half) return false;
        const Vec3 surf = transform->TransformPoint({local.x, WaveAt(local.x, local.z), local.z});
        surfaceY = surf.y;
        floorY = surfaceY - depth;
        return true;
    }

    /// The surface at a world point: height, slope and flow. False when the point is outside the square (grown by
    /// `margin`). Works for depth 0 too (a flat sheet with no bottom), so anything can float on it.
    bool SurfaceAt(const Vec3& world, Surface& out, float margin = 0.0f) const {
        if (!transform || size <= 0.0f) return false;
        const Vec3 local = transform->InverseTransformPoint(world);
        const float half = size * 0.5f + std::max(0.0f, margin);
        if (std::fabs(local.x) > half || std::fabs(local.z) > half) return false;
        const waves::Sample w = SampleLocal(local.x, local.z);
        const Vec3 p = transform->TransformPoint({local.x, w.height, local.z});
        out.height = p.y;
        out.floorY = depth > 0.0f ? p.y - depth : -1.0e9f;
        Vec3 n = transform->TransformDirection({w.nx, w.ny, w.nz});
        const float nl = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        out.normal = nl > 1e-6f ? Vec3{n.x / nl, n.y / nl, n.z / nl} : Vec3{0.0f, 1.0f, 0.0f};
        out.velocity = transform->TransformDirection({w.vx + current.x, w.vy, w.vz + current.y});
        return true;
    }
    /// The best water at a world point among `volumes` (the highest surface wins).
    static bool SampleIn(const std::vector<Water*>& volumes, const Vec3& world, Surface& out, float margin = 0.0f) {
        bool got = false;
        for (Water* w : volumes) {
            if (!w || !w->enabled || !w->gameObject || !w->gameObject->active) continue;
            Surface s;
            if (!w->SurfaceAt(world, s, margin)) continue;
            if (!got || s.height > out.height) { out = s; got = true; }
        }
        return got;
    }
    /// Water time in seconds (waveSpeed already applied). Set it to put the waves at an exact moment (tests, replays).
    float WaveTime() const { return m_time; }
    void  SetWaveTime(float t) { m_time = t; }

    /// Sea state: 1 Calm, 2 Light, 3 Rough, 4 Storm (0 leaves your numbers alone). Writes the wave height, length,
    /// steepness, count, speed and foam, and turns on the travelling-wave style.
    void SetSeaState(int s) {
        seaState = s < 0 ? 0 : (s > 4 ? 4 : s);
        if (seaState == 0) return;
        const watershade::SeaParams p = watershade::SeaParamsFor((watershade::SeaState)(seaState - 1));
        style = WaveStyle::Ocean;
        waveHeight = p.height; waveLength = p.length; waveSteepness = p.steepness; waveCount = p.count;
        waveSpeed = p.speed; foamAmount = p.foam;
    }
    /// Ready-made waters: 1 Calm Lake, 2 Pond, 3 Ocean, 4 Stormy Sea, 5 River, 6 Swamp, 7 Tropical, 8 Ice-cold.
    /// Sets the waves and the look; size, depth and the object's place are not touched.
    static const char* PresetName(int i) {
        static const char* k[] = {"Calm Lake", "Pond", "Ocean", "Stormy Sea", "River", "Swamp", "Tropical", "Ice-cold"};
        return (i >= 1 && i <= 8) ? k[i - 1] : "Custom";
    }
    void ApplyPreset(int preset) {
        if (preset < 1 || preset > 8) return;
        auto C = [](int r, int g, int b) { return Color::FromBytes((std::uint8_t)r, (std::uint8_t)g, (std::uint8_t)b); };
        realisticLook = true; seaState = 0; style = WaveStyle::Calm; waveDirection = 20.0f; waveSeed = 7;
        current = {0.0f, 0.0f}; flow = {0.02f, 0.012f}; lod = false; infinite = false; interactive = true;
        underwaterEffect = true; bubbles = false; foamColor = C(240, 247, 252); shoreFoamWidth = 1.2f;
        rippleSize = 1.0f; scatter = 0.4f; specular = 0.85f; shininess = 80.0f; opacity = 0.78f;
        switch (preset) {
        case 1: // Calm Lake
            waveHeight = 0.10f; waveLength = 12.0f; waveSteepness = 0.2f; waveCount = 2; waveSpeed = 0.8f;
            shallowColor = C(70, 170, 175); deepColor = C(14, 66, 108); clarity = 7.0f; foamAmount = 0.1f;
            ripples = 0.6f; reflectivity = 0.72f; break;
        case 2: // Pond
            waveHeight = 0.04f; waveLength = 4.0f; waveSteepness = 0.1f; waveCount = 2; waveSpeed = 0.7f;
            shallowColor = C(85, 135, 105); deepColor = C(30, 70, 62); clarity = 2.5f; foamAmount = 0.05f;
            ripples = 0.8f; rippleSize = 0.6f; reflectivity = 0.55f; shoreFoamWidth = 0.5f; break;
        case 3: // Ocean
            SetSeaState(2); style = WaveStyle::Ocean; waveSteepness = 0.5f;
            shallowColor = C(40, 170, 190); deepColor = C(5, 38, 96); clarity = 10.0f; ripples = 0.5f;
            reflectivity = 0.6f; lod = true; infinite = true; break;
        case 4: // Stormy Sea
            SetSeaState(4);
            shallowColor = C(70, 105, 112); deepColor = C(14, 38, 48); clarity = 5.0f; ripples = 0.7f;
            reflectivity = 0.45f; specular = 0.5f; scatter = 0.2f; lod = true; infinite = true; break;
        case 5: // River
            waveHeight = 0.12f; waveLength = 5.0f; waveSteepness = 0.25f; waveCount = 3; waveSpeed = 1.4f;
            shallowColor = C(95, 150, 140); deepColor = C(30, 78, 88); clarity = 3.0f; foamAmount = 0.2f;
            ripples = 0.9f; reflectivity = 0.5f; current = {2.5f, 0.0f}; flow = {0.06f, 0.0f}; waveDirection = 0.0f; break;
        case 6: // Swamp
            waveHeight = 0.03f; waveLength = 3.0f; waveSteepness = 0.05f; waveCount = 2; waveSpeed = 0.5f;
            shallowColor = C(88, 105, 52); deepColor = C(26, 40, 22); clarity = 1.2f; foamAmount = 0.05f;
            ripples = 0.3f; reflectivity = 0.3f; opacity = 0.92f; specular = 0.3f; scatter = 0.1f; shoreFoamWidth = 0.4f; break;
        case 7: // Tropical
            SetSeaState(2); waveHeight = 0.3f; waveSteepness = 0.3f;
            shallowColor = C(60, 225, 205); deepColor = C(8, 92, 150); clarity = 9.0f; scatter = 0.7f; ripples = 0.5f;
            reflectivity = 0.55f; opacity = 0.65f; lod = true; infinite = true; break;
        case 8: // Ice-cold
            waveHeight = 0.05f; waveLength = 10.0f; waveSteepness = 0.15f; waveCount = 2; waveSpeed = 0.6f;
            shallowColor = C(160, 215, 228); deepColor = C(36, 96, 140); clarity = 12.0f; foamAmount = 0.25f;
            ripples = 0.4f; reflectivity = 0.8f; specular = 1.0f; break;
        }
        color = Color{shallowColor.r, shallowColor.g, shallowColor.b, opacity};
        m_fieldOk = false;
    }

    /// The current as a world vector (metres per second). The boat and floating things read this; the water does not push
    /// anything itself here.
    Vec3 FlowVector() const {
        if (!transform) return {current.x, 0.0f, current.y};
        return transform->TransformDirection({current.x, 0.0f, current.y});
    }
    /// A ring spreads from a world point (strength 0..1+). Used by splashes; a script can call it too.
    void AddRipple(const Vec3& world, float strength) {
        if (!transform) return;
        const Vec3 l = transform->InverseTransformPoint(world);
        if (m_rings.size() >= 64) m_rings.erase(m_rings.begin());
        m_rings.push_back({l.x, l.z, m_time, strength});
    }
    /// Wake hook: tell the water a thing at `world` moves with `velocity` for `dt` seconds. It lays rings along the path (one
    /// per 0.5 m travelled, bigger when faster), so a boat leaves a trail. Safe to call every frame at any frame rate.
    void AddWake(const Vec3& world, const Vec3& velocity, float dt, int id = 0) {
        const float sp = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
        float& acc = m_wakeAcc[id];
        acc += sp * (dt > 0.0f ? dt : 0.0f);
        int guard = 0;
        while (acc >= 0.5f && guard++ < 8) {
            acc -= 0.5f;
            AddRipple(world, std::min(1.0f, 0.15f + sp * 0.08f));
        }
    }
    int  RingCount() const { return (int)m_rings.size(); }
    /// Rings lift the surface by this many metres at a world point right now (for tests and for floating things).
    float RingLift(const Vec3& world) const {
        if (!transform) return 0.0f;
        const Vec3 l = transform->InverseTransformPoint(world);
        float h = 0.0f;
        for (const watershade::Ring& r : m_rings) h += watershade::RingHeight(r, l.x, l.z, m_time);
        return h;
    }
    /// Is the camera below the surface right now (only meaningful inside the plane)?
    bool CameraIsUnder() const { return m_underwater; }
    /// Vertices and triangles of the last mesh built (for the benchmark and the tests).
    int LastVertexCount() const { return m_lastVerts; }
    int LastTriangleCount() const { return m_lastTris; }

    /// The deepest water the body at `feet` (height `bodyHeight`) is standing in.
    static Probe ProbeAt(const Scene* scene, const Vec3& feet, float bodyHeight) {
        if (!scene) return Probe{};
        return ProbeIn(scene->FindObjectsOfType<Water>(), feet, bodyHeight);
    }
    /// ProbeAt over a list of volumes the caller already has (a controller keeps the scene's list between
    /// frames instead of searching every object every frame).
    static Probe ProbeIn(const std::vector<Water*>& volumes, const Vec3& feet, float bodyHeight) {
        Probe best;
        const float top = feet.y + std::max(0.2f, bodyHeight);
        for (Water* w : volumes) {
            if (!w || !w->enabled || !w->gameObject || !w->gameObject->active) continue;
            float surf = 0.0f, floorY = 0.0f;
            if (!w->Column(feet, surf, floorY, 0.35f)) continue;
            if (top < floorY || feet.y > surf + 0.15f) continue;
            const float d = surf - feet.y;
            if (!best.inWater || d > best.feetDepth) {
                best.inWater = true;
                best.surfaceY = surf;
                best.feetDepth = d;
            }
        }
        return best;
    }

    /// (Re)build the water mesh + material into the sibling MeshRenderer. Call after
    /// changing size/resolution (the editor does this from the inspector).
    void Apply() {
        if (!gameObject) return;
        BuildBase();
        auto* mr = gameObject->GetComponent<MeshRenderer>();
        if (!mr) mr = gameObject->AddComponent<MeshRenderer>();
        // Water material: reflective + glossy + translucent, with a flowing surface.
        mr->color = {color.r, color.g, color.b, opacity};
        mr->reflectivity = reflectivity;
        mr->specular = specular;
        mr->shininess = shininess;
        mr->doubleSided = true;       // seen from above and below the surface
        mr->groundShadow = false;     // water doesn't cast a blob shadow
        mr->uvScroll = flow;          // scroll the texture like a current
        mr->texture = texture;
        if (realisticLook) {
            // Vertex colours carry the whole look (depth colour, foam, glow), so the material colour is neutral.
            mr->color = {1.0f, 1.0f, 1.0f, opacity};
            mr->normalMap = ripples > 0.0f ? "proc:water_normal" : std::string();
            mr->normalStrength = 0.25f + 0.9f * ripples;
            const float tile = RippleTile();
            mr->uvScroll = {flow.x + current.x / tile, flow.y + current.y / tile};
        }
        Animate();                    // seat the first frame's waves
    }

private:
    std::vector<Vec3> m_base;   ///< flat grid positions (waves are added on top)
    int m_dim = 0;
    float m_time = 0.0f;
    mutable waves::Field m_field;
    mutable bool m_fieldOk = false;
    float m_windStrength = 0.0f;   ///< horizontal wind speed last Update
    Vec2  m_windDir{1.0f, 0.0f};

    int Dim() const { return resolution + 1; }

    void BuildBase() {
        m_dim = Dim();
        const float cell = size / resolution, half = size * 0.5f;
        m_base.clear();
        m_base.reserve((std::size_t)m_dim * m_dim);
        for (int z = 0; z < m_dim; ++z)
            for (int x = 0; x < m_dim; ++x)
                m_base.push_back({x * cell - half, 0.0f, z * cell - half});
    }

    // Ocean and Calm use the travelling-wave field; Classic keeps the original sines below.
    waves::Params FieldParams() const {
        waves::Params p;
        const bool calm = style == WaveStyle::Calm;
        p.count = calm ? std::min(2, std::max(1, waveCount)) : waveCount;
        p.height = calm ? waveHeight * 0.25f : waveHeight;
        p.length = calm ? waveLength * 1.6f : waveLength;
        p.directionDeg = waveDirection;
        p.steepness = calm ? waveSteepness * 0.3f : waveSteepness;
        p.seed = (std::uint32_t)waveSeed;
        return p;
    }
    const waves::Field& FieldNow() const {
        const waves::Params p = FieldParams();
        if (!m_fieldOk || p != m_field.params()) { m_field.Build(p); m_fieldOk = true; }
        return m_field;
    }
    waves::Sample SampleLocal(float x, float z) const {
        if (style == WaveStyle::Classic) {
            waves::Sample s;
            s.height = WaveAt(x, z);
            // Slope by differences, so the old waves also give a normal (and no flow).
            const float e = 0.1f;
            const float dx = (WaveAt(x + e, z) - WaveAt(x - e, z)) / (2.0f * e);
            const float dz = (WaveAt(x, z + e) - WaveAt(x, z - e)) / (2.0f * e);
            const float l = std::sqrt(dx * dx + 1.0f + dz * dz);
            s.nx = -dx / l; s.ny = 1.0f / l; s.nz = -dz / l;
            return s;
        }
        return FieldNow().At(x, z, m_time);
    }

    float WaveAt(float x, float z) const {
        if (style != WaveStyle::Classic) return FieldNow().Height(x, z, m_time);
        float k = 6.2831853f / (waveLength > 0.01f ? waveLength : 0.01f);
        // Crossing swells at angles + a finer, faster ripple on top give a natural,
        // non-repeating, lively surface rather than one rolling sine.
        float a = std::sin(x * k + m_time);
        float b = std::sin((x * 0.6f + z * 0.8f) * k + m_time * 1.3f);
        float c = std::sin((x * 1.7f - z * 1.3f) * k * 2.1f + m_time * 2.2f) * 0.35f;
        float w = 0.0f;
        if (m_windStrength > 1e-4f) {   // a wave running downwind, up to half the crest height at a gale
            const float along = x * m_windDir.x + z * m_windDir.y;
            w = std::sin(along * k * 1.3f - m_time * 1.8f) * std::fmin(m_windStrength * 0.08f, 1.2f);
        }
        return (a + b + c + w) * 0.42f * waveHeight;
    }

    void Animate() {
        if (!gameObject) return;
        auto* mr = gameObject->GetComponent<MeshRenderer>();
        if (!mr) return;
        if (realisticLook || lod) { AnimateRich(mr); return; }
        if ((int)m_base.size() != m_dim * m_dim || m_dim < 2) BuildBase();

        Mesh m;
        m.name = "Water";
        m.vertices.reserve(m_base.size());
        m.uvs.reserve(m_base.size());
        const int dim = m_dim;
        for (int z = 0; z < dim; ++z)
            for (int x = 0; x < dim; ++x) {
                const Vec3& b = m_base[(std::size_t)z * dim + x];
                if (style == WaveStyle::Classic) m.vertices.push_back({b.x, WaveAt(b.x, b.z), b.z});
                else { float px, py, pz; FieldNow().Displace(b.x, b.z, m_time, px, py, pz); m.vertices.push_back({px, py, pz}); }
                m.uvs.push_back({(float)x / (dim - 1), (float)z / (dim - 1)});
            }
        m.triangles.reserve((std::size_t)resolution * resolution * 6);
        for (int z = 0; z < resolution; ++z)
            for (int x = 0; x < resolution; ++x) {
                int i00 = z * dim + x, i10 = z * dim + x + 1;
                int i01 = (z + 1) * dim + x, i11 = (z + 1) * dim + x + 1;
                m.triangles.push_back(i00); m.triangles.push_back(i01); m.triangles.push_back(i10);
                m.triangles.push_back(i10); m.triangles.push_back(i01); m.triangles.push_back(i11);
            }
        // Smooth, rolling swell. Grid vertices never share a position, so skip the welding walk (same normals, much faster).
        if (meshnormals::GridCellsAreDistinct(size / (float)std::max(1, resolution))) meshnormals::SmoothNoWeld(m);
        else m.ComputeSmoothNormals();
        mr->mesh = std::move(m);
    }

    // ===== the look (docs/water_visuals.md) ===========================================================================
    std::vector<watershade::Ring> m_rings;          ///< splash rings alive (water-local)
    std::unordered_map<int, float> m_wakeAcc;       ///< distance travelled since the last wake ring, per emitter id
    struct BodyState { Rigidbody3D* rb = nullptr; bool in = false; float acc = 0.0f; };
    bool m_underwater = false;
    bool m_fogApplied = false;
    bool m_savedFog = false; int m_savedFogMode = 0; Color m_savedFogColor; float m_savedFogDensity = 0.02f;
    GameObject* m_splashFx = nullptr;
    GameObject* m_bubbleFx = nullptr;
    float m_bubbleAcc = 0.0f;
    int m_lastVerts = 0, m_lastTris = 0;
    // The ring grid is rebuilt only when its centre or settings change.
    watershade::RingGrid m_grid;
    float m_gridCx = 1e30f, m_gridCz = 1e30f;
    int m_gridKey = -1;

    float RippleTile() const { return 8.0f * (rippleSize > 0.05f ? rippleSize : 0.05f); }

    static watershade::RGB ToRGB(const Color& c) { return {c.r, c.g, c.b}; }

    void UpdateLook(float dt) {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc) return;
        // forget rings that have faded
        m_rings.erase(std::remove_if(m_rings.begin(), m_rings.end(),
                      [&](const watershade::Ring& r) { return !watershade::RingAlive(r, m_time); }), m_rings.end());
        if (interactive) UpdateBodies(sc, dt);
        UpdateUnderwater(sc, dt);
    }

    GameObject* MakeFx(const char* name) {
        Scene* sc = gameObject->scene();
        GameObject* g = sc->CreateGameObject(name);
        g->transient = true;
        return g;
    }

    void Splash(const Vec3& at, float strength) {
        if (!m_splashFx) {
            m_splashFx = MakeFx("Water Splash");
            auto* ps = m_splashFx->AddComponent<ParticleSystem>();
            ps->emissionRate = 0.0f; ps->maxParticles = 240; ps->shape = ParticleSystem::Shape::Circle; ps->shapeRadius = 0.2f;
            ps->startLifetime = 0.8f; ps->startLifetimeRandom = 0.25f; ps->startSize = 0.07f; ps->startSizeRandom = 0.03f;
            ps->startVelocity = {0.0f, 3.2f, 0.0f}; ps->velocityRandom = 1.3f;
            ps->startColor = {0.88f, 0.94f, 1.0f, 0.9f}; ps->endColor = {0.88f, 0.94f, 1.0f, 0.0f}; ps->colorOverLife = true;
            ps->gravity = {0.0f, -9.0f, 0.0f}; ps->simulationSpace = ParticleSystem::SimulationSpace::World;
        }
        if (!m_splashFx) return;
        m_splashFx->transform->SetPosition(at);
        if (auto* ps = m_splashFx->GetComponent<ParticleSystem>()) ps->Emit((int)(6.0f + 22.0f * std::min(1.0f, strength)));
    }

    void UpdateBodies(Scene* sc, float dt) {
        std::vector<BodyState> next;
        for (Rigidbody3D* rb : sc->FindObjectsOfType<Rigidbody3D>()) {
            if (!rb || !rb->enabled || !rb->gameObject || !rb->transform || rb->gameObject == gameObject) continue;
            if (rb->gameObject == m_splashFx || rb->gameObject == m_bubbleFx) continue;
            if (rb->bodyType == Rigidbody3D::BodyType::Static) continue;
            BodyState st;
            st.rb = rb;
            for (const BodyState& o : m_prevBodies) if (o.rb == rb) { st = o; break; }
            Vec3 mn, mx; bool have = false;
            for (Collider3D* col : rb->gameObject->GetComponents<Collider3D>()) {
                if (!col || col->isTrigger) continue;
                col->WorldAABB(mn, mx); have = true; break;
            }
            const Vec3 p = rb->transform->Position();
            if (!have) { mn = p - Vec3{0.3f, 0.3f, 0.3f}; mx = p + Vec3{0.3f, 0.3f, 0.3f}; }
            const Vec3 c{(mn.x + mx.x) * 0.5f, mn.y, (mn.z + mx.z) * 0.5f};
            Surface s;
            bool in = false;
            if (SurfaceAt(c, s, 0.5f)) in = c.y < s.height;
            const Vec3 v = rb->velocity;
            const float hsp = std::sqrt(v.x * v.x + v.z * v.z);
            const Vec3 at{c.x, s.height, c.z};
            if (in && !st.in && (std::fabs(v.y) > 0.8f || hsp > 1.5f)) {
                const float str = std::min(1.2f, 0.25f + std::fabs(v.y) * 0.12f + hsp * 0.05f);
                AddRipple(at, str);
                Splash(at, str);
            } else if (in && c.y > s.height - (mx.y - mn.y) && hsp > 0.4f) {
                AddWake(at, v, dt, (int)(std::intptr_t)rb);
            }
            st.in = in;
            next.push_back(st);
        }
        m_prevBodies = next;
    }
    std::vector<BodyState> m_prevBodies;

    void UpdateUnderwater(Scene* sc, float dt) {
        Camera* cam = sc->mainCamera;
        bool under = false;
        Surface s;
        Vec3 cp{0, 0, 0};
        if (cam && cam->transform) {
            cp = cam->transform->Position();
            if (SurfaceAt(cp, s, 0.0f)) under = cp.y < s.height;
        }
        m_underwater = under;
        auto& rs = sc->renderSettings;
        if (under && underwaterEffect) {
            if (!m_fogApplied) {
                m_savedFog = rs.fog; m_savedFogMode = rs.fogMode; m_savedFogColor = rs.fogColor; m_savedFogDensity = rs.fogDensity;
                m_fogApplied = true;
            }
            const watershade::UnderwaterFog f = watershade::UnderwaterFogFor(ToRGB(shallowColor), ToRGB(deepColor), clarity, s.height - cp.y);
            rs.fog = true; rs.fogMode = Scene::RenderSettings::FogExponential;
            rs.fogColor = Color{f.color.r, f.color.g, f.color.b, 1.0f}; rs.fogDensity = f.density;
        } else if (m_fogApplied) {
            rs.fog = m_savedFog; rs.fogMode = m_savedFogMode; rs.fogColor = m_savedFogColor; rs.fogDensity = m_savedFogDensity;
            m_fogApplied = false;
        }
        if (bubbles && under) {
            if (!m_bubbleFx) {
                m_bubbleFx = MakeFx("Water Bubbles");
                auto* ps = m_bubbleFx->AddComponent<ParticleSystem>();
                ps->emissionRate = 0.0f; ps->maxParticles = 160; ps->shape = ParticleSystem::Shape::Sphere; ps->shapeRadius = 2.5f;
                ps->startLifetime = 2.2f; ps->startLifetimeRandom = 0.6f; ps->startSize = 0.05f; ps->startSizeRandom = 0.03f;
                ps->startVelocity = {0.0f, 0.5f, 0.0f}; ps->velocityRandom = 0.15f;
                ps->startColor = {0.85f, 0.95f, 1.0f, 0.55f}; ps->endColor = {0.85f, 0.95f, 1.0f, 0.0f}; ps->colorOverLife = true;
                ps->gravity = {0.0f, 0.6f, 0.0f}; ps->simulationSpace = ParticleSystem::SimulationSpace::World;
            }
            if (m_bubbleFx) {
                m_bubbleFx->transform->SetPosition(cp);
                m_bubbleAcc += 28.0f * dt;                 // 28 bubbles a second, whatever the frame rate
                const int n = (int)m_bubbleAcc; m_bubbleAcc -= (float)n;
                if (auto* ps = m_bubbleFx->GetComponent<ParticleSystem>()) ps->Emit(std::min(n, 40));
            }
        }
    }

    /// Height of the ground under a water-local x/z in world space, or false when no Terrain is below.
    bool GroundWorldY(const std::vector<Terrain*>& terrains, const Vec3& worldXZ, float& y) const {
        for (Terrain* t : terrains) {
            if (!t || !t->transform || !t->enabled) continue;
            const Vec3 l = t->transform->InverseTransformPoint(worldXZ);
            const float h = t->size * 0.5f;
            if (std::fabs(l.x) > h || std::fabs(l.z) > h) continue;
            y = t->transform->TransformPoint({l.x, t->SampleHeight(l.x, l.z), l.z}).y;
            return true;
        }
        return false;
    }

    void AnimateRich(MeshRenderer* mr) {
        using namespace watershade;
        Scene* sc = gameObject->scene();
        const float lim = size * 0.5f;
        // --- the grid ---
        const int n = std::max(4, resolution);
        if (lod) {
            const int levels = std::min(8, std::max(1, lodRings));
            const int nn = std::max(8, (n + 3) / 4 * 4);
            const float cTop = size / (float)nn;
            const float c0 = cTop / (float)(1 << (levels - 1));
            Vec3 cam{0, 0, 0};
            if (sc && sc->mainCamera && sc->mainCamera->transform && transform) cam = transform->InverseTransformPoint(sc->mainCamera->transform->Position());
            float cx = cam.x, cz = cam.z;
            if (!infinite) { cx = std::min(lim, std::max(-lim, cx)); cz = std::min(lim, std::max(-lim, cz)); }
            cx = std::floor(cx / cTop + 0.5f) * cTop; cz = std::floor(cz / cTop + 0.5f) * cTop;
            const int key = nn * 100 + levels * 10 + (infinite ? 1 : 0) + (int)(size * 10.0f) * 1000;
            if (key != m_gridKey || cx != m_gridCx || cz != m_gridCz) {
                m_grid = BuildRingGrid(nn, levels, c0, cx, cz, lim, !infinite);
                m_gridKey = key; m_gridCx = cx; m_gridCz = cz;
            }
        } else {
            const int key = -n - (int)(size * 10.0f) * 1000;
            if (key != m_gridKey) {
                m_grid = RingGrid();
                const float cell = size / (float)n;
                for (int z = 0; z <= n; ++z)
                    for (int x = 0; x <= n; ++x) {
                        m_grid.x.push_back(x * cell - lim); m_grid.z.push_back(z * cell - lim);
                        m_grid.stitchA.push_back(-1); m_grid.stitchB.push_back(-1); m_grid.level.push_back(0);
                    }
                for (int z = 0; z < n; ++z)
                    for (int x = 0; x < n; ++x) {
                        const int a = z * (n + 1) + x, b = a + 1, c = a + n + 1, d = c + 1;
                        m_grid.tri.insert(m_grid.tri.end(), {a, c, b, b, c, d});
                    }
                m_gridKey = key; m_gridCx = m_gridCz = 1e30f;
            }
        }
        const RingGrid& g = m_grid;
        const int nv = g.vertexCount();

        // --- what the shading needs this frame ---
        std::vector<Terrain*> terrains;
        if (sc) terrains = sc->FindObjectsOfType<Terrain>();
        Vec3 sunDir{0.3f, 0.8f, 0.2f};                       // direction TOWARDS the sun
        if (sc) for (Light* l : sc->FindObjectsOfType<Light>())
            if (l && l->enabled && l->gameObject && l->gameObject->transform && l->type == Light::Type::Directional) {
                const Vec3 f = l->gameObject->transform->Forward();
                sunDir = {-f.x, -f.y, -f.z}; break;
            }
        Vec3 camW{0, 100, 0};
        if (sc && sc->mainCamera && sc->mainCamera->transform) camW = sc->mainCamera->transform->Position();
        const float maxAmp = style == WaveStyle::Classic ? std::max(0.01f, waveHeight * 1.2f)
                                                         : std::max(0.01f, FieldNow().MaxAmplitude());
        const float tile = RippleTile();
        const RGB shallow = ToRGB(shallowColor), deep = ToRGB(deepColor), foamRGB = ToRGB(foamColor);
        const Mat4 toWorld = transform ? transform->LocalToWorldMatrix() : Mat4::Identity();
        const bool haveRings = !m_rings.empty();

        Mesh m;
        m.name = "Water";
        m.vertices.resize(nv); m.normals.resize(nv); m.uvs.resize(nv); m.vertexColors.resize(nv);
        std::vector<Vec3> nrm(nv);
        for (int i = 0; i < nv; ++i) {
            const float bx = g.x[i], bz = g.z[i];
            float px = bx, py = 0.0f, pz = bz;
            waves::Sample w;
            if (style == WaveStyle::Classic) { w = SampleLocal(bx, bz); py = w.height; }
            else { FieldNow().Displace(bx, bz, m_time, px, py, pz); w = FieldNow().At(px, pz, m_time); }
            const float waveY = py;
            // ripples: tiny height change, and (more important) a slope that bends the normal
            float rsx = 0.0f, rsz = 0.0f;
            if (ripples > 0.0f) {
                py += RippleHeight(px, pz, m_time, ripples, rippleSize, current.x, current.y);
                RippleSlope(px, pz, m_time, ripples, rippleSize, current.x, current.y, rsx, rsz);
            }
            if (haveRings) for (const Ring& r : m_rings) py += RingHeight(r, px, pz, m_time);
            m.vertices[i] = {px, py, pz};
            Vec3 nn{w.nx - rsx, w.ny, w.nz - rsz};
            const float nl = std::sqrt(nn.x * nn.x + nn.y * nn.y + nn.z * nn.z);
            nrm[i] = nl > 1e-6f ? Vec3{nn.x / nl, nn.y / nl, nn.z / nl} : Vec3{0, 1, 0};
            m.uvs[i] = {bx / tile, bz / tile};

            if (!realisticLook) { m.vertexColors.clear(); continue; }
            // depth under this point
            const Vec3 wp = toWorld.MultiplyPoint({px, py, pz});
            float depthHere = depth > 0.0f ? depth : assumedDepth, groundY = 0.0f;
            if (GroundWorldY(terrains, wp, groundY)) depthHere = wp.y - groundY;
            const float crest = std::max(-1.0f, std::min(1.0f, waveY / maxAmp));
            const float slope = 1.0f - nrm[i].y;
            const float pat = FoamPattern(px - current.x * m_time * 0.5f, pz - current.y * m_time * 0.5f, m_time);
            RGB body = DepthColor(depthHere, clarity, shallow, deep);
            // a little sky in the surface even where the renderer's own reflection is weak: Fresnel against the camera
            const Vec3 toCam{camW.x - wp.x, camW.y - wp.y, camW.z - wp.z};
            const float cl = std::sqrt(toCam.x * toCam.x + toCam.y * toCam.y + toCam.z * toCam.z);
            const Vec3 ev = cl > 1e-4f ? Vec3{toCam.x / cl, toCam.y / cl, toCam.z / cl} : Vec3{0, 1, 0};
            const float back = -(ev.x * sunDir.x + ev.y * sunDir.y + ev.z * sunDir.z);   // looking towards the sun: +
            const float glow = Scatter(back, (crest + 1.0f) * 0.5f, scatter);
            body.r += glow * 0.10f; body.g += glow * 0.55f; body.b += glow * 0.35f;
            float foam = CrestFoam(crest, slope, foamAmount, pat);
            foam = std::max(foam, ShoreFoam(depthHere, shoreFoamWidth, m_time, pat, std::max(0.3f, foamAmount)));
            if (haveRings) foam = std::max(foam, std::min(1.0f, std::fabs(py - waveY) * 9.0f * pat));
            RGB c = Mix(body, foamRGB, Clamp01(foam));
            m.vertexColors[i] = Color{Clamp01(c.r), Clamp01(c.g), Clamp01(c.b), 1.0f};
        }
        // crack-free joins between rings
        for (int i = 0; i < nv; ++i) {
            const int a = g.stitchA[i], b = g.stitchB[i];
            if (a < 0 || b < 0) continue;
            m.vertices[i] = (m.vertices[a] + m.vertices[b]) * 0.5f;
            Vec3 nn = nrm[a] + nrm[b];
            const float nl = nn.Magnitude();
            if (nl > 1e-6f) nrm[i] = nn * (1.0f / nl);
            if (realisticLook) {
                const Color& ca = m.vertexColors[a]; const Color& cb = m.vertexColors[b];
                m.vertexColors[i] = Color{(ca.r + cb.r) * 0.5f, (ca.g + cb.g) * 0.5f, (ca.b + cb.b) * 0.5f, 1.0f};
            }
        }
        for (int i = 0; i < nv; ++i) m.normals[i] = nrm[i];
        m.triangles = g.tri;
        m_lastVerts = nv; m_lastTris = (int)g.tri.size() / 3;
        mr->mesh = std::move(m);
    }

    // Props float or sink. Dynamic bodies a motor doesn't already drive get an
    // upward force of (displaced volume * fluidDensity * g) and some drag.
    // Gravity in this engine is an acceleration, and AddForce is a force, so a
    // body floats when its mass is less than fluidDensity times its volume.
    void BuoyBodies() {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc || fluidDensity <= 0.0f) return;
        constexpr float kG = 9.81f;
        for (Rigidbody3D* rb : sc->FindObjectsOfType<Rigidbody3D>()) {
            if (!rb || !rb->enabled || !rb->gameObject || !rb->transform) continue;
            if (rb->bodyType != Rigidbody3D::BodyType::Dynamic || rb->mass <= 1e-4f) continue;
            if (rb->gameObject == gameObject) continue;
            bool driven = false;
            for (Component* c : rb->gameObject->GetComponents<Component>())
                if (c && c->IgnoresBuoyancy()) { driven = true; break; }
            if (driven) continue;
            Vec3 mn, mx;
            bool have = false;
            for (Collider3D* col : rb->gameObject->GetComponents<Collider3D>()) {
                if (!col || col->isTrigger) continue;
                col->WorldAABB(mn, mx);
                have = true;
                break;
            }
            if (!have) {
                const Vec3 p = rb->transform->Position();
                mn = p - Vec3{0.5f, 0.5f, 0.5f};
                mx = p + Vec3{0.5f, 0.5f, 0.5f};
            }
            const Vec3 centre{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f};
            const float margin = std::max(mx.x - mn.x, mx.z - mn.z) * 0.5f;
            float surf = 0.0f, floorY = 0.0f;
            if (!Column(centre, surf, floorY, margin)) continue;
            const float bot = std::max(mn.y, floorY);
            const float top = std::min(mx.y, surf);
            const float height = std::max(0.05f, mx.y - mn.y);
            const float submerged = std::max(0.0f, top - bot);
            if (submerged <= 1e-4f) continue;
            const float frac = std::min(1.0f, submerged / height);
            const float volume = std::max(0.0f, (mx.x - mn.x) * (mx.y - mn.y) * (mx.z - mn.z));
            rb->AddForce({0.0f, volume * frac * fluidDensity * kG, 0.0f});
            const float drag = std::max(0.0f, linearDrag) * frac;
            if (drag > 0.0f) rb->AddForce(rb->velocity * (-drag * rb->mass));
        }
    }
};

/// Ease a swimming body so the chest rides the surface. Space-style `up` climbs,
/// crouch-style `down` dives; with neither, a fall is caught and the chest settles
/// at the waterline. The caller turns gravity off while this runs.
inline void SwimVertical(Rigidbody3D& rb, float feetY, float surfaceY, bool up, bool down,
                         float upSpeed, float downSpeed, float dt) {
    if (up && !down) { rb.velocity.y = upSpeed; return; }
    if (down && !up) { rb.velocity.y = -downSpeed; return; }
    const float chest = feetY + 1.15f;
    const float err = (surfaceY - 0.05f) - chest;
    float target = err * 3.2f;
    if (target > 2.2f) target = 2.2f;
    if (target < -2.2f) target = -2.2f;
    if (rb.velocity.y < -1.5f) rb.velocity.y = -1.5f;
    const float t = dt > 0.0f ? (1.0f - std::exp(-8.0f * dt)) : 1.0f;
    rb.velocity.y += (target - rb.velocity.y) * t;
}

} // namespace okay
