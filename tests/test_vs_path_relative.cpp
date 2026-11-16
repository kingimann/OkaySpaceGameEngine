// A scene's `visualscriptpath` stays as it was written (#888, finding 42).
//
// Before: loading a scene turned `visualscriptpath "gl_health.okayvs"` into the full path
// of the file on this machine, and the next save wrote that full path back. Once the
// project folder was moved or cloned, the scene no longer found its graph file and ran
// the old copy saved inside the scene, with no warning. Built games carried the
// developer's folder (which can hold their user name).
//
// This test uses the Node Playground health example (a Fighter whose graph takes
// 12 health per second) and checks:
//  - load + save in place keeps `visualscriptpath "gl_health.okayvs"`;
//  - after the folder is renamed, an edit to the .okayvs is what runs;
//  - with the .okayvs gone, the copy in the scene runs and ONE warning names the file;
//  - an old scene holding a full path to a folder that is gone heals (the file next to
//    the scene is used, and the next save writes it relative);
//  - a full path inside the scene's folder, or inside the project's Assets folder, is
//    written relative on save;
//  - a scene rebuilt from text (editor Undo / Stop) or a copy of an object still
//    reads the file;
//  - the scene a build writes (`<Game>_Data/game.okayscene`) holds no full path.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/BuildPipeline.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

std::string ReadFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void WriteFile(const fs::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << s;
}

bool Replace(std::string& s, const std::string& from, const std::string& to) {
    const std::size_t at = s.find(from);
    if (at == std::string::npos) return false;
    s.replace(at, from.size(), to);
    return true;
}

// The scene format's quoting: a backslash before '"' and '\'.
std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

// Every value of a `visualscriptpath` record in a scene, in file order (unquoted).
std::vector<std::string> GraphPaths(const std::string& sceneText) {
    std::vector<std::string> out;
    std::istringstream in(sceneText);
    std::string line;
    const std::string key = "visualscriptpath ";
    while (std::getline(in, line)) {
        const std::size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos || line.compare(b, key.size(), key) != 0) continue;
        std::string v = line.substr(b + key.size());
        while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
        std::string un;
        if (v.size() >= 2 && v.front() == '"') {
            for (std::size_t i = 1; i + 1 < v.size(); ++i) {
                if (v[i] == '\\' && i + 2 < v.size()) ++i;
                un += v[i];
            }
        } else {
            un = v;
        }
        out.push_back(un);
    }
    return out;
}

// A full machine path: "/home/..." or "C:\..." / "C:/...".
bool IsFullPath(const std::string& p) {
    if (p.empty()) return false;
    if (p[0] == '/' || p[0] == '\\') return true;
    return p.size() >= 2 && p[1] == ':';
}

bool AnyFullPath(const std::string& sceneText) {
    for (const std::string& p : GraphPaths(sceneText)) if (IsFullPath(p)) return true;
    return false;
}

std::string Show(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : ", ") + ("\"" + x + "\"");
    return "[" + s + "]";
}

// What happened when a scene was loaded and played.
struct Run {
    bool loaded = false;
    float health = -1.0f;          // the graph's `health` variable after the frames
    int warningsNaming = 0;        // load-time warnings that name the graph file
    std::vector<std::string> warnings;
};

constexpr int kFrames = 120;       // 2 seconds at 60 frames a second
constexpr float kOldHealth = 76;   // 100 - 2 s * 12/s: the graph as shipped (and as embedded by the first save)
constexpr float kNewHealth = 60;   // 100 - 2 s * 20/s: the edited graph file

void PlayFighter(Scene& sc, Run& r) {
    sc.Start();
    for (int i = 0; i < kFrames; ++i) sc.Update(1.0f / 60.0f);
    GameObject* f = sc.Find("Fighter");
    auto* vc = f ? f->GetComponent<VisualScriptComponent>() : nullptr;
    if (vc) r.health = vc->GetVariable("health").AsFloat();
}

