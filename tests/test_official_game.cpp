#include "test_framework.hpp"
#include "OfficialGame.hpp"
#include <filesystem>
#include <fstream>

// The launcher's official game: manifest parsing (untrusted input), versions,
// update diffs, and the journaled swap that must never leave a broken install.
namespace {
namespace fs = std::filesystem;
using namespace okayofficial;

std::string Sha(const std::string& s) { return okay::account::detail::Sha256Hex(s); }

void Put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}
std::string Get(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss; ss << in.rdbuf();
    return ss.str();
}

// A manifest for `files` (path -> contents).
Manifest Make(const std::string& ver, const std::map<std::string, std::string>& files) {
    Manifest m;
    m.name = "Okay Sandbox"; m.version = ver; m.exe = "Okay Sandbox.exe";
    for (const auto& kv : files) {
        m.files.push_back({kv.first, kv.second.size(), Sha(kv.second)});
        m.size += kv.second.size();
    }
    return m;
}

// Stage every file of the plan from `files`, as the launcher's download step does.
void Stage(const fs::path& root, const Plan& plan, const std::map<std::string, std::string>& files) {
    for (const File& f : plan.download) Put(StagingDir(root) / f.path, files.at(f.path));
}
} // namespace

int main() {
    RUN_SUITE("official_game");

    // ---- Versions ----
    CHECK(LooksLikeVersion("1.0") && LooksLikeVersion("2026.10.24.1"));
    CHECK(!LooksLikeVersion("1") && !LooksLikeVersion("v1.0") && !LooksLikeVersion("1..2") &&
          !LooksLikeVersion(".1") && !LooksLikeVersion("1.") && !LooksLikeVersion("") && !LooksLikeVersion("1.0-beta"));
    CHECK(CompareVersions("1.10", "1.9") == 1);
    CHECK(CompareVersions("1.2", "1.2.0") == 0);
    CHECK(CompareVersions("2026.10.24.1", "2026.10.24.2") == -1);
    CHECK(CompareVersions("2026.10.25.1", "2026.10.24.9") == 1);

    // ---- Path safety ----
    CHECK(SafeRelPath("Game.exe") && SafeRelPath("Data/game.okayscene") && SafeRelPath("Data/My Level 2.okayscene"));
    CHECK(!SafeRelPath(""));
    CHECK(!SafeRelPath("/etc/passwd"));
    CHECK(!SafeRelPath("../evil.dll"));
    CHECK(!SafeRelPath("Data/../../evil.dll"));
    CHECK(!SafeRelPath("Data/./x"));
    CHECK(!SafeRelPath("Data//x"));
    CHECK(!SafeRelPath("Data/"));
    CHECK(!SafeRelPath("C:/Windows/evil.dll"));
    CHECK(!SafeRelPath("C:evil.dll"));
    CHECK(!SafeRelPath("Data\\..\\..\\evil.dll"));
    CHECK(!SafeRelPath("Game.exe:stream"));
    CHECK(!SafeRelPath("Data/CON") && !SafeRelPath("nul.txt") && !SafeRelPath("Data/com1.png"));
    CHECK(!SafeRelPath("Data. /x") && !SafeRelPath("trail."));
    CHECK(!SafeRelPath(".okayofficial/manifest.json") && !SafeRelPath(".OkayOfficial/x"));
    CHECK(SafeRelPath("Data/.okayofficial"));   // only the top-level folder is ours
    CHECK(!SafeRelPath(std::string("a\0b", 3)) && !SafeRelPath("a\nb"));
    {
        const fs::path root = fs::temp_directory_path() / "okay_official_paths";
        fs::remove_all(root); fs::create_directories(root / "Data");
        CHECK(!ResolveInside(root, "Data/x.png").empty());
        CHECK(ResolveInside(root, "../x").empty());
        CHECK(ResolveInside(root, "/x").empty());
        std::error_code ec;   // a link inside the folder that points out of it
        fs::create_directory_symlink(fs::temp_directory_path(), root / "out", ec);
        if (!ec) CHECK(ResolveInside(root, "out/x").empty());
        fs::remove_all(root);
    }
    CHECK(UrlEncodePath("Data/My Level #1.okayscene") == "Data/My%20Level%20%231.okayscene");

    // ---- Manifest parsing ----
    const std::string exeSha = Sha("MZexe"), dllSha = Sha("dll");
    const std::string good =
        "{\"name\": \"Okay Sandbox\", \"version\": \"2026.10.24.1\", \"exe\": \"Okay Sandbox.exe\",\n"
        " \"notes\": \"- New maps\\n- Faster \\u00e9\", \"image\": \"cover.png\", \"size\": 999,\n"
        " \"files\": [{\"path\": \"Okay Sandbox.exe\", \"size\": 5, \"sha256\": \"" + exeSha + "\"},\n"
        "           {\"path\": \"SDL2.dll\", \"size\": 3, \"sha256\": \"" + dllSha + "\"},\n"
        "           {\"path\": \"cover.png\", \"size\": 3, \"sha256\": \"" + dllSha + "\"}]}";
    Manifest m;
    std::string err;
    CHECK(ParseManifest(good, m, &err));
    CHECK(m.name == "Okay Sandbox" && m.version == "2026.10.24.1" && m.exe == "Okay Sandbox.exe");
    CHECK(m.files.size() == 3 && m.size == 11);              // the total is recomputed, not trusted
    CHECK(m.notes == "- New maps\n- Faster \xC3\xA9");
    CHECK(m.image == "cover.png");
    {   // round trip through the installed copy
        Manifest back;
        CHECK(ParseManifest(ToJson(m), back) && back.files.size() == 3 && back.notes == m.notes &&
              back.files[1].sha256 == dllSha && back.image == "cover.png");
    }
    auto bad = [&](const std::string& path, const std::string& extra = "") {
        const std::string t = "{\"name\":\"G\",\"version\":\"1.0\",\"exe\":\"G.exe\",\"files\":["
                              "{\"path\":\"G.exe\",\"size\":5,\"sha256\":\"" + exeSha + "\"},"
                              "{\"path\":\"" + path + "\",\"size\":1,\"sha256\":\"" + dllSha + "\"}" + extra + "]}";
        Manifest x;
        return !ParseManifest(t, x);
    };
    CHECK(!bad("Data/ok.png"));
    CHECK(bad("../evil.dll"));
    CHECK(bad("/abs.dll"));
    CHECK(bad("C:/abs.dll"));
    CHECK(bad("Data/../../x"));
    CHECK(bad("Data\\\\..\\\\x"));                         // an escaped backslash in the JSON
    CHECK(bad(".okayofficial/manifest.json"));
    CHECK(bad("g.EXE"));                                     // duplicate on Windows
    CHECK(bad("G.exe/inside"));                              // a file can't be a folder too
    Manifest x;
    CHECK(!ParseManifest("", x));
    CHECK(!ParseManifest("not json", x));
    CHECK(!ParseManifest("{\"name\":\"G\"", x));             // cut short
    CHECK(!ParseManifest("[1,2]", x));
    CHECK(!ParseManifest("{\"name\":\"G\",\"version\":\"1.0\",\"exe\":\"G.exe\",\"files\":[]}", x));
    CHECK(!ParseManifest("{\"name\":\"G\",\"version\":\"one\",\"exe\":\"G.exe\",\"files\":[{\"path\":\"G.exe\",\"size\":5,\"sha256\":\"" + exeSha + "\"}]}", x));
    CHECK(!ParseManifest("{\"name\":\"G\",\"version\":\"1.0\",\"exe\":\"H.exe\",\"files\":[{\"path\":\"G.exe\",\"size\":5,\"sha256\":\"" + exeSha + "\"}]}", x));
    CHECK(!ParseManifest("{\"name\":\"G\",\"version\":\"1.0\",\"exe\":\"G.exe\",\"files\":[{\"path\":\"G.exe\",\"size\":-5,\"sha256\":\"" + exeSha + "\"}]}", x));
    CHECK(!ParseManifest("{\"name\":\"G\",\"version\":\"1.0\",\"exe\":\"G.exe\",\"files\":[{\"path\":\"G.exe\",\"size\":5,\"sha256\":\"xyz\"}]}", x));
    CHECK(!ParseManifest(std::string(200, '[') + std::string(200, ']'), x));   // nesting bomb
    CHECK(!ParseManifest("{\"name\":\"G\",\"version\":\"1.0\",\"exe\":\"G.exe\",\"files\":[{\"path\":\"G.exe\",\"size\":5,\"sha256\":\"" + exeSha + "\"}]} trailing", x));
    {   // a cover that isn't one of the files (or isn't an image) is just dropped
        Manifest y;
        CHECK(ParseManifest("{\"name\":\"G\",\"version\":\"1.0\",\"exe\":\"G.exe\",\"image\":\"../x.png\",\"files\":[{\"path\":\"G.exe\",\"size\":5,\"sha256\":\"" + exeSha + "\"}]}", y));
        CHECK(y.image.empty());
    }

    // ---- Diff ----
    {
        const Manifest a = Make("1.0", {{"Okay Sandbox.exe", "MZ1"}, {"SDL2.dll", "dll"}, {"Data/old.png", "o"}});
        const Manifest b = Make("1.1", {{"Okay Sandbox.exe", "MZ2"}, {"SDL2.dll", "dll"}, {"Data/new.png", "n"}});
        Plan fresh = Diff(nullptr, b);
        CHECK(fresh.download.size() == 3 && fresh.remove.empty() && fresh.downloadBytes == 3 + 3 + 1);
        Plan up = Diff(&a, b);
        CHECK(up.download.size() == 2 && up.downloadBytes == 4);   // the exe + the new image; SDL2.dll is unchanged
        CHECK(up.remove.size() == 1 && up.remove[0] == "Data/old.png");
        CHECK(Diff(&b, b).empty());
        // A file the player deleted (or that's the wrong size) is fetched again.
        Plan heal = Diff(&b, b, [](const File& f) { return f.path != "SDL2.dll"; });
        CHECK(heal.download.size() == 1 && heal.download[0].path == "SDL2.dll" && heal.remove.empty());
        // A case-only rename is one file on Windows: update it, never delete it.
        const Manifest c = Make("1.2", {{"okay sandbox.exe", "MZ2"}, {"SDL2.dll", "dll"}, {"Data/new.png", "n"}});
        Plan ren = Diff(&b, c);
        CHECK(ren.download.size() == 1 && ren.remove.empty());
    }

    // ---- Apply / rollback / recover ----
    const fs::path root = fs::temp_directory_path() / "okay_official_test";
    fs::remove_all(root);
    const std::map<std::string, std::string> v1 = {{"Okay Sandbox.exe", "MZ1"}, {"SDL2.dll", "dll"}, {"Data/old.png", "o"}};
    const std::map<std::string, std::string> v2 = {{"Okay Sandbox.exe", "MZ2"}, {"SDL2.dll", "dll"}, {"Data/new.png", "n"}};
    const Manifest m1 = Make("1.0", v1), m2 = Make("1.1", v2);
    {   // fresh install
        Plan p = Diff(nullptr, m1);
        CHECK(!Apply(root, m1, p, &err));                    // nothing staged yet: refused, nothing touched
        CHECK(!fs::exists(ManifestPath(root)));
        Stage(root, p, v1);
        for (const File& f : p.download) CHECK(Verify(StagingDir(root) / f.path, f));
        CHECK(Apply(root, m1, p, &err));
        Manifest inst;
        CHECK(LoadInstalled(root, inst) && inst.version == "1.0");
        CHECK(Get(root / "Okay Sandbox.exe") == "MZ1" && Get(root / "Data/old.png") == "o");
        CHECK(!fs::exists(StagingDir(root)) && !fs::exists(MetaDir(root) / "apply.txt"));
    }
    CHECK(!Verify(root / "SDL2.dll", m1.files[0]));          // wrong file / hash
    {   // an update that fails part-way leaves 1.0 exactly as it was
        Manifest inst; LoadInstalled(root, inst);
        Plan p = Diff(&inst, m2);
        for (int failAt = 0; failAt <= (int)(p.download.size() + p.remove.size()); ++failAt) {
            Stage(root, p, v2);
            CHECK(!Apply(root, m2, p, &err, failAt));
            Manifest still;
            CHECK(LoadInstalled(root, still) && still.version == "1.0");
            CHECK(Get(root / "Okay Sandbox.exe") == "MZ1" && Get(root / "Data/old.png") == "o" && Get(root / "SDL2.dll") == "dll");
            CHECK(!fs::exists(root / "Data/new.png"));
            CHECK(!fs::exists(MetaDir(root) / "apply.txt") && !fs::exists(MetaDir(root) / "old"));
        }
        // ...and then succeeds.
        Stage(root, p, v2);
        CHECK(Apply(root, m2, p, &err));
        Manifest now;
        CHECK(LoadInstalled(root, now) && now.version == "1.1");
        CHECK(Get(root / "Okay Sandbox.exe") == "MZ2" && Get(root / "Data/new.png") == "n" && Get(root / "SDL2.dll") == "dll");
        CHECK(!fs::exists(root / "Data/old.png"));
    }
    {   // a crash mid-swap (journal + half-moved files) rolls back on the next start
        Manifest inst; LoadInstalled(root, inst);          // 1.1
        const std::map<std::string, std::string> v3 = {{"Okay Sandbox.exe", "MZ3"}, {"SDL2.dll", "dll"}, {"Data/new.png", "n"}};
        const Manifest m3 = Make("1.2", v3);
        Plan p = Diff(&inst, m3);
        Stage(root, p, v3);
        Put(MetaDir(root) / "apply.txt", "version 1.2\nput Okay Sandbox.exe\n");
        fs::create_directories(MetaDir(root) / "old");
        fs::rename(root / "Okay Sandbox.exe", MetaDir(root) / "old" / "Okay Sandbox.exe");   // first move done, then "power cut"
        Recover(root);
        Manifest after;
        CHECK(LoadInstalled(root, after) && after.version == "1.1");
        CHECK(Get(root / "Okay Sandbox.exe") == "MZ2");
        CHECK(!fs::exists(MetaDir(root) / "apply.txt") && !fs::exists(MetaDir(root) / "old"));
        // A crash after every file moved but before the manifest's rename finishes it.
        Put(MetaDir(root) / "apply.txt", "version 1.2\nput Okay Sandbox.exe\n");
        fs::remove(root / "Okay Sandbox.exe");
        fs::rename(StagingDir(root) / "Okay Sandbox.exe", root / "Okay Sandbox.exe");
        Put(ManifestPath(root).string() + ".new", ToJson(m3));
        Recover(root);
        CHECK(LoadInstalled(root, after) && after.version == "1.2" && Get(root / "Okay Sandbox.exe") == "MZ3");
        CHECK(!fs::exists(ManifestPath(root).string() + ".new"));
    }
    fs::remove_all(root);

    TEST_MAIN_RESULT();
}
