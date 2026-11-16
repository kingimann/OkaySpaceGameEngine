// Mirror and Subdivision Surface of the own Blender reader (engine/src/Render/BlendModifiers.cpp), on hand-built
// meshes with hand-computed results, through the reader on the synthetic fixture tests/data/models/blend_modifiers.blend
// (scripts/gen_blend_fixtures.py), on the real corpus files that carry modifiers (when OKAY_MODEL_CORPUS is set), and
// a deterministic fuzz of the modifier code on random meshes. Standalone (no engine library), so it also builds under
// sanitizers:
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all
//       -Iengine/include -Iengine/src/Core -Iengine/src/Render -Itests -DOKAY_SOURCE_DIR='"."' tests/test_blend_modifiers.cpp
//       engine/src/Render/BlendModifiers.cpp engine/src/Render/BlendReader.cpp engine/src/Core/ImageDecode.cpp
//       engine/src/Core/ImageDecodeJpeg.cpp -o blend_modifiers
#include "test_framework.hpp"
#include "BlendModifiers.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace okay;
using namespace okay::blendmod;

static const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";

static PolyMesh MakeMesh(const std::vector<Vec3>& pos, const std::vector<std::vector<int>>& faces) {
    PolyMesh m;
    m.pos = pos;
    for (const auto& f : faces) {
        m.faceStart.push_back((int)m.loopVert.size()); m.faceCount.push_back((int)f.size()); m.faceMat.push_back(0); m.faceSmooth.push_back(1);
        for (int v : f) m.loopVert.push_back(v);
    }
    return m;
}
static PolyMesh Cube() {   // +-1, outward winding
    return MakeMesh({{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},
                    {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}});
}
static Vec3 FaceNormal(const PolyMesh& m, std::size_t f) {   // Newell
    Vec3 n{0, 0, 0};
    const int s = m.faceStart[f], c = m.faceCount[f];
    for (int i = 0; i < c; ++i) {
        const Vec3& a = m.pos[(std::size_t)m.loopVert[(std::size_t)(s + i)]];
        const Vec3& b = m.pos[(std::size_t)m.loopVert[(std::size_t)(s + (i + 1) % c)]];
        n.x += (a.y - b.y) * (a.z + b.z); n.y += (a.z - b.z) * (a.x + b.x); n.z += (a.x - b.x) * (a.y + b.y);
    }
    return n;
}
static Vec3 FaceCenter(const PolyMesh& m, std::size_t f) {
    Vec3 c{0, 0, 0};
    for (int i = 0; i < m.faceCount[f]; ++i) c += m.pos[(std::size_t)m.loopVert[(std::size_t)(m.faceStart[f] + i)]];
    return c * (1.0f / (float)m.faceCount[f]);
}
// the distinct positions used by faces, rounded to 1e-5
static std::set<std::tuple<long, long, long>> UsedPositions(const PolyMesh& m) {
    std::set<std::tuple<long, long, long>> s;
    for (int v : m.loopVert) { const Vec3& p = m.pos[(std::size_t)v]; s.insert({std::lround(p.x * 1e5), std::lround(p.y * 1e5), std::lround(p.z * 1e5)}); }
    return s;
}
static std::tuple<long, long, long> K(double x, double y, double z) { return {std::lround(x * 1e5), std::lround(y * 1e5), std::lround(z * 1e5)}; }
static bool Valid(const PolyMesh& m) {
    if (m.faceStart.size() != m.faceCount.size() || m.faceMat.size() != m.faceStart.size() || m.faceSmooth.size() != m.faceStart.size()) return false;
    if (!m.loopUv.empty() && m.loopUv.size() != m.loopVert.size()) return false;
    if (!m.dvStart.empty() && m.dvStart.size() != m.pos.size() + 1) return false;
    for (std::size_t f = 0; f < m.faceStart.size(); ++f)
        if (m.faceStart[f] < 0 || m.faceCount[f] < 3 || (std::size_t)(m.faceStart[f] + m.faceCount[f]) > m.loopVert.size()) return false;
    for (int v : m.loopVert) if (v < 0 || (std::size_t)v >= m.pos.size()) return false;
    for (const Vec3& p : m.pos) if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    for (const Vec2& u : m.loopUv) if (!std::isfinite(u.x) || !std::isfinite(u.y)) return false;
    return true;
}

