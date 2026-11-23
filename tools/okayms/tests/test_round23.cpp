// Round 23: what stopped real engine files. Variables with static storage whose initializers are not constants
// (globals, static members, static locals, parts of aggregates), static and copy-initialized global objects, static
// inline members, delegating constructors, functional casts inside parentheses, `using X = R (*)(A)`, a `const C&`
// parameter given a value that a converting constructor turns into a C, forward-declared classes, a `static`
// prototype before its definition, our own <map> <unordered_map> <cfloat> <ctime>, vector::emplace_back, std::fmin and
// std::fmax, and a quoted include next to a main file named without a directory.
// Programs return 0 when all of their checks hold, or the number of the first check that failed. Each is built by
// okay-cl at /O0, /O1 and /O2 and run under Wine; MinGW g++ builds the same text and its exit code must match.
// Written from ISO/IEC 14882 [basic.start.static], [basic.start.dynamic], [stmt.dcl], [class.static.data],
// [dcl.inline], [class.base.init], [expr.type.conv], [dcl.typedef], [dcl.init.ref], [over.ics.user], [class.name],
// [basic.def], [dcl.stc], [map], [unord.map], [sequence.reqmts], [c.math], [cpp.include], and ISO/IEC 9899 5.2.4.2.2
// and 7.27. No code from other compilers or libraries.

#include "program_check.hpp"

#include <string>
#include <unistd.h>

namespace {

using okayms_test::Tally;
using okayms_test::run_shell;
const okayms_test::ProgramRun kRun{"r23", {}};

}  // namespace

// The bump allocator of program_check.hpp (no C runtime is linked on the okay-ms side).
static const char* kAlloc = okayms_test::bump_alloc();

