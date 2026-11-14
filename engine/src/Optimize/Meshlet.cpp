// Meshlet (cluster) builder and CPU culling reference. See Meshlet.hpp.
#include "okay/Optimize/Meshlet.hpp"
#include <algorithm>
#include <cmath>

namespace okay::optimize {
namespace {

struct D3 { double x = 0, y = 0, z = 0; };
D3 ToD(const Vec3& v) { return {v.x, v.y, v.z}; }
D3 Sub(const D3& a, const D3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
double Dot(const D3& a, const D3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
D3 Cross(const D3& a, const D3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double Len(const D3& a) { return std::sqrt(Dot(a, a)); }
bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

constexpr double kHuge = 1e15;

struct Tri { int src, a, b, c; D3 centroid; D3 normal; /* unit */ };

std::uint32_t Part1By2(std::uint32_t x) {
    x &= 0x3ff;
    x = (x | (x << 16)) & 0x030000FF;
    x = (x | (x << 8)) & 0x0300F00F;
    x = (x | (x << 4)) & 0x030C30C3;
    x = (x | (x << 2)) & 0x09249249;
    return x;
}

} // namespace

MeshletSet BuildMeshlets(const Mesh& mesh, const MeshletOptions& opt) {
    MeshletSet out;
    out.sourceTriangles = mesh.TriangleCount();
    const int maxV = std::min(std::max(opt.maxVertices, 3), kMeshletMaxVertices);
    const int maxT = std::min(std::max(opt.maxTriangles, 1), kMeshletMaxTriangles);
    const int nv = (int)mesh.vertices.size();

    // ---- usable triangles -------------------------------------------------------------------
    std::vector<Tri> tris;
    tris.reserve((std::size_t)out.sourceTriangles);
    D3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    for (int t = 0; t < out.sourceTriangles; ++t) {
        const int a = mesh.triangles[(std::size_t)t * 3], b = mesh.triangles[(std::size_t)t * 3 + 1], c = mesh.triangles[(std::size_t)t * 3 + 2];
        if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv || a == b || b == c || a == c) { ++out.skippedTriangles; continue; }
        const Vec3 &pa = mesh.vertices[(std::size_t)a], &pb = mesh.vertices[(std::size_t)b], &pc = mesh.vertices[(std::size_t)c];
        if (!Finite(pa) || !Finite(pb) || !Finite(pc) ||
            std::fabs((double)pa.x) > kHuge || std::fabs((double)pa.y) > kHuge || std::fabs((double)pa.z) > kHuge ||
            std::fabs((double)pb.x) > kHuge || std::fabs((double)pb.y) > kHuge || std::fabs((double)pb.z) > kHuge ||
            std::fabs((double)pc.x) > kHuge || std::fabs((double)pc.y) > kHuge || std::fabs((double)pc.z) > kHuge) {
            ++out.skippedTriangles; continue;
        }
        const D3 A = ToD(pa), B = ToD(pb), C = ToD(pc);
        D3 n = Cross(Sub(B, A), Sub(C, A));
        const double len = Len(n);
        if (!(len > 0.0) || !std::isfinite(len)) { ++out.skippedTriangles; continue; }
        n = {n.x / len, n.y / len, n.z / len};
        Tri tr{t, a, b, c, {(A.x + B.x + C.x) / 3, (A.y + B.y + C.y) / 3, (A.z + B.z + C.z) / 3}, n};
        lo = {std::min(lo.x, tr.centroid.x), std::min(lo.y, tr.centroid.y), std::min(lo.z, tr.centroid.z)};
        hi = {std::max(hi.x, tr.centroid.x), std::max(hi.y, tr.centroid.y), std::max(hi.z, tr.centroid.z)};
        tris.push_back(tr);
    }
    const int nt = (int)tris.size();
    if (nt == 0) return out;

    // ---- vertex -> triangles (CSR) ------------------------------------------------------------
    std::vector<int> start((std::size_t)nv + 1, 0);
    for (const Tri& t : tris) { ++start[(std::size_t)t.a + 1]; ++start[(std::size_t)t.b + 1]; ++start[(std::size_t)t.c + 1]; }
    for (int i = 0; i < nv; ++i) start[(std::size_t)i + 1] += start[(std::size_t)i];
    std::vector<int> adj((std::size_t)start[(std::size_t)nv]);
    {
        std::vector<int> fill(start.begin(), start.end() - 1);
        for (int i = 0; i < nt; ++i) {
            adj[(std::size_t)fill[(std::size_t)tris[(std::size_t)i].a]++] = i;
            adj[(std::size_t)fill[(std::size_t)tris[(std::size_t)i].b]++] = i;
            adj[(std::size_t)fill[(std::size_t)tris[(std::size_t)i].c]++] = i;
        }
    }

    // ---- seed order: Morton code of the triangle centre ---------------------------------------
    std::vector<std::pair<std::uint32_t, int>> order((std::size_t)nt);
    for (int i = 0; i < nt; ++i) {
        const D3& c = tris[(std::size_t)i].centroid;
        auto q = [](double v, double l, double h) {
            const double e = h - l;
            const double f = e > 0.0 ? (v - l) / e : 0.0;
            return (std::uint32_t)std::min(1023.0, std::max(0.0, f * 1023.0));
        };
        const std::uint32_t code = Part1By2(q(c.x, lo.x, hi.x)) | (Part1By2(q(c.y, lo.y, hi.y)) << 1) | (Part1By2(q(c.z, lo.z, hi.z)) << 2);
        order[(std::size_t)i] = {code, i};
    }
    std::sort(order.begin(), order.end());

    // ---- grow clusters ------------------------------------------------------------------------
    std::vector<char> done((std::size_t)nt, 0);
    std::vector<int> candStamp((std::size_t)nt, -1);
    std::vector<int> localOf((std::size_t)nv, -1);   // vertex -> local index in the open meshlet
    std::size_t seedPos = 0;
    int meshletNo = 0;
    std::vector<int> cand;
    while (true) {
        while (seedPos < order.size() && done[(std::size_t)order[seedPos].second]) ++seedPos;
        if (seedPos >= order.size()) break;
        Meshlet m;
        m.vertexOffset = (std::uint32_t)out.vertices.size();
        m.triangleOffset = (std::uint32_t)out.sourceTriangle.size();
        std::vector<int> mtris;
        cand.clear();
        D3 sum{0, 0, 0};
        int vcount = 0;
        auto newVerts = [&](const Tri& t) {
            int n = 0;
            const int v[3] = {t.a, t.b, t.c};
            for (int k = 0; k < 3; ++k) if (localOf[(std::size_t)v[k]] < 0 && !(k > 0 && v[k] == v[0]) && !(k > 1 && v[k] == v[1])) ++n;
            return n;
        };
        auto addTri = [&](int ti) {
            const Tri& t = tris[(std::size_t)ti];
            done[(std::size_t)ti] = 1;
            const int v[3] = {t.a, t.b, t.c};
            for (int k = 0; k < 3; ++k) {
                if (localOf[(std::size_t)v[k]] < 0) {
                    localOf[(std::size_t)v[k]] = vcount++;
                    out.vertices.push_back((std::uint32_t)v[k]);
                }
            }
            for (int k = 0; k < 3; ++k) out.triangles.push_back((std::uint8_t)localOf[(std::size_t)v[k]]);
            out.sourceTriangle.push_back(t.src);
            mtris.push_back(ti);
            sum.x += t.centroid.x; sum.y += t.centroid.y; sum.z += t.centroid.z;
            for (int k = 0; k < 3; ++k)
                for (int j = start[(std::size_t)v[k]]; j < start[(std::size_t)v[k] + 1]; ++j) {
                    const int o = adj[(std::size_t)j];
                    if (!done[(std::size_t)o] && candStamp[(std::size_t)o] != meshletNo) { candStamp[(std::size_t)o] = meshletNo; cand.push_back(o); }
                }
        };
        addTri(order[seedPos].second);
        while ((int)mtris.size() < maxT) {
            const double inv = 1.0 / (double)mtris.size();
            const D3 cen{sum.x * inv, sum.y * inv, sum.z * inv};
            int best = -1, bestShared = -1;
            double bestD = 0.0;
            for (std::size_t i = 0; i < cand.size(); ++i) {
                const int ti = cand[i];
                if (done[(std::size_t)ti]) continue;
                const Tri& t = tris[(std::size_t)ti];
                const int nvNew = newVerts(t);
                if (vcount + nvNew > maxV) continue;
                const int shared = 3 - nvNew;
                const D3 d = Sub(t.centroid, cen);
                const double d2 = Dot(d, d);
                if (best < 0 || shared > bestShared || (shared == bestShared && (d2 < bestD || (d2 == bestD && ti < best)))) {
                    best = ti; bestShared = shared; bestD = d2;
                }
            }
            if (best < 0) break;
            addTri(best);
            cand.erase(std::remove_if(cand.begin(), cand.end(), [&](int x) { return done[(std::size_t)x] != 0; }), cand.end());
        }
        m.vertexCount = (std::uint32_t)vcount;
        m.triangleCount = (std::uint32_t)mtris.size();

        // Bounding sphere: box centre, farthest point (double maths, then a hair of slack for float rounding).
        D3 bl{1e300, 1e300, 1e300}, bh{-1e300, -1e300, -1e300};
        for (std::uint32_t k = 0; k < m.vertexCount; ++k) {
            const D3 p = ToD(mesh.vertices[out.vertices[m.vertexOffset + k]]);
            bl = {std::min(bl.x, p.x), std::min(bl.y, p.y), std::min(bl.z, p.z)};
            bh = {std::max(bh.x, p.x), std::max(bh.y, p.y), std::max(bh.z, p.z)};
        }
        const Vec3 c{(float)((bl.x + bh.x) * 0.5), (float)((bl.y + bh.y) * 0.5), (float)((bl.z + bh.z) * 0.5)};
        double r2 = 0.0;
        for (std::uint32_t k = 0; k < m.vertexCount; ++k) {
            const D3 d = Sub(ToD(mesh.vertices[out.vertices[m.vertexOffset + k]]), ToD(c));
            r2 = std::max(r2, Dot(d, d));
        }
        m.center = c;
        const double r = std::sqrt(r2);
        m.radius = (float)(r * (1.0 + 1e-6) + 1e-6 * (std::fabs((double)c.x) + std::fabs((double)c.y) + std::fabs((double)c.z)));

        // Normal cone: mean facing, widest deviation. Too wide (> ~84 degrees) means no cone.
        D3 axis{0, 0, 0};
        for (int ti : mtris) { const D3& n = tris[(std::size_t)ti].normal; axis = {axis.x + n.x, axis.y + n.y, axis.z + n.z}; }
        const double al = Len(axis);
        m.coneCutoff = 1.0f;
        if (al > 1e-9) {
            axis = {axis.x / al, axis.y / al, axis.z / al};
            double mindp = 1.0;
            for (int ti : mtris) mindp = std::min(mindp, Dot(axis, tris[(std::size_t)ti].normal));
            mindp -= 1e-5;   // float safety margin: the cone only gets wider
            if (mindp > 0.1) {
                m.coneAxis = {(float)axis.x, (float)axis.y, (float)axis.z};
                m.coneCutoff = (float)std::sqrt(std::max(0.0, 1.0 - mindp * mindp));
                m.coneCutoff = std::min(1.0f, m.coneCutoff * 1.00001f + 1e-6f);
            }
        }
        for (std::uint32_t k = 0; k < m.vertexCount; ++k) localOf[out.vertices[m.vertexOffset + k]] = -1;
        out.meshlets.push_back(m);
        ++meshletNo;
    }
    return out;
}

namespace {

double WorldScale(const Mat4& w) {
    const Vec3 v = w.MultiplyVector({1, 0, 0});
    return std::sqrt((double)v.x * v.x + (double)v.y * v.y + (double)v.z * v.z);
}

double PlaneDist(const Frustum& f, int i, const D3& p) {
    return (double)f.pl[i][0] * p.x + (double)f.pl[i][1] * p.y + (double)f.pl[i][2] * p.z + (double)f.pl[i][3];
}

} // namespace

MeshletCullStats CullMeshlets(const MeshletSet& set, const MeshletCullParams& p, std::vector<std::uint32_t>& visible) {
    MeshletCullStats st;
    visible.clear();
    st.total = (int)set.meshlets.size();
    const double sc = WorldScale(p.world);
    const D3 cam = ToD(p.cameraPos);
    for (std::size_t i = 0; i < set.meshlets.size(); ++i) {
        const Meshlet& m = set.meshlets[i];
        const D3 c = ToD(p.world.MultiplyPoint(m.center));
        const double r = (double)m.radius * sc;
        if (p.useFrustum) {
            bool out = false;
            for (int k = 0; k < 6 && !out; ++k) out = PlaneDist(p.frustum, k, c) < -r;
            if (out) { ++st.culledFrustum; continue; }
        }
        const D3 w = Sub(c, cam);
        const double wl = Len(w);
        if (p.useDistance && p.maxDistance > 0.0f && wl - r > (double)p.maxDistance) { ++st.culledDistance; continue; }
        if (p.useCone && m.coneCutoff < 1.0f) {
            const Vec3 ax = p.world.MultiplyVector(m.coneAxis);
            const D3 a = ToD(ax);
            const double al = Len(a);
            if (al > 0.0 && Dot(w, a) / al >= (double)m.coneCutoff * wl + r) { ++st.culledCone; continue; }
        }
        visible.push_back((std::uint32_t)i);
        st.visibleTriangles += (int)m.triangleCount;
    }
    st.visible = (int)visible.size();
    return st;
}

std::vector<int> BruteForceVisibleTriangles(const Mesh& mesh, const MeshletCullParams& p) {
    std::vector<int> res;
    const int nv = (int)mesh.vertices.size();
    const D3 cam = ToD(p.cameraPos);
    for (int t = 0; t < mesh.TriangleCount(); ++t) {
        const int a = mesh.triangles[(std::size_t)t * 3], b = mesh.triangles[(std::size_t)t * 3 + 1], c = mesh.triangles[(std::size_t)t * 3 + 2];
        if (a < 0 || b < 0 || c < 0 || a >= nv || b >= nv || c >= nv || a == b || b == c || a == c) continue;
        const Vec3 &va = mesh.vertices[(std::size_t)a], &vb = mesh.vertices[(std::size_t)b], &vc = mesh.vertices[(std::size_t)c];
        if (!Finite(va) || !Finite(vb) || !Finite(vc)) continue;
        if (std::max({std::fabs((double)va.x), std::fabs((double)va.y), std::fabs((double)va.z), std::fabs((double)vb.x), std::fabs((double)vb.y),
                      std::fabs((double)vb.z), std::fabs((double)vc.x), std::fabs((double)vc.y), std::fabs((double)vc.z)}) > kHuge) continue;
        const D3 w[3] = {ToD(p.world.MultiplyPoint(va)), ToD(p.world.MultiplyPoint(vb)), ToD(p.world.MultiplyPoint(vc))};
        // Same usability rule as the builder: zero-area triangles are not part of any meshlet.
        const D3 ma = ToD(va), mb = ToD(vb), mc = ToD(vc);
        const double area = Len(Cross(Sub(mb, ma), Sub(mc, ma)));
        if (!(area > 0.0) || !std::isfinite(area)) continue;
        if (p.useFrustum) {
            bool out = false;
            for (int k = 0; k < 6 && !out; ++k)
                out = PlaneDist(p.frustum, k, w[0]) < 0 && PlaneDist(p.frustum, k, w[1]) < 0 && PlaneDist(p.frustum, k, w[2]) < 0;
            if (out) continue;
        }
        if (p.useDistance && p.maxDistance > 0.0f) {
            const double d = std::min({Len(Sub(w[0], cam)), Len(Sub(w[1], cam)), Len(Sub(w[2], cam))});
            if (d > (double)p.maxDistance) continue;
        }
        if (p.useCone) {
            const D3 n = Cross(Sub(w[1], w[0]), Sub(w[2], w[0]));
            if (Dot(n, Sub(cam, w[0])) <= 0.0) continue;   // faces away
        }
        res.push_back(t);
    }
    return res;
}

std::vector<int> TrianglesOfMeshlets(const MeshletSet& set, const std::vector<std::uint32_t>& ids) {
    std::vector<int> r;
    for (std::uint32_t id : ids) {
        if (id >= set.meshlets.size()) continue;
        const Meshlet& m = set.meshlets[id];
        for (std::uint32_t k = 0; k < m.triangleCount; ++k) r.push_back(set.sourceTriangle[m.triangleOffset + k]);
    }
    std::sort(r.begin(), r.end());
    return r;
}

std::string ValidateMeshlets(const Mesh& mesh, const MeshletSet& set) {
    const int nv = (int)mesh.vertices.size();
    std::vector<char> seen((std::size_t)std::max(0, set.sourceTriangles), 0);
    std::size_t tcount = 0;
    for (std::size_t i = 0; i < set.meshlets.size(); ++i) {
        const Meshlet& m = set.meshlets[i];
        if (m.vertexCount == 0 || m.vertexCount > (std::uint32_t)kMeshletMaxVertices) return "meshlet vertex count out of range";
        if (m.triangleCount == 0 || m.triangleCount > (std::uint32_t)kMeshletMaxTriangles) return "meshlet triangle count out of range";
        if ((std::size_t)m.vertexOffset + m.vertexCount > set.vertices.size()) return "vertex list overrun";
        if (((std::size_t)m.triangleOffset + m.triangleCount) * 3 > set.triangles.size()) return "triangle list overrun";
        if ((std::size_t)m.triangleOffset + m.triangleCount > set.sourceTriangle.size()) return "source list overrun";
        const double cl = Len(ToD(m.coneAxis));
        if (m.coneCutoff < 1.0f && std::fabs(cl - 1.0) > 1e-3) return "cone axis is not a unit vector";
        for (std::uint32_t k = 0; k < m.vertexCount; ++k) {
            const std::uint32_t v = set.vertices[m.vertexOffset + k];
            if (v >= (std::uint32_t)nv) return "vertex index out of range";
            const D3 d = Sub(ToD(mesh.vertices[v]), ToD(m.center));
            if (Len(d) > (double)m.radius) return "a vertex lies outside its sphere";
        }
        for (std::uint32_t k = 0; k < m.triangleCount; ++k) {
            const std::uint8_t* l = &set.triangles[((std::size_t)m.triangleOffset + k) * 3];
            for (int j = 0; j < 3; ++j) if (l[j] >= m.vertexCount) return "local index out of range";
            const int src = set.sourceTriangle[m.triangleOffset + k];
            if (src < 0 || src >= set.sourceTriangles) return "source triangle out of range";
            if (seen[(std::size_t)src]) return "a triangle is in two meshlets";
            seen[(std::size_t)src] = 1;
            ++tcount;
            // The local indices must name the same mesh vertices as the source triangle.
            for (int j = 0; j < 3; ++j)
                if ((int)set.vertices[m.vertexOffset + l[j]] != mesh.triangles[(std::size_t)src * 3 + j]) return "local triangle differs from the source triangle";
            if (m.coneCutoff < 1.0f) {
                const D3 a = ToD(mesh.vertices[(std::size_t)mesh.triangles[(std::size_t)src * 3]]);
                const D3 n = Cross(Sub(ToD(mesh.vertices[(std::size_t)mesh.triangles[(std::size_t)src * 3 + 1]]), a),
                                   Sub(ToD(mesh.vertices[(std::size_t)mesh.triangles[(std::size_t)src * 3 + 2]]), a));
                const double nl = Len(n);
                if (nl > 0.0) {
                    const double dp = Dot(ToD(m.coneAxis), n) / nl;
                    const double sinMax = m.coneCutoff;
                    if (std::sqrt(std::max(0.0, 1.0 - sinMax * sinMax)) > dp + 1e-4) return "a normal lies outside its cone";
                }
            }
        }
    }
    if ((int)tcount + set.skippedTriangles != set.sourceTriangles) return "triangle count does not add up";
    return std::string();
}

} // namespace okay::optimize
