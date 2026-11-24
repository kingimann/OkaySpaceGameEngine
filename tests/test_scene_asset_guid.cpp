// Scene files keep an asset GUID next to the path. Load uses the GUID when the file has moved.
#include "test_framework.hpp"
#include "okay/Core/AssetDatabase.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include <filesystem>
#include <fstream>

using namespace okay;
namespace fs = std::filesystem;

static const char* kGuid = "0123456789abcdef0123456789abcdef";

static void Put(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    o << bytes;
}

static void Meta(const fs::path& asset) {
    Put(fs::path(asset.string() + ".okaymeta"),
        std::string("okaymeta 1\nguid ") + kGuid + "\ntype texture\n");
}

int main() {
    RUN_SUITE("scene_asset_guid");
    const fs::path root = fs::temp_directory_path() / "okay_scene_guid";
    fs::remove_all(root);
    const fs::path hero = root / "hero.png";
    Put(hero, "png");
    Meta(hero);

    AssetDatabase db;
    ScanOptions opt;
    opt.logWarnings = false;
    opt.createMetas = false;
    opt.indexUnmetaed = true;
    db.Scan(root.string(), opt);
    SetSceneAssetIndex(&db);
    CHECK(SceneAssetGuid("hero.png") == kGuid);

    Scene scene("shot");
    auto* sr = scene.CreateGameObject("Hero")->AddComponent<SpriteRenderer>();
    sr->texture = "hero.png";
    const std::string saved = SceneSerializer::Serialize(scene);
    CHECK(saved.find(kGuid) != std::string::npos);
    CHECK(saved.find(std::string("guid=") + kGuid) != std::string::npos);
    const std::size_t spriteAt = saved.find("\n  sprite ");
    CHECK(spriteAt != std::string::npos);
    const std::string spriteLine = saved.substr(spriteAt, saved.find('\n', spriteAt + 1) - spriteAt);
    CHECK(spriteLine.find(" guid ") == std::string::npos);   // not between the positional fields

    Scene round("round");
    std::string err;
    CHECK(SceneSerializer::Deserialize(round, saved, &err));
    auto* back = round.Find("Hero");
    CHECK(back && back->GetComponent<SpriteRenderer>() && back->GetComponent<SpriteRenderer>()->texture == "hero.png");

    // A scene written before GUIDs still loads the path it names.
    const char* legacy =
        "okayscene 3\n"
        "gameobject 0 \"Old\"\n"
        "  transform 0 0 0 0 0 0 1 1 1 1\n"
        "  sprite 64 1 1 1 1 1 1 \"tex/old.png\"\n"
        "end\n";
    Scene old("old");
    CHECK(SceneSerializer::Deserialize(old, legacy, &err));
    auto* oldSr = old.Find("Old")->GetComponent<SpriteRenderer>();
    CHECK(oldSr && oldSr->texture == "tex/old.png");

    // The file moves. The saved GUID still finds it.
    const fs::path moved = root / "moved" / "hero.png";
    fs::create_directories(moved.parent_path());
    fs::rename(hero, moved);
    fs::rename(fs::path(hero.string() + ".okaymeta"), fs::path(moved.string() + ".okaymeta"));
    db.Scan(root.string(), opt);
    Scene renamed("renamed");
    CHECK(SceneSerializer::Deserialize(renamed, saved, &err));
    auto* now = renamed.Find("Hero")->GetComponent<SpriteRenderer>();
    CHECK(now && now->texture == "moved/hero.png");

    SetSceneAssetIndex(nullptr);
    fs::remove_all(root);
    TEST_MAIN_RESULT();
}
