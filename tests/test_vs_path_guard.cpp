// Visual-script nodes that read or write a file keep to the same rule as the
// OkayScript calls (test_script_path_guard_rest.cpp): the file name a graph
// gives must stay inside the game's own folders. No absolute path, no drive
// letter, no "..". Issue #888, finding 51 (visual-script follow-up).
//
// A temp folder holds game/ (the working directory, as when a game runs) and
// outside/ with real files of each kind. Every node is tried with
//   C:\x.<ext> (a real file of that name sits in the game folder: on Linux it is
//   a valid file name, so only the guard stops it), ../outside/evil.<ext> and
//   the absolute path of that same file,
// and must load nothing and log a "refusing" line naming the path. A relative
// file inside the game must still load.
//
// Nodes: PoolGet and SpawnEvery (both spawn through GpSpawn), SpawnProjectile,
// BtRun, LoadScene (already guarded: kept as a check), SaveCreation and
// LoadCreation.
#include "test_framework.hpp"
#include <Okay.hpp>

#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <direct.h>
#  define chdir _chdir
#  define getpid _getpid
#  include <process.h>
#else
#  include <unistd.h>
#endif

using namespace okay;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> g_log;

bool LoggedRefusal(const std::string& path) {
    for (const auto& l : g_log)
        if (l.find("refusing") != std::string::npos && l.find(path) != std::string::npos) return true;
    return false;
}

bool AnyRefusal() {
    for (const auto& l : g_log)
        if (l.find("refusing") != std::string::npos) return true;
    return false;
}

int CountNamed(Scene& s, const std::string& prefix) {
    int n = 0;
    for (auto& up : s.Objects())
        if (up && up->name.compare(0, prefix.size(), prefix) == 0) ++n;
    return n;
}

void MakePrefab(const fs::path& file, const std::string& name) {
    Scene tmp("p");
    GameObject* b = tmp.CreateGameObject(name);
    b->AddComponent<SpriteRenderer>()->color = Color::Red;
    CHECK(SceneSerializer::SaveObjectToFile(*b, file.string()));
}

void MakeScene(const fs::path& file, const std::string& name) {
    Scene tmp("s");
    tmp.CreateGameObject(name);
    CHECK(SceneSerializer::SaveToFile(tmp, file.string()));
}

void MakeTree(const fs::path& file) {
    BehaviorTree t;
    t.name = "Guard";
    const int seq = t.AddNode(BtNodeType::Sequence);
    t.AddNode(BtNodeType::Wait, seq);
    std::string err;
    CHECK(t.SaveToFile(file.string(), &err));
}

void MakeCreation(const fs::path& file) {
    Scene tmp("c");
    std::string err;
    CHECK(BuildCreation::Save(tmp, file.string(), &err));
}

// Runs a one-node graph (OnStart -> `type path`) on an object, for a few frames.
// Returns the scene's state through `probe`.
using Probe = std::function<int(Scene&, GameObject* self)>;
int RunNode(const std::string& type, const std::string& path, const Probe& probe, float dt = 0.016f) {
    Scene scene("V");
    scene.physicsEnabled = false;
    GameObject* go = scene.CreateGameObject("Spawner");
    auto* v = go->AddComponent<VisualScriptComponent>();
    std::string err;
    const std::string entry = type == "SpawnEvery" ? "OnUpdate" : "OnStart";
    const bool ok = v->LoadFromText("node 0 " + entry + "\nnode 1 " + type + " " + path + "\nexec 0 0 1\nentry " + entry +
                                        " 0\n",
                                    &err);
    CHECK(ok);
    if (!ok) std::printf("    parse error (%s): %s\n", type.c_str(), err.c_str());
    scene.Start();
    for (int i = 0; i < 3; ++i) scene.Update(dt);
    return probe(scene, go);
}

}  // namespace

