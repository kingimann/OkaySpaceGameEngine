#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>

using namespace okay;

int main() {
    RUN_SUITE("script_lang3");

    // --- foreach over an array literal ---
    {
        const char* src = R"SCRIPT(
            var total = 0;
            for x in [3, 4, 5] { total = total + x; }   // 12
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("total").AsFloat(), 12.0f, 0.001f);
    }

    // --- foreach with break/continue ---
    {
        const char* src = R"SCRIPT(
            var s = 0;
            for v in [1, 2, 3, 4, 5, 6] {
                if (v == 2) { continue; }
                if (v == 5) { break; }
                s = s + v;     // 1 + 3 + 4 = 8
            }
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("s").AsFloat(), 8.0f, 0.001f);
    }

    // --- foreach over a variable holding an array ---
    {
        const char* src = R"SCRIPT(
            var names = ["a", "bb", "ccc"];
            var chars = 0;
            for n in names { chars = chars + str_len(n); }  // 1+2+3 = 6
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("chars").AsFloat(), 6.0f, 0.001f);
    }

    // --- ternary operator ---
    {
        const char* src = R"SCRIPT(
            var hp = 10;
            var status = hp > 0 ? "alive" : "dead";
            var big = (5 > 3 ? 100 : 1) + (1 > 2 ? 100 : 2);  // 100 + 2 = 102
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK(vm->GetGlobal("status").AsString() == "alive");
        CHECK_NEAR(vm->GetGlobal("big").AsFloat(), 102.0f, 0.001f);
    }

    // --- "for" still works in the classic C-style form ---
    {
        const char* src = R"SCRIPT(
            var s = 0;
            for (var i = 0; i < 5; i = i + 1) { s = s + i; }  // 10
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("s").AsFloat(), 10.0f, 0.001f);
    }

    // --- a script's own function wins over a builtin with the same name ---
    // The language core added about 34 builtins (sort, zip, clock, ...). Builtins used to be found first, so a game
    // script's own `sort` was silently replaced by the library's (its `sort(a, b, c)` returned 1, not "mine").
    {
        const char* names[] = {"sort", "zip", "flatten", "set_of", "set_add", "set_has", "set_union", "set_diff",
                               "set_intersect", "set_remove", "chr", "ord", "long", "int64", "trunc", "cbrt", "log2",
                               "log10", "is_int", "enumerate", "date_text", "base64_encode", "base64_decode",
                               "hex_encode", "hex_decode", "url_encode", "url_decode", "utf8_len", "utf8_chars", "now",
                               "now_ms", "clock", "rand_seed",
                               // and the engine's older ones
                               "move", "print", "abs", "len", "to_json"};
        for (const char* n : names) {
            const std::string src = std::string("function ") + n + "(a, b, c) { return \"mine\"; }\nvar r = " + n + "(3, 1, 2);\n";
            auto vm = CreateScriptVM("okayscript");
            std::string err;
            CHECK(vm->Load(src, &err));
            const std::string got = vm->GetGlobal("r").AsString();
            if (got != "mine") std::printf("    %s(...) gave \"%s\" (%s)\n", n, got.c_str(), err.c_str());
            CHECK(got == "mine");
        }
        // Without a script function of that name the builtin still answers; a class's own method wins in it too.
        const char* src = R"SCRIPT(
            var s = to_json(sort([3, 1, 2]));
            class Box { public string clock() { return "box"; } public string Go() { return clock(); } }
            var b = new Box().Go();
            var c = typeof(clock());
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK(vm->GetGlobal("s").AsString() == "[1,2,3]");
        CHECK(vm->GetGlobal("b").AsString() == "box");
        CHECK(vm->GetGlobal("c").AsString() == "number");
    }

    TEST_MAIN_RESULT();
}
