// A scene file that starts with a UTF-8 byte-order mark (EF BB BF, added by some text editors such as Notepad's
// "UTF-8 with BOM") loads. Before, SceneSerializer said "missing 'okayscene' header" (issue #888; the agent's known-bug
// check scene_bom_bug_load_from_file on claude2/upgrade-3-bom-scene, which fails 2 of 4 checks without this fix).
#include "test_framework.hpp"
#include <Okay.hpp>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace okay;

int main() {
    RUN_SUITE("scene_bom_load");
    const fs::path dir = fs::temp_directory_path() /
        ("okay_scenebom_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::create_directories(dir, ec);
    Scene made("Bom");
    made.CreateGameObject("Hero");
    const std::string text = SceneSerializer::Serialize(made);
    const std::string bom = "\xEF\xBB\xBF";
    std::ofstream(dir / "plain.okayscene", std::ios::binary) << text;
    std::ofstream(dir / "bom.okayscene", std::ios::binary) << bom + text;
    std::ofstream(dir / "bomcrlf.okayscene", std::ios::binary) << [&] {
        std::string t = bom; for (char c : text) { if (c == '\n') t += '\r'; t += c; } return t; }();
    for (const char* f : {"plain.okayscene", "bom.okayscene", "bomcrlf.okayscene"}) {
        Scene s("probe");
        std::string err;
        const bool ok = SceneSerializer::LoadFromFile(s, (dir / f).string(), &err);
        if (!ok) std::fprintf(stderr, "  %s: %s\n", f, err.c_str());
        CHECK(ok && s.Find("Hero") != nullptr);
    }
    {   // the text path too (Deserialize), and a merge
        Scene s("t");
        std::string err;
        CHECK(SceneSerializer::Deserialize(s, bom + text, &err) && s.Find("Hero"));
        Scene m("m");
        CHECK(SceneSerializer::MergeFromText(m, bom + text) && m.Find("Hero"));
    }
    {   // a mark in the middle of the text is not stripped (only a leading one is a byte-order mark)
        Scene s("mid");
        std::string err;
        CHECK(!SceneSerializer::Deserialize(s, "x" + bom + text, &err));
    }
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
