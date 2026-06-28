#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

// Voxel (marching-cubes) terrain: a 3D density field you can dig caves/tunnels
// into, meshed smoothly and round-tripping through the scene serializer.
int main() {
    RUN_SUITE("voxel");

    // --- FillSlab: solid below the fill line, air above; surface in between -----
    {
        VoxelTerrain v;
        v.Resize(16, 16, 16); v.voxelSize = 1.0f;
        v.FillSlab(0.5f);                       // bottom half solid
        float topY = v.SizeY() * 0.5f;
        CHECK(v.SolidAt(Vec3{0, topY - 3.0f, 0}));   // well below = solid
        CHECK(!v.SolidAt(Vec3{0, topY + 3.0f, 0}));  // well above = air
        float sy = 0.0f;
        CHECK(v.SurfaceY(0, 0, sy));
        CHECK_NEAR(sy, topY, v.voxelSize * 1.5f);    // surface near the fill line

        Mesh m = v.BuildMesh();
        CHECK(m.vertices.size() > 0);                // the slab top meshed
        CHECK(m.TriangleCount() > 0);
        CHECK(m.HasNormals());
        // Top surface is roughly flat: most vertices sit near topY.
        int nearTop = 0;
        for (const auto& vert : m.vertices)
            if (std::fabs(vert.y - topY) < v.voxelSize * 1.5f) ++nearTop;
        CHECK(nearTop > (int)m.vertices.size() / 2);
    }

    // --- Digging opens a real hole (caves/overhangs) --------------------------
    {
        VoxelTerrain v;
        v.Resize(24, 24, 24); v.voxelSize = 1.0f;
        v.FillSlab(0.8f);                       // mostly solid block
        Vec3 spot{0, v.SizeY() * 0.4f, 0};      // a point deep inside the solid
        CHECK(v.SolidAt(spot));
        v.Dig(spot, 4.0f, 6.0f);                // carve a sphere of air there
        CHECK(!v.SolidAt(spot));                // now hollow — a cave, mid-rock
        // The rock above the cave is still solid (an overhang / ceiling exists).
        CHECK(v.SolidAt(Vec3{0, v.SizeY() * 0.7f, 0}));

        // Adding deposits material back.
        v.Add(spot, 4.0f, 12.0f);
        CHECK(v.SolidAt(spot));
    }

    // --- Generate yields a meshable landscape with relief ---------------------
    {
        VoxelTerrain v;
        v.Resize(32, 24, 32); v.voxelSize = 1.0f;
        v.Generate(0.5f, 6.0f, 0.6f, 7u);       // hills + caves
        Mesh m = v.BuildMesh();
        CHECK(m.vertices.size() > 100);
        CHECK(m.HasFaceColors());               // autoColor on by default
        // Deterministic: same seed -> same field.
        VoxelTerrain v2; v2.Resize(32, 24, 32); v2.voxelSize = 1.0f;
        v2.Generate(0.5f, 6.0f, 0.6f, 7u);
        CHECK_NEAR(v2.SampleDensity(Vec3{0, 6, 0}), v.SampleDensity(Vec3{0, 6, 0}), 1e-4f);
    }

    // --- Density encode/decode round-trips (within quantization) --------------
    {
        VoxelTerrain v;
        v.Resize(12, 12, 12); v.voxelSize = 1.0f;
        v.Generate(0.5f, 4.0f, 0.5f, 3u);
        std::string blob = v.EncodeDensity();
        CHECK(!blob.empty());

        VoxelTerrain v2; v2.Resize(12, 12, 12); v2.voxelSize = 1.0f;
        CHECK(v2.DecodeDensity(blob));
        // Surface-relevant densities (|d| small) reproduce closely.
        float tol = v.voxelSize * 6.0f / 127.0f * 2.0f;
        int checks = 0;
        for (int i = 0; i < v.Count(); i += 137) {
            float a = v.density[i], b = v2.density[i];
            if (std::fabs(a) < v.voxelSize * 5.0f) { CHECK_NEAR(b, a, tol); ++checks; }
        }
        CHECK(checks > 0);
        // A mismatched-size buffer is rejected.
        VoxelTerrain v3; v3.Resize(8, 8, 8);
        CHECK(!v3.DecodeDensity(blob));
    }

    // --- Full serialize round-trip (terrain field + digger) -------------------
    {
        Scene s("VOX"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Voxel");
        auto* v = go->AddComponent<VoxelTerrain>();
        v->Resize(16, 16, 16); v->voxelSize = 1.5f; v->iso = 0.0f;
        v->Generate(0.5f, 5.0f, 0.4f, 11u);
        v->snowLevel = 13.0f; v->rockSlope = 0.7f;
        auto* dig = go->AddComponent<VoxelDigger>();
        dig->mode = VoxelDigger::Mode::Add; dig->radius = 3.5f; dig->button = 1;
        dig->key = 'g'; dig->showBrush = false;

        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        GameObject* g2 = s2.Find("Voxel");
        CHECK(g2 != nullptr);
        auto* v2 = g2 ? g2->GetComponent<VoxelTerrain>() : nullptr;
        CHECK(v2 != nullptr);
        if (v2) {
            CHECK(v2->nx == 16 && v2->ny == 16 && v2->nz == 16);
            CHECK_NEAR(v2->voxelSize, 1.5f, 1e-3f);
            CHECK_NEAR(v2->snowLevel, 13.0f, 1e-3f);
            // The density field survived (sample a mid point matches within tol).
            float tol = v->voxelSize * 6.0f / 127.0f * 3.0f;
            float a = v->SampleDensity(Vec3{0, 6, 0});
            if (std::fabs(a) < v->voxelSize * 5.0f)
                CHECK_NEAR(v2->SampleDensity(Vec3{0, 6, 0}), a, tol);
            // The mesh was rebuilt on load.
            auto* mr = g2->GetComponent<MeshRenderer>();
            CHECK(mr && mr->mesh.vertices.size() > 0);
        }
        auto* d2 = g2 ? g2->GetComponent<VoxelDigger>() : nullptr;
        CHECK(d2 != nullptr);
        if (d2) {
            CHECK(d2->mode == VoxelDigger::Mode::Add);
            CHECK_NEAR(d2->radius, 3.5f, 1e-3f);
            CHECK(d2->button == 1);
            CHECK(d2->key == 'g');
            CHECK(d2->showBrush == false);
        }
    }

    // --- Voxel collision: a body rests ON the terrain, doesn't fall through -----
    {
        Scene s("VCOL");
        GameObject* go = s.CreateGameObject("Voxel");
        auto* v = go->AddComponent<VoxelTerrain>();
        v->Resize(24, 24, 24); v->voxelSize = 1.0f;
        v->FillSlab(0.5f);                       // solid bottom half
        v->Apply();
        float topY = v->SizeY() * 0.5f;

        GameObject* body = s.CreateGameObject("Body");
        body->transform->localPosition = {0, v->SizeY() + 4.0f, 0};   // start high above
        auto* rb = body->AddComponent<Rigidbody3D>();
        rb->bodyType = Rigidbody3D::BodyType::Dynamic;
        auto* bc = body->AddComponent<BoxCollider3D>();
        bc->size = {1.0f, 1.8f, 1.0f}; bc->offset = {0, 0.9f, 0};

        for (int i = 0; i < 300; ++i) s.Update(1.0f / 60.0f);   // fall + settle
        float restY = body->transform->Position().y;
        CHECK(restY > topY - 1.0f);              // landed ON the surface, not through it
        CHECK(restY < topY + 3.0f);              // and didn't hover way above
        CHECK(rb->groundedOnTerrain);            // flagged grounded -> can jump

        // Dig a pit straight down under it; it should drop into the hole.
        v->Dig(Vec3{0, topY, 0}, 4.0f, 30.0f);   // carve a deep crater
        v->Dig(Vec3{0, topY - 3.0f, 0}, 4.0f, 30.0f);
        v->Apply();
        for (int i = 0; i < 300; ++i) s.Update(1.0f / 60.0f);
        CHECK(body->transform->Position().y < restY - 1.0f);   // fell into the dug pit
    }

    TEST_MAIN_RESULT();
}
