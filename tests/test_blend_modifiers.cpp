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
#include <array>
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
    // the copy shipped for the owner to try (docs/try_it.md) reads the same
    ImportedModel ex;
    CHECK(ReadBlendFile(std::string(OKAY_SOURCE_DIR) + "/examples/own_readers/mirror_subdiv_box.blend", ex, &err, true));
    CHECK(ex.parts.size() == 1 && ex.parts[0].mesh.triangles.size() == 80 * 3);
}

// DeRose et al. (1998) semi-sharp creases, beyond one uniform edge.
static void TestCreasesDeRose() {
    // Vertex (1,1,1) (index 6) with two creased edges: to vertex 2 (sharpness 2) and to vertex 7 (sharpness 0.5). Its rule
    // is "crease" now and "smooth" one level down (only the half towards 2 stays creased), so the paper blends the two by
    // the vertex sharpness, the mean (2 + 0.5) / 2 = 1.25, clamped to 1: the crease rule alone,
    // ((1,1,-1) + 6 (1,1,1) + (-1,1,1)) / 8 = (0.75, 1, 0.75). (Clamping each edge first would give 0.75 and a blend.)
    PolyMesh m = Cube();
    m.creases = {Crease{2, 6, 2.0f}, Crease{6, 7, 0.5f}};
    SubsurfSettings s1; s1.levels = 1;
    CHECK(Subdivide(m, s1, 100000) == 1);
    CHECK_NEAR(m.pos[6].x, 0.75, 1e-6); CHECK_NEAR(m.pos[6].y, 1.0, 1e-6); CHECK_NEAR(m.pos[6].z, 0.75, 1e-6);
    // Both creases 0.5: crease now, smooth next: blend by 0.5 between the crease point and the smooth point (5/9 each).
    PolyMesh h = Cube();
    h.creases = {Crease{2, 6, 0.5f}, Crease{6, 7, 0.5f}};
    CHECK(Subdivide(h, s1, 100000) == 1);
    CHECK_NEAR(h.pos[6].x, 0.5 * 0.75 + 0.5 * 5.0 / 9.0, 1e-6); CHECK_NEAR(h.pos[6].y, 0.5 * 1.0 + 0.5 * 5.0 / 9.0, 1e-6);

    // Sharpness goes down by one per level: sharpness 1.5 on edge 2-6 for two levels equals one level, then the two
    // halves with sharpness 0.5 for one more level.
    auto findPos = [](const PolyMesh& pm, Vec3 p) { for (std::size_t i = 0; i < pm.pos.size(); ++i) if (std::fabs(pm.pos[i].x - p.x) < 1e-6f && std::fabs(pm.pos[i].y - p.y) < 1e-6f && std::fabs(pm.pos[i].z - p.z) < 1e-6f) return (int)i; return -1; };
    auto samePos = [](const PolyMesh& a, const PolyMesh& b) {
        if (a.pos.size() != b.pos.size() || a.loopVert != b.loopVert) return false;
        for (std::size_t i = 0; i < a.pos.size(); ++i) if (std::fabs(a.pos[i].x - b.pos[i].x) > 1e-6f || std::fabs(a.pos[i].y - b.pos[i].y) > 1e-6f || std::fabs(a.pos[i].z - b.pos[i].z) > 1e-6f) return false;
        return true;
    };
    {
        PolyMesh two = Cube(); two.creases = {Crease{2, 6, 1.5f}};
        SubsurfSettings s2; s2.levels = 2;
        CHECK(Subdivide(two, s2, 100000) == 2);
        PolyMesh step = Cube(); step.creases = {Crease{2, 6, 1.5f}};
        CHECK(Subdivide(step, s1, 100000) == 1);
        const int mid = findPos(step, Vec3{1, 1, 0});   // sharp for this level: the edge point is the midpoint
        CHECK(mid >= 0);
        step.creases = {Crease{2, mid, 0.5f}, Crease{mid, 6, 0.5f}};
        CHECK(Subdivide(step, s1, 100000) == 1);
        CHECK(samePos(two, step));
    }
    // Variable sharpness (the paper's Chaikin rule (s_neighbour + 3 s) / 4 - 1 per half): edge 2-6 sharpness 2 with
    // neighbour 6-7 sharpness 0.5. Half at 2 (no other crease there): (2 + 6) / 4 - 1 = 1; half at 6: (0.5 + 6) / 4 - 1
    // = 0.625; both halves of 6-7: (2 + 1.5) / 4 - 1 < 0 and (0.5 + 1.5) / 4 - 1 < 0, so 0.
    {
        PolyMesh two = Cube(); two.creases = {Crease{2, 6, 2.0f}, Crease{6, 7, 0.5f}};
        SubsurfSettings s2; s2.levels = 2;
        CHECK(Subdivide(two, s2, 100000) == 2);
        PolyMesh step = Cube(); step.creases = {Crease{2, 6, 2.0f}, Crease{6, 7, 0.5f}};
        CHECK(Subdivide(step, s1, 100000) == 1);
        const int mid = findPos(step, Vec3{1, 1, 0});
        CHECK(mid >= 0);
        step.creases = {Crease{2, mid, 1.0f}, Crease{mid, 6, 0.625f}};
        CHECK(Subdivide(step, s1, 100000) == 1);
        CHECK(samePos(two, step));
        // and a uniform rule (s - 1 for every half) would not give this result
        PolyMesh other = Cube(); other.creases = {Crease{2, 6, 2.0f}, Crease{6, 7, 0.5f}};
        Subdivide(other, s1, 100000);
        other.creases = {Crease{2, mid, 1.0f}, Crease{mid, 6, 1.0f}};
        Subdivide(other, s1, 100000);
        CHECK(!samePos(two, other));
    }
    // Blender's 0..1 crease as a sharpness
    CHECK_NEAR(CreaseToSharpness(0.5f, 2, false), 1.0, 1e-7);
    CHECK_NEAR(CreaseToSharpness(0.5f, 2, true), 2.5, 1e-6);
    CHECK(std::isinf(CreaseToSharpness(1.0f, 3, true)) && std::isinf(CreaseToSharpness(1.0f, 3, false)));
    CHECK(CreaseToSharpness(0.0f, 3, true) == 0.0f);
}

