#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("scripting");

    // The built-in backend is always available.
    auto langs = AvailableScriptLanguages();
    bool hasOkay = false;
    for (auto& l : langs) if (l == "okayscript") hasOkay = true;
    CHECK(hasOkay);

    // --- Arithmetic, variables, functions, return ---
    {
        const char* src = R"SCRIPT(
            function square(n) { return n * n; }
            var answer = square(6) + 6;   // 42
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        CHECK(vm != nullptr);
        std::string err;
        bool ok = vm->Load(src, &err);
        CHECK(ok);
        if (!ok) std::cerr << "  load error: " << err << "\n";
        CHECK_NEAR(vm->GetGlobal("answer").AsFloat(), 42.0f, 0.001f);
    }

    // --- Control flow: while loop sum 1..5 = 15 ---
    {
        const char* src = R"SCRIPT(
            var i = 1;
            var sum = 0;
            while (i <= 5) { sum = sum + i; i = i + 1; }
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        CHECK(vm->Load(src));
        CHECK_NEAR(vm->GetGlobal("sum").AsFloat(), 15.0f, 0.001f);
    }

    // --- if/else branching ---
    {
        const char* src = R"SCRIPT(
            var x = 10;
            var label = "";
            if (x > 5) { label = "big"; } else { label = "small"; }
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        CHECK(vm->Load(src));
        CHECK(vm->GetGlobal("label").AsString() == "big");
    }

    // --- Driving a Transform through a ScriptComponent ---
    {
        Scene scene("Scripted");
        GameObject* go = scene.CreateGameObject("Hero");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        const char* src = R"SCRIPT(
            function start() { set_pos(0, 0); }
            function update(d) { move(2 * d, 0); }   // 2 units/sec to the right
        )SCRIPT";
        std::string err;
        bool ok = sc->LoadSource(src, &err);
        CHECK(ok);
        if (!ok) std::cerr << "  load error: " << err << "\n";

        scene.Start();
        for (int i = 0; i < 10; ++i) scene.Update(0.1f); // ~1 second
        CHECK_NEAR(go->transform->localPosition.x, 2.0f, 0.01f);
    }

    TEST_MAIN_RESULT();
}
