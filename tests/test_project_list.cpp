#include "test_framework.hpp"
#include "ProjectList.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

// "Remove from list" / "Delete project" on the launcher's Create page, on real temp folders.
// Windows-only behaviour (case folding, the Recycle Bin) can't run here: the key function is
// tested on strings with foldCase=true instead.
namespace fs = std::filesystem;
using namespace okayproj;

static void mk(const fs::path& root, const std::string& name) {
    fs::create_directories(root / "Assets");
    std::ofstream(root / "Assets" / (name + ".okayscene")) << "name \"" << name << "\"\n";
}
static bool listed(const std::vector<ListedProject>& l, const std::string& name) {
    for (auto& p : l) if (p.name == name) return true;
    return false;
}
// What the launcher did BEFORE this change (kept only to show the bug the tests found).
static std::vector<std::string> legacyScan(const fs::path& dir, const std::vector<std::string>& hidden, const std::vector<std::string>& extras) {
    std::vector<std::string> out;
    for (auto& e : fs::directory_iterator(dir)) {
        if (!e.is_directory()) continue;
        if (std::find(hidden.begin(), hidden.end(), e.path().lexically_normal().string()) != hidden.end()) continue;
        out.push_back(SceneIn(e.path()));
    }
    for (auto& ep : extras) { bool dup = false; for (auto& s : out) if (s == ep) dup = true; if (!dup) out.push_back(ep); }
    return out;
}

