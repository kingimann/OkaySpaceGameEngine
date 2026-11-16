#include "okay/Modeling/SelectOps.hpp"
#include "okay/Modeling/Measure.hpp"
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

std::vector<std::vector<int>> LooseParts(const Mesh& m) {
    std::vector<std::vector<int>> parts;
    const Adj a = Build(m);
    const int nt = m.TriangleCount();
    std::vector<int> comp((std::size_t)nt, -1);
    for (int t = 0; t < nt; ++t) {
        if (comp[t] >= 0 || a.corner[t][0] < 0) continue;
        const int id = (int)parts.size();
        parts.emplace_back();
        std::vector<int> stack = {t};
        comp[t] = id;
        while (!stack.empty()) {
            const int c = stack.back(); stack.pop_back();
            parts[id].push_back(c);
            for (int k = 0; k < 3; ++k) for (int n : a.around[a.corner[c][k]]) if (comp[n] < 0) { comp[n] = id; stack.push_back(n); }
        }
        std::sort(parts[id].begin(), parts[id].end());
    }
    std::stable_sort(parts.begin(), parts.end(), [](const std::vector<int>& x, const std::vector<int>& y) { return x.size() > y.size(); });
    return parts;
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
    out.hiddenPos = m.hiddenPos; out.hiddenUV = m.hiddenUV; out.hiddenSlot = m.hiddenSlot; out.hiddenPoly = m.hiddenPoly; out.hiddenColor = m.hiddenColor;
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

namespace {
// A uniform grid over the target's triangles, so each vertex only looks at nearby ones.
struct TriGrid {
    const Mesh& t; Vec3 lo{0, 0, 0}, cell{1, 1, 1}; int n[3] = {1, 1, 1};
    std::vector<std::vector<int>> cells;
    explicit TriGrid(const Mesh& target) : t(target) {
        Vec3 hi{0, 0, 0}; bool any = false;
        for (const Vec3& v : t.vertices) {
            if (!any) { lo = hi = v; any = true; }
            else { lo = Vec3{std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)}; hi = Vec3{std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)}; }
        }
        const int nt = t.TriangleCount();
        const int res = std::clamp((int)std::cbrt((float)std::max(1, nt) * 0.5f) + 1, 1, 48);
        for (int a = 0; a < 3; ++a) {
            n[a] = res;
            const float ext = (&hi.x)[a] - (&lo.x)[a];
            (&cell.x)[a] = std::max(ext / (float)res, 1e-6f);
        }
        cells.assign((std::size_t)n[0] * n[1] * n[2], {});
        for (int k = 0; k < nt; ++k) {
            if (!Ok(k)) continue;
            Vec3 bl = t.vertices[t.triangles[k * 3]], bh = bl;
            for (int c = 1; c < 3; ++c) { const Vec3& v = t.vertices[t.triangles[k * 3 + c]]; bl = Vec3{std::min(bl.x, v.x), std::min(bl.y, v.y), std::min(bl.z, v.z)}; bh = Vec3{std::max(bh.x, v.x), std::max(bh.y, v.y), std::max(bh.z, v.z)}; }
            int c0[3], c1[3];
            for (int a = 0; a < 3; ++a) { c0[a] = idx((&bl.x)[a], a); c1[a] = idx((&bh.x)[a], a); }
            for (int x = c0[0]; x <= c1[0]; ++x) for (int y = c0[1]; y <= c1[1]; ++y) for (int z = c0[2]; z <= c1[2]; ++z) cells[(std::size_t)(x * n[1] + y) * n[2] + z].push_back(k);
        }
    }
    bool Ok(int k) const { for (int c = 0; c < 3; ++c) { const int v = t.triangles[k * 3 + c]; if (v < 0 || v >= (int)t.vertices.size()) return false; } return true; }
    int idx(float v, int a) const { return std::clamp((int)std::floor((v - (&lo.x)[a]) / (&cell.x)[a]), 0, n[a] - 1); }
    // Nearest surface point to p within `limit` (<= 0: anywhere). Returns the triangle or -1.
    int Nearest(const Vec3& p, float limit, Vec3& out) const {
        if (t.TriangleCount() == 0) return -1;
        int best = -1; float bestD = limit > 0 ? limit : 1e30f;
        const int c[3] = {idx(p.x, 0), idx(p.y, 1), idx(p.z, 2)};
        std::vector<char> seen((std::size_t)t.TriangleCount(), 0);
        const int maxRing = std::max(n[0], std::max(n[1], n[2]));
        for (int ring = 0; ring <= maxRing; ++ring) {
            for (int x = std::max(0, c[0] - ring); x <= std::min(n[0] - 1, c[0] + ring); ++x)
                for (int y = std::max(0, c[1] - ring); y <= std::min(n[1] - 1, c[1] + ring); ++y)
                    for (int z = std::max(0, c[2] - ring); z <= std::min(n[2] - 1, c[2] + ring); ++z) {
                        if (std::max(std::abs(x - c[0]), std::max(std::abs(y - c[1]), std::abs(z - c[2]))) != ring) continue;   // only the new shell
                        for (int k : cells[(std::size_t)(x * n[1] + y) * n[2] + z]) {
                            if (seen[k]) continue;
                            seen[k] = 1;
                            const Vec3 q = ClosestOnTriangle(p, t.vertices[t.triangles[k * 3]], t.vertices[t.triangles[k * 3 + 1]], t.vertices[t.triangles[k * 3 + 2]]);
                            const float d = (q - p).Magnitude();
                            if (d <= bestD) { bestD = d; best = k; out = q; }
                        }
                    }
            // Cells in the next ring are at least `ring * cell` away: stop once the best hit is closer than that.
            const float minCell = std::min(cell.x, std::min(cell.y, cell.z));
            if (best >= 0 && bestD <= (float)ring * minCell) break;
        }
        return best;
    }
};
Vec3 TriNormal(const Mesh& t, int k) {
    const Vec3& a = t.vertices[t.triangles[k * 3]];
    const Vec3 n = Vec3::Cross(t.vertices[t.triangles[k * 3 + 1]] - a, t.vertices[t.triangles[k * 3 + 2]] - a);
    return n.SqrMagnitude() > 1e-20f ? n.Normalized() : Vec3{0, 1, 0};
}
} // namespace

