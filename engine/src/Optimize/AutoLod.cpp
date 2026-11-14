// Safe LOD chain generator over the quadric simplifier. See AutoLod.hpp.
#include "okay/Optimize/AutoLod.hpp"
#include "okay/Components/LODGroup.hpp"
#include <algorithm>
#include <cmath>

namespace okay::optimize {
namespace {

bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Points beyond this make float maths overflow (areas, distances). Treated as unsupported.
constexpr double kHuge = 1e15;

double TriArea2(const Vec3& a, const Vec3& b, const Vec3& c) {
    const double ux = (double)b.x - a.x, uy = (double)b.y - a.y, uz = (double)b.z - a.z;
    const double vx = (double)c.x - a.x, vy = (double)c.y - a.y, vz = (double)c.z - a.z;
    const double cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
    return std::sqrt(cx * cx + cy * cy + cz * cz);
}

/// Copy of `m` without the unusable triangles. Vertices are kept, so per-vertex
/// attributes stay parallel. Returns how many triangles were skipped.
int CleanMesh(const Mesh& m, Mesh& out) {
    out = m;
    out.triangles.clear();
    out.triColors.clear();
    out.polyFace.clear();
    out.cornerUVs.clear();
    const int nv = (int)m.vertices.size();
    const int nt = m.TriangleCount();
    const bool tc = m.HasFaceColors();
    const bool cu = m.HasCornerUVs();
    int skipped = 0;
    for (int t = 0; t < nt; ++t) {
        const int a = m.triangles[(std::size_t)t * 3], b = m.triangles[(std::size_t)t * 3 + 1], c = m.triangles[(std::size_t)t * 3 + 2];
        bool ok = a >= 0 && b >= 0 && c >= 0 && a < nv && b < nv && c < nv && a != b && b != c && a != c;
        if (ok) ok = Finite(m.vertices[(std::size_t)a]) && Finite(m.vertices[(std::size_t)b]) && Finite(m.vertices[(std::size_t)c]);
        if (ok) {
            const double ar = TriArea2(m.vertices[(std::size_t)a], m.vertices[(std::size_t)b], m.vertices[(std::size_t)c]);
            ok = std::isfinite(ar) && ar > 0.0;
        }
        if (!ok) { ++skipped; continue; }
        out.triangles.insert(out.triangles.end(), {a, b, c});
        if (tc) out.triColors.push_back(m.triColors[(std::size_t)t]);
        if (cu) for (int k = 0; k < 3; ++k) out.cornerUVs.push_back(m.cornerUVs[(std::size_t)t * 3 + k]);
    }
    return skipped;
}

} // namespace

bool MeshIsSound(const Mesh& m, std::string* why) {
    auto bad = [&](const char* s) { if (why) *why = s; return false; };
    const int nv = (int)m.vertices.size();
    for (const Vec3& v : m.vertices) if (!Finite(v)) return bad("a vertex is NaN or infinite");
    if (m.triangles.size() % 3 != 0) return bad("index count is not a multiple of 3");
    for (std::size_t t = 0; t + 2 < m.triangles.size(); t += 3) {
        const int a = m.triangles[t], b = m.triangles[t + 1], c = m.triangles[t + 2];
        if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv) return bad("an index is out of range");
        const double ar = TriArea2(m.vertices[(std::size_t)a], m.vertices[(std::size_t)b], m.vertices[(std::size_t)c]);
        if (!(ar > 0.0)) return bad("a triangle has zero area");
    }
    return true;
}