static void test_dynamic_init(Tally& tally) {
    std::printf("test_dynamic_init\n");
    okayms_test::run_both(kRun, tally, "dyn_init",
             "static int counter = 0;\n"
             "static int bump() { return ++counter; }\n"
             "static int twice(int x) { ++counter; return x * 2; }\n"
             "int g1 = bump() + 10;\n"
             "static int g2 = bump() * 100;\n"
             "struct S { static int m; };\n"
             "int S::m = bump();\n"
             "static int lazy(int x) { static const int v = [] { return bump() + 1000; }(); return v + x; }\n"
             "static int lazy2() { static int w = bump(); return w; }\n"
             "struct E { const char* n; char c; int w; };\n"
             "static const E gt[] = {{\"a\", 1, twice(3)}, {\"b\", 2, 9}, {\"c\", 3, twice(5)}};\n"
             "static int find_w(int i) {\n"
             "  static const E t[] = {{\"x\", 'x', twice(10)}, {\"y\", 'y', 4}};\n"
             "  return t[i].w;\n"
             "}\n"
             "int arr[4] = {1, twice(2), 3, twice(4)};\n"
             "double dv = twice(3) / 4.0;\n"
             "int entry(void) {\n"
             "  CK(1, g1 == 11); CK(2, g2 == 200); CK(3, S::m == 3);\n"
             "  CK(4, gt[0].w == 6 && gt[1].w == 9 && gt[2].w == 10 && gt[1].c == 2 && gt[2].n[0] == 'c');\n"
             "  CK(5, arr[0] == 1 && arr[1] == 4 && arr[2] == 3 && arr[3] == 8);\n"
             "  CK(6, dv == 1.5 && counter == 8);\n"
             "  CK(7, lazy(0) == 1009); CK(8, lazy(1) == 1010); CK(9, lazy2() == 10); CK(10, lazy2() == 10);\n"
             "  CK(11, find_w(0) == 20 && find_w(1) == 4 && counter == 11);\n"
             "  CK(12, find_w(0) == 20 && counter == 11);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_class_statics(Tally& tally) {
    std::printf("test_class_statics\n");
    okayms_test::run_both(kRun, tally, "class_statics",
             std::string(kAlloc) + "#include <string>\n#include <vector>\n" +
             "static int counter = 0;\n"
             "static int bump() { return ++counter; }\n"
             "struct B { int v; B() : v(bump() * 10) {} B(int x) : v(x) {} ~B() { counter += 1000; } };\n"
             "struct R { int s; R(int x) : s(x) {} R(const R& o) : s(o.s + 1) {} };\n"
             "static R make(int x) { return R(x); }\n"
             "static B gb;\n"
             "B gb2 = 7;\n"
             "R gr = make(40);\n"
             "static int use() { static R r = make(5); r.s += 100; return r.s; }\n"
             "static int use2() { static B b(3); return b.v; }\n"
             "struct Col { float r, g; static Col FromBytes(int a, int b) { Col c; c.r = a / 255.0f; c.g = b / 255.0f; return c; } };\n"
             "static const std::vector<Col>& Palette() {\n"
             "  static const std::vector<Col> p = { Col::FromBytes(255, 0), Col::FromBytes(0, 255), Col::FromBytes(51, 51) };\n"
             "  return p;\n"
             "}\n"
             "struct Named { static std::string label; static std::string other; };\n"
             "std::string Named::label;\n"
             "std::string Named::other = \"abc\";\n"
             "int entry(void) {\n"
             "  CK(1, gb.v == 10); CK(2, gb2.v == 7); CK(3, gr.s == 40);\n"
             "  int u = use(); CK(4, u == 105); CK(5, use() == 205);\n"
             "  CK(6, use2() == 3);\n"
             "  const std::vector<Col>& pal = Palette(); const std::vector<Col>& pal2 = Palette();\n"
             "  CK(7, pal.size() == 3 && &pal == &pal2 && pal[1].g == 1.0f && pal[0].r == 1.0f);\n"
             "  CK(8, Named::label.empty() && Named::other == \"abc\" && Named::other.size() == 3);\n"
             "  Named::label += \"x\"; CK(9, Named::label == \"x\");\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_static_inline(Tally& tally) {
    std::printf("test_static_inline\n");
    okayms_test::run_both(kRun, tally, "static_inline",
             "static int seven() { return 7; }\n"
             "static int made = 0;\n"
             "struct T {\n"
             "  static inline float s_scale = 1.5f;\n"
             "  static inline float s_hist[4] = {0.5f, 1.0f, 2.0f, 4.0f};\n"
             "  static inline int s_a = 3, s_b = 4;\n"
             "  static inline int s_dyn = seven() * 2;\n"
             "  static constexpr int kN = 5;\n"
             "  static constexpr int kTab[3] = {1, 2, kN};\n"
             "  static constexpr char kUp = 1;\n"
             "  static constexpr char kCmd = static_cast<char>(153);\n"
             "  static void Bump() { s_scale *= 2.0f; s_a += s_b; }\n"
             "  struct Guard { int got; explicit Guard(int k) : got(T::Get() + k) {} };\n"
             "  struct KE { const char* n; char c; };\n"
             "  static int Find(int i) { static const KE t[] = {{\"up\", kUp}, {\"cmd\", kCmd}, {\"esc\", static_cast<char>(27)}}; return t[i].c; }\n"
             "  T() : T(kN) { made += 10; }\n"
             "  explicit T(int v) : v_(v) { made += 1; }\n"
             "  int v_;\n"
             "  static int Get() { return 40 + kN; }\n"
             "};\n"
             "int entry(void) {\n"
             "  CK(1, T::s_scale == 1.5f); T::Bump(); CK(2, T::s_scale == 3.0f && T::s_a == 7 && T::s_b == 4);\n"
             "  CK(3, T::s_hist[3] == 4.0f && T::s_hist[0] == 0.5f); T::s_hist[1] = 9.0f; CK(4, T::s_hist[1] == 9.0f);\n"
             "  CK(5, T::s_dyn == 14 && T::kTab[2] == 5 && T::kTab[0] == 1);\n"
             "  T t; CK(6, t.v_ == 5 && made == 11); T::Guard g(2); CK(7, g.got == 47);\n"
             "  CK(8, T::kCmd == (char)153 && T::kCmd < 0 && T::Find(0) == 1 && T::Find(1) == T::kCmd && T::Find(2) == 27);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_casts_typedefs(Tally& tally) {
    std::printf("test_casts_typedefs\n");
    okayms_test::run_both(kRun, tally, "casts_typedefs",
             "static int seven() { return 7; }\n"
             "static double half(double x) { return x / 2; }\n"
             "using Fn = int (*)();\n"
             "typedef double (*DFn)(double);\n"
             "using Arr3 = int[3];\n"
             "struct Clock { using ClockFn = double (*)(double); typedef int (*IntFn)(); ClockFn fn; IntFn ifn; double run(double v) const { return fn(v); } };\n"
             "int entry(void) {\n"
             "  double k = 3;\n"
             "  CK(1, (double(2) * k) == 6.0); CK(2, int(7.9) == 7); CK(3, (unsigned(5) + 1u) == 6u);\n"
             "  long long big = (long long)(int(3) * 4) + 1; CK(4, big == 13);\n"
             "  Fn f = seven; CK(5, f() == 7);\n"
             "  DFn d = half; CK(6, d(5.0) == 2.5);\n"
             "  Arr3 a = {1, 2, 3}; CK(7, sizeof(a) == 12 && a[2] == 3);\n"
             "  Clock c; c.fn = half; c.ifn = seven; CK(8, c.run(9.0) == 4.5 && c.ifn() == 7);\n"
             "  using G = int (*)(); G g = seven; CK(9, g() == 7);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_ref_convert(Tally& tally) {
    std::printf("test_ref_convert\n");
    okayms_test::run_both(kRun, tally, "ref_convert",
             std::string(kAlloc) + "#include <string>\n" +
             "static int len(const std::string& s) { return (int)s.size(); }\n"
             "struct C { int n; C(const char* p) : n(0) { while (p[n]) ++n; } };\n"
             "static int lenc(const C& c) { return c.n; }\n"
             "struct H { int len(const std::string& s) { return (int)s.size(); } int operator[](const std::string& s) { return (int)s.size() * 10; } };\n"
             "template <typename K> struct T { int len(const K& s) { return (int)s.size(); } };\n"
             "struct D { int v; D(int x) : v(x * 3) {} };\n"
             "static int dv(const D& d) { return d.v; }\n"
             "int entry(void) {\n"
             "  CK(1, len(\"abc\") == 3);\n"
             "  int n = len(\"hello\"); CK(2, n == 5);\n"
             "  CK(3, lenc(\"abcd\") == 4);\n"
             "  const char* p = \"xy\"; CK(4, lenc(p) == 2 && len(p) == 2);\n"
             "  H h; CK(5, h.len(\"abc\") == 3 && h[\"ab\"] == 20);\n"
             "  T<std::string> t; CK(6, t.len(\"four\") == 4);\n"
             "  CK(7, dv(4) == 12);\n"
             "  std::string s(\"q\"); CK(8, len(s) == 1 && len(std::string(\"zz\")) == 2);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_containers(Tally& tally) {
    std::printf("test_containers\n");
    okayms_test::run_both(kRun, tally, "containers",
             std::string(kAlloc) +
             "#include <map>\n"
             "#include <unordered_map>\n"
             "#include <string>\n"
             "#include <vector>\n"
             "#include <cmath>\n"
             "#include <cfloat>\n"
             "#include <ctime>\n"
             "struct P { int a, b; P() : a(1), b(2) {} P(int x, int y) : a(x), b(y) {} };\n"
             "int entry(void) {\n"
             "  std::map<int, int> m;\n"
             "  m[5] = 50; m[1] = 10; m[3] = 30; m[5] += 1;\n"
             "  CK(1, m.size() == 3 && m[5] == 51 && m.count(3) == 1 && m.count(4) == 0);\n"
             "  int keys = 0; for (auto& kv : m) keys = keys * 10 + kv.first;\n"
             "  CK(2, keys == 135);\n"
             "  auto it = m.find(3); CK(3, it != m.end() && it->second == 30);\n"
             "  m.erase(3); CK(4, m.find(3) == m.end() && m.size() == 2);\n"
             "  auto r1 = m.insert({7, 70}); CK(5, r1.second && r1.first->second == 70);\n"
             "  CK(6, !m.insert(std::pair<int, int>(7, 1)).second && m[7] == 70);\n"
             "  std::unordered_map<std::string, int> u;\n"
             "  u[\"b\"] = 2; u[\"a\"] = 1; u[\"b\"] += 5;\n"
             "  CK(7, u.size() == 2 && u[\"b\"] == 7 && u.count(\"a\") == 1 && u.find(\"zz\") == u.end());\n"
             "  std::map<std::string, std::string> names; names[\"x\"] = \"1\"; names[\"a\"] = \"2\";\n"
             "  CK(8, names.begin()->first == \"a\" && names[\"x\"] == \"1\");\n"
             "  CK(9, FLT_MAX > 3.4e38f && FLT_EPSILON * 2 > 2.3e-7f && DBL_EPSILON < 3e-16 && FLT_MANT_DIG == 24 && DBL_DIG == 15);\n"
             "  time_t t = 5; CK(10, sizeof(t) == 8 && CLOCKS_PER_SEC == 1000 && t == 5);\n"
             "  std::vector<P> v; v.emplace_back(3, 4); v.emplace_back(); v.emplace_back(5, 6);\n"
             "  CK(11, v.size() == 3 && v[0].a == 3 && v[0].b == 4 && v[1].a == 1 && v[2].b == 6);\n"
             "  std::vector<std::string> s; s.emplace_back(\"hi\"); s.emplace_back(3, 'z');\n"
             "  CK(12, s[0] == \"hi\" && s[1] == \"zzz\");\n"
             "  CK(13, std::fmin(2.0f, 3.0f) == 2.0f && std::fmax(2.0f, 3.0f) == 3.0f);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_forward_decl(Tally& tally) {
    std::printf("test_forward_decl\n");
    okayms_test::run_both(kRun, tally, "fwd_decl",
             "namespace n { class A; struct L; }\n"
             "struct B { n::A* a; int get() const; static int twice(const n::A& x); };\n"
             "struct Outer { struct Info; Info* info; int sz() const; struct Info { int k = 4; }; };\n"
             "int Outer::sz() const { return info->k; }\n"
             "namespace n { class A { public: int v = 9; virtual int f() const { return v * 2; } }; struct L { int q; }; }\n"
             "int B::get() const { return a->v + a->f(); }\n"
             "int B::twice(const n::A& x) { return x.v * 2; }\n"
             "class Late;\n"
             "static int use_late(Late* p);\n"
             "class Late { public: int w; };\n"
             "static int use_late(Late* p) { return p->w; }\n"
             "static int proto(int p);\n"
             "static int proto(int p) { return p + 1; }\n"
             "int entry(void) {\n"
             "  n::A obj; B b; b.a = &obj; CK(1, b.get() == 27); CK(2, B::twice(obj) == 18);\n"
             "  Outer o; Outer::Info i; o.info = &i; CK(3, o.sz() == 4);\n"
             "  Late l; l.w = 6; CK(4, use_late(&l) == 6);\n"
             "  n::L s; s.q = 3; CK(5, s.q == 3 && sizeof(n::L) == 4);\n"
             "  CK(6, proto(2) == 3);\n"
             "  return 0;\n"
             "}\n",
             0);
}

// mutable members, an enumeration (scoped or not) named through its class, a braced default argument T{...}, and a
// nested class of a plain struct as a template argument (InputActions.hpp, GamepadInfo.hpp, PMath.hpp, SceneDiff.hpp).
static void test_qualified_names(Tally& tally) {
    std::printf("test_qualified_names\n");
    okayms_test::run_both(kRun, tally, "qualified_names",
             std::string(kAlloc) + "#include <vector>\n" +
             "struct M { mutable int c = 0; int get() const { return ++c; } };\n"
             "struct In { enum class PadFamily { A, B }; enum Plain { P0, P1 }; };\n"
             "static int lab(In::PadFamily f) { return f == In::PadFamily::B ? 2 : 1; }\n"
             "static int pl(In::Plain p) { return p == In::P1 ? 5 : 4; }\n"
             "struct V3 { float x, y, z; };\n"
             "static float ny(const V3& v, const V3& fb = V3{0, 1, 0}) { return v.x + fb.y; }\n"
             "struct OC { struct Slot { int a; }; };\n"
             "struct D { std::vector<OC::Slot> lay; };\n"
             "int entry(void) {\n"
             "  const M m; m.get(); CK(1, m.get() == 2);\n"
             "  CK(2, lab(In::PadFamily::B) == 2 && lab(In::PadFamily::A) == 1 && pl(In::P1) == 5);\n"
             "  V3 v{2, 0, 0}; CK(3, ny(v) == 3.0f && ny(v, V3{0, 5, 0}) == 7.0f);\n"
             "  D d; OC::Slot s; s.a = 4; d.lay.push_back(s); CK(4, d.lay[0].a == 4);\n"
             "  return 0;\n"
             "}\n",
             0);
}

// Found in review of this round (both were silent miscompiles): A::Size read the last enumerator of that name from any
// class (B's 8), and a const global or const static member with a non-constant initializer was read as the constant 0
// (and /O2 refused the startup store as an "assignment to const"). A::X for an X that is not a member of A is an error.
static void test_review_fixes(Tally& tally) {
    std::printf("test_review_fixes\n");
    okayms_test::run_both(kRun, tally, "review_fixes",
             "struct A { enum { Size = 4, Only = 1 }; };\n"
             "struct B { enum { Size = 8 }; };\n"
             "struct C { int x; enum E { Size = 4, K = 9 }; int f() const { return Size + K; } };\n"
             "static int f() { return 7; }\n"
             "const int k = f();\n"
             "static const int k2 = f() + 1;\n"
             "struct S { static const int m; static const int n; };\n"
             "const int S::m = f();\n"
             "const int S::n = 3;\n"
             "static int g() { static const int q = f() * 2; return q; }\n"
             "const int arr[2] = {f(), 5};\n"
             "int entry(void) {\n"
             "  CK(1, A::Size == 4); CK(2, B::Size == 8); CK(3, C::Size == 4 && C::K == 9);\n"
             "  C c; CK(4, c.f() == 13); CK(5, A::Only == 1);\n"
             "  CK(6, k == 7); CK(7, k2 == 8); CK(8, S::m == 7); CK(9, S::n == 3); CK(10, g() == 14);\n"
             "  CK(11, arr[0] == 7 && arr[1] == 5);\n"
             "  int s = 0; for (int i = 0; i < k; ++i) s += 1; CK(12, s == 7);\n"
             "  return 0;\n"
             "}\n",
             0);
    okayms::CompileInput in;
    in.source = "struct A { enum { Size = 4 }; };\nstruct B { enum { Other = 8 }; };\nint entry(void) { return B::Size; }\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, !compiled.ok());
    if (compiled.ok()) std::fprintf(stderr, "  B::Size compiled, but Size is not a member of B\n");
}

// okay-cl /c file.cpp: a quoted include is looked up next to the main file, also when the file is named without a
// directory ([cpp.include]); before this round only the include paths were searched.
static void test_include_next_to_main(Tally& tally) {
    std::printf("test_include_next_to_main\n");
    const std::string dir = "/tmp/okayms-r23/inc";
    run_shell("mkdir -p " + dir);
    {
        std::ofstream h((dir + "/local_r23.hpp").c_str());
        h << "static int from_local(void) { return 23; }\n";
    }
    char keep[4096];
    const bool have_cwd = getcwd(keep, sizeof keep) != nullptr;
    CHECK(tally, have_cwd);
    if (!have_cwd || chdir(dir.c_str()) != 0) return;
    okayms::CompileInput in;
    in.name = "main_r23.cpp";
    in.source = "#include \"local_r23.hpp\"\nint entry(void) { return from_local(); }\n";
    const auto compiled = okayms::compile_c(in);
    if (chdir(keep) != 0) std::fprintf(stderr, "  could not go back to %s\n", keep);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) std::fprintf(stderr, "  %s\n", compiled.error.c_str());
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    (void)root;
    Tally tally;
    test_dynamic_init(tally);
    test_class_statics(tally);
    test_static_inline(tally);
    test_casts_typedefs(tally);
    test_ref_convert(tally);
    test_containers(tally);
    test_forward_decl(tally);
    test_qualified_names(tally);
    test_review_fixes(tally);
    test_include_next_to_main(tally);
    return tally.finish(10);
}