WrapReport Shrinkwrap(Mesh& m, const std::vector<int>& verts, const Mesh& target, WrapMode mode, float offset, float maxDistance, const Vec3& dir) {
    WrapReport r;
    if (m.vertices.empty() || target.triangles.empty() || !std::isfinite(offset + maxDistance + dir.x + dir.y + dir.z)) return r;
    std::vector<int> list = verts;
    if (list.empty()) for (int i = 0; i < (int)m.vertices.size(); ++i) list.push_back(i);
    const TriGrid grid(target);
    const bool projecting = mode == WrapMode::Project && dir.SqrMagnitude() > 1e-12f;
    const Vec3 d = projecting ? dir.Normalized() : Vec3{0, -1, 0};
    std::vector<char> done(m.vertices.size(), 0);
    for (int v : list) {
        if (v < 0 || v >= (int)m.vertices.size() || done[v]) continue;
        done[v] = 1;
        const Vec3 p = m.vertices[v];
        Vec3 hit{0, 0, 0}; int tri = -1;
        if (projecting) {
            const RayHit a = RayMeshHit(target, p, d), b = RayMeshHit(target, p, d * -1.0f);
            const RayHit* pick = (a.found && (!b.found || a.t <= b.t)) ? &a : (b.found ? &b : nullptr);
            if (pick && (maxDistance <= 0 || pick->t <= maxDistance)) { hit = pick->point; tri = pick->tri; }
        } else {
            tri = grid.Nearest(p, maxDistance, hit);
        }
        if (tri < 0) { ++r.missed; continue; }
        Vec3 q = hit;
        if (offset != 0.0f) q = hit + TriNormal(target, tri) * offset;
        r.maxMove = std::max(r.maxMove, (q - p).Magnitude());
        m.vertices[v] = q;
        ++r.moved;
    }
    if (r.moved) { m.name = ""; m.RefreshNormals(); }
    return r;
}

} // namespace okay::modeling
