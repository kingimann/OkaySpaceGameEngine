#include "okay/Components/Terrain.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Core/Random.hpp"

#include <cmath>

namespace okay {

void Terrain::Resize(int res) {
    if (res < 1) res = 1;
    resolution = res;
    heights.assign((std::size_t)Dim() * Dim(), 0.0f);
}

float Terrain::GetHeight(int x, int z) const {
    if (x < 0 || z < 0 || x >= Dim() || z >= Dim()) return 0.0f;
    return heights[(std::size_t)z * Dim() + x];
}

void Terrain::SetHeight(int x, int z, float h) {
    if (x < 0 || z < 0 || x >= Dim() || z >= Dim()) return;
    heights[(std::size_t)z * Dim() + x] = h;
}

void Terrain::Flatten(float h) {
    for (auto& v : heights) v = h;
}

void Terrain::Smooth() {
    std::vector<float> out = heights;
    for (int z = 0; z < Dim(); ++z)
        for (int x = 0; x < Dim(); ++x) {
            float sum = 0.0f; int n = 0;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = x + dx, nz = z + dz;
                    if (nx < 0 || nz < 0 || nx >= Dim() || nz >= Dim()) continue;
                    sum += GetHeight(nx, nz); ++n;
                }
            out[(std::size_t)z * Dim() + x] = n ? sum / n : GetHeight(x, z);
        }
    heights.swap(out);
}

void Terrain::Randomize(float amount, unsigned seed) {
    Random rng(seed);
    for (auto& v : heights) v += rng.Range(-amount, amount);
}

void Terrain::Hills(int count, float maxHeight, unsigned seed) {
    Random rng(seed);
    for (int i = 0; i < count; ++i) {
        float cx = rng.Range(0.0f, (float)resolution);
        float cz = rng.Range(0.0f, (float)resolution);
        float r  = rng.Range(resolution * 0.08f, resolution * 0.3f);
        float h  = rng.Range(maxHeight * 0.3f, maxHeight);
        for (int z = 0; z < Dim(); ++z)
            for (int x = 0; x < Dim(); ++x) {
                float dx = x - cx, dz = z - cz;
                float d2 = dx * dx + dz * dz;
                float falloff = std::exp(-d2 / (2.0f * r * r));   // gaussian bump
                heights[(std::size_t)z * Dim() + x] += h * falloff;
            }
    }
}

void Terrain::RaiseAt(float localX, float localZ, float radius, float delta) {
    float cell = CellSize();
    float half = size * 0.5f;
    for (int z = 0; z < Dim(); ++z)
        for (int x = 0; x < Dim(); ++x) {
            float vx = x * cell - half, vz = z * cell - half;
            float dx = vx - localX, dz = vz - localZ;
            float d = std::sqrt(dx * dx + dz * dz);
            if (d > radius) continue;
            float falloff = 1.0f - d / radius;           // soft brush
            heights[(std::size_t)z * Dim() + x] += delta * falloff;
        }
}

Mesh Terrain::BuildMesh() const {
    Mesh m;
    m.name = "Terrain";
    const int dim = Dim();
    const float cell = CellSize();
    const float half = size * 0.5f;
    m.vertices.reserve((std::size_t)dim * dim);
    m.uvs.reserve((std::size_t)dim * dim);
    for (int z = 0; z < dim; ++z)
        for (int x = 0; x < dim; ++x) {
            m.vertices.push_back({x * cell - half, GetHeight(x, z), z * cell - half});
            m.uvs.push_back({(float)x / (dim - 1), (float)z / (dim - 1)});
        }
    m.triangles.reserve((std::size_t)resolution * resolution * 6);
    for (int z = 0; z < resolution; ++z)
        for (int x = 0; x < resolution; ++x) {
            int i00 = z * dim + x, i10 = z * dim + x + 1;
            int i01 = (z + 1) * dim + x, i11 = (z + 1) * dim + x + 1;
            // Wound so the face normal points up (+Y).
            m.triangles.push_back(i00); m.triangles.push_back(i01); m.triangles.push_back(i10);
            m.triangles.push_back(i10); m.triangles.push_back(i01); m.triangles.push_back(i11);
        }
    return m;
}

void Terrain::Apply() {
    if (!gameObject) return;
    auto* mr = gameObject->GetComponent<MeshRenderer>();
    if (!mr) mr = gameObject->AddComponent<MeshRenderer>();
    mr->mesh = BuildMesh();
    mr->color = color;
    mr->doubleSided = true;   // visible from above and below regardless of winding
}

} // namespace okay