static void TestCubeLevel1() {
    // Catmull-Clark on the cube, worked by hand: face points are the face centres (+-1, 0, 0) ...; an edge point is
    // (ends + the two face points) / 4, e.g. edge (1,1,-1)-(1,1,1): ((1,1,-1) + (1,1,1) + (1,0,0) + (0,1,0)) / 4 = (3/4, 3/4, 0);
    // a vertex point with n = 3 is (Q + 2R) / 3: Q = (1/3, 1/3, 1/3), R = (2/3, 2/3, 2/3) for corner (1,1,1), so 5/9 each.
    PolyMesh m = Cube();
    SubsurfSettings s; s.levels = 1;
    CHECK(Subdivide(m, s, 1000000) == 1);
    CHECK(Valid(m));
    CHECK(m.faceStart.size() == 24);
    for (int c : m.faceCount) CHECK(c == 4);
    CHECK(m.pos.size() == 26);   // 8 vertex + 12 edge + 6 face points
    std::set<std::tuple<long, long, long>> want;
    const double a = 5.0 / 9.0, e = 0.75;
    for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) for (int sz = -1; sz <= 1; sz += 2) want.insert(K(sx * a, sy * a, sz * a));
    for (int s1 = -1; s1 <= 1; s1 += 2) for (int s2 = -1; s2 <= 1; s2 += 2) { want.insert(K(0, s1 * e, s2 * e)); want.insert(K(s1 * e, 0, s2 * e)); want.insert(K(s1 * e, s2 * e, 0)); }
    for (int s1 = -1; s1 <= 1; s1 += 2) { want.insert(K(s1, 0, 0)); want.insert(K(0, s1, 0)); want.insert(K(0, 0, s1)); }
    CHECK(want.size() == 26);
    CHECK(UsedPositions(m) == want);
    // every child quad still faces outward
    for (std::size_t f = 0; f < m.faceStart.size(); ++f) CHECK(Vec3::Dot(FaceNormal(m, f), FaceCenter(m, f)) > 0.0f);
    // level 2: 96 quads, 98 vertices (V - E + F = 2: 98 - 192 + 96)
    PolyMesh m2 = Cube(); s.levels = 2;
    CHECK(Subdivide(m2, s, 1000000) == 2);
    CHECK(m2.faceStart.size() == 96);
    CHECK(UsedPositions(m2).size() == 98);
    // the face limit stops before a level that would pass it
    PolyMesh m3 = Cube(); s.levels = 5;
    CHECK(Subdivide(m3, s, 400) == 3);   // 24, 96, 384 faces; 1536 would pass 400
    CHECK(m3.faceStart.size() == 384);
}

static void TestSimpleAndBoundary() {
    PolyMesh m = Cube();
    SubsurfSettings s; s.levels = 2; s.simple = true;
    CHECK(Subdivide(m, s, 1000000) == 2);
    CHECK(m.faceStart.size() == 96);
    float mx = 0; for (const Vec3& p : m.pos) mx = std::max({mx, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)});
    CHECK_NEAR(mx, 1.0, 1e-6);   // simple subdivision keeps the shape
    // one open quad: boundary corners use (a + 6 S + b) / 8, boundary edge points the midpoint
    PolyMesh q = MakeMesh({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}, {{0, 1, 2, 3}});
    SubsurfSettings s1; s1.levels = 1;
    CHECK(Subdivide(q, s1, 1000) == 1);
    CHECK(q.faceStart.size() == 4);
    std::set<std::tuple<long, long, long>> want{K(0.125, 0.125, 0), K(0.875, 0.125, 0), K(0.875, 0.875, 0), K(0.125, 0.875, 0),
                                                K(0.5, 0, 0), K(1, 0.5, 0), K(0.5, 1, 0), K(0, 0.5, 0), K(0.5, 0.5, 0)};
    CHECK(UsedPositions(q) == want);
}