static PolyMesh Grid(int n) {   // n x n quads in z = 0, from (0,0) to (n,n)
    std::vector<Vec3> pos;
    for (int y = 0; y <= n; ++y) for (int x = 0; x <= n; ++x) pos.push_back(Vec3{(float)x, (float)y, 0.0f});
    std::vector<std::vector<int>> faces;
    for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) { const int a = y * (n + 1) + x; faces.push_back({a, a + 1, a + n + 2, a + n + 1}); }
    return MakeMesh(pos, faces);
}

static void TestKeepCorners() {
    // boundary smooth "keep corners": a boundary vertex with one face stays; the others follow the boundary rule
    PolyMesh g = Grid(2);
    SubsurfSettings s; s.levels = 1;
    PolyMesh all = g;
    Subdivide(all, s, 100000);
    CHECK(UsedPositions(all).count(K(0.125, 0.125, 0)) == 1);   // (a + 6 S + b) / 8 with a = (1, 0), b = (0, 1)
    CHECK(UsedPositions(all).count(K(0, 0, 0)) == 0);
    s.keepCorners = true;
    PolyMesh kc = g;
    Subdivide(kc, s, 100000);
    for (int c : {0, 2, 6, 8}) CHECK_NEAR(kc.pos[(std::size_t)c].x, g.pos[(std::size_t)c].x, 1e-7);
    CHECK(UsedPositions(kc).count(K(0, 0, 0)) == 1 && UsedPositions(kc).count(K(2, 2, 0)) == 1);
    CHECK(UsedPositions(kc).count(K(0.125, 0.125, 0)) == 0);
    // the corners stay through further levels too
    s.levels = 3;
    PolyMesh kc3 = g;
    Subdivide(kc3, s, 100000);
    CHECK(UsedPositions(kc3).count(K(0, 2, 0)) == 1);
}

