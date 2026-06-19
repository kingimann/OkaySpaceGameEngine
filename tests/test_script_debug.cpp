#include "test_framework.hpp"
#include <Okay.hpp>
#include <vector>
#include <string>

using namespace okay;

int main() {
    RUN_SUITE("script_debug");

    // --- The log sink captures script print/log/debug output -----------
    {
        std::vector<std::string> captured;
        Log::Level worst = Log::Level::Trace;
        Log::sink = [&](Log::Level lvl, const std::string& msg) {
            captured.push_back(msg);
            if (lvl > worst) worst = lvl;
        };

        Scene s("D"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Logic");
        go->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "function start() {\n"
            "  print(\"hello\", 42);\n"
            "  log_warn(\"careful\");\n"
            "  watch(\"hp\", 7);\n"
            "  assert(0, \"boom\");\n"
            "}\n");
        s.Start();

        auto has = [&](const std::string& sub) {
            for (auto& m : captured) if (m.find(sub) != std::string::npos) return true;
            return false;
        };
        CHECK(has("hello 42"));     // print joined args
        CHECK(has("careful"));      // log_warn
        CHECK(has("hp = 7"));       // watch
        CHECK(has("failed"));       // assert(0,...) logged a failure
        CHECK(worst == Log::Level::Error);   // the failed assert was an error

        Log::sink = nullptr;        // detach
    }

    // --- New helper functions return the expected values ---------------
    {
        Scene s("H"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Logic");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        sc->LoadSource(
            "var a = approach(0, 10, 3);\n"      // 3
            "var b = remap(5, 0, 10, 0, 100);\n" // 50
            "var c = format(\"hp={} of {}\", 7, 9);\n"
            "var d = mod(-1, 5);\n"              // 4
            "var e = snap(7.3, 0.5);\n"          // 7.5
            "var f = max3(2, 9, 4);\n"           // 9
            "var g = str_repeat(\"ab\", 3);\n"   // "ababab"
            "function start() {}\n");
        s.Start();
        auto* vm = sc->VM();
        CHECK_NEAR(vm->GetGlobal("a").AsFloat(), 3.0f, 1e-4f);
        CHECK_NEAR(vm->GetGlobal("b").AsFloat(), 50.0f, 1e-3f);
        CHECK(vm->GetGlobal("c").AsString() == "hp=7 of 9");
        CHECK_NEAR(vm->GetGlobal("d").AsFloat(), 4.0f, 1e-4f);
        CHECK_NEAR(vm->GetGlobal("e").AsFloat(), 7.5f, 1e-4f);
        CHECK_NEAR(vm->GetGlobal("f").AsFloat(), 9.0f, 1e-4f);
        CHECK(vm->GetGlobal("g").AsString() == "ababab");
    }

    // --- Friendly aliases call the same builtins -----------------------
    {
        Scene s("A"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Logic");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        sc->LoadSource(
            "var s = to_string(123);\n"     // alias of to_str
            "var n = num(\"4.5\");\n"        // alias of to_num
            "var d = distance(0, 0, 3, 4);\n" // alias of dist -> 5
            "function start() {}\n");
        s.Start();
        auto* vm = sc->VM();
        CHECK(vm->GetGlobal("s").AsString() == "123");
        CHECK_NEAR(vm->GetGlobal("n").AsFloat(), 4.5f, 1e-4f);
        CHECK_NEAR(vm->GetGlobal("d").AsFloat(), 5.0f, 1e-3f);
    }

    TEST_MAIN_RESULT();
}