int main() {
    RUN_SUITE("project_list");
    const fs::path base = fs::temp_directory_path() / "okay_project_list_test";
    fs::remove_all(base);
    const fs::path projects = base / "Projects", other = base / "elsewhere";
    mk(projects / "Alpha", "Alpha");
    mk(projects / "Beta", "Beta");
    mk(other / "Gamma", "Gamma");
    const std::string alphaScene = (projects / "Alpha" / "Assets" / "Alpha.okayscene").string();
    const std::string gammaScene = (other / "Gamma" / "Assets" / "Gamma.okayscene").string();

    // --- scan basics
    std::vector<std::string> hidden, extras;
    auto l = ScanProjects(projects, hidden, extras);
    CHECK(l.size() == 2 && listed(l, "Alpha") && listed(l, "Beta"));
    extras = {gammaScene};
    l = ScanProjects(projects, hidden, extras);
    CHECK(l.size() == 3 && listed(l, "Gamma"));

    // --- Remove from list: folder project, then a prefs round trip
    CHECK(ForgetProject(projects, alphaScene, hidden, extras) == ForgetResult::Removed);
    l = ScanProjects(projects, hidden, extras);
    CHECK(!listed(l, "Alpha") && listed(l, "Beta"));
    CHECK(fs::exists(alphaScene));   // files stay
    {
        std::stringstream cfg; WritePrefLines(cfg, extras, hidden);
        std::vector<std::string> e2, h2; std::string line;
        while (std::getline(cfg, line)) { auto eq = line.find('='); CHECK(ApplyPrefLine(line.substr(0, eq), line.substr(eq + 1), e2, h2)); }
        l = ScanProjects(projects, h2, e2);
        CHECK(!listed(l, "Alpha") && listed(l, "Beta") && listed(l, "Gamma"));   // still gone after a restart
    }

    // --- BUG 1: a project in the Projects folder AND in the extras came straight back.
    {
        std::vector<std::string> h, e = {alphaScene};
        CHECK(listed(ScanProjects(projects, h, e), "Alpha"));
        h.clear();
        CHECK(ForgetProject(projects, alphaScene, h, e) == ForgetResult::Removed);
        CHECK(e.empty() && !listed(ScanProjects(projects, h, e), "Alpha"));
        // and even if an old prefs file still has the extra, a hidden project stays hidden
        std::vector<std::string> e3 = {alphaScene};
        CHECK(!listed(ScanProjects(projects, h, e3), "Alpha"));
    }
    {   // prove the old logic really listed it again
        std::vector<std::string> h = {RootOfScene(fs::path(alphaScene)).lexically_normal().string()}, e = {alphaScene};
        int n = 0; for (auto& s : legacyScan(projects, h, e)) if (s == alphaScene) ++n;
        CHECK(n == 1);   // old code: still listed (this is the bug; the new ScanProjects lists it 0 times)
        CHECK(!listed(ScanProjects(projects, h, e), "Alpha"));
    }

    // --- Extra project outside the folder
    {
        std::vector<std::string> h, e = {gammaScene};
        CHECK(ForgetProject(projects, gammaScene, h, e) == ForgetResult::Removed);
        CHECK(e.empty() && h.empty() && !listed(ScanProjects(projects, h, e), "Gamma"));
        CHECK(fs::exists(gammaScene));
    }

    // --- BUG 2: path-form mismatches between the stored key and the scan key
    {
        std::vector<std::string> h;
        h.push_back((projects / "Beta").string() + "/");                       // trailing slash
        CHECK(!listed(ScanProjects(projects, h, {}), "Beta"));
        h = {KeyOf((projects / "Beta").string())};
        CHECK(!listed(ScanProjects(fs::path(projects.string() + "/"), h, {}), "Beta"));   // Projects folder typed with a trailing slash
        h = {(projects / "Beta").string() + "/./"};
        CHECK(!listed(ScanProjects(projects, h, {}), "Beta"));
        // Forget with the Projects folder typed differently from the scene's form
        h.clear(); std::vector<std::string> e;
        const std::string betaScene = (projects / "Beta" / "Assets" / "Beta.okayscene").string();
        CHECK(ForgetProject(fs::path(projects.string() + "/"), betaScene, h, e) == ForgetResult::Removed);
        CHECK(!listed(ScanProjects(projects, h, e), "Beta"));
        CHECK(Unhide(h, RootKey(betaScene)) && listed(ScanProjects(projects, h, e), "Beta"));
    }
    // Key function on strings (Windows forms, run here with foldCase on/off)
    CHECK(KeyOf("C:\\Proj\\Game", true) == KeyOf("c:/proj/GAME/", true));
    CHECK(KeyOf("C:\\Proj\\\\Game\\.\\", false) == KeyOf("C:/Proj/Game", false));
    CHECK(KeyOf("C:\\Proj\\Game", false) != KeyOf("c:/proj/game", false));   // Linux: case matters
    CHECK(KeyOf("/home/a//b/") == "/home/a/b");
    CHECK(RootKey("C:\\P\\Game\\Assets\\Game.okayscene", true) == RootKey("c:/p/game/Assets/x.okayscene", true));

    // --- BUG 3: the 100-entry hidden cap silently dropped the oldest, so a hidden project came back
    {
        std::vector<std::string> h;
        for (int i = 0; i < 150; ++i) CHECK(Hide(h, "/p/proj" + std::to_string(i)));
        CHECK(h.size() == 150 && IsHidden(h, "/p/proj0"));
        std::stringstream cfg; WritePrefLines(cfg, {}, h);
        std::vector<std::string> e2, h2; std::string line;
        while (std::getline(cfg, line)) { auto eq = line.find('='); ApplyPrefLine(line.substr(0, eq), line.substr(eq + 1), e2, h2); }
        CHECK(h2.size() == 150 && IsHidden(h2, "/p/proj0"));   // old load cap: 100, oldest lost
        std::vector<std::string> full;
        for (size_t i = 0; i < kMaxHiddenProjects; ++i) full.push_back("/x" + std::to_string(i));
        CHECK(!Hide(full, "/new") && full.size() == kMaxHiddenProjects);   // full is reported, nothing dropped
    }

    // --- Open again un-hides; AddExtra dedupes by root
    {
        std::vector<std::string> h = {RootKey(alphaScene)}, e;
        CHECK(Unhide(h, RootKey(alphaScene)) && h.empty());
        CHECK(!Unhide(h, RootKey(alphaScene)));
        CHECK(AddExtra(e, gammaScene) && AddExtra(e, gammaScene) && e.size() == 1);
    }

    // --- Delete from disk: refusals and the cleanup after
    {
        const std::vector<Kept> keep = {{projects, "your projects folder"}, {base / "launcher", "the launcher"}};
        CHECK(DeleteRefusal(other / "Gamma", keep).empty());
        CHECK(DeleteRefusal(projects, keep).find("projects folder") != std::string::npos);   // shown in the popup as "Can't delete it from disk. ..."
        CHECK(!DeleteRefusal(projects / "Nope", keep).empty());
        std::vector<std::string> h, e = {gammaScene};
        const fs::path root = RootOfScene(fs::path(gammaScene));
        CHECK(DeleteRefusal(root, keep).empty());
        std::error_code ec; fs::remove_all(root, ec);
        CHECK(!ec && !fs::exists(root));
        // BUG 4: the extras entry was erased only on an exact string match; a differently written
        // path left a "missing" ghost row after the delete.
        e = {other.string() + "/Gamma/Assets/Gamma.okayscene"};
        h = {RootKey(gammaScene)};
        ForgetDeleted(gammaScene, h, e);
        CHECK(e.empty() && h.empty());
        CHECK(!listed(ScanProjects(projects, h, e), "Gamma"));
    }
    // Delete of a folder project that is also an extra: no ghost "missing" row remains
    {
        std::vector<std::string> h, e = {alphaScene};
        fs::remove_all(projects / "Alpha");
        ForgetDeleted(alphaScene, h, e);
        CHECK(!listed(ScanProjects(projects, h, e), "Alpha"));
    }
    fs::remove_all(base);
    TEST_MAIN_RESULT();
}
