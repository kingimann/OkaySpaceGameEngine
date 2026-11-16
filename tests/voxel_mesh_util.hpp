#pragma once
// Shared helpers for the voxel terrain tests (test_voxel_edit.cpp, test_voxel_chunks.cpp).
#include <Okay.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace voxeltest {
using namespace okay;

using TriKey = std::array<float, 22>;   // 3 x (position, normal) + the face colour (r, g, b, a) or 0s

/// Every triangle of a mesh as plain numbers, sorted, so two meshes with the same triangles in a different order
/// (the combined mesh goes chunk by chunk, BuildMesh goes row by row) compare equal.
inline std::vector<TriKey> Canon(const Mesh& m) {
    std::vector<TriKey> out;
    const bool hasN = m.HasNormals();
    const bool hasC = m.HasFaceColors();
    for (int t = 0; t < m.TriangleCount(); ++t) {
        TriKey k{};
        for (int c = 0; c < 3; ++c) {
            const int vi = m.triangles[(std::size_t)t * 3 + c];
            const Vec3& p = m.vertices[(std::size_t)vi];
            const Vec3 n = hasN ? m.normals[(std::size_t)vi] : Vec3{0, 0, 0};
            k[(std::size_t)c * 6 + 0] = p.x; k[(std::size_t)c * 6 + 1] = p.y; k[(std::size_t)c * 6 + 2] = p.z;
            k[(std::size_t)c * 6 + 3] = n.x; k[(std::size_t)c * 6 + 4] = n.y; k[(std::size_t)c * 6 + 5] = n.z;
        }
        if (hasC) { const Color& c = m.triColors[(std::size_t)t]; k[18] = c.r; k[19] = c.g; k[20] = c.b; k[21] = c.a; }
        out.push_back(k);
    }
    std::sort(out.begin(), out.end());
    return out;
}

/// How many triangles differ (bit for bit) between two meshes, ignoring the order they come in.
inline int Differences(const Mesh& a, const Mesh& b) {
    std::vector<TriKey> ca = Canon(a), cb = Canon(b);
    if (ca.size() != cb.size()) return (int)std::max(ca.size(), cb.size());
    int diff = 0;
    for (std::size_t i = 0; i < ca.size(); ++i)
        if (std::memcmp(ca[i].data(), cb[i].data(), sizeof(TriKey)) != 0) ++diff;
    return diff;
}

/// True when two meshes hold the very same bytes in the very same order (not just the same triangles).
inline bool IdenticalArrays(const Mesh& a, const Mesh& b) {
    auto same = [](const auto& x, const auto& y) {
        using T = typename std::decay<decltype(x)>::type::value_type;
        return x.size() == y.size() && (x.empty() || std::memcmp(x.data(), y.data(), x.size() * sizeof(T)) == 0);
    };
    return same(a.vertices, b.vertices) && same(a.normals, b.normals) && same(a.uvs, b.uvs) &&
           same(a.triangles, b.triangles) && same(a.triColors, b.triColors);
}

/// Everything the terrain asks the renderers to draw: its own mesh plus every chunk object under it. (While the
/// running game edits the terrain it draws with one small object per chunk instead of one combined mesh.)
inline Mesh GatherDrawn(GameObject* go) {
    Mesh all;
    auto add = [&](const Mesh& m) {
        const int base = (int)all.vertices.size();
        all.vertices.insert(all.vertices.end(), m.vertices.begin(), m.vertices.end());
        all.normals.insert(all.normals.end(), m.normals.begin(), m.normals.end());
        all.uvs.insert(all.uvs.end(), m.uvs.begin(), m.uvs.end());
        for (int t : m.triangles) all.triangles.push_back(base + t);
        all.triColors.insert(all.triColors.end(), m.triColors.begin(), m.triColors.end());
    };
    if (auto* mr = go->GetComponent<MeshRenderer>()) add(mr->mesh);
    for (Transform* c : go->transform->Children())
        if (c && c->gameObject && c->gameObject->active && !c->gameObject->IsDestroyed())   // an object being destroyed is not drawn
            if (auto* mr = c->gameObject->GetComponent<MeshRenderer>()) add(mr->mesh);
    return all;
}

/// The chunk objects under a terrain (every child that has a MeshRenderer).
inline std::vector<GameObject*> ChunkObjects(GameObject* go) {
    std::vector<GameObject*> out;
    for (Transform* c : go->transform->Children())
        if (c && c->gameObject && c->gameObject->active && !c->gameObject->IsDestroyed() && c->gameObject->GetComponent<MeshRenderer>())
            out.push_back(c->gameObject);
    return out;
}

/// Tiny deterministic random numbers, so a failure reproduces.
struct Lcg {
    std::uint32_t s;
    explicit Lcg(std::uint32_t seed) : s(seed) {}
    float Next() { s = s * 1664525u + 1013904223u; return (float)(s >> 8) / 16777216.0f; }
    float Range(float a, float b) { return a + (b - a) * Next(); }
    int   Int(int n) { return (int)(Next() * n) % n; }
};

inline bool AllFinite(const VoxelTerrain& v) {
    for (float d : v.density) if (!std::isfinite(d)) return false;
    return true;
}

/// One random edit of any of the four kinds, somewhere on or inside the terrain (centres range a little
/// past the grid edge, and some land exactly on a chunk border).
inline void RandomEdit(VoxelTerrain& v, Lcg& rng, int i) {
    float gx = rng.Range(-1.0f, (float)v.nx), gz = rng.Range(-1.0f, (float)v.nz);
    if (i % 5 == 0) gx = 16.0f + (float)(rng.Int(3) - 1);
    if (i % 7 == 0) gz = 16.0f + (float)(rng.Int(3) - 1);
    if (i % 11 == 0) gx = (float)(v.nx - 1) - (float)rng.Int(3);
    Vec3 c{gx * v.voxelSize - v.HalfX(), rng.Range(0.15f, 0.65f) * v.SizeY(), gz * v.voxelSize - v.HalfZ()};
    const float r = rng.Range(1.2f, 4.0f) * v.voxelSize, amt = rng.Range(0.3f, 4.0f);
    switch (rng.Int(4)) {
        case 0: v.Dig(c, r, amt); break;
        case 1: v.Add(c, r, amt); break;
        case 2: v.SmoothAt(c, r, rng.Range(0.2f, 1.0f)); break;
        default: v.DigBox({c.x - r, c.y - r, c.z - r}, {c.x + r, c.y + r, c.z + r}, rng.Range(-3.0f, 3.0f)); break;
    }
}

} // namespace voxeltest
