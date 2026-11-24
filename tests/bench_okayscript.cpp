// OkayScript hot loops through the engine's OkayScriptVM (the code every game script runs on).
//   bench_okayscript arith|calls|deep|list|text
// Each case is one Load (the loop runs in the script's top level). Run it under
//   valgrind --tool=callgrind --toggle-collect='*OkayScriptVM::Load*'
// to count the instructions of the script alone; tests/okayscript_bench.cmake does that and compares with
// tests/okayscript_bench_baseline.txt (the okayscript_bench test).
#include <Okay.hpp>
#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    const char* which = argc > 1 ? argv[1] : "calls";
    std::string src;
    if (!std::strcmp(which, "arith")) {
        src = "var s = 0\nfor (var i = 0; i < 100000; i++) { s = s + i * 2 - 1; }\n";
    } else if (!std::strcmp(which, "calls")) {
        src = "function f(a) { return a + 1; }\nvar s = 0\nfor (var i = 0; i < 50000; i++) { s = f(s); }\n";
    } else if (!std::strcmp(which, "deep")) {
        // A global read from 50 calls deep: every read searches the whole scope stack.
        src = "var g = 1\nvar s = 0\nfunction d(n) {\n  if (n > 0) return d(n - 1);\n"
              "  for (var i = 0; i < 20000; i++) s = s + g;\n  return s;\n}\nd(50)\n";
    } else if (!std::strcmp(which, "list")) {
        // List work: append, index reads and writes, then a foreach over the list.
        src = "var xs = []\nfor (var i = 0; i < 20000; i++) { xs.Add(i); }\n"
              "for (var i = 0; i < 20000; i++) { xs[i] = xs[i] * 2; }\n"
              "var s = 0\nforeach (var x in xs) { s = s + x; }\n";
    } else if (!std::strcmp(which, "text")) {
        // Building a string piece by piece, numbers mixed in.
        src = "var t = \"\"\nfor (var i = 0; i < 20000; i++) { t = t + \"a\" + (i % 10); }\nvar s = t.Length\n";
    } else {
        std::fprintf(stderr, "usage: bench_okayscript arith|calls|deep|list|text\n");
        return 2;
    }
    auto vm = okay::CreateScriptVM("okayscript");
    std::string err;
    if (!vm || !vm->Load(src, &err)) { std::fprintf(stderr, "load failed: %s\n", err.c_str()); return 1; }
    std::printf("%s: s = %s\n", which, vm->GetGlobal("s").AsString().c_str());
    return 0;
}
