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
    ++tests;
    build(t, "std::function, shared_ptr, weak_ptr, map", "#include <functional>\n#include <memory>\n#include <map>\n#include <string>\n"
          "struct Base { virtual int id() const { return 1; } virtual ~Base() {} };\nstruct Der : Base { int k; Der(int x) : k(x) {} int id() const override { return k; } };\n"
          "struct Ev { int a; };\nint twice(int x) { return 2 * x; }\n"
          "int main() { int k = 3, cnt = 0; std::function<int(int)> f = [k](int x) { return x + k; }; std::function<int(int)> g = twice;\n"
          "std::function<void(const Ev&)> sink = [&cnt](const Ev& e) { cnt = e.a; }; Ev e; e.a = 4; sink(e);\n"
          "std::shared_ptr<Der> a = std::make_shared<Der>(7); std::shared_ptr<Base> b = a; std::weak_ptr<Der> w = a;\n"
          "std::map<std::string, int> m; m[\"b\"] = 2; return f(1) + g(2) + cnt + b->id() + (w.expired() ? 1 : 0) + m[\"b\"]; }\n", &o);
    ++tests;
    build(t, "cmath additions, atomic_flag, system_error, vector::emplace_back, getenv",
          "#include <cmath>\n#include <atomic>\n#include <cstdlib>\n#include <vector>\n#include <system_error>\nstruct P { int a; P() : a(1) {} P(int x) : a(x) {} };\n"
          "int main() { std::atomic<int> x(1); int e = 1; x.compare_exchange_weak(e, 2, std::memory_order_acquire, std::memory_order_relaxed);\n"
          "std::atomic_flag fl = ATOMIC_FLAG_INIT; fl.test_and_set(); std::vector<P> v; v.emplace_back(); v.emplace_back(5); std::error_code ec;\n"
          "double d = std::lgamma(5.0) + std::log1p(0.5) + std::fmax(1.0, 2.0) + std::nextafter(1.0, 2.0); const char* g = std::getenv(\"HOME\");\n"
          "return (int)d + (g ? 1 : 0) + (ec ? 1 : 0); }\n", &o);
    ++tests;
    build(t, "std::filesystem path", "#include <filesystem>\nint main() { namespace fs = std::filesystem; fs::path p = fs::u8path(\"C:/a/./b/../c.txt\"); fs::path n = p.lexically_normal();\n"
          "std::error_code ec; fs::path q = fs::weakly_canonical(fs::absolute(p, ec), ec); fs::path base = n.parent_path() / \"x\";\n"
          "return (n.is_absolute() ? 1 : 0) + (int)n.extension().string().size() + (base == q ? 1 : 0) + (fs::is_directory(p, ec) ? 1 : 0); }\n", &o);
    CHECK(t, has_symbol(o, "lexically_normal"));
    ++tests;
    build(t, "range for over a class with an iterator class", "struct It { int* p; bool operator!=(const It& o) const { return p != o.p; } It& operator++() { ++p; return *this; } int& operator*() const { return *p; } };\n"
          "struct C { int a[3]; It begin() { It i; i.p = a; return i; } It end() { It i; i.p = a + 3; return i; } };\n"
          "int main() { C c; c.a[0] = 1; c.a[1] = 2; c.a[2] = 3; int s = 0; for (int x : c) s += x; for (auto& y : c) y = y * 2; return s + c.a[2]; }\n", &o);
    CHECK(t, has_symbol(o, "??9It@@") && has_symbol(o, "??EIt@@") && has_symbol(o, "??DIt@@"));
    ++tests;
    build(t, "tuple, unordered_map with a hash, sstream, fstream", "#include <tuple>\n#include <map>\n#include <unordered_map>\n#include <sstream>\n#include <fstream>\n#include <string>\n"
          "struct Cell { int x, y; bool operator==(const Cell& o) const { return x == o.x && y == o.y; } };\n"
          "struct CellHash { size_t operator()(const Cell& c) const { return (size_t)(c.x * 73856093) ^ (size_t)(c.y * 19349663); } };\n"
          "int main() { std::map<std::tuple<long, long, long>, int> m; std::tuple<long, long, long> c = std::make_tuple(1L, 2L, 3L); m[c] = 5;\n"
          "std::unordered_map<Cell, int, CellHash> grid; Cell k; k.x = 1; k.y = 2; grid[k] = 5; std::unordered_map<long long, int> cells; cells[7] = 1;\n"
          "std::ifstream mf(\"a.mtl\"); std::string ml; int n = 0; while (mf && std::getline(mf, ml)) { std::istringstream ms(ml); std::string mt; ms >> mt; float r; if (ms >> r) n += (int)r; }\n"
          "std::ofstream f(\"o.obj\"); f << \"v \" << 1.5f << \" \" << n << \"\\n\"; std::ostringstream o; o << \"x\" << 3;\n"
          "return std::get<1>(c) + grid[k] + cells[7] + (int)o.str().size(); }\n", &o);
    return t.finish(tests);
}