static void TestLimit() {
    // The limit position does not depend on the level it is computed from: each vertex of level 1 (indices below 26 on the
    // cube; vertex points keep their index) has the same limit whether projected at level 1 or at level 2.
    auto check = [](PolyMesh base, bool keepCorners) {
        SubsurfSettings s; s.limitSurface = true; s.keepCorners = keepCorners;
        PolyMesh a = base, b = base;
        s.levels = 1; CHECK(Subdivide(a, s, 1000000) == 1);
        s.levels = 2; CHECK(Subdivide(b, s, 1000000) == 2);
        double worst = 0;
        for (std::size_t i = 0; i < a.pos.size(); ++i)
            worst = std::max({worst, (double)std::fabs(a.pos[i].x - b.pos[i].x), (double)std::fabs(a.pos[i].y - b.pos[i].y), (double)std::fabs(a.pos[i].z - b.pos[i].z)});
        CHECK(worst < 2e-6);
        if (worst >= 2e-6) std::printf("  limit: level 1 vs level 2 differ by %g\n", worst);
        return a;
    };
    const PolyMesh c1 = check(Cube(), false);
    // by hand: the face point (1, 0, 0) of the cube's level 1 is a regular vertex; its 4 edge neighbours are (3/4, +-3/4, 0)
    // and (3/4, 0, +-3/4), its 4 diagonal corners (5/9, +-5/9, +-5/9): x = (16 + 4 * 4 * 3/4 + 4 * 5/9) / 36 = 0.839506
    CHECK(UsedPositions(c1).count(K(68.0 / 81.0, 0, 0)) == 1);
    // and the plain subdivision converges to it: level 6, the same point, without the limit
    // (the gap shrinks about 4 times per level: 6e-4 at level 5, 1.5e-4 at level 6)
    double gap[2] = {0, 0};
    for (int lv = 5; lv <= 6; ++lv) {
        PolyMesh c6 = Cube(); SubsurfSettings s6; s6.levels = lv;
        CHECK(Subdivide(c6, s6, 1000000) == lv);
        float mx = 0; for (const Vec3& p : c6.pos) if (std::fabs(p.y) < 1e-6f && std::fabs(p.z) < 1e-6f) mx = std::max(mx, p.x);
        gap[lv - 5] = std::fabs(mx - 68.0 / 81.0);
    }
    CHECK(gap[1] < 2e-4 && gap[1] < gap[0] * 0.3);
    // an open grid (boundary curve limit (a + 4 S + b) / 6 and corners), with and without "keep corners"
    PolyMesh g = Grid(3);
    g.pos[5].z = 1.0f; g.pos[10].z = -0.5f;   // two inner vertices raised and lowered
    check(g, false);
    const PolyMesh gk = check(g, true);
    CHECK(UsedPositions(gk).count(K(3, 3, 0)) == 1);
    // a cube with every edge infinitely sharp keeps its corners and straight edges at the limit too
    PolyMesh sharp = Cube();
    const int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto& e : edges) sharp.creases.push_back(Crease{e[0], e[1], INFINITY});
    const PolyMesh sl = check(sharp, false);
    CHECK(UsedPositions(sl).count(K(1, 1, 1)) == 1 && UsedPositions(sl).count(K(1, 1, 0)) == 1);
    // a flat mesh stays flat
    PolyMesh flat = Grid(2);
    SubsurfSettings sf; sf.levels = 2; sf.limitSurface = true;
    Subdivide(flat, sf, 100000);
    for (const Vec3& p : flat.pos) CHECK(p.z == 0.0f);
    // UVs on islands stay inside their island at the limit
    PolyMesh cu = Cube();
    for (std::size_t f = 0; f < 6; ++f) { cu.loopUv.push_back({0, 0}); cu.loopUv.push_back({1, 0}); cu.loopUv.push_back({1, 1}); cu.loopUv.push_back({0, 1}); }
    SubsurfSettings su; su.levels = 2; su.limitSurface = true;
    Subdivide(cu, su, 100000);
    CHECK(Valid(cu));
    for (const Vec2& u : cu.loopUv) CHECK(u.x >= -1e-6f && u.x <= 1.000001f && u.y >= -1e-6f && u.y <= 1.000001f);
}

