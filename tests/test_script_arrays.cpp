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

    // --- A callback that changes the array being walked: the walk is over a snapshot ---
    // (push reallocates the vector and clear empties it; walking it live was a use-after-free)
    {
        struct Case { const char* call; float result; float sizeAfter; };
        const Case cases[] = {
            {"var r = count(map_fn(a, \"grow\"));",       3.0f, 6.0f},
            {"var r = count(filter_fn(a, \"grow\"));",    3.0f, 6.0f},
            {"var r = reduce_fn(a, \"fold\", 0);",        6.0f, 6.0f},
            {"var r = count(for_each(a, \"grow\")) ;",    6.0f, 6.0f},   // returns the array itself
            {"var r = find_fn(a, \"never\");",            0.0f, 6.0f},   // not found: null reads as 0
            {"var r = any_fn(a, \"never\");",             0.0f, 6.0f},
            {"var r = all_fn(a, \"grow\");",              1.0f, 6.0f},
            {"var r = count_fn(a, \"grow\");",            3.0f, 6.0f},
            {"var r = count_fn(a, \"wipe\");",            3.0f, 0.0f},   // clear() mid-walk
            {"var r = count(filter_fn(a, \"wipe\"));",    3.0f, 0.0f},
        };
        for (const Case& c : cases) {
            std::string src =
                "var a = [1, 2, 3];\n"
                "function grow(x) { push(a, x); return true; }\n"
                "function never(x) { push(a, x); return false; }\n"
                "function wipe(x) { clear(a); return true; }\n"
                "function fold(acc, x) { push(a, x); return acc + x; }\n";
            src += c.call;
            src += "\nvar n = count(a);\n";
            auto vm = CreateScriptVM("okayscript");
            std::string err;
            CHECK(vm->Load(src, &err));
            CHECK_NEAR(vm->GetGlobal("r").AsFloat(), c.result, 0.001f);
            CHECK_NEAR(vm->GetGlobal("n").AsFloat(), c.sizeAfter, 0.001f);
        }
    }

    // --- Out-of-range numbers where an int is wanted saturate instead of being undefined ---
    {
        const char* src = R"SCRIPT(
            var a = [10, 20, 30];
            var big = 1000000.0 * 1000000.0 * 1000000.0 * 1000000.0;   // 1e24, past any int
            var s1 = substr("hello", 1, big);       // length saturates: the rest of the string
            var s2 = substr("hello", big, 2);       // start past the end: empty
            var s3 = substr("hello", 0 - big, 2);   // start below zero clamps to 0
            var c1 = char_at("hello", big);
            var c2 = char_at("hello", 0 - big);
            var i1 = a[big];
            var i2 = a[0 - big];
            var bits = big & 3;                     // a saturated 64-bit value
        )SCRIPT";
        auto vm = CreateScriptVM("okayscript");
        std::string err;
        CHECK(vm->Load(src, &err));
        CHECK(vm->GetGlobal("s1").AsString() == "ello");
        CHECK(vm->GetGlobal("s2").AsString().empty());
        CHECK(vm->GetGlobal("s3").AsString() == "he");
        CHECK(vm->GetGlobal("c1").AsString().empty());
        CHECK(vm->GetGlobal("c2").AsString().empty());
        CHECK_NEAR(vm->GetGlobal("i1").AsFloat(), 0.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("i2").AsFloat(), 0.0f, 0.001f);
        CHECK_NEAR(vm->GetGlobal("bits").AsFloat(), 3.0f, 0.001f);
    }

    TEST_MAIN_RESULT();
}
