// GUID references in scene files: Stamp / Resolve / FindBroken (Core/SceneAssetGuids) over a real AssetDatabase, with old-format
// fixtures (no table) proving old files are untouched and a renamed or moved asset is found again through its GUID.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/AssetDatabase.hpp"
#include "okay/Core/SceneAssetGuids.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Graphics/Image.hpp"
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

using namespace okay;
namespace fs = std::filesystem;
namespace sg = okay::sceneguids;

namespace {
void WritePng(const fs::path& p, float r) {
    std::error_code ec; fs::create_directories(p.parent_path(), ec);
    Image img(2, 2); img.Fill(Color(r, 0, 0, 1)); img.SavePNG(p.string());
}
void WriteText(const fs::path& p, const std::string& s) {
    std::error_code ec; fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc); f << s;
}
std::string ReadText(const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
bool Has(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }

std::string SceneText(int version, const std::string& tex) {
    Scene sc("Level"); sc.physicsEnabled = false;
    sc.CreateGameObject("Hero")->AddComponent<SpriteRenderer>()->texture = tex;
    sc.CreateGameObject("Boss")->AddComponent<SpriteRenderer>()->texture = tex;
    std::string t = SceneSerializer::Serialize(sc);
    const std::size_t nl = t.find('\n');
    return "okayscene " + std::to_string(version) + t.substr(nl);
}
}

