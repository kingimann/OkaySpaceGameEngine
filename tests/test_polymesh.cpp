// Modeling milestone 2: the polygon mesh (quads / n-gons), its conversion to
// and from the triangle Mesh, topology checks and .okaymesh files.
#include "test_framework.hpp"
#include <filesystem>
#include <Okay.hpp>
#include "okay/Modeling/PolyMesh.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>

using namespace okay;
using namespace okay::modeling;

static float Area(const Mesh& m) {
    float a = 0;
    for (int t = 0; t < m.TriangleCount(); ++t) {
        const Vec3& p = m.vertices[m.triangles[t * 3]];
        const Vec3& q = m.vertices[m.triangles[t * 3 + 1]];
        const Vec3& r = m.vertices[m.triangles[t * 3 + 2]];
        a += Vec3::Cross(q - p, r - p).Magnitude() * 0.5f;
    }
    return a;
}
// Signed volume: positive when the faces wind outward.
static float Volume(const Mesh& m) {
    float v = 0;
    for (int t = 0; t < m.TriangleCount(); ++t) {
        const Vec3& p = m.vertices[m.triangles[t * 3]];
        const Vec3& q = m.vertices[m.triangles[t * 3 + 1]];
        const Vec3& r = m.vertices[m.triangles[t * 3 + 2]];
        v += Vec3::Dot(p, Vec3::Cross(q, r)) / 6.0f;
    }
    return v;
}

