#include "test_framework.hpp"
#include <Okay.hpp>
#include <filesystem>

using namespace okay;

// Bugs the terrain audit (docs/terrain_audit.md) proved. Each block fails on the code before the fix.
namespace {
GameObject* AimCamera(Scene& s, const char* name, bool main, Vec3 pos, Vec3 aim) {
    GameObject* c = s.CreateGameObject(name);
    auto* cam = c->AddComponent<Camera>(); cam->main = main;
    c->transform->localPosition = pos;
    c->transform->localRotation = Quat::LookRotation(aim * -1.0f, Vec3{0, 0, -1});   // the diggers aim along -Forward
    return c;
}
float Sum(const std::vector<float>& v) { double t = 0; for (float f : v) t += f; return (float)t; }
}

int main() {
    RUN_SUITE("terrain_audit");
    VoxelTerrain::ForceOldRemesh(0);

    // --- 1. The heightmap digger must not dig while a bag or menu is open (the Voxel Digger already stopped) -----
    {
        Scene s("A"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Terrain");
        auto* t = g->AddComponent<Terrain>(); t->Resize(32); t->size = 40.0f; t->Flatten(0.0f); t->Apply();
        auto* d = g->AddComponent<TerrainDigger>(); d->radius = 4.0f; d->strength = 20.0f;
        AimCamera(s, "Cam", true, {0, 8, 0}, {0, -1, 0});
        GameObject* bagObj = s.CreateGameObject("Bag");
        auto* bag = bagObj->AddComponent<InventoryUI>(); bag->open = true;       // an open bag blocks game input
        s.Update(0.0f);
        const float before = Sum(t->heights);
        Input::FeedMouse({0, 0}, 1u);
        for (int i = 0; i < 20; ++i) s.Update(1.0f / 60.0f);
        CHECK(Sum(t->heights) == before);                 // an open bag: no digging
        bag->open = false;
        for (int i = 0; i < 20; ++i) s.Update(1.0f / 60.0f);
        CHECK(Sum(t->heights) < before - 1.0f);           // and it digs again once the bag is closed
        Input::FeedMouse({0, 0}, 0u);
    }

    // --- 2. Both diggers aim from the MAIN camera, not from whichever camera is first in the scene -----------------------
    {
        Scene s("B"); s.physicsEnabled = false;
        // a minimap camera that comes first, looking at the sky
        AimCamera(s, "Minimap", false, {0, 8, 0}, {0, 1, 0});
        AimCamera(s, "Main", true, {0, 8, 0}, {0, -1, 0});
        GameObject* g = s.CreateGameObject("Terrain");
        auto* t = g->AddComponent<Terrain>(); t->Resize(32); t->size = 40.0f; t->Flatten(0.0f); t->Apply();
        auto* d = g->AddComponent<TerrainDigger>(); d->radius = 4.0f; d->strength = 20.0f;
        GameObject* vg = s.CreateGameObject("Voxel");
        vg->transform->localPosition = {100, 0, 0};
        auto* v = vg->AddComponent<VoxelTerrain>(); v->Resize(24, 20, 24); v->voxelSize = 1.0f; v->FillSlab(0.5f); v->Apply();
        auto* vd = vg->AddComponent<VoxelDigger>(); vd->radius = 3.0f; vd->strength = 20.0f;
        // the main camera hovers over each terrain in turn
        s.Update(0.0f);
        const float h0 = Sum(t->heights);
        Input::FeedMouse({0, 0}, 1u);
        for (int i = 0; i < 20; ++i) s.Update(1.0f / 60.0f);
        CHECK(Sum(t->heights) < h0 - 1.0f);               // the heightmap digger used the main camera
        Input::FeedMouse({0, 0}, 0u);
        s.Update(1.0f / 60.0f);
        s.Find("Main")->transform->localPosition = {100, v->SizeY() * 0.5f + 8.0f, 0};
        const std::vector<float> before = v->density;
        Input::FeedMouse({0, 0}, 1u);
        for (int i = 0; i < 20; ++i) s.Update(1.0f / 60.0f);
        CHECK(v->density != before);                      // so did the voxel digger
        Input::FeedMouse({0, 0}, 0u);
    }

    // --- 3. Digging a streamed cell is not thrown away when the player walks off and the cell unloads ---------------------
    {
        namespace fs = std::filesystem;
        const std::string dir = "/tmp/okay_ws_voxel_cells";
        fs::remove_all(dir); fs::create_directories(dir);
        {
            Scene bake("bake");
            GameObject* go = bake.CreateGameObject("cell_0_0");
            auto* v = go->AddComponent<VoxelTerrain>(); v->Resize(20, 16, 20); v->voxelSize = 1.0f; v->FillSlab(0.5f); v->Apply();
            CHECK(SceneSerializer::SaveObjectToFile(*go, dir + "/cell_0_0.okayprefab"));
        }
        Scene s("C"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        player->transform->localPosition = {5, 0, 5};
        GameObject* so = s.CreateGameObject("Streamer");
        auto* ws = so->AddComponent<WorldStreamer>();
        ws->cellSize = 50.0f; ws->loadRadius = 0; ws->unloadRadius = 0; ws->folder = dir; ws->prefix = "cell"; ws->target = "Player";
        ws->SyncNow();
        GameObject* cell = s.Find("cell_0_0");
        CHECK(cell != nullptr);
        auto* v = cell ? cell->GetComponent<VoxelTerrain>() : nullptr;
        CHECK(v != nullptr);
        if (v) {
            CHECK(v->Dig({0, 8.0f, 0}, 3.0f, 10.0f));
            const std::vector<float> dug = v->density;
            player->transform->localPosition = {500, 0, 5};      // far away: the cell is out of range
            ws->SyncNow(); s.Update(0.0f);
            GameObject* still = s.Find("cell_0_0");
            CHECK(still != nullptr);                               // an edited cell stays
            if (still) CHECK(still->GetComponent<VoxelTerrain>()->density == dug);
            // an untouched cell still unloads as before
            Scene s2("D"); s2.physicsEnabled = false;
            GameObject* p2 = s2.CreateGameObject("Player"); p2->transform->localPosition = {5, 0, 5};
            GameObject* so2 = s2.CreateGameObject("Streamer");
            auto* w2 = so2->AddComponent<WorldStreamer>();
            w2->cellSize = 50.0f; w2->loadRadius = 0; w2->unloadRadius = 0; w2->folder = dir; w2->prefix = "cell"; w2->target = "Player";
            w2->SyncNow(); CHECK(s2.Find("cell_0_0") != nullptr);
            p2->transform->localPosition = {500, 0, 5};
            w2->SyncNow(); s2.Update(0.0f);
            CHECK(s2.Find("cell_0_0") == nullptr);
        }
    }

    TEST_MAIN_RESULT();
}
