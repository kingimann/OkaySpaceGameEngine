// The build pipeline's pure logic (okay/Core/BuildPipeline.hpp): layout planner, the player's data-folder
// lookup, the dependency walk (what a build ships and what it leaves out), the build manifest, the report,
// the zip writer, and the Build Settings text (new keys, and old profile files that lack them).
// Temp folders only; nothing sleeps.
#include "test_framework.hpp"
#include "okay/Core/BuildPipeline.hpp"
#include "../editor/ProjectState.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace fs = std::filesystem;
namespace bp = okay::buildpipe;

namespace {

void Write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}
std::string Blob(std::size_t n, char c = 'x') { return std::string(n, c); }

bool Has(const bp::WalkResult& w, const std::string& dest) {
    for (const auto& a : w.assets) if (a.dest == dest) return true;
    return false;
}

void TestLayout() {
    RUN_SUITE("layout planner");
    bp::Layout l;
    CHECK(bp::ParseLayout("Folder", l) && l == bp::Layout::Folder);
    CHECK(bp::ParseLayout("compact", l) && l == bp::Layout::Compact);
    CHECK(bp::ParseLayout("legacy", l) && l == bp::Layout::Compact);
    CHECK(bp::ParseLayout("ZIP", l) && l == bp::Layout::Zip);
    CHECK(!bp::ParseLayout("tarball", l));
    CHECK(std::string(bp::LayoutName(bp::Layout::Zip)) == "zip");

    CHECK(bp::FileSafeName("My Game") == "My Game");
    CHECK(bp::FileSafeName("a/b\\c:d*e?f") == "a_b_c_d_e_f");
    CHECK(bp::FileSafeName("  Game. ") == "Game");
    CHECK(bp::FileSafeName("") == "Game");
    CHECK(bp::FileSafeName("...") == "Game");
    CHECK(bp::FileSafeName("CON") == "CON_");
    CHECK(bp::FileSafeName("", "Fallback") == "Fallback");

    bp::NameTokens t{"Star Arena", "1.2.0", "windows", "2026-11-12", 7};
    CHECK(bp::ExpandPattern("{name}-{version}-b{build}-{platform}", t) == "Star Arena-1.2.0-b7-windows");
    CHECK(bp::ExpandPattern("", t) == "Star Arena-1.2.0");
    CHECK(bp::ExpandPattern("{Name}_{date}", t) == "Star Arena_2026-11-12");
    CHECK(bp::ExpandPattern("x{unknown}y", t) == "x{unknown}y");
    CHECK(bp::ExpandPattern("{name}/{version}", t) == "Star Arena_1.2.0");   // never a path

    bp::LayoutInput in;
    in.gameName = "Star Arena"; in.tokens = t; in.archivePattern = "{name}-{version}";
    bp::LayoutPlan p = bp::PlanLayout(in);
    CHECK(p.effective == bp::Layout::Folder && p.exeName == "Star Arena.exe" && p.dataDir == "Star Arena_Data" && !p.makeZip && p.keepFolder && p.notes.empty());
    in.windows = false;
    CHECK(bp::PlanLayout(in).exeName == "Star Arena");
    in.windows = true;

    in.layout = bp::Layout::Compact;
    p = bp::PlanLayout(in);
    CHECK(p.effective == bp::Layout::Compact && p.dataDir == "Data" && !p.makeZip);
    in.compactDataFolder = false;
    CHECK(bp::PlanLayout(in).dataDir.empty());   // everything beside the exe
    in.compactDataFolder = true;

    in.layout = bp::Layout::Zip;
    p = bp::PlanLayout(in);
    CHECK(p.effective == bp::Layout::Zip && p.makeZip && !p.keepFolder && p.dataDir == "Star Arena_Data" && p.zipName == "Star Arena-1.2.0.zip");
    in.keepFolderWithZip = true;
    CHECK(bp::PlanLayout(in).keepFolder);
    in.keepFolderWithZip = false;

    // Anti-cheat verifies Data/manifest.json, so it needs the compact layout (and says so).
    in.antiCheat = true; in.layout = bp::Layout::Folder;
    p = bp::PlanLayout(in);
    CHECK(p.requested == bp::Layout::Folder && p.effective == bp::Layout::Compact && p.dataDir == "Data" && p.notes.size() == 1);
    in.layout = bp::Layout::Zip;
    p = bp::PlanLayout(in);
    CHECK(p.effective == bp::Layout::Zip && p.dataDir == "Data" && p.notes.size() == 1);
    in.antiCheat = false;

    // A name with characters a file system rejects still gives one folder and one exe.
    in.layout = bp::Layout::Folder; in.gameName = "A:B/C";
    p = bp::PlanLayout(in);
    CHECK(p.safeName == "A_B_C" && p.dataDir == "A_B_C_Data" && p.exeName == "A_B_C.exe");
}

