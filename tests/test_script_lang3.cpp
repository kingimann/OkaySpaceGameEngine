#include "test_framework.hpp"
#include <Okay.hpp>

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

    TEST_MAIN_RESULT();
}
