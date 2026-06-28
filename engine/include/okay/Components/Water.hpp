#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/Color.hpp"
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

    void Start() override { Apply(); }

    void Update(float dt) override {
        m_time += dt * waveSpeed;
        Animate();
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
};

} // namespace okay