void TestFindDataDir(const fs::path& tmp) {
    RUN_SUITE("player data folder lookup");
    const fs::path a = tmp / "find_a";
    Write(a / "Game_Data" / "game.okayconfig", "title=Game\n");
    Write(a / "Data" / "game.okayconfig", "title=Old\n");
    CHECK(bp::FindDataDir(a.generic_string() + "/", "Game") == (a / "Game_Data").generic_string() + "/");   // the named folder wins over Data/
    CHECK(bp::FindDataDir(a.generic_string() + "/", "Renamed") == (a / "Game_Data").generic_string() + "/");  // exe renamed: the only *_Data folder with a config
    Write(a / "Other_Data" / "game.okayconfig", "title=Other\n");
    CHECK(bp::FindDataDir(a.generic_string() + "/", "Other") == (a / "Other_Data").generic_string() + "/");
    CHECK(bp::FindDataDir(a.generic_string() + "/", "Nope") == (a / "Data").generic_string() + "/");           // two candidates, no match: the compact layout

    const fs::path c = tmp / "find_c";
    Write(c / "Data" / "game.okayconfig", "x");
    CHECK(bp::FindDataDir(c.generic_string() + "/", "Game") == (c / "Data").generic_string() + "/");        // a compact build
    const fs::path d = tmp / "find_d";
    Write(d / "game.okayconfig", "x");
    CHECK(bp::FindDataDir(d.generic_string() + "/", "Game") == d.generic_string() + "/");                   // an old flat build: its own folder
    const fs::path e = tmp / "find_e";
    Write(e / "Game_Data" / "readme.txt", "no config here");
    CHECK(bp::FindDataDir(e.generic_string() + "/", "Game") == e.generic_string() + "/");                   // an empty _Data folder is not a game
}

