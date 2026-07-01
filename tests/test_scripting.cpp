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

    // --- ValidateAll: report EVERY syntax error via statement-level recovery ---
    {
        auto vm = CreateScriptVM("okayscript");
        // Clean source → no diagnostics.
        CHECK(vm->ValidateAll("var a = 1;\nvar b = 2;\nfunction f() { return a + b; }").empty());
        // Two independent broken statements → recovery finds BOTH, not just the first.
        auto diags = vm->ValidateAll("var a = ;\nvar b = 2;\nvar c = ;\n");
        CHECK(diags.size() >= 2);
        // Diagnostics carry a source line so the editor can jump to each.
        bool anyLine = false;
        for (auto& d : diags) if (d.line > 0) anyLine = true;
        CHECK(anyLine);
        // A later broken function after a good statement is still reached (recovery works).
        auto diags2 = vm->ValidateAll("function bad( { }\nvar ok = 5;\nfunction alsoBad( { }");
        CHECK(diags2.size() >= 2);
    }

    // --- BuiltinNames: the editor pulls the full builtin set for autocomplete ---
    {
        auto vm = CreateScriptVM("okayscript");
        auto names = vm->BuiltinNames();
        CHECK(names.size() > 100);                       // hundreds of builtins
        auto has = [&](const char* n){ for (auto& s : names) if (s == n) return true; return false; };
        CHECK(has("print") && has("move") && has("spawn") && has("key"));
        // Sorted (the impl sorts), so it's stable for the UI.
        bool sorted = true;
        for (std::size_t i = 1; i < names.size(); ++i) if (names[i] < names[i-1]) sorted = false;
        CHECK(sorted);
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

    // --- New array/map/string/type builtins ---
    {
        const char* src = R"SCRIPT(
            var xs = array(3, 1, 2);
            var f = first(xs);          // 3
            var l = last(xs);           // 2
            sort_num(xs);               // [1,2,3]
            var lo = first(xs);         // 1
            var sl = slice(xs, 1, 3);   // [2,3]
            var sln = count(sl);        // 2
            var r = range(5);           // [0,1,2,3,4]
            var rn = count(r);          // 5
            var rsum = sum(range(1, 5)); // 1+2+3+4 = 10
            insert_at(xs, 0, 99);       // [99,1,2,3]
            var head = first(xs);       // 99
            clear(xs);
            var empty = count(xs);      // 0
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        if (!err.empty()) std::cerr << "  load error: " << err << "\n";
        CHECK_NEAR(vm->GetGlobal("f").AsFloat(), 3.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("l").AsFloat(), 2.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("lo").AsFloat(), 1.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("sln").AsFloat(), 2.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("rn").AsFloat(), 5.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("rsum").AsFloat(), 10.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("head").AsFloat(), 99.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("empty").AsFloat(), 0.0f, 0.001f);
    }
    {
        const char* src = R"SCRIPT(
            var m = map();
            map_set(m, "a", 1);
            map_set(m, "b", 2);
            var vs = map_values(m);
            var vc = count(vs);         // 2
            var vsum = sum(vs);         // 3
            var n = map();
            map_set(n, "b", 20);
            map_set(n, "c", 3);
            map_merge(m, n);            // a=1, b=20, c=3
            var bb = map_get(m, "b");   // 20 (src wins)
            var cc = map_get(m, "c");   // 3
            map_clear(m);
            var mc = map_count(m);      // 0
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        if (!err.empty()) std::cerr << "  load error: " << err << "\n";
        CHECK_NEAR(vm->GetGlobal("vc").AsFloat(), 2.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("vsum").AsFloat(), 3.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("bb").AsFloat(), 20.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("cc").AsFloat(), 3.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("mc").AsFloat(), 0.0f, 0.001f);
    }
    {
        const char* src = R"SCRIPT(
            var a = capitalize("hello");        // "Hello"
            var b = title_case("hi there world"); // "Hi There World"
            var c = str_reverse("abc");         // "cba"
            var d = trim_start("  x");          // "x"
            var e = trim_end("y  ");            // "y"
            var t1 = typeof(array(1));          // "array"
            var t2 = typeof("s");               // "string"
            var t3 = typeof(3);                 // "number"
            var t4 = typeof(true);              // "bool"
            var isa = is_array(array(1));       // true
            var isn = is_num(5);                // true
            var fr = fract(2.75);               // 0.75
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        if (!err.empty()) std::cerr << "  load error: " << err << "\n";
        CHECK(vm->GetGlobal("a").AsString() == "Hello");
        CHECK(vm->GetGlobal("b").AsString() == "Hi There World");
        CHECK(vm->GetGlobal("c").AsString() == "cba");
        CHECK(vm->GetGlobal("d").AsString() == "x");
        CHECK(vm->GetGlobal("e").AsString() == "y");
        CHECK(vm->GetGlobal("t1").AsString() == "array");
        CHECK(vm->GetGlobal("t2").AsString() == "string");
        CHECK(vm->GetGlobal("t3").AsString() == "number");
        CHECK(vm->GetGlobal("t4").AsString() == "bool");
        CHECK(vm->GetGlobal("isa").AsBool());
        CHECK(vm->GetGlobal("isn").AsBool());
        CHECK_NEAR(vm->GetGlobal("fr").AsFloat(), 0.75f, 0.001f);
    }

    TEST_MAIN_RESULT();
}
