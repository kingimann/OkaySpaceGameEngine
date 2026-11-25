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

    // An edit made outside the editor, scanned with the editor's options: the path is known, so it is
    // the same asset with new content. It keeps its GUID and raises SourceChanged (not Added), which is
    // what the editor's Browser::TakeChangedFiles passes on to refresh the Scene view.
    {
        ScanOptions ed = opt;
        ed.verifyHashes = false;
        ed.fullHashLimit = 32u << 20;
        AssetDatabase db;
        db.Scan(root.string(), ed);
        const Guid g = db.PathToGuid("art/hero.png");
        CHECK(!g.IsNull());
        Put(root / "art" / "hero.png", "png-bytes-edited-outside");
        r = db.Scan(root.string(), ed);
        CHECK(db.PathToGuid("art/hero.png") == g);
        bool changed = false, added = false;
        for (const ReimportEvent& ev : r.reimports) {
            if (ev.path != "art/hero.png") continue;
            if (ev.reason == ReimportEvent::Reason::SourceChanged) changed = true;
            if (ev.reason == ReimportEvent::Reason::Added) added = true;
        }
        CHECK(changed);
        CHECK(!added);

        // An asset with a meta: the meta is deleted and the file edited. The database still knows the
        // path, so the GUID stays.
        ScanOptions withMeta = ed;
        withMeta.createMetas = true;
        AssetDatabase m;
        m.Scan(root.string(), withMeta);
        const Guid mg = m.PathToGuid("art/other.png");
        CHECK(!mg.IsNull() && fs::exists(root / "art" / "other.png.okaymeta"));
        fs::remove(root / "art" / "other.png.okaymeta");
        Put(root / "art" / "other.png", "png-other-edited");
        r = m.Scan(root.string(), ed);
        CHECK(m.PathToGuid("art/other.png") == mg);
    }

    std::error_code ec;
    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
