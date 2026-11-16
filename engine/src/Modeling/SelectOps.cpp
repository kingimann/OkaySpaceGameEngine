#include "okay/Modeling/SelectOps.hpp"
#include "okay/Modeling/PolyOps.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>
#include <cstdint>

namespace okay::modeling {

namespace {
using Key = std::tuple<long, long, long>;
Key KeyOf(const Vec3& p) { return {std::lround(p.x * 4096.0f), std::lround(p.y * 4096.0f), std::lround(p.z * 4096.0f)}; }
bool Valid(const Mesh& m, int t) { return t >= 0 && t < m.TriangleCount(); }
bool TriOk(const Mesh& m, int t) {
    for (int k = 0; k < 3; ++k) { const int v = m.triangles[t * 3 + k]; if (v < 0 || v >= (int)m.vertices.size()) return false; }
    return true;
}
Vec3 Normal(const Mesh& m, int t) {
    const Vec3& a = m.vertices[m.triangles[t * 3]];
    return Vec3::Cross(m.vertices[m.triangles[t * 3 + 1]] - a, m.vertices[m.triangles[t * 3 + 2]] - a);
}
// Corner position ids of every triangle, and the triangles around each position.
struct KeyHash {
    std::size_t operator()(const Key& k) const {
        std::uint64_t h = (std::uint64_t)std::get<0>(k) * 0x9E3779B97F4A7C15ull;
        h ^= (std::uint64_t)std::get<1>(k) + 0x7F4A7C15ull + (h << 6) + (h >> 2);
        h ^= (std::uint64_t)std::get<2>(k) * 0xC2B2AE3D27D4EB4Full + (h << 6) + (h >> 2);
        return (std::size_t)h;
    }
};
struct Adj {
    std::vector<std::array<int, 3>> corner;
    std::vector<std::vector<int>> around;      // by position id
};
Adj Build(const Mesh& m) {
    Adj a;
    std::unordered_map<Key, int, KeyHash> ids;
    ids.reserve(m.vertices.size() * 2);
    a.corner.assign((std::size_t)m.TriangleCount(), std::array<int, 3>{-1, -1, -1});
    for (int t = 0; t < m.TriangleCount(); ++t) {
        if (!TriOk(m, t)) continue;
        for (int k = 0; k < 3; ++k) {
            auto it = ids.emplace(KeyOf(m.vertices[m.triangles[t * 3 + k]]), (int)ids.size()).first;
            a.corner[t][k] = it->second;
            if ((int)a.around.size() <= it->second) a.around.resize(it->second + 1);
            a.around[it->second].push_back(t);
        }
    }
    return a;
}
std::set<int> Clean(const Mesh& m, const std::vector<int>& tris) {
    std::set<int> s;
    for (int t : tris) if (Valid(m, t) && TriOk(m, t)) s.insert(t);
    return s;
}
} // namespace

std::vector<int> GrowFaces(const Mesh& m, const std::vector<int>& tris) {
    const std::set<int> sel = Clean(m, tris);
    if (sel.empty()) return {};
    const Adj a = Build(m);
    std::vector<char> mark((std::size_t)m.TriangleCount(), 0);
    for (int t : sel) for (int k = 0; k < 3; ++k) for (int n : a.around[a.corner[t][k]]) mark[n] = 1;
    for (int t : sel) mark[t] = 1;
    std::vector<int> out;
    for (int t = 0; t < (int)mark.size(); ++t) if (mark[t]) out.push_back(t);
    return out;
}

std::vector<int> ShrinkFaces(const Mesh& m, const std::vector<int>& tris) {
    const std::set<int> sel = Clean(m, tris);
    if (sel.empty()) return {};
    const Adj a = Build(m);
    std::vector<char> in((std::size_t)m.TriangleCount(), 0);
    for (int t : sel) in[t] = 1;
    std::vector<int> out;
    for (int t : sel) {
        bool rim = false;
        for (int k = 0; k < 3 && !rim; ++k) for (int n : a.around[a.corner[t][k]]) if (!in[n]) { rim = true; break; }
        if (!rim) out.push_back(t);
    }
    return out;
}

const char* SimilarKindName(SimilarKind k) {
    switch (k) {
        case SimilarKind::Normal: return "Facing";
        case SimilarKind::Area: return "Area";
        case SimilarKind::Slot: return "Material slot";
        case SimilarKind::Coplanar: return "Same plane";
    }
    return "Facing";
}

std::vector<int> SelectSimilar(const Mesh& m, const std::vector<int>& tris, SimilarKind kind, float tolerance) {
    const std::set<int> sel = Clean(m, tris);
    std::vector<int> out;
    if (sel.empty() || !std::isfinite(tolerance)) return out;
    tolerance = std::max(0.0f, tolerance);
    struct Ref { Vec3 n; float area; int slot; float d; };
    std::vector<Ref> refs;
    for (int t : sel) {
        const Vec3 c = Normal(m, t);
        const float len = c.Magnitude();
        if (len < 1e-12f) continue;
        const Vec3 n = c * (1.0f / len);
        refs.push_back({n, len * 0.5f, m.HasSlots() ? m.triSlot[t] : 0, Vec3::Dot(n, m.vertices[m.triangles[t * 3]])});
    }
    for (int t = 0; t < m.TriangleCount(); ++t) {
        if (!TriOk(m, t)) continue;
        const Vec3 c = Normal(m, t);
        const float len = c.Magnitude();
        if (len < 1e-12f) continue;
        const Vec3 n = c * (1.0f / len);
        const float area = len * 0.5f;
        const int slot = m.HasSlots() ? m.triSlot[t] : 0;
        const float d = Vec3::Dot(n, m.vertices[m.triangles[t * 3]]);
        bool hit = false;
        for (const Ref& r : refs) {
            switch (kind) {
                case SimilarKind::Normal: hit = Vec3::Dot(n, r.n) >= std::cos(tolerance * 0.01745329252f) - 1e-6f; break;
                case SimilarKind::Area: hit = std::fabs(area - r.area) <= r.area * tolerance + 1e-9f; break;
                case SimilarKind::Slot: hit = slot == r.slot; break;
                case SimilarKind::Coplanar: hit = Vec3::Dot(n, r.n) > 0.9999f && std::fabs(d - r.d) <= tolerance + 1e-6f; break;
            }
            if (hit) break;
        }
        if (hit) out.push_back(t);
    }
    return out;
}

std::vector<int> SelectByNormal(const Mesh& m, const Vec3& dir, float angleDegrees) {
    std::vector<int> out;
    if (dir.SqrMagnitude() < 1e-12f || !std::isfinite(angleDegrees + dir.x + dir.y + dir.z)) return out;
    const Vec3 d = dir.Normalized();
    const float lim = std::cos(std::clamp(angleDegrees, 0.0f, 180.0f) * 0.01745329252f) - 1e-6f;
    for (int t = 0; t < m.TriangleCount(); ++t) {
        if (!TriOk(m, t)) continue;
        const Vec3 c = Normal(m, t);
        const float len = c.Magnitude();
        if (len < 1e-12f) continue;
        if (Vec3::Dot(c * (1.0f / len), d) >= lim) out.push_back(t);
    }
    return out;
}

std::vector<int> SelectByArea(const Mesh& m, float minArea, float maxArea) {
    std::vector<int> out;
    if (!std::isfinite(minArea)) return out;
    for (int t = 0; t < m.TriangleCount(); ++t) {
        if (!TriOk(m, t)) continue;
        const float a = Normal(m, t).Magnitude() * 0.5f;
        if (a >= minArea && (maxArea < 0.0f || a <= maxArea)) out.push_back(t);
    }
    return out;
}

bool BakeTransform(Mesh& m, const Vec3& scale, const Quat& rotation) {
    if (!std::isfinite(scale.x + scale.y + scale.z) || std::fabs(scale.x) < 1e-9f || std::fabs(scale.y) < 1e-9f || std::fabs(scale.z) < 1e-9f) return false;
    for (Vec3& v : m.vertices) v = rotation * Vec3{v.x * scale.x, v.y * scale.y, v.z * scale.z};
    // Normals use the inverse transpose of the scale, then the rotation.
    for (Vec3& n : m.normals) {
        const Vec3 s{n.x / scale.x, n.y / scale.y, n.z / scale.z};
        const Vec3 r = rotation * s;
        n = r.SqrMagnitude() > 1e-20f ? r.Normalized() : r;
    }
    const int flips = (scale.x < 0) + (scale.y < 0) + (scale.z < 0);
    if (flips % 2 == 1) m.FlipWinding();
    m.name = "";
    return true;
}

bool MakeWindowHole(Mesh& m, const std::vector<int>& tris, float border, std::string* error) {
    const std::set<int> sel = Clean(m, tris);
    if (sel.empty()) { if (error) *error = "Select the faces to cut the window in first (Face mode, key 3)."; return false; }
    const std::vector<int> faces(sel.begin(), sel.end());
    m.InsetFaces(faces, std::clamp(std::isfinite(border) ? border : 0.3f, 0.05f, 0.95f));   // the inner faces keep their numbers
    m.DeleteFaces(faces);
    return true;
}

int RoundEdges(Mesh& m, const std::vector<std::pair<int, int>>& edges, float width, int segments, std::string* error) {
    if (edges.empty()) { if (error) *error = "Select the edges to round first (Edge mode, key 2)."; return 0; }
    PolyMesh pm = PolyMesh::FromMesh(m, {false, true, 1.0f});
    std::string err;
    const int n = BevelEdges(pm, edges, width, &err, std::clamp(segments, 1, 16), 0.5f);
    if (n <= 0) { if (error) *error = err; return 0; }
    CompactVerts(pm);
    const bool smooth = m.HasNormals();
    const float ang = m.autoSmoothAngle;
    Mesh out = pm.ToMesh();
    out.name = "";
    if (smooth) { if (ang > 0) out.ComputeAutoSmoothNormals(ang); else out.ComputeSmoothNormals(); }
    m = std::move(out);
    return n;
}

Mesh MakeTable(float width, float depth, float height, float legSize, float topThickness) {
    auto fin = [](float v, float def) { return std::isfinite(v) ? v : def; };
    width = fin(width, 1.6f); depth = fin(depth, 0.9f); height = fin(height, 0.75f); legSize = fin(legSize, 0.07f); topThickness = fin(topThickness, 0.06f);
    width = std::clamp(width, 0.2f, 20.0f); depth = std::clamp(depth, 0.2f, 20.0f); height = std::clamp(height, 0.2f, 10.0f);
    legSize = std::clamp(legSize, 0.01f, std::min(width, depth) * 0.25f);
    topThickness = std::clamp(topThickness, 0.01f, height * 0.5f);
    Mesh t;
    const Mesh unit = Mesh::Cube(1.0f);
    t.Add(unit, {0, height - topThickness * 0.5f, 0}, {width, topThickness, depth});           // the top
    const float legH = height - topThickness;
    const float lx = width * 0.5f - legSize * 0.5f - 0.03f * width, lz = depth * 0.5f - legSize * 0.5f - 0.03f * depth;
    for (int sx : {-1, 1}) for (int sz : {-1, 1}) t.Add(unit, {sx * lx, legH * 0.5f, sz * lz}, {legSize, legH, legSize});
    t.AssignSlot({12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59}, 1);
    t.AssignSlot({0}, 0);
    t.slotColors = {Color{0.80f, 0.66f, 0.45f, 1}, Color{0.55f, 0.38f, 0.20f, 1}};
    t.ApplySlotColors();
    t.RefreshNormals();
    return t;
}

} // namespace okay::modeling
