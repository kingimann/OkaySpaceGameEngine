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

    // --- repeat / pad_left / pad_right: hostile counts are capped, not a hang or OOM (#719) ---
    {
        const char* src = R"SCRIPT(
            var big = repeat("x", 999999999);        // hangs / ~1GB before the cap
            var neg = repeat("x", -5);               // nothing
            var nan = repeat("x", 0.0 / 0.0);        // NaN count: nothing
            var p1 = pad_left("7", 999999999, "0");  // ~1GB before the cap
            var p2 = pad_right("7", 999999999);
            var ok = repeat("ab", 3);
            var pl = pad_left("7", 3, "0");
            var pr = pad_right("7", 3);
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK(vm->GetGlobal("big").AsString().size() <= (1u << 20));   // capped, truncated
        CHECK(vm->GetGlobal("neg").AsString().empty());
        CHECK(vm->GetGlobal("nan").AsString().empty());
        CHECK(vm->GetGlobal("p1").AsString().size() <= (1u << 20));
        CHECK(vm->GetGlobal("p2").AsString().size() <= (1u << 20));
        CHECK(vm->GetGlobal("ok").AsString() == "ababab");
        CHECK(vm->GetGlobal("pl").AsString() == "007");
        CHECK(vm->GetGlobal("pr").AsString() == "7  ");
    }

    // --- format(): a huge index must be an empty field, not a thrown out_of_range (#719) ---
    {
        const char* src = R"SCRIPT(
            var a = format("hp={0} of {1}", 7, 9);
            var b = format("{99999999999999}!", 1);   // std::stoi threw here
            var c = format("{2147483648} / {1}", 1, 2);
            var d = format("hp={} of {}", 7, 9);
            var e = format("x={5}", 1);               // valid index, out of range
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK(vm->GetGlobal("a").AsString() == "hp=7 of 9");
        CHECK(vm->GetGlobal("b").AsString() == "!");
        CHECK(vm->GetGlobal("c").AsString() == " / 2");
        CHECK(vm->GetGlobal("d").AsString() == "hp=7 of 9");
        CHECK(vm->GetGlobal("e").AsString() == "x=");
    }

    TEST_MAIN_RESULT();
}