Run LoadAndPlay(const fs::path& scenePath) {
    Run r;
    Log::sink = [&r](Log::Level lv, const std::string& m) {
        if (lv == Log::Level::Warning) r.warnings.push_back(m);
    };
    Scene sc("t");
    std::string err;
    r.loaded = SceneSerializer::LoadFromFile(sc, scenePath.string(), &err);
    Log::sink = nullptr;
    if (!r.loaded) std::printf("  load %s failed: %s\n", scenePath.string().c_str(), err.c_str());
    for (const std::string& w : r.warnings)
        if (w.find("gl_health.okayvs") != std::string::npos) ++r.warningsNaming;
    if (r.loaded) PlayFighter(sc, r);
    return r;
}

// Load a scene and save it where it is (what opening it in the editor and pressing Ctrl+S does).
bool LoadSave(const fs::path& scenePath) {
    Scene sc("t");
    std::string err;
    if (!SceneSerializer::LoadFromFile(sc, scenePath.string(), &err)) { std::printf("  load: %s\n", err.c_str()); return false; }
    if (!SceneSerializer::SaveToFile(sc, scenePath.string(), &err)) { std::printf("  save: %s\n", err.c_str()); return false; }
    return true;
}

void SetRate(const fs::path& graph, const char* rate) {
    std::string g = ReadFile(graph);
    // node 2 is the GlDamageOverTime the first chain runs; its pin 0 is the damage per second
    const bool ok = Replace(g, "default 2 0 12\n", std::string("default 2 0 ") + rate + "\n") ||
                    Replace(g, "default 2 0 20\n", std::string("default 2 0 ") + rate + "\n") ||
                    Replace(g, "default 2 0 30\n", std::string("default 2 0 ") + rate + "\n");
    CHECK(ok);
    WriteFile(graph, g);
}

} // namespace