void TestWalk(const fs::path& tmp) {
    RUN_SUITE("dependency walk");
    const fs::path proj = tmp / "proj";
    Write(proj / "Assets" / "Textures" / "hero.png", Blob(300));
    Write(proj / "Assets" / "Textures" / "hero.png.okaymeta", "guid x\n");
    Write(proj / "Assets" / "Textures" / "unused_big.png", Blob(2 << 20));
    Write(proj / "Assets" / "Audio" / "boom.wav", Blob(500));
    Write(proj / "Assets" / "Audio" / "never.ogg", Blob(900));
    Write(proj / "Assets" / "Prefabs" / "enemy.okayprefab", "okayscene 1\ngameobject 0 \"E\"\n  sprite \"Textures/enemy.png\"\n  audio \"Audio/boom.wav\"\n  prefab \"Prefabs/loop.okayprefab\"\nend\n");
    Write(proj / "Assets" / "Prefabs" / "loop.okayprefab", "okayscene 1\ngameobject 0 \"L\"\n  prefab \"Prefabs/enemy.okayprefab\"\n  sprite \"Textures/loop.png\"\nend\n");
    Write(proj / "Assets" / "Textures" / "enemy.png", Blob(100));
    Write(proj / "Assets" / "Textures" / "loop.png", Blob(110));
    Write(proj / "Assets" / "Materials" / "stone.okaymat", "okaymat 1\ntexture Textures/stone.png\n");
    Write(proj / "Assets" / "Textures" / "stone.png", Blob(120));
    Write(proj / "Assets" / "Levels" / "one.okayscene", "okayscene 1\n");
    Write(proj / "Assets" / "Levels" / "two.okayscene", "okayscene 1\n");
    Write(proj / "Assets" / "old.psd", Blob(40));
    Write(proj / "Assets" / ".cache" / "junk.bin", Blob(40));
    Write(tmp / "elsewhere" / "outside.png", Blob(77));

    bp::WalkSettings s;
    s.projectDir = proj.generic_string();
    // The scene uses hero.png (as written relative to Assets), a prefab, a material, a file that does not exist,
    // and one by absolute path.
    std::vector<bp::AssetRef> roots = {
        {"Textures/hero.png", "level"}, {"Prefabs/enemy.okayprefab", "level"}, {"Materials/stone.okaymat", "level"},
        {"Textures/ghost.png", "level"}, {(tmp / "elsewhere" / "outside.png").generic_string(), "level"},
        {"Textures/hero.png", "level"},   // twice: shipped once
        {"@baked_layer", "level"}, {"proc:checker", "level"}, {"https://example.com/x.png", "level"}};
    const bp::WalkResult w = bp::WalkAssets(roots, s);
    CHECK(Has(w, "Textures/hero.png"));
    CHECK(Has(w, "Audio/boom.wav"));                  // used by the prefab, not by the scene
    CHECK(Has(w, "Textures/enemy.png"));
    CHECK(Has(w, "Prefabs/loop.okayprefab") && Has(w, "Textures/loop.png"));   // the prefab cycle terminates and is followed
    CHECK(Has(w, "Textures/stone.png"));              // through the material
    CHECK(Has(w, "outside.png"));                     // an absolute reference is kept by file name
    CHECK(!Has(w, "Textures/unused_big.png") && !Has(w, "Audio/never.ogg"));
    CHECK(w.assets.size() == 9);
    CHECK(w.missing.size() == 1 && w.missing[0].ref == "Textures/ghost.png");
    CHECK(w.absolute.size() == 1);
    for (const auto& a : w.assets) CHECK(!a.src.empty() && a.bytes > 0 && !a.reason.empty());
    bool prefabReason = false;
    for (const auto& a : w.assets) if (a.dest == "Audio/boom.wav") prefabReason = a.reason.find("enemy.okayprefab") != std::string::npos;
    CHECK(prefabReason);

    // References cannot climb out of the project: "../" never reaches a file by search.
    bp::WalkSettings s2; s2.projectDir = (proj / "Assets").generic_string();
    const bp::WalkResult up = bp::WalkAssets({{"../elsewhere/outside.png", "level"}}, s2);
    CHECK(up.assets.empty() && up.missing.size() == 1);
    CHECK(bp::DestinationFor("../../etc/passwd") == "etc/passwd");
    CHECK(bp::DestinationFor("a\\b\\c.png") == "a/b/c.png");
    CHECK(bp::DestinationFor("/abs/dir/x.png") == "x.png");
    CHECK(bp::DestinationFor("./x/./y.png") == "x/y.png");

    // A project listing, and what a build with stripping leaves out (with the reason).
    const std::vector<bp::ProjectAsset> all = bp::ListProjectAssets(proj.generic_string());
    bool sawMeta = false, sawHidden = false, psdEditorOnly = false, sceneFlag = false;
    for (const auto& a : all) {
        if (a.rel.find(".okaymeta") != std::string::npos) sawMeta = true;
        if (a.rel.find(".cache") != std::string::npos) sawHidden = true;
        if (a.rel == "old.psd") psdEditorOnly = a.editorOnly;
        if (a.rel == "Levels/one.okayscene") sceneFlag = a.scene;
    }
    CHECK(!sawMeta && !sawHidden && psdEditorOnly && sceneFlag);
    CHECK(bp::ListProjectAssets("").empty());

    std::vector<std::string> shipped, scenes = {(proj / "Assets" / "Levels" / "one.okayscene").generic_string()};
    for (const auto& a : w.assets) shipped.push_back(a.src);
    const std::vector<bp::ExcludedFile> ex = bp::FindUnshipped(proj.generic_string(), shipped, scenes);
    auto reasonOf = [&](const std::string& path) { for (const auto& x : ex) if (x.path == path) return x.reason; return std::string("(shipped)"); };
    CHECK(reasonOf("Assets/Textures/unused_big.png") == "not used by any scene in the build");
    CHECK(reasonOf("Assets/Audio/never.ogg") == "not used by any scene in the build");
    CHECK(reasonOf("Assets/Levels/two.okayscene") == "scene is not in the build");
    CHECK(reasonOf("Assets/old.psd") == "editor-only file");
    CHECK(reasonOf("Assets/Textures/hero.png") == "(shipped)");
    CHECK(reasonOf("Assets/Levels/one.okayscene") == "(shipped)");
    CHECK(ex.size() == 4);

    // StreamingAssets: listed as they are.
    Write(proj / "StreamingAssets" / "levels" / "pack.json", "{}");
    Write(proj / "StreamingAssets" / "readme.txt", "hi");
    const auto sa = bp::ListStreamingAssets(proj.generic_string());
    CHECK(sa.size() == 2 && sa[0].first == "levels/pack.json" && sa[1].first == "readme.txt");
    CHECK(bp::ListStreamingAssets((tmp / "nope").generic_string()).empty());
}