int main() {
    RUN_SUITE("vs_path_guard");
    Log::sink = [](Log::Level, const std::string& m) { g_log.push_back(m); };

    const fs::path root = fs::temp_directory_path() / ("okay_vs_path_guard_" + std::to_string((long)getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);
    for (const char* d : {"game/prefabs", "game/ai", "game/levels", "game/mine", "outside"}) fs::create_directories(root / d, ec);

    MakePrefab(root / "outside" / "evil.okayprefab", "EvilOutside");
    MakePrefab(root / "game" / "prefabs" / "good.okayprefab", "Good");
    MakeTree(root / "outside" / "evil.okaybt");
    MakeTree(root / "game" / "ai" / "good.okaybt");
    MakeScene(root / "outside" / "evil.okayscene", "EvilScene");
    MakeScene(root / "game" / "levels" / "good.okayscene", "GoodScene");
    MakeCreation(root / "outside" / "evil.okaycreation");
    MakeCreation(root / "game" / "good.okaycreation");
#if !defined(_WIN32)
    MakePrefab(root / "game" / "C:\\x.okayprefab", "EvilDrive");   // plain file names on Linux
    MakeTree(root / "game" / "C:\\x.okaybt");
    MakeScene(root / "game" / "C:\\x.okayscene", "EvilDriveScene");
    MakeCreation(root / "game" / "C:\\x.okaycreation");
#endif
    const fs::path oldCwd = fs::current_path();
    CHECK(chdir((root / "game").string().c_str()) == 0);

    auto badPaths = [&](const std::string& ext) {
        return std::vector<std::string>{
            "C:\\x." + ext,
            "../outside/evil." + ext,
            (root / "outside" / ("evil." + ext)).string(),
        };
    };

    const Probe countEvil = [](Scene& s, GameObject*) { return CountNamed(s, "Evil"); };
    const Probe countGood = [](Scene& s, GameObject*) { return CountNamed(s, "Good"); };
    const Probe treeLoaded = [](Scene&, GameObject* self) {
        auto* r = self->GetComponent<BehaviorTreeRunner>();
        return (r && r->Tree()) ? 1 : 0;
    };
    // The scene load is queued (RequestLoad) and applied by a later Update: either
    // still pending or already replaced the scene with the file's objects.
    const Probe sceneEvil = [](Scene& s, GameObject*) { return (s.HasPendingLoad() ? 1 : 0) + CountNamed(s, "Evil"); };
    const Probe sceneGood = [](Scene& s, GameObject*) { return (s.HasPendingLoad() ? 1 : 0) + CountNamed(s, "Good"); };
    // Load Creation: a refused call never reaches the file, so only the log tells.
    const Probe none = [](Scene&, GameObject*) { return 0; };
    const Probe ranOk = [](Scene&, GameObject*) { return AnyRefusal() ? 0 : 1; };

    struct Case {
        const char* type;
        const char* ext;
        Probe badProbe;
        std::string goodPath;
        Probe goodProbe;
        float dt;
    };
    const std::vector<Case> cases = {
        {"PoolGet", "okayprefab", countEvil, "prefabs/good.okayprefab", countGood, 0.016f},
        {"SpawnEvery", "okayprefab", countEvil, "prefabs/good.okayprefab", countGood, 3.0f},
        {"SpawnProjectile", "okayprefab", countEvil, "prefabs/good.okayprefab", countGood, 0.016f},
        {"BtRun", "okaybt", treeLoaded, "ai/good.okaybt", treeLoaded, 0.016f},
        {"LoadScene", "okayscene", sceneEvil, "levels/good.okayscene", sceneGood, 0.016f},
        {"LoadCreation", "okaycreation", none, "good.okaycreation", ranOk, 0.016f},
    };

    int refused = 0, total = 0, goodOk = 0, goodTotal = 0;
    auto report = [&](const std::string& where, const std::string& p, int loaded) {
        ++total;
        const bool logged = LoggedRefusal(p);
        const bool ok = loaded == 0 && logged;
        if (ok) ++refused;
        else std::printf("    NOT REFUSED: %s('%s') loaded %d, refusal logged: %s\n", where.c_str(), p.c_str(), loaded,
                         logged ? "yes" : "no");
        CHECK(loaded == 0);
        CHECK(logged);
    };
    auto good = [&](const std::string& where, int loaded) {
        ++goodTotal;
        const bool refusedLog = AnyRefusal();
        CHECK(loaded >= 1);
        CHECK(!refusedLog);
        if (loaded >= 1 && !refusedLog) ++goodOk;
        else std::printf("    INSIDE PATH FAILED: %s loaded %d, refusal logged: %s\n", where.c_str(), loaded,
                         refusedLog ? "yes" : "no");
    };

    for (const auto& c : cases) {
        for (const auto& p : badPaths(c.ext)) {
            g_log.clear();
            report(c.type, p, RunNode(c.type, p, c.badProbe, c.dt));
        }
        g_log.clear();
        good(c.type, RunNode(c.type, c.goodPath, c.goodProbe, c.dt));
    }

    // Save Creation writes a file: an outside name must write nothing.
    {
        const std::vector<std::pair<std::string, fs::path>> bad = {
            {"C:\\saved", root / "game" / "C:\\saved.okaycreation"},
            {"../outside/saved", root / "outside" / "saved.okaycreation"},
            {(root / "outside" / "saved2").string(), root / "outside" / "saved2.okaycreation"},
        };
        for (const auto& [name, file] : bad) {
            g_log.clear();
            report("SaveCreation", name, RunNode("SaveCreation", name, [&](Scene&, GameObject*) {
                       return fs::exists(file, ec) ? 1 : 0;
                   }));
        }
        g_log.clear();
        good("SaveCreation", RunNode("SaveCreation", "mine/saved", [&](Scene&, GameObject*) {
                 return fs::exists(root / "game" / "mine" / "saved.okaycreation", ec) ? 1 : 0;
             }));
    }

    std::printf("    refused %d/%d outside paths (with a log line); loaded %d/%d inside paths\n", refused, total, goodOk,
                goodTotal);

    CHECK(chdir(oldCwd.string().c_str()) == 0);
    fs::remove_all(root, ec);
    Log::sink = nullptr;
    TEST_MAIN_RESULT();
}