// A fan of 6 quads around vertex 0 (12 ring vertices at 30 degree steps), UVs = xy cut into islands (lists of faces).
static PolyMesh Fan(const std::vector<std::vector<int>>& islands) {
    std::vector<Vec3> pos{{0, 0, 0.25f}};
    for (int j = 0; j < 12; ++j) {
        const double a = j * 3.14159265358979323846 / 6.0;
        pos.push_back(Vec3{(float)(std::round(std::cos(a) * 1e6) / 1e6), (float)(std::round(std::sin(a) * 1e6) / 1e6), 0.0f});
    }
    std::vector<std::vector<int>> faces;
    for (int i = 0; i < 6; ++i) faces.push_back({0, 1 + 2 * i, 1 + 2 * i + 1, 1 + (2 * i + 2) % 12});
    PolyMesh m = MakeMesh(pos, faces);
    for (int f = 0; f < 6; ++f) {
        int k = 0;
        for (std::size_t i = 0; i < islands.size(); ++i) for (int x : islands[i]) if (x == f) k = (int)i;
        for (int v : faces[(std::size_t)f])
            m.loopUv.push_back(Vec2{(float)(std::round((pos[(std::size_t)v].x * 0.2 + 0.25 + 0.5 * (k % 2)) * 1e6) / 1e6), (float)(std::round((pos[(std::size_t)v].y * 0.2 + 0.25 + 0.5 * (k / 2)) * 1e6) / 1e6)});
    }
    return m;
}

static void TestUvModes() {
    // For each original face f, child quad 4 f's corner 0 is the centre vertex's new UV in f's island. Held: unchanged.
    auto centreMoved = [](PolyMesh m, UvSmooth mode, std::vector<bool>& moved) {
        std::vector<Vec2> before(6);
        for (int f = 0; f < 6; ++f) before[(std::size_t)f] = m.loopUv[(std::size_t)m.faceStart[(std::size_t)f]];
        SubsurfSettings s; s.levels = 1; s.uvSmooth = mode;
        Subdivide(m, s, 100000);
        moved.assign(6, false);
        for (int f = 0; f < 6; ++f) {
            const Vec2 u = m.loopUv[(std::size_t)(4 * f * 4)];
            moved[(std::size_t)f] = std::fabs(u.x - before[(std::size_t)f].x) > 1e-6f || std::fabs(u.y - before[(std::size_t)f].y) > 1e-6f;
        }
    };
    std::vector<bool> mv;
    // three islands of two faces (120 degrees each): the centre has 3 UVs, a junction, but in each island it has two faces
    const PolyMesh junction = Fan({{0, 1}, {2, 3}, {4, 5}});
    centreMoved(junction, UvSmooth::All, mv);                          CHECK(mv[0] && mv[2] && mv[4]);
    centreMoved(junction, UvSmooth::KeepCorners, mv);                  CHECK(mv[0] && mv[2] && mv[4]);
    centreMoved(junction, UvSmooth::KeepCornersJunctions, mv);         CHECK(!mv[0] && !mv[2] && !mv[4]);
    centreMoved(junction, UvSmooth::KeepCornersJunctionsConcave, mv);  CHECK(!mv[0] && !mv[2] && !mv[4]);
    centreMoved(junction, UvSmooth::KeepBoundaries, mv);               CHECK(!mv[0] && !mv[2] && !mv[4]);
    centreMoved(junction, UvSmooth::Linear, mv);                       CHECK(!mv[0] && !mv[2] && !mv[4]);
    // islands of 4 faces (240 degrees: concave at the centre) and 2 faces (120 degrees): 2 UVs, no junction
    const PolyMesh concave = Fan({{0, 1, 2, 3}, {4, 5}});
    centreMoved(concave, UvSmooth::KeepCornersJunctions, mv);          CHECK(mv[0] && mv[4]);
    centreMoved(concave, UvSmooth::KeepCornersJunctionsConcave, mv);   CHECK(!mv[0] && mv[4]);
    centreMoved(concave, UvSmooth::KeepBoundaries, mv);                CHECK(!mv[0] && !mv[4]);
    // islands of one face each: the centre is a corner (one face) in every island
    const PolyMesh corners = Fan({{0}, {1}, {2}, {3}, {4}, {5}});
    centreMoved(corners, UvSmooth::All, mv);                           CHECK(mv[0] && mv[3]);
    centreMoved(corners, UvSmooth::KeepCorners, mv);                   CHECK(!mv[0] && !mv[3]);
    // the open quad: its UV corners are corners (one face)
    PolyMesh q = MakeMesh({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}, {{0, 1, 2, 3}});
    q.loopUv = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    SubsurfSettings s; s.levels = 1; s.uvSmooth = UvSmooth::KeepCorners;
    Subdivide(q, s, 1000);
    CHECK_NEAR(q.loopUv[0].x, 0.0, 1e-7); CHECK_NEAR(q.loopUv[0].y, 0.0, 1e-7);
}