static void TestCreases() {
    // every cube edge infinitely sharp: corners stay, edge points are midpoints (the cube keeps its shape)
    PolyMesh m = Cube();
    const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto& e : edges) m.creases.push_back(Crease{e[0], e[1], INFINITY});
    SubsurfSettings s; s.levels = 2;
    CHECK(Subdivide(m, s, 1000000) == 2);
    bool corner = false;
    for (int v : m.loopVert) { const Vec3& p = m.pos[(std::size_t)v]; if (std::fabs(p.x) > 0.99999f && std::fabs(p.y) > 0.99999f && std::fabs(p.z) > 0.99999f) corner = true; }
    CHECK(corner);
    CHECK(m.creases.empty());
    // one edge (1,1,-1)-(1,1,1) with sharpness 0.5: its edge point is halfway between smooth (3/4,3/4,0) and the midpoint (1,1,0)
    PolyMesh h = Cube();
    h.creases.push_back(Crease{2, 6, 0.5f});
    SubsurfSettings s1; s1.levels = 1;
    CHECK(Subdivide(h, s1, 1000000) == 1);
    CHECK(UsedPositions(h).count(K(0.875, 0.875, 0)) == 1);
    CHECK(UsedPositions(h).count(K(0.75, 0.75, 0)) == 0);
    // creases off: the same edge is smooth
    PolyMesh h2 = Cube(); h2.creases.push_back(Crease{2, 6, 0.5f});
    s1.useCreases = false;
    Subdivide(h2, s1, 1000000);
    CHECK(UsedPositions(h2).count(K(0.75, 0.75, 0)) == 1);
}

static void TestUv() {
    // linear UVs: the face point gets the mean of the face's UVs
    PolyMesh q = MakeMesh({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}, {{0, 1, 2, 3}});
    q.loopUv = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    SubsurfSettings s; s.levels = 1; s.smoothUv = false;
    PolyMesh lin = q;
    Subdivide(lin, s, 1000);
    CHECK(lin.loopUv.size() == 16);
    CHECK_NEAR(lin.loopUv[2].x, 0.5, 1e-6); CHECK_NEAR(lin.loopUv[2].y, 0.5, 1e-6);   // corner 2 of child 0 is the face point
    CHECK_NEAR(lin.loopUv[1].x, 0.5, 1e-6); CHECK_NEAR(lin.loopUv[1].y, 0.0, 1e-6);   // corner 1: edge 0-1 midpoint
    // smooth UVs on an open quad follow the boundary rule like positions do: corner uv (0,0) -> (1/8, 1/8)
    PolyMesh sm = q; s.smoothUv = true;
    Subdivide(sm, s, 1000);
    CHECK_NEAR(sm.loopUv[0].x, 0.125, 1e-6); CHECK_NEAR(sm.loopUv[0].y, 0.125, 1e-6);
    // a cube with a UV seam (each face its own island): no UV leaves its island's [0,1] square
    PolyMesh c = Cube();
    for (std::size_t f = 0; f < 6; ++f) { c.loopUv.push_back({0, 0}); c.loopUv.push_back({1, 0}); c.loopUv.push_back({1, 1}); c.loopUv.push_back({0, 1}); }
    SubsurfSettings s2; s2.levels = 2;
    Subdivide(c, s2, 100000);
    CHECK(Valid(c));
    for (const Vec2& u : c.loopUv) CHECK(u.x >= -1e-6f && u.x <= 1.000001f && u.y >= -1e-6f && u.y <= 1.000001f);
}

