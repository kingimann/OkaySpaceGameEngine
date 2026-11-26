#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <map>
#include <tuple>

using namespace okay;

// A mesh is watertight when, after joining vertices at the same place, every edge is used by exactly two
// triangles, once in each direction (a closed surface wound one way). Returns the number of bad edges and
// counts the degenerate triangles (two corners at the same place).
static int OpenEdges(const Mesh& m, int& degenerate, int& misWound) {
    std::map<std::tuple<long, long, long>, int> ids;
    auto id = [&](const Vec3& p) {
        auto k = std::make_tuple(std::lround(p.x * 4096.0f), std::lround(p.y * 4096.0f), std::lround(p.z * 4096.0f));
        auto it = ids.find(k);
        if (it != ids.end()) return it->second;
        const int n = (int)ids.size(); ids.emplace(k, n); return n;
    };
    std::map<std::pair<int, int>, int> directed;
    degenerate = 0; misWound = 0;
    for (std::size_t t = 0; t + 2 < m.triangles.size(); t += 3) {
        const int a = id(m.vertices[m.triangles[t]]), b = id(m.vertices[m.triangles[t + 1]]), c = id(m.vertices[m.triangles[t + 2]]);
        if (a == b || b == c || c == a) { ++degenerate; continue; }
        ++directed[{a, b}]; ++directed[{b, c}]; ++directed[{c, a}];
    }
    int bad = 0;
    for (const auto& kv : directed) {
        auto rev = directed.find({kv.first.second, kv.first.first});
        const int back = rev == directed.end() ? 0 : rev->second;
        if (kv.second + back != 2) ++bad;            // used once (a hole) or three or more times
        else if (kv.second != 1) ++misWound;         // used twice the same way: neighbours wound against each other
    }
    return bad;
}