// Point clouds (position + UV) equal within a tolerance, duplicates removed.
using P5 = std::array<float, 5>;
static std::vector<P5> Dedup(const std::vector<P5>& in) {
    std::vector<P5> out;
    for (const P5& p : in) {
        bool dup = false;
        for (const P5& q : out) { bool same = true; for (int k = 0; k < 5; ++k) if (std::fabs(p[(std::size_t)k] - q[(std::size_t)k]) > 1e-4f) same = false; if (same) { dup = true; break; } }
        if (!dup) out.push_back(p);
    }
    return out;
}
static bool SameCloud(const std::vector<P5>& a0, const std::vector<P5>& b0) {
    const std::vector<P5> a = Dedup(a0), b = Dedup(b0);
    if (a.size() != b.size()) return false;
    for (const P5& p : a) {
        bool found = false;
        for (const P5& q : b) { bool same = true; for (int k = 0; k < 5; ++k) if (std::fabs(p[(std::size_t)k] - q[(std::size_t)k]) > 1e-4f) same = false; if (same) { found = true; break; } }
        if (!found) return false;
    }
    return true;
}
static std::vector<P5> CloudOf(const ImportedModel& m) {
    std::vector<P5> c;
    for (const ImportedPart& p : m.parts)
        for (std::size_t i = 0; i < p.mesh.vertices.size(); ++i) {
            const Vec2 uv = i < p.mesh.uvs.size() ? p.mesh.uvs[i] : Vec2{0, 0};
            c.push_back(P5{p.mesh.vertices[i].x, p.mesh.vertices[i].y, p.mesh.vertices[i].z, uv.x, uv.y});
        }
    return c;
}
static std::vector<P5> CloudOf(const PolyMesh& m) {   // Blender space -> engine space (x, z, -y), UV v flipped
    std::vector<P5> c;
    for (std::size_t l = 0; l < m.loopVert.size(); ++l) {
        const Vec3& p = m.pos[(std::size_t)m.loopVert[l]];
        const Vec2 uv = l < m.loopUv.size() ? m.loopUv[l] : Vec2{0, 0};
        c.push_back(P5{p.x, p.z, -p.y, uv.x, 1.0f - uv.y});
    }
    return c;
}

