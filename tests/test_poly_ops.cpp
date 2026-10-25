// Edge-loop modeling tools on PolyMesh: loop / ring select, loop cut, edge
// slide, dissolve, bridge, bevel, knife — checked on real geometry: counts,
// positions, closed shells and consistent winding after each tool.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Modeling/PolyMesh.hpp"
#include "okay/Modeling/PolyOps.hpp"
#include <cmath>
#include <map>
#include <set>

using namespace okay;
using namespace okay::modeling;

// A unit cube (corners at +-0.5) as 8 vertices and 6 quads, outward winding.
static PolyMesh Cube() { return PolyMesh::FromMesh(Mesh::Cube(1.0f)); }

// An nx by nz grid of unit quads on y = 0, facing up.
static PolyMesh Grid(int nx, int nz) {
    PolyMesh pm;
    for (int z = 0; z <= nz; ++z) for (int x = 0; x <= nx; ++x) pm.verts.push_back({(float)x, 0, (float)z});
    auto id = [&](int x, int z) { return z * (nx + 1) + x; };
    for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) {
        PolyFace f;
        f.verts = {id(x, z), id(x, z + 1), id(x + 1, z + 1), id(x + 1, z)};   // counter-clockwise from +y
        pm.faces.push_back(f);
    }
    return pm;
}

// An open square tube (4 sides, no caps) from y0 to y1.
static void AddTube(PolyMesh& pm, float y0, float y1) {
    const int base = (int)pm.verts.size();
    const float c[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (int i = 0; i < 4; ++i) pm.verts.push_back({c[i][0], y0, c[i][1]});
    for (int i = 0; i < 4; ++i) pm.verts.push_back({c[i][0], y1, c[i][1]});
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        PolyFace f;
        f.verts = {base + i, base + j, base + 4 + j, base + 4 + i};   // outward
        pm.faces.push_back(f);
    }
}

// Every directed edge used once at most: faces agree on which way is out.
static bool ConsistentWinding(const PolyMesh& pm) {
    std::set<std::pair<int, int>> seen;
    for (const PolyFace& f : pm.faces)
        for (int i = 0; i < f.Size(); ++i)
            if (!seen.insert({f.verts[i], f.verts[(i + 1) % f.Size()]}).second) return false;
    return true;
}

static int UsedVerts(const PolyMesh& pm) {
    std::set<int> u;
    for (const PolyFace& f : pm.faces) for (int v : f.verts) u.insert(v);
    return (int)u.size();
}

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }

static int FindVert(const PolyMesh& pm, Vec3 p) {
    for (int i = 0; i < (int)pm.verts.size(); ++i) if ((pm.verts[i] - p).SqrMagnitude() < 1e-8f) return i;
    return -1;
}

