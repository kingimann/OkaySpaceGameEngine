// Node Playground: every high-level gameplay node is used in a demo scene, and
// every demo scene loads and runs without a visual-script error.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/VisualScript/NodeGraph.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace okay;

static std::string Dir() {
    return std::string(OKAY_SOURCE_DIR) + "/examples/visual_script/playground";
}

static std::set<std::string> FactoryNodes() {
    std::ifstream in(std::string(OKAY_SOURCE_DIR) + "/engine/include/okay/VisualScript/NodesGameplay.hpp");
    std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::set<std::string> names;
    const std::string key = "type == \"";
    for (std::size_t at = 0; (at = src.find(key, at)) != std::string::npos; ) {
        at += key.size();
        const std::size_t end = src.find('"', at);
        if (end == std::string::npos) break;
        names.insert(src.substr(at, end - at));
        at = end + 1;
    }
    return names;
}

static std::set<std::string> NodesInGraphs(const std::string& dir) {
    std::set<std::string> names;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        if (e.path().extension() != ".okayvs") continue;
        std::ifstream in(e.path());
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream ls(line);
            std::string kind, id, type;
            ls >> kind >> id >> type;
            if (kind == "node" && !type.empty()) names.insert(type);
        }
    }
    return names;
}

static std::vector<std::string> g_errs;
static void OnErr(const vs::VsRuntimeError& e) {
    g_errs.push_back(e.nodeType + " " + e.message);
}

int main() {
    RUN_SUITE("vs_playground");
    const std::string dir = Dir();
    CHECK(std::filesystem::exists(dir));

    const std::set<std::string> factory = FactoryNodes();
    const std::set<std::string> used = NodesInGraphs(dir);
    CHECK(factory.size() >= 60);
    int missing = 0;
    for (const auto& name : factory) {
        if (!used.count(name)) {
            std::printf("  playground missing node: %s\n", name.c_str());
            ++missing;
        }
    }
    CHECK(missing == 0);
    std::printf("  playground uses %d / %d gameplay nodes\n", (int)used.size(), (int)factory.size());

    // The categories Iman asked to be able to open.
    const char* scenes[] = {
        "movement.okayscene", "animation.okayscene", "interaction.okayscene", "combat.okayscene",
        "ai.okayscene", "spawning.okayscene", "gameflow.okayscene", "ui.okayscene",
        "camera.okayscene", "audio.okayscene", "input.okayscene", "random.okayscene", "math.okayscene",
        "logic.okayscene",
    };
    vs::SetRuntimeErrorHook(&OnErr);
    Time::SetTimeScale(1);
    int ran = 0;
    for (const char* file : scenes) {
        g_errs.clear();
        Scene scene;
        std::string err;
        const std::string path = dir + "/" + file;
        CHECK(SceneSerializer::LoadFromFile(scene, path, &err));
        if (!err.empty()) std::printf("  load %s: %s\n", file, err.c_str());
        bool told = false, script = false;
        for (const auto& up : scene.Objects()) {
            if (!up) continue;
            if (auto* t = up->GetComponent<TextRenderer>())
                if (t->text.find("Play") != std::string::npos) told = true;
            for (auto* vc : up->GetComponents<VisualScriptComponent>()) {
                if (vc->Graph() && vc->Graph()->NodeCount() > 0) script = true;
                if (!vc->LastError().empty()) g_errs.push_back(vc->LastError());
            }
        }
        CHECK(told);
        CHECK(script);
        scene.Start();
        for (int i = 0; i < 180; ++i) scene.Update(1.0f / 60.0f);
        if (!g_errs.empty()) {
            std::printf("  %s errors:\n", file);
            for (const auto& e : g_errs) std::printf("    %s\n", e.c_str());
        }
        CHECK(g_errs.empty());
        ++ran;
    }
    CHECK(ran == 14);
    vs::SetRuntimeErrorHook(nullptr);
    Time::SetTimeScale(1);
    TEST_MAIN_RESULT();
}
