// Optimization tools 27.1: quadric mesh simplification, LOD chains and LOD group selection.
#include "test_framework.hpp"
#include "okay/Optimize/LodGroup.hpp"
#include "okay/Optimize/MeshSimplify.hpp"
#include <cmath>
#include <map>
#include <set>
#include <tuple>

using namespace okay;
using namespace okay::optimize;

namespace {

// ---- test-side topology analysis on a position-welded copy ------------------------------------
struct Topo {
    int V = 0, E = 0, F = 0, boundary = 0, nonManifold = 0, duplicateDirected = 0, degenerate = 0;
    int Euler() const { return V - E + F; }
    bool ClosedManifold() const { return boundary == 0 && nonManifold == 0 && duplicateDirected == 0; }
};

Topo Analyze(const Mesh& m) {
    Topo t;
    std::map<std::tuple<long, long, long>, int> ids;
    std::vector<int> id(m.vertices.size());
    for (std::size_t i = 0; i < m.vertices.size(); ++i) {
        const Vec3& v = m.vertices[i];
        auto key = std::make_tuple(std::lround(v.x * 1e4f), std::lround(v.y * 1e4f), std::lround(v.z * 1e4f));
        auto it = ids.find(key);
        if (it == ids.end()) it = ids.emplace(key, static_cast<int>(ids.size())).first;
        id[i] = it->second;
    }
    std::map<std::pair<int, int>, int> undirected, directed;
    std::set<int> used;
    for (int f = 0; f < m.TriangleCount(); ++f) {
        const int a = id[m.triangles[f * 3]], b = id[m.triangles[f * 3 + 1]], c = id[m.triangles[f * 3 + 2]];
        if (a == b || b == c || a == c) { ++t.degenerate; continue; }
        ++t.F;
        used.insert({a, b, c});
        const int e[3][2] = {{a, b}, {b, c}, {c, a}};
        for (auto& p : e) {
            ++directed[{p[0], p[1]}];
            ++undirected[{std::min(p[0], p[1]), std::max(p[0], p[1])}];
        }
    }
    t.V = static_cast<int>(used.size());
    t.E = static_cast<int>(undirected.size());
    for (auto& kv : undirected) { if (kv.second == 1) ++t.boundary; else if (kv.second > 2) ++t.nonManifold; }
    for (auto& kv : directed) if (kv.second > 1) ++t.duplicateDirected;
    return t;
}

float MeshVolume(const Mesh& m) {
    double v = 0;
    for (int t = 0; t < m.TriangleCount(); ++t) {
        const Vec3& p = m.vertices[m.triangles[t * 3]];
        const Vec3& q = m.vertices[m.triangles[t * 3 + 1]];
        const Vec3& r = m.vertices[m.triangles[t * 3 + 2]];
        v += Vec3::Dot(p, Vec3::Cross(q, r)) / 6.0;
    }
    return static_cast<float>(v);
}

float DistPointTri(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    // Ericson closest point on triangle.
    const Vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = Vec3::Dot(ab, ap), d2 = Vec3::Dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return (p - a).Magnitude();
    const Vec3 bp = p - b;
    const float d3 = Vec3::Dot(ab, bp), d4 = Vec3::Dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return (p - b).Magnitude();
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return (p - (a + ab * (d1 / (d1 - d3)))).Magnitude();
    const Vec3 cp = p - c;
    const float d5 = Vec3::Dot(ab, cp), d6 = Vec3::Dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return (p - c).Magnitude();
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return (p - (a + ac * (d2 / (d2 - d6)))).Magnitude();
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return (p - (b + (c - b) * w)).Magnitude();
    }
    const float den = 1.0f / (va + vb + vc);
    return (p - (a + ab * (vb * den) + ac * (vc * den))).Magnitude();
}

/// Largest distance from any vertex of `from` to the surface of `to`.
float OneSidedDistance(const Mesh& from, const Mesh& to) {
    float worst = 0;
    for (const Vec3& p : from.vertices) {
        float best = 1e30f;
        for (int t = 0; t < to.TriangleCount(); ++t)
            best = std::min(best, DistPointTri(p, to.vertices[to.triangles[t * 3]], to.vertices[to.triangles[t * 3 + 1]], to.vertices[to.triangles[t * 3 + 2]]));
        worst = std::max(worst, best);
    }
    return worst;
}

