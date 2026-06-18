#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("script_util");

    // --- sum / min_of / max_of / reverse / sort_num ---
    {
        const char* src = R"SCRIPT(
            var a = [3, 1, 4, 1, 5];
            var total = sum(a);     // 14
            var lo = min_of(a);     // 1
            var hi = max_of(a);     // 5
            sort_num(a);            // [1,1,3,4,5]
            var first = a[0];       // 1
            var last = a[4];        // 5
            reverse(a);             // [5,4,3,1,1]
            var top = a[0];         // 5
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("total").AsFloat(), 14.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("lo").AsFloat(), 1.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("hi").AsFloat(), 5.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("first").AsFloat(), 1.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("last").AsFloat(), 5.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("top").AsFloat(), 5.0f, 0.001f);
    }

    // --- move_toward steps toward a target without overshooting ---
    {
        const char* src = R"SCRIPT(
            var a = move_toward(0, 10, 3);    // 3
            var b = move_toward(9, 10, 3);    // 10 (clamped to target)
            var c = move_toward(10, 0, 4);    // 6
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("a").AsFloat(), 3.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("b").AsFloat(), 10.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("c").AsFloat(), 6.0f, 0.001f);
    }

    // --- choose / randi / shuffle stay within bounds ---
    {
        const char* src = R"SCRIPT(
            var opts = [10, 20, 30];
            var pick = choose(opts);          // one of 10/20/30
            var r = randi(5, 5);              // always 5
            shuffle(opts);
            var n = count(opts);             // still 3
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        float pick = vm->GetGlobal("pick").AsFloat();
        CHECK(pick == 10.0f || pick == 20.0f || pick == 30.0f);
        CHECK_NEAR(vm->GetGlobal("r").AsFloat(), 5.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("n").AsFloat(), 3.0f, 0.001f);
    }

    TEST_MAIN_RESULT();
}
