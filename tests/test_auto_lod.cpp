// Virtual geometry phase 1: AutoLod (safe LOD chain generator over the quadric simplifier).
#include "test_framework.hpp"
#include "okay/Components/LODGroup.hpp"
#include "okay/Optimize/AutoLod.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>

using namespace okay;
using namespace okay::optimize;

namespace {

struct Topo { int boundary = 0, nonManifold = 0, degenerate = 0, euler = 0; };

// Topology of a position-welded copy (the simplifier welds by position too).
Topo Analyze(const Mesh& m) {
    Topo t;
    std::map<std::tuple<long, long, long>, int> ids;
    std::vector<int> id(m.vertices.size());
    for (std::size_t i = 0; i < m.vertices.size(); ++i) {
        const Vec3& v = m.vertices[i];
        auto key = std::make_tuple(std::lround(v.x * 1e4f), std::lround(v.y * 1e4f), std::lround(v.z * 1e4f));
        auto it = ids.find(key);
        if (it == ids.end()) it = ids.emplace(key, (int)ids.size()).first;
        id[i] = it->second;
    }
    std::map<std::pair<int, int>, int> und;
    std::set<int> used;
    int F = 0;
    for (int f = 0; f < m.TriangleCount(); ++f) {
        const int a = id[m.triangles[f * 3]], b = id[m.triangles[f * 3 + 1]], c = id[m.triangles[f * 3 + 2]];
        if (a == b || b == c || a == c) { ++t.degenerate; continue; }
        ++F;
        used.insert({a, b, c});
        const int e[3][2] = {{a, b}, {b, c}, {c, a}};
        for (auto& p : e) ++und[{std::min(p[0], p[1]), std::max(p[0], p[1])}];
    }
    for (auto& kv : und) { if (kv.second == 1) ++t.boundary; else if (kv.second > 2) ++t.nonManifold; }
    t.euler = (int)used.size() - (int)und.size() + F;
    return t;
}

float PointTriDist(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
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
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return (p - (b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6))))).Magnitude();
    const float den = 1.0f / (va + vb + vc);
    return (p - (a + ab * (vb * den) + ac * (vc * den))).Magnitude();
}

float OneSided(const Mesh& from, const Mesh& to) {
    float worst = 0;
    for (const Vec3& p : from.vertices) {
        float best = 1e30f;
        for (int t = 0; t < to.TriangleCount(); ++t)
            best = std::min(best, PointTriDist(p, to.vertices[to.triangles[t * 3]], to.vertices[to.triangles[t * 3 + 1]], to.vertices[to.triangles[t * 3 + 2]]));
        worst = std::max(worst, best);
    }
    return worst;
}

bool Same(const AutoLodResult& a, const AutoLodResult& b) {
    if (a.levels.size() != b.levels.size()) return false;
    for (std::size_t i = 0; i < a.levels.size(); ++i) {
        const Mesh &x = a.levels[i].mesh, &y = b.levels[i].mesh;
        if (x.triangles != y.triangles || x.vertices.size() != y.vertices.size()) return false;
        for (std::size_t k = 0; k < x.vertices.size(); ++k)
            if (std::memcmp(&x.vertices[k], &y.vertices[k], sizeof(Vec3)) != 0) return false;
        if (a.levels[i].switchDistance != b.levels[i].switchDistance) return false;
    }
    return true;
}

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 6364136223846793005ull + 1442695040888963407ull) {}
    std::uint32_t Next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return (std::uint32_t)(s >> 33); }
    float F() { return (float)(Next() & 0xffffff) / 16777216.0f; }
};

// Every level must be safe to hand to a renderer, whatever went in.
void CheckLevelsSound(const AutoLodResult& r, const Mesh& original) {
    for (std::size_t i = 1; i < r.levels.size(); ++i) {
        std::string why;
        const bool ok = MeshIsSound(r.levels[i].mesh, &why);
        CHECK(ok);
        if (!ok) std::cerr << "    level " << i << ": " << why << "\n";
        CHECK(r.levels[i].triangles == r.levels[i].mesh.TriangleCount());
        CHECK(r.levels[i].triangles < r.levels[i - 1].triangles);
    }
    if (!r.levels.empty()) CHECK(r.levels[0].mesh.triangles == original.triangles);
}

} // namespace