int main() {
    RUN_SUITE("vs_path_relative");
    const fs::path examples = fs::path(OKAY_SOURCE_DIR) / "examples" / "visual_script" / "playground";
    const std::string sceneText = ReadFile(examples / "gl_health.okayscene");
    const std::string graphText = ReadFile(examples / "gl_health.okayvs");
    CHECK(!sceneText.empty() && !graphText.empty());
    CHECK(GraphPaths(sceneText) == std::vector<std::string>{"gl_health.okayvs"});
    CHECK(graphText.find("default 2 0 12\n") != std::string::npos);

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() / ("okay_vs_path_relative_" + std::to_string((long long)stamp));
    std::error_code ec;
    fs::remove_all(root, ec);

    // ---- 1. Round trip: load and save in place keeps the path as written. ----------------------
    const fs::path projA = root / "projA";
    fs::create_directories(projA);
    WriteFile(projA / "gl_health.okayscene", sceneText);
    WriteFile(projA / "gl_health.okayvs", graphText);
    {
        const Run before = LoadAndPlay(projA / "gl_health.okayscene");
        CHECK(before.loaded);
        CHECK_NEAR(before.health, kOldHealth, 1.5f);   // the harness itself: 12 per second
        CHECK(LoadSave(projA / "gl_health.okayscene"));
        const std::string saved = ReadFile(projA / "gl_health.okayscene");
        const auto paths = GraphPaths(saved);
        std::printf("  round trip: visualscriptpath %s\n", Show(paths).c_str());
        CHECK(paths == std::vector<std::string>{"gl_health.okayvs"});
        CHECK(!AnyFullPath(saved));
        CHECK(saved.find(projA.string()) == std::string::npos);   // no trace of this machine's folder
    }

    // ---- 2. Move: rename the folder, edit the graph file: the edit is what runs. ---------------
    const fs::path projB = root / "projB";
    fs::rename(projA, projB);
    SetRate(projB / "gl_health.okayvs", "20");
    {
        const Run moved = LoadAndPlay(projB / "gl_health.okayscene");
        std::printf("  moved + edited: health %.2f (want %.0f, the stale copy gives %.0f)\n", moved.health, kNewHealth, kOldHealth);
        CHECK(moved.loaded);
        CHECK_NEAR(moved.health, kNewHealth, 1.5f);
        CHECK(moved.warningsNaming == 0);
    }

    // ---- 2b. Rebuilt from text (editor Undo / Stop) and a copy of the object still read the file.
    {
        Scene sc("t");
        std::string err;
        CHECK(SceneSerializer::LoadFromFile(sc, (projB / "gl_health.okayscene").string(), &err));
        SetRate(projB / "gl_health.okayvs", "30");   // edited while the scene is open
        const std::string text = SceneSerializer::Serialize(sc);
        CHECK(!AnyFullPath(text));
        CHECK(SceneSerializer::Deserialize(sc, text, &err));   // what Undo and Stop do
        GameObject* f = sc.Find("Fighter");
        auto* vc = f ? f->GetComponent<VisualScriptComponent>() : nullptr;
        CHECK(vc && vc->Source().find("default 2 0 30\n") != std::string::npos);
        if (vc) {
            bool found = false;
            CHECK(vc->Path() == "gl_health.okayvs");
            CHECK(fs::path(vc->ResolvedPath(&found)) == projB / "gl_health.okayvs" && found);
            CHECK(vc->LastResolvedPath() == vc->ResolvedPath());
            CHECK(vc->Graph() && vc->Graph()->debugKey == vc->ResolvedPath());   // breakpoints and Run Graph look beside the file
        }
        // A copy made in memory (Duplicate, a spawner) reads the file too.
        SetRate(projB / "gl_health.okayvs", "20");
        GameObject* copy = f ? SceneSerializer::Instantiate(sc, *f) : nullptr;
        auto* cvc = copy ? copy->GetComponent<VisualScriptComponent>() : nullptr;
        CHECK(cvc && cvc->Source().find("default 2 0 20\n") != std::string::npos);
        CHECK(cvc && cvc->Path() == "gl_health.okayvs" && fs::path(cvc->ResolvedPath()) == projB / "gl_health.okayvs");
    }

    // ---- 3. Missing file: the copy in the scene runs, and one warning names the file. ----------
    fs::remove(projB / "gl_health.okayvs");
    {
        const Run missing = LoadAndPlay(projB / "gl_health.okayscene");
        std::printf("  missing file: health %.2f, %d warning(s) name the file\n", missing.health, missing.warningsNaming);
        for (const std::string& w : missing.warnings) std::printf("    warning: %s\n", w.c_str());
        CHECK(missing.loaded);
        CHECK_NEAR(missing.health, kOldHealth, 1.5f);   // the copy saved in the scene (12 per second)
        CHECK(missing.warningsNaming == 1);
    }
    // No path at all: no warning.
    {
        const fs::path projN = root / "projN";
        fs::create_directories(projN);
        std::string noPath = sceneText;
        CHECK(Replace(noPath, "  visualscript \"\"\n  visualscriptpath \"gl_health.okayvs\"\n",
                      "  visualscript " + Quote(graphText) + "\n"));
        WriteFile(projN / "gl_health.okayscene", noPath);
        const Run r = LoadAndPlay(projN / "gl_health.okayscene");
        CHECK(r.loaded);
        CHECK_NEAR(r.health, kOldHealth, 1.5f);
        CHECK(r.warnings.empty());
    }

    // A scene holding the original graph (12 per second) inside it, with `pathValue` as its path.
    auto oldScene = [&](const std::string& pathValue) {
        std::string s = sceneText;
        CHECK(Replace(s, "  visualscript \"\"\n  visualscriptpath \"gl_health.okayvs\"\n",
                      "  visualscript " + Quote(graphText) + "\n  visualscriptpath " + Quote(pathValue) + "\n"));
        return s;
    };

    // ---- 4. An old scene with a full path to a folder that is gone heals. ----------------------
    {
        const fs::path projC = root / "projC";
        fs::create_directories(projC);
        const std::string gone = (root / "gone" / "gl_health.okayvs").string();
        WriteFile(projC / "gl_health.okayscene", oldScene(gone));
        WriteFile(projC / "gl_health.okayvs", graphText);
        SetRate(projC / "gl_health.okayvs", "20");
        const Run healed = LoadAndPlay(projC / "gl_health.okayscene");
        std::printf("  old full path, file next to the scene: health %.2f (want %.0f)\n", healed.health, kNewHealth);
        CHECK(healed.loaded);
        CHECK_NEAR(healed.health, kNewHealth, 1.5f);
        CHECK(healed.warningsNaming == 0);
        CHECK(LoadSave(projC / "gl_health.okayscene"));
        const auto paths = GraphPaths(ReadFile(projC / "gl_health.okayscene"));
        std::printf("  healed scene saved: visualscriptpath %s\n", Show(paths).c_str());
        CHECK(paths == std::vector<std::string>{"gl_health.okayvs"});
    }

    // ---- 4b. A full path inside the scene's folder is written relative. ------------------------
    {
        const fs::path projD = root / "projD";
        fs::create_directories(projD / "graphs");
        WriteFile(projD / "graphs" / "gl_health.okayvs", graphText);
        WriteFile(projD / "gl_health.okayscene", oldScene((projD / "graphs" / "gl_health.okayvs").string()));
        CHECK(LoadSave(projD / "gl_health.okayscene"));
        const auto paths = GraphPaths(ReadFile(projD / "gl_health.okayscene"));
        std::printf("  full path in the scene's folder saved as %s\n", Show(paths).c_str());
        CHECK(paths == std::vector<std::string>{"graphs/gl_health.okayvs"});
    }

    // ---- 4c. A full path inside the project's Assets folder is written "Assets/...", and still
    //          found after the project moves. -----------------------------------------------------
    {
        const fs::path projE = root / "projE";
        fs::create_directories(projE / "Assets" / "levels");
        fs::create_directories(projE / "Assets" / "scripts");
        WriteFile(projE / "Assets" / "scripts" / "gl_health.okayvs", graphText);
        WriteFile(projE / "Assets" / "levels" / "gl_health.okayscene",
                  oldScene((projE / "Assets" / "scripts" / "gl_health.okayvs").string()));
        CHECK(LoadSave(projE / "Assets" / "levels" / "gl_health.okayscene"));
        const auto paths = GraphPaths(ReadFile(projE / "Assets" / "levels" / "gl_health.okayscene"));
        std::printf("  full path in the project's Assets saved as %s\n", Show(paths).c_str());
        CHECK(paths == std::vector<std::string>{"Assets/scripts/gl_health.okayvs"});
        const fs::path projF = root / "projF";
        fs::rename(projE, projF);
        SetRate(projF / "Assets" / "scripts" / "gl_health.okayvs", "20");
        const Run moved = LoadAndPlay(projF / "Assets" / "levels" / "gl_health.okayscene");
        std::printf("  project moved: health %.2f (want %.0f)\n", moved.health, kNewHealth);
        CHECK_NEAR(moved.health, kNewHealth, 1.5f);
        CHECK(moved.warningsNaming == 0);
    }

    // ---- 5. Build: the scene a build writes holds no full machine path. ------------------------
    {
        // The editor's build writes the open scene with SaveToFile into the data folder.
        const fs::path projG = root / "projG";
        const fs::path data = root / "out" / "Game_Data";
        fs::create_directories(projG);
        fs::create_directories(data);
        WriteFile(projG / "gl_health.okayscene", sceneText);
        WriteFile(projG / "gl_health.okayvs", graphText);
        Scene open("t");
        std::string err;
        CHECK(SceneSerializer::LoadFromFile(open, (projG / "gl_health.okayscene").string(), &err));
        CHECK(SceneSerializer::SaveToFile(open, (data / "game.okayscene").string(), &err));
        const std::string shipped = ReadFile(data / "game.okayscene");
        std::printf("  build: game.okayscene visualscriptpath %s\n", Show(GraphPaths(shipped)).c_str());
        CHECK(!AnyFullPath(shipped));
        CHECK(shipped.find(projG.string()) == std::string::npos);
        // The build then drops the path of every graph saved inside the open scene: the .okayvs is not
        // shipped for it, so the game runs the copy without looking for (and warning about) a file.
        std::string why;
        CHECK(buildpipe::MakeSceneFileShippable((data / "game.okayscene").string(), /*graphFilesShip=*/false, &why));
        const std::string game = ReadFile(data / "game.okayscene");
        std::printf("  build: after the build's clean-up %s\n", Show(GraphPaths(game)).c_str());
        CHECK(GraphPaths(game).empty());
        CHECK(game.find("visualscript \"") != std::string::npos);   // the graph itself is still there
        const Run played = LoadAndPlay(data / "game.okayscene");
        CHECK(played.loaded);
        CHECK_NEAR(played.health, kOldHealth, 1.5f);
        CHECK(played.warnings.empty());

        // A scene copied into the build as a file keeps its paths (its .okayvs ships with it); a full
        // path in it becomes the name the build ships the file under.
        WriteFile(data / "level2.okayscene", oldScene((projG / "gl_health.okayvs").string()));
        CHECK(buildpipe::MakeSceneFileShippable((data / "level2.okayscene").string(), /*graphFilesShip=*/true, &why));
        CHECK(GraphPaths(ReadFile(data / "level2.okayscene")) == std::vector<std::string>{"gl_health.okayvs"});
        WriteFile(data / "level3.okayscene", sceneText);   // relative already: left byte for byte
        CHECK(buildpipe::MakeSceneFileShippable((data / "level3.okayscene").string(), /*graphFilesShip=*/true, &why));
        CHECK(ReadFile(data / "level3.okayscene") == sceneText);

        // Only records are rewritten: a line inside a quoted value that looks like one is left alone,
        // and a Windows path counts as a full path on any OS.
        const std::string tricky =
            "okayscene 3\ngameobject 0 \"A\"\n"
            "  visualscript \"node 1 OnStart\n  visualscriptpath \\\"/keep/me.okayvs\\\"\n\"\n"
            "  visualscriptpath \"C:\\\\Users\\\\someone\\\\Game\\\\a.okayvs\"\n"
            "end\ngameobject 1 \"B\"\n  visualscript \"\"\n  visualscriptpath \"/home/someone/b.okayvs\"\nend\n";
        int changed = 0;
        const std::string kept = buildpipe::ShippableSceneText(tricky, /*graphFilesShip=*/true, &changed);
        CHECK(changed == 2);
        CHECK(kept.find("visualscriptpath \\\"/keep/me.okayvs\\\"") != std::string::npos);
        CHECK(kept.find("  visualscriptpath \"a.okayvs\"\n") != std::string::npos);
        CHECK(kept.find("  visualscriptpath \"b.okayvs\"\n") != std::string::npos);
        CHECK(kept.find("someone") == std::string::npos);
        const std::string dropped = buildpipe::ShippableSceneText(tricky, /*graphFilesShip=*/false, &changed);
        CHECK(changed == 2);   // A's path dropped (its graph is inside), B's (nothing inside) made a file name
        CHECK(dropped.find("C:") == std::string::npos && dropped.find("/home/") == std::string::npos);
        CHECK(dropped.find("visualscriptpath \\\"/keep/me.okayvs\\\"") != std::string::npos);
        CHECK(dropped.find("  visualscriptpath \"b.okayvs\"\n") != std::string::npos);
    }

    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