void TestKinds() {
    RUN_SUITE("file kinds");
    CHECK(bp::KindOf("Game.exe") == bp::FileKind::Executable);
    CHECK(bp::KindOf("Game") == bp::FileKind::Executable);
    CHECK(bp::KindOf("SDL2.dll") == bp::FileKind::Library);
    CHECK(bp::KindOf("libSDL2-2.0.so.0") == bp::FileKind::Library);
    CHECK(bp::KindOf("Game_Data/game.okayscene") == bp::FileKind::Scene);
    CHECK(bp::KindOf("Game_Data/game.okayconfig") == bp::FileKind::Config);
    CHECK(bp::KindOf("Game_Data/Textures/a.PNG") == bp::FileKind::Texture);
    CHECK(bp::KindOf("Game_Data/Audio/a.ogg") == bp::FileKind::Audio);
    CHECK(bp::KindOf("Game_Data/Tower.okaymesh") == bp::FileKind::Model);
    CHECK(bp::KindOf("Game_Data/f.ttf") == bp::FileKind::Font);
    CHECK(bp::KindOf("Game_Data/StreamingAssets/pack.dll") == bp::FileKind::Streaming);
    CHECK(bp::KindOf("Game_Data/build.json") == bp::FileKind::Data);
    CHECK(bp::IsEditorOnlyFile("a/b.psd") && bp::IsEditorOnlyFile("x.bak") && bp::IsEditorOnlyFile("a/Thumbs.db") && bp::IsEditorOnlyFile("n~"));
    CHECK(!bp::IsEditorOnlyFile("a/b.png"));
}

void TestManifest() {
    RUN_SUITE("build manifest");
    bp::BuildManifest m;
    m.game = "Star \"Arena\""; m.company = "Acme\\Co"; m.bundleId = "com.acme.arena"; m.version = "1.2.0"; m.engineVersion = "2.852.0";
    m.dateUtc = "2026-11-12T09:30:00Z"; m.platform = "windows"; m.layout = "folder"; m.settingsHash = "00ff00ff00ff00ff";
    m.buildNumber = 41; m.development = true; m.stripped = false; m.compressed = true;
    m.scenes = {"game.okayscene", "level 2.okayscene"}; m.assetCount = 12; m.totalBytes = 5000000000ull;
    const std::string js = bp::ManifestToJson(m);
    bp::BuildManifest back; std::string err;
    CHECK(bp::ParseManifestJson(js, back, &err));
    CHECK(back.game == m.game && back.company == m.company && back.bundleId == "com.acme.arena" && back.version == m.version && back.engineVersion == m.engineVersion);
    CHECK(back.dateUtc == m.dateUtc && back.platform == m.platform && back.layout == m.layout && back.settingsHash == m.settingsHash);
    CHECK(back.buildNumber == 41 && back.development && !back.stripped && back.compressed);
    CHECK(back.scenes == m.scenes && back.assetCount == 12 && back.totalBytes == 5000000000ull);
    CHECK(bp::ManifestToJson(back) == js);   // stable text
    CHECK(bp::ManifestSummary(m).find("build 41") != std::string::npos && bp::ManifestSummary(m).find("2 scenes") != std::string::npos && bp::ManifestSummary(m).find("development") != std::string::npos);

    bp::BuildManifest bad;
    CHECK(!bp::ParseManifestJson("", bad, &err));
    CHECK(!bp::ParseManifestJson("[1,2]", bad, &err));
    CHECK(!bp::ParseManifestJson("{\"game\" \"x\"}", bad, &err));
    CHECK(!bp::ParseManifestJson("{}", bad, &err));
    CHECK(bp::ParseManifestJson("{\"game\":\"G\",\"futureKey\":[\"a\"],\"buildNumber\":3}", bad, &err) && bad.game == "G" && bad.buildNumber == 3);   // unknown keys are ignored

    // The hash tells two settings texts apart and is the same for the same text.
    CHECK(bp::HashText("a=1") == bp::HashText("a=1") && bp::HashText("a=1") != bp::HashText("a=2"));
    CHECK(bp::HashHex(255) == "00000000000000ff");
    const std::string now = bp::UtcNowIso();
    CHECK(now.size() == 20 && now[4] == '-' && now[10] == 'T' && now.back() == 'Z');
}

