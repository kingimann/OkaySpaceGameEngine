#include "okay/Modeling/Remesh.hpp"
#include "okay/Modeling/PolyOps.hpp"
#include "okay/Modeling/SelectOps.hpp"
#include "okay/Optimize/MeshSimplify.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace okay::modeling {
namespace {
bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
float AngleAt(const Vec3& prev, const Vec3& at, const Vec3& next) {   // radians, 0..pi
    Vec3 a = prev - at, b = next - at;
    const float la = a.Magnitude(), lb = b.Magnitude();
    if (!(la > 1e-12f) || !(lb > 1e-12f)) return 0.0f;
    float d = Vec3::Dot(a, b) / (la * lb);
    d = std::clamp(d, -1.0f, 1.0f);
    return std::acos(d);
}
} // namespace

PolyMesh TrisToQuads(const Mesh& m, float maxAngleDegrees, float minQuality, int* leftover) {
    PolyMesh pm; pm.verts = m.vertices;
    const int nv = (int)m.vertices.size(), nt = m.TriangleCount();
    std::vector<char> good((std::size_t)std::max(nt, 0), 0);
    std::vector<Vec3> nrm((std::size_t)std::max(nt, 0));
    std::map<std::pair<int, int>, std::vector<int>> edgeTris;   // directed edge a->b -> triangle
    for (int t = 0; t < nt; ++t) {
        const int a = m.triangles[t * 3], b = m.triangles[t * 3 + 1], c = m.triangles[t * 3 + 2];
        if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv || a == b || b == c || a == c) continue;
        if (!Finite(m.vertices[a]) || !Finite(m.vertices[b]) || !Finite(m.vertices[c])) continue;
        Vec3 n = Vec3::Cross(m.vertices[b] - m.vertices[a], m.vertices[c] - m.vertices[a]);
        const float len = n.Magnitude();
        if (!(len > 1e-14f)) continue;
        nrm[t] = n * (1.0f / len); good[t] = 1;
        edgeTris[{a, b}].push_back(t); edgeTris[{b, c}].push_back(t); edgeTris[{c, a}].push_back(t);
    }
    const float cosMax = std::cos(std::clamp(maxAngleDegrees, 0.0f, 180.0f) * 3.14159265f / 180.0f);
    struct Cand { float score; int t1, t2; int quad[4]; };
    std::vector<Cand> cands;
    for (const auto& kv : edgeTris) {
        const int a = kv.first.first, b = kv.first.second;
        if (a > b || kv.second.size() != 1) continue;                      // each edge once, only clean two-triangle edges
        const auto other = edgeTris.find({b, a});
        if (other == edgeTris.end() || other->second.size() != 1) continue;
        const int t1 = kv.second[0], t2 = other->second[0];
        if (t1 == t2 || Vec3::Dot(nrm[t1], nrm[t2]) < cosMax) continue;
        int c = -1, d = -1;
        for (int k = 0; k < 3; ++k) { const int v = m.triangles[t1 * 3 + k]; if (v != a && v != b) c = v; }
        for (int k = 0; k < 3; ++k) { const int v = m.triangles[t2 * 3 + k]; if (v != a && v != b) d = v; }
        if (c < 0 || d < 0 || c == d) continue;
        const int q[4] = {a, d, b, c};                                      // counter-clockwise: a, d, b, c
        const Vec3 qn = nrm[t1] + nrm[t2];
        bool convex = true; float worst = 0.0f;
        for (int k = 0; k < 4; ++k) {
            const Vec3& p0 = m.vertices[q[(k + 3) % 4]]; const Vec3& p1 = m.vertices[q[k]]; const Vec3& p2 = m.vertices[q[(k + 1) % 4]];
            if (Vec3::Dot(Vec3::Cross(p1 - p0, p2 - p1), qn) <= 0.0f) { convex = false; break; }
            worst = std::max(worst, std::fabs(AngleAt(p0, p1, p2) - 1.57079633f));
        }
        if (!convex) continue;
        const float quality = 1.0f - worst / 1.57079633f;                  // 1 = all right angles, 0 = a corner flat or pinched
        if (!(quality >= minQuality)) continue;
        cands.push_back({quality + 0.1f * Vec3::Dot(nrm[t1], nrm[t2]), t1, t2, {q[0], q[1], q[2], q[3]}});
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.score > y.score; });
    std::vector<char> used((std::size_t)std::max(nt, 0), 0);
    for (const Cand& c : cands) {
        if (used[c.t1] || used[c.t2]) continue;
        used[c.t1] = used[c.t2] = 1;
        PolyFace f; f.verts = {c.quad[0], c.quad[1], c.quad[2], c.quad[3]};
        pm.faces.push_back(f);
    }
    int left = 0;
    for (int t = 0; t < nt; ++t) {
        if (!good[t] || used[t]) continue;
        PolyFace f; f.verts = {m.triangles[t * 3], m.triangles[t * 3 + 1], m.triangles[t * 3 + 2]};
        pm.faces.push_back(f); ++left;
    }
    if (leftover) *leftover = left;
    return pm;
}

