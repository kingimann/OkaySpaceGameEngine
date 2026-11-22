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

    // ---- Edge slide carries the UVs along (per corner, seams stay split) ----
    {
        // A 2 x 2 grid of unit quads, textured in four islands: u = x / 2 (+5 right of x = 1),
        // v = z / 2 (+3 above z = 1). The lines x = 1 and z = 1 are seams, so the middle vertex
        // (1, 1) has four different UVs, one in each face.
        PolyMesh g = Grid(2, 2);
        auto uvAt = [](int face, const Vec3& p) {
            const int fx = face % 2, fz = face / 2;           // Grid() makes faces row by row
            return Vec2{p.x * 0.5f + (fx ? 5.0f : 0.0f), p.z * 0.5f + (fz ? 3.0f : 0.0f)};
        };
        for (int f = 0; f < (int)g.faces.size(); ++f)
            for (int v : g.faces[f].verts) g.faces[f].uvs.push_back(uvAt(f, g.verts[v]));
        g.SetSeam(1, 4, true); g.SetSeam(4, 7, true); g.SetSeam(3, 4, true); g.SetSeam(4, 5, true);
        const EdgeList line = EdgeLoop(g, 3, 4);              // (0,1)-(1,1)-(2,1)
        const std::vector<Vec3> gs = g.verts;
        CHECK(EdgeSlide(g, line, 0.5f, &gs) == 3);
        // Which way it went: half-way to z = 0 or to z = 2.
        const float z = g.verts[4].z;
        CHECK(Near(z, 0.5f) || Near(z, 1.5f));
        // Every corner's UV is its face's island mapping at the corner's new position.
        auto uvsFollow = [&](const PolyMesh& pm) {
            float worst = 0;
            for (int f = 0; f < (int)pm.faces.size(); ++f)
                for (int i = 0; i < pm.faces[f].Size(); ++i) {
                    const Vec2 want = uvAt(f, pm.verts[pm.faces[f].verts[i]]), got = pm.faces[f].uvs[i];
                    worst = std::fmax(worst, std::fabs(want.x - got.x) + std::fabs(want.y - got.y));
                }
            return worst;
        };
        const float err = uvsFollow(g);
        CHECK(err < 1e-5f);
        if (err >= 1e-5f) std::cerr << "  edge slide UV error " << err << "\n";
        // By hand for the middle vertex, if it slid down to z = 0.5: lower-left face (0, 0.25),
        // lower-right (5.5, 0.25), upper-left (0.5, 3.25), upper-right (5.5, 3.25) - still four UVs.
        std::set<std::pair<int, int>> mid;
        for (const PolyFace& f : g.faces)
            for (int i = 0; i < f.Size(); ++i)
                if (f.verts[i] == 4) mid.insert({(int)std::lround(f.uvs[i].x * 100), (int)std::lround(f.uvs[i].y * 100)});
        if (Near(z, 0.5f)) CHECK(mid == (std::set<std::pair<int, int>>{{50, 25}, {550, 25}, {50, 325}, {550, 325}}));
        else CHECK(mid == (std::set<std::pair<int, int>>{{50, 75}, {550, 75}, {50, 375}, {550, 375}}));
        // Sliding again from the same start (interactive use) gives the same answer, not a sum.
        CHECK(EdgeSlide(g, line, 0.25f, &gs) == 3);
        CHECK(uvsFollow(g) < 1e-5f);
        CHECK(EdgeSlide(g, line, 0.0f, &gs) == 3);
        CHECK(uvsFollow(g) < 1e-5f && Near(g.verts[4].z, 1.0f));
        // A mesh without UVs still slides (and gets none).
        PolyMesh plain = Grid(2, 2);
        const std::vector<Vec3> ps = plain.verts;
        CHECK(EdgeSlide(plain, line, 0.5f, &ps) == 3 && plain.faces[0].uvs.empty());
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

    // ---- Bridge two face groups: the faces are removed, then their rims are bridged ----
    {
        // Two unit cubes side by side along x (gap 1): A at x -0.5..0.5, B at x 1.5..2.5.
        PolyMesh pm = Cube();
        PolyMesh b = Cube();
        const int off = (int)pm.verts.size();
        for (Vec3 p : b.verts) pm.verts.push_back(p + Vec3{2, 0, 0});
        for (PolyFace f : b.faces) { for (int& v : f.verts) v += off; pm.faces.push_back(f); }
        auto faceAtX = [&](float x) {
            for (int f = 0; f < (int)pm.faces.size(); ++f) {
                bool all = true;
                for (int v : pm.faces[f].verts) if (!Near(pm.verts[v].x, x)) all = false;
                if (all) return f;
            }
            return -1;
        };
        const int fa = faceAtX(0.5f), fb = faceAtX(1.5f);    // the two facing faces
        CHECK(fa >= 0 && fb >= 0);
        std::string err;
        // 12 faces - 2 removed + 4 bridge quads = 14; 16 vertices; 12 + 12 + 4 = 28 edges;
        // V - E + F = 16 - 28 + 14 = 2: one closed shell.
        CHECK(BridgeFaces(pm, {fa, fb}, &err) == 4);
        if (!err.empty()) std::cerr << "  bridge faces: " << err << "\n";
        CHECK(pm.faces.size() == 14 && pm.verts.size() == 16 && UsedVerts(pm) == 16);
        CHECK(pm.Edges().size() == 28 && pm.EulerCharacteristic() == 2);
        CHECK(pm.Check().Closed() && pm.Check().Valid() && ConsistentWinding(pm));
        // The faces at x = 0.5 and x = 1.5 are gone; the new ones run straight along x.
        CHECK(faceAtX(0.5f) < 0 && faceAtX(1.5f) < 0);
        bool straight = true;
        for (int f = 10; f < 14; ++f)
            for (int i = 0; i < 4; ++i) {
                const Vec3 p = pm.verts[pm.faces[f].verts[i]], q = pm.verts[pm.faces[f].verts[(i + 1) % 4]];
                if (!(Near(p.y, q.y) && Near(p.z, q.z)) && !Near(p.x, q.x)) straight = false;
            }
        CHECK(straight);
        // Outward: every new face's normal points away from the x axis through the shell.
        bool outward = true;
        for (int f = 10; f < 14; ++f) {
            Vec3 c{0, 0, 0};
            for (int v : pm.faces[f].verts) c += pm.verts[v];
            c = c * 0.25f;
            if (Vec3::Dot(pm.FaceNormal(f), Vec3{0, c.y, c.z}) <= 0) outward = false;
        }
        CHECK(outward);

        // Groups of more than one face: two 1 x 2 strips (the facing sides of two boxes cut in half).
        PolyMesh two = Cube();
        LoopCut(two, FindVert(two, {-0.5f, -0.5f, -0.5f}), FindVert(two, {-0.5f, 0.5f, -0.5f}));   // cut around y
        PolyMesh other = two;
        const int off2 = (int)two.verts.size();
        for (Vec3 p : other.verts) two.verts.push_back(p + Vec3{2, 0, 0});
        for (PolyFace f : other.faces) { for (int& v : f.verts) v += off2; two.faces.push_back(f); }
        std::vector<int> sel;
        for (int f = 0; f < (int)two.faces.size(); ++f) {
            bool a = true, bb = true;
            for (int v : two.faces[f].verts) { if (!Near(two.verts[v].x, 0.5f)) a = false; if (!Near(two.verts[v].x, 1.5f)) bb = false; }
            if (a || bb) sel.push_back(f);
        }
        CHECK(sel.size() == 4);
        const std::size_t before = two.faces.size();
        CHECK(BridgeFaces(two, sel, &err) == 6);               // a rim of 6 edges on each side
        CHECK(two.faces.size() == before - 4 + 6 && two.Check().Closed() && ConsistentWinding(two) && two.EulerCharacteristic() == 2);

        // Refusals leave the mesh exactly as it was.
        PolyMesh c = Cube();
        const PolyMesh c0 = c;
        err.clear();
        CHECK(BridgeFaces(c, {0}, &err) == 0 && !err.empty());                // one group only
        CHECK(c.faces.size() == c0.faces.size());
        err.clear();
        int adjA = -1, adjB = -1;                                          // two faces sharing an edge
        for (int f = 0; f < 6 && adjB < 0; ++f) for (int g2 = f + 1; g2 < 6 && adjB < 0; ++g2) {
            int shared = 0;
            for (int v : c.faces[f].verts) for (int w : c.faces[g2].verts) if (v == w) ++shared;
            if (shared == 2) { adjA = f; adjB = g2; }
        }
        CHECK(BridgeFaces(c, {adjA, adjB}, &err) == 0 && !err.empty());       // they touch: one group
        CHECK(c.faces.size() == 6 && c.Check().Closed());
        err.clear();
        CHECK(BridgeFaces(c, {0, 99}, &err) == 0 && !err.empty());            // a face that doesn't exist
        CHECK(c.faces.size() == 6);
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

    // ---- Selection helpers: shortest path, checker deselect, region outline, problem edges ----
    {
        PolyMesh g = Grid(4, 4);                                 // vertex (x, z) = z * 5 + x, face (x, z) = z * 4 + x
        // Along the bottom row: 0 -> 4 is the straight run of 5 vertices.
        std::vector<int> p = ShortestVertPath(g, 0, 4);
        CHECK(p.size() == 5 && p.front() == 0 && p.back() == 4);
        bool row = true; for (int v : p) if (g.verts[v].z != 0.0f) row = false;
        CHECK(row);
        // Corner to corner: 8 unit steps (no diagonals in a quad grid), every step an edge.
        p = ShortestVertPath(g, 0, 24);
        CHECK(p.size() == 9);
        PolyTopology T(g);
        bool edges = true; for (std::size_t i = 1; i < p.size(); ++i) if (!T.IsEdge(p[i - 1], p[i])) edges = false;
        CHECK(edges);
        CHECK(ShortestVertPath(g, 7, 7) == std::vector<int>{7});
        // Hostile: out of range, negative, a vertex no face uses, two separate parts.
        CHECK(ShortestVertPath(g, 0, 99).empty());
        CHECK(ShortestVertPath(g, -1, 3).empty());
        PolyMesh lone = g; lone.verts.push_back({9, 9, 9});
        CHECK(ShortestVertPath(lone, 0, 25).empty());
        PolyMesh two = Grid(1, 1); AddTube(two, 5, 6);           // a quad and a tube that never touch
        CHECK(ShortestVertPath(two, 0, 6).empty());
        CHECK(ShortestVertPath(PolyMesh{}, 0, 0).empty());

        // Faces: (0,0) -> (3,0) is the 4 faces of the bottom row; (0,0) -> (3,3) is 7 faces.
        std::vector<int> fp = ShortestFacePath(g, 0, 3);
        CHECK(fp == (std::vector<int>{0, 1, 2, 3}));
        fp = ShortestFacePath(g, 0, 15);
        CHECK(fp.size() == 7 && fp.front() == 0 && fp.back() == 15);
        CHECK(ShortestFacePath(g, 5, 5) == std::vector<int>{5});
        CHECK(ShortestFacePath(g, 0, 16).empty() && ShortestFacePath(g, -2, 0).empty());
        CHECK(ShortestFacePath(two, 0, 1).empty());              // the quad and the tube are not joined
        PolyMesh bad = g; bad.faces[1].verts = {0, 0, 1};        // a face with a repeated corner is still walked round
        CHECK(!ShortestFacePath(bad, 0, 3).empty());
        PolyMesh oob = g; oob.faces[1].verts[2] = 999;           // a face naming a missing vertex is skipped, never read
        CHECK(ShortestFacePath(oob, 0, 1).empty());
        CHECK(ShortestVertPath(oob, 0, 4).size() >= 5);

        // Checker: a 4x4 block of faces comes back as a checkerboard of 8.
        std::vector<int> all; for (int f = 0; f < 16; ++f) all.push_back(f);
        std::vector<int> ck = CheckerFaces(g, all);
        CHECK(ck.size() == 8);
        bool board = true; for (int f : ck) if (((f % 4) + (f / 4)) % 2 != 0) board = false;
        CHECK(board);
        // Starting from another face flips which squares stay.
        std::vector<int> fromOne = all; std::swap(fromOne[0], fromOne[1]);
        ck = CheckerFaces(g, fromOne);
        board = ck.size() == 8; for (int f : ck) if (((f % 4) + (f / 4)) % 2 != 1) board = false;
        CHECK(board);
        // Vertices: the 5 of the bottom row keep 0, 2, 4.
        CHECK(CheckerVerts(g, {0, 1, 2, 3, 4}) == (std::vector<int>{0, 2, 4}));
        // Two separate selected vertices each keep themselves (each part starts its own walk).
        CHECK(CheckerVerts(g, {0, 24}) == (std::vector<int>{0, 24}));
        // Hostile: empty, repeats, out of range.
        CHECK(CheckerFaces(g, {}).empty());
        CHECK(CheckerFaces(g, {0, 0, 0, -1, 99}) == std::vector<int>{0});
        CHECK(CheckerVerts(g, {3, 3, 999}) == std::vector<int>{3});

        // Region outline: the middle 2x2 block of faces has an 8-edge outline, one closed loop.
        const std::vector<int> mid = {5, 6, 9, 10};
        EdgeList ol = RegionOutline(g, mid);
        CHECK(ol.size() == 8);
        const std::vector<EdgeChain> ch = OrderChains(ol);
        CHECK(ch.size() == 1 && ch[0].closed);
        // Directions follow the faces: no directed edge twice, so the loop runs one way round.
        std::set<std::pair<int, int>> dir(ol.begin(), ol.end());
        CHECK(dir.size() == 8);
        CHECK(RegionOutline(g, {}).empty() && RegionOutline(g, {-1, 77}).empty());
        CHECK(RegionOutline(g, {5, 5}).size() == 4);             // a repeated face counts once

        // Problem edges: a closed cube has none; the grid has 16 border edges; a fin makes a non-manifold edge.
        CHECK(ProblemEdges(Cube(), true, true).empty());
        CHECK(ProblemEdges(g, true, false).size() == 16);
        CHECK(ProblemEdges(g, false, true).empty());
        PolyMesh fin = Grid(2, 1);                               // two quads sharing edge 1-4; add a third face on it
        fin.verts.push_back({1, 1, 0}); fin.verts.push_back({1, 1, 1});
        PolyFace third; third.verts = {1, 4, 7, 6}; fin.faces.push_back(third);
        const EdgeList nm = ProblemEdges(fin, false, true);
        CHECK(nm.size() == 1 && nm[0] == std::make_pair(1, 4));
        CHECK(ProblemEdges(PolyMesh{}, true, true).empty());
    }

    // ---- Fill refuses a chain of vertices in a straight line ----
    // Bug: Fill on three border edges along one side of a grid (vertices 0-1-2-3, all on a line) made a
    // four-corner face with no area: invisible, no usable normal, and it closed nothing.
    {
        PolyMesh g = Grid(3, 3);
        const std::size_t before = g.faces.size();
        std::string err;
        CHECK(FillFaces(g, {{0, 1}, {1, 2}, {2, 3}}, &err) == 0);
        CHECK(g.faces.size() == before);
        CHECK(err.find("straight line") != std::string::npos);
        // A real corner (two sides of the grid) still fills: 0-1-2-3 then up the side to 7.
        PolyMesh c = Grid(3, 3);
        err.clear();
        CHECK(FillFaces(c, {{0, 1}, {1, 2}, {2, 3}, {3, 7}}, &err) == 1);
        CHECK(c.faces.size() == before + 1);
        // A collinear chain next to a good loop: the good one is filled, the flat one skipped.
        PolyMesh two = Grid(3, 3); AddTube(two, 5, 6);
        err.clear();
        CHECK(FillFaces(two, {{0, 1}, {1, 2}, {2, 3}, {20, 21}, {21, 22}, {22, 23}, {23, 20}}, &err) == 1);
    }

    // ---- Round 3: hostile inputs the round-2 probe found and left (modeling_tab_audit.md) ----
    // SplitEdge with t outside 0..1 put the new vertex beyond the edge; it now clamps to the edge.
    {
        PolyMesh g = Grid(1, 1);                                 // edge 0 (0,0,0) - 1 (1,0,0)
        const int v = SplitEdge(g, 0, 1, 2.0f);
        CHECK(v >= 0 && v < (int)g.verts.size() && g.verts[v].x >= 0.0f && g.verts[v].x <= 1.0f);
        PolyMesh h = Grid(1, 1);
        const int w = SplitEdge(h, 0, 1, -5.0f);
        CHECK(w >= 0 && w < (int)h.verts.size() && h.verts[w].x >= 0.0f && h.verts[w].x <= 1.0f);
        PolyMesh k = Grid(1, 1);
        CHECK(SplitEdge(k, 0, 1, NAN) == -1 && k.verts.size() == 4);   // not a number: refused, as before
    }
    // LoopCut had no upper limit on the number of cuts (the editor capped it at 32; a script could ask for millions).
    {
        PolyMesh g = Grid(1, 3);                                 // a ring of 4 edges across 3 quads
        const EdgeList made = LoopCut(g, 0, 1, 500);
        CHECK(!made.empty());
        CHECK((int)made.size() <= kLoopCutMax * 3);
        CHECK(kLoopCutMax >= 32);                                // the editor's own cap still fits
    }
    // A knife cut between two points on the same edge reported one new edge but split no face.
    {
        PolyMesh q = Grid(1, 1);
        const PolyMesh before = q;
        std::string err;
        const EdgeList cut = KnifeCut(q, {{0, 1, 0.25f}, {0, 1, 0.75f}}, &err);
        CHECK(cut.empty());
        CHECK(err.find("edge") != std::string::npos);
        CHECK(q.verts.size() == before.verts.size() && q.faces.size() == before.faces.size());
        // Corner to the next corner along an edge: also nothing to cut.
        PolyMesh c = Grid(1, 1);
        err.clear();
        CHECK(KnifeCut(c, {{0, -1, 0}, {1, -1, 0}}, &err).empty() && !err.empty() && c.faces.size() == 1);
    }

    // ---- Round 3 tools ----
    // Give every corner the UV of its position (x / 4, z / 4), so a tool's UVs can be checked against where corners are.
    auto planarUVs = [](PolyMesh& pm) {
        for (PolyFace& f : pm.faces) { f.uvs.clear(); for (int v : f.verts) f.uvs.push_back(Vec2{pm.verts[v].x * 0.25f, pm.verts[v].z * 0.25f}); }
    };
    auto uvsMatchPositions = [](const PolyMesh& pm) {
        for (const PolyFace& f : pm.faces) {
            if (f.uvs.size() != f.verts.size()) return false;
            for (std::size_t i = 0; i < f.verts.size(); ++i) {
                const Vec3& p = pm.verts[f.verts[i]];
                if (!Near(f.uvs[i].x, p.x * 0.25f, 1e-3f) || !Near(f.uvs[i].y, p.z * 0.25f, 1e-3f)) return false;
            }
        }
        return true;
    };
    // Edge Split: the outline of the cube's top face splits it off (one copy per corner) and marks the edges sharp.
    {
        PolyMesh c = Cube();
        int topF = -1;
        for (int f = 0; f < (int)c.faces.size(); ++f) if (c.FaceNormal(f).y > 0.9f) topF = f;
        CHECK(topF >= 0);
        const EdgeList ol = RegionOutline(c, {topF});
        CHECK(EdgeSplit(c, ol) == 4);
        CHECK(c.verts.size() == 12 && c.faces.size() == 6 && c.Check().boundaryEdges == 8 && c.Check().Valid() && ConsistentWinding(c));
        CHECK(c.sharp.size() == 8);
        bool marked = true; for (const auto& e : ol) if (!c.IsSharp(e.first, e.second)) marked = false;
        CHECK(marked);
        // Through a Mesh: the sharp marks go to Mesh::sharpEdges and come back.
        const Mesh m = c.ToMesh(nullptr, false);
        CHECK(m.sharpEdges.size() == 8 && PolyMesh::FromMesh(m, {false, true, 1.0f}).sharp.size() == 8);
        // Saved and read back as a .okaymesh.
        PolyMesh back;
        CHECK(PolyMesh::Parse(c.Serialize(), back) && back.sharp.size() == 8);
        // One edge alone splits nothing (its ends stay joined round the other side) but is marked sharp.
        PolyMesh one = Cube();
        const auto e0 = one.Edges()[0];
        CHECK(EdgeSplit(one, {{e0.a, e0.b}}) == 0 && one.verts.size() == 8 && one.IsSharp(e0.a, e0.b));
        // Hostile: nothing, bad numbers, border edges (one face only).
        PolyMesh g = Grid(2, 2);
        CHECK(EdgeSplit(g, {}) == 0 && EdgeSplit(g, {{-1, 3}, {2, 2}, {0, 999}}) == 0 && EdgeSplit(g, {{0, 1}, {1, 2}}) == 0);
        CHECK(g.verts.size() == 9 && g.sharp.empty());
        // UVs stay with their faces.
        PolyMesh gu = Grid(2, 2); planarUVs(gu);
        CHECK(EdgeSplit(gu, {{1, 4}, {4, 7}}) == 3 && uvsMatchPositions(gu) && gu.Check().Valid());
    }
    // Vertex Slide: the middle vertex of a 2x2 grid slides along the edge pointing most nearly the way asked.
    {
        PolyMesh g = Grid(2, 2); planarUVs(g);   // vertex 4 at (1, 0, 1)
        const std::vector<Vec3> start = g.verts;
        CHECK(VertexSlide(g, {4}, Vec3{1, 0, 0.1f}, 0.5f, &start) == 1);
        CHECK(Near(g.verts[4].x, 1.5f) && Near(g.verts[4].z, 1.0f) && uvsMatchPositions(g));
        // Interactive: again from the same start replaces the last slide.
        CHECK(VertexSlide(g, {4}, Vec3{1, 0, 0}, 0.25f, &start) == 1 && Near(g.verts[4].x, 1.25f) && uvsMatchPositions(g));
        // More than 1 stops at the neighbour.
        CHECK(VertexSlide(g, {4}, Vec3{0, 0, -1}, 5.0f, &start) == 1 && Near(g.verts[4].z, 0.0f) && Near(g.verts[4].x, 1.0f));
        // Hostile: not a number, no direction, bad indices, a short start list.
        PolyMesh h = Grid(2, 2); const PolyMesh hb = h;
        CHECK(VertexSlide(h, {4}, Vec3{1, 0, 0}, NAN) == 0 && VertexSlide(h, {4}, Vec3{0, 0, 0}, 0.5f) == 0);
        CHECK(VertexSlide(h, {4}, Vec3{NAN, 0, 0}, 0.5f) == 0 && VertexSlide(h, {-1, 99}, Vec3{1, 0, 0}, 0.5f) == 0);
        const std::vector<Vec3> shortList(2);
        CHECK(VertexSlide(h, {4}, Vec3{1, 0, 0}, 0.5f, &shortList) == 0);
        bool same = true; for (std::size_t i = 0; i < h.verts.size(); ++i) if ((h.verts[i] - hb.verts[i]).SqrMagnitude() > 0) same = false;
        CHECK(same);
        // Two picked neighbours do not slide onto each other: 3's only edge toward +X goes to 4 (picked), so it stays.
        PolyMesh two = Grid(2, 2);
        CHECK(VertexSlide(two, {3, 4}, Vec3{1, 0, 0}, 1.0f) == 1 && Near(two.verts[3].x, 0.0f) && Near(two.verts[4].x, 2.0f));
    }
    // Offset Edge Loops: the edge between two stacked quads gets a parallel loop on each side.
    {
        PolyMesh g = Grid(1, 2); planarUVs(g);   // edge 2-3 at z = 1
        std::string err;
        const EdgeList made = OffsetEdgeLoops(g, {{2, 3}}, 0.25f, &err);
        CHECK(made.size() == 2 && g.faces.size() == 4 && g.verts.size() == 10 && g.Check().Valid() && ConsistentWinding(g));
        bool zs = true; std::set<int> zSeen;
        for (const auto& e : made) {
            const float z0 = g.verts[e.first].z, z1 = g.verts[e.second].z;
            if (!Near(z0, z1)) zs = false;
            zSeen.insert((int)std::lround(z0 * 100));
        }
        CHECK(zs && zSeen == (std::set<int>{75, 125}));
        CHECK(uvsMatchPositions(g));
        // Three edges in a row across a 3x2 grid: six new edges, every face of both rows split.
        PolyMesh r = Grid(3, 2);
        CHECK(OffsetEdgeLoops(r, {{4, 5}, {5, 6}, {6, 7}}, 0.3f, &err).size() == 6 && r.faces.size() == 12 && r.Check().Valid() && ConsistentWinding(r));
        // A border edge: only the side with a face.
        PolyMesh b = Grid(1, 1);
        CHECK(OffsetEdgeLoops(b, {{0, 1}}, 0.5f, &err).size() == 1 && b.faces.size() == 2);
        // Hostile: not a number, nothing picked, an amount past the ends (clamped), an edge joining two picked edges.
        PolyMesh h = Grid(1, 2); const std::size_t nv = h.verts.size();
        err.clear(); CHECK(OffsetEdgeLoops(h, {{2, 3}}, NAN, &err).empty() && !err.empty() && h.verts.size() == nv);
        err.clear(); CHECK(OffsetEdgeLoops(h, {}, 0.3f, &err).empty() && !err.empty());
        err.clear(); CHECK(OffsetEdgeLoops(h, {{-1, 2}, {0, 99}}, 0.3f, &err).empty() && !err.empty());
        CHECK(OffsetEdgeLoops(h, {{2, 3}}, 9.0f, &err).size() == 2 && h.Check().Valid());
        PolyMesh rung = Grid(1, 1);   // edges 0-1 and 2-3 both picked: the sides 0-2 and 1-3 join them
        CHECK(OffsetEdgeLoops(rung, {{0, 1}, {2, 3}}, 0.3f, &err).size() == 2 && rung.faces.size() == 3 && rung.Check().Valid());
        PolyMesh wide = Grid(1, 1);
        err.clear(); CHECK(OffsetEdgeLoops(wide, {{0, 1}, {2, 3}}, 0.6f, &err).empty() && err.find("0.5") != std::string::npos && wide.faces.size() == 1);
    }
    // Rip: tear a 2x2 grid open along its middle column of edges; the +X side gets new vertices.
    {
        PolyMesh g = Grid(2, 2); planarUVs(g);
        std::string err;
        const std::vector<int> nv = RipEdges(g, {{1, 4}, {4, 7}}, Vec3{1, 0, 0}, &err);
        CHECK(nv.size() == 3 && g.verts.size() == 12 && g.Check().boundaryEdges == 12 && g.Check().Valid() && ConsistentWinding(g));
        // The new vertices sit where 1, 4, 7 were, and only faces on the +X side use them.
        bool rightSide = true;
        for (const PolyFace& f : g.faces) {
            float cx = 0; for (int v : f.verts) cx += g.verts[v].x; cx /= (float)f.Size();
            for (int v : f.verts) if (std::find(nv.begin(), nv.end(), v) != nv.end() && cx < 1.0f) rightSide = false;
        }
        CHECK(rightSide && uvsMatchPositions(g));
        // A tear that ends inside: only the middle vertex splits.
        PolyMesh in = Grid(4, 4);
        CHECK(RipEdges(in, {{11, 12}, {12, 13}}, Vec3{0, 0, 1}, &err).size() == 1 && in.Check().Valid() && ConsistentWinding(in));
        // Hostile: one edge inside (both ends stay joined), border edges only, not a number, nothing.
        PolyMesh h = Grid(4, 4); const std::size_t before = h.verts.size();
        err.clear(); CHECK(RipEdges(h, {{11, 12}}, Vec3{0, 0, 1}, &err).empty() && err.find("Nothing to rip") != std::string::npos && h.verts.size() == before);
        err.clear(); CHECK(RipEdges(h, {{0, 1}}, Vec3{0, 0, 1}, &err).empty() && !err.empty());
        err.clear(); CHECK(RipEdges(h, {{11, 12}, {12, 13}}, Vec3{NAN, 0, 0}, &err).empty() && !err.empty() && h.verts.size() == before);
        err.clear(); CHECK(RipEdges(h, {}, Vec3{1, 0, 0}, &err).empty() && RipEdges(h, {{-4, 2}, {3, 3}}, Vec3{1, 0, 0}, &err).empty());
    }
    // Merge at First / Last / Center.
    {
        PolyMesh g = Grid(2, 1);   // 0 (0,0) 1 (1,0) 2 (2,0) / 3 4 5 at z = 1
        CHECK(MergeVertsAt(g, {0, 1}, MergeAt::Last) == 1);
        CHECK(Near(g.verts[1].x, 1.0f) && UsedVerts(g) == 5 && g.Check().Valid());
        bool zeroGone = true; for (const PolyFace& f : g.faces) for (int v : f.verts) if (v == 0) zeroGone = false;
        CHECK(zeroGone);
        PolyMesh h = Grid(2, 1);
        CHECK(MergeVertsAt(h, {0, 1}, MergeAt::First) == 1 && Near(h.verts[0].x, 0.0f));
        PolyMesh c = Grid(2, 1);
        CHECK(MergeVertsAt(c, {0, 1}, MergeAt::Center) == 1 && Near(c.verts[0].x, 0.5f));
        // Hostile: repeats, bad numbers, one vertex.
        PolyMesh b = Grid(2, 1);
        CHECK(MergeVertsAt(b, {7, 7, -1}, MergeAt::Last) == 0 && MergeVertsAt(b, {1, 1}, MergeAt::Last) == 0 && MergeVertsAt(b, {}, MergeAt::First) == 0);
        CHECK(MergeVertsAt(b, {-1, 2, 99, 1}, MergeAt::Last) == 1 && Near(b.verts[1].x, 1.0f));
    }
    // Loop to Region: the faces inside a closed loop.
    {
        PolyMesh g = Grid(4, 4);
        const std::vector<int> mid = {5, 6, 9, 10};
        CHECK(LoopToRegion(g, RegionOutline(g, mid)) == mid);
        CHECK(LoopToRegion(g, {{0, 1}, {1, 2}}).empty());       // an open line cuts nothing off
        PolyMesh c = Cube();
        int topF = -1; for (int f = 0; f < (int)c.faces.size(); ++f) if (c.FaceNormal(f).y > 0.9f) topF = f;
        CHECK(LoopToRegion(c, RegionOutline(c, {topF})) == std::vector<int>{topF});
        CHECK(LoopToRegion(g, {}).empty() && LoopToRegion(g, {{-1, 5}, {3, 3}, {0, 999}}).empty());
    }
    // SplitEdge carries a sharp mark onto both halves (as it does a seam).
    {
        PolyMesh g = Grid(1, 1);
        g.SetSharp(0, 1, true);
        const int v = SplitEdge(g, 0, 1, 0.5f);
        CHECK(v >= 0 && !g.IsSharp(0, 1) && g.IsSharp(0, v) && g.IsSharp(v, 1));
        PolyMesh dropped = Grid(1, 1);   // CompactVerts renumbers them
        dropped.SetSharp(2, 3, true);
        dropped.verts.insert(dropped.verts.begin(), Vec3{9, 9, 9});
        for (PolyFace& f : dropped.faces) for (int& w : f.verts) ++w;
        dropped.sharp = {{3, 4}};
        CompactVerts(dropped);
        CHECK(dropped.sharp.size() == 1 && dropped.sharp[0] == std::make_pair(2, 3));
    }

    TEST_MAIN_RESULT();
}