static void TestMirror() {
    // a quad touching the plane x = 0: two corners merge, the copy's winding is reversed
    PolyMesh q = MakeMesh({{0.0004f, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}, {{0, 1, 2, 3}});
    q.loopUv = {{0.1f, 0}, {0.9f, 0}, {0.9f, 1}, {0.1f, 1}};
    MirrorSettings ms; ms.flipU = true;
    CHECK(Mirror(q, ms) == 2);
    CHECK(Valid(q));
    CHECK(q.pos.size() == 6);
    CHECK(q.faceStart.size() == 2);
    CHECK_NEAR(q.pos[0].x, 0.0, 1e-7);   // merged onto the plane
    CHECK(UsedPositions(q) == (std::set<std::tuple<long, long, long>>{K(0, 0, 0), K(1, 0, 0), K(1, 1, 0), K(0, 1, 0), K(-1, 0, 0), K(-1, 1, 0)}));
    CHECK(FaceNormal(q, 0).z > 0 && FaceNormal(q, 1).z > 0);   // both face +z after the reversal
    CHECK(q.loopVert[4] == 0 && q.loopVert[5] == 3);           // copy: corners 0, 3, 2, 1 of the original, mirrored
    CHECK_NEAR(q.loopUv[4].x, 0.9, 1e-6);                     // U flipped: 1 - 0.1
    CHECK_NEAR(q.loopUv[5].x, 0.9, 1e-6);
    CHECK_NEAR(q.loopUv[6].x, 0.1, 1e-6);
    // no merge: 8 vertices
    PolyMesh n = MakeMesh({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}, {{0, 1, 2, 3}});
    MirrorSettings nm; nm.merge = false;
    CHECK(Mirror(n, nm) == 0);
    CHECK(n.pos.size() == 8);
    // three axes: 8 copies
    PolyMesh c = Cube();
    for (Vec3& p : c.pos) p += Vec3{3, 3, 3};
    MirrorSettings m3; m3.axis[1] = m3.axis[2] = true;
    Mirror(c, m3);
    CHECK(c.faceStart.size() == 48 && c.pos.size() == 64);
    for (std::size_t f = 0; f < c.faceStart.size(); ++f) {   // all copies still face outward from their own centre
        const Vec3 fc = FaceCenter(c, f);
        const Vec3 cc{fc.x > 0 ? 3.0f : -3.0f, fc.y > 0 ? 3.0f : -3.0f, fc.z > 0 ? 3.0f : -3.0f};
        CHECK(Vec3::Dot(FaceNormal(c, f), fc - cc) > 0.0f);
    }
    // a face lying in the mirror plane is not doubled
    PolyMesh p = MakeMesh({{0, 0, 0}, {0, 1, 0}, {0, 1, 1}, {0, 0, 1}}, {{0, 1, 2, 3}});
    MirrorSettings pm;
    Mirror(p, pm);
    CHECK(p.faceStart.size() == 1 && p.pos.size() == 4);
    // a mirror object: the plane x = 2 of the mirror space; (3, 0, 0) -> (1, 0, 0)
    PolyMesh o = MakeMesh({{3, 0, 0}, {4, 0, 0}, {4, 1, 0}}, {{0, 1, 2}});
    MirrorSettings om; om.useSpace = true;
    om.fromSpace.m[12] = 2.0f;                          // mirror space -> mesh: translate by (2, 0, 0)
    om.toSpace.m[12] = -2.0f;
    Mirror(o, om);
    CHECK_NEAR(o.pos[3].x, 1.0, 1e-6); CHECK_NEAR(o.pos[4].x, 0.0, 1e-6);
    // vertex groups: the copy of a vertex in "Arm.L" goes to "Arm.R"
    PolyMesh w = MakeMesh({{1, 0, 0}, {2, 0, 0}, {2, 1, 0}}, {{0, 1, 2}});
    w.dvStart = {0, 1, 2, 3}; w.dvGroup = {0, 0, 1}; w.dvWeight = {1.0f, 0.5f, 0.25f};
    MirrorSettings wm; wm.groupFlip = {1, 0};
    Mirror(w, wm);
    CHECK(Valid(w));
    CHECK(w.dvStart.size() == 7);
    CHECK(w.dvGroup[3] == 1 && w.dvGroup[4] == 1 && w.dvGroup[5] == 0);
    CHECK_NEAR(w.dvWeight[4], 0.5, 1e-7);
    // subdivision carries weights: a vertex point keeps its own, an edge point gets the mean of its ends
    PolyMesh sw = MakeMesh({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}, {{0, 1, 2, 3}});
    sw.dvStart = {0, 1, 2, 2, 2}; sw.dvGroup = {0, 1}; sw.dvWeight = {1.0f, 1.0f};
    SubsurfSettings ss; ss.levels = 1;
    Subdivide(sw, ss, 100);
    CHECK(Valid(sw));
    CHECK(sw.dvStart[1] - sw.dvStart[0] == 1);                // vertex 0: group 0 weight 1
    // the edge point of edge 0-1 (vertex 4): 0.5 in group 0 and 0.5 in group 1
    CHECK(sw.dvStart[5] - sw.dvStart[4] == 2);
    CHECK_NEAR(sw.dvWeight[sw.dvStart[4]], 0.5, 1e-7);

    CHECK(FlipSideName("Arm.L") == "Arm.R");
    CHECK(FlipSideName("hand_r") == "hand_l");
    CHECK(FlipSideName("L-leg") == "R-leg");
    CHECK(FlipSideName("LeftFoot") == "RightFoot");
    CHECK(FlipSideName("right_eye") == "left_eye");
    CHECK(FlipSideName("Spine").empty());
    CHECK(FlipSideName("").empty());
}

// The fixture: an open box mirrored (merge), an Array (named, skipped), then one subdivision level with a crease.
static void TestFixture() {
    ImportedModel m; std::string err;
    CHECK(ReadBlendFile(kDir + "blend_modifiers.blend", m, &err, true));
    CHECK(m.parts.size() == 1);
    if (m.parts.size() != 1) { std::fprintf(stderr, "  read: %s\n", err.c_str()); return; }
    CHECK(m.parts[0].mesh.triangles.size() == 80 * 3);
    bool named = false;
    for (const std::string& n : m.notes) if (n.find("'Array' (Array)") != std::string::npos) named = true;
    CHECK(named);
    // the same steps on the same mesh by hand, in Blender space, then (x, y, z) -> (x, z, -y)
    PolyMesh b = MakeMesh({{0, -1, -1}, {1, -1, -1}, {1, 1, -1}, {0, 1, -1}, {0, -1, 1}, {1, -1, 1}, {1, 1, 1}, {0, 1, 1}},
                          {{0, 3, 2, 1}, {4, 5, 6, 7}, {1, 2, 6, 5}, {0, 1, 5, 4}, {3, 7, 6, 2}});
    b.creases.push_back(Crease{1, 2, INFINITY});
    MirrorSettings ms; ms.flipU = true;
    CHECK(Mirror(b, ms) == 4);
    CHECK(b.faceStart.size() == 10);
    SubsurfSettings ss; ss.levels = 1;
    Subdivide(b, ss, 100000);
    std::set<std::tuple<long, long, long>> want;
    for (int v : b.loopVert) { const Vec3& p = b.pos[(std::size_t)v]; want.insert(K(p.x, p.z, -p.y)); }
    std::set<std::tuple<long, long, long>> got;
    for (const Vec3& p : m.parts[0].mesh.vertices) got.insert(K(p.x, p.y, p.z));
    CHECK(got == want);
    // the crease keeps the edge (1,1,-1)-(1,-1,-1) straight: its edge point (1, 0, -1) is unmoved (engine space (1, -1, 0))
    CHECK(got.count(K(1, -1, 0)) == 1);
    // the mirror image of the crease is creased too
    CHECK(got.count(K(-1, -1, 0)) == 1);
    // symmetric in x
    for (const auto& k : got) CHECK(got.count({-std::get<0>(k), std::get<1>(k), std::get<2>(k)}) == 1);
}

// Real files with modifiers (Assimp's test directory): the triangle counts Assimp gives, our bounds in engine space.
static void TestCorpus() {
    const char* corpus = std::getenv("OKAY_MODEL_CORPUS");
    if (!corpus) { std::printf("  corpus: skipped (OKAY_MODEL_CORPUS not set)\n"); return; }
    struct Row { const char* file; std::size_t tris; float lo[3], hi[3]; };
    const Row rows[] = {
        {"models/BLEND/SuzanneSubdiv_252.blend", 251904, {-1.777f, -0.2133f, -0.8091f}, {0.8809f, 1.388f, 1.102f}},
        {"models/BLEND/yxa_1.blend", 115712, {-0.4144f, -0.004283f, -0.8198f}, {1.392f, 5.096f, -0.1808f}},
        {"models/BLEND/HUMAN.blend", 8560, {-2.345f, -5.196f, -0.8247f}, {2.345f, 5.523f, 0.7525f}},
        {"models/BLEND/MirroredCube_252.blend", 108, {-5.102f, -4.951f, -5.762f}, {5.102f, 4.951f, 4.772f}},
    };
    for (const Row& r : rows) {
        ImportedModel m; std::string err;
        const std::string path = std::string(corpus) + "/" + r.file;
        if (!ReadBlendFile(path, m, &err, true)) { std::printf("  corpus: %s missing or unreadable (%s)\n", r.file, err.c_str()); continue; }
        std::size_t tris = 0;
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (const ImportedPart& p : m.parts) {
            tris += p.mesh.triangles.size() / 3;
            for (const Vec3& v : p.mesh.vertices) for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], v[k]); hi[k] = std::max(hi[k], v[k]); }
        }
        CHECK(tris == r.tris);
        for (int k = 0; k < 3; ++k) { CHECK_NEAR(lo[k], r.lo[k], 2e-3); CHECK_NEAR(hi[k], r.hi[k], 2e-3); }
        std::printf("  corpus: %s %zu triangles\n", r.file, tris);
    }
}