int main() {
    RUN_SUITE("polymesh");

    // ---- A triangulated cube comes back as six quads ----
    {
        Mesh cube = Mesh::Cube(1.0f);
        PolyMesh p = PolyMesh::FromMesh(cube);
        CHECK(p.verts.size() == 8);
        CHECK(p.faces.size() == 6 && p.CountFaces(4) == 6);
        CHECK(p.Edges().size() == 12);
        CHECK(p.EulerCharacteristic() == 2);
        CHECK(p.Check().Closed() && p.Check().Valid());
        Mesh back = p.ToMesh();
        CHECK(back.TriangleCount() == 12 && back.vertices.size() == 8);
        CHECK(back.Topology().Closed());
        CHECK(std::fabs(Area(back) - 6.0f) < 1e-4f);
        CHECK(std::fabs(Volume(back) - Volume(cube)) < 1e-4f);   // same winding as the original
        CHECK(Volume(back) > 0.0f);
        // Each face is a real square: normal is a unit axis.
        for (int f = 0; f < 6; ++f) {
            Vec3 n = p.FaceNormal(f);
            float big = std::max(std::fabs(n.x), std::max(std::fabs(n.y), std::fabs(n.z)));
            CHECK(std::fabs(big - 1.0f) < 1e-4f);
        }
    }
    // ---- Grids and tori: every quad recovered; a torus has Euler characteristic 0 ----
    {
        PolyMesh g = PolyMesh::FromMesh(Mesh::Grid(4, 4, 4, 4));
        CHECK(g.CountFaces(4) == 16 && g.faces.size() == 16 && g.verts.size() == 25);
        CHECK(g.Check().boundaryEdges == 16);
        PolyMesh t = PolyMesh::FromMesh(Mesh::FromName("Torus"));
        CHECK(t.CountFaces(3) == 0 && t.CountFaces(4) == (int)t.faces.size());
        CHECK(t.EulerCharacteristic() == 0 && t.Check().Closed());
    }
    // ---- A sphere: quads round the middle, triangles at the poles, degenerate pole triangles dropped ----
    {
        Mesh s = Mesh::Sphere(0.5f, 8, 12);
        PolyMesh p = PolyMesh::FromMesh(s);
        CHECK(p.CountFaces(4) == 72 && p.CountFaces(3) == 24);
        CHECK(p.EulerCharacteristic() == 2 && p.Check().Closed());
        CHECK(p.ToMesh().Topology().Closed());
    }
    // ---- A fan-shaped cap stays triangles (equal spokes aren't a quad's diagonal) ----
    {
        PolyMesh c = PolyMesh::FromMesh(Mesh::FromName("Cylinder"));
        CHECK(c.Check().Closed() && c.EulerCharacteristic() == 2);
        for (const PolyFace& f : c.faces) {
            if (f.Size() != 4) continue;
            // Every quad is a side wall: its normal is horizontal.
            const int i = (int)(&f - &c.faces[0]);
            CHECK(std::fabs(c.FaceNormal(i).y) < 1e-3f);
        }
    }
    // ---- Two triangles folded at an angle don't merge; with mergeQuads off nothing merges ----
    {
        Mesh m;
        m.vertices = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0.5f, 1}};
        m.triangles = {0, 2, 1, 0, 3, 2};
        CHECK(PolyMesh::FromMesh(m).faces.size() == 2);
        PolyFromMeshOptions o; o.mergeQuads = false;
        CHECK(PolyMesh::FromMesh(Mesh::Cube(1.0f), o).faces.size() == 12);
    }
    // ---- Different colours on the two halves keep them separate ----
    {
        Mesh m = Mesh::Plane(2.0f);
        m.triColors = {Color{1, 0, 0, 1}, Color{0, 0, 1, 1}};
        PolyMesh p = PolyMesh::FromMesh(m);
        CHECK(p.faces.size() == 2 && p.faces[0].hasColor);
        Mesh back = p.ToMesh();
        CHECK(back.HasFaceColors());
    }
    // ---- A concave L-shaped n-gon triangulates without spilling outside ----
    {
        PolyMesh p;
        p.verts = {{0, 0, 0}, {2, 0, 0}, {2, 0, 1}, {1, 0, 1}, {1, 0, 2}, {0, 0, 2}};
        PolyFace f; f.verts = {0, 5, 4, 3, 2, 1};   // counter-clockwise seen from +Y
        p.faces.push_back(f);
        CHECK(p.CountNgons() == 1);
        Mesh m = p.ToMesh();
        CHECK(m.TriangleCount() == 4);
        CHECK(std::fabs(Area(m) - 3.0f) < 1e-4f);   // the L's area, not its 4.0 bounding box
        CHECK(p.FaceNormal(0).y > 0.99f);
        for (int t = 0; t < m.TriangleCount(); ++t) {   // every triangle faces up like the polygon
            const Vec3& a = m.vertices[m.triangles[t * 3]];
            const Vec3& b = m.vertices[m.triangles[t * 3 + 1]];
            const Vec3& c = m.vertices[m.triangles[t * 3 + 2]];
            CHECK(Vec3::Cross(b - a, c - a).y > 0.0f);
        }
    }
    // ---- Per-corner UVs: a seam splits vertices on the way back out, nothing else does ----
    {
        PolyMesh p;
        p.verts = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}, {2, 0, 0}, {2, 0, 1}};
        PolyFace a; a.verts = {0, 3, 2, 1}; a.uvs = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
        PolyFace b; b.verts = {1, 2, 5, 4}; b.uvs = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};   // restarts at 0: a seam on edge 1-2
        p.faces = {a, b};
        Mesh m = p.ToMesh();
        CHECK(m.vertices.size() == 8);   // 6 corners + 2 split by the seam
        CHECK(m.uvs.size() == m.vertices.size());
        PolyMesh back = PolyMesh::FromMesh(m);
        CHECK(back.verts.size() == 6 && back.faces.size() == 2);   // welded again, seam kept in the corners
        CHECK(back.CountFaces(4) == 2);
    }
    // ---- Topology checks catch holes, non-manifold edges, degenerate faces and bad indices ----
    {
        PolyMesh p = PolyMesh::FromMesh(Mesh::Cube(1.0f));
        p.faces.pop_back();
        CHECK(p.Check().boundaryEdges == 4 && !p.Check().Closed());
        PolyMesh q = PolyMesh::FromMesh(Mesh::Cube(1.0f));
        q.faces.push_back(q.faces[0]);   // a doubled face: its edges are used three times
        CHECK(q.Check().nonManifoldEdges == 4);
        PolyMesh d = PolyMesh::FromMesh(Mesh::Cube(1.0f));
        d.faces[0].verts = {0, 0, 1};
        CHECK(d.Check().degenerateFaces == 1 && !d.Check().Valid());
        PolyMesh b = PolyMesh::FromMesh(Mesh::Cube(1.0f));
        b.faces[0].verts[0] = 99;
        CHECK(b.Check().badIndices == 1);
    }
    // ---- .okaymesh: write, read back identical, and reject damaged files with a reason ----
    {
        Mesh src = Mesh::Cube(1.0f);
        src.ProjectUVBox(1.0f);
        PolyMesh p = PolyMesh::FromMesh(src);
        p.faces[2].material = 3;
        p.faces[1].hasColor = true; p.faces[1].color = Color{0.25f, 0.5f, 0.75f, 1.0f};
        const std::string text = p.Serialize();
        PolyMesh r;
        std::string err;
        CHECK(PolyMesh::Parse(text, r, &err));
        CHECK(r.Serialize() == text);
        CHECK(r.faces[2].material == 3 && r.faces[1].hasColor && std::fabs(r.faces[1].color.b - 0.75f) < 1e-6f);
        CHECK(r.faces[0].uvs.size() == 4);

        const std::string path = (std::filesystem::temp_directory_path() / "okay_test_polymesh.okaymesh").string();
        CHECK(p.Save(path, &err));
        PolyMesh l;
        CHECK(PolyMesh::Load(path, l, &err));
        CHECK(l.Serialize() == text);
        std::remove(path.c_str());
        CHECK(!PolyMesh::Load(path, l, &err) && err.find("can't open") != std::string::npos);

        CHECK(!PolyMesh::Parse("hello\n", r, &err) && err.find("not a .okaymesh") != std::string::npos);
        CHECK(!PolyMesh::Parse("okaymesh 2\n", r, &err) && err.find("newer") != std::string::npos);
        CHECK(!PolyMesh::Parse("okaymesh 1\nverts 1\nv 0 0 0\nfaces 1\nf 3 0 1 2\n", r, &err) && err.find("doesn't exist") != std::string::npos);
        CHECK(!PolyMesh::Parse("okaymesh 1\nverts 2\nv 0 0 0\n", r, &err) && err.find("expected 2 vertices") != std::string::npos);
        CHECK(!PolyMesh::Parse("okaymesh 1\nverts 3\nv 0 0 0\nv 1 0 0\nv 0 0 1\nfaces 1\nf 3 0 1 2 glitter\n", r, &err) && err.find("unknown face field") != std::string::npos);
        CHECK(PolyMesh::Parse("okaymesh 1\n# a comment\nverts 3\nv 0 0 0\nv 1 0 0\nv 0 0 1\nfaces 1\nf 3 0 2 1\n", r, &err));
        CHECK(r.faces.size() == 1 && r.ToMesh().TriangleCount() == 1);
    }
    CHECK(IsOkayMeshPath("a/b/Rock.okaymesh") && IsOkayMeshPath("X.OKAYMESH") && !IsOkayMeshPath("rock.obj") && !IsOkayMeshPath("okaymesh"));

    TEST_MAIN_RESULT();
}