/// A cube made of n x n quads per face (two triangles each), welded, outward winding.
Mesh GridCube(int n, float size = 1.0f) {
    Mesh m;
    m.name = "GridCube";
    std::map<std::tuple<int, int, int>, int> idx;
    auto vert = [&](int x, int y, int z) {
        auto k = std::make_tuple(x, y, z);
        auto it = idx.find(k);
        if (it != idx.end()) return it->second;
        const float s = size / static_cast<float>(n);
        m.vertices.push_back({-size * 0.5f + x * s, -size * 0.5f + y * s, -size * 0.5f + z * s});
        return idx[k] = static_cast<int>(m.vertices.size()) - 1;
    };
    for (int f = 0; f < 6; ++f) {
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) {
                int c[4][3];
                for (int q = 0; q < 4; ++q) {
                    const int a = i + (q == 1 || q == 2), b = j + (q == 2 || q == 3);
                    switch (f) {
                        case 0: c[q][0] = a; c[q][1] = b; c[q][2] = 0; break;
                        case 1: c[q][0] = a; c[q][1] = b; c[q][2] = n; break;
                        case 2: c[q][0] = 0; c[q][1] = a; c[q][2] = b; break;
                        case 3: c[q][0] = n; c[q][1] = a; c[q][2] = b; break;
                        case 4: c[q][0] = a; c[q][1] = 0; c[q][2] = b; break;
                        default: c[q][0] = a; c[q][1] = n; c[q][2] = b; break;
                    }
                }
                const int v0 = vert(c[0][0], c[0][1], c[0][2]), v1 = vert(c[1][0], c[1][1], c[1][2]);
                const int v2 = vert(c[2][0], c[2][1], c[2][2]), v3 = vert(c[3][0], c[3][1], c[3][2]);
                // Faces 0 (-z), 2 (-x), 5 (+y) wind the other way round to keep every face outward.
                if (f == 0 || f == 2 || f == 5) m.triangles.insert(m.triangles.end(), {v0, v2, v1, v0, v3, v2});
                else m.triangles.insert(m.triangles.end(), {v0, v1, v2, v0, v2, v3});
            }
    }
    return m;
}

SimplifyOptions Ratio(float r) { SimplifyOptions o; o.targetRatio = r; return o; }

float WeightOf(const SkinData& s, int v, int joint) {
    float w = 0;
    for (int k = 0; k < 4; ++k) if (s.joints[v][k] == joint) w += s.weights[v][k];
    return w;
}

bool SameMesh(const Mesh& a, const Mesh& b) {
    if (a.vertices.size() != b.vertices.size() || a.triangles != b.triangles) return false;
    for (std::size_t i = 0; i < a.vertices.size(); ++i)
        if (a.vertices[i].x != b.vertices[i].x || a.vertices[i].y != b.vertices[i].y || a.vertices[i].z != b.vertices[i].z) return false;
    return true;
}

void BoundsOf(const Mesh& m, Vec3& lo, Vec3& hi) {
    lo = hi = m.vertices[0];
    for (const Vec3& v : m.vertices) {
        lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
        hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
    }
}

}  // namespace

