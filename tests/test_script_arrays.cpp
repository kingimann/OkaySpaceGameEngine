#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("script_arrays");

    // --- Array literal, indexing, and count ---
    {
        const char* src = R"SCRIPT(
            var a = [10, 20, 30];
            var first = a[0];
            var last  = a[2];
            var n     = count(a);
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("first").AsFloat(), 10.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("last").AsFloat(), 30.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("n").AsFloat(), 3.0f, 0.001f);
    }

    // --- Element assignment and append-at-end ---
    {
        const char* src = R"SCRIPT(
            var a = [1, 2, 3];
            a[1] = 99;       // overwrite
            a[3] = 4;        // append (index == size)
            var s = a[1] + a[3] + count(a);  // 99 + 4 + 4 = 107
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("s").AsFloat(), 107.0f, 0.001f);
    }

    // --- push/pop, and summing via a for loop ---
    {
        const char* src = R"SCRIPT(
            var a = array();
            push(a, 5);
            push(a, 7);
            push(a, 9);
            var total = 0;
            for (var i = 0; i < count(a); i = i + 1) { total = total + a[i]; }
            var popped = pop(a);   // 9
            var after = count(a);  // 2
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("total").AsFloat(), 21.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("popped").AsFloat(), 9.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("after").AsFloat(), 2.0f, 0.001f);
    }

    // --- Arrays share by reference (push through one name is seen by another) ---
    {
        const char* src = R"SCRIPT(
            var a = [1];
            var b = a;
            push(b, 2);
            var n = count(a);  // a sees b's push -> 2
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK_NEAR(vm->GetGlobal("n").AsFloat(), 2.0f, 0.001f);
    }

    // --- Strings can live in arrays ---
    {
        const char* src = R"SCRIPT(
            var names = ["hero", "goblin"];
            var who = names[1];
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK(vm->GetGlobal("who").AsString() == "goblin");
    }

    TEST_MAIN_RESULT();
}
