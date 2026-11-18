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
    ++tests;
    build(t, "reference members, forward declarations, class enums, nested statics",
          "struct Later;\nstruct Later { int v; };\nstruct Node;\nstruct Holder { Node* n; int k; };\nstruct Node { int v; Holder* h; };\n"
          "struct P { const int& a; int& b; explicit P(int& x) : a(x), b(x) {} void inc() { b = b + 1; } };\n"
          "struct Input { enum Plain { Pa, Pb }; enum class PF { A, B }; static int Get() { return 1; } };\n"
          "int fe(Input::Plain p) { return p; }\nint fp(Input::PF p) { return p == Input::PF::B; }\n"
          "class R { public: static int g; static void Lock() { ++g; } struct Guard { Guard() { Lock(); R::Lock(); } }; };\nint R::g = 0;\n"
          "int main() { Later l; l.v = 3; Node n; Holder h; h.n = &n; int v = 5; P p(v); p.inc(); R::Guard gg; return fe(Input::Pb) + fp(Input::PF::B) + l.v; }\n", &o);
    ++tests;
    build(t, "extern variables", "extern int counter_ext;\nextern \"C\" int c_counter;\nint main() { return counter_ext + c_counter; }\n", &o);
    bool undefined_ext = false, undefined_c = false;
    for (const auto& sym : o.symbols) {
        if (sym.name == "counter_ext" && sym.section == 0) undefined_ext = true;
        if (sym.name == "c_counter" && sym.section == 0) undefined_c = true;
    }
    CHECK(t, undefined_ext && undefined_c);
    ++tests;
    build(t, "while with a declaration", "struct Tok { int v; Tok(int x) : v(x) {} ~Tok() {} operator bool() const { return v != 0; } };\n"
          "int nv() { static int p = 0; static int s[] = {3, 0}; return s[p++]; }\nint main() { int sum = 0; while (int v = nv()) { sum += v; } int z{}; return sum + z; }\n", &o);
    ++tests;
    build(t, "deleted copy with only a move constructor", "struct M { int v; M(int x) : v(x) {} M(M&& o) : v(o.v) {} };\nint main() { M a(3); M b(a); return b.v; }\n", nullptr, false);
    ++tests;
    build(t, "static members with values, class-scope using", "struct T { using Fn = double (*)(); static inline int a = 0, b = 2; static inline float c = 1.5f; static constexpr float PI = 3.14f; static constexpr float D = PI / 2.0f; };\n"
          "struct Vv { int x; static const Vv Zero; };\ninline const Vv Vv::Zero{0};\nint main() { return T::a + T::b + (int)T::c + (int)T::D; }\n", &o);
    return t.finish(tests);
}
