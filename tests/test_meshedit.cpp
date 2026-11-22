#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <vector>

using namespace okay;

int main() {
    RUN_SUITE("meshedit");

    // --- Persistence: an edited mesh (name cleared) survives a round-trip and is
    // NOT regenerated as a fresh primitive --------------------------------------
    {
        Mesh m = Mesh::FromName("Cube");
        m.Subdivide();                       // clears name -> custom geometry
        CHECK(m.name.empty());
        int vc = (int)m.vertices.size();
        int tc = m.TriangleCount();
        // A subdivided cube is no longer the 8-vertex / 12-triangle plain cube.
        CHECK(vc != 8);
        CHECK(tc != 12);

        Scene scene("x");
        GameObject* go = scene.CreateGameObject("Edited");
        auto* mr = go->AddComponent<MeshRenderer>();
        mr->mesh = m;

        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("y"); std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));

        GameObject* lgo = loaded.Find("Edited");
        CHECK(lgo != nullptr);
        auto* lmr = lgo->GetComponent<MeshRenderer>();
        CHECK(lmr != nullptr);
        CHECK(lmr->mesh.name.empty());                       // not regenerated as a Cube
        CHECK((int)lmr->mesh.vertices.size() == vc);
        CHECK(lmr->mesh.TriangleCount() == tc);
        // Vertex positions preserved.
        bool posMatch = true;
        for (int i = 0; i < vc && posMatch; ++i) {
            if ((lmr->mesh.vertices[i] - m.vertices[i]).Magnitude() > 1e-3f) posMatch = false;
        }
        CHECK(posMatch);
    }

    // --- A plain (named) primitive still round-trips compactly (no meshgeo) ------
    {
        Scene scene("x");
        GameObject* go = scene.CreateGameObject("Cubey");
        auto* mr = go->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::FromName("Cube");
        std::string text = SceneSerializer::Serialize(scene);
        CHECK(text.find("meshgeo") == std::string::npos);    // primitive: no geometry record
        Scene loaded("y"); std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* lmr = loaded.Find("Cubey")->GetComponent<MeshRenderer>();
        CHECK(lmr->mesh.name == "Cube");
    }

    // --- ExtrudeFaces: extruding one face (its 2 triangles) grows the mesh ------
    {
        Mesh m = Mesh::Cube();
        int vBefore = (int)m.vertices.size();
        int tBefore = m.TriangleCount();
        // The front (+z) face is triangles 2 and 3 (indices 4,5,6 / 4,6,7).
        std::vector<int> faces = {2, 3};
        Vec3 n = m.FaceNormal(2);
        CHECK(n.Magnitude() > 0.5f);
        // Record a corner of that face before the extrude (vertex 5 = {h,-h,h}).
        Vec3 before = m.vertices[5];
        m.ExtrudeFaces(faces, 1.0f);
        CHECK((int)m.vertices.size() > vBefore);             // duplicated cap verts
        CHECK(m.TriangleCount() > tBefore);                  // side walls added
        CHECK(m.name.empty());
        // The extruded cap should have moved along the face normal: the duplicated
        // vertex for old-5 is the last region vert; check some vertex moved +z.
        bool moved = false;
        for (const Vec3& v : m.vertices)
            if (std::fabs(v.z - (before.z + 1.0f)) < 1e-3f) moved = true;
        CHECK(moved);
        // Selection still points at valid (re-pointed) cap faces.
        CHECK(m.FaceNormal(2).Magnitude() > 0.5f);
    }

    // --- SubdivideFaces: 1 face -> 4, +3 verts, others untouched ----------------
    {
        Mesh m = Mesh::Cube();
        int vBefore = (int)m.vertices.size();
        int tBefore = m.TriangleCount();
        m.SubdivideFaces({0});
        CHECK((int)m.vertices.size() == vBefore + 3);        // 3 new midpoints
        CHECK(m.TriangleCount() == tBefore + 3);             // 1 -> 4 == net +3
        CHECK(m.name.empty());
    }

    // --- MoveVertices: listed vertices translate, others stay -------------------
    {
        Mesh m = Mesh::Cube();
        Vec3 v0 = m.vertices[0], v1 = m.vertices[1];
        m.MoveVertices({0}, Vec3{0, 5, 0});
        CHECK((m.vertices[0] - (v0 + Vec3{0, 5, 0})).Magnitude() < 1e-4f);
        CHECK((m.vertices[1] - v1).Magnitude() < 1e-4f);
        CHECK(m.name.empty());
    }

    // --- FlipNormals: winding reversed, counts unchanged ------------------------
    {
        Mesh m = Mesh::Cube();
        int t0 = m.triangles[0], t1 = m.triangles[1], t2 = m.triangles[2];
        int tc = m.TriangleCount();
        m.FlipNormals();
        CHECK(m.TriangleCount() == tc);
        CHECK(m.triangles[0] == t0);
        CHECK(m.triangles[1] == t2);                         // 2nd and 3rd swapped
        CHECK(m.triangles[2] == t1);
        CHECK(m.name.empty());
    }

    // --- DeleteFaces: removes the listed triangles ------------------------------
    {
        Mesh m = Mesh::Cube();
        int tc = m.TriangleCount();
        m.DeleteFaces({0, 1});
        CHECK(m.TriangleCount() == tc - 2);
        CHECK(m.name.empty());
    }

    // --- SculptBrush GRAB: in-radius vertex moves along dir, out-of-radius stays -
    {
        Mesh m = Mesh::Cube();                               // verts at +-0.5
        Vec3 inV = m.vertices[6];                            // {h,h,h}
        Vec3 farV = m.vertices[0];                           // {-h,-h,-h}
        Vec3 before6 = inV, before0 = farV;
        // Brush centered at vertex 6 with a small radius so only it is affected.
        m.SculptBrush(inV, Vec3{0, 1, 0}, 0.2f, 1.0f, 0);
        CHECK(m.vertices[6].y > before6.y);                  // grabbed up
        CHECK((m.vertices[0] - before0).Magnitude() < 1e-4f);// outside radius: unchanged
        CHECK(m.name.empty());
    }

    // --- InsetFaces: grows geometry without crashing ----------------------------
    {
        Mesh m = Mesh::Cube();
        int tBefore = m.TriangleCount();
        m.InsetFaces({2, 3}, 0.3f);
        CHECK(m.TriangleCount() > tBefore);
        CHECK(m.name.empty());
    }

    // --- FlipNormals keeps the shading: a flat (edit-mode) mesh stays flat --------
    // Bug: FlipNormals always called ComputeSmoothNormals, so pressing Flip Normals in
    // Edit Mesh (where the welded mesh is flat shaded, normals empty) turned a cube
    // into a blobby smooth-shaded one.
    {
        Mesh m = Mesh::Cube();
        m.WeldVertices();
        m.normals.clear();                                   // flat, as in edit mode
        m.FlipNormals();
        CHECK(m.normals.empty());                            // still flat
        Mesh s = Mesh::Sphere();
        s.ComputeSmoothNormals();
        const Vec3 n0 = s.normals[0];
        s.FlipNormals();
        CHECK(s.normals.size() == s.vertices.size());
        CHECK((s.normals[0] + n0).Magnitude() < 1e-4f);      // smooth normals turn round with the faces
    }

    // --- FlipFaces: only the listed triangles turn; UVs follow; hostile lists ------
    {
        Mesh m = Mesh::Cube();
        m.WeldVertices();
        m.normals.clear();
        m.cornerUVs.clear();
        for (std::size_t i = 0; i < m.triangles.size(); ++i) m.cornerUVs.push_back(Vec2{(float)i, (float)(i % 3)});   // a distinct UV per corner
        const std::vector<int> before = m.triangles;
        const std::vector<Vec2> uvBefore = m.cornerUVs;
        CHECK(m.FlipFaces({2, 3, 3, -1, 999}) == 2);         // repeats and bad numbers ignored
        CHECK(m.triangles[6] == before[6] && m.triangles[7] == before[8] && m.triangles[8] == before[7]);
        CHECK(m.triangles[9] == before[9] && m.triangles[10] == before[11]);
        bool rest = true;
        for (std::size_t i = 0; i < before.size(); ++i) if ((i < 6 || i >= 12) && m.triangles[i] != before[i]) rest = false;
        CHECK(rest);
        CHECK(m.cornerUVs.size() == uvBefore.size());
        CHECK(m.cornerUVs.size() > 8 && m.cornerUVs[7].x == uvBefore[8].x && m.cornerUVs[8].x == uvBefore[7].x);
        CHECK(m.normals.empty());                            // flat stays flat
        CHECK(m.FlipFaces({2, 3}) == 2);
        CHECK(m.triangles == before);                        // twice is back to the start
        CHECK(m.FlipFaces({}) == 0);
        Mesh e; CHECK(e.FlipFaces({0}) == 0);                // empty mesh
    }

    // ---- Round 3: Mark Sharp keeps hard edges through Auto Smooth ----
    {
        // A flat 2x2 grid of quads (as triangles): Auto Smooth shares every vertex (9).
        auto grid = [] {
            Mesh m;
            for (int z = 0; z <= 2; ++z) for (int x = 0; x <= 2; ++x) m.vertices.push_back({(float)x, 0, (float)z});
            auto id = [](int x, int z) { return z * 3 + x; };
            for (int z = 0; z < 2; ++z) for (int x = 0; x < 2; ++x)
                m.triangles.insert(m.triangles.end(), {id(x, z), id(x, z + 1), id(x + 1, z + 1), id(x, z), id(x + 1, z + 1), id(x + 1, z)});
            return m;
        };
        Mesh plain = grid();
        plain.ComputeAutoSmoothNormals(30.0f);
        CHECK(plain.vertices.size() == 9);
        // The middle column of edges (1-4, 4-7) marked sharp: the three vertices on it split in two (12).
        Mesh sharp = grid();
        sharp.SetSharp(1, 4, true); sharp.SetSharp(7, 4, true);
        CHECK(sharp.IsSharp(4, 1) && sharp.sharpEdges.size() == 2);
        sharp.SetSharp(4, 7, true);                          // marking again changes nothing
        CHECK(sharp.sharpEdges.size() == 2);
        sharp.ComputeAutoSmoothNormals(30.0f);
        CHECK(sharp.vertices.size() == 12);
        // After the rebuild the marks point at vertices at the same places as before.
        bool placed = sharp.sharpEdges.size() == 2;
        for (const auto& e : sharp.sharpEdges)
            if (std::fabs(sharp.vertices[e.first].x - 1.0f) > 1e-5f || std::fabs(sharp.vertices[e.second].x - 1.0f) > 1e-5f) placed = false;
        CHECK(placed);
        // Running Auto Smooth again (as loading a scene does) keeps the split.
        sharp.ComputeAutoSmoothNormals(30.0f);
        CHECK(sharp.vertices.size() == 12);
        // One sharp edge ending inside: only its border end splits (10).
        Mesh half = grid();
        half.SetSharp(1, 4, true);
        half.ComputeAutoSmoothNormals(30.0f);
        CHECK(half.vertices.size() == 10);
        // Hostile marks (not edges, out of range, a vertex with itself) change nothing.
        Mesh junk = grid();
        junk.SetSharp(0, 8, true); junk.SetSharp(-1, 2, true); junk.SetSharp(3, 3, true);
        junk.sharpEdges.push_back({2, 500});
        junk.ComputeAutoSmoothNormals(30.0f);
        CHECK(junk.vertices.size() == 9);
        // Clearing a mark.
        Mesh clr = grid(); clr.SetSharp(1, 4, true); clr.SetSharp(4, 1, false);
        CHECK(clr.sharpEdges.empty());

        // Welding follows the marks to the kept vertices.
        Mesh w = grid();
        w.vertices.push_back(w.vertices[4]);                 // vertex 9: a copy of 4
        for (int k = 0; k < 6; ++k) if (w.triangles[k] == 4) w.triangles[k] = 9;
        w.SetSharp(9, 1, true);
        w.WeldVertices();
        CHECK(w.vertices.size() == 9 && w.sharpEdges.size() == 1 && w.IsSharp(1, 4));

        // Saved with the scene and read back (before the shading is rebuilt on load, so the split survives).
        Scene scene("s");
        auto* mr = scene.CreateGameObject("G")->AddComponent<MeshRenderer>();
        mr->mesh = grid(); mr->mesh.name = "";
        mr->mesh.SetSharp(1, 4, true); mr->mesh.SetSharp(4, 7, true);
        mr->mesh.ComputeAutoSmoothNormals(30.0f);
        const std::string text = SceneSerializer::Serialize(scene);
        CHECK(text.find("meshsharp 2") != std::string::npos);
        Scene back("b"); std::string err;
        CHECK(SceneSerializer::Deserialize(back, text, &err));
        const Mesh& lm = back.Find("G")->GetComponent<MeshRenderer>()->mesh;
        CHECK(lm.sharpEdges.size() == 2 && lm.vertices.size() == 12 && lm.HasNormals());
    }
    // Auto Smooth used to leave UV seams, creases and bevel weights pointing at the old vertex numbers.
    {
        Mesh c = Mesh::Cube(1.0f);
        c.WeldVertices();
        const Vec3 pa = c.vertices[0], pb = c.vertices[1];
        c.seams = {{0, 1}};
        c.SetCrease(0, 1, 2.0f);
        c.ComputeAutoSmoothNormals(30.0f);
        CHECK(c.vertices.size() == 24);
        CHECK(c.seams.size() == 1 && c.creases.size() == 1);
        auto at = [&](int v, const Vec3& p) { return v >= 0 && v < (int)c.vertices.size() && (c.vertices[v] - p).SqrMagnitude() < 1e-10f; };
        CHECK((at(c.seams[0].first, pa) && at(c.seams[0].second, pb)) || (at(c.seams[0].first, pb) && at(c.seams[0].second, pa)));
        CHECK((at(c.creases[0].a, pa) && at(c.creases[0].b, pb)) || (at(c.creases[0].a, pb) && at(c.creases[0].b, pa)));
    }

    TEST_MAIN_RESULT();
}
