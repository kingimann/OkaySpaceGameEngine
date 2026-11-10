// Object and asset references in visual scripts: a guid still finds an object
// after it is renamed, a plain name still works, a dropped prefab path spawns,
// and an old string is upgraded when the object or the file extension is known.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/VisualScript/VsRefs.hpp"
#include <fstream>

using namespace okay;

int main() {
    RUN_SUITE("vs_refs");

    {
        Scene scene("refs");
        GameObject* cube = scene.CreateGameObject("Cube");
        GameObject* mover = scene.CreateGameObject("Mover");
        const std::string src =
            "node 1 OnStart\nnode 2 Teleport @guid:" + cube->Guid() + "\n"
            "default 2 0 4\nexec 1 0 2\nentry OnStart 1\n";
        auto* vs = mover->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vs->LoadFromText(src, &err));
        cube->name = "Renamed";
        scene.Start();
        CHECK(cube->name == "Renamed");
        CHECK_NEAR(cube->transform->localPosition.x, 4.0f, 0.01f);
    }

    {
        Scene scene("legacy");
        GameObject* cube = scene.CreateGameObject("Cube");
        GameObject* mover = scene.CreateGameObject("Mover");
        auto* vs = mover->AddComponent<VisualScriptComponent>();
        std::string err;
        const std::string src =
            "node 1 OnStart\nnode 2 Teleport Cube\ndefault 2 0 3\nexec 1 0 2\nentry OnStart 1\n";
        CHECK(vs->LoadFromText(src, &err));
        scene.Start();
        CHECK_NEAR(cube->transform->localPosition.x, 3.0f, 0.01f);
    }

    {
        Scene scene("up");
        GameObject* cube = scene.CreateGameObject("Cube");
        std::string name = "Cube";
        CHECK(vs::VsUpgradeRefString(name, vs::VsRefKind::Object, &scene));
        CHECK(name == "@guid:" + cube->Guid());
        std::string path = "shot.okayprefab";
        CHECK(vs::VsUpgradeRefString(path, vs::VsRefKind::Prefab, nullptr));
        CHECK(path == "@prefab:shot.okayprefab");
        std::string keep = "Player";
        CHECK(!vs::VsUpgradeRefString(keep, vs::VsRefKind::Object, &scene));
        CHECK(keep == "Player");
        CHECK(!vs::VsUpgradeRefString(path, vs::VsRefKind::Prefab, nullptr));
    }

    {
        const std::string prefabPath = "/tmp/okay_vs_ref_marker.okayprefab";
        {
            std::ofstream f(prefabPath);
            f << "okayscene 3\n"
              << "gameobject 0 \"Marker\"\n"
              << "  active 1\n  parent -1\n"
              << "  transform 0 0 0 0 0 0 1 1 1 1\n"
              << "end\n";
        }
        auto run = [&](const std::string& arg) {
            Scene scene("spawn");
            GameObject* host = scene.CreateGameObject("Host");
            auto* vs = host->AddComponent<VisualScriptComponent>();
            const std::string src =
                "node 1 OnStart\nnode 2 Spawn " + arg + "\nexec 1 0 2\nentry OnStart 1\n";
            std::string err;
            CHECK(vs->LoadFromText(src, &err));
            if (!err.empty()) std::printf("  spawn parse: %s\n", err.c_str());
            scene.Start();
            int markers = 0;
            for (const auto& up : scene.Objects()) if (up && up->name == "Marker") ++markers;
            CHECK(markers == 1);
        };
        run("@prefab:" + prefabPath);
        run(prefabPath);
    }

    {
        Scene scene("round");
        GameObject* cube = scene.CreateGameObject("Cube");
        const std::string src =
            "node 1 OnStart\nnode 2 Teleport @guid:" + cube->Guid() + "\n"
            "default 2 0 8\nexec 1 0 2\nentry OnStart 1\n";
        std::string err;
        CHECK(SceneSerializer::SaveToFile(scene, "/tmp/okay_vs_ref_scene.okayscene"));
        // The graph is on no object yet: attach, save, load, rename, run.
        auto* vs = scene.CreateGameObject("Mover")->AddComponent<VisualScriptComponent>();
        CHECK(vs->LoadFromText(src, &err));
        CHECK(SceneSerializer::SaveToFile(scene, "/tmp/okay_vs_ref_scene.okayscene"));
        Scene loaded("loaded");
        CHECK(SceneSerializer::LoadFromFile(loaded, "/tmp/okay_vs_ref_scene.okayscene", &err));
        GameObject* again = loaded.Find("Cube");
        CHECK(again != nullptr);
        if (again) again->name = "AfterLoad";
        loaded.Start();
        if (again) CHECK_NEAR(again->transform->localPosition.x, 8.0f, 0.01f);
    }

    TEST_MAIN_RESULT();
}
