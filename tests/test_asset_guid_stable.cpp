// Assets indexed with no .okaymeta keep the same GUID in a later session.
// Area 1.12: indexUnmetaed used to mint a fresh GUID every process, so the id lasted only for that session.
#include "test_framework.hpp"
#include "okay/Core/AssetDatabase.hpp"
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

static void Put(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    o << bytes;
}

int main() {
    RUN_SUITE("asset_guid_stable");
    const fs::path root = fs::temp_directory_path() / ("okay_guid_" + std::to_string(std::random_device{}()));
    Put(root / "art" / "hero.png", "png-bytes");
    Put(root / "scenes" / "a.okayscene", "scene \"A\"\n");

    ScanOptions opt;
    opt.createMetas = false;
    opt.indexUnmetaed = true;
    opt.logWarnings = false;

    AssetDatabase first;
    ScanReport r = first.Scan(root.string(), opt);
    CHECK(r.created == 0 && r.unmetaed == 2);
    CHECK(!fs::exists(root / "art" / "hero.png.okaymeta"));
    const Guid hero = first.PathToGuid("art/hero.png");
    const Guid scene = first.PathToGuid("scenes/a.okayscene");
    CHECK(!hero.IsNull() && !scene.IsNull() && hero != scene);

    // Same process, nothing changed: the id does not move.
    r = first.Scan(root.string(), opt);
    CHECK(first.PathToGuid("art/hero.png") == hero && r.reimports.empty());

    // A new database is a new session. The id comes from the path, not from a meta file.
    AssetDatabase second;
    second.Scan(root.string(), opt);
    CHECK(second.PathToGuid("art/hero.png") == hero);
    CHECK(second.PathToGuid("scenes/a.okayscene") == scene);
    CHECK(!fs::exists(root / "art" / "hero.png.okaymeta"));

    // A third path gets its own stable id and does not disturb the first.
    Put(root / "art" / "other.png", "png-other");
    AssetDatabase third;
    third.Scan(root.string(), opt);
    const Guid other = third.PathToGuid("art/other.png");
    CHECK(!other.IsNull() && other != hero && third.PathToGuid("art/hero.png") == hero);

    std::error_code ec;
    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
