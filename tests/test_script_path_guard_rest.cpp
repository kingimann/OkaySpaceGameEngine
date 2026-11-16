// The rest of the script-reachable file loaders keep to the same rule as spawn
// (test_script_spawn_path_guard.cpp): a file name a script, action list or
// visual-script graph passes must stay inside the game's own folders. No
// absolute path, no drive letter, no "..". Issue #888, finding 51 (follow-up).
//
// A temp folder holds game/ (the working directory, as when a game runs) and
// outside/ with real files of each kind. Every entry point is tried with
//   /etc/x.<ext>, C:\x.<ext> (a real file of that name sits in the game folder:
//   on Linux it is a valid file name, so only the guard stops it),
//   ../outside/evil.<ext> and the absolute path of that same file,
// and must load nothing and log a "refusing" line naming the path. A relative
// file inside the game must still load.
//
// Entry points: OkayScript pool_get, pool_prewarm, pool_idle, ui_load_theme,
// bt_run, net_spawn_owned; the ActionList op net_spawn_owned; the Visual Script
// NetSpawnOwned node; music_play (the Music Player's track), set_texture,
// set_image and set_mesh_texture (a texture file) and the ActionList op
// set_sprite. For those the "file" is a name the component keeps and loads
// later, so "loaded" means the name was taken.
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

void MakeTheme(const fs::path& file) {
    Scene tmp("t");
    auto* th = tmp.CreateGameObject("T")->AddComponent<UITheme>();
    th->accent = Color::FromBytes(1, 2, 3);
    CHECK(SaveUIThemeAsset(*th, file.string()));
}

void MakeTree(const fs::path& file) {
    BehaviorTree t;
    t.name = "Guard";
    const int seq = t.AddNode(BtNodeType::Sequence);
    t.AddNode(BtNodeType::Wait, seq);
    std::string err;
    CHECK(t.SaveToFile(file.string(), &err));
}

// OkayScript string literal: escape backslashes and quotes.
std::string Lit(const std::string& s) {
    std::string o = "\"";
    for (char c : s) { if (c == '\\' || c == '"') o += '\\'; o += c; }
    return o + "\"";
}

using Setup = std::function<void(Scene&, GameObject* self)>;
// Returns how much was loaded (objects made, theme applied, tree loaded): 0 = nothing.
using Probe = std::function<int(Scene&, GameObject* self, IScriptVM* vm)>;

// Runs one OkayScript statement (every {P} in `call` is the file path) once from update().
int RunScript(const std::string& call, const std::string& path, const Setup& setup, const Probe& probe) {
    Scene scene("S");
    scene.physicsEnabled = false;
    GameObject* go = scene.CreateGameObject("Self");
    if (setup) setup(scene, go);
    auto* sc = go->AddComponent<ScriptComponent>("okayscript");
    std::string c = call;
    for (std::size_t p; (p = c.find("{P}")) != std::string::npos;) c.replace(p, 3, Lit(path));
    const std::string src = "var done = 0;\nvar r = 0;\nfunction update(d) {\n  if (done == 0) { " + c +
                            "; done = 1; }\n}\n";
    std::string err;
    CHECK(sc->LoadSource(src, &err));
    if (!err.empty()) std::printf("    script error: %s\n", err.c_str());
    scene.Start();
    scene.Update(0.016f);
    scene.Update(0.016f);
    return probe(scene, go, sc->VM());
}

void AddNet(Scene& s, GameObject*) { s.CreateGameObject("__Network")->AddComponent<NetworkManager>(); }

int RunActions(const std::string& op, std::vector<std::string> args, const std::string& countPrefix) {
    ActionList::ResetVars();
    Scene scene("A");
    scene.physicsEnabled = false;
    AddNet(scene, nullptr);
    GameObject* o = scene.CreateGameObject("Hero");
    auto* al = o->AddComponent<ActionList>();
    al->trigger = ActionList::Trigger::OnStart;
    al->instructions = {ActionList::Item{op, std::move(args)}};
    scene.Start();
    scene.Update(0.016f);
    scene.Update(0.016f);
    return CountNamed(scene, countPrefix);
}