int main() {
    RUN_SUITE("optimize_simplify");

    // ---- sphere: closed manifold stays closed, shape, winding and UVs stay put ------------------
    {
        const Mesh sphere = Mesh::Sphere(0.5f, 16, 24);
        const Topo t0 = Analyze(sphere);
        CHECK(t0.ClosedManifold());
        CHECK(t0.Euler() == 2);
        const float area0 = MeshSurfaceArea(sphere);
        const float vol0 = MeshVolume(sphere);
        CHECK(vol0 > 0.4f);

        const float maxHaus[3] = {0.03f, 0.05f, 0.09f};
        int k = 0;
        for (float ratio : {0.5f, 0.25f, 0.1f}) {
            SimplifyResult r = SimplifyMesh(sphere, Ratio(ratio));
            const Topo t = Analyze(r.mesh);
            CHECK(t.ClosedManifold());
            CHECK(t.degenerate == 0);
            CHECK(t.Euler() == 2);                                              // topology unchanged
            CHECK(r.reachedTarget);
            CHECK(r.trianglesAfter == r.mesh.TriangleCount());
            CHECK(r.trianglesBefore == 768);
            CHECK(r.trianglesAfter <= static_cast<int>(768 * ratio) + 1);
            CHECK(r.trianglesAfter >= static_cast<int>(768 * ratio * 0.9f));     // and it does not overshoot far below
            CHECK(r.collapses > 0);
            CHECK(std::fabs(MeshSurfaceArea(r.mesh) - area0) / area0 < 0.05f);   // area within 5 %
            CHECK(MeshVolume(r.mesh) > 0.8f * vol0);                                  // outward winding kept, volume close
            const float haus = OneSidedDistance(sphere, r.mesh);
            CHECK(haus < maxHaus[k]);                                             // geometric deviation
            CHECK(haus <= 2.0f * r.error + 0.005f);                               // the reported error is the right order
            CHECK(r.mesh.HasNormals() && r.mesh.HasUVs());
            for (const Vec3& n : r.mesh.normals) CHECK_NEAR(n.Magnitude(), 1.0, 1e-3);
            ++k;
        }
        // Without the optimal-position solve (endpoint collapses only) it is just as sound.
        SimplifyOptions endpoints = Ratio(0.25f);
        endpoints.optimalPosition = false;
        SimplifyResult e = SimplifyMesh(sphere, endpoints);
        CHECK(Analyze(e.mesh).ClosedManifold());
        CHECK(Analyze(e.mesh).Euler() == 2);
        CHECK(e.trianglesAfter <= 193);
    }

    // ---- UV seams survive: no texture tear, seam vertices stay on the seam ----------------------
    {
        const Mesh sphere = Mesh::Sphere(0.5f, 16, 24);
        for (float ratio : {0.5f, 0.25f, 0.1f}) {
            SimplifyResult r = SimplifyMesh(sphere, Ratio(ratio));
            const Mesh& m = r.mesh;
            const double kPi = 3.14159265358979323846;
            double worstU = 0, worstV = 0;
            for (std::size_t i = 0; i < m.vertices.size(); ++i) {
                const Vec3& p = m.vertices[i];
                const double R = p.Magnitude();
                if (std::fabs(p.y / R) > 0.97) continue;                          // poles have no meaningful u
                const double v = 1.0 - std::acos(std::max(-1.0, std::min(1.0, static_cast<double>(p.y) / R))) / kPi;
                double th = std::atan2(p.z, p.x);
                if (th < 0) th += 2 * kPi;
                double du = std::fabs(th / (2 * kPi) - m.uvs[i].x);
                du = std::min(du, 1.0 - du);                                       // u wraps at the seam
                worstU = std::max(worstU, du);
                worstV = std::max(worstV, std::fabs(v - m.uvs[i].y));
            }
            CHECK(worstU < 0.03);                                                  // UVs still match the surface
            CHECK(worstV < 0.03);
            // No triangle spans the seam in UV space (that would smear the texture across it).
            double span = 0;
            for (int t = 0; t < m.TriangleCount(); ++t) {
                if (std::fabs(m.vertices[m.triangles[t * 3]].y) > 0.45f) continue;
                float lo = 9, hi = -9;
                for (int c = 0; c < 3; ++c) { const float u = m.uvs[m.triangles[t * 3 + c]].x; lo = std::min(lo, u); hi = std::max(hi, u); }
                span = std::max(span, static_cast<double>(hi - lo));
            }
            CHECK(span < 0.5);
            // Vertices on the u = 0 / u = 1 edge of the texture sit on the seam meridian (z = 0, x > 0).
            int seamVerts = 0;
            for (std::size_t i = 0; i < m.vertices.size(); ++i)
                if ((m.uvs[i].x < 0.001f || m.uvs[i].x > 0.999f) && std::fabs(m.vertices[i].y / 0.5f) < 0.97f) {
                    ++seamVerts;
                    CHECK(std::fabs(m.vertices[i].z) < 0.02f && m.vertices[i].x > 0.0f);
                }
            CHECK(seamVerts >= 4);                                                 // the seam is still there
        }
    }

    // ---- hard edges (split normals) are never collapsed away -----------------------------------
    {
        Mesh cube = Mesh::Cube(1.0f);
        cube.ComputeAutoSmoothNormals(30.0f);                                      // 24 vertices, one set of normals per face
        SimplifyResult r = SimplifyMesh(cube, Ratio(0.5f));
        CHECK(r.trianglesAfter == 12);                                             // every edge is a hard edge
        CHECK(!r.reachedTarget);
        SimplifyOptions o = Ratio(0.25f);
        o.lockSeams = true;
        CHECK(SimplifyMesh(cube, o).trianglesAfter == 12);
    }

    // ---- subdivided cube: flat faces and sharp creases cost nothing, corners stay --------------
    {
        const Mesh cube = GridCube(6);
        CHECK(cube.TriangleCount() == 432);
        const Topo t0 = Analyze(cube);
        CHECK(t0.ClosedManifold());
        CHECK(t0.Euler() == 2);
        CHECK_NEAR(MeshVolume(cube), 1.0, 1e-4);
        int expect[3] = {216, 108, 43};
        int k = 0;
        for (float ratio : {0.5f, 0.25f, 0.1f}) {
            SimplifyResult r = SimplifyMesh(cube, Ratio(ratio));
            const Topo t = Analyze(r.mesh);
            CHECK(t.ClosedManifold());
            CHECK(t.Euler() == 2);
            CHECK(r.trianglesAfter <= expect[k]);
            CHECK_NEAR(MeshSurfaceArea(r.mesh), 6.0, 0.01);                        // area unchanged
            CHECK_NEAR(MeshVolume(r.mesh), 1.0, 0.01);
            Vec3 lo, hi;
            BoundsOf(r.mesh, lo, hi);
            CHECK_NEAR(lo.x, -0.5, 1e-4); CHECK_NEAR(hi.x, 0.5, 1e-4);              // corners kept
            CHECK_NEAR(lo.y, -0.5, 1e-4); CHECK_NEAR(hi.y, 0.5, 1e-4);
            CHECK_NEAR(lo.z, -0.5, 1e-4); CHECK_NEAR(hi.z, 0.5, 1e-4);
            CHECK(OneSidedDistance(cube, r.mesh) < 0.01f);
            CHECK(r.error < 0.01f);
            ++k;
        }
        // The 8-vertex cube is simplified only as far as a closed mesh can go (never a collapsed pillow).
        SimplifyResult tiny = SimplifyMesh(Mesh::Cube(1.0f), Ratio(0.01f));
        CHECK(tiny.trianglesAfter >= 4);
        CHECK(Analyze(tiny.mesh).ClosedManifold());
        CHECK(Analyze(tiny.mesh).Euler() == 2);
    }

    // ---- plane grid: boundary preserved ---------------------------------------------------------
    {
        const Mesh grid = Mesh::Grid(10.0f, 10.0f, 16, 16);
        const Topo t0 = Analyze(grid);
        CHECK(t0.boundary == 64);
        CHECK(t0.nonManifold == 0 && t0.duplicateDirected == 0);
        const float up = Vec3::Cross(grid.vertices[grid.triangles[1]] - grid.vertices[grid.triangles[0]],
                                     grid.vertices[grid.triangles[2]] - grid.vertices[grid.triangles[0]]).y;
        for (float ratio : {0.5f, 0.25f, 0.05f}) {
            SimplifyResult r = SimplifyMesh(grid, Ratio(ratio));
            const Topo t = Analyze(r.mesh);
            CHECK(t.nonManifold == 0 && t.duplicateDirected == 0 && t.degenerate == 0);
            CHECK(t.Euler() == 1);                                                  // still one disc
            CHECK(r.trianglesAfter <= static_cast<int>(grid.TriangleCount() * ratio) + 1);
            CHECK_NEAR(MeshSurfaceArea(r.mesh), 100.0, 1e-3);                       // a flat square keeps its area
            Vec3 lo, hi;
            BoundsOf(r.mesh, lo, hi);
            CHECK_NEAR(lo.x, -5.0, 1e-4); CHECK_NEAR(hi.x, 5.0, 1e-4);
            CHECK_NEAR(lo.z, -5.0, 1e-4); CHECK_NEAR(hi.z, 5.0, 1e-4);
            for (const Vec3& v : r.mesh.vertices) CHECK_NEAR(v.y, 0.0, 1e-5);       // stays flat
            for (int f = 0; f < r.mesh.TriangleCount(); ++f) {                      // no flips
                const Vec3 n = Vec3::Cross(r.mesh.vertices[r.mesh.triangles[f * 3 + 1]] - r.mesh.vertices[r.mesh.triangles[f * 3]],
                                           r.mesh.vertices[r.mesh.triangles[f * 3 + 2]] - r.mesh.vertices[r.mesh.triangles[f * 3]]);
                CHECK(n.y * up > 0.0f);
            }
            // Every boundary vertex still lies on the original outline: the outline is never pulled inward.
            int corners = 0;
            for (const Vec3& v : r.mesh.vertices) {
                const bool onEdge = std::fabs(std::fabs(v.x) - 5.0f) < 1e-4f || std::fabs(std::fabs(v.z) - 5.0f) < 1e-4f;
                if (!onEdge) continue;
                if (std::fabs(std::fabs(v.x) - 5.0f) < 1e-4f && std::fabs(std::fabs(v.z) - 5.0f) < 1e-4f) ++corners;
            }
            CHECK(corners == 4);
            CHECK(t.boundary >= 4 && t.boundary <= t0.boundary);
        }
        // lockBoundary keeps every outline vertex exactly where it was.
        SimplifyOptions lock = Ratio(0.1f);
        lock.lockBoundary = true;
        SimplifyResult l = SimplifyMesh(grid, lock);
        CHECK(Analyze(l.mesh).boundary == 64);
        auto edgeSet = [](const Mesh& m) {
            std::set<std::tuple<long, long, long>> s;
            for (const Vec3& v : m.vertices)
                if (std::fabs(std::fabs(v.x) - 5.0f) < 1e-4f || std::fabs(std::fabs(v.z) - 5.0f) < 1e-4f)
                    s.insert(std::make_tuple(std::lround(v.x * 1e3f), std::lround(v.y * 1e3f), std::lround(v.z * 1e3f)));
            return s;
        };
        CHECK(edgeSet(l.mesh) == edgeSet(grid));
        CHECK(l.trianglesAfter < grid.TriangleCount() / 2);                          // the inside still collapses a lot
    }

    // ---- open disc: the rim only loses vertices, the outline keeps its shape --------------------
    {
        const Mesh disc = Mesh::Disc(0.5f, 32);
        SimplifyResult r = SimplifyMesh(disc, Ratio(0.5f));
        const Topo t = Analyze(r.mesh);
        CHECK(t.Euler() == 1);
        CHECK(t.nonManifold == 0);
        CHECK(MeshSurfaceArea(r.mesh) > 0.9f * MeshSurfaceArea(disc));
        for (const Vec3& v : r.mesh.vertices)                                         // everything left is on the disc
            CHECK(v.Magnitude() <= 0.5f + 1e-4f);
        CHECK(r.trianglesAfter <= 17);
    }

    // ---- a hole stays a hole ----------------------------------------------------------------------
    {
        Mesh grid = Mesh::Grid(8.0f, 8.0f, 16, 16);
        // Remove the four central quads (8 triangles): the quad at column c, row r owns triangles 2*(r*16+c), +1.
        std::vector<int> kept;
        for (int f = 0; f < grid.TriangleCount(); ++f) {
            const int q = f / 2, c = q % 16, rr = q / 16;
            if (c >= 7 && c <= 8 && rr >= 7 && rr <= 8) continue;
            kept.insert(kept.end(), {grid.triangles[f * 3], grid.triangles[f * 3 + 1], grid.triangles[f * 3 + 2]});
        }
        grid.triangles = kept;
        const Topo t0 = Analyze(grid);
        CHECK(t0.Euler() == 0);                                                       // an annulus
        SimplifyResult r = SimplifyMesh(grid, Ratio(0.2f));
        const Topo t = Analyze(r.mesh);
        CHECK(t.Euler() == 0);
        CHECK(t.nonManifold == 0 && t.duplicateDirected == 0);
        bool covered = false;                                                         // nothing covers the hole's centre
        for (int f = 0; f < r.mesh.TriangleCount() && !covered; ++f) {
            const Vec3 &a = r.mesh.vertices[r.mesh.triangles[f * 3]], &b = r.mesh.vertices[r.mesh.triangles[f * 3 + 1]], &c = r.mesh.vertices[r.mesh.triangles[f * 3 + 2]];
            auto side = [](const Vec3& p, const Vec3& q) { return (q.x - p.x) * (0.0f - p.z) - (q.z - p.z) * (0.0f - p.x); };
            const float s0 = side(a, b), s1 = side(b, c), s2 = side(c, a);
            covered = (s0 > 0 && s1 > 0 && s2 > 0) || (s0 < 0 && s1 < 0 && s2 < 0);
        }
        CHECK(!covered);
        CHECK(MeshSurfaceArea(r.mesh) > 0.9f * MeshSurfaceArea(grid));
    }

    // ---- genus 1: the torus keeps its hole -------------------------------------------------------
    {
        const Mesh torus = Mesh::Torus(0.5f, 0.2f, 24, 12);
        CHECK(Analyze(torus).Euler() == 0);
        for (float ratio : {0.5f, 0.25f}) {
            SimplifyResult r = SimplifyMesh(torus, Ratio(ratio));
            const Topo t = Analyze(r.mesh);
            CHECK(t.ClosedManifold());
            CHECK(t.Euler() == 0);
            CHECK(std::fabs(MeshSurfaceArea(r.mesh) - MeshSurfaceArea(torus)) / MeshSurfaceArea(torus) < 0.05f);
            CHECK(OneSidedDistance(torus, r.mesh) < 0.05f);
        }
    }

    // ---- per-triangle colours ride along -----------------------------------------------------------
    {
        Mesh sphere = Mesh::Sphere(0.5f, 12, 16);
        for (int t = 0; t < sphere.TriangleCount(); ++t) sphere.triColors.push_back(Color{(t % 3) / 2.0f, 0.5f, 1.0f, 1.0f});
        CHECK(sphere.HasFaceColors());
        SimplifyResult r = SimplifyMesh(sphere, Ratio(0.25f));
        CHECK(r.mesh.HasFaceColors());
        CHECK(static_cast<int>(r.mesh.triColors.size()) == r.mesh.TriangleCount());
        for (const Color& c : r.mesh.triColors) CHECK(c.r == 0.0f || c.r == 0.5f || c.r == 1.0f);
    }

    // ---- skin weights ----------------------------------------------------------------------------
    {
        const Mesh cyl = Mesh::Cylinder(0.5f, 2.0f, 24);
        SkinData skin;
        for (const Vec3& v : cyl.vertices) {
            const float t = std::min(1.0f, std::max(0.0f, (v.y + 1.0f) * 0.5f));
            skin.joints.push_back({0, 1, 0, 0});
            skin.weights.push_back({1.0f - t, t, 0.0f, 0.0f});
        }
        CHECK(skin.Matches(cyl));
        for (float ratio : {0.5f, 0.25f}) {
            SimplifyOptions o = Ratio(ratio);
            o.skin = &skin;
            SimplifyResult r = SimplifyMesh(cyl, o);
            CHECK(r.skinned && r.skinSupported);
            CHECK(r.reachedTarget);
            CHECK(r.skin.Matches(r.mesh));                                           // weights parallel to the new vertices
            CHECK(Analyze(r.mesh).ClosedManifold());
            for (std::size_t i = 0; i < r.mesh.vertices.size(); ++i) {
                const float want = std::min(1.0f, std::max(0.0f, (r.mesh.vertices[i].y + 1.0f) * 0.5f));
                CHECK_NEAR(WeightOf(r.skin, static_cast<int>(i), 1), want, 0.1);     // still blends by height
                float sum = 0;
                for (float w : r.skin.weights[i]) sum += w;
                CHECK_NEAR(sum, 1.0, 1e-3);
            }
        }
        // A skin whose joints change abruptly stops collapses from blending across the change.
        const Mesh sphere = Mesh::Sphere(0.5f, 16, 24);
        SkinData hard;
        for (const Vec3& v : sphere.vertices) { hard.joints.push_back({v.y > 0 ? 1 : 0, 0, 0, 0}); hard.weights.push_back({1, 0, 0, 0}); }
        SimplifyOptions o = Ratio(0.1f);
        o.skin = &hard;
        SimplifyResult r = SimplifyMesh(sphere, o);
        CHECK(r.skin.Matches(r.mesh));
        for (std::size_t i = 0; i < r.mesh.vertices.size(); ++i)
            if (std::fabs(r.mesh.vertices[i].y) > 0.12f) CHECK((r.mesh.vertices[i].y > 0) == (r.skin.joints[i][0] == 1));

        // Unsupported mode: skinned meshes come back untouched with a clear flag.
        SimplifyOptions refuse = Ratio(0.25f);
        refuse.skin = &skin;
        refuse.skinPolicy = SkinPolicy::Refuse;
        SimplifyResult u = SimplifyMesh(cyl, refuse);
        CHECK(u.skinned && !u.skinSupported);
        CHECK(!u.note.empty());
        CHECK(SameMesh(u.mesh, cyl));
        CHECK(u.trianglesAfter == u.trianglesBefore);
        // Skin that does not match the mesh is refused too.
        SkinData wrong = skin;
        wrong.joints.pop_back();
        SimplifyOptions bad = Ratio(0.25f);
        bad.skin = &wrong;
        SimplifyResult b = SimplifyMesh(cyl, bad);
        CHECK(b.skinned && !b.skinSupported);
        CHECK(SameMesh(b.mesh, cyl));
    }

    // ---- target controls, determinism, edge cases -------------------------------------------------
    {
        const Mesh sphere = Mesh::Sphere(0.5f, 16, 24);
        // Absolute triangle target.
        SimplifyOptions abs;
        abs.targetTriangles = 300;
        SimplifyResult a = SimplifyMesh(sphere, abs);
        CHECK(a.trianglesAfter <= 300 && a.trianglesAfter >= 290);
        // Ratio 1 (or more) is the identical mesh.
        CHECK(SameMesh(SimplifyMesh(sphere, Ratio(1.0f)).mesh, sphere));
        CHECK(SameMesh(SimplifyMesh(sphere, Ratio(2.0f)).mesh, sphere));
        // Error limit: stop when the surface would move too much.
        SimplifyOptions lim = Ratio(0.1f);
        lim.maxError = 0.01f;
        SimplifyResult l = SimplifyMesh(sphere, lim);
        CHECK(l.error <= 0.01f + 1e-6f);
        CHECK(!l.reachedTarget);
        CHECK(l.trianglesAfter > 76 && l.trianglesAfter < 768);
        // Error-driven: ratio 1 with a maxError simplifies as far as the error allows.
        SimplifyOptions drv;
        drv.targetRatio = 1.0f;
        drv.maxError = 0.02f;
        SimplifyResult d = SimplifyMesh(sphere, drv);
        CHECK(d.trianglesAfter < 768 && d.error <= 0.02f + 1e-6f);
        SimplifyOptions loose = drv;
        loose.maxError = 0.05f;
        CHECK(SimplifyMesh(sphere, loose).trianglesAfter < d.trianglesAfter);        // a bigger budget removes more
        // Deterministic.
        CHECK(SameMesh(SimplifyMesh(sphere, Ratio(0.2f)).mesh, SimplifyMesh(sphere, Ratio(0.2f)).mesh));
        // Empty, single triangle, bad indices.
        Mesh empty;
        SimplifyResult e = SimplifyMesh(empty, Ratio(0.5f));
        CHECK(e.mesh.vertices.empty() && e.trianglesAfter == 0);
        Mesh one;
        one.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        one.triangles = {0, 1, 2};
        CHECK(SimplifyMesh(one, Ratio(0.1f)).trianglesAfter >= 1);
        Mesh broken = Mesh::Sphere(0.5f, 8, 12);
        broken.triangles.insert(broken.triangles.end(), {0, 9999, 1, -3, 1, 2});
        SimplifyResult br = SimplifyMesh(broken, Ratio(0.5f));
        CHECK(Analyze(br.mesh).nonManifold == 0);
        CHECK(br.trianglesAfter > 0);
    }

    // ---- LOD chain -----------------------------------------------------------------------------------
    {
        const Mesh sphere = Mesh::Sphere(0.5f, 16, 24);
        LodChain chain = GenerateLodChain(sphere, {1.0f, 0.5f, 0.25f, 0.1f});
        CHECK(chain.levels.size() == 4);
        CHECK(chain.objectSize > 1.0f && chain.objectSize < 2.0f);
        CHECK(SameMesh(chain.levels[0].mesh, sphere));                                // LOD0 is the original
        CHECK(chain.levels[0].switchDistance == 0.0f);
        CHECK(chain.levels[0].mesh.name == "Sphere_LOD0");
        CHECK(chain.levels[3].mesh.name == "Sphere_LOD3");
        for (std::size_t i = 1; i < chain.levels.size(); ++i) {
            const LodMeshLevel& prev = chain.levels[i - 1];
            const LodMeshLevel& cur = chain.levels[i];
            CHECK(cur.triangles < prev.triangles);                                    // monotonic, and strictly for a sphere
            CHECK(cur.triangles == cur.mesh.TriangleCount());
            CHECK(cur.error >= prev.error);
            CHECK(cur.switchDistance > prev.switchDistance);                          // switch distances grow
            CHECK(cur.switchScreenHeight < prev.switchScreenHeight);                  // and the screen size they stand for shrinks
            CHECK(Analyze(cur.mesh).ClosedManifold());
        }
        CHECK(chain.levels[3].triangles <= 77);
        // The pixel error knob: a stricter limit pushes every switch farther away.
        LodChainOptions strict;
        strict.pixelError = 0.25f;
        LodChain strictChain = GenerateLodChain(sphere, {1.0f, 0.5f, 0.25f, 0.1f}, strict);
        for (std::size_t i = 1; i < strictChain.levels.size(); ++i) CHECK(strictChain.levels[i].switchDistance > chain.levels[i].switchDistance);
        // Ratios out of order never make a level bigger than the one above it.
        LodChain odd = GenerateLodChain(sphere, {1.0f, 0.1f, 0.5f, 0.25f});
        for (std::size_t i = 1; i < odd.levels.size(); ++i) CHECK(odd.levels[i].triangles <= odd.levels[i - 1].triangles);
        for (std::size_t i = 1; i < odd.levels.size(); ++i) CHECK(odd.levels[i].switchDistance > odd.levels[i - 1].switchDistance);
        // A flat mesh has no error to measure but the chain is still ordered.
        LodChain flat = GenerateLodChain(Mesh::Grid(10, 10, 16, 16), {1.0f, 0.5f, 0.1f});
        CHECK(flat.levels[1].triangles < flat.levels[0].triangles && flat.levels[2].triangles < flat.levels[1].triangles);
        CHECK(flat.levels[2].switchDistance > flat.levels[1].switchDistance && flat.levels[1].switchDistance > 0.0f);
        // Skin goes through the chain.
        SkinData skin;
        for (const Vec3& v : sphere.vertices) { skin.joints.push_back({0, 1, 0, 0}); skin.weights.push_back({0.5f - v.y * 0.5f, 0.5f + v.y * 0.5f, 0, 0}); }
        LodChainOptions so;
        so.base.skin = &skin;
        LodChain sc = GenerateLodChain(sphere, {1.0f, 0.5f, 0.25f}, so);
        CHECK(sc.skinSupported);
        for (const LodMeshLevel& lv : sc.levels) CHECK(lv.skin.Matches(lv.mesh));

        // ---- LOD group built from the chain ----
        LodGroup g = MakeLodGroup(chain, 0.01f);
        CHECK(g.LevelCount() == 4);
        CHECK(g.Valid());
        CHECK(g.levels[0].triangles == 768);
        CHECK_NEAR(g.size, chain.objectSize, 1e-6);
        LodView view;
        CHECK_NEAR(view.FovHeightPx(), 1080.0 / (2.0 * std::tan(30.0 * 3.14159265358979 / 180.0)), 0.5);
        int last = -1;
        bool sawCull = false;
        for (float dist = 0.5f; dist < 2000.0f; dist *= 1.25f) {
            LodSelection s = SelectLod(g, dist, view);
            const int lv = s.culled ? g.LevelCount() : s.level;
            CHECK(lv >= last);                                                         // farther never means more detail
            last = lv;
            sawCull = sawCull || s.culled;
            if (s.culled) CHECK(s.level == -1);
        }
        CHECK(sawCull);
        CHECK(SelectLod(g, 0.5f, view).level == 0);
        CHECK(SelectLod(g, 1.0e5f, view).culled);
        // The distance each level is left at matches what the selector does.
        for (int i = 0; i + 1 < g.LevelCount(); ++i) {
            const float d = LodSwitchDistance(g, i, view);
            CHECK_NEAR(LodGroupScreenHeight(g, d, view), g.levels[i].screenHeight, 1e-4);
            CHECK(SelectLod(g, d * 0.98f, view).level <= i);
            CHECK(SelectLod(g, d * 1.02f, view).level > i);
        }
        // Bias: a larger bias keeps detail longer, a smaller one drops it sooner.
        for (float dist : {5.0f, 15.0f, 40.0f, 100.0f}) {
            CHECK(SelectLod(g, dist, view, 2.0f).level <= SelectLod(g, dist, view, 1.0f).level || SelectLod(g, dist, view, 1.0f).culled);
            const LodSelection hi = SelectLod(g, dist, view, 2.0f), lo = SelectLod(g, dist, view, 0.5f);
            const int a = hi.culled ? 99 : hi.level, b = lo.culled ? 99 : lo.level;
            CHECK(a <= b);
        }
    }

    // ---- LOD selection: hysteresis, cross-fade, cull ---------------------------------------------------
    {
        LodGroup g;
        g.size = 2.0f;
        g.levels.resize(3);
        g.levels[0].screenHeight = 0.5f;
        g.levels[1].screenHeight = 0.25f;
        g.cullScreenHeight = 0.05f;
        g.hysteresis = 0.1f;
        CHECK(g.Valid());
        // Without history the thresholds are exact.
        CHECK(SelectLodByHeight(g, 0.51f).level == 0);
        CHECK(SelectLodByHeight(g, 0.49f).level == 1);
        CHECK(SelectLodByHeight(g, 0.24f).level == 2);
        CHECK(SelectLodByHeight(g, 0.049f).culled);
        // With history, small movements around a threshold do not switch.
        CHECK(SelectLodByHeight(g, 0.48f, 0).level == 0);                              // 0.48 >= 0.5 * 0.9
        CHECK(SelectLodByHeight(g, 0.44f, 0).level == 1);                              // 0.44 <  0.45: leave
        CHECK(SelectLodByHeight(g, 0.52f, 1).level == 1);                              // 0.52 <  0.55: stay
        CHECK(SelectLodByHeight(g, 0.56f, 1).level == 0);                              // 0.56 >= 0.55: come back
        // Wobble +-4 % around a threshold: one decision, no flicker.
        int state = 0, switches = 0;
        for (int i = 0; i < 200; ++i) {
            const float h = 0.5f + 0.02f * std::sin(i * 0.9f);
            const int before = state;
            state = SelectLodByHeight(g, h, state).state;
            if (state != before) ++switches;
        }
        CHECK(switches == 0);
        state = 0; switches = 0;
        for (int i = 0; i < 200; ++i) {
            const float h = 0.5f + 0.02f * std::sin(i * 0.9f);
            const int before = state;
            state = SelectLodByHeight([&] { LodGroup z = g; z.hysteresis = 0.0f; return z; }(), h, state).state;
            if (state != before) ++switches;
        }
        CHECK(switches > 4);                                                           // and without hysteresis it would
        // A big move can skip levels.
        CHECK(SelectLodByHeight(g, 0.2f, 0).level == 2);
        CHECK(SelectLodByHeight(g, 0.9f, 2).level == 0);
        // Cull with history: leave at 0.045, come back at 0.055.
        CHECK(SelectLodByHeight(g, 0.047f, 2).level == 2);
        CHECK(SelectLodByHeight(g, 0.044f, 2).culled);
        CHECK(SelectLodByHeight(g, 0.052f, SelectLodByHeight(g, 0.044f, 2).state).culled);
        CHECK(SelectLodByHeight(g, 0.056f, SelectLodByHeight(g, 0.044f, 2).state).level == 2);
        // Never culled when cullScreenHeight is 0.
        LodGroup keep = g;
        keep.cullScreenHeight = 0.0f;
        CHECK(!SelectLodByHeight(keep, 1e-6f).culled && SelectLodByHeight(keep, 1e-6f).level == 2);
        // Minimum pixel height culls tiny objects.
        LodGroup px = keep;
        px.minPixelHeight = 8.0f;
        LodView view;
        const float tinyDist = LodDistanceFor(px.size, 4.0f / 1080.0f, view.fovYDeg);   // 4 px tall
        const float bigDist = LodDistanceFor(px.size, 40.0f / 1080.0f, view.fovYDeg);   // 40 px tall
        CHECK(SelectLod(px, tinyDist, view).culled);
        CHECK(!SelectLod(px, bigDist, view).culled);
        // Cross-fade: blends toward the next level as a threshold approaches.
        LodGroup fade = g;
        fade.hysteresis = 0.0f;
        fade.fadeMode = LodFadeMode::CrossFade;
        fade.fadeWidth = 0.2f;
        LodSelection f0 = SelectLodByHeight(fade, 0.7f);
        CHECK(f0.level == 0 && f0.fadeTo == -1 && f0.fade == 0.0f);
        LodSelection f1 = SelectLodByHeight(fade, 0.55f);                               // halfway through the 0.5..0.6 band
        CHECK(f1.level == 0 && f1.fadeTo == 1);
        CHECK_NEAR(f1.fade, 0.5, 1e-4);
        LodSelection f2 = SelectLodByHeight(fade, 0.5f);
        CHECK(f2.level == 0 && f2.fadeTo == 1);
        CHECK_NEAR(f2.fade, 1.0, 1e-4);
        LodSelection f3 = SelectLodByHeight(fade, 0.499f);                              // switched, and the fade restarts at 0
        CHECK(f3.level == 1 && f3.fade == 0.0f);
        // The fade never goes backwards as the object shrinks.
        float prevFade = -1.0f;
        int prevLevel = 0;
        for (float h = 0.8f; h > 0.3f; h -= 0.005f) {
            const LodSelection s = SelectLodByHeight(fade, h);
            if (s.level == prevLevel) CHECK(s.fade >= prevFade - 1e-6f);
            prevFade = s.fade;
            prevLevel = s.level;
        }
        // No cross-fade by default.
        CHECK(SelectLodByHeight(g, 0.52f).fadeTo == -1);
        // Empty groups cull, invalid ones say so.
        LodGroup none;
        CHECK(SelectLodByHeight(none, 0.5f).culled && !none.Valid());
        LodGroup unordered = g;
        unordered.levels[1].screenHeight = 0.7f;
        CHECK(!unordered.Valid());
        // Distance <-> height helpers invert each other.
        CHECK_NEAR(LodScreenHeightFor(2.0f, LodDistanceFor(2.0f, 0.3f, 60.0f), 60.0f), 0.3, 1e-4);
    }

    TEST_MAIN_RESULT();
}
