// The Script Editor sample that ships in examples\script_editor (docs/try_it.md, row "Own code editor"): the scene loads, its one
// script object carries the same text as highlight_sample.okay, and that text compiles, so the owner can open it in the Script
// Editor, see it coloured, and press Play without an error.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Scene/SceneSerializer.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

namespace {
std::vector<std::string> g_lines, g_errors;

std::string ReadAll(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
}

int main() {
    RUN_SUITE("script editor sample");
    const std::string dir = std::string(OKAY_SOURCE_DIR) + "/examples/script_editor/";
    std::string file = ReadAll(dir + "highlight_sample.okay");
    CHECK(file.size() > 500);
    if (!file.empty() && file.back() == '\n') file.pop_back();      // the scene stores the text without the final newline

    Log::sink = [](Log::Level l, const std::string& m) { g_lines.push_back(m); if (l >= Log::Level::Error) g_errors.push_back(m); };
    Scene scene;
    std::string err;
    CHECK(SceneSerializer::LoadFromFile(scene, dir + "script_editor_sample.okayscene", &err));
    CHECK(err.empty());
    GameObject* go = scene.Find("Script sample");
    CHECK(go != nullptr);
    ScriptComponent* sc = go ? go->GetComponent<ScriptComponent>() : nullptr;
    CHECK(sc != nullptr);
    if (sc) {
        CHECK(sc->Language() == "okayscript");
        CHECK(sc->Source() == file);                                  // one text, two places: the scene and the loose file
        std::string cerr;
        CHECK(sc->LoadSource(sc->Source(), &cerr));                   // it compiles
        CHECK(cerr.empty());
        // Play: Start logs its line, a few frames run without an error, nothing is pressed so no hop starts
        g_lines.clear(); g_errors.clear();
        scene.Start();
        for (int i = 0; i < 3; ++i) scene.Update(1.0f / 60.0f);
        bool logged = false;
        for (const std::string& l : g_lines) if (l.find("Script sample is ready, speed 5") != std::string::npos) logged = true;
        CHECK(logged);
        CHECK(g_errors.empty());
    }
    Log::sink = nullptr;
    // The Latin-1 sample (docs/try_it.md, "a script the editor cannot show"): not valid UTF-8 on purpose (a single byte E9 after "caf"),
    // and still a script that compiles, so whoever copies it into a project sees the read-only note and no compile error.
    {
        const std::string bad = ReadAll(dir + "latin1_sample.okay");
        CHECK(bad.size() > 200 && bad.find("caf\xE9\n") != std::string::npos);
        CHECK(bad.find('\r') == std::string::npos && bad.find('\0') == std::string::npos);
        ScriptComponent latin("okayscript");
        std::string lerr;
        CHECK(latin.LoadSource(bad, &lerr));
        CHECK(lerr.empty());
    }
    // the text shows each thing the editor colours: keywords, types, strings, numbers, both kinds of line comment, a block comment
    CHECK(file.find("public float") != std::string::npos && file.find("Vector3") != std::string::npos);
    CHECK(file.find("\"Horizontal\"") != std::string::npos && file.find("0.25") != std::string::npos);
    CHECK(file.find("\n// ") != std::string::npos && file.find("\n# ") != std::string::npos && file.find("/* ") != std::string::npos);
    TEST_MAIN_RESULT();
}