AutoLodResult GenerateAutoLod(const Mesh& mesh, const AutoLodOptions& opt, const SkinData* skin) {
    AutoLodResult res;
    auto level0 = [&]() {
        AutoLodLevel l;
        l.mesh = mesh;
        if (skin && skin->Matches(mesh)) l.skin = *skin;
        l.triangles = mesh.TriangleCount();
        l.ratio = l.achievedRatio = 1.0f;
        res.levels.push_back(std::move(l));
    };
    if (mesh.vertices.empty() || mesh.triangles.size() < 3) {
        res.note = "empty mesh: nothing to simplify";
        if (!mesh.vertices.empty()) level0();
        return res;
    }
    // Size of the finite part of the mesh (and refuse enormous coordinates).
    bool any = false;
    Vec3 lo{0, 0, 0}, hi{0, 0, 0};
    for (const Vec3& v : mesh.vertices) {
        if (!Finite(v)) continue;
        if (!any) { lo = hi = v; any = true; continue; }
        lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
        hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
    }
    level0();
    if (!any) { res.note = "no finite vertices"; return res; }
    const double dx = (double)hi.x - lo.x, dy = (double)hi.y - lo.y, dz = (double)hi.z - lo.z;
    const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(diag < kHuge) || std::fabs((double)lo.x) > kHuge || std::fabs((double)hi.x) > kHuge ||
        std::fabs((double)lo.y) > kHuge || std::fabs((double)hi.y) > kHuge ||
        std::fabs((double)lo.z) > kHuge || std::fabs((double)hi.z) > kHuge) {
        res.note = "coordinates are too large to simplify safely";
        return res;
    }
    res.objectSize = (float)diag;

    Mesh clean;
    res.skippedTriangles = CleanMesh(mesh, clean);
    if (clean.TriangleCount() == 0) { res.note = "no usable triangles"; return res; }
    if (res.skippedTriangles > 0) res.note = "some unusable triangles were left out of the lower levels";
    SkinData cleanSkin;
    if (skin && skin->Matches(clean)) cleanSkin = *skin;

    LodChainOptions co = opt.chain;
    co.objectSize = res.objectSize > 0.0f ? res.objectSize : -1.0f;
    co.base.skin = cleanSkin.Empty() ? nullptr : &cleanSkin;
    std::vector<float> ratios;
    for (float r : opt.ratios) if (std::isfinite(r) && r > 0.0f && r < 1.0f) ratios.push_back(r);
    std::sort(ratios.begin(), ratios.end(), [](float a, float b) { return a > b; });
    ratios.erase(std::unique(ratios.begin(), ratios.end()), ratios.end());
    ratios.insert(ratios.begin(), 1.0f);
    LodChain chain = GenerateLodChain(clean, ratios, co);

    const int origTris = std::max(1, mesh.TriangleCount());
    for (std::size_t i = 1; i < chain.levels.size(); ++i) {
        LodMeshLevel& src = chain.levels[i];
        const AutoLodLevel& prev = res.levels.back();
        std::string why;
        if (src.triangles < opt.minTriangles) { ++res.droppedLevels; res.note = "a level was dropped: too few triangles"; break; }
        if (src.triangles >= prev.triangles) { ++res.droppedLevels; continue; }   // no real reduction
        if (!MeshIsSound(src.mesh, &why)) { ++res.droppedLevels; res.note = "a level was dropped: " + why; break; }
        const float frac = res.objectSize > 0.0f ? src.error / res.objectSize : 0.0f;
        if (opt.maxErrorFraction > 0.0f && frac > opt.maxErrorFraction) {
            ++res.droppedLevels;
            res.note = "a level was dropped: error above the budget";
            break;
        }
        AutoLodLevel l;
        l.mesh = std::move(src.mesh);
        l.skin = std::move(src.skin);
        l.ratio = src.ratio;
        l.triangles = src.triangles;
        l.achievedRatio = (float)src.triangles / (float)origTris;
        l.error = src.error;
        l.errorFraction = frac;
        l.switchDistance = std::max(src.switchDistance, prev.switchDistance * 1.0001f + 1e-6f);
        l.enterScreenSize = std::min(1.0f, LodScreenHeightFor(res.objectSize, l.switchDistance, opt.chain.fovYDeg));
        res.levels.push_back(std::move(l));
    }
    for (std::size_t i = 0; i + 1 < res.levels.size(); ++i) res.levels[i].leaveScreenSize = res.levels[i + 1].enterScreenSize;
    res.simplified = res.levels.size() > 1;
    return res;
}

void FillLodGroup(okay::LODGroup& group, const AutoLodResult& lod) {
    group.levels.clear();
    group.lodMeshes.clear();
    const std::size_t n = std::min<std::size_t>(lod.levels.size(), okay::LODGroup::kMaxLevels);
    for (std::size_t i = 0; i < n; ++i) {
        okay::LODGroup::Level lv;
        lv.ratio = lod.levels[i].achievedRatio;
        lv.screenSize = i + 1 < n ? std::clamp(lod.levels[i].leaveScreenSize, 0.001f, 0.99f) : 0.0f;
        lv.distance = i + 1 < n ? lod.levels[i + 1].switchDistance : lod.levels[i].switchDistance * 2.0f + 10.0f;
        group.levels.push_back(lv);
        if (i > 0) group.lodMeshes.push_back(lod.levels[i].mesh);
    }
    group.useScreenSize = true;
    group.lodState = -1;
}

} // namespace okay::optimize
