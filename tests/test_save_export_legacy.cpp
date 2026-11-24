// An older .okaygame slot must export for sync without being saved or renamed first.
#include "test_framework.hpp"
#include "okay/Core/SaveFramework.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

static std::string LegacyFile(Scene& s) {
    std::string t = "okaygame 2\nlabel\tOld one\nscene\t" + s.Name() + "\nsavedAt\t1000\nplayTime\t" +
                    savefmt::FormatFloat(12.0f) + "\n--scene--\n" + SceneSerializer::Serialize(s) +
                    "--state--\nkv\tcoins\ti:7\n--end--\n";
    t += "checksum\t" + savefmt::Hex16(savefmt::Fnv1a(t.data(), t.size())) + "\n";
    return t;
}

int main() {
    RUN_SUITE("save_export_legacy");
    const fs::path dir = fs::temp_directory_path() / "okay_export_legacy";
    fs::remove_all(dir);
    fs::create_directories(dir);
    Scene scene("Level");
    scene.CreateGameObject("Crate");
    {
        std::ofstream o(dir / "slot1.okaygame", std::ios::binary);
        o << LegacyFile(scene);
    }
    SaveStore store(dir.string());
    CHECK(!fs::exists(dir / "slot1.okayslot"));
    std::string bytes, err;
    SaveSlotInfo info;
    CHECK(store.ExportRaw("slot1", bytes, &info, &err));
    CHECK(err.empty());
    CHECK(info.label == "Old one");
    CHECK(info.wholeScene);
    CHECK(bytes.find("coins") != std::string::npos);
    CHECK(fs::exists(dir / "slot1.okaygame"));
    CHECK(!fs::exists(dir / "slot1.okayslot"));
    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