// Random small meshes (any topology the reader can hand over: repeated corners, non-manifold edges, loose vertices,
// huge and tiny coordinates, creases on any pair) through Mirror and Subdivide: the result must stay valid and finite.
static std::uint64_t g_state = 0x243F6A8885A308D3ull;
static std::uint64_t Rnd() { g_state ^= g_state << 13; g_state ^= g_state >> 7; g_state ^= g_state << 17; return g_state; }
static void TestFuzz() {
    long iters = 3000;
    if (const char* e = std::getenv("OKAY_BLEND_MOD_FUZZ_ITERS")) iters = std::atol(e);
    long bad = 0;
    for (long it = 0; it < iters; ++it) {
        PolyMesh m;
        const int nv = 1 + (int)(Rnd() % 24);
        const float scale = (Rnd() % 4 == 0) ? 1e12f : 1.0f;
        for (int i = 0; i < nv; ++i) m.pos.push_back(Vec3{(float)((int)(Rnd() % 2001) - 1000) / 500.0f * scale, (float)((int)(Rnd() % 2001) - 1000) / 500.0f, (Rnd() % 3 == 0) ? 0.0f : (float)(Rnd() % 100) / 50.0f});
        const int nf = (int)(Rnd() % 12);
        for (int f = 0; f < nf; ++f) {
            const int n = 3 + (int)(Rnd() % 6);
            m.faceStart.push_back((int)m.loopVert.size()); m.faceCount.push_back(n); m.faceMat.push_back((int)(Rnd() % 3)); m.faceSmooth.push_back((char)(Rnd() & 1));
            for (int k = 0; k < n; ++k) m.loopVert.push_back((int)(Rnd() % (std::uint64_t)nv));
        }
        if (Rnd() & 1) for (std::size_t l = 0; l < m.loopVert.size(); ++l) m.loopUv.push_back(Vec2{(float)(Rnd() % 5) / 4.0f, (float)(Rnd() % 5) / 4.0f});
        if (Rnd() & 1) {
            m.dvStart.push_back(0);
            for (int v = 0; v < nv; ++v) { const int k = (int)(Rnd() % 3); for (int j = 0; j < k; ++j) { m.dvGroup.push_back((int)(Rnd() % 4)); m.dvWeight.push_back((float)(Rnd() % 10) / 10.0f); } m.dvStart.push_back((std::uint32_t)m.dvGroup.size()); }
        }
        const int nc = (int)(Rnd() % 5);
        for (int c = 0; c < nc; ++c) m.creases.push_back(Crease{(int)(Rnd() % (std::uint64_t)nv), (int)(Rnd() % (std::uint64_t)nv), (Rnd() & 1) ? INFINITY : (float)(Rnd() % 30) / 10.0f});
        if (Rnd() & 1) {
            MirrorSettings ms;
            for (int a = 0; a < 3; ++a) ms.axis[a] = (Rnd() & 1) != 0;
            ms.merge = (Rnd() & 1) != 0; ms.tolerance = (float)(Rnd() % 100) / 100.0f; ms.flipU = (Rnd() & 1) != 0; ms.flipV = (Rnd() & 1) != 0;
            if (Rnd() & 1) ms.groupFlip = {1, 0, 3, 2};
            Mirror(m, ms);
            if (!Valid(m)) ++bad;
        }
        SubsurfSettings ss; ss.levels = 1 + (int)(Rnd() % 3); ss.simple = (Rnd() % 4) == 0; ss.smoothUv = (Rnd() & 1) != 0; ss.joinCoincident = (Rnd() & 1) != 0;
        const std::size_t faces = m.loopVert.size();
        const int done = Subdivide(m, ss, 20000);
        if (!Valid(m)) ++bad;
        if (done >= 1 && faces > 0 && m.faceStart.size() < faces) ++bad;
    }
    CHECK(bad == 0);
    std::printf("  fuzz: %ld random meshes, %ld invalid results\n", iters, bad);
}

int main() {
    RUN_SUITE("blend_modifiers");
    TestCubeLevel1();
    TestSimpleAndBoundary();
    TestCreases();
    TestUv();
    TestMirror();
    TestFixture();
    TestCorpus();
    TestFuzz();
    TEST_MAIN_RESULT();
}