void TestReport() {
    RUN_SUITE("build report");
    bp::Report r;
    r.layout = "folder"; r.outputDir = "build/Game"; r.seconds = 1.5;
    r.AddFile("Game.exe", 4000000, "the player runtime");
    r.AddFile("Game_Data/game.okayscene", 2000);
    r.AddFile("Game_Data/Textures/a.png", 300000, "used by scene one");
    r.AddFile("Game_Data/Textures/b.png", 200000);
    r.AddFile("Game_Data/Audio/x.wav", 900000);
    r.AddFile("Game_Data/StreamingAssets/n.dat", 10);
    r.AddExcluded("Assets/big.png", 5 << 20, "not used by any scene in the build");
    r.AddExcluded("Assets/old.psd", 100, "editor-only file");
    r.Warn("Missing asset 'q.png'");
    r.Finish();
    CHECK(r.valid && r.total == 4000000 + 2000 + 300000 + 200000 + 900000 + 10);
    CHECK(r.files[0].path == "Game.exe" && r.files[1].path == "Game_Data/Audio/x.wav");   // largest first
    CHECK(r.kindBytes[(int)bp::FileKind::Texture] == 500000 && r.kindCount[(int)bp::FileKind::Texture] == 2);
    CHECK(r.kindBytes[(int)bp::FileKind::Executable] == 4000000 && r.kindCount[(int)bp::FileKind::Scene] == 1 && r.kindCount[(int)bp::FileKind::Streaming] == 1);
    CHECK(r.excluded.size() == 2 && r.excluded[0].path == "Assets/big.png" && r.excludedBytes == (5u << 20) + 100);
    const auto types = r.ByExtension();
    CHECK(!types.empty() && types[0].ext == "exe" && types[0].count == 1);
    bool png = false; for (const auto& t : types) if (t.ext == "png") png = t.count == 2 && t.bytes == 500000;
    CHECK(png);
    const std::string text = r.ToText();
    CHECK(text.find("Total: 5.2 MB in 6 files") != std::string::npos);
    CHECK(text.find("Build time: 1.50 s") != std::string::npos);
    CHECK(text.find("Game_Data/Textures/a.png") != std::string::npos && text.find("used by scene one") != std::string::npos);
    CHECK(text.find("Assets/big.png") != std::string::npos && text.find("editor-only file") != std::string::npos);
    CHECK(text.find("Missing asset 'q.png'") != std::string::npos && text.find("Warnings: 1") != std::string::npos);
    CHECK(text.find("dry run") == std::string::npos);
    r.dryRun = true;
    CHECK(r.ToText().find("dry run: nothing was written") != std::string::npos);
    CHECK(r.ToText(2).find("... 4 more") != std::string::npos);
    CHECK(bp::HumanBytes(0) == "0 B" && bp::HumanBytes(1536) == "1.5 KB" && bp::HumanBytes(3u << 20) == "3.0 MB");
}

