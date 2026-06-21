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

// --- Fractal value noise (Perlin-like): smooth, tileable-ish hills in [0,1]. ---
static float ValueNoise(float x, float z, unsigned seed) {
    int xi = (int)std::floor(x), zi = (int)std::floor(z);
    float xf = x - xi, zf = z - zi;
    auto r = [&](int a, int b) {
        unsigned h = seed + 0x9E3779B9u;
        h ^= (unsigned)a * 0x85EBCA6Bu; h = (h ^ (h >> 13)) * 0xC2B2AE35u;
        h ^= (unsigned)b * 0x27D4EB2Fu; h = (h ^ (h >> 16));
        return (h & 0xFFFFu) / 65535.0f;
    };
    float u = xf * xf * (3.0f - 2.0f * xf), v = zf * zf * (3.0f - 2.0f * zf);
    float a = r(xi, zi), b = r(xi + 1, zi), c = r(xi, zi + 1), d = r(xi + 1, zi + 1);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
static float Fractal(float x, float z, int octaves, unsigned seed) {
    float sum = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += ValueNoise(x * freq, z * freq, seed + (unsigned)o * 1013u) * amp;
        norm += amp; amp *= 0.5f; freq *= 2.0f;
    }
    return norm > 0.0f ? sum / norm : 0.0f;   // [0,1]
}

void Terrain::Generate(int type, float amplitude, float frequency, int octaves, unsigned seed) {
    if (octaves < 1) octaves = 1;
    const int dim = Dim();
    const float cx = (dim - 1) * 0.5f, cz = (dim - 1) * 0.5f;
    const float maxr = std::sqrt(cx * cx + cz * cz) + 1e-3f;
    for (int z = 0; z < dim; ++z)
        for (int x = 0; x < dim; ++x) {
            float fx = (float)x / resolution * frequency;
            float fz = (float)z / resolution * frequency;
            float n = Fractal(fx, fz, octaves, seed);     // [0,1]
            float h = 0.0f;
            switch (type) {
                case 0:  h = std::pow(n, 1.6f) * amplitude; break;            // Mountains (sharp peaks)
                case 1:  h = n * amplitude * 0.6f; break;                     // Hills (rolling)
                case 2:  h = (n - 0.5f) * amplitude * 0.4f; break;            // Plains (gentle)
                case 3: {                                                     // Plateau (mesas)
                    float t = (n - 0.45f) / 0.25f; t = t < 0 ? 0 : (t > 1 ? 1 : t);
                    h = t * t * (3.0f - 2.0f * t) * amplitude; break;
                }
                default: {                                                    // Islands (radial falloff)
                    float dx = x - cx, dz = z - cz;
                    float d = std::sqrt(dx * dx + dz * dz) / maxr;            // 0 center .. 1 edge
                    float island = n - d * 1.15f;
                    h = (island > 0.0f ? island : 0.0f) * amplitude; break;
                }
            }
            heights[(std::size_t)z * dim + x] = h;
        }
}

float Terrain::SampleHeight(float localX, float localZ) const {
    float cell = CellSize(), half = size * 0.5f;
    float gx = (localX + half) / cell, gz = (localZ + half) / cell;
    int x0 = (int)std::floor(gx), z0 = (int)std::floor(gz);
    float fx = gx - x0, fz = gz - z0;
    float h00 = GetHeight(x0, z0), h10 = GetHeight(x0 + 1, z0);
    float h01 = GetHeight(x0, z0 + 1), h11 = GetHeight(x0 + 1, z0 + 1);
    return (h00 * (1 - fx) + h10 * fx) * (1 - fz) + (h01 * (1 - fx) + h11 * fx) * fz;
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

void Terrain::SmoothAt(float localX, float localZ, float radius, float amount) {
    float cell = CellSize(), half = size * 0.5f;
    std::vector<float> src = heights;   // read from a snapshot so it relaxes evenly
    auto at = [&](int x, int z) {
        if (x < 0) x = 0; if (z < 0) z = 0;
        if (x >= Dim()) x = Dim() - 1; if (z >= Dim()) z = Dim() - 1;
        return src[(std::size_t)z * Dim() + x];
    };
    for (int z = 0; z < Dim(); ++z)
        for (int x = 0; x < Dim(); ++x) {
            float vx = x * cell - half, vz = z * cell - half;
            float dx = vx - localX, dz = vz - localZ;
            float d = std::sqrt(dx * dx + dz * dz);
            if (d > radius) continue;
            float avg = (at(x, z) + at(x - 1, z) + at(x + 1, z) + at(x, z - 1) + at(x, z + 1)) * 0.2f;
            float falloff = (1.0f - d / radius) * amount;
            float& h = heights[(std::size_t)z * Dim() + x];
            h += (avg - h) * falloff;
        }
}

void Terrain::FlattenAt(float localX, float localZ, float radius, float target, float amount) {
    float cell = CellSize(), half = size * 0.5f;
    for (int z = 0; z < Dim(); ++z)
        for (int x = 0; x < Dim(); ++x) {
            float vx = x * cell - half, vz = z * cell - half;
            float dx = vx - localX, dz = vz - localZ;
            float d = std::sqrt(dx * dx + dz * dz);
            if (d > radius) continue;
            float falloff = (1.0f - d / radius) * amount;
            float& h = heights[(std::size_t)z * Dim() + x];
            h += (target - h) * falloff;
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
    if (autoColor) m.triColors.reserve((std::size_t)resolution * resolution * 2);
    // Pick a layer color for a face from its elevation + steepness.
    auto faceColor = [&](int a, int b, int c) {
        const Vec3& va = m.vertices[a]; const Vec3& vb = m.vertices[b]; const Vec3& vc = m.vertices[c];
        float y = (va.y + vb.y + vc.y) * (1.0f / 3.0f);
        Vec3 n = Vec3::Cross(vb - va, vc - va).Normalized();
        float slope = 1.0f - std::fabs(n.y);                 // 0 flat .. 1 vertical
        if (y <= waterLevel) return waterColor;
        if (y <= waterLevel + std::fmax(0.4f, snowLevel * 0.06f)) return sandColor;
        if (slope >= rockSlope) return rockColor;
        if (y >= snowLevel) return snowColor;
        // Blend grass->rock as the slope approaches the rock threshold for a
        // softer transition.
        float t = slope / rockSlope; t = t < 0 ? 0 : (t > 1 ? 1 : t);
        return Color{grassColor.r + (rockColor.r - grassColor.r) * t * 0.5f,
                     grassColor.g + (rockColor.g - grassColor.g) * t * 0.5f,
                     grassColor.b + (rockColor.b - grassColor.b) * t * 0.5f, 1.0f};
    };
    for (int z = 0; z < resolution; ++z)
        for (int x = 0; x < resolution; ++x) {
            int i00 = z * dim + x, i10 = z * dim + x + 1;
            int i01 = (z + 1) * dim + x, i11 = (z + 1) * dim + x + 1;
            // Wound so the face normal points up (+Y).
            m.triangles.push_back(i00); m.triangles.push_back(i01); m.triangles.push_back(i10);
            m.triangles.push_back(i10); m.triangles.push_back(i01); m.triangles.push_back(i11);
            if (autoColor) {
                m.triColors.push_back(faceColor(i00, i01, i10));
                m.triColors.push_back(faceColor(i10, i01, i11));
            }
        }
    m.ComputeSmoothNormals();   // smooth (Gouraud) shading for rolling hills
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
