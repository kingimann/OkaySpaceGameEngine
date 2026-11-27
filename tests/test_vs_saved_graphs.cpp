// Saved graphs keep loading: every graph under examples/ and assets/ still names only node types that exist
// (directly or through the alias table), and each one loads and runs a few frames. Display names changed in the
// plain-words pass; the saved identifiers did not, and this test is what proves it.
#include "test_framework.hpp"
#include "VisualScriptModel.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/VisualScript/NodeAliases.hpp"
#include "okay/VisualScript/NodeGraph.hpp"
#include <Okay.hpp>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

using namespace okay;
using namespace okay::editor::vsed;
namespace fs = std::filesystem;

int main() {
#ifdef OKAY_SOURCE_DIR
    fs::current_path(OKAY_SOURCE_DIR);
#endif
    RUN_SUITE("vs_saved_graphs");
    auto sink = okay::Log::sink;
    okay::Log::sink = [](okay::Log::Level, const std::string&) {};
    std::set<std::string> known;
    for (const VsDef& d : Registry()) known.insert(d.type);
    int graphs = 0, nodes = 0, retired = 0;
    for (const char* root : {"examples", "assets"}) {
        if (!fs::exists(root)) continue;
        for (const auto& e : fs::recursive_directory_iterator(root)) {
            if (!e.is_regular_file() || e.path().extension() != ".okayvs") continue;
            std::ifstream in(e.path());
            std::stringstream ss; ss << in.rdbuf();
            const std::string text = ss.str();
            ++graphs;
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line)) {
                if (line.rfind("node ", 0) != 0) continue;
                std::istringstream w(line);
                std::string kw, id, type;
                w >> kw >> id >> type;
                ++nodes;
                const std::string canon = okay::vs::CanonicalNodeType(type);
                if (canon != type) ++retired;
                const bool ok = known.count(canon) > 0 || okay::vs::MakeNode(canon, {}, nullptr) != nullptr;
                CHECK(ok);
                if (!ok) std::printf("  %s: unknown saved id '%s'\n", e.path().string().c_str(), type.c_str());
            }
            Scene scene("Saved");
            auto* vs = scene.CreateGameObject("A")->AddComponent<VisualScriptComponent>();
            std::string err;
            const bool loaded = vs->LoadFromText(text, &err);
            CHECK(loaded);
            if (!loaded) std::printf("  %s did not load: %s\n", e.path().string().c_str(), err.c_str());
            scene.Start();
            scene.Update(0.016f);
            scene.Update(0.016f);
        }
    }
    CHECK(graphs >= 30);
    std::printf("  saved graphs loaded and run: %d graphs, %d nodes, %d through the alias table\n", graphs, nodes, retired);
    okay::Log::sink = sink;
    TEST_MAIN_RESULT();
}