void TestZip(const fs::path& tmp) {
    RUN_SUITE("zip archive");
    const fs::path src = tmp / "zipsrc";
    Write(src / "Game.exe", Blob(5000, 'e'));
    std::string text; for (int i = 0; i < 400; ++i) text += "line " + std::to_string(i % 7) + " of the scene\n";
    Write(src / "Game_Data" / "game.okayscene", text);
    Write(src / "Game_Data" / "pic.png", Blob(2000, 'p'));          // already compressed type: stored
    Write(src / "Game_Data" / "empty.txt", "");
    Write(src / "Game_Data" / "StreamingAssets" / "x y.json", "{}");

    bp::ZipStats stored, packed; std::string err;
    CHECK(bp::WriteZip((tmp / "stored.zip").string(), src.string(), false, &stored, &err));
    CHECK(bp::WriteZip((tmp / "packed.zip").string(), src.string(), true, &packed, &err));
    CHECK(stored.files == 5 && packed.files == 5 && stored.stored == packed.stored);
    CHECK(stored.packed == stored.stored);                 // nothing compressed
    CHECK(packed.packed < packed.stored / 2);              // repeated text and bytes shrink a lot

    std::vector<bp::ZipEntry> es;
    CHECK(bp::ReadZipDirectory((tmp / "packed.zip").string(), es, &err) && es.size() == 5);
    std::vector<std::string> names;
    for (const auto& e : es) names.push_back(e.name);
    CHECK(std::is_sorted(names.begin(), names.end()));
    CHECK(std::find(names.begin(), names.end(), "Game_Data/StreamingAssets/x y.json") != names.end() && std::find(names.begin(), names.end(), "Game.exe") != names.end());
    for (const auto& e : es) {
        if (e.name == "Game_Data/game.okayscene") CHECK(e.method == 8 && e.packed < e.size && e.size == text.size());
        if (e.name == "Game_Data/pic.png") CHECK(e.method == 0 && e.packed == e.size);
        if (e.name == "Game_Data/empty.txt") CHECK(e.method == 0 && e.size == 0);
    }
    std::vector<bp::ZipEntry> es2;
    CHECK(bp::ReadZipDirectory((tmp / "stored.zip").string(), es2, &err) && es2.size() == 5);
    for (const auto& e : es2) CHECK(e.method == 0);

    // The same folder gives the same list; a zip inside the folder is not packed into itself.
    const fs::path src2 = tmp / "zipsrc2";
    fs::copy(src, src2, fs::copy_options::recursive);
    CHECK(bp::WriteZip((src2 / "self.zip").string(), src2.string(), true, nullptr, &err));
    std::vector<bp::ZipEntry> es3;
    CHECK(bp::ReadZipDirectory((src2 / "self.zip").string(), es3, &err) && es3.size() == 5);

    // Failures say why and leave no half-written archive.
    CHECK(!bp::WriteZip((tmp / "no.zip").string(), (tmp / "missing-folder").string(), true, nullptr, &err) && !err.empty());
    CHECK(!bp::ReadZipDirectory((tmp / "no.zip").string(), es3, &err));
    Write(tmp / "junk.zip", "this is not a zip file at all, just some text of enough length");
    CHECK(!bp::ReadZipDirectory((tmp / "junk.zip").string(), es3, &err));

    // A real unzip agrees (when the tool is installed): every entry's CRC and the deflate data are valid.
    if (std::system("unzip -v > /dev/null 2>&1") == 0) {
        CHECK(std::system(("unzip -tq \"" + (tmp / "packed.zip").string() + "\" > /dev/null 2>&1").c_str()) == 0);
        CHECK(std::system(("unzip -tq \"" + (tmp / "stored.zip").string() + "\" > /dev/null 2>&1").c_str()) == 0);
    }
}

void TestSettingsText() {
    RUN_SUITE("settings text");
    oked::BuildProfile p;
    p.name = "Release"; p.productName = "Star Arena";
    CHECK(p.settings.layout == 0 && p.settings.stripAssets && p.settings.compress && p.settings.buildNumber == 1 && p.settings.autoBuildNumber);
    p.settings.layout = 2; p.settings.stripAssets = false; p.settings.buildNumber = 99;
    std::snprintf(p.settings.archiveName, sizeof(p.settings.archiveName), "%s", "{name}_{build}");
    const std::string text = oked::WriteBuildProfiles({p});
    CHECK(text.find("layout=2") != std::string::npos && text.find("strip=0") != std::string::npos && text.find("buildNumber=99") != std::string::npos);
    const auto back = oked::ParseBuildProfiles(text);
    CHECK(back.size() == 1 && back[0].settings.layout == 2 && !back[0].settings.stripAssets && back[0].settings.buildNumber == 99 && std::string(back[0].settings.archiveName) == "{name}_{build}");

    // A profile file from before these settings existed loads with the defaults (Folder, strip, compress, build 1).
    const auto old = oked::ParseBuildProfiles("@profile Old\nproduct=Old Game\nwidth=800\ndataFolder=1\n");
    CHECK(old.size() == 1 && old[0].settings.width == 800 && old[0].settings.layout == 0 && old[0].settings.stripAssets && old[0].settings.compress
          && old[0].settings.buildNumber == 1 && old[0].settings.autoBuildNumber && std::string(old[0].settings.archiveName) == "{name}-{version}-{platform}");
    // Out-of-range values are clamped, not trusted.
    const auto odd = oked::ParseBuildProfiles("@profile Odd\nlayout=9\nbuildNumber=-5\n");
    CHECK(odd.size() == 1 && odd[0].settings.layout == 2 && odd[0].settings.buildNumber == 1);
}

} // namespace

int main() {
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path(ec) / ("okay_build_pipeline_" + std::to_string((long long)std::time(nullptr)));
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    TestLayout();
    TestFindDataDir(tmp);
    TestWalk(tmp);
    TestKinds();
    TestManifest();
    TestReport();
    TestZip(tmp);
    TestSettingsText();
    fs::remove_all(tmp, ec);
    TEST_MAIN_RESULT();
}
