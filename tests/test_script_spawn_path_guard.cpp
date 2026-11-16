// Script-supplied prefab paths must stay inside the game's own folders, like
// every other script file name (ScriptPathOk / Save::IsSafeRelative): no
// absolute path, no drive letter, no "..". Issue #888, finding 51.
//
// A temp folder holds game/ (the working directory, as when a game runs) and
// outside/ with a real prefab. Every spawn entry point is tried with
//   /etc/x.okayprefab, C:\x.okayprefab (a real file of that name sits in the
//   game folder: on Linux it is a valid file name, so only the guard stops it),
//   ../outside/evil.okayprefab and the absolute path of that same file,
// and must create nothing and log a "refusing" line naming the path. A relative
// prefab inside the game (prefabs/good.okayprefab) must still spawn.
//
// Entry points: OkayScript spawn, spawn3, shoot_at, spawn_wave; ActionList ops
// spawn, spawn3, spawn_at, spawn_wave; the Visual Script Spawn node.
#include "test_framework.hpp"
#include <Okay.hpp>

#include <cstdio>
#include <filesystem>
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

// OkayScript string literal: escape backslashes and quotes.
std::string Lit(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '\\' || c == '"') o += '\\'; o += c; }
    return o + "\"";
}

// Runs one OkayScript call (the {P} in `call` is the prefab path) once from
// update(); returns how many objects named Evil* / Good exist afterwards.
int RunScript(const std::string& call, const std::string& path, const std::string& countPrefix) {
    Scene scene("S");
    scene.physicsEnabled = false;
    GameObject* target = scene.CreateGameObject("Target");
    target->transform->localPosition = {5, 0, 0};
    GameObject* go = scene.CreateGameObject("Gun");
    auto* sc = go->AddComponent<ScriptComponent>("okayscript");
    std::string c = call;
    c.replace(c.find("{P}"), 3, Lit(path));
    const std::string src = "var done = 0;\nfunction update(d) {\n  if (done == 0) { " + c + "; done = 1; }\n}\n";
    std::string err;
    CHECK(sc->LoadSource(src, &err));
    scene.Start();
    scene.Update(0.016f);
    scene.Update(0.016f);
    return CountNamed(scene, countPrefix);
}

int RunActions(const std::string& op, std::vector<std::string> args, const std::string& countPrefix) {
    ActionList::ResetVars();
    Scene scene("A");
    scene.physicsEnabled = false;
    scene.CreateGameObject("Target");
    GameObject* o = scene.CreateGameObject("Hero");
    auto* al = o->AddComponent<ActionList>();
    al->trigger = ActionList::Trigger::OnStart;
    al->instructions = {ActionList::Item{op, std::move(args)}};
    scene.Start();
    scene.Update(0.016f);
    scene.Update(0.016f);
    return CountNamed(scene, countPrefix);
}

int RunVs(const std::string& path, const std::string& countPrefix) {
    Scene scene("V");
    scene.physicsEnabled = false;
    GameObject* go = scene.CreateGameObject("Spawner");
    auto* v = go->AddComponent<VisualScriptComponent>();
    std::string err;
    const bool ok = v->LoadFromText("node 0 OnStart\nnode 1 Spawn " + path + "\nexec 0 0 1\nentry OnStart 0\n", &err);
    CHECK(ok);
    if (!ok) std::printf("    parse error: %s\n", err.c_str());
    scene.Start();
    scene.Update(0.016f);
    scene.Update(0.016f);
    return CountNamed(scene, countPrefix);
}

}  // namespace

int main() {
    RUN_SUITE("script_spawn_path_guard");
    Log::sink = [](Log::Level, const std::string& m) { g_log.push_back(m); };

    const fs::path root = fs::temp_directory_path() / ("okay_spawn_guard_" + std::to_string((long)getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "game" / "prefabs", ec);
    fs::create_directories(root / "outside", ec);
    MakePrefab(root / "outside" / "evil.okayprefab", "EvilOutside");
    MakePrefab(root / "game" / "prefabs" / "good.okayprefab", "Good");
#if !defined(_WIN32)
    MakePrefab(root / "game" / "C:\\x.okayprefab", "EvilDrive");   // a plain file name on Linux
#endif
    const fs::path oldCwd = fs::current_path();
    CHECK(chdir((root / "game").string().c_str()) == 0);

    const std::vector<std::string> bad = {
        "/etc/x.okayprefab",
        "C:\\x.okayprefab",
        "../outside/evil.okayprefab",
        (root / "outside" / "evil.okayprefab").string(),
    };
    const std::string good = "prefabs/good.okayprefab";

    struct ScriptCase { const char* name; const char* call; };
    const std::vector<ScriptCase> scripts = {
        {"spawn", "spawn({P}, 1, 2)"},
        {"spawn3", "spawn3({P}, 1, 2, 3)"},
        {"shoot_at", "shoot_at(\"Target\", {P}, 4)"},
        {"spawn_wave", "spawn_wave({P}, 2, 1)"},
    };
    struct ActionCase { const char* op; std::vector<std::string> rest; };
    const std::vector<ActionCase> actions = {
        {"spawn", {"1", "2", "0"}},
        {"spawn3", {"1", "2", "3"}},
        {"spawn_at", {"Target"}},
        {"spawn_wave", {"2", "1"}},
    };

    int refused = 0, total = 0, goodOk = 0, goodTotal = 0;
    auto report = [&](const std::string& where, const std::string& p, int made) {
        ++total;
        const bool logged = LoggedRefusal(p);
        const bool ok = made == 0 && logged;
        if (ok) ++refused;
        else std::printf("    NOT REFUSED: %s('%s') made %d object(s), refusal logged: %s\n", where.c_str(), p.c_str(),
                         made, logged ? "yes" : "no");
        CHECK(made == 0);
        CHECK(logged);
    };

    for (const auto& c : scripts) {
        for (const auto& p : bad) {
            g_log.clear();
            report(std::string("script ") + c.name, p, RunScript(c.call, p, "Evil"));
        }
        ++goodTotal;
        const int n = RunScript(c.call, good, "Good");
        CHECK(n >= 1);
        if (n >= 1) ++goodOk;
    }
    for (const auto& c : actions) {
        for (const auto& p : bad) {
            g_log.clear();
            std::vector<std::string> args{p};
            args.insert(args.end(), c.rest.begin(), c.rest.end());
            report(std::string("actions ") + c.op, p, RunActions(c.op, args, "Evil"));
        }
        ++goodTotal;
        std::vector<std::string> args{good};
        args.insert(args.end(), c.rest.begin(), c.rest.end());
        const int n = RunActions(c.op, args, "Good");
        CHECK(n >= 1);
        if (n >= 1) ++goodOk;
    }
    for (const auto& p : bad) {
        g_log.clear();
        report("visual script Spawn", p, RunVs(p, "Evil"));
    }
    ++goodTotal;
    {
        const int n = RunVs(good, "Good");
        CHECK(n >= 1);
        if (n >= 1) ++goodOk;
        const int n2 = RunVs("@prefab:" + good, "Good");   // an asset reference still works
        CHECK(n2 >= 1);
    }

    std::printf("    refused %d/%d outside paths (with a log line); spawned %d/%d inside paths\n", refused, total, goodOk,
                goodTotal);

    CHECK(chdir(oldCwd.string().c_str()) == 0);
    fs::remove_all(root, ec);
    Log::sink = nullptr;
    TEST_MAIN_RESULT();
}
