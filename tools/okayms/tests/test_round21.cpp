// Round 21: aggregate member default initializers, and our own standard headers (array, cctype, atomic, functional, string, vector).
// Programs return 0 when all of their checks hold, or the number of the first check
// that failed. Each is built by okay-cl at /O0, /O1 and /O2 and run under Wine; MinGW g++ builds the same
// text and its exit code must match. Written from ISO/IEC 14882 [dcl.fct.default], [stmt.return],
// [over.oper], [over.match], [over.binary], [over.unary], [over.sub], [over.call], [over.inc], [class.conv.fct],
// [expr.ref], [stmt.return], [dcl.spec.auto], [stmt.ranged], [expr.prim.lambda], and the MSVC name decoration rules
// (operator codes ?H ?G ?D ?K ?A ?R ?8 ?9 ?M ?O ?Y ?Z ?E ?F ?6 ?B).

#include "check.hpp"
#include "okayms/coff.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

int run_shell(const std::string& cmd) {
    const int rc = std::system(cmd.c_str());
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

int wine_exit(const std::string& exe) {
    return run_shell("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + exe + " >/dev/null 2>&1");
}

bool write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

[[maybe_unused]] bool has_symbol(const okayms::ObjectFile& object, const std::string& name) {
    for (const auto& symbol : object.symbols)
        if (symbol.name == name) return true;
    return false;
}

const char* kChecks = "#define CK(n, c) if (!(c)) return n;\n";

// Builds `source` at each optimization level and runs it; MinGW g++ runs the same text.
void run_both(Tally& tally, const std::string& name, const std::string& source, int want,
              okayms::ObjectFile* object_out = nullptr) {
    std::printf("r21 program %s\n", name.c_str());
    const std::string dir = "/tmp/okayms-r21";
    run_shell("mkdir -p " + dir);
    for (int level = 0; level <= 2; ++level) {
        okayms::CompileInput in;
        in.source = std::string(kChecks) + source;
        in.opt_level = level;
        in.optimize = level > 0;
        in.include_paths.push_back("tools/okayms/stdinc");
        in.include_paths.push_back("stdinc");
        const auto compiled = okayms::compile_c(in);
        CHECK(tally, compiled.ok());
        if (!compiled.ok()) {
            std::fprintf(stderr, "  %s /O%d: %s\n", name.c_str(), level, compiled.error.c_str());
            continue;
        }
        if (object_out != nullptr && level == 0) *object_out = compiled.value.object;
        const auto linked = okayms::link_executable({compiled.value.object}, {});
        CHECK(tally, linked.ok());
        if (!linked.ok()) {
            std::fprintf(stderr, "  %s\n", linked.error.c_str());
            continue;
        }
        const std::string ok_exe = dir + "/" + name + "-ok" + std::to_string(level) + ".exe";
        CHECK(tally, write_bytes(ok_exe, linked.value));
        const int code = wine_exit(ok_exe);
        CHECK(tally, code == want);
        if (code != want) std::fprintf(stderr, "  %s /O%d: okay exit %d, wanted %d\n", name.c_str(), level, code, want);
    }
    if (run_shell("command -v x86_64-w64-mingw32-g++ >/dev/null") != 0) {
        std::printf("skipping g++ comparison: MinGW g++ is not installed\n");
        return;
    }
    const std::string src = dir + "/" + name + ".cpp";
    {
        std::ofstream out(src.c_str());
        out << kChecks << "#include <cstdint>\n#include <cstddef>\n" << source << "\nint main() { return entry(); }\n";
    }
    const std::string gpp_exe = dir + "/" + name + "-gpp.exe";
    const int built = run_shell("x86_64-w64-mingw32-g++ -static -fno-exceptions -fno-rtti -w -o " + gpp_exe + " " + src +
                                " 2>" + dir + "/" + name + ".log");
    CHECK(tally, built == 0);
    if (built != 0) return;
    const int ref = wine_exit(gpp_exe);
    CHECK(tally, ref == want);
    if (ref != want) std::fprintf(stderr, "  g++ exit %d, wanted %d (the test itself is wrong)\n", ref, want);
}

}  // namespace

static void test_member_aggregates(Tally& tally) {
    std::printf("test_member_aggregates\n");
    run_both(tally, "agg_array",
             "struct M {\n"
             "  float m[4] = {1, 0, 0, 2};\n"
             "  int n[3] = {5};\n"
             "  int z[2] = {};\n"
             "  int k = 9;\n"
             "};\n"
             "struct Id { float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; };\n"
             "int entry(void) {\n"
             "  M a; Id b;\n"
             "  CK(1, a.m[0] == 1.0f && a.m[1] == 0.0f && a.m[3] == 2.0f);\n"
             "  CK(2, a.n[0] == 5 && a.n[1] == 0 && a.n[2] == 0);\n"
             "  CK(3, a.z[0] == 0 && a.z[1] == 0 && a.k == 9);\n"
             "  float t = 0; for (int i = 0; i < 16; ++i) t += b.m[i];\n"
             "  CK(4, t == 4.0f && b.m[5] == 1.0f && b.m[15] == 1.0f && b.m[14] == 0.0f);\n"
             "  M c; c.m[0] = 7; M d = c; CK(5, d.m[0] == 7.0f && d.m[3] == 2.0f);\n"
             "  return 0;\n"
             "}\n",
             0);
    run_both(tally, "agg_struct",
             "struct P { int x; int y; };\n"
             "struct Q { P p = {3, 4}; int k = 7; P arr[2] = {{1, 2}, {5, 6}}; double d[2] = {0.5, 1.5}; };\n"
             "struct W { int a = 1; Q q; W() {} W(int v) : a(v) {} };\n"
             "struct F { float v[3] = {1, 2, 3}; F() {} F(float s) { v[0] = s; } float sum() const { return v[0] + v[1] + v[2]; } };\n"
             "int entry(void) {\n"
             "  Q q;\n"
             "  CK(1, q.p.x == 3 && q.p.y == 4 && q.k == 7);\n"
             "  CK(2, q.arr[0].x == 1 && q.arr[0].y == 2 && q.arr[1].x == 5 && q.arr[1].y == 6);\n"
             "  CK(3, q.d[0] == 0.5 && q.d[1] == 1.5);\n"
             "  W w(5); CK(4, w.a == 5 && w.q.p.x == 3 && w.q.arr[1].y == 6);\n"
             "  F f; CK(5, f.sum() == 6.0f); F g(10.0f); CK(6, g.sum() == 15.0f);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_cmath_classify(Tally& tally) {
    std::printf("test_cmath_classify\n");
    run_both(tally, "cmath_fp",
             "#include <cmath>\n"
             "static float inf_() { float z = 0.0f; return 1.0f / z; }\n"
             "static float nan_() { float z = 0.0f; return z / z; }\n"
             "int entry(void) {\n"
             "  float a = 2.5f, b = inf_(), c = nan_(), d = -inf_();\n"
             "  CK(1, std::isfinite(a) && !std::isfinite(b) && !std::isfinite(c) && !std::isfinite(d));\n"
             "  CK(2, !std::isnan(a) && !std::isnan(b) && std::isnan(c));\n"
             "  CK(3, !std::isinf(a) && std::isinf(b) && !std::isinf(c) && std::isinf(d));\n"
             "  CK(4, !(c < 1.0f) && !(c <= 1.0f) && !(c == c) && (c != c) && !(c > 1.0f) && !(c >= 1.0f));\n"
             "  CK(5, (a < b) && (a <= b) && (a == a) && !(a != a) && (d < a));\n"
             "  double e = c; CK(6, !(e == e) && (e != e) && !(e < 1.0) && (a == 2.5f));\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_found_on_the_way(Tally& tally) {
    std::printf("test_found_on_the_way\n");
    run_both(tally, "mat_like",
             "struct V3 { float x, y, z; };\n"
             "struct Q4 { float x, y, z, w; };\n"
             "static Q4 mkq(float a, float b, float c, float d, float e, float f, float g, float h, float i) {\n"
             "  Q4 q; q.x = a + i; q.y = b + h; q.z = c + g; q.w = d + e + f; return q; }\n"
             "static V3 mkv(float a) { V3 v; v.x = a; v.y = a + 1; v.z = a + 2; return v; }\n"
             "static bool dec(const float* m, V3& t, Q4& r) {\n"
             "  V3 n0 = mkv(m[0]), n1 = mkv(m[1]), n2 = mkv(m[2]);\n"
             "  r = mkq(n0.x, n1.x, n2.x, n0.y, n1.y, n2.y, n0.z, n1.z, n2.z);\n"
             "  t = mkv(m[3]);\n"
             "  return true;\n"
             "}\n"
             "static V3 pick(bool c, float a, float b) { V3 w = c ? mkv(a) : mkv(b); return w; }\n"
             "static V3 pick2(bool c, const V3& a) { V3 z = {0, 0, 0}; V3 w = c ? a : z; return c ? w : mkv(9); }\n"
             "int entry(void) {\n"
             "  float m[4] = {1, 2, 3, 10}; V3 t; Q4 r;\n"
             "  CK(1, dec(m, t, r));\n"
             "  CK(2, r.x == 6 && r.y == 6 && r.z == 3 + 3 && r.w == 2 + 3 + 4);\n"
             "  CK(3, t.x == 10 && t.y == 11 && t.z == 12);\n"
             "  V3 a = pick(true, 1, 5), b = pick(false, 1, 5); CK(4, a.x == 1 && a.z == 3 && b.x == 5 && b.z == 7);\n"
             "  V3 s = {4, 5, 6}; V3 c = pick2(true, s), d = pick2(false, s); CK(5, c.y == 5 && d.y == 10);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_many_parameters(Tally& tally) {
    std::printf("test_many_parameters\n");
    run_both(tally, "many_params",
             "struct S3 { int a, b, c; };\n"
             "static int gi[8] = {1, 2, 3, 4, 5, 6, 7, 8};\n"
             "static float gf[8] = {1, 2, 3, 4, 5, 6, 7, 8};\n"
             "static double gd[8] = {1, 2, 3, 4, 5, 6, 7, 8};\n"
             "static int sum6(int a, int b, int c, int d, int e, int f) { return a + 2*b + 3*c + 4*d + 5*e + 6*f; }\n"
             "static float fsum7(float a, float b, float c, float d, float e, float f, float g) {\n"
             "  return a + 2*b + 3*c + 4*d + 5*e + 6*f + 7*g; }\n"
             "static double dsum6(double a, double b, double c, double d, double e, double f) {\n"
             "  return a + 2*b + 3*c + 4*d + 5*e + 6*f; }\n"
             "static long long mix(char a, short b, long long c, float d, int e, double f, unsigned char g, long long h) {\n"
             "  return a + b + c + (long long)d + e + (long long)f + g + h * 100; }\n"
             "static int withs(int a, int b, int c, int d, S3 s, int e) { return s.a + s.b * 10 + s.c * 100 + e * 1000 + a + b + c + d; }\n"
             "static int refs(int a, int b, int c, int d, int& e, int* f) { e += a; *f += b; return e + *f; }\n"
             "static int loopy(int a, int b, int c, int d, int e, int f) {\n"
             "  int t = 0; for (int i = 0; i < e; ++i) t += f + i; return t + a + b + c + d; }\n"
             "int entry(void) {\n"
             "  CK(1, sum6(gi[0], gi[1], gi[2], gi[3], gi[4], gi[5]) == 1 + 4 + 9 + 16 + 25 + 36);\n"
             "  CK(2, fsum7(gf[0], gf[1], gf[2], gf[3], gf[4], gf[5], gf[6]) == 140.0f);\n"
             "  CK(3, dsum6(gd[0], gd[1], gd[2], gd[3], gd[4], gd[5]) == 91.0);\n"
             "  CK(4, mix(1, 2, 3, 4.0f, 5, 6.0, 7, 8) == 1 + 2 + 3 + 4 + 5 + 6 + 7 + 800);\n"
             "  S3 s = {4, 5, 6}; CK(5, withs(1, 1, 1, 1, s, 7) == 4 + 50 + 600 + 7000 + 4);\n"
             "  int e = 10, f = 20; CK(6, refs(1, 2, 3, 4, e, &f) == 11 + 22 && e == 11 && f == 22);\n"
             "  CK(7, loopy(1, 2, 3, 4, 5, 6) == (6 + 7 + 8 + 9 + 10) + 10);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_std_array_cctype(Tally& tally) {
    std::printf("test_std_array_cctype\n");
    run_both(tally, "std_array",
             "#include <array>\n"
             "struct P { int x, y; };\n"
             "int entry(void) {\n"
             "  std::array<int, 4> a = {1, 2, 3, 4};\n"
             "  CK(1, a.size() == 4 && a[0] == 1 && a[3] == 4 && a.front() == 1 && a.back() == 4);\n"
             "  int s = 0; for (int v : a) s += v; CK(2, s == 10);\n"
             "  a[2] = 30; a.fill(7); CK(3, a[0] == 7 && a[3] == 7);\n"
             "  std::array<float, 3> f{}; CK(4, f[0] == 0.0f && f[2] == 0.0f);\n"
             "  for (auto& v : f) v = 2.5f; CK(5, f[1] == 2.5f);\n"
             "  std::array<P, 2> ps; ps[0].x = 3; ps[1].y = 9; CK(6, ps[0].x + ps[1].y == 12);\n"
             "  const std::array<int, 3> c = {5, 6, 7}; CK(7, c[1] == 6 && c.data()[2] == 7);\n"
             "  int t = 0; for (const int* p = c.begin(); p != c.end(); ++p) t += *p; CK(8, t == 18);\n"
             "  return 0;\n"
             "}\n",
             0);
    run_both(tally, "std_cctype",
             "#include <cctype>\n"
             "int entry(void) {\n"
             "  CK(1, std::isdigit('5') && !std::isdigit('a') && std::isalpha('Q') && !std::isalpha('1'));\n"
             "  CK(2, std::isspace(' ') && std::isspace('\\n') && std::isspace('\\t') && !std::isspace('x'));\n"
             "  CK(3, std::tolower('Q') == 'q' && std::toupper('q') == 'Q' && std::tolower('1') == '1');\n"
             "  CK(4, std::isalnum('z') && std::isalnum('0') && !std::isalnum('_') && std::isupper('A') && std::islower('a'));\n"
             "  CK(5, std::isxdigit('f') && std::isxdigit('F') && std::isxdigit('9') && !std::isxdigit('g'));\n"
             "  CK(6, std::ispunct('!') && !std::ispunct('a') && std::isprint(' ') && !std::isprint(10));\n"
             "  int n = 0; for (int c = 0; c < 256; ++c) if (std::isalpha(c)) ++n; CK(7, n == 52);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_member_array_decay(Tally& tally) {
    std::printf("test_member_array_decay\n");
    run_both(tally, "member_decay",
             "struct S { int a[3]; int* p() { return a; } const int* q() const { return a + 1; } };\n"
             "template <typename T, int N> struct A { T a[N]; T* p() { return a; } T* e() { return a + N; } };\n"
             "struct B { int m[2][3]; };\n"
             "static int sum(const int* p, int n) { int t = 0; for (int i = 0; i < n; ++i) t += p[i]; return t; }\n"
             "int entry(void) {\n"
             "  S s; s.a[0] = 1; s.a[1] = 2; s.a[2] = 3;\n"
             "  int* w = s.a; w[2] = 7; CK(1, s.a[2] == 7);\n"
             "  s.p()[0] = 4; CK(2, s.a[0] == 4 && *s.q() == 2);\n"
             "  CK(3, sum(s.a, 3) == 13);\n"
             "  A<float, 4> t; float* b = t.p(); for (int i = 0; i < 4; ++i) b[i] = (float)i; CK(4, t.a[3] == 3.0f && t.e() - t.p() == 4);\n"
             "  B m; for (int i = 0; i < 2; ++i) for (int j = 0; j < 3; ++j) m.m[i][j] = i * 3 + j;\n"
             "  CK(5, sum(m.m[1], 3) == 12 && m.m[1][2] == 5);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_std_atomic_functional(Tally& tally) {
    std::printf("test_std_atomic_functional\n");
    run_both(tally, "std_atomic",
             "#include <atomic>\n"
             "std::atomic<int> g(5);\n"
             "int entry(void) {\n"
             "  std::atomic<int> a(1);\n"
             "  CK(1, a.load() == 1 && (int)a == 1);\n"
             "  a.store(7); CK(2, a.load() == 7);\n"
             "  CK(3, a.exchange(9) == 7 && a.load() == 9);\n"
             "  int exp = 9; CK(4, a.compare_exchange_strong(exp, 11) && a.load() == 11);\n"
             "  exp = 1; CK(5, !a.compare_exchange_strong(exp, 12) && exp == 11 && a.load() == 11);\n"
             "  CK(6, a.fetch_add(4) == 11 && a.load() == 15 && a.fetch_sub(5) == 15 && a.load() == 10);\n"
             "  a += 5; ++a; a++; CK(7, a.load() == 17);\n"
             "  a = 6; CK(8, a.fetch_or(9) == 6 && a.load() == 15 && a.fetch_and(3) == 15 && a.load() == 3);\n"
             "  CK(9, g.load() == 5 && g.fetch_add(1) == 5 && g.load(std::memory_order_relaxed) == 6);\n"
             "  std::atomic<bool> flag(false); CK(10, !flag.load()); flag.store(true, std::memory_order_release); CK(11, flag.load(std::memory_order_acquire));\n"
             "  CK(12, !flag.exchange(false) == false && !flag.load());\n"
             "  std::atomic<unsigned long long> big(0); big.fetch_add(1ull << 40); CK(13, big.load() == (1ull << 40));\n"
             "  std::atomic<float> fl(1.5f); fl.store(2.5f); CK(14, fl.load() == 2.5f);\n"
             "  int arr[4] = {10, 20, 30, 40}; std::atomic<int*> ap(arr); CK(15, *ap.load() == 10);\n"
             "  int* o = ap.fetch_add(2); CK(16, *o == 10 && *ap.load() == 30); ++ap; CK(17, *ap.load() == 40);\n"
             "  int* ex = arr + 3; CK(18, ap.compare_exchange_strong(ex, arr) && *ap.load() == 10);\n"
             "  return 0;\n"
             "}\n",
             0);
    run_both(tally, "std_functional",
             "#include <functional>\n"
             "int entry(void) {\n"
             "  std::hash<int> h; CK(1, h(5) == h(5) && h(5) != h(6));\n"
             "  std::less<int> lt; std::greater<int> gt; std::equal_to<float> eq;\n"
             "  CK(2, lt(1, 2) && !lt(2, 1) && gt(3, 2) && eq(1.5f, 1.5f) && !eq(1.0f, 2.0f));\n"
             "  std::plus<int> pl; std::minus<int> mi; std::multiplies<int> mu; std::negate<int> ng;\n"
             "  CK(3, pl(2, 3) == 5 && mi(2, 3) == -1 && mu(4, 5) == 20 && ng(7) == -7);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_placement_new(Tally& tally) {
    std::printf("test_placement_new\n");
    run_both(tally, "placement_new",
             "#ifdef __GNUC__\n"
             "#include <stdlib.h>\n"
             "#include <new>\n"
             "#else\n"
             "#include <new>\n"
             "char pool[512];\n"
             "void *malloc(unsigned long long n) { return pool; }\n"
             "void free(void *p) { }\n"
             "#endif\n"
             "int g;\n"
             "struct N { int v; N(int a) : v(a) { g += a; } ~N() { g += 100; } };\n"
             "struct W { N a; N b; W(int x) : a(x), b(x + 1) {} };\n"
             "template <typename T> void destroy_it(T* p) { p->~T(); }\n"
             "template <typename T> T* make_at(void* where, const T& v) { return new (where) T(v); }\n"
             "int* static_array() { static int a[4]; return a; }\n"
             "int entry(void) {\n"
             "  char* raw = (char*)malloc(256);\n"
             "  N* p = new (raw) N(5);\n"
             "  CK(1, p->v == 5 && g == 5 && (char*)p == raw);\n"
             "  p->~N(); CK(2, g == 105);\n"
             "  int* ip = new (raw + 64) int(42); CK(3, *ip == 42 && (char*)ip == raw + 64);\n"
             "  float* fp = new ((void*)(raw + 96)) float(2.5f); CK(4, *fp == 2.5f);\n"
             "  g = 0; W* w = new (raw + 128) W(10); CK(5, w->a.v == 10 && w->b.v == 11 && g == 21);\n"
             "  destroy_it(w); CK(6, g == 21 + 100 + 100);\n"
             "  destroy_it(ip); destroy_it(fp);\n"
             "  int* q = make_at<int>(raw + 200, 7); CK(7, *q == 7);\n"
             "  g = 0; N src(3); N* c = make_at<N>(raw + 220, src); CK(8, c->v == 3);\n"
             "  int* sa = static_array(); sa[1] = 3; CK(9, static_array()[1] == 3);\n"
             "  return 0;\n"
             "}\n",
             0);
}

// A bump allocator for the okay-ms side of a program (no C runtime is linked); g++ uses the real one.
static const char* kAlloc =
    "#ifdef __GNUC__\n"
    "#include <stdlib.h>\n"
    "#else\n"
    "char pool_[65536];\n"
    "unsigned long long used_;\n"
    "void *malloc(unsigned long long n) { char *p = pool_ + used_; used_ += ((n + 15) / 16) * 16; return p; }\n"
    "void free(void *p) { }\n"
    "#endif\n";

static void test_std_vector(Tally& tally) {
    std::printf("test_std_vector\n");
    run_both(tally, "std_vector_int",
             std::string(kAlloc) +
             "#include <vector>\n"
             "int entry(void) {\n"
             "  std::vector<int> v;\n"
             "  CK(1, v.size() == 0 && v.empty());\n"
             "  for (int i = 0; i < 20; ++i) v.push_back(i * 3);\n"
             "  CK(2, v.size() == 20 && v[0] == 0 && v[19] == 57 && v.front() == 0 && v.back() == 57 && v.capacity() >= 20);\n"
             "  int s = 0; for (int x : v) s += x; CK(3, s == 570);\n"
             "  v[5] = 100; CK(4, v[5] == 100);\n"
             "  v.pop_back(); CK(5, v.size() == 19 && v.back() == 54);\n"
             "  std::vector<int> w(v); CK(6, w.size() == 19 && w[5] == 100 && w[18] == 54);\n"
             "  w[0] = -1; CK(7, v[0] == 0);\n"
             "  std::vector<int> z; z = w; CK(8, z.size() == 19 && z[0] == -1);\n"
             "  v.clear(); CK(9, v.empty() && w.size() == 19);\n"
             "  std::vector<int> n(5, 7); CK(10, n.size() == 5 && n[4] == 7);\n"
             "  std::vector<int> r(4); CK(11, r.size() == 4 && r[0] == 0 && r[3] == 0);\n"
             "  n.resize(8); CK(12, n.size() == 8 && n[4] == 7 && n[7] == 0); n.resize(2); CK(13, n.size() == 2);\n"
             "  n.insert(n.begin() + 1, 9); CK(14, n.size() == 3 && n[0] == 7 && n[1] == 9 && n[2] == 7);\n"
             "  n.erase(n.begin()); CK(15, n.size() == 2 && n[0] == 9);\n"
             "  n.push_back(n[0]); CK(16, n[2] == 9);\n"
             "  std::vector<int> big; for (int i = 0; i < 1000; ++i) big.push_back(i); CK(17, big[999] == 999 && big[500] == 500);\n"
             "  big.push_back(big[3]); CK(18, big[1000] == 3);\n"
             "  const std::vector<int>& cr = big; int t = 0; for (int x : cr) t += x; CK(19, t == 499500 + 3);\n"
             "  return 0;\n"
             "}\n",
             0);
    run_both(tally, "std_vector_class",
             std::string(kAlloc) +
             "#include <vector>\n"
             "int live;\n"
             "struct C { int v; C() : v(0) { ++live; } C(int a) : v(a) { ++live; } C(const C& o) : v(o.v) { ++live; } ~C() { --live; } C& operator=(const C& o) { v = o.v; return *this; } };\n"
             "struct P { float x, y; };\n"
             "int entry(void) {\n"
             "  {\n"
             "    std::vector<C> v;\n"
             "    for (int i = 0; i < 10; ++i) v.push_back(C(i));\n"
             "    CK(1, v.size() == 10 && live == 10 && v[7].v == 7);\n"
             "    v.pop_back(); CK(2, live == 9);\n"
             "    std::vector<C> w = v; CK(3, live == 18 && w[3].v == 3);\n"
             "    v.erase(v.begin()); CK(4, live == 17 && v[0].v == 1);\n"
             "    v.clear(); CK(5, live == 9);\n"
             "    std::vector<C> e(3); CK(6, live == 12 && e[2].v == 0);\n"
             "  }\n"
             "  CK(7, live == 0);\n"
             "  std::vector<P> ps; for (int i = 0; i < 6; ++i) { P p; p.x = (float)i; p.y = (float)(i * 2); ps.push_back(p); }\n"
             "  CK(8, ps.size() == 6 && ps[5].y == 10.0f);\n"
             "  std::vector<std::vector<int> > g(3); g[1].push_back(4); g[1].push_back(5); g[2].push_back(6);\n"
             "  CK(9, g[0].size() == 0 && g[1].size() == 2 && g[1][1] == 5 && g[2][0] == 6);\n"
             "  std::vector<std::vector<int> > h = g; h[1][0] = 40; CK(10, g[1][0] == 4 && h[1][0] == 40);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_std_string(Tally& tally) {
    std::printf("test_std_string\n");
    run_both(tally, "std_string",
             std::string(kAlloc) +
             "#include <string>\n"
             "static bool same(const std::string& s, const char* t) { return s == t; }\n"
             "int entry(void) {\n"
             "  std::string e; CK(1, e.size() == 0 && e.empty() && e.c_str()[0] == 0);\n"
             "  std::string a(\"hello\"); CK(2, a.size() == 5 && a.length() == 5 && a[0] == 'h' && a[4] == 'o' && a.c_str()[5] == 0);\n"
             "  std::string b = a; b[0] = 'J'; CK(3, same(a, \"hello\") && same(b, \"Jello\"));\n"
             "  a += \" world\"; CK(4, same(a, \"hello world\") && a.size() == 11);\n"
             "  a.push_back('!'); a += '?'; CK(5, same(a, \"hello world!?\"));\n"
             "  std::string c = a + b; CK(6, c.size() == 18 && c[13] == 'J');\n"
             "  std::string d = \"x\" + b; CK(7, same(d, \"xJello\")); std::string f = b + \"y\"; CK(8, same(f, \"Jelloy\"));\n"
             "  CK(9, a == a && a != b && !(a == b) && a == \"hello world!?\" && \"Jello\" == b && b != \"x\");\n"
             "  CK(10, std::string(\"abc\") < std::string(\"abd\") && std::string(\"ab\") < std::string(\"abc\") && std::string(\"b\") > std::string(\"abc\"));\n"
             "  std::string s = a.substr(6, 5); CK(11, same(s, \"world\") && same(a.substr(6), \"world!?\"));\n"
             "  CK(12, a.find('w') == 6 && a.find(\"lo w\") == 3 && a.find('z') == std::string::npos && a.find('l', 4) == 9);\n"
             "  CK(13, a.rfind('l') == 9 && a.find(\"hello\") == 0 && a.find_first_of(\"ow\") == 4);\n"
             "  std::string g(3, 'z'); CK(14, same(g, \"zzz\")); g.resize(5, 'q'); CK(15, same(g, \"zzzqq\")); g.resize(2); CK(16, same(g, \"zz\"));\n"
             "  std::string h(\"abcdef\"); h.erase(1, 2); CK(17, same(h, \"adef\")); h.insert(1, \"XY\"); CK(18, same(h, \"aXYdef\"));\n"
             "  std::string k(\"abcdef\", 3); CK(19, same(k, \"abc\"));\n"
             "  std::string big; for (int i = 0; i < 300; ++i) big.push_back((char)('a' + i % 26)); CK(20, big.size() == 300 && big[299] == 'a' + 299 % 26);\n"
             "  std::string self = \"abc\"; self += self; CK(21, same(self, \"abcabc\"));\n"
             "  int n = 0; for (char ch : std::string(\"xyz\")) n += ch; CK(22, n == 'x' + 'y' + 'z');\n"
             "  CK(23, same(std::to_string(0), \"0\") && same(std::to_string(1234), \"1234\") && same(std::to_string(-56), \"-56\") && same(std::to_string(4000000000u), \"4000000000\"));\n"
             "  std::string cc; cc = \"assigned\"; cc = cc; CK(24, same(cc, \"assigned\") && cc.compare(\"assigned\") == 0 && cc.compare(\"b\") < 0);\n"
             "  cc.clear(); CK(25, cc.empty() && cc.size() == 0);\n"
             "  return 0;\n"
             "}\n",
             0);
}

// The example program: real engine headers (Vec3, Mat4) and our own <vector>, <string>, <array>, <atomic>, built at
// each level, linked, run under Wine, and also built by MinGW g++ (which uses its own library) for the same exit code.
static void test_engine_example(Tally& tally, const std::string& root) {
    std::printf("test_engine_example\n");
    const std::string path = root + "/tools/okayms/examples/engine_math.cpp";
    std::ifstream in_file(path.c_str());
    CHECK(tally, static_cast<bool>(in_file));
    if (!in_file) return;
    std::string source((std::istreambuf_iterator<char>(in_file)), std::istreambuf_iterator<char>());
    const std::string dir = "/tmp/okayms-r21";
    run_shell("mkdir -p " + dir);
    for (int level = 0; level <= 2; ++level) {
        okayms::CompileInput in;
        in.source = source;
        in.opt_level = level;
        in.optimize = level > 0;
        in.include_paths.push_back(root + "/engine/include");
        in.include_paths.push_back(root + "/engine/src");
        in.include_paths.push_back(root + "/shared");
        in.include_paths.push_back(root + "/tools/okayms/stdinc");
        const auto compiled = okayms::compile_c(in);
        CHECK(tally, compiled.ok());
        if (!compiled.ok()) {
            std::fprintf(stderr, "  engine_math /O%d: %s\n", level, compiled.error.c_str());
            continue;
        }
        const auto linked = okayms::link_executable({compiled.value.object}, {});
        CHECK(tally, linked.ok());
        if (!linked.ok()) {
            std::fprintf(stderr, "  engine_math /O%d link: %s\n", level, linked.error.c_str());
            continue;
        }
        const std::string exe = dir + "/engine_math-ok" + std::to_string(level) + ".exe";
        CHECK(tally, write_bytes(exe, linked.value));
        const int code = wine_exit(exe);
        CHECK(tally, code == 0);
        if (code != 0) std::fprintf(stderr, "  engine_math /O%d: exit %d\n", level, code);
    }
    if (run_shell("command -v x86_64-w64-mingw32-g++ >/dev/null") != 0) return;
    const std::string gpp_exe = dir + "/engine_math-gpp.exe";
    const int built = run_shell("x86_64-w64-mingw32-g++ -static -fno-exceptions -fno-rtti -w -I " + root + "/engine/include -I " +
                                root + "/engine/src -I " + root + "/shared -o " + gpp_exe + " " + path + " 2>" + dir +
                                "/engine_math.log");
    CHECK(tally, built == 0);
    if (built != 0) return;
    const int ref = wine_exit(gpp_exe);
    CHECK(tally, ref == 0);
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    Tally tally;
    test_member_aggregates(tally);
    test_cmath_classify(tally);
    test_found_on_the_way(tally);
    test_many_parameters(tally);
    test_std_array_cctype(tally);
    test_member_array_decay(tally);
    test_std_atomic_functional(tally);
    test_placement_new(tally);
    test_std_vector(tally);
    test_std_string(tally);
    test_engine_example(tally, root);
    return tally.finish(11);
}