// Voxel (marching-cubes) terrain: a 3D density field you can dig caves/tunnels
// into, meshed smoothly and round-tripping through the scene serializer.
int main() {
    RUN_SUITE("voxel");

    // --- Every one of the 256 corner patterns meshes into a closed surface ------------------------------------
    // One cube's corners are solid or air in every combination, inside a field of air, so the solid bit is a
    // closed blob and its mesh must be closed too: no hole on any cube face (provenance audit A1).
    {
        int openCases = 0, emptyWrong = 0, degenerate = 0, misWound = 0;
        for (int pattern = 0; pattern < 256; ++pattern) {
            VoxelTerrain v;
            v.Resize(4, 4, 4); v.voxelSize = 1.0f; v.iso = 0.0f;
            for (int c = 0; c < 8; ++c)
                if (pattern & (1 << c))      // uneven values, so the crossings are not all at the middle of an edge
                    v.Set(1 + (c & 1), 1 + ((c >> 1) & 1), 1 + ((c >> 2) & 1), 0.3f + 0.1f * c);
            Mesh m = v.BuildMesh();
            if (pattern == 0) { if (m.TriangleCount() != 0) ++emptyWrong; continue; }
            if (m.TriangleCount() == 0) { ++emptyWrong; continue; }
            int d = 0, w = 0;
            if (OpenEdges(m, d, w) != 0) ++openCases;
            degenerate += d; misWound += w;
        }
        CHECK(emptyWrong == 0);
        CHECK(openCases == 0);
        CHECK(degenerate == 0);
        CHECK(misWound == 0);
        if (openCases || misWound) std::cout << "  open cases " << openCases << ", mis-wound edges " << misWound << "\n";
    }

    // --- A random field (air at the border) is closed everywhere: ambiguous faces agree between neighbours -----
    {
        int open = 0, degenerate = 0, misWound = 0, tris = 0;
        for (unsigned seed = 1; seed <= 6; ++seed) {
            VoxelTerrain v;
            v.Resize(10, 10, 10); v.voxelSize = 1.0f; v.iso = 0.0f;
            unsigned s = seed * 2654435761u;
            for (int z = 1; z < 9; ++z)
                for (int y = 1; y < 9; ++y)
                    for (int x = 1; x < 9; ++x) {
                        s = s * 1664525u + 1013904223u;
                        v.Set(x, y, z, ((s >> 8) & 0xFFFF) / 32767.5f - 1.0f);
                    }
            Mesh m = v.BuildMesh();
            tris += (int)m.TriangleCount();
            int d = 0, w = 0;
            open += OpenEdges(m, d, w);
            degenerate += d; misWound += w;
            Mesh r = v.BuildMeshReference();        // the reference loop uses the same cases
            CHECK(r.TriangleCount() == m.TriangleCount());
        }
        CHECK(tris > 1000);
        CHECK(open == 0);
        CHECK(degenerate == 0);
        CHECK(misWound == 0);
        if (open || misWound) std::cout << "  random fields: open edges " << open << ", mis-wound edges " << misWound << "\n";
    }

    // --- The generated landscape still looks the same: about as many triangles as before the case rewrite -------
    {
        VoxelTerrain v;
        v.Resize(32, 24, 32); v.voxelSize = 1.0f;
        v.Generate(0.5f, 6.0f, 0.6f, 7u);
        Mesh m = v.BuildMesh();
        std::cout << "  generated terrain: " << m.TriangleCount() << " triangles\n";
    }

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

    // --- SmoothAt relaxes the field (rounds off a sharp dug edge) -------------
    {
        VoxelTerrain v;
        v.Resize(20, 20, 20); v.voxelSize = 1.0f;
        v.FillSlab(0.6f);
        // Punch a jagged single-voxel spike of air into the solid.
        int cx = 10, cy = 6, cz = 10;
        v.Set(cx, cy, cz, -3.0f);
        float before = v.Get(cx, cy, cz);
        v.SmoothAt(Vec3{cx - v.HalfX(), (float)cy, cz - v.HalfZ()}, 3.0f, 1.0f);
        // The carved cell relaxes back toward its (solid) neighbours.
        CHECK(v.Get(cx, cy, cz) > before);
    }

    // --- Voxel triplanar texture round-trips + drives the renderer ------------
    {
        Scene s("VTEX"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Voxel");
        auto* v = go->AddComponent<VoxelTerrain>();
        v->Resize(12, 12, 12); v->voxelSize = 1.0f; v->FillSlab(0.5f);
        v->texture = "rock.png"; v->textureTiling = 0.05f;
        v->Apply();
        auto* mr = go->GetComponent<MeshRenderer>();
        CHECK(mr && mr->texture == "rock.png" && mr->triplanar);

        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* v2 = s2.Find("Voxel") ? s2.Find("Voxel")->GetComponent<VoxelTerrain>() : nullptr;
        CHECK(v2 != nullptr);
        if (v2) {
            CHECK(v2->texture == "rock.png");
            CHECK_NEAR(v2->textureTiling, 0.05f, 1e-4f);
        }
    }

    // --- Anti-tunnel: a fast fall (big dt / low FPS) does NOT pass through ------
    {
        Scene s("TUN");
        GameObject* go = s.CreateGameObject("Voxel");
        auto* v = go->AddComponent<VoxelTerrain>();
        v->Resize(24, 24, 24); v->voxelSize = 1.0f;
        v->FillSlab(0.5f);
        v->Apply();
        float topY = v->SizeY() * 0.5f;

        GameObject* body = s.CreateGameObject("Body");
        body->transform->localPosition = {0, topY + 6.0f, 0};
        auto* rb = body->AddComponent<Rigidbody3D>();
        rb->bodyType = Rigidbody3D::BodyType::Dynamic;
        auto* bc = body->AddComponent<BoxCollider3D>();
        bc->size = {1.0f, 1.8f, 1.0f}; bc->offset = {0, 0.9f, 0};

        // Establish a previous position, then slam it down at huge speed with a big
        // timestep (would skip ~9 units in one step — far past the surface).
        s.Update(1.0f / 60.0f);
        rb->velocity = {0, -90.0f, 0};
        s.Update(0.1f);                 // 10 FPS step
        for (int i = 0; i < 120; ++i) s.Update(1.0f / 60.0f);
        float y = body->transform->Position().y;
        CHECK(y > topY - 1.0f);         // caught on the surface, did not tunnel through
    }

    TEST_MAIN_RESULT();
}
