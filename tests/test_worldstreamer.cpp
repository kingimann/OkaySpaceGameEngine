#include "test_framework.hpp"
#include <Okay.hpp>
#include <filesystem>
#include <string>

using namespace okay;

// World Partition / streaming: keep only the cells near the target resident,
// load/unload as it moves, with hysteresis so border-pacing doesn't thrash.
int main() {
    RUN_SUITE("worldstreamer");

    namespace fs = std::filesystem;
    const std::string dir = "/tmp/okay_ws_cells";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // Bake a 6x6 grid of cell prefab files, each one object named "cell_x_z".
    // Deliberately leave cell (1,1) missing to prove sparse worlds are fine.
    const float kCell = 50.0f;
    {
        Scene bake("bake");
        for (int z = 0; z < 6; ++z)
            for (int x = 0; x < 6; ++x) {
                if (x == 1 && z == 1) continue;   // hole in the world
                GameObject* go = bake.CreateGameObject("cell_" + std::to_string(x) + "_" + std::to_string(z));
                go->transform->localPosition = Vec3{x * kCell + 1.0f, 0.0f, z * kCell + 1.0f};
                std::string path = dir + "/cell_" + std::to_string(x) + "_" + std::to_string(z) + ".okayprefab";
                CHECK(SceneSerializer::SaveObjectToFile(*go, path));
            }
    }

    Scene scene("world");
    GameObject* player = scene.CreateGameObject("Player");
    player->transform->localPosition = Vec3{kCell * 2 + 5.0f, 0.0f, kCell * 2 + 5.0f};  // cell (2,2)

    GameObject* streamObj = scene.CreateGameObject("Streamer");
    auto* ws = streamObj->AddComponent<WorldStreamer>();
    ws->cellSize     = kCell;
    ws->loadRadius   = 1;     // 3x3 block around the target
    ws->unloadRadius = 2;     // hysteresis ring
    ws->folder       = dir;
    ws->prefix       = "cell";
    ws->ext          = ".okayprefab";
    ws->target       = "Player";

    // --- Initial sync: the 3x3 block around (2,2) loads; (1,1) hole is skipped.
    {
        ws->SyncNow();
        int cx = -99, cz = -99;
        CHECK(ws->CurrentCell(cx, cz));
        CHECK(cx == 2 && cz == 2);
        for (int z = 1; z <= 3; ++z)
            for (int x = 1; x <= 3; ++x)
                CHECK(ws->IsCellLoaded(x, z));     // every cell in the ring is tracked
        // The actual prefab instantiated for present cells; (1,1) is a recorded
        // miss (tracked so we don't retry every frame) but spawned nothing.
        CHECK(scene.Find("cell_2_2") != nullptr);
        CHECK(scene.Find("cell_1_1") == nullptr);
        // Far cell never touched.
        CHECK(!ws->IsCellLoaded(5, 5));
        CHECK(scene.Find("cell_5_5") == nullptr);
    }

    // --- Move two cells over to (4,2): (1,*) cells fall outside the unload ring
    //     and are dropped; the new column (5,*) streams in.
    {
        player->transform->localPosition = Vec3{kCell * 4 + 5.0f, 0.0f, kCell * 2 + 5.0f};  // cell (4,2)
        ws->Update(0.016f);
        int cx = 0, cz = 0; ws->CurrentCell(cx, cz);
        CHECK(cx == 4 && cz == 2);

        CHECK(ws->IsCellLoaded(5, 2));     // newly in range
        CHECK(ws->IsCellLoaded(3, 2));     // still in the 3x3 around (4,2)
        // Cells at x=1 are now |4-1|=3 > unloadRadius(2): unloaded.
        CHECK(!ws->IsCellLoaded(1, 2));
        CHECK(!ws->IsCellLoaded(1, 3));

        // Flush the frame so queued Destroy actually removes the objects.
        scene.Update(0.016f);
        CHECK(scene.Find("cell_5_2") != nullptr);   // streamed in
        CHECK(scene.Find("cell_1_2") == nullptr);   // unloaded
        CHECK(scene.Find("cell_4_2") != nullptr);   // center present
    }

    // --- Hysteresis: nudge within the same cell -> no re-evaluation, set stable.
    {
        std::size_t before = ws->LoadedCount();
        player->transform->localPosition = Vec3{kCell * 4 + 8.0f, 0.0f, kCell * 2 + 8.0f};  // still cell (4,2)
        ws->Update(0.016f);
        CHECK(ws->LoadedCount() == before);
    }

    // --- Serialization round-trip of the component config -------------------
    {
        std::string text = SceneSerializer::SerializeObject(*streamObj);
        Scene re("re");
        std::string err;
        GameObject* root = SceneSerializer::InstantiateFromText(re, text, &err);
        CHECK(root != nullptr);
        auto* ws2 = root ? root->GetComponent<WorldStreamer>() : nullptr;
        CHECK(ws2 != nullptr);
        if (ws2) {
            CHECK_NEAR(ws2->cellSize, kCell, 0.01f);
            CHECK(ws2->loadRadius == 1);
            CHECK(ws2->unloadRadius == 2);
            CHECK(ws2->folder == dir);
            CHECK(ws2->prefix == "cell");
            CHECK(ws2->ext == ".okayprefab");
            CHECK(ws2->target == "Player");
        }
    }

    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
