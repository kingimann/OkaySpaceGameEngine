#include "test_framework.hpp"
#include <Okay.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

// Scripts come with game content, so the file names they pass to save / prefs /
// data / build / scene APIs must stay inside the game's folders: absolute paths
// and ".." are refused, and saves land in the game's save folder.

static vs::VsValue Run(const std::string& src, const char* var) {
    auto vm = CreateScriptVM("okayscript");
    std::string err;
    if (!vm->Load(src, &err)) { std::printf("  load error: %s\n", err.c_str()); return vs::VsValue{}; }
    return vm->GetGlobal(var);
}

int main() {
    RUN_SUITE("script_paths");

    // Work in a fresh temp folder: the game folder, with saves/ under it.
    std::mt19937 rng(std::random_device{}());
    const fs::path root = fs::temp_directory_path() / ("okay_paths_" + std::to_string(rng()));
    fs::create_directories(root / "game");
    const fs::path oldCwd = fs::current_path();
    fs::current_path(root / "game");
    Log::sink = [](Log::Level, const std::string&) {};   // refusals are logged; keep the output clean

    // ---- The rule itself ----
    CHECK(Save::IsSafeRelative("save.okaysave"));
    CHECK(Save::IsSafeRelative("slots/one.okaysave"));
    CHECK(Save::IsSafeRelative("a..b.txt"));            // dots inside a name are fine
    CHECK(!Save::IsSafeRelative(""));
    CHECK(!Save::IsSafeRelative("../x"));
    CHECK(!Save::IsSafeRelative("a/../../x"));
    CHECK(!Save::IsSafeRelative("a\\..\\x"));
    CHECK(!Save::IsSafeRelative(".."));
    CHECK(!Save::IsSafeRelative("/etc/passwd"));
    CHECK(!Save::IsSafeRelative("\\\\server\\share\\x"));
    CHECK(!Save::IsSafeRelative("C:\\Windows\\x"));
    CHECK(!Save::IsSafeRelative("C:x"));
    CHECK(!Save::IsSafeRelative(std::string("ok\0../x", 7)));

    // ---- SEC1: Windows-only dangers are refused on every system (the game may run on Windows) ----
    for (const char* bad : {"NUL", "con.txt", "dir/aux", "COM1", "x.", "x ", "a:b", "file.txt:hidden", "C:/x", "a|b", "what?"})
        CHECK(!Save::IsSafeRelative(bad));
    CHECK(!Save::IsSafeRelative(std::string(300, 'a')));
    CHECK(!Run(R"(var r = save("k", 1, "NUL");)", "r").AsBool());
    CHECK(!Run(R"(var r = save("k", 1, "slot.okaysave:stream");)", "r").AsBool());
    CHECK(!Run(R"(var r = data_save("aux.okaydata");)", "r").AsBool());
    CHECK(!Run(R"(var r = prefs_save("com1");)", "r").AsBool());
    CHECK(!Run(R"(var r = build_save("nul");)", "r").AsBool());
    CHECK(!Run(R"(var r = build_load("C:evil");)", "r").AsBool());
    CHECK(!Run(R"(var r = spawn("/etc/passwd", 0, 0);)", "r").AsBool());
    CHECK(!Run(R"(var r = spawn3("../../x.okayprefab", 0, 0, 0);)", "r").AsBool());
    // A visual-script Save Creation node takes its own name: it must not write outside the game folder.
    {
        Scene scene("VSP"); scene.physicsEnabled = false;
        GameObject* go = scene.CreateGameObject("G");
        auto* vsc = go->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vsc->LoadFromText("node 0 OnStart\nnode 1 SaveCreation ../../vs_evil\nnode 2 SaveCreation nul\nexec 0 0 1\nexec 1 0 2\nentry OnStart 0\n", &err));
        scene.Start();
        for (int i = 0; i < 3; ++i) scene.Update(0.016f);
        CHECK(!fs::exists(root / "vs_evil.okaycreation"));
    }
    // A link inside the game folder that points out of it is refused for reads and writes.
    {
        std::error_code lec;
        fs::create_directories(root / "outside_dir");
        fs::create_directory_symlink(root / "outside_dir", root / "game" / "linkdir", lec);
        if (!lec) {
            CHECK(!Run(R"(var r = save("k", 1, "linkdir/x.okaysave");)", "r").AsBool());
            CHECK(!fs::exists(root / "outside_dir" / "x.okaysave"));
            CHECK(!Run(R"(var r = prefs_save("linkdir/p.okayprefs");)", "r").AsBool());
            CHECK(!fs::exists(root / "outside_dir" / "p.okayprefs"));
            fs::remove(root / "game" / "linkdir", lec);
        }
    }

    // ---- reading a prefab by absolute path: inside the game, save or temp folder only ----
    {
        const std::string inTemp = (root / "game" / "p.okayprefab").generic_string();
        CHECK(ScriptAssetPathOk(inTemp, "t"));
        CHECK(ScriptAssetPathOk("p.okayprefab", "t"));
        CHECK(!ScriptAssetPathOk("/etc/passwd", "t"));
        CHECK(!ScriptAssetPathOk("/proc/self/environ", "t"));
        CHECK(!ScriptAssetPathOk((root / "game" / ".." / ".." / "etc" / "passwd").generic_string(), "t"));
        CHECK(!ScriptAssetPathOk("//server/share/p.okayprefab", "t"));
        CHECK(!ScriptAssetPathOk("\\\\?\\C:\\x", "t"));
        CHECK(!ScriptAssetPathOk(inTemp + ":stream", "t"));
        CHECK(!ScriptAssetPathOk((root / "game" / "NUL").generic_string(), "t"));
        CHECK(!ScriptAssetPathOk("C:/Windows/x.okayprefab", "t"));       // a drive path means nothing on this system
        CHECK(!ScriptAssetPathOk("../x.okayprefab", "t"));
    }

    // ---- prefs_save / prefs_load ----
    CHECK(!Run(R"(var r = prefs_save("../x");)", "r").AsBool());
    CHECK(!fs::exists(root / "x"));
    CHECK(!Run(R"(var r = prefs_save("/tmp/okay_abs_prefs");)", "r").AsBool());
    CHECK(!Run(R"(var r = prefs_load("../../x");)", "r").AsBool());
    CHECK(Run(R"(prefs_set("hi", 7); var r = prefs_save();)", "r").AsBool());
    CHECK(fs::exists(root / "game" / "game.okayprefs"));     // no save folder set: beside the game, as before

    // ---- save(...) with a file argument ----
    CHECK(!Run(R"(var r = save("coins", 5, "../stolen.okaysave");)", "r").AsBool());
    CHECK(!fs::exists(root / "stolen.okaysave"));
    CHECK(Run(R"(var r = save("coins", 5, "slot1.okaysave");)", "r").AsBool());
    CHECK_NEAR(Run(R"(var r = load("coins", 0, "slot1.okaysave");)", "r").AsFloat(), 5.0f, 1e-4f);
    CHECK_NEAR(Run(R"(var r = load("coins", 9, "../slot1.okaysave");)", "r").AsFloat(), 9.0f, 1e-4f);   // refused -> default
    CHECK(!Run(R"(var r = save_exists("/etc/hosts");)", "r").AsBool());
    CHECK(!Run(R"(var r = save_delete_file("../game/slot1.okaysave");)", "r").AsBool());
    CHECK(fs::exists(root / "game" / "slot1.okaysave"));

    // ---- data assets ----
    {
        std::ofstream(root / "secret.okaydata") << "key=leaked\n";
        CHECK(Run(R"(var r = data_str("../secret.okaydata", "key", "none");)", "r").AsString() == "none");
        CHECK(!Run(R"(var r = data_save("../out.okaydata");)", "r").AsBool());
        CHECK(!fs::exists(root / "out.okaydata"));
        std::ofstream(root / "game" / "items.okaydata") << "sword=12\n";
        CHECK_NEAR(Run(R"(var r = data_num("items.okaydata", "sword", 0);)", "r").AsFloat(), 12.0f, 1e-4f);
    }

    // ---- Build Mode creations ----
    CHECK(!Run(R"(var r = build_save("../../evil");)", "r").AsBool());
    CHECK(!Run(R"(var r = build_load("/abs/creation");)", "r").AsBool());

    // ---- With a save folder: writes go there, old files beside the game still load ----
    {
        fs::create_directories(root / "userdata");
        Save::SetBaseDir((root / "userdata").string());
        // An old prefs file beside the game (written before the save folder existed).
        CHECK(Run(R"(prefs_set("legacy", 3); var r = prefs_save("old.okayprefs");)", "r").AsBool());
        CHECK(fs::exists(root / "userdata" / "old.okayprefs"));
        fs::rename(root / "userdata" / "old.okayprefs", root / "game" / "old.okayprefs");
        Prefs::Clear();
        CHECK(Run(R"(var r = prefs_load("old.okayprefs");)", "r").AsBool());   // found beside the game
        CHECK_NEAR(Prefs::GetFloat("legacy", 0.0f), 3.0f, 1e-4f);
        CHECK(Run(R"(var r = prefs_save("new.okayprefs");)", "r").AsBool());
        CHECK(fs::exists(root / "userdata" / "new.okayprefs"));
        CHECK(!fs::exists(root / "game" / "new.okayprefs"));
        CHECK(!Run(R"(var r = prefs_save("../escape.okayprefs");)", "r").AsBool());
        CHECK(!fs::exists(root / "escape.okayprefs"));
        Save::SetBaseDir("");
    }

    // ---- Scene snapshot slots need a scene: a ScriptComponent in a real scene ----
    {
        Scene scene("S"); scene.physicsEnabled = false;
        GameObject* go = scene.CreateGameObject("Saver");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        CHECK(sc->LoadSource(R"(
            var bad = 0; var good = 0;
            function start() { bad = save_game("../../pwned"); good = save_game("slot_a"); }
        )", &err));
        scene.Start();
        scene.Update(0.016f);
        CHECK_NEAR(sc->VM()->GetGlobal("bad").AsFloat(), 0.0f, 1e-4f);
        CHECK_NEAR(sc->VM()->GetGlobal("good").AsFloat(), 1.0f, 1e-4f);
        CHECK(!fs::exists(root / "pwned.okaygame") && !fs::exists(root / "pwned.okayslot"));
        CHECK(fs::exists(root / "game" / "saves" / "slot_a.okayslot"));   // SaveGame writes the framework's slot files
    }

    // ---- Every scene loader and the action-list file ops refuse outside paths ----
    {
        Scene m("m"); m.CreateGameObject("OUTSIDE");
        SceneSerializer::SaveToFile(m, (root / "outside.okayscene").string());
        auto loads = [&](const std::string& call) {
            Scene sc("L");
            auto* c = sc.CreateGameObject("o")->AddComponent<ScriptComponent>("okayscript");
            std::string e;
            if (!c->LoadSource("function start() { " + call + "; }", &e)) return true;   // treat as a failure
            sc.Start();
            for (int i = 0; i < 4; ++i) sc.Update(1.0f / 60.0f);
            return sc.Find("OUTSIDE") != nullptr;
        };
        const std::string abs = (root / "outside.okayscene").generic_string();
        CHECK(!loads(R"(load_scene("../outside.okayscene"))"));
        CHECK(!loads(R"(load_scene_name("../outside.okayscene"))"));
        CHECK(!loads("load_scene_name(\"" + abs + "\")"));
        CHECK(!loads(R"(SceneManager.LoadScene("../outside.okayscene"))"));
        CHECK(!loads(R"(load_scene_additive("../outside.okayscene"))"));
        // A scene inside the game still loads by path.
        SceneSerializer::SaveToFile(m, (root / "game" / "inside.okayscene").string());
        CHECK(loads(R"(load_scene_name("inside.okayscene"))"));

        auto act = [&](std::vector<ActionList::Item> items) {
            Scene sc("A");
            auto* al = sc.CreateGameObject("o")->AddComponent<ActionList>();
            al->trigger = ActionList::Trigger::OnStart;
            al->instructions = std::move(items);
            sc.Start();
            for (int i = 0; i < 4; ++i) sc.Update(1.0f / 60.0f);
            return sc.Find("OUTSIDE") != nullptr;
        };
        CHECK(!act({{"load_scene", {"../outside.okayscene"}}}));
        CHECK(!act({{"load_scene", {abs}}}));
        act({{"set_prefs", {"k", "1"}}, {"save_prefs", {"../escaped.okayprefs"}},
             {"save_prefs", {(root / "escaped_abs.okayprefs").string()}}, {"save_prefs", {"al.okayprefs"}}});
        CHECK(!fs::exists(root / "escaped.okayprefs"));
        CHECK(!fs::exists(root / "escaped_abs.okayprefs"));
        CHECK(fs::exists(root / "game" / "al.okayprefs"));     // no save folder set: beside the game
    }

    fs::current_path(oldCwd);
    std::error_code ec; fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