int main() {
    RUN_SUITE("scene_guids");
    const fs::path root = fs::temp_directory_path() / ("okay_sguid_" + std::to_string(std::random_device{}()));
    fs::create_directories(root);
    const std::string r = root.generic_string();
    WritePng(root / "art/hero.png", 1.0f);
    WritePng(root / "art/other.png", 0.5f);
    WriteText(root / "scenes/a.okayscene", SceneText(3, "art/hero.png"));

    AssetDatabase db;
    ScanOptions so; so.logWarnings = false;
    db.Scan(r, so);

    // --- Old files (versions 1, 2, 3, no table) are untouched by Resolve and Stamp+Strip is lossless.
    for (int v = 1; v <= 3; ++v) {
        const std::string old = SceneText(v, "art/hero.png");
        sg::Report rep;
        CHECK(sg::Resolve(old, db, &rep) == old);
        CHECK(rep.repaired == 0 && rep.broken.empty());
        CHECK(sg::ReadTable(old).empty());
        const std::string stamped = sg::Stamp(old, db, &rep);
        CHECK(rep.stamped == 1);                          // one asset, named twice
        CHECK(Has(stamped, "assetguid "));
        CHECK(stamped.compare(0, 11, "okayscene " + std::to_string(v) + "\n") == 0 || stamped.rfind("okayscene " + std::to_string(v) + "\n", 0) == 0);
        CHECK(sg::StripTable(stamped) == old);
        // Stamping twice does not grow the table.
        CHECK(sg::Stamp(stamped, db) == stamped);
        // The loader reads a stamped file like the old one.
        Scene a, b;
        CHECK(SceneSerializer::Deserialize(a, old));
        CHECK(SceneSerializer::Deserialize(b, stamped));
        CHECK(a.Objects().size() == b.Objects().size() && a.Objects().size() == 2);
        CHECK(SceneSerializer::Serialize(a) == SceneSerializer::Serialize(b));
    }
    {   // A table that has a stray quote or backslash in a path survives its own escaping.
        const std::string t = "okayscene 3\nassetguid 0123456789abcdef0123456789abcdef \"a\\\"b\\\\c.png\" \"p.png\"\ngameobject 0 \"X\"\nend\n";
        const auto tab = sg::ReadTable(t);
        CHECK(tab.size() == 1 && tab[0].written == "a\"b\\c.png" && tab[0].projectPath == "p.png");
    }

    // --- Stamp writes real .okaymeta files so the GUID lasts
    const std::string stamped = sg::Stamp(SceneText(3, "art/hero.png"), db);
    const Guid heroGuid = db.PathToGuid("art/hero.png");
    CHECK(!heroGuid.IsNull());
    CHECK(fs::exists(root / "art/hero.png.okaymeta"));
    CHECK(Has(stamped, "assetguid " + heroGuid.ToString()));

    // --- An asset renamed and moved outside the editor (file + meta moved by hand) is found again through the GUID
    WriteText(root / "scenes/b.okayscene", stamped);
    fs::create_directories(root / "art/heroes");
    fs::rename(root / "art/hero.png", root / "art/heroes/knight.png");
    fs::rename(root / "art/hero.png.okaymeta", root / "art/heroes/knight.png.okaymeta");
    db.Rescan();
    CHECK(db.GuidToPath(heroGuid) == "art/heroes/knight.png");
    CHECK(!db.Resolve("art/hero.png") || db.Resolve("art/hero.png").viaRedirect);
    {
        sg::Report rep;
        const std::string fixed = sg::Resolve(stamped, db, &rep);
        CHECK(rep.repaired == 1 && rep.broken.empty());
        CHECK(Has(fixed, "\"art/heroes/knight.png\""));
        CHECK(!Has(sg::StripTable(fixed), "art/hero.png"));
        CHECK(sg::Resolve(fixed, db) == fixed);           // a second pass has nothing left to do
        CHECK(sg::FindBroken(fixed, db).empty());
        std::string err;
        sg::Report fr;
        CHECK(sg::RepairFile((root / "scenes/b.okayscene").string(), db, &fr, &err));
        CHECK(fr.repaired == 1);
        CHECK(Has(ReadText(root / "scenes/b.okayscene"), "art/heroes/knight.png"));
        CHECK(Has(ReadText(root / "scenes/b.okayscene.bak"), "art/hero.png"));   // the old text is kept
        Scene sc;
        CHECK(SceneSerializer::LoadFromFile(sc, (root / "scenes/b.okayscene").string(), &err) && sc.Objects().size() == 2);
    }

    // --- Moved through the database (MoveAsset keeps a redirect): still repaired by GUID, spelled with the prefix style kept
    {
        const std::string t = "okayscene 3\ngameobject 0 \"X\"\n  active 1\n  sprite \"Assets/art/other.png\" 1 1 1 1\nend\n";
        // "Assets/" is not part of the project path here, so nothing resolves and nothing is recorded.
        sg::Report rep;
        CHECK(sg::Stamp(t, db, &rep) == t && rep.stamped == 0);
        const std::string s2 = sg::Stamp(SceneText(3, "art/other.png"), db);
        std::string err;
        CHECK(db.MoveAsset("art/other.png", "art/moved/other2.png", &err));
        sg::Report r2;
        const std::string f2 = sg::Resolve(s2, db, &r2);
        CHECK(r2.repaired == 1 && Has(f2, "\"art/moved/other2.png\""));
    }

    // --- A deleted asset is reported, not hidden; a path that now names another asset is not hijacked
    {
        const std::string s3 = sg::Stamp(SceneText(3, "art/heroes/knight.png"), db);
        fs::remove(root / "art/heroes/knight.png");
        fs::remove(root / "art/heroes/knight.png.okaymeta");
        db.Rescan();
        sg::Report rep;
        const std::string f3 = sg::Resolve(s3, db, &rep);
        CHECK(f3 == s3);
        CHECK(rep.broken.size() == 1 && rep.broken[0] == "art/heroes/knight.png");
        CHECK(sg::FindBroken(s3, db).size() == 1);
        CHECK(rep.ToString().find("broken art/heroes/knight.png") != std::string::npos);
    }
    {
        WritePng(root / "art/swap.png", 0.1f);
        db.Rescan();
        const std::string s4 = sg::Stamp(SceneText(3, "art/swap.png"), db);
        const Guid swapGuid = db.PathToGuid("art/swap.png");
        // The record claims a different GUID than the file now at that path: the path stays.
        std::string forged = s4;
        const std::size_t at = forged.find(swapGuid.ToString());
        forged.replace(at, 32, "0123456789abcdef0123456789abcdef");
        CHECK(sg::Resolve(forged, db) == forged);
    }

    // --- Remote and in-memory names are never recorded
    {
        const std::string t = "okayscene 3\ngameobject 0 \"X\"\n  active 1\n  sprite \"@proc\" 1 1 1 1\n  netprefab \"https://example.com/a.png\"\nend\n";
        CHECK(sg::Stamp(t, db) == t);
        CHECK(sg::FindBroken(t, db).empty());
    }

    std::error_code ec; fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
