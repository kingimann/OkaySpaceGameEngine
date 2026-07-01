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

    // --- Validate: side-effect-free syntax check for live editor diagnostics ---
    {
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        // Valid code passes with no error.
        CHECK(vm->Validate("function f(n) { return n + 1; }\nvar x = f(2);", &err));
        CHECK(err.empty());
        // A syntax error fails and reports a line.
        err.clear();
        CHECK(!vm->Validate("function bad( { return 1; }", &err));
        CHECK(!err.empty());
        // Validate does NOT execute: a global assigned at top level is not created
        // (Load would create it; Validate only parses).
        auto vm2 = CreateScriptVM("okayscript");
        vm2->Validate("var sideEffect = 123;", nullptr);
        CHECK(vm2->GetGlobal("sideEffect").AsFloat() == 0.0f);   // never ran → unset
        // ...whereas Load DOES run it.
        vm2->Load("var sideEffect = 123;", nullptr);
        CHECK_NEAR(vm2->GetGlobal("sideEffect").AsFloat(), 123.0f, 1e-4f);
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

    // --- for loop: sum 0..9 = 45 ---
    {
        const char* src = R"SCRIPT(
            var sum = 0;
            for (var i = 0; i < 10; i = i + 1) { sum = sum + i; }
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("sum").AsFloat(), 45.0f, 0.001f);
    }

    // --- compound assignment operators ---
    {
        const char* src = R"SCRIPT(
            var x = 10;
            x += 5;   // 15
            x -= 3;   // 12
            x *= 2;   // 24
            x /= 4;   // 6
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("x").AsFloat(), 6.0f, 0.001f);
    }

    // --- string concatenation and string equality ---
    {
        const char* src = R"SCRIPT(
            var greeting = "hello" + " " + "world";
            var same = ("ab" == "ab");
            var diff = ("ab" != "cd");
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK(vm->GetGlobal("greeting").AsString() == "hello world");
        CHECK(vm->GetGlobal("same").AsBool());
        CHECK(vm->GetGlobal("diff").AsBool());
    }

    // --- new math builtins: clamp, lerp, pow, sign, round ---
    {
        const char* src = R"SCRIPT(
            var a = clamp(15, 0, 10);   // 10
            var b = lerp(0, 10, 0.5);   // 5
            var c = pow(2, 8);          // 256
            var d = sign(-3) + round(2.6);  // -1 + 3 = 2
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("a").AsFloat(), 10.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("b").AsFloat(), 5.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("c").AsFloat(), 256.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("d").AsFloat(), 2.0f, 0.001f);
    }

    // --- for loop driving a Transform via set_x ---
    {
        Scene scene("ForDrive");
        GameObject* go = scene.CreateGameObject("Mover");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        const char* src = R"SCRIPT(
            function start() {
                var total = 0;
                for (var i = 1; i <= 4; i += 1) { total += i; }  // 10
                set_x(total);
            }
        )SCRIPT";
        std::string err;
        CHECK(sc->LoadSource(src, &err));
        scene.Start();
        CHECK_NEAR(go->transform->localPosition.x, 10.0f, 0.001f);
    }

    TEST_MAIN_RESULT();
}
