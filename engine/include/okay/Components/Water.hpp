#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
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

    /// Where a swimmer is relative to the water volumes in the scene.
    struct Probe {
        bool  inWater = false;     ///< the body overlaps a volume
        float surfaceY = 0.0f;     ///< world height of the surface at the feet
        float feetDepth = 0.0f;    ///< surfaceY - feet.y (negative when the feet are above it)
    };

    void Start() override { Apply(); }

    void Update(float dt) override {
        m_time += dt * waveSpeed;
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

    /// The deepest water the body at `feet` (height `bodyHeight`) is standing in.
    static Probe ProbeAt(const Scene* scene, const Vec3& feet, float bodyHeight) {
        Probe best;
        if (!scene) return best;
        const float top = feet.y + std::max(0.2f, bodyHeight);
        for (Water* w : scene->FindObjectsOfType<Water>()) {
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

    float WaveAt(float x, float z) const {
        float k = 6.2831853f / (waveLength > 0.01f ? waveLength : 0.01f);
        // Crossing swells at angles + a finer, faster ripple on top give a natural,
        // non-repeating, lively surface rather than one rolling sine.
        float a = std::sin(x * k + m_time);
        float b = std::sin((x * 0.6f + z * 0.8f) * k + m_time * 1.3f);
        float c = std::sin((x * 1.7f - z * 1.3f) * k * 2.1f + m_time * 2.2f) * 0.35f;
        return (a + b + c) * 0.42f * waveHeight;
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
                m.vertices.push_back({b.x, WaveAt(b.x, b.z), b.z});
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
        m.ComputeSmoothNormals();   // smooth, rolling swell
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
