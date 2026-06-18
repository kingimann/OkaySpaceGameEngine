#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

static float Num(const char* src, const char* global) {
    auto vm = CreateScriptVM("okayscript");
    std::string err;
    if (!vm->Load(src, &err)) { std::cerr << "  load error: " << err << "\n"; return -99999.0f; }
    return vm->GetGlobal(global).AsFloat();
}

int main() {
    RUN_SUITE("script_lang2");

    // --- break exits the loop early ---
    CHECK_NEAR(Num(
        "var s = 0;\n"
        "for (var i = 0; i < 100; i = i + 1) { if (i == 5) { break; } s = s + 1; }\n",
        "s"), 5.0f, 0.001f);

    // --- continue skips the rest of the iteration (sum of odds 1..9) ---
    CHECK_NEAR(Num(
        "var s = 0;\n"
        "for (var i = 0; i < 10; i = i + 1) { if (i % 2 == 0) { continue; } s = s + i; }\n",
        "s"), 25.0f, 0.001f); // 1+3+5+7+9

    // --- break works in while too ---
    CHECK_NEAR(Num(
        "var i = 0;\n"
        "while (true) { i = i + 1; if (i >= 7) { break; } }\n",
        "i"), 7.0f, 0.001f);

    // --- string builtins ---
    {
        const char* src = R"SCRIPT(
            var name = "Hero";
            var L = str_len(name);          // 4
            var up = upper(name);           // HERO
            var lo = lower("ABC");          // abc
            var sub = substr("platformer", 0, 4); // plat
            var idx = str_find("abcdef", "cd");    // 2
            var ch = char_at("xyz", 1);     // y
            var n = to_num("42") + 1;       // 43
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("L").AsFloat(), 4.0f, 0.001f);
        CHECK(vm->GetGlobal("up").AsString() == "HERO");
        CHECK(vm->GetGlobal("lo").AsString() == "abc");
        CHECK(vm->GetGlobal("sub").AsString() == "plat");
        CHECK_NEAR(vm->GetGlobal("idx").AsFloat(), 2.0f, 0.001f);
        CHECK(vm->GetGlobal("ch").AsString() == "y");
        CHECK_NEAR(vm->GetGlobal("n").AsFloat(), 43.0f, 0.001f);
    }

    // --- array query builtins: contains / index_of / remove_at ---
    {
        const char* src = R"SCRIPT(
            var a = [10, 20, 30];
            var has20 = contains(a, 20);   // true
            var has99 = contains(a, 99);   // false
            var at = index_of(a, 30);      // 2
            remove_at(a, 0);               // -> [20, 30]
            var first = a[0];              // 20
            var n = count(a);              // 2
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK(vm->GetGlobal("has20").AsBool());
        CHECK(!vm->GetGlobal("has99").AsBool());
        CHECK_NEAR(vm->GetGlobal("at").AsFloat(), 2.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("first").AsFloat(), 20.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("n").AsFloat(), 2.0f, 0.001f);
    }

    TEST_MAIN_RESULT();
}
