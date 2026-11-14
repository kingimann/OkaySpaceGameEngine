// Virtual geometry phase 2 start: meshlet builder and the CPU culling reference.
#include "test_framework.hpp"
#include "okay/Optimize/Meshlet.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace okay;
using namespace okay::optimize;

namespace {

struct Rng {
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s(seed * 6364136223846793005ull + 1442695040888963407ull) {}
    std::uint32_t Next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return (std::uint32_t)(s >> 33); }
    float F() { return (float)(Next() & 0xffffff) / 16777216.0f; }
    float R(float a, float b) { return a + (b - a) * F(); }
};

bool SameSet(const MeshletSet& a, const MeshletSet& b) {
    return a.vertices == b.vertices && a.triangles == b.triangles && a.sourceTriangle == b.sourceTriangle &&
           a.meshlets.size() == b.meshlets.size() &&
           (a.meshlets.empty() || std::memcmp(a.meshlets.data(), b.meshlets.data(), a.meshlets.size() * sizeof(Meshlet)) == 0);
}

void CheckBuild(const Mesh& m, const char* what) {
    MeshletSet s = BuildMeshlets(m);
    const std::string err = ValidateMeshlets(m, s);
    CHECK(err.empty());
    if (!err.empty()) std::cerr << "    " << what << ": " << err << "\n";
    for (const Meshlet& ml : s.meshlets) CHECK(ml.vertexCount <= 64 && ml.triangleCount <= 126);
}

MeshletCullParams MakeCam(const Vec3& eye, const Vec3& target, float fov, float farZ) {
    MeshletCullParams p;
    const Mat4 view = Mat4::LookAt(eye, target, {0, 1, 0});
    const Mat4 proj = Mat4::Perspective(fov, 1.0f, 0.1f, farZ);
    p.frustum = Frustum::FromVP(proj * view);
    p.cameraPos = eye;
    return p;
}

// Compare cluster culling with the per-triangle truth. Returns false positives (extra triangles kept).
struct Cmp { int truth = 0, kept = 0, missed = 0, extra = 0; };
Cmp Compare(const Mesh& m, const MeshletSet& s, const MeshletCullParams& p) {
    std::vector<std::uint32_t> vis;
    CullMeshlets(s, p, vis);
    const std::vector<int> kept = TrianglesOfMeshlets(s, vis);
    const std::vector<int> truth = BruteForceVisibleTriangles(m, p);
    Cmp c;
    c.truth = (int)truth.size();
    c.kept = (int)kept.size();
    std::vector<int> missed, extra;
    std::set_difference(truth.begin(), truth.end(), kept.begin(), kept.end(), std::back_inserter(missed));
    std::set_difference(kept.begin(), kept.end(), truth.begin(), truth.end(), std::back_inserter(extra));
    c.missed = (int)missed.size();
    c.extra = (int)extra.size();
    return c;
}

} // namespace

