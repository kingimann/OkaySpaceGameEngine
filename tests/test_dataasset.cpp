#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>
#include <filesystem>
#include <random>

using namespace okay;

int main() {
    RUN_SUITE("dataasset");

    // Work in a fresh temp folder: scripts may only name files relative to the game.
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("okay_dataasset_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    const fs::path oldCwd = fs::current_path();
    fs::current_path(dir);
    const std::string path = "okay_item.okaydata";

    // --- Build, query, save, reload ------------------------------------
    {
        DataAsset a;
        a.Set("name", "Goblin");
        a.Set("hp", "30");
        a.Set("speed", "3.5");
        CHECK(a.Has("hp"));
        CHECK(a.GetString("name") == "Goblin");
        CHECK_NEAR((float)a.GetNumber("hp"), 30.0f, 1e-4f);
        CHECK_NEAR((float)a.GetNumber("speed"), 3.5f, 1e-4f);
        CHECK_NEAR((float)a.GetNumber("missing", 9.0), 9.0f, 1e-4f);

        a.Set("hp", "45");                 // replace, not duplicate
        CHECK(a.Fields().size() == 3);
        CHECK_NEAR((float)a.GetNumber("hp"), 45.0f, 1e-4f);

        CHECK(a.Save(path));
        DataAsset b; CHECK(b.Load(path));
        CHECK(a.Save((dir / "abs_item.okaydata").string()));   // the C++ API itself takes any path
        CHECK(b.GetString("name") == "Goblin");
        CHECK_NEAR((float)b.GetNumber("hp"), 45.0f, 1e-4f);
    }

    // --- Read from OkayScript via data_num / data_str ------------------
    {
        Scene s("d"); s.physicsEnabled = false;
        auto* sc = s.CreateGameObject("Reader")->AddComponent<ScriptComponent>("okayscript");
        sc->LoadSource(
            std::string("var hp = 0; var nm = \"\";\n") +
            "function start() {\n"
            "  hp = data_num(\"" + path + "\", \"hp\");\n"
            "  nm = data_str(\"" + path + "\", \"name\");\n"
            "}\n");
        s.Start();
        CHECK_NEAR(sc->VM()->GetGlobal("hp").AsFloat(), 45.0f, 1e-4f);
        CHECK(sc->VM()->GetGlobal("nm").AsString() == "Goblin");
    }

    // --- Scripts can't read data assets by absolute path ----------------
    {
        Scene s("abs"); s.physicsEnabled = false;
        auto* sc = s.CreateGameObject("Reader")->AddComponent<ScriptComponent>("okayscript");
        const std::string abs = (dir / "abs_item.okaydata").string();
        sc->LoadSource(
            std::string("var hp = 0;\n") +
            "function start() { hp = data_num(\"" + abs + "\", \"hp\", -1); }\n");
        s.Start();
        CHECK_NEAR(sc->VM()->GetGlobal("hp").AsFloat(), -1.0f, 1e-4f);   // refused -> the default
    }

    fs::current_path(oldCwd);
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
