#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>

using namespace okay;

int main() {
    RUN_SUITE("terrain");

    // --- Heightmap sizing + mesh generation ---------------------------
    {
        Scene s("T"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Ground");
        auto* t = go->AddComponent<Terrain>();
        t->Resize(8);                         // 9x9 vertices
        CHECK((int)t->heights.size() == 9 * 9);

        t->SetHeight(4, 4, 3.0f);
        CHECK_NEAR(t->GetHeight(4, 4), 3.0f, 1e-4f);

        Mesh m = t->BuildMesh();
        CHECK((int)m.vertices.size() == 9 * 9);
        CHECK(m.uvs.size() == m.vertices.size());
        CHECK(m.TriangleCount() == 8 * 8 * 2);   // two tris per cell

        // The peak vertex carries the height we set.
        CHECK_NEAR(m.vertices[4 * 9 + 4].y, 3.0f, 1e-4f);

        // Apply() builds the mesh into a (new) MeshRenderer.
        t->Apply();
        auto* mr = go->GetComponent<MeshRenderer>();
        CHECK(mr != nullptr);
        CHECK((int)mr->mesh.vertices.size() == 9 * 9);
    }

    // --- Sculpt brush raises within its radius and falls off ----------
    {
        Terrain t;
        t.Resize(16); t.size = 16.0f;         // 1 unit per cell, centered
        t.Flatten(0.0f);
        t.RaiseAt(0.0f, 0.0f, 4.0f, 5.0f);    // raise around the center
        // Center vertex (8,8) is at local (0,0): full strength.
        CHECK(t.GetHeight(8, 8) > 4.0f);
        // A vertex outside the radius is untouched.
        CHECK_NEAR(t.GetHeight(0, 0), 0.0f, 1e-4f);
        // A negative delta lowers.
        float before = t.GetHeight(8, 8);
        t.RaiseAt(0.0f, 0.0f, 4.0f, -2.0f);
        CHECK(t.GetHeight(8, 8) < before);
    }

    // --- Generators change the surface ---------------------------------
    {
        Terrain t; t.Resize(16);
        t.Hills(5, 6.0f, 7u);
        float maxH = 0.0f;
        for (float h : t.heights) if (h > maxH) maxH = h;
        CHECK(maxH > 0.0f);                    // hills raised something
        t.Flatten(1.5f);
        CHECK_NEAR(t.GetHeight(3, 3), 1.5f, 1e-4f);
    }

    // --- Procedural generation + smooth normals + auto-color layers ----
    {
        Terrain t; t.Resize(32);
        t.Generate(0, 15.0f, 4.0f, 5, 99u);   // Mountains
        float maxH = 0.0f, minH = 1e9f;
        for (float h : t.heights) { if (h > maxH) maxH = h; if (h < minH) minH = h; }
        CHECK(maxH > 1.0f);                    // produced relief
        CHECK(maxH > minH);

        // Same seed is deterministic; a different type yields a different surface.
        Terrain a; a.Resize(32); a.Generate(0, 15.0f, 4.0f, 5, 99u);
        CHECK_NEAR(a.GetHeight(10, 10), t.GetHeight(10, 10), 1e-4f);
        Terrain b; b.Resize(32); b.Generate(2, 15.0f, 4.0f, 5, 99u);  // Plains
        bool differ = false;
        for (int i = 0; i < (int)b.heights.size(); ++i)
            if (std::fabs(b.heights[i] - t.heights[i]) > 1e-3f) { differ = true; break; }
        CHECK(differ);

        // BuildMesh emits per-face colors + per-vertex smooth normals when autoColor.
        Mesh m = t.BuildMesh();
        CHECK(m.HasFaceColors());
        CHECK(m.HasNormals());
        CHECK((int)m.normals.size() == (int)m.vertices.size());

        // Disabling autoColor drops the face colors (single albedo).
        t.autoColor = false;
        Mesh m2 = t.BuildMesh();
        CHECK(!m2.HasFaceColors());
    }

    // --- Smooth + Flatten brushes -------------------------------------
    {
        Terrain t; t.Resize(16); t.size = 16.0f; t.Flatten(0.0f);
        t.SetHeight(8, 8, 10.0f);              // a single spike
        float spike = t.GetHeight(8, 8);
        t.SmoothAt(0.0f, 0.0f, 4.0f, 1.0f);    // relax around the center
        CHECK(t.GetHeight(8, 8) < spike);      // spike pulled down toward neighbors

        Terrain f; f.Resize(16); f.size = 16.0f; f.Randomize(5.0f, 3u);
        f.FlattenAt(0.0f, 0.0f, 5.0f, 2.0f, 1.0f);  // full-strength flatten to 2.0
        CHECK_NEAR(f.GetHeight(8, 8), 2.0f, 1e-3f); // center reaches the target
    }

    // --- Terrain serializes (heights round-trip; mesh rebuilt on load) -
    {
        Scene s("T2"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Ground");
        auto* t = go->AddComponent<Terrain>();
        t->Resize(6); t->size = 30.0f; t->color = Color::FromBytes(120, 90, 60);
        t->SetHeight(2, 3, 4.5f);
        t->snowLevel = 22.0f; t->rockSlope = 0.72f; t->autoColor = true;
        t->snowColor = Color::FromBytes(250, 250, 255);

        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        GameObject* g2 = s2.Find("Ground");
        CHECK(g2 != nullptr);
        auto* t2 = g2->GetComponent<Terrain>();
        CHECK(t2 != nullptr);
        if (t2) {
            CHECK(t2->resolution == 6);
            CHECK_NEAR(t2->size, 30.0f, 1e-3f);
            CHECK_NEAR(t2->GetHeight(2, 3), 4.5f, 1e-3f);
            CHECK(t2->autoColor);
            CHECK_NEAR(t2->snowLevel, 22.0f, 1e-3f);
            CHECK_NEAR(t2->rockSlope, 0.72f, 1e-3f);
            CHECK_NEAR(t2->snowColor.r, t->snowColor.r, 0.02f);
            // Apply ran on load: the MeshRenderer exists with the right vert count.
            auto* mr2 = g2->GetComponent<MeshRenderer>();
            CHECK(mr2 != nullptr);
            if (mr2) CHECK((int)mr2->mesh.vertices.size() == 7 * 7);
        }
    }

    // --- Material preset: from renderer, round-trip text + file, apply --
    {
        MeshRenderer mr;
        mr.color = Color::FromBytes(200, 100, 50);
        mr.specular = 0.7f; mr.shininess = 40.0f; mr.unlit = true;
        mr.texture = "wall.png"; mr.tiling = {3, 2};

        Material m = Material::FromRenderer(mr);
        Material m2 = Material::FromText(m.ToText());
        CHECK_NEAR(m2.color.r, mr.color.r, 0.01f);
        CHECK_NEAR(m2.specular, 0.7f, 1e-3f);
        CHECK(m2.unlit);
        CHECK(m2.texture == "wall.png");
        CHECK_NEAR(m2.tiling.x, 3.0f, 1e-3f);

        CHECK(m.SaveToFile("test.okaymat"));
        Material m3;
        CHECK(Material::LoadFromFile("test.okaymat", m3));
        CHECK(m3.texture == "wall.png");

        MeshRenderer dst;
        m3.ApplyTo(dst);
        CHECK_NEAR(dst.shininess, 40.0f, 1e-3f);
        CHECK(dst.unlit);
        std::remove("test.okaymat");
    }

    TEST_MAIN_RESULT();
}
