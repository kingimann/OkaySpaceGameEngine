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
    ++tests;
    build(t, "in-place construction, function assignment, loop condition temporaries, braced index", "#include <functional>\n#include <map>\n#include <utility>\nstatic int made = 0;\n"
          "struct Tok { int v; Tok(int x) : v(x) { ++made; } ~Tok() {} };\nstruct It { int* p; bool operator!=(const It& o) const { return p != o.p; } };\n"
          "struct C { int a[3]; It end() const { It i; i.p = (int*)a + 3; return i; } };\n"
          "int main() { Tok t = Tok(5); std::function<void()> v; int c = 0; v = [&c]() { c = 1; }; v(); C k; It r; r.p = k.a; int n = 0; for (; r != k.end(); ++r.p) ++n;\n"
          "std::map<std::pair<int, int>, int> m; ++m[{1, 2}]; return t.v + made + n + m.count({1, 2}); }\n", &o);
    CHECK(t, has_symbol(o, "??0Tok@@QEAA@H@Z"));
    ++tests;
    build(t, "generic lambdas, member-wise assignment, class conditional, reverse iterators", "#include <vector>\n#include <deque>\n#include <string>\n#include <algorithm>\n"
          "struct W { std::string s; int k; };\nstruct V { W w; int z; };\nstd::string f(bool c, const std::string& a) { return c ? std::string() : a; }\n"
          "int g(bool c) { int x = 1, y = 2; (c ? x : y)++; return x + y; }\n"
          "int main() { int k = 3; auto add = [k](auto a, const auto& b) { return a + b + k; }; auto id = [](auto x) { return x; };\n"
          "std::vector<int> v; v.push_back(1); std::deque<int> d; d.push_back(2); int s = 0; for (auto it = d.rbegin(); it != d.rend(); ++it) s += *it;\n"
          "W a, b; b = a; V p, q; q = p; return add(1, 2) + id(4) + (int)id(1.5f) + s + (int)f(true, \"ab\").size() + g(false) + (int)std::minmax(1, 2).second; }\n", &o);
    CHECK(t, has_symbol(o, "??4W@@") && has_symbol(o, "??4V@@") && has_symbol(o, "??R__lam1@@QEAAMM@Z"));
    ++tests;
    build(t, "round 26 front end: lambda this capture, unary plus, braced max, chained class assignment, aggregate with reference, string_view, queue",
          "#include <algorithm>\n#include <string_view>\n#include <queue>\n#include <array>\n#include <vector>\nstruct V3 { float x, y, z; };\n"
          "struct M { V3 verts[4]; int n; int helper(int a) { return a + n; }\n"
          "int run(float step) { auto snap = [&](int v) { verts[v] = {step, step, 0}; n = helper(v); }; snap(1); return n; } };\n"
          "int main() { M m; m.n = 1; V3 a, b; a = b = V3{1, 2, 3}; float mx = std::max({1.0f, 2.0f, 3.0f}); int k = +1; std::vector<int> src; src.push_back(1);\n"
          "struct { std::vector<int>& second; } cv{src}; std::vector<std::array<int, 3>> g; int s = 1; g.push_back({s, k, 0}); std::string_view sv(\"abc\"); std::queue<int> q; q.push(1);\n"
          "std::vector<int*> ptrs; int z = 0; for (int* p : {&z, &k}) ptrs.push_back(p); return m.run(2.0f) + (int)mx + (int)sv.size() + (int)q.size() + (int)cv.second.size() + g[0][0]; }\n", &o);
    ++tests;
    build(t, "typeid, type_index, wide strings, if constexpr", "#include <typeinfo>\n#include <typeindex>\n#include <unordered_map>\n#include <string>\nstruct B { virtual ~B() {} };\nstruct D : B {};\n"
          "int main() { D d; B* b = &d; std::unordered_map<std::type_index, int> m; m[typeid(D)] = 1; std::u16string a; a.push_back(65); std::u32string c; c += 66; std::wstring w; w.push_back(67);\n"
          "int k = 0; if constexpr (sizeof(wchar_t) == 2) { k = 1; } else { k = 2; }\n"
          "return (typeid(*b) == typeid(D) ? 1 : 0) + m[typeid(*b)] + (int)typeid(B).name()[0] + (int)a.size() + (int)c.size() + (int)w.size() + k; }\n", &o);
    CHECK(t, has_symbol(o, "__ti_UD@@") && has_symbol(o, "??_7D@@6B@"));
    bool rtti_slot = false;
    for (const auto& sec : o.sections)
        if (sec.name == ".rdata")
            for (const auto& r : sec.relocations)
                if (r.symbol < o.symbols.size() && o.symbols[r.symbol].name.compare(0, 5, "__ti_") == 0) rtti_slot = true;
    CHECK(t, rtti_slot);
    ++tests;
    build(t, "variant, trailing pack deduction", "#include <variant>\n#include <string>\ntemplate <typename... Ts> struct L { int n; };\ntemplate <typename T, typename... Ts> int pick(L<Ts...>& v) { return v.n; }\n"
          "struct V3 { float x, y, z; };\nint main() { L<int, float> l; l.n = 3; std::variant<std::monostate, int, std::string, V3> v; v = 5; int a = std::holds_alternative<int>(v) ? std::get<int>(v) : 0;\n"
          "v = std::string(\"hi\"); const std::string* s = std::get_if<std::string>(&v); std::variant<std::monostate, int, std::string, V3> w = v; return pick<int>(l) + a + (s ? (int)s->size() : 0) + (int)w.index(); }\n", &o);
    CHECK(t, has_symbol(o, "holds_alternative") && has_symbol(o, "get_if"));
    ++tests;
    build(t, "iostream: cout, cerr, cin, endl, setw, setprecision", "#include <iostream>\n#include <string>\n"
          "int main() { int a = 0; std::string s; std::cin >> a >> s; std::getline(std::cin, s); std::cout << \"hello \" << 42 << ' ' << 1.5 << std::setw(5) << a << std::endl;\n"
          "std::cerr << std::setprecision(2) << 2.5 << std::endl; return a; }\n", &o);
    CHECK(t, has_symbol(o, "okay_write_handle") && has_symbol(o, "okay_read_stdin"));
    ++tests;
    build(t, "round 29 front end: signature parameter names, braced member init, C{} default, const Self arg, class typedef field",
          "#include <functional>\n#include <memory>\ntemplate <typename C> struct tp { int v; };\nstruct clk { typedef tp<clk> time_point; };\n"
          "struct Vec3 { float x, y, z; };\nstruct Self { int k; };\nstruct Holder { clk::time_point s; std::shared_ptr<const Self> p; std::function<int(int count, const Vec3& v)> cb; Vec3 b; int n; Holder() : b{}, n{3} {} };\n"
          "int area(Vec3 v = Vec3{}) { return (int)v.x; }\nint main() { Holder h; h.s.v = 2; return h.s.v + area() + h.n; }\n", &o);
    CHECK(t, has_symbol(o, "main"));
    ++tests;
    build(t, "round 30: auto and decltype(auto) result types",
          "auto f(int a) { return a + 1.5; }\ntemplate <typename T> decltype(auto) e(T a) { return a * 2.0; }\n"
          "struct B { int er(int x) { return x; } };\nstruct S : B { template <class... A> decltype(auto) er(A&&... a) { return B::er(a...); } template <typename T> auto g(T t) { return t; } auto h() { return g(3); } };\n"
          "int main() { S s; return (int)(f(2) + e(1) + s.h() + s.er(1)); }\n", &o);
    CHECK(t, has_symbol(o, "??$e@H@@YANH@Z") && has_symbol(o, "??$g@H@S@@QEAAHH@Z"));
    ++tests;
    build(t, "round 30: base class with namespace in mem-init, inherited typedefs, static vector of strings from a list",
          "#include <string>\n#include <vector>\nstruct T : public std::string { T(const char* s) : std::string(s ? s : \"\") {} T(const T&) = default; char& at2(size_type i) { return (*this)[i]; } };\n"
          "const std::vector<std::string>& B() { static const std::vector<std::string> b = {\"a\", \"b\"}; return b; }\n"
          "int main() { T t(\"xy\"); T u(t); return (int)(B().size() + u.size() + t.at2(0)); }\n", &o);
    CHECK(t, has_symbol(o, "??0?$vector@Vstring@@@@QEAA@V?$initializer_list@Vstring@@@@@Z") && has_symbol(o, "main"));
    ++tests;
    build(t, "round 30: function pointer members, global braces, const local array length, .5 literals, variable named like a class, assign(str,pos,n)",
          "#include <atomic>\n#include <filesystem>\n#include <string>\n#include <vector>\nstruct G;\nstruct E { bool (*has)(const G&); int (*f)(int, int); };\nint add(int a, int b) { return a + b; }\n"
          "std::atomic<int> h{3};\nstruct P { int a; int b; };\nP gp{1, 2};\nstruct PS { int a; };\nstruct VV { float x; VV() : x(0) {} };\n"
          "inline VV Fn(PS& sample, G* parent, const VV& b, float dt, VV& out) { return b; }\nstruct C { struct Code { int a; }; std::vector<Code> slow; };\n"
          "int main() { constexpr int n = 8; int arr[n]; arr[1] = 2; E e; e.f = add; std::vector<int> path; path.clear(); path.push_back(1);\n"
          "std::string a(\"hello\"), b2, c2; b2.assign(a, 1, 3); c2.append(a, 2, 100); C cc; cc.slow.push_back({1});\n"
          "return e.f(1, 2) + arr[1] + h.load() + gp.b + (int)(.5f * 4) + (int)path.size() + (int)(b2.size() + c2.size()); }\n", &o);
    CHECK(t, has_symbol(o, "main"));
    ++tests;
    build(t, "round 30: hash of string and pointer, class array member init list, nested class defined outside",
          "#include <cstdlib>\n#include <new>\n#include <string>\n#include <unordered_map>\nstruct V2 { float x, y; V2() : x(0), y(0) {} V2(float a, float b) : x(a), y(b) {} };\n"
          "struct O { int p[3] = {0, 0, 0}; V2 uv[3] = {{0, 0}, {1, 1}, {2, 2}}; int mat = 0; };\n"
          "struct VS { struct Impl; Impl* p; VS(); int f(); };\nstruct VS::Impl { int a; int g() { return a; } };\nVS::VS() : p(new Impl) { p->a = 4; }\nint VS::f() { return p->g(); }\n"
          "std::unordered_map<std::string, int> ms;\nstd::unordered_map<const void*, int> mp;\n"
          "int main() { O o; VS v; ms[\"a\"] = 1; mp[&o] = 2; return (int)o.uv[1].x + v.f() + ms[\"a\"] + mp[&o]; }\n", &o);
    CHECK(t, has_symbol(o, "main") && has_symbol(o, "?f@VS@@QEAAHXZ"));
    ++tests;
    build(t, "round 30: alias base, defaulted dtor outside, reference result outside, comma, (void) of a large result, function = nullptr, next/prev, member hides namespace function",
          "#include <clocale>\n#include <functional>\n#include <iterator>\n#include <map>\n#include <string>\n#include <vector>\n"
          "namespace okay {\ntemplate <typename T> T Clamp(T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }\n"
          "struct Base { virtual ~Base() {} int a = 1; };\nusing Behaviour = Base;\nstruct D : public Behaviour { std::string s; D(); ~D(); float t = 3; void Clamp(); void Load(); static const D& Get(); };\n"
          "D::D() {}\nD::~D() = default;\nvoid D::Clamp() { t = okay::Clamp(t, 0.5f, 2.0f); }\nvoid D::Load() { Clamp(); }\nstatic D g_d;\nconst D& D::Get() { return g_d; }\n"
          "struct Big { int a, b, c, d; };\nBig mk(int x) { Big r; r.a = x; r.b = 0; r.c = 0; r.d = 0; return r; }\n}\n"
          "int main() { using namespace okay; int a = 0, b = 0; (a = 1, b = 2); (void)mk(2); std::function<void(int)> f = [](int) {}; f = nullptr;\n"
          "std::vector<int> v; v.push_back(1); v.push_back(2); auto it = std::next(v.begin()); auto pv = std::prev(v.end());\n"
          "std::map<int, int> m; m[1] = 2; m[3] = 4; auto mi = std::next(m.begin()); D d; d.Load(); const char* dp = std::localeconv()->decimal_point;\n"
          "return a + b + *it + *pv + mi->second + (int)d.t + dp[0] + D::Get().a + (f ? 1 : 0); }\n", &o);
    CHECK(t, has_symbol(o, "main") && has_symbol(o, "??1D@@UEAA@XZ"));
    ++tests;
    build(t, "round 30: captureless lambda call with temporaries, nearest-scope names, using namespace in nested namespaces, ?: string and text, rotate, nth_element",
          "#include <algorithm>\n#include <string>\n#include <vector>\nstruct V3 { float x, y, z; };\nV3 mk() { return V3{1, 2, 3}; }\n"
          "namespace okay { namespace { namespace ac { bool Fail(int* e, int m) { if (e) *e = m; return false; } } }\n"
          "static bool Impl(int* e) { using namespace ac; return Fail(e, 3); }\n"
          "int Run(std::vector<int>& B, std::vector<std::string>& tok) {\n"
          "  { unsigned long bi = 0; bi = 1; }\n  int nB = 2;\n  auto bi = [&](int i) { return B[i % nB]; };\n"
          "  auto dot = [](const V3& u, const V3& w) { return u.x * w.x; };\n  auto fn = [&](int a) { return mk(); };\n"
          "  auto add = [&](int a) { return dot(fn(a), mk()) < 0.0f ? 1 : 2; };\n"
          "  std::string name = tok.size() >= 2 ? tok[1] : \"poly\";\n"
          "  std::rotate(B.begin(), B.begin() + 1, B.end()); std::nth_element(B.begin(), B.begin() + 1, B.end());\n"
          "  int e = 0; Impl(&e); return bi(1) + add(0) + (int)name.size() + e; } }\n"
          "int main() { std::vector<int> b; b.push_back(1); b.push_back(2); std::vector<std::string> t; return okay::Run(b, t); }\n", &o);
    CHECK(t, has_symbol(o, "main"));
    ++tests;
    build(t, "round 30: scopes in constructor bodies, 2.f literals, sizeof of nested classes, frexp/ldexp, memchr",
          "#include <cmath>\n#include <cstring>\n#include <vector>\nstruct VS { struct Impl; Impl* m; };\nstruct VS::Impl { struct Entry { int a; }; int setupBytes; };\n"
          "struct D { std::vector<int> v; int id_; D(int id) : id_(id) { for (int b = 0; b < 2; ++b) id_ += b; for (int& x : v) id_ += x; } };\n"
          "int main() { D d(1); float a = 2.f; double b = 3.; double c = 1.e2; int e; double m = std::frexp(40.0, &e); const char* s = \"a\\nb\";\n"
          "return d.id_ + (int)(a + b + c + std::ldexp(m, e)) + (int)sizeof(VS::Impl::Entry) + (int)sizeof(VS::Impl) + (std::memchr(s, 10, 3) != 0); }\n", &o);
    CHECK(t, has_symbol(o, "main"));
    return t.finish(tests);
}