PolyMesh QuadRemesh(const Mesh& src, const QuadRemeshOptions& opt, QuadRemeshReport* report) {
    QuadRemeshReport rep; rep.inputTriangles = src.TriangleCount();
    auto done = [&](PolyMesh pm) { if (report) *report = rep; return pm; };
    Mesh base;
    base.vertices = src.vertices; base.triangles = src.triangles;
    for (Vec3& v : base.vertices) if (!Finite(v)) v = Vec3{0, 0, 0};
    base.WeldVertices(1e-5f);
    // Keep only triangles that point at real vertices and have an area.
    {
        std::vector<int> tri; const int nv = (int)base.vertices.size();
        for (int t = 0; t < base.TriangleCount(); ++t) {
            const int a = base.triangles[t * 3], b = base.triangles[t * 3 + 1], c = base.triangles[t * 3 + 2];
            if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv || a == b || b == c || a == c) continue;
            tri.insert(tri.end(), {a, b, c});
        }
        base.triangles = tri;
    }
    if (base.TriangleCount() == 0) { rep.error = "The mesh has no usable triangles."; return done(PolyMesh{}); }
    const int target = std::clamp(opt.targetQuads, 8, 100000);
    const int wantTris = opt.allQuads ? std::max(4, target / 2) : std::max(4, target * 2);
    Mesh work = base;
    if (work.TriangleCount() > wantTris) {
        optimize::SimplifyOptions so; so.targetTriangles = wantTris; so.lockBoundary = true;
        optimize::SimplifyResult r = optimize::SimplifyMesh(work, so);
        if (r.mesh.TriangleCount() > 0) { work.vertices = r.mesh.vertices; work.triangles = r.mesh.triangles; }
    }
    rep.simplifiedTriangles = work.TriangleCount();
    int left = 0;
    PolyMesh pm = TrisToQuads(work, 40.0f, 0.3f, &left);
    rep.leftoverTriangles = left; rep.pairedQuads = (int)pm.faces.size() - left;
    if (opt.allQuads) pm = CatmullClark(pm, 1);   // every face becomes quads; points are smoothed, the snap below restores the shape
    const int iters = std::clamp(opt.relaxIterations, 0, 50);
    if (iters > 0 || opt.snapToSource) {
        const int nv = (int)pm.verts.size();
        std::map<std::pair<int, int>, int> edgeUse;
        std::vector<std::set<int>> nb((std::size_t)nv);
        for (const PolyFace& f : pm.faces)
            for (int k = 0; k < f.Size(); ++k) {
                const int a = f.verts[k], b = f.verts[(k + 1) % f.Size()];
                if (a < 0 || b < 0 || a >= nv || b >= nv) continue;
                ++edgeUse[{std::min(a, b), std::max(a, b)}]; nb[a].insert(b); nb[b].insert(a);
            }
        std::vector<char> border((std::size_t)nv, 0);
        for (const auto& kv : edgeUse) if (kv.second == 1) border[kv.first.first] = border[kv.first.second] = 1;
        for (int it = 0; it < std::max(iters, opt.snapToSource ? 1 : 0); ++it) {
            if (it < iters) {
                std::vector<Vec3> np = pm.verts;
                for (int v = 0; v < nv; ++v) {
                    if (border[v] || nb[v].empty()) continue;
                    Vec3 avg{0, 0, 0}; for (int n : nb[v]) avg = avg + pm.verts[n];
                    avg = avg * (1.0f / (float)nb[v].size());
                    np[v] = pm.verts[v] + (avg - pm.verts[v]) * 0.5f;
                }
                pm.verts = np;
            }
            if (opt.snapToSource) {
                Mesh tmp; tmp.vertices = pm.verts; tmp.triangles = {};
                tmp.triangles.push_back(0); tmp.triangles.push_back(0); tmp.triangles.push_back(0);   // Shrinkwrap only moves vertices
                Shrinkwrap(tmp, {}, base, WrapMode::Nearest);
                for (int v = 0; v < nv; ++v) if (Finite(tmp.vertices[v])) pm.verts[v] = tmp.vertices[v];
            }
        }
    }
    for (const PolyFace& f : pm.faces) { if (f.Size() == 4) ++rep.outputQuads; else if (f.Size() == 3) ++rep.outputTriangles; }
    rep.ok = true;
    return done(pm);
}

} // namespace okay::modeling