// The three 2.9+ fixtures (scripts/gen_blend_fixtures.py): every Subdivision option is read by field name from the file
// and has its effect: the reader's mesh equals the same steps done by hand with those settings, and differs from the
// result with the old defaults (all UVs smoothed, no keep corners, no limit surface).
static void TestSubsurfFixtures() {
    struct Case { const char* file; PolyMesh mesh; SubsurfSettings set; std::size_t tris; };
    std::vector<Case> cases;
    {   // 4.x names: grid, crease 0.5 on edge 1-4 (10 c^2 = 2.5), 2 levels, limit, UV keep corners, boundary keep corners
        PolyMesh g = MakeMesh({{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0, 1, 0}, {1, 1, 0.5f}, {2, 1, 0}, {0, 2, 0}, {1, 2, 0}, {2, 2, 0}},
                              {{0, 1, 4, 3}, {1, 2, 5, 4}, {3, 4, 7, 6}, {4, 5, 8, 7}});
        for (int v : g.loopVert) g.loopUv.push_back(Vec2{g.pos[(std::size_t)v].x / 2.0f, g.pos[(std::size_t)v].y / 2.0f});
        g.creases = {Crease{1, 4, CreaseToSharpness(0.5f, 2, true)}};
        SubsurfSettings s; s.levels = 2; s.limitSurface = true; s.uvSmooth = UvSmooth::KeepCorners; s.keepCorners = true;
        cases.push_back({"blend_subsurf_41.blend", g, s, 2 * 64});
    }
    {   // 3.6 names: the fan, UV islands of two faces (junction), 1 level, creases off, limit on, UV keep corners + junctions
        SubsurfSettings s; s.levels = 1; s.limitSurface = true; s.uvSmooth = UvSmooth::KeepCornersJunctions; s.useCreases = false;
        cases.push_back({"blend_subsurf_36.blend", Fan({{0, 1}, {2, 3}, {4, 5}}), s, 2 * 24});
    }
    {   // 2.93 layout: the open box, crease byte 128 on edge 1-2, 2 levels (render), no limit (flag 64), UV keep boundaries
        PolyMesh b = MakeMesh({{0, -1, -1}, {1, -1, -1}, {1, 1, -1}, {0, 1, -1}, {0, -1, 1}, {1, -1, 1}, {1, 1, 1}, {0, 1, 1}},
                              {{0, 3, 2, 1}, {4, 5, 6, 7}, {1, 2, 6, 5}, {0, 1, 5, 4}, {3, 7, 6, 2}});
        for (int v : b.loopVert) { const Vec3& p = b.pos[(std::size_t)v]; b.loopUv.push_back(Vec2{p.x * 0.5f, (p.y + 1) * 0.25f + (p.z + 1) * 0.25f}); }
        b.creases = {Crease{1, 2, CreaseToSharpness(128.0f / 255.0f, 2, true)}};
        SubsurfSettings s; s.levels = 2; s.uvSmooth = UvSmooth::KeepBoundaries;
        cases.push_back({"blend_subsurf_293.blend", b, s, 2 * 80});
    }
    for (Case& c : cases) {
        ImportedModel m; std::string err;
        CHECK(ReadBlendFile(kDir + c.file, m, &err, true));
        std::size_t tris = 0;
        for (const ImportedPart& p : m.parts) tris += p.mesh.triangles.size() / 3;
        CHECK(tris == c.tris);
        PolyMesh want = c.mesh;
        Subdivide(want, c.set, 1000000);
        const bool same = SameCloud(CloudOf(m), CloudOf(want));
        CHECK(same);
        PolyMesh old = c.mesh;
        SubsurfSettings o; o.levels = c.set.levels; o.useCreases = c.set.useCreases;
        Subdivide(old, o, 1000000);
        CHECK(!SameCloud(CloudOf(m), CloudOf(old)));
        std::printf("  %s: %zu triangles, %s the hand-made result\n", c.file, tris, same ? "equal to" : "DIFFERENT from");
        if (!m.notes.empty()) for (const std::string& n : m.notes) std::printf("    note: %s\n", n.c_str());
    }
    // the copy shipped for the owner to try (docs/try_it.md, examples/own_readers/subsurf_options_42.blend) reads the same
    ImportedModel ex; std::string err;
    CHECK(ReadBlendFile(std::string(OKAY_SOURCE_DIR) + "/examples/own_readers/subsurf_options_42.blend", ex, &err, true));
    CHECK(ex.parts.size() == 1 && ex.parts[0].mesh.triangles.size() == 128 * 3);
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
        if (Rnd() % 4 == 0) {   // loops of faces a reader dropped: out of range, not in any face
            const int extra = 1 + (int)(Rnd() % 4);
            for (int k = 0; k < extra; ++k) m.loopVert.push_back((Rnd() & 1) ? -1 : nv + (int)(Rnd() % 1000));
        }
        if (Rnd() % 8 == 0 && !m.faceStart.empty()) m.faceStart[Rnd() % m.faceStart.size()] = (int)(m.loopVert.size() - 1);   // a face that runs past the end
        if (Rnd() & 1) for (std::size_t l = 0; l < m.loopVert.size(); ++l) m.loopUv.push_back(Vec2{(float)(Rnd() % 5) / 4.0f, (float)(Rnd() % 5) / 4.0f});
        if (Rnd() & 1) {
            m.dvStart.push_back(0);
            if (Rnd() % 8 == 0) m.dvStart.push_back(99);   // inconsistent weights: must be dropped, not read
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
        ss.uvSmooth = (UvSmooth)(Rnd() % 6); ss.keepCorners = (Rnd() & 1) != 0; ss.limitSurface = (Rnd() & 1) != 0; ss.useCreases = (Rnd() % 4) != 0;
        const std::size_t faces = m.loopVert.size();
        const int done = Subdivide(m, ss, 20000);
        if (!Valid(m)) ++bad;
        (void)faces; (void)done;
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
    TestCreasesDeRose();
    TestKeepCorners();
    TestLimit();
    TestUvModes();
    TestMirror();
    TestFixture();
    TestSubsurfFixtures();
    TestCorpus();
    TestFuzz();
    TEST_MAIN_RESULT();
}
