#include "test_framework.hpp"
#include <Okay.hpp>
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

    TEST_MAIN_RESULT();
}