int main() {
    RUN_SUITE("meshlets: build invariants");
    {
        Mesh sphere = Mesh::Sphere(0.5f, 32, 48);
        Mesh torus = Mesh::Torus(0.5f, 0.2f, 40, 24);
        Mesh grid = Mesh::Grid(10, 10, 40, 40);
        Mesh knot = Mesh::TorusKnot();
        const std::pair<const char*, const Mesh*> list[] = {{"sphere", &sphere}, {"torus", &torus}, {"grid", &grid}, {"knot", &knot}, {"cube", nullptr}};
        for (auto& kv : list) {
            Mesh cube = Mesh::Cube();
            const Mesh& m = kv.second ? *kv.second : cube;
            MeshletSet s = BuildMeshlets(m);
            CHECK(ValidateMeshlets(m, s).empty());
            CHECK(s.TriangleTotal() + s.skippedTriangles == m.TriangleCount());   // every usable triangle exactly once
            CHECK(s.skippedTriangles <= m.TriangleCount() / 20);                   // only pole slivers are zero-area
            double fill = 0.0;
            for (const Meshlet& ml : s.meshlets) { CHECK(ml.vertexCount <= 64 && ml.triangleCount <= 126); fill += ml.triangleCount; }
            fill /= (double)s.meshlets.size() * 126.0;
            std::cout << "  " << kv.first << ": " << m.TriangleCount() << " triangles -> " << s.meshlets.size()
                      << " meshlets, average fill " << (int)(fill * 100) << "%\n";
            if (m.TriangleCount() > 1000) CHECK(fill > 0.45);        // clusters are reasonably full
            CHECK(SameSet(s, BuildMeshlets(m)));                      // deterministic
        }
        // Locality: a cluster of a big grid is a compact patch, not a scatter.
        MeshletSet gs = BuildMeshlets(grid);
        double worst = 0;
        for (const Meshlet& ml : gs.meshlets) worst = std::max<double>(worst, ml.radius);
        std::cout << "  grid largest cluster radius " << worst << " (grid is 10 wide)\n";
        CHECK(worst < 2.2);
        // Smaller limits are honoured.
        MeshletOptions small;
        small.maxVertices = 12; small.maxTriangles = 16;
        MeshletSet ss = BuildMeshlets(sphere, small);
        CHECK(ValidateMeshlets(sphere, ss).empty());
        for (const Meshlet& ml : ss.meshlets) CHECK(ml.vertexCount <= 12 && ml.triangleCount <= 16);
        MeshletOptions silly;
        silly.maxVertices = 1000; silly.maxTriangles = -4;
        MeshletSet sl = BuildMeshlets(sphere, silly);
        CHECK(ValidateMeshlets(sphere, sl).empty());
        for (const Meshlet& ml : sl.meshlets) CHECK(ml.triangleCount == 1);
        // A flat grid has a perfect cone; a sphere patch a narrow one.
        for (const Meshlet& ml : gs.meshlets) CHECK(ml.coneCutoff < 0.01f);
        int narrow = 0;
        for (const Meshlet& ml : BuildMeshlets(sphere).meshlets) if (ml.coneCutoff < 0.6f) ++narrow;
        CHECK(narrow > 0);
    }

    RUN_SUITE("meshlets: hostile input");
    {
        Mesh empty;
        MeshletSet e = BuildMeshlets(empty);
        CHECK(e.meshlets.empty() && ValidateMeshlets(empty, e).empty());
        Mesh one;
        one.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        one.triangles = {0, 1, 2};
        MeshletSet o = BuildMeshlets(one);
        CHECK(o.meshlets.size() == 1u && o.meshlets[0].triangleCount == 1 && ValidateMeshlets(one, o).empty());
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        Mesh bad = Mesh::Sphere(0.5f, 12, 16);
        bad.vertices[3].x = nan; bad.vertices[7].y = inf;
        bad.triangles.insert(bad.triangles.end(), {0, 9999, 1, -1, 2, 3, 5, 5, 6, 8});
        MeshletSet b = BuildMeshlets(bad);
        CHECK(b.skippedTriangles > 3);
        CHECK(ValidateMeshlets(bad, b).empty());
        Mesh huge = Mesh::Sphere(0.5f, 12, 16);
        for (Vec3& v : huge.vertices) v = v * 1e30f;
        MeshletSet h = BuildMeshlets(huge);
        CHECK(h.meshlets.empty() && h.skippedTriangles == huge.TriangleCount());
        Mesh dup = Mesh::Sphere(0.5f, 12, 16);
        const int n0 = (int)dup.vertices.size();
        for (int i = 0; i < n0; ++i) dup.vertices.push_back(dup.vertices[(std::size_t)i]);
        for (std::size_t i = 0; i < dup.triangles.size(); i += 2) dup.triangles[i] += n0;
        CheckBuild(dup, "duplicate vertices");
        Mesh same;                                    // all points equal
        same.vertices.assign(9, Vec3{2, 2, 2});
        same.triangles = {0, 1, 2, 3, 4, 5, 6, 7, 8};
        MeshletSet sm = BuildMeshlets(same);
        CHECK(sm.meshlets.empty() && sm.skippedTriangles == 3);
        Mesh fan;                                     // one vertex shared by 500 triangles: cluster must still cut at 64 vertices
        fan.vertices.push_back({0, 0, 0});
        for (int i = 0; i < 501; ++i) fan.vertices.push_back({std::cos(i * 0.0125f), std::sin(i * 0.0125f), 0});
        for (int i = 1; i < 501; ++i) fan.triangles.insert(fan.triangles.end(), {0, i, i + 1});
        MeshletSet fs = BuildMeshlets(fan);
        CHECK(ValidateMeshlets(fan, fs).empty() && fs.TriangleTotal() == 500);
        for (const Meshlet& ml : fs.meshlets) CHECK(ml.vertexCount <= 64);
        // Fuzz (also run under the address and undefined sanitizers).
        int ok = 0;
        for (int seed = 1; seed <= 200; ++seed) {
            Rng rng((std::uint64_t)seed);
            Mesh m;
            const int nv = (int)(rng.Next() % 300);
            for (int i = 0; i < nv; ++i) m.vertices.push_back({rng.R(-5, 5), rng.R(-5, 5), rng.R(-5, 5)});
            const int nt = (int)(rng.Next() % 700);
            for (int i = 0; i < nt * 3; ++i) m.triangles.push_back((int)(rng.Next() % (std::uint32_t)(nv + 3)) - 1);
            if (nv > 4 && seed % 5 == 0) m.vertices[rng.Next() % (std::uint32_t)nv].z = nan;
            if (nv > 4 && seed % 7 == 0) m.vertices[rng.Next() % (std::uint32_t)nv] = {1e20f, 0, 0};
            MeshletSet s = BuildMeshlets(m);
            const std::string err = ValidateMeshlets(m, s);
            CHECK(err.empty());
            if (err.empty()) ++ok;
            MeshletCullParams p = MakeCam({rng.R(-8, 8), rng.R(-8, 8), rng.R(-8, 8)}, {0, 0, 0}, 60, 100);
            p.maxDistance = rng.R(0, 20);
            std::vector<std::uint32_t> vis;
            CullMeshlets(s, p, vis);
            CHECK(Compare(m, s, p).missed == 0);
        }
        CHECK(ok == 200);
    }

    RUN_SUITE("meshlets: culling equals brute force per triangle");
    {
        const Mesh sphere = Mesh::Sphere(0.5f, 32, 48);
        const Mesh torus = Mesh::Torus(0.5f, 0.2f, 40, 24);
        const Mesh knot = Mesh::TorusKnot();
        const Mesh* meshes[] = {&sphere, &torus, &knot};
        const char* names[] = {"sphere", "torus", "knot"};
        for (int mi = 0; mi < 3; ++mi) {
            const Mesh& m = *meshes[mi];
            const MeshletSet s = BuildMeshlets(m);
            Rng rng((std::uint64_t)mi + 77);
            long missed = 0, truth = 0, extra = 0, kept = 0, views = 0;
            int clustersCulled = 0, clustersTotal = 0;
            for (int v = 0; v < 400; ++v) {
                // A random world transform (rotation, translation, uniform scale) and a random camera.
                const float scale = rng.R(0.5f, 3.0f);
                const Quat q = Quat::Euler(rng.R(0, 360), rng.R(0, 360), rng.R(0, 360));
                const Vec3 pos{rng.R(-3, 3), rng.R(-3, 3), rng.R(-3, 3)};
                MeshletCullParams p;
                const Vec3 eye{rng.R(-12, 12), rng.R(-12, 12), rng.R(-12, 12)};
                const Vec3 target{rng.R(-4, 4), rng.R(-4, 4), rng.R(-4, 4)};
                const Mat4 view = Mat4::LookAt(eye, target, {0, 1, 0});
                const Mat4 proj = Mat4::Perspective(rng.R(20, 110), rng.R(0.6f, 1.8f), 0.1f, 200.0f);
                p.frustum = Frustum::FromVP(proj * view);
                p.cameraPos = eye;
                p.world = Mat4::TRS(pos, q, {scale, scale, scale});
                p.maxDistance = (v % 3 == 0) ? rng.R(2, 25) : 0.0f;
                p.useCone = (v % 5) != 0;
                std::vector<std::uint32_t> vis;
                const MeshletCullStats st = CullMeshlets(s, p, vis);
                const Cmp c = Compare(m, s, p);
                missed += c.missed; truth += c.truth; extra += c.extra; kept += c.kept; ++views;
                clustersCulled += st.total - st.visible; clustersTotal += st.total;
                CHECK(c.missed == 0);                        // never drops a visible triangle
            }
            const double fp = kept > 0 ? (double)extra / (double)kept : 0.0;
            std::cout << "  " << names[mi] << ": " << views << " views, brute-force visible " << truth << " triangles, missed " << missed
                      << ", extra kept " << extra << " (" << (int)(fp * 1000) / 10.0 << "% of kept), clusters culled "
                      << (int)(100.0 * clustersCulled / std::max(1, clustersTotal)) << "%\n";
            CHECK(missed == 0);
            CHECK(fp < 0.50);                                // tolerance: at most half of the kept triangles are extra (whole clusters are kept or dropped)
        }

        // Each test alone, on a sphere seen from outside.
        const MeshletSet s = BuildMeshlets(sphere);
        {
            MeshletCullParams p = MakeCam({0, 0, 5}, {0, 0, 0}, 40, 100);
            p.useFrustum = false; p.useDistance = false;
            std::vector<std::uint32_t> vis;
            const MeshletCullStats st = CullMeshlets(s, p, vis);
            std::cout << "  cone only: " << st.culledCone << " of " << st.total << " clusters culled\n";
            CHECK(st.culledCone > st.total * 25 / 100);           // clusters that are wholly on the far side
            CHECK(Compare(sphere, s, p).missed == 0);
            p.useCone = false; p.useFrustum = true;
            p.frustum = MakeCam({0, 0, 5}, {0, 0, 0}, 4, 100).frustum;   // telescope: only the middle
            const MeshletCullStats sf = CullMeshlets(s, p, vis);
            CHECK(sf.culledFrustum > sf.total / 4);
            CHECK(Compare(sphere, s, p).missed == 0);
            p.frustum = MakeCam({0, 0, 5}, {0, 0, 0}, 40, 100).frustum;
            p.useFrustum = false; p.useDistance = true; p.maxDistance = 4.8f;
            const MeshletCullStats sd = CullMeshlets(s, p, vis);
            CHECK(sd.culledDistance > 0 && sd.visible > 0);
            CHECK(Compare(sphere, s, p).missed == 0);
            p.maxDistance = 1.0f;                                  // nothing is that close
            CHECK(CullMeshlets(s, p, vis).visible == 0);
            CHECK(BruteForceVisibleTriangles(sphere, p).empty());
            p.maxDistance = 0.0f;                                  // no limit: everything stays
            CHECK(CullMeshlets(s, p, vis).visible == (int)s.meshlets.size());
        }
        // Camera behind the object and a camera inside it.
        {
            MeshletCullParams p = MakeCam({0, 0, -5}, {0, 0, 10}, 60, 100);
            std::vector<std::uint32_t> vis;
            CullMeshlets(s, p, vis);
            CHECK(Compare(sphere, s, p).missed == 0);
            MeshletCullParams in = MakeCam({0, 0, 0}, {0, 0, 1}, 60, 100);
            in.useCone = false;                                    // double-sided look from inside
            CullMeshlets(s, in, vis);
            CHECK(Compare(sphere, s, in).missed == 0);
        }
        // Frustum planes must reject a whole object that is simply elsewhere.
        {
            MeshletCullParams p = MakeCam({0, 0, 5}, {0, 0, 50}, 40, 100);   // looking away
            std::vector<std::uint32_t> vis;
            const MeshletCullStats st = CullMeshlets(s, p, vis);
            CHECK(st.visible == 0 && st.culledFrustum == st.total);
            CHECK(BruteForceVisibleTriangles(sphere, p).empty());
        }
    }

    TEST_MAIN_RESULT();
}