int main() {
    RUN_SUITE("auto lod: default chain on a sphere");
    {
        const Mesh sphere = Mesh::Sphere(0.5f, 24, 36);
        const int full = sphere.TriangleCount();
        AutoLodResult r = GenerateAutoLod(sphere);
        CHECK(r.simplified);
        CHECK(r.levels.size() == 4u);
        CHECK(r.levels[0].triangles == full);
        CHECK(r.levels[0].switchDistance == 0.0f);
        CHECK_NEAR(r.levels[0].enterScreenSize, 1.0f, 1e-6);
        double prevAchieved = 1.01;
        for (std::size_t i = 0; i < r.levels.size(); ++i) {
            const AutoLodLevel& l = r.levels[i];
            std::cout << "  level " << i << ": " << l.triangles << " tris (" << (int)(l.achievedRatio * 100) << "%) error "
                      << l.error << " (" << l.errorFraction * 100 << "% of size) enter screen " << l.enterScreenSize << "\n";
            CHECK(l.achievedRatio < prevAchieved + 1e-6);
            prevAchieved = l.achievedRatio;
            CHECK(l.errorFraction <= 0.05f + 1e-6f);
            if (i > 0) {
                CHECK(l.switchDistance > r.levels[i - 1].switchDistance);
                CHECK(l.enterScreenSize < r.levels[i - 1].enterScreenSize);
                CHECK_NEAR(r.levels[i - 1].leaveScreenSize, l.enterScreenSize, 1e-6);   // thresholds chain up
                CHECK(Analyze(l.mesh).boundary == 0 && Analyze(l.mesh).nonManifold == 0);   // watertight stays watertight
                CHECK(Analyze(l.mesh).euler == 2);
            }
        }
        CHECK(r.levels.back().leaveScreenSize == 0.0f);
        // Measured reduction: the last level is at most ~13% of the triangles.
        CHECK(r.levels.back().triangles <= (int)(full * 0.14f));
        CheckLevelsSound(r, sphere);
        // The reported error is the right order against a measured surface distance.
        for (std::size_t i = 1; i < r.levels.size(); ++i) {
            const float haus = OneSided(r.levels[i].mesh, sphere);
            CHECK(haus <= 2.0f * r.levels[i].error + 0.01f * r.objectSize);
            CHECK(haus <= 0.05f * r.objectSize * 2.0f);   // inside twice the 5% budget
        }
        // Deterministic.
        CHECK(Same(r, GenerateAutoLod(sphere)));
    }

    RUN_SUITE("auto lod: torus keeps its hole, imported-style mesh with UVs");
    {
        const Mesh torus = Mesh::Torus(0.5f, 0.2f, 32, 20);
        AutoLodResult r = GenerateAutoLod(torus);
        CHECK(r.levels.size() >= 3u);
        for (std::size_t i = 1; i < r.levels.size(); ++i) {
            const Topo t = Analyze(r.levels[i].mesh);
            CHECK(t.boundary == 0 && t.nonManifold == 0);
            CHECK(t.euler == 0);                      // genus 1
        }
        std::cout << "  torus " << torus.TriangleCount() << " -> " << r.levels.back().triangles << " triangles\n";
        CHECK(r.levels.back().triangles < torus.TriangleCount() / 4);
        CheckLevelsSound(r, torus);
        // A mesh as importers deliver it: UVs, normals and per-triangle colours.
        Mesh m = Mesh::Sphere(0.5f, 16, 24);
        m.ComputeSmoothNormals();
        m.triColors.assign((std::size_t)m.TriangleCount(), Color::White);
        AutoLodResult ri = GenerateAutoLod(m);
        CHECK(ri.levels.size() >= 3u);
        for (std::size_t i = 1; i < ri.levels.size(); ++i) {
            const Mesh& lm = ri.levels[i].mesh;
            CHECK(lm.HasNormals());
            CHECK(lm.uvs.empty() || lm.HasUVs());
            for (const Vec3& n : lm.normals) CHECK(std::fabs(n.Magnitude() - 1.0f) < 0.05f);
        }
    }

    RUN_SUITE("auto lod: open surface keeps its border");
    {
        const Mesh grid = Mesh::Grid(10, 10, 24, 24);
        AutoLodResult r = GenerateAutoLod(grid);
        CHECK(r.levels.size() >= 3u);
        for (std::size_t i = 1; i < r.levels.size(); ++i) {
            const Mesh& m = r.levels[i].mesh;
            CHECK(Analyze(m).euler == 1);
            // The outline still sits on the original square: no vertex that was on the rim moves inward,
            // and all four corners are still there.
            int corners = 0;
            for (const Vec3& v : m.vertices) {
                if (std::fabs(std::fabs(v.x) - 5.0f) < 1e-4f && std::fabs(std::fabs(v.z) - 5.0f) < 1e-4f) ++corners;
                CHECK_NEAR(v.y, 0.0, 1e-4);
            }
            CHECK(corners >= 4);
        }
        CheckLevelsSound(r, grid);
    }

    RUN_SUITE("auto lod: error budget drops levels");
    {
        const Mesh sphere = Mesh::Sphere(0.5f, 24, 36);
        AutoLodOptions tight;
        tight.maxErrorFraction = 0.002f;
        AutoLodResult t = GenerateAutoLod(sphere, tight);
        AutoLodResult loose = GenerateAutoLod(sphere);
        CHECK(t.levels.size() < loose.levels.size());
        CHECK(t.droppedLevels >= 1);
        for (const AutoLodLevel& l : t.levels) CHECK(l.errorFraction <= 0.002f + 1e-6f);
        AutoLodOptions none;
        none.maxErrorFraction = 0.0f;
        CHECK(GenerateAutoLod(sphere, none).levels.size() == 4u);
    }

    RUN_SUITE("auto lod: hostile meshes");
    {
        Mesh empty;
        AutoLodResult e = GenerateAutoLod(empty);
        CHECK(e.levels.empty() && !e.simplified && !e.note.empty());

        Mesh verts;                       // vertices but no triangles
        verts.vertices = {{0, 0, 0}, {1, 0, 0}};
        CHECK(GenerateAutoLod(verts).levels.size() == 1u);

        Mesh one;
        one.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        one.triangles = {0, 1, 2};
        AutoLodResult o = GenerateAutoLod(one);
        CHECK(o.levels.size() == 1u && !o.simplified);
        CHECK(o.levels[0].triangles == 1);

        Mesh dup = Mesh::Sphere(0.5f, 12, 16);    // every vertex twice, triangles use the copies alternately
        const int n0 = (int)dup.vertices.size();
        for (int i = 0; i < n0; ++i) dup.vertices.push_back(dup.vertices[(std::size_t)i]);
        for (std::size_t i = 0; i < dup.triangles.size(); i += 2) dup.triangles[i] += n0;
        AutoLodResult d = GenerateAutoLod(dup);
        CHECK(d.simplified);
        CheckLevelsSound(d, dup);
        for (std::size_t i = 1; i < d.levels.size(); ++i) CHECK(Analyze(d.levels[i].mesh).euler == 2);

        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        Mesh bad = Mesh::Sphere(0.5f, 12, 16);
        bad.vertices[5].x = nan; bad.vertices[9].y = inf; bad.vertices[11].z = -inf;
        AutoLodResult b = GenerateAutoLod(bad);
        CHECK(b.skippedTriangles > 0);
        CheckLevelsSound(b, bad);

        Mesh allNan = one;
        for (Vec3& v : allNan.vertices) v = {nan, nan, nan};
        CHECK(GenerateAutoLod(allNan).levels.size() <= 1u);

        Mesh huge = Mesh::Sphere(0.5f, 12, 16);
        for (Vec3& v : huge.vertices) v = v * 1e30f;
        AutoLodResult h = GenerateAutoLod(huge);
        CHECK(!h.simplified && !h.note.empty());     // refused safely, not mangled

        Mesh big = Mesh::Sphere(0.5f, 24, 36);       // large but sane (1e6 units)
        for (Vec3& v : big.vertices) v = v * 2e6f;
        AutoLodResult g = GenerateAutoLod(big);
        CHECK(g.simplified);
        CheckLevelsSound(g, big);

        Mesh tiny = Mesh::Sphere(0.5f, 24, 36);      // millimetre-scale
        for (Vec3& v : tiny.vertices) v = v * 1e-5f;
        AutoLodResult ti = GenerateAutoLod(tiny);
        CHECK(ti.levels.size() >= 1u);
        CheckLevelsSound(ti, tiny);

        Mesh idx = Mesh::Sphere(0.5f, 8, 12);        // bad and degenerate indices, odd index count
        idx.triangles.insert(idx.triangles.end(), {0, 9999, 1, -3, 1, 2, 4, 4, 5, 7, 8});
        AutoLodResult ix = GenerateAutoLod(idx);
        CHECK(ix.skippedTriangles >= 3);
        CheckLevelsSound(ix, idx);

        Mesh coincident;                             // all points equal: zero-area triangles only
        coincident.vertices.assign(6, Vec3{1, 1, 1});
        coincident.triangles = {0, 1, 2, 3, 4, 5};
        AutoLodResult c = GenerateAutoLod(coincident);
        CHECK(!c.simplified);

        AutoLodOptions weird;                        // hostile options
        weird.ratios = {0.0f, -1.0f, 5.0f, std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f};
        weird.minTriangles = -5;
        AutoLodResult w = GenerateAutoLod(Mesh::Sphere(0.5f, 16, 24), weird);
        CHECK(w.levels.size() >= 1u);
        CheckLevelsSound(w, Mesh::Sphere(0.5f, 16, 24));
    }

    RUN_SUITE("auto lod: fuzz (run under address and undefined sanitizers in CI)");
    {
        int built = 0, simplified = 0;
        for (int seed = 1; seed <= 150; ++seed) {
            Rng rng((std::uint64_t)seed);
            Mesh m;
            const int kind = (int)(rng.Next() % 5);
            if (kind == 0) m = Mesh::Sphere(0.5f, 4 + (int)(rng.Next() % 14), 4 + (int)(rng.Next() % 20));
            else if (kind == 1) m = Mesh::Torus(0.5f, 0.2f, 6 + (int)(rng.Next() % 16), 4 + (int)(rng.Next() % 10));
            else if (kind == 2) m = Mesh::Grid(4, 4, 2 + (int)(rng.Next() % 14), 2 + (int)(rng.Next() % 14));
            else if (kind == 3) {                                       // random triangle soup
                const int nv = 3 + (int)(rng.Next() % 40);
                for (int i = 0; i < nv; ++i) m.vertices.push_back({rng.F() * 4 - 2, rng.F() * 4 - 2, rng.F() * 4 - 2});
                const int nt = (int)(rng.Next() % 80);
                for (int i = 0; i < nt * 3; ++i) m.triangles.push_back((int)(rng.Next() % (std::uint32_t)(nv + 2)) - 1);   // some out of range
            } else {                                                    // cube with duplicated vertices and a few NaNs
                m = Mesh::Cube();
                const int n0 = (int)m.vertices.size();
                for (int i = 0; i < n0; ++i) m.vertices.push_back(m.vertices[(std::size_t)i]);
                for (std::size_t i = 0; i < m.triangles.size(); ++i) if (rng.Next() & 1) m.triangles[i] += n0;
            }
            const std::uint32_t poison = rng.Next() % 6;                 // sometimes poison a vertex or two
            if (!m.vertices.empty() && poison == 0) m.vertices[rng.Next() % m.vertices.size()].x = std::numeric_limits<float>::quiet_NaN();
            if (!m.vertices.empty() && poison == 1) m.vertices[rng.Next() % m.vertices.size()] = {1e20f, -1e20f, 1e20f};
            if (!m.vertices.empty() && poison == 2) m.vertices[rng.Next() % m.vertices.size()] = m.vertices[0];
            AutoLodOptions o;
            if (rng.Next() % 3 == 0) o.maxErrorFraction = 0.0f;
            if (rng.Next() % 3 == 0) o.ratios = {1.0f, 0.7f, 0.3f, 0.1f, 0.03f};
            AutoLodResult r = GenerateAutoLod(m, o);
            ++built;
            if (r.simplified) ++simplified;
            CheckLevelsSound(r, m);
            CHECK(Same(r, GenerateAutoLod(m, o)));
        }
        std::cout << "  fuzz: " << built << " meshes, " << simplified << " simplified\n";
        CHECK(simplified > 40);
    }

    RUN_SUITE("auto lod: fill a LODGroup (editor hook)");
    {
        const Mesh sphere = Mesh::Sphere(0.5f, 24, 36);
        AutoLodResult r = GenerateAutoLod(sphere);
        LODGroup g;
        FillLodGroup(g, r);
        CHECK(g.levels.size() == r.levels.size());
        CHECK(g.lodMeshes.size() + 1 == g.levels.size());
        CHECK(g.useScreenSize);
        for (std::size_t i = 1; i < g.levels.size(); ++i) {
            CHECK(g.levels[i].screenSize < g.levels[i - 1].screenSize || g.levels[i].screenSize == 0.0f);
            CHECK(g.levels[i].distance > g.levels[i - 1].distance);
            CHECK(g.lodMeshes[i - 1].TriangleCount() == r.levels[i].triangles);
        }
        AutoLodResult none;
        FillLodGroup(g, none);
        CHECK(g.levels.empty() && g.lodMeshes.empty());
    }

    TEST_MAIN_RESULT();
}
