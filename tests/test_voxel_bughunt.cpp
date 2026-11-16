#include "test_framework.hpp"
#include "voxel_mesh_util.hpp"
#include <cmath>
#include <iostream>
#include <limits>

using namespace okay;
using namespace voxeltest;

// A hunt for bugs in the voxel terrain and everything the real-life digging added (docs/terrain_audit.md, "Bug hunt").
// Each block is a way to break it that a player, a scene file or a script can reach.
namespace {
const float kNaN = std::numeric_limits<float>::quiet_NaN(), kInf = std::numeric_limits<float>::infinity();

VoxelTerrain Field(int X = 34, int Y = 26, int Z = 34, bool mats = true) {
    VoxelTerrain v; v.Resize(X, Y, Z); v.voxelSize = 1.0f; v.Generate(0.5f, 4.0f, 0.2f, 9u);
    if (mats) v.GenerateMaterials(9u);
    return v;
}
}

int main() {
    RUN_SUITE("voxel_bughunt");
    VoxelTerrain::ForceOldRemesh(0);

    // --- tiny and empty grids: nothing may crash or write outside the field ---------------------------------------------------------
    {
        int done = 0;
        const int sizes[][3] = {{0, 0, 0}, {1, 1, 1}, {2, 2, 2}, {3, 1, 5}, {4, 4, 4}};
        for (auto& sz : sizes) {
            Scene s("T"); s.physicsEnabled = false;
            GameObject* g = s.CreateGameObject("V");
            auto* v = g->AddComponent<VoxelTerrain>();
            v->Resize(sz[0], sz[1], sz[2]); v->voxelSize = 1.0f;
            v->FillSlab(0.5f); v->EnableMaterials(0);
            v->Apply();
            VoxelTerrain::EditOptions o;
            v->Edit({0, 0, 0}, 2.0f, 3.0f, o); v->Edit({0, 0, 0}, 2.0f, -3.0f, o);
            v->Dig({0, 0, 0}, 2.0f, 3.0f); v->Add({0, 0, 0}, 2.0f, 3.0f); v->SmoothAt({0, 0, 0}, 2.0f, 1.0f);
            v->DigBox({-1, -1, -1}, {1, 1, 1}, 2.0f);
            v->Slump(10); v->ExtractFloaters(4); v->BeginStroke(); v->EndStroke(); v->UndoStroke();
            float y = 0; v->SurfaceY(0, 0, y); (void)v->SampleDensity({0, 0, 0}); (void)v->SolidVolume();
            const std::string text = SceneSerializer::Serialize(s);
            Scene s2("T2"); s2.physicsEnabled = false;
            CHECK(SceneSerializer::Deserialize(s2, text));
            CHECK(v->density.size() == (std::size_t)v->nx * v->ny * v->nz);
            std::cout << "  grid " << sz[0] << "x" << sz[1] << "x" << sz[2] << " became " << v->nx << "x" << v->ny << "x" << v->nz << "\n";
            s.Update(1.0f / 60.0f);
            ++done;
        }
        CHECK(done == 5);   // every size got through every call without a crash
    }

    // --- NaN, infinite, huge and tiny brushes ---------------------------------------------------------------------------------------
    {
        VoxelTerrain v = Field();
        const std::vector<float> d0 = v.density;
        VoxelTerrain::EditOptions o;
        const float bad[] = {kNaN, kInf, -kInf};
        for (float b : bad) {
            v.Edit({b, 10, 0}, 3.0f, 3.0f, o); v.Edit({0, 10, 0}, b, 3.0f, o); v.Edit({0, 10, 0}, 3.0f, b, o);
            v.Dig({b, 10, 0}, 3.0f, 3.0f); v.Dig({0, 10, 0}, b, 3.0f); v.Add({0, 10, 0}, 3.0f, b); v.SmoothAt({0, b, 0}, 3.0f, 1.0f);
            v.DigBox({b, 0, 0}, {1, 1, 1}, 2.0f); v.DigBox({0, 0, 0}, {1, 1, 1}, b);
            o.power = b; o.hardnessScale = b; o.protectBelowY = b; o.maxDepth = b; v.Edit({0, 10, 0}, 3.0f, 3.0f, o); o = {};
            v.AddVolume({b, 10, 0}, 5.0, 0, 2.0f); v.AddVolume({0, 10, 0}, (double)b, 0, 2.0f); v.AddVolume({0, 10, 0}, 5.0, 0, b);
        }
        CHECK(AllFinite(v));
        // tiny and enormous radii
        v.Edit({0, 10, 0}, 1e-9f, 3.0f, o); v.Dig({0, 10, 0}, 1e-30f, 3.0f);
        CHECK(AllFinite(v));
        v.Edit({0, 10, 0}, 1e30f, 3.0f, o);        // the whole field in one go
        CHECK(AllFinite(v));
        CHECK(v.SolidVolume() >= 0.0);
        v.Edit({0, 10, 0}, 1e30f, -3.0f, o);
        CHECK(AllFinite(v));
        v.AddVolume({0, 10, 0}, 1e12, 0, 2.0f);     // asks for more than the world holds: must stop
        CHECK(AllFinite(v));
        (void)d0;
    }

    // --- the grid edge and the four corners: incremental surface == full rebuild --------------------------------------------------
    {
        for (int mats = 0; mats < 2; ++mats) {
            Scene s("E"); s.physicsEnabled = false; s.runScripts = true;
            GameObject* g = s.CreateGameObject("V");
            auto* v = g->AddComponent<VoxelTerrain>();
            v->Resize(34, 26, 34); v->voxelSize = 1.0f; v->Generate(0.5f, 4.0f, 0.2f, 9u);
            if (mats) v->GenerateMaterials(9u);
            v->renderMode = VoxelTerrain::RenderMode::Chunked; v->Apply();
            const float hx = v->HalfX(), hz = v->HalfZ(), top = v->SizeY();
            const Vec3 spots[] = {{-hx, 0, -hz}, {hx, 0, -hz}, {-hx, 0, hz}, {hx, 0, hz}, {-hx, top, -hz}, {hx, top, hz},
                                  {0, top * 0.5f, -hz - 1}, {hx + 2, top * 0.5f, 0}, {-hx, top * 0.5f, 0}, {0, 0, 0}, {0, top, 0}};
            VoxelTerrain::EditOptions o;
            for (const Vec3& c : spots) { v->Edit(c, 3.0f, 5.0f, o); v->Edit(c, 2.5f, -5.0f, o); v->Apply(); }
            for (const Vec3& c : spots) { v->Edit(c, 3.5f, 6.0f, o); v->Apply(); }
            CHECK(AllFinite(*v));
            const Mesh drawn = GatherDrawn(g), full = v->BuildMesh();
            const int diff = Differences(drawn, full);
            if (diff) std::cout << "  edge edits (materials " << mats << "): " << diff << " triangles differ from a full rebuild\n";
            CHECK(diff == 0);
        }
    }

    // --- many fast strokes, then undo: the stroke log stays bounded and undo is exact --------------------------------------------------
    {
        VoxelTerrain v = Field(40, 30, 40);
        const std::vector<float> d0 = v.density; const std::vector<unsigned char> m0 = v.material;
        v.BeginStroke();
        Lcg rng(77u);
        VoxelTerrain::EditOptions o;
        for (int i = 0; i < 3000; ++i) {
            Vec3 c{rng.Range(-20, 20), rng.Range(2, 25), rng.Range(-20, 20)};
            v.Edit(c, rng.Range(0.5f, 4.0f), rng.Range(-4.0f, 4.0f), o);
            if (i % 50 == 0) v.Slump(40);
        }
        v.EndStroke();
        CHECK(AllFinite(v));
        CHECK(v.UndoStroke());
        CHECK(v.density == d0 && v.material == m0);
        // an open stroke that is never ended must not corrupt the next one
        v.BeginStroke(); v.Edit({0, 10, 0}, 3.0f, 3.0f, o);
        v.BeginStroke(); v.Edit({5, 10, 0}, 3.0f, 3.0f, o); v.EndStroke();
        CHECK(v.UndoStroke());
        CHECK(AllFinite(v));
    }

    // --- save and load: a second round trip changes nothing, and densities and materials survive -------------------------------------
    {
        Scene s("S"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Terrain");
        auto* v = g->AddComponent<VoxelTerrain>();
        v->Resize(30, 24, 30); v->voxelSize = 1.25f; v->Generate(0.5f, 5.0f, 0.3f, 4u); v->GenerateMaterials(4u);
        Lcg rng(5u);
        for (int i = 0; i < 20; ++i) RandomEdit(*v, rng, i);
        v->Apply();
        const std::string t1 = SceneSerializer::Serialize(s);
        Scene s2("S2"); s2.physicsEnabled = false;
        CHECK(SceneSerializer::Deserialize(s2, t1));
        auto* v2 = s2.Find("Terrain")->GetComponent<VoxelTerrain>();
        CHECK(v2->nx == 30 && v2->voxelSize == 1.25f && v2->material == v->material);
        float worst = 0; for (std::size_t i = 0; i < v->density.size(); ++i) worst = std::max(worst, std::fabs(v->density[i] - v2->density[i]));
        std::cout << "  save/load worst density error " << worst << "\n";
        CHECK(worst < 0.05f);
        const std::string t2 = SceneSerializer::Serialize(s2);
        Scene s3("S3"); s3.physicsEnabled = false;
        CHECK(SceneSerializer::Deserialize(s3, t2));
        auto* v3 = s3.Find("Terrain")->GetComponent<VoxelTerrain>();
        CHECK(v3->density == v2->density && v3->material == v2->material);       // no drift on later round trips
        CHECK(!v2->CanUndo());
    }

    // --- Play then Stop restores the field ----------------------------------------------------------------------------------------
    {
        Scene s("P"); s.physicsEnabled = false;
        GameObject* cam = s.CreateGameObject("Cam"); cam->AddComponent<Camera>()->main = true;
        cam->transform->localPosition = {0, 25, 0}; cam->transform->localRotation = Quat::LookRotation(Vec3{0, 1, 0}, Vec3{0, 0, -1});
        GameObject* g = s.CreateGameObject("Terrain");
        auto* v = g->AddComponent<VoxelTerrain>();
        v->Resize(34, 26, 34); v->voxelSize = 1.0f; v->FillSlab(0.5f); v->GenerateMaterials(3u);
        auto* d = g->AddComponent<VoxelDigger>(); d->ApplyPreset(VoxelDigger::Preset::Realistic); d->showBrush = false;
        v->Apply();
        const std::string snapshot = SceneSerializer::Serialize(s);           // what Play stores
        s.Update(0.0f);
        Input::FeedMouse({0, 0}, 1u);
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
        Input::FeedMouse({0, 0}, 0u); s.Update(1.0f / 60.0f);
        CHECK(v->editCount > 0);
        Scene back("B"); back.physicsEnabled = false;
        CHECK(SceneSerializer::Deserialize(back, snapshot));                   // what Stop does
        auto* vb = back.Find("Terrain")->GetComponent<VoxelTerrain>();
        auto* db = back.Find("Terrain")->GetComponent<VoxelDigger>();
        VoxelTerrain ref; ref.Resize(34, 26, 34); ref.voxelSize = 1.0f; ref.FillSlab(0.5f);
        float err = 0; for (std::size_t i = 0; i < ref.density.size(); ++i) err = std::max(err, std::fabs(ref.density[i] - vb->density[i]));
        CHECK(err < 0.05f);
        CHECK(db && db->collect == VoxelDigger::Collect::Both && db->store.empty());   // the store does not leak into the snapshot
        CHECK(!vb->CanUndo());
        CHECK(back.FindObjectsOfType<VoxelDebris>().empty());                  // debris is transient: never saved
    }

    // --- two terrains in one scene, each with its own digger ---------------------------------------------------------------------------
    {
        Scene s("2"); s.physicsEnabled = false;
        GameObject* cam = s.CreateGameObject("Cam"); cam->AddComponent<Camera>()->main = true;
        GameObject* a = s.CreateGameObject("A"); GameObject* b = s.CreateGameObject("B");
        b->transform->localPosition = {100, 0, 0};
        VoxelTerrain* va = a->AddComponent<VoxelTerrain>(); VoxelTerrain* vb = b->AddComponent<VoxelTerrain>();
        for (auto* t : {va, vb}) { t->Resize(30, 24, 30); t->voxelSize = 1.0f; t->FillSlab(0.5f); t->GenerateMaterials(1u); t->Apply(); }
        auto* da = a->AddComponent<VoxelDigger>(); auto* db = b->AddComponent<VoxelDigger>();
        for (auto* d : {da, db}) { d->showBrush = false; d->collect = VoxelDigger::Collect::Store; d->removeFloaters = 0; }
        cam->transform->localRotation = Quat::LookRotation(Vec3{0, 1, 0}, Vec3{0, 0, -1});
        cam->transform->localPosition = {100, 25, 0};                         // over terrain B
        s.Update(0.0f);
        const std::vector<float> a0 = va->density, b0 = vb->density;
        Input::FeedMouse({0, 0}, 1u);
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
        Input::FeedMouse({0, 0}, 0u); s.Update(1.0f / 60.0f);
        std::cout << "  two terrains: A changed " << (va->density != a0) << ", B changed " << (vb->density != b0) << "\n";
        CHECK(vb->density != b0);
        CHECK(va->density == a0);                                             // the digger on A must not dig under the camera over B
        CHECK(da->store.empty() || [&] { double t = 0; for (double x : da->store) t += x; return t == 0.0; }());
        // destroying one leaves the other's chunk objects alone
        const int chunksA = va->ChunkObjectCount(), chunksB = vb->ChunkObjectCount();
        s.Destroy(b); s.Update(1.0f / 60.0f);
        CHECK(va->ChunkObjectCount() == chunksA);
        (void)chunksB;
    }

    // --- under an inactive parent: it still edits and does not draw ---------------------------------------------------------------------
    {
        Scene s("I"); s.physicsEnabled = false;
        GameObject* parent = s.CreateGameObject("Parent");
        GameObject* g = s.CreateGameObject("Terrain");
        g->transform->SetParent(parent->transform);
        auto* v = g->AddComponent<VoxelTerrain>();
        v->Resize(30, 24, 30); v->voxelSize = 1.0f; v->FillSlab(0.5f); v->Apply();
        parent->active = false;
        s.Update(1.0f / 60.0f);
        v->Dig({0, 12, 0}, 3.0f, 3.0f); v->Apply();
        s.Update(1.0f / 60.0f);
        parent->active = true;
        s.Update(1.0f / 60.0f);
        const Mesh drawn = GatherDrawn(g);
        CHECK(Differences(drawn, v->BuildMesh()) == 0);                       // reactivating shows the dug terrain
    }

    TEST_MAIN_RESULT();
}
