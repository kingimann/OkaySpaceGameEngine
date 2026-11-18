// Round 23, compile-only (unverified: not run under Wine, Wine was broken in the container this was written in).
// Each program must compile with okay-cl, and some object properties are checked (symbols, calls). The same programs
// should be run under Wine and compared with MinGW g++ once Wine works (see docs/own_msvc_plan.md, Round 23).
#include "check.hpp"
#include "okayms/coff.hpp"
#include "okayms/compile.hpp"

#include <string>

namespace {
using okayms_test::Tally;

bool has_symbol(const okayms::ObjectFile& o, const std::string& part) {
    for (const auto& s : o.symbols)
        if (s.name.find(part) != std::string::npos) return true;
    return false;
}

// Compiles `source`; returns true when it compiled. The object goes to *obj.
bool build(Tally& t, const char* name, const std::string& source, okayms::ObjectFile* obj, bool expect_ok = true) {
    std::printf("r23 %s\n", name);
    okayms::CompileInput in;
    in.source = source;
    in.include_paths.push_back("tools/okayms/stdinc");
    in.include_paths.push_back("stdinc");
    in.include_paths.push_back("engine/include");
    const auto r = okayms::compile_c(in);
    CHECK(t, r.ok() == expect_ok);
    if (r.ok() && obj != nullptr) *obj = r.value.object;
    if (r.ok() != expect_ok) std::fprintf(stderr, "  %s: %s\n", name, r.error.c_str());
    return r.ok();
}

}  // namespace

int main(int, char**) {
    Tally t;
    int tests = 0;
    okayms::ObjectFile o;
    ++tests;
    build(t, "delegating constructor", "class R { public: R() : R(5ull) {} explicit R(unsigned long long s) : m(s) {} unsigned long long m; };\nint main() { R r; return (int)r.m; }\n", &o);
    CHECK(t, has_symbol(o, "??0R@@QEAA@XZ"));
    ++tests;
    build(t, "function-local statics", "int f() { return 3; }\nstruct E { const char* name; char code; };\nconstexpr char K(int n) { return (char)(100 + n); }\n"
          "int g() { static int n = f(); n++; return n; }\nconst E* find(int i) { static const E tab[] = {{\"a\", K(1)}, {\"b\", K(2)}}; return &tab[i]; }\n"
          "int main() { return g() + find(1)->code; }\n", &o);
    CHECK(t, has_symbol(o, "_guard"));
    ++tests;
    build(t, "thread_local and template defaults", "struct S { S() {} int v; };\ntemplate <class G> int P(G& g, int z = 5, S c = {}) { return g + z; }\n"
          "int f() { thread_local int tl = 1; return tl; }\nint main() { int q = 1; return P(q) + f(); }\n", &o);
    return t.finish(tests);
}