int RunVsNetSpawn(const std::string& path, const std::string& countPrefix) {
    Scene scene("V");
    scene.physicsEnabled = false;
    AddNet(scene, nullptr);
    GameObject* go = scene.CreateGameObject("Spawner");
    auto* v = go->AddComponent<VisualScriptComponent>();
    std::string err;
    const bool ok = v->LoadFromText("node 0 OnStart\nnode 1 NetSpawnOwned " + path + "\nexec 0 0 1\nentry OnStart 0\n", &err);
    CHECK(ok);
    if (!ok) std::printf("    parse error: %s\n", err.c_str());
    scene.Start();
    scene.Update(0.016f);
    scene.Update(0.016f);
    return CountNamed(scene, countPrefix);
}

}  // namespace

int main() {
    RUN_SUITE("script_path_guard_rest");
    Log::sink = [](Log::Level, const std::string& m) { g_log.push_back(m); };

    const fs::path root = fs::temp_directory_path() / ("okay_path_guard_rest_" + std::to_string((long)getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);
    for (const char* d : {"game/prefabs", "game/themes", "game/ai", "outside"}) fs::create_directories(root / d, ec);

    MakePrefab(root / "outside" / "evil.okayprefab", "EvilOutside");
    MakePrefab(root / "game" / "prefabs" / "good.okayprefab", "Good");
    MakeTheme(root / "outside" / "evil.okaytheme");
    MakeTheme(root / "game" / "themes" / "good.okaytheme");
    MakeTree(root / "outside" / "evil.okaybt");
    MakeTree(root / "game" / "ai" / "good.okaybt");
#if !defined(_WIN32)
    MakePrefab(root / "game" / "C:\\x.okayprefab", "EvilDrive");   // plain file names on Linux
    MakeTheme(root / "game" / "C:\\x.okaytheme");
    MakeTree(root / "game" / "C:\\x.okaybt");
#endif
    const fs::path oldCwd = fs::current_path();
    CHECK(chdir((root / "game").string().c_str()) == 0);

    auto badPaths = [&](const std::string& ext) {
        return std::vector<std::string>{
            "/etc/x." + ext,
            "C:\\x." + ext,
            "../outside/evil." + ext,
            (root / "outside" / ("evil." + ext)).string(),
        };
    };

    const Probe countEvil = [](Scene& s, GameObject*, IScriptVM*) { return CountNamed(s, "Evil"); };
    const Probe countGood = [](Scene& s, GameObject*, IScriptVM*) { return CountNamed(s, "Good"); };
    const Probe resultR = [](Scene&, GameObject*, IScriptVM* vm) {
        return vm ? (int)vm->GetGlobal("r").AsFloat() : 0;
    };
    const Probe treeLoaded = [](Scene&, GameObject* self, IScriptVM*) {
        auto* r = self->GetComponent<BehaviorTreeRunner>();
        return (r && r->Tree()) ? 1 : 0;
    };
    const Setup addTheme = [](Scene& s, GameObject*) { s.CreateGameObject("Theme")->AddComponent<UITheme>(); };
    const Setup addMusic = [](Scene& s, GameObject*) { s.CreateGameObject("Music")->AddComponent<MusicPlayer>(); };
    const Probe musicTaken = [](Scene& s, GameObject*, IScriptVM*) {
        auto all = s.FindObjectsOfType<MusicPlayer>();
        return all.empty() ? 0 : (int)all.front()->tracks.size();
    };
    const Setup addSprite = [](Scene&, GameObject* g) { g->AddComponent<SpriteRenderer>(); };
    const Setup addImage = [](Scene&, GameObject* g) { g->AddComponent<UIImage>(); };
    const Setup addMesh = [](Scene&, GameObject* g) { g->AddComponent<MeshRenderer>(); };
    const Probe spriteTex = [](Scene&, GameObject* g, IScriptVM*) {
        auto* c = g->GetComponent<SpriteRenderer>(); return (c && !c->texture.empty()) ? 1 : 0; };
    const Probe imageTex = [](Scene&, GameObject* g, IScriptVM*) {
        auto* c = g->GetComponent<UIImage>(); return (c && !c->texture.empty()) ? 1 : 0; };
    const Probe meshTex = [](Scene&, GameObject* g, IScriptVM*) {
        auto* c = g->GetComponent<MeshRenderer>(); return (c && !c->texture.empty()) ? 1 : 0; };

    struct ScriptCase {
        const char* name;
        const char* ext;
        const char* badCall;    // tried with each outside path
        Probe badProbe;
        const char* goodCall;   // tried with the inside path
        Probe goodProbe;
        Setup setup;
    };
    const std::vector<ScriptCase> scripts = {
        {"pool_get", "okayprefab", "r = pool_get({P}, 1, 2)", countEvil, "r = pool_get({P}, 1, 2)", countGood, nullptr},
        {"pool_prewarm", "okayprefab", "r = pool_prewarm({P}, 2)", countEvil, "r = pool_prewarm({P}, 2)", countGood, nullptr},
        {"pool_idle", "okayprefab", "r = pool_idle({P})", countEvil, "pool_prewarm({P}, 2); r = pool_idle({P})", resultR,
         nullptr},
        {"ui_load_theme", "okaytheme", "r = ui_load_theme({P})", resultR, "r = ui_load_theme({P})", resultR, addTheme},
        {"bt_run", "okaybt", "r = bt_run({P})", treeLoaded, "r = bt_run({P})", treeLoaded, nullptr},
        {"net_spawn_owned", "okayprefab", "r = net_spawn_owned({P}, 1, 2)", countEvil, "r = net_spawn_owned({P}, 1, 2)",
         countGood, AddNet},
        {"music_play", "wav", "r = music_play({P})", musicTaken, "r = music_play({P})", musicTaken, addMusic},
        {"set_texture", "png", "set_texture({P})", spriteTex, "set_texture({P})", spriteTex, addSprite},
        {"set_image", "png", "set_image({P})", imageTex, "set_image({P})", imageTex, addImage},
        {"set_mesh_texture", "png", "set_mesh_texture({P})", meshTex, "set_mesh_texture({P})", meshTex, addMesh},
    };
    auto goodFor = [](const std::string& ext) {
        return ext == "okaytheme" ? std::string("themes/good.okaytheme")
             : ext == "wav"       ? std::string("music/good.wav")
             : ext == "png"       ? std::string("textures/good.png")
             : ext == "okaybt"    ? std::string("ai/good.okaybt")
                                  : std::string("prefabs/good.okayprefab");
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
        const bool refusedLog = [] {
            for (const auto& l : g_log) if (l.find("refusing") != std::string::npos) return true;
            return false;
        }();
        CHECK(loaded >= 1);
        CHECK(!refusedLog);
        if (loaded >= 1 && !refusedLog) ++goodOk;
        else std::printf("    INSIDE PATH FAILED: %s loaded %d, refusal logged: %s\n", where.c_str(), loaded,
                         refusedLog ? "yes" : "no");
    };

    for (const auto& c : scripts) {
        for (const auto& p : badPaths(c.ext)) {
            g_log.clear();
            report(std::string("script ") + c.name, p, RunScript(c.badCall, p, c.setup, c.badProbe));
        }
        g_log.clear();
        good(std::string("script ") + c.name, RunScript(c.goodCall, goodFor(c.ext), c.setup, c.goodProbe));
    }
    for (const auto& p : badPaths("okayprefab")) {
        g_log.clear();
        report("actions net_spawn_owned", p, RunActions("net_spawn_owned", {p, "1", "2", "0"}, "Evil"));
    }
    g_log.clear();
    good("actions net_spawn_owned", RunActions("net_spawn_owned", {goodFor("okayprefab"), "1", "2", "0"}, "Good"));
    auto runSetSprite = [](const std::string& p) {
        Scene scene("A");
        scene.physicsEnabled = false;
        GameObject* o = scene.CreateGameObject("Hero");
        auto* sr = o->AddComponent<SpriteRenderer>();
        auto* al = o->AddComponent<ActionList>();
        al->trigger = ActionList::Trigger::OnStart;
        al->instructions = {ActionList::Item{"set_sprite", {p}}};
        scene.Start();
        scene.Update(0.016f);
        return sr->texture.empty() ? 0 : 1;
    };
    for (const auto& p : badPaths("png")) {
        g_log.clear();
        report("actions set_sprite", p, runSetSprite(p));
    }
    g_log.clear();
    good("actions set_sprite", runSetSprite(goodFor("png")));
    for (const auto& p : badPaths("okayprefab")) {
        g_log.clear();
        report("visual script NetSpawnOwned", p, RunVsNetSpawn(p, "Evil"));
    }
    g_log.clear();
    good("visual script NetSpawnOwned", RunVsNetSpawn(goodFor("okayprefab"), "Good"));

    std::printf("    refused %d/%d outside paths (with a log line); loaded %d/%d inside paths\n", refused, total, goodOk,
                goodTotal);

    CHECK(chdir(oldCwd.string().c_str()) == 0);
    fs::remove_all(root, ec);
    Log::sink = nullptr;
    TEST_MAIN_RESULT();
}