int main() {
    RUN_SUITE("poly_ops");

    // ---- Exact polygons survive a triangle round trip (mesh.polyFace) ----
    {
        // A skewed quad whose diagonal isn't its longest edge: the old pair merging can't
        // tell it's a quad, the stored polygon ids can.
        PolyMesh q;
        q.verts = {{0, 0, 0}, {4, 0, 0}, {4.5f, 0, 0.5f}, {0, 0, 0.5f}};
        PolyFace f; f.verts = {0, 3, 2, 1}; q.faces.push_back(f);
        Mesh m = q.ToMesh();
        CHECK(m.TriangleCount() == 2 && m.polyFace.size() == 2);
        CHECK(PolyMesh::FromMesh(m).CountFaces(4) == 1);
        Mesh noIds = m; noIds.polyFace.clear();
        CHECK(PolyMesh::FromMesh(noIds).CountFaces(4) == 0);   // what the guess gives without them
        // A primitive split per face (24 vertices) welds and keeps them.
        PolyMesh cube = Cube();
        Mesh cm = cube.ToMesh();
        cm.WeldVertices();
        CHECK(cm.polyFace.size() == 12 && PolyMesh::FromMesh(cm, {false, true, 1.0f}).CountFaces(4) == 6);
        // Ids that don't describe a polygon (two triangles not touching) are ignored.
        Mesh bad = Mesh::Cube(1.0f);
        bad.WeldVertices();
        bad.polyFace.assign(bad.TriangleCount(), 0);
        CHECK(PolyMesh::FromMesh(bad).CountFaces(4) == 6);
        // FromMesh says which polygon each triangle went into; written back as polyFace
        // it gives the same polygons without guessing again.
        Mesh w = Mesh::Cube(1.0f);
        w.WeldVertices();
        std::vector<int> map;
        PolyMesh wp = PolyMesh::FromMesh(w, {false, true, 1.0f}, &map);
        std::map<int, int> per;
        for (int f : map) ++per[f];
        CHECK(map.size() == 12 && per.size() == 6 && wp.faces.size() == 6);
        bool twoEach = true;
        for (const auto& kv : per) if (kv.second != 2 || kv.first < 0 || kv.first >= 6) twoEach = false;
        CHECK(twoEach);
        w.polyFace = map;
        std::vector<int> map2;
        CHECK(PolyMesh::FromMesh(w, {false, true, 1.0f}, &map2).CountFaces(4) == 6 && map2 == map);
        // Saved with the scene's edited geometry and read back.
        Scene s("S");
        auto* mr = s.CreateGameObject("Q")->AddComponent<MeshRenderer>();
        mr->mesh = m; mr->mesh.name = "";
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("meshpoly 2") != std::string::npos);
        Scene r("R");
        CHECK(SceneSerializer::Deserialize(r, text));
        CHECK(PolyMesh::FromMesh(r.Find("Q")->GetComponent<MeshRenderer>()->mesh).CountFaces(4) == 1);
    }

    // ---- Edge loop and ring ----
    {
        PolyMesh g = Grid(3, 3);   // 16 vertices
        // An inner grid line: from the border, across the two 4-way vertices, to the far border.
        const int a = 1 * 4 + 1, b = 1 * 4 + 2;   // (1,1)-(2,1)
        EdgeList loop = EdgeLoop(g, a, b);
        CHECK(loop.size() == 3);
        std::set<int> zs;
        for (const auto& e : loop) { zs.insert((int)g.verts[e.first].z); zs.insert((int)g.verts[e.second].z); }
        CHECK(zs.size() == 1 && *zs.begin() == 1);          // stays on the line z = 1
        // A border edge follows the whole border.
        CHECK(EdgeLoop(g, 0, 1).size() == 12);
        CHECK(EdgeLoop(g, 0, 5).empty());                    // not an edge (a diagonal)
        // Ring across a row of quads.
        EdgeList ring = EdgeRing(g, 0, 4);                   // (0,0)-(0,1): crosses the 3 quads of row 0
        CHECK(ring.size() == 4);
        PolyMesh c = Cube();
        int top0 = FindVert(c, {-0.5f, 0.5f, -0.5f}), top1 = FindVert(c, {0.5f, 0.5f, -0.5f});
        CHECK(top0 >= 0 && top1 >= 0);
        CHECK(EdgeRing(c, top0, top1).size() == 4);          // round the cube
        CHECK(EdgeLoop(c, top0, top1).size() == 1);          // cube corners are 3-way: no loop on
    }

    // ---- Loop cut ----
    {
        PolyMesh c = Cube();
        const int a = FindVert(c, {-0.5f, -0.5f, -0.5f}), b = FindVert(c, {-0.5f, 0.5f, -0.5f});   // a vertical edge
        EdgeList made = LoopCut(c, a, b);
        CHECK(made.size() == 4);
        CHECK(c.verts.size() == 12 && c.faces.size() == 10 && c.CountFaces(4) == 10);
        CHECK(c.Check().Closed() && c.Check().Valid() && c.EulerCharacteristic() == 2 && ConsistentWinding(c));
        bool mid = true;
        for (const auto& e : made) for (int v : {e.first, e.second}) if (!Near(c.verts[v].y, 0.0f)) mid = false;
        CHECK(mid);                                          // the new loop runs round the middle
        // Two cuts, evenly spaced; and a cut placed at 0.25.
        PolyMesh c2 = Cube();
        made = LoopCut(c2, a, b, 2);
        CHECK(made.size() == 8 && c2.verts.size() == 16 && c2.faces.size() == 14 && c2.Check().Closed());
        std::set<int> ys;
        for (const auto& e : made) ys.insert((int)std::lround(c2.verts[e.first].y * 6));
        CHECK(ys.size() == 2 && ys.count(-1) && ys.count(1));   // y = -1/6 and +1/6
        PolyMesh c3 = Cube();
        made = LoopCut(c3, a, b, 1, 0.25f);
        CHECK(!made.empty() && Near(c3.verts[made[0].first].y, -0.25f));
        // An open strip: the cut goes from border to border.
        PolyMesh g = Grid(3, 1);
        made = LoopCut(g, 0, 1);                             // ring across (0,0)-(1,0) and its row
        CHECK(made.size() == 1 && g.faces.size() == 4 && g.Check().Valid() && ConsistentWinding(g));
        // A ring ending at a triangle: the triangle takes the new vertex (becomes a quad).
        PolyMesh t = Grid(1, 1);
        t.verts.push_back({2, 0, 0.5f});
        PolyFace tri; tri.verts = {3, 2, 4}; t.faces.push_back(tri);   // (1,0) (1,1) (2,0.5)
        made = LoopCut(t, 0, 1);                              // ring (0,0)-(1,0) -> (0,1)-(1,1): stays in the quad
        CHECK(made.size() == 1 && t.Check().Valid() && ConsistentWinding(t));
        PolyMesh t2 = Grid(1, 1);
        t2.verts.push_back({2, 0, 0.5f});
        t2.faces.push_back(tri);
        made = LoopCut(t2, 0, 1);                             // (0,0)-(0,1) ring -> (1,0)-(1,1), shared with the triangle
        CHECK(made.size() == 1 && t2.CountFaces(4) == 3 && t2.CountFaces(3) == 0 && t2.Check().Valid() && ConsistentWinding(t2));
        // UVs follow the cut.
        PolyMesh u = Grid(1, 1);
        u.faces[0].uvs = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
        made = LoopCut(u, 0, 1);                              // cut across x at 0.5
        bool uvOk = true;
        for (const PolyFace& f : u.faces) for (int i = 0; i < f.Size(); ++i)
            if (!Near(f.uvs[i].x, u.verts[f.verts[i]].x) || !Near(f.uvs[i].y, u.verts[f.verts[i]].z)) uvOk = false;
        CHECK(u.faces.size() == 2 && uvOk);
        CHECK(LoopCut(u, 0, 3 + 10).empty());                 // not an edge: nothing happens
    }

    // ---- Edge slide ----
    {
        PolyMesh c = Cube();
        const int a = FindVert(c, {-0.5f, -0.5f, -0.5f}), b = FindVert(c, {-0.5f, 0.5f, -0.5f});
        EdgeList loop = LoopCut(c, a, b);
        const std::vector<Vec3> start = c.verts;
        CHECK(EdgeSlide(c, loop, 0.5f, &start) == 4);
        std::set<int> ys;
        for (const auto& e : loop) for (int v : {e.first, e.second}) ys.insert((int)std::lround(c.verts[v].y * 100));
        CHECK(ys.size() == 1 && std::abs(*ys.begin()) == 25);   // all the same way, a quarter up or down
        const int side = *ys.begin();
        CHECK(EdgeSlide(c, loop, -0.5f, &start) == 4);
        ys.clear();
        for (const auto& e : loop) for (int v : {e.first, e.second}) ys.insert((int)std::lround(c.verts[v].y * 100));
        CHECK(ys.size() == 1 && *ys.begin() == -side);         // the other way
        CHECK(EdgeSlide(c, loop, 0.0f, &start) == 4 && Near(c.verts[loop[0].first].y, 0.0f));
        // An open loop on a grid slides its end vertices along the border too.
        PolyMesh g = Grid(2, 2);
        EdgeList line = EdgeLoop(g, 3, 4);                     // (0,1)-(1,1)-(2,1)
        CHECK(line.size() == 2);
        const std::vector<Vec3> gs = g.verts;
        CHECK(EdgeSlide(g, line, 1.0f, &gs) == 3);
        bool allMoved = true;
        for (int v : {3, 4, 5}) if (Near(g.verts[v].z, 1.0f)) allMoved = false;
        CHECK(allMoved);
    }

    // ---- Dissolve ----
    {
        PolyMesh g = Grid(2, 1);                               // two quads side by side
        CHECK(DissolveEdges(g, {{1, 4}}) == 1);
        CHECK(g.faces.size() == 1 && g.faces[0].Size() == 4 && g.Check().Valid());   // the middle vertices go too
        PolyMesh g2 = Grid(2, 1);
        CHECK(DissolveEdges(g2, {{1, 4}}, false) == 1 && g2.faces[0].Size() == 6);
        CHECK(DissolveEdges(g2, {{0, 1}}) == 0);               // a border edge: nothing to merge with
        PolyMesh c = Cube();
        const int a = FindVert(c, {-0.5f, 0.5f, -0.5f}), b = FindVert(c, {0.5f, 0.5f, -0.5f});
        CHECK(DissolveEdges(c, {{a, b}}) == 1);
        CHECK(c.faces.size() == 5 && c.Check().Closed() && c.Check().Valid() && ConsistentWinding(c) && UsedVerts(c) == 6);
        PolyMesh g3 = Grid(2, 2);
        CHECK(DissolveVerts(g3, {4}) == 1);                    // the centre of a 2x2 grid
        CHECK(g3.faces.size() == 1 && g3.faces[0].Size() == 8 && g3.Check().Valid());
        // Dissolving a whole loop cut puts the cube back.
        PolyMesh c2 = Cube();
        EdgeList loop = LoopCut(c2, FindVert(c2, {-0.5f, -0.5f, -0.5f}), FindVert(c2, {-0.5f, 0.5f, -0.5f}));
        CHECK(DissolveEdges(c2, loop) == 4);
        CHECK(c2.faces.size() == 6 && c2.CountFaces(4) == 6 && UsedVerts(c2) == 8 && c2.Check().Closed());
    }

    // ---- Bridge ----
    {
        PolyMesh pm;
        AddTube(pm, 0, 1);
        AddTube(pm, 2, 3);
        CHECK(pm.Check().boundaryEdges == 16);
        EdgeList rims;
        for (int i = 0; i < 4; ++i) rims.push_back({4 + i, 4 + (i + 1) % 4});        // top of the first tube
        for (int i = 0; i < 4; ++i) rims.push_back({8 + i, 8 + (i + 1) % 4});        // bottom of the second
        std::string err;
        CHECK(BridgeEdgeLoops(pm, rims, &err) == 4);
        CHECK(pm.Check().boundaryEdges == 8 && pm.Check().nonManifoldEdges == 0 && ConsistentWinding(pm));
        // Each new face joins straight up (no twist).
        bool straight = true;
        for (int f = 8; f < 12; ++f) {
            const PolyFace& F = pm.faces[f];
            for (int i = 0; i < 4; ++i) {
                const Vec3 p = pm.verts[F.verts[i]], q = pm.verts[F.verts[(i + 1) % 4]];
                if (!Near(p.x, q.x) && !Near(p.z, q.z)) straight = false;
            }
        }
        CHECK(straight);
        PolyMesh bad;
        AddTube(bad, 0, 1);
        EdgeList one;
        for (int i = 0; i < 4; ++i) one.push_back({4 + i, 4 + (i + 1) % 4});
        CHECK(BridgeEdgeLoops(bad, one, &err) == 0 && !err.empty());
        PolyMesh c = Cube();
        CHECK(BridgeEdgeLoops(c, {{0, 1}}, &err) == 0 && err.find("border") != std::string::npos);
    }

    // ---- Bevel ----
    {
        const float w = 0.1f;
        PolyMesh c = Cube();
        const int a = FindVert(c, {-0.5f, 0.5f, -0.5f}), b = FindVert(c, {0.5f, 0.5f, -0.5f});
        std::string err;
        CHECK(BevelEdges(c, {{a, b}}, w, &err) == 1);
        CHECK(c.faces.size() == 7 && UsedVerts(c) == 10);
        CHECK(c.Check().Closed() && c.Check().Valid() && ConsistentWinding(c) && c.EulerCharacteristic() == 2);
        // The new corners sit the bevel width back from the old edge on both faces.
        CHECK(FindVert(c, {-0.5f, 0.5f, -0.4f}) >= 0 && FindVert(c, {-0.5f, 0.4f, -0.5f}) >= 0);
        CHECK(c.CountFaces(5) == 2);                            // the two end faces gained a corner
        // The rim of the top face: the top shrinks in, four chamfer faces round it.
        PolyMesh t = Cube();
        EdgeList rim;
        const int t0 = FindVert(t, {-0.5f, 0.5f, -0.5f}), t1 = FindVert(t, {0.5f, 0.5f, -0.5f});
        const int t2 = FindVert(t, {0.5f, 0.5f, 0.5f}), t3 = FindVert(t, {-0.5f, 0.5f, 0.5f});
        rim = {{t0, t1}, {t1, t2}, {t2, t3}, {t3, t0}};
        CHECK(BevelEdges(t, rim, w, &err) == 4);
        CHECK(t.faces.size() == 10 && UsedVerts(t) == 12 && t.Check().Closed() && ConsistentWinding(t) && t.EulerCharacteristic() == 2);
        CHECK(FindVert(t, {-0.4f, 0.5f, -0.4f}) >= 0);          // the inset top corner
        // Every edge of the cube: a corner patch where three bevels meet.
        PolyMesh all = Cube();
        EdgeList every;
        for (const auto& e : all.Edges()) every.push_back({e.a, e.b});
        CHECK(BevelEdges(all, every, w, &err) == 12);
        CHECK(all.faces.size() == 26 && UsedVerts(all) == 24 && all.CountFaces(3) == 8);
        CHECK(all.Check().Closed() && all.Check().Valid() && ConsistentWinding(all) && all.EulerCharacteristic() == 2);
        // Unused originals can be dropped, and the selection remapped.
        std::vector<int> remap = CompactVerts(all);
        CHECK(all.verts.size() == 24 && remap.size() == 8 + 24 && all.Check().Valid());
        // A border edge can't be beveled.
        PolyMesh g = Grid(1, 1);
        CHECK(BevelEdges(g, {{0, 1}}, w, &err) == 0 && !err.empty());
        // A plain edge in the middle of a grid: its ends have 3 other edges each.
        PolyMesh g3 = Grid(2, 2);
        CHECK(BevelEdges(g3, {{4, 5}}, w, &err) == 0);           // (1,1)-(2,1) ends on the border
        PolyMesh g4 = Grid(4, 4);
        const int c0 = 2 * 5 + 1, c1 = 2 * 5 + 2;               // (1,2)-(2,2), both 4-way inner vertices
        CHECK(BevelEdges(g4, {{c0, c1}}, w, &err) == 1);
        CHECK(g4.Check().Valid() && ConsistentWinding(g4) && g4.Check().boundaryEdges == 16);
    }

    // ---- Bevel with segments (a rounded profile) ----
    {
        const float w = 0.1f;
        std::string err;
        PolyMesh c = Cube();
        const int a = FindVert(c, {-0.5f, 0.5f, -0.5f}), b = FindVert(c, {0.5f, 0.5f, -0.5f});
        CHECK(BevelEdges(c, {{a, b}}, w, &err, 3) == 1);
        CHECK(c.faces.size() == 9 && UsedVerts(c) == 14);       // a strip of 3, two profile points each end
        CHECK(c.Check().Closed() && c.Check().Valid() && ConsistentWinding(c) && c.EulerCharacteristic() == 2);
        CHECK(c.CountFaces(7) == 2);                             // the end faces take the profile points too
        // Round: the profile points bulge out toward the old corner, past the flat chamfer.
        bool round = true;
        for (std::size_t i = 8; i < c.verts.size(); ++i) {
            const Vec3 p = c.verts[i];
            if (!Near(std::fabs(p.x), 0.5f)) continue;           // the points at the two ends
            const float out = (p.y - 0.4f) + (-0.4f - p.z);      // how far past the chamfer line y - z = 0.9
            if (out <= 1e-3f || p.y > 0.5f + 1e-5f || p.z < -0.5f - 1e-5f) round = false;
        }
        CHECK(round);
        // Flat profile (0) keeps every profile point on the chamfer line.
        PolyMesh f = Cube();
        CHECK(BevelEdges(f, {{a, b}}, w, &err, 4, 0.0f) == 1);
        bool flat = true;
        for (const Vec3& p : f.verts) if (std::fabs(p.x) > 0.49f && p.y > 0.39f && p.z < -0.39f && !Near(p.y - p.z, 0.9f) && !Near(p.y, 0.5f)) flat = false;
        CHECK(flat && f.Check().Closed());
        // A loop (two bevels through each vertex), every edge (corner patches), and a
        // chain end inside a grid (a cap): all still one closed, consistent shell.
        PolyMesh t = Cube();
        const int t0 = FindVert(t, {-0.5f, 0.5f, -0.5f}), t1 = FindVert(t, {0.5f, 0.5f, -0.5f});
        const int t2 = FindVert(t, {0.5f, 0.5f, 0.5f}), t3 = FindVert(t, {-0.5f, 0.5f, 0.5f});
        CHECK(BevelEdges(t, {{t0, t1}, {t1, t2}, {t2, t3}, {t3, t0}}, w, &err, 3) == 4);
        CHECK(t.faces.size() == 6 + 12 && t.Check().Closed() && ConsistentWinding(t) && t.EulerCharacteristic() == 2);
        PolyMesh all = Cube();
        EdgeList every;
        for (const auto& e : all.Edges()) every.push_back({e.a, e.b});
        CHECK(BevelEdges(all, every, w, &err, 2) == 12);
        CHECK(all.Check().Closed() && all.Check().Valid() && ConsistentWinding(all) && all.EulerCharacteristic() == 2);
        PolyMesh g4 = Grid(4, 4);
        CHECK(BevelEdges(g4, {{2 * 5 + 1, 2 * 5 + 2}}, w, &err, 3) == 1);
        CHECK(g4.Check().Valid() && ConsistentWinding(g4) && g4.Check().boundaryEdges == 16);
    }

    // ---- Fill, grid fill, merge ----
    {
        std::string err;
        // An open box (cube without its top): Fill closes it.
        PolyMesh box = Cube();
        int top = -1;
        for (int f = 0; f < (int)box.faces.size(); ++f) if (box.FaceNormal(f).y > 0.9f) top = f;
        box.faces.erase(box.faces.begin() + top);
        EdgeList rim;
        for (const auto& e : box.Edges()) if (e.faceCount == 1) rim.push_back({e.a, e.b});
        CHECK(rim.size() == 4);
        PolyMesh filled = box;
        CHECK(FillFaces(filled, rim, &err) == 1);
        CHECK(filled.Check().Closed() && ConsistentWinding(filled) && filled.FaceNormal((int)filled.faces.size() - 1).y > 0.9f);
        PolyMesh cube = Cube();
        CHECK(FillFaces(cube, {{0, 1}, {1, 2}}, &err) == 0 && !err.empty());   // edges between faces: refused
        // A square hole in a grid: Grid Fill puts quads back, the centre where it was.
        PolyMesh g = Grid(4, 4);
        auto at = [](int x, int z) { return z * 5 + x; };
        for (int f = (int)g.faces.size() - 1; f >= 0; --f) {
            Vec3 c{0, 0, 0};
            for (int v : g.faces[f].verts) c += g.verts[v];
            c = c * 0.25f;
            if (c.x > 1 && c.x < 3 && c.z > 1 && c.z < 3) g.faces.erase(g.faces.begin() + f);
        }
        CHECK(g.faces.size() == 12);
        EdgeList hole;
        const int ring[8] = {at(1, 1), at(2, 1), at(3, 1), at(3, 2), at(3, 3), at(2, 3), at(1, 3), at(1, 2)};
        for (int i = 0; i < 8; ++i) hole.push_back({ring[i], ring[(i + 1) % 8]});
        PolyMesh gf = g;
        CHECK(GridFill(gf, hole, 0, &err) == 4);
        CHECK(gf.faces.size() == 16 && gf.CountFaces(4) == 16 && gf.Check().boundaryEdges == 16 && ConsistentWinding(gf));
        CHECK(FindVert(gf, {2, 0, 2}) >= 0);                     // the new centre vertex lands where the old one was
        bool up = true;
        for (int f = 12; f < 16; ++f) if (gf.FaceNormal(f).y < 0.9f) up = false;
        CHECK(up);
        PolyMesh odd = Grid(1, 1);
        odd.faces.clear();
        PolyFace tri; tri.verts = {0, 1, 3}; odd.faces.push_back(tri);
        CHECK(GridFill(odd, {{0, 1}, {1, 3}, {3, 0}}, 0, &err) == 0 && err.find("even") != std::string::npos);
        // Grid fill with UVs: the inner UV blends the rim.
        PolyMesh gu = g;
        for (PolyFace& f : gu.faces) { f.uvs.clear(); for (int v : f.verts) f.uvs.push_back({gu.verts[v].x / 4, gu.verts[v].z / 4}); }
        CHECK(GridFill(gu, hole, 0, &err) == 4);
        bool uvOk = true;
        for (int f = 12; f < 16; ++f) for (int i = 0; i < 4; ++i) {
            const Vec3 p = gu.verts[gu.faces[f].verts[i]];
            if (!Near(gu.faces[f].uvs[i].x, p.x / 4) || !Near(gu.faces[f].uvs[i].y, p.z / 4)) uvOk = false;
        }
        CHECK(uvOk);
        // Merge: the top four corners of a cube into one point makes a pyramid.
        PolyMesh m = Cube();
        std::vector<int> topV;
        for (int i = 0; i < (int)m.verts.size(); ++i) if (m.verts[i].y > 0) topV.push_back(i);
        CHECK(MergeVerts(m, topV, {0, 0.5f, 0}) == 3);
        CHECK(m.faces.size() == 5 && m.CountFaces(3) == 4 && UsedVerts(m) == 5 && m.Check().Closed() && ConsistentWinding(m));
        CHECK(MergeVerts(m, {0}, {0, 0, 0}) == 0);
    }

    // ---- Knife ----
    {
        PolyMesh q = Grid(1, 1);                                 // corners 0 (0,0) 1 (1,0) 2 (0,1) 3 (1,1)
        std::string err;
        EdgeList cut = KnifeCut(q, {{0, 1, 0.5f}, {2, 3, 0.5f}}, &err);
        CHECK(cut.size() == 1 && q.faces.size() == 2 && q.CountFaces(4) == 2 && ConsistentWinding(q));
        CHECK(Near(q.verts[cut[0].first].x, 0.5f) && Near(q.verts[cut[0].second].x, 0.5f));
        // Corner to corner.
        PolyMesh d = Grid(1, 1);
        cut = KnifeCut(d, {{0, -1, 0}, {3, -1, 0}}, &err);
        CHECK(cut.size() == 1 && d.CountFaces(3) == 2);
        // Across two faces: a path through three points.
        PolyMesh g = Grid(2, 1);
        cut = KnifeCut(g, {{0, 1, 0.5f}, {1, 4, 0.5f}, {1, 2, 0.5f}}, &err);
        CHECK(cut.size() == 2 && g.faces.size() == 4 && g.Check().Valid() && ConsistentWinding(g));
        // Points on faces that don't meet: refused, the mesh untouched.
        PolyMesh h = Grid(3, 1);
        const PolyMesh before = h;
        cut = KnifeCut(h, {{0, 1, 0.5f}, {2, 3, 0.5f}}, &err);
        CHECK(cut.empty() && err.find("same face") != std::string::npos && h.faces.size() == before.faces.size() && h.verts.size() == before.verts.size());
        // Two points on the same edge line split it twice.
        PolyMesh e = Grid(1, 1);
        cut = KnifeCut(e, {{0, 1, 0.25f}, {2, 3, 0.25f}, {2, 3, 0.75f}, {0, 1, 0.75f}}, &err);
        CHECK(cut.size() == 3 && e.Check().Valid() && ConsistentWinding(e) && e.faces.size() == 3);
    }

    // ---- Through a Mesh, as the editor does it (vertex numbers kept) ----
    {
        Mesh m = Mesh::Cube(1.0f);
        m.WeldVertices();
        PolyMesh pm = PolyMesh::FromMesh(m, {false, true, 1.0f});
        CHECK(pm.verts.size() == m.vertices.size());
        const int a = 0, b = pm.faces[0].verts[1];
        EdgeList made = LoopCut(pm, a, b);
        Mesh back = pm.ToMesh();
        CHECK(back.vertices.size() == pm.verts.size());
        bool same = true;
        for (std::size_t i = 0; i < m.vertices.size(); ++i) if ((back.vertices[i] - m.vertices[i]).SqrMagnitude() > 1e-12f) same = false;
        CHECK(same && !made.empty());
        PolyMesh again = PolyMesh::FromMesh(back, {false, true, 1.0f});
        CHECK(again.CountFaces(4) == 10);                        // all quads, straight back
    }

    TEST_MAIN_RESULT();
}
