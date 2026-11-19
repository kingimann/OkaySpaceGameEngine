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
#include <algorithm>
#include <cmath>
#include <vector>

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
