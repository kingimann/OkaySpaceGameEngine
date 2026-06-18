#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("script_maps");

    // --- Maps: set/get/has/remove/keys/count ---
    {
        const char* src = R"SCRIPT(
            var m = map();
            map_set(m, "hp", 100);
            map_set(m, "name", "Hero");
            var hp = map_get(m, "hp");          // 100
            var nm = map_get(m, "name");         // Hero
            var miss = map_get(m, "mana", -1);   // -1 (default)
            var has = map_has(m, "hp");          // true
            var n = map_count(m);                // 2
            map_remove(m, "hp");
            var n2 = map_count(m);               // 1
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("hp").AsFloat(), 100.0f, 0.001f);
        CHECK(vm->GetGlobal("nm").AsString() == "Hero");
        CHECK_NEAR(vm->GetGlobal("miss").AsFloat(), -1.0f, 0.001f);
        CHECK(vm->GetGlobal("has").AsBool());
        CHECK_NEAR(vm->GetGlobal("n").AsFloat(), 2.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("n2").AsFloat(), 1.0f, 0.001f);
    }

    // --- map_keys returns an array you can iterate ---
    {
        const char* src = R"SCRIPT(
            var m = map();
            map_set(m, "a", 1);
            map_set(m, "b", 2);
            map_set(m, "c", 3);
            var keys = map_keys(m);
            var total = 0;
            for (var i = 0; i < count(keys); i = i + 1) { total = total + map_get(m, keys[i]); }
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("total").AsFloat(), 6.0f, 0.001f);
    }

    // --- Maps share by reference ---
    {
        const char* src = R"SCRIPT(
            var m = map();
            var n = m;
            map_set(n, "x", 5);
            var got = map_get(m, "x");
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("got").AsFloat(), 5.0f, 0.001f);
    }

    // --- split / join round-trip ---
    {
        const char* src = R"SCRIPT(
            var parts = split("a,b,c", ",");   // ["a","b","c"]
            var n = count(parts);              // 3
            var mid = parts[1];                // b
            var joined = join(parts, "-");     // a-b-c
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("n").AsFloat(), 3.0f, 0.001f);
        CHECK(vm->GetGlobal("mid").AsString() == "b");
        CHECK(vm->GetGlobal("joined").AsString() == "a-b-c");
    }

    TEST_MAIN_RESULT();
}
