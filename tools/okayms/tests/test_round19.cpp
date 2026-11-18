// Round 19 correctness: float and double array elements, float constant folding and static constexpr float
// members, default arguments, a braced return, a conditional that is an lvalue, volatile in casts and
// using namespace. Every program returns 0 when all of its checks hold, or the number of the first check
// that failed. Each is built by okay-cl at /O0, /O1 and /O2 and run under Wine; MinGW g++ builds the same
// text and its exit code must match. Written from ISO/IEC 14882 [dcl.fct.default], [stmt.return],
// [expr.cond], [dcl.type.cv], [namespace.udir], [expr.const], [conv.fpint], [expr.arith.conv].

#include "check.hpp"
#include "okayms/coff.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <cstdlib>
#include <fstream>
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
    std::printf("r19 program %s\n", name.c_str());
    const std::string dir = "/tmp/okayms-r19";
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


static void test_float_arrays(Tally& tally) {
    std::printf("test_float_arrays\n");
    // The first case is the miscompile found by the Round 18 measurement: a[1] * 4.0f gave 5, g++ 10.
    run_both(tally, "fa_small",
             "int entry(void) { float a[2]; a[0] = 1.5f; a[1] = 2.5f; return (int)(a[1] * 4.0f); }\n",
             10);
    run_both(tally, "fa_many",
             "float g[3];\n"
             "double gd[2] = { 1.5, 2.5 };\n"
             "float gf[2] = { 1.5f, 2 };\n"
             "struct V { float x[3]; double d[2]; };\n"
             "float sum(float* p, int n) { float t = 0; for (int i = 0; i < n; i++) t += p[i]; return t; }\n"
             "int entry(void) {\n"
             "  float a[2]; a[0] = 1.5f; a[1] = 2.5f;\n"
             "  CK(1, (int)(a[1] * 4.0f) == 10);\n"
             "  double b[3]; b[0] = 0.5; b[1] = 1.5; b[2] = b[0] + b[1];\n"
             "  CK(2, (int)(b[2] * 10) == 20);\n"
             "  g[0] = 1; g[1] = 2.5f; g[2] = g[0] + g[1];\n"
             "  CK(3, (int)(g[2] * 2) == 7);\n"
             "  CK(4, (int)(gd[0] + gd[1]) == 4);\n"
             "  CK(5, (int)(gf[0] * 2 + gf[1]) == 5);\n"
             "  V v; v.x[0] = 1; v.x[1] = 2; v.x[2] = 3; v.d[0] = 0.25; v.d[1] = 0.5;\n"
             "  CK(6, (int)(v.x[0] + v.x[1] + v.x[2] + v.d[0] * 4 + v.d[1] * 2) == 8);\n"
             "  CK(7, sum(a, 2) == 4.0f);\n"
             "  float m[2][2]; m[0][0] = 1; m[0][1] = 2; m[1][0] = 3; m[1][1] = 4.5f;\n"
             "  CK(8, (int)(m[1][1] * 2) == 9);\n"
             "  float f = 1.5f; f += 2; f *= 2; f -= 0.5f; f /= 2;\n"
             "  CK(9, (int)(f * 4) == 13);\n"
             "  double d = 3; d += 1.5f; d *= 2;\n"
             "  CK(10, d == 9.0);\n"
             "  int n = 7; n += 1.9; CK(11, n == 8);\n"
             "  n = 2.9f; CK(12, n == 2);\n"
             "  float fi = 3; CK(13, fi == 3.0f);\n"
             "  double dv = fi; CK(14, dv == 3.0);\n"
             "  float* pf = a; pf[1] = 9; CK(15, a[1] == 9.0f);\n"
             "  a[0]++; CK(16, a[0] == 2.5f);\n"
             "  if (a[0]) {} else return 17;\n"
             "  float z = 0; if (z) return 18;\n"
             "  CK(19, (a[0] > 1) && (a[0] < 3));\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_float_const(Tally& tally) {
    std::printf("test_float_const\n");
    run_both(tally, "fc_members",
             "struct M {\n"
             "  static constexpr float PI = 3.14159265358979323846f;\n"
             "  static constexpr float Deg2Rad = PI / 180.0f;\n"
             "  static constexpr float Rad2Deg = 180.0f / PI;\n"
             "  static constexpr float Eps = 1e-6f;\n"
             "  static constexpr double D = 1.0 / 3;\n"
             "  static constexpr float Half = 1 / 2.0f;\n"
             "  static constexpr float Neg = -2.5f;\n"
             "  static constexpr int K = 3;\n"
             "  static constexpr float FK = K * 0.5f;\n"
             "  static constexpr long long Big = 1LL << 40;\n"
             "  static constexpr unsigned long long UB = 0xFFFFFFFFFFFFFFFFull;\n"
             "  static constexpr float Whole = 4;\n"
             "  static float scale(float v) { return v * Deg2Rad; }\n"
             "};\n"
             "float g1 = 1.5f * 2;\n"
             "float g2 = 10 / 4.0f;\n"
             "double g3 = (float)1 / 3;\n"
             "int g4 = (int)7.9;\n"
             "float g5 = -0.5f;\n"
             "double g6 = 2.5e3 + 1;\n"
             "float g7[3] = { 1, 2.5f, -3 };\n"
             "int entry(void) {\n"
             "  CK(1, (int)(M::Deg2Rad * 1000000) == 17453);\n"
             "  CK(2, (int)(M::Rad2Deg * 1000) == 57295);\n"
             "  CK(3, M::Half == 0.5f);\n"
             "  CK(4, M::Neg < 0 && M::Neg == -2.5f);\n"
             "  CK(5, M::FK == 1.5f);\n"
             "  CK(6, M::Big == 1099511627776LL && M::UB > 0);\n"
             "  CK(7, g1 == 3.0f && g2 == 2.5f && g5 == -0.5f && g4 == 7);\n"
             "  CK(8, (int)(g3 * 3000) == 1000);\n"
             "  CK(9, M::D * 3 == 1.0);\n"
             "  float local = M::PI * 2; CK(10, (int)(local * 100) == 628);\n"
             "  char buf[(int)(1.5f * 4)]; CK(11, sizeof(buf) == 6);\n"
             "  CK(12, g6 == 2501.0);\n"
             "  CK(13, g7[0] == 1.0f && g7[1] == 2.5f && g7[2] == -3.0f);\n"
             "  CK(14, M::Whole == 4.0f && M::Eps > 0 && M::Eps < 1e-5f);\n"
             "  CK(15, (int)(M::scale(180.0f) * 1000) == 3141);\n"
             "  CK(16, (1.5f < 2) && (2.5 > 1) && !(1.5f == 2));\n"
             "  return 0;\n"
             "}\n",
             0);
    run_both(tally, "fc_global",
             "constexpr float kPi = 3.14159f;\n"
             "constexpr float kTwoPi = kPi * 2;\n"
             "constexpr int kN = 4;\n"
             "constexpr float kQuarter = 1.0f / kN;\n"
             "float table[kN] = { kQuarter, kQuarter * 2, kQuarter * 3, 1 };\n"
             "int entry(void) {\n"
             "  CK(1, (int)(kTwoPi * 1000) == 6283);\n"
             "  CK(2, table[1] == 0.5f && table[3] == 1.0f);\n"
             "  return 0;\n"
             "}\n",
             0);
}


static void test_default_args(Tally& tally) {
    std::printf("test_default_args\n");
    run_both(tally, "da_free",
             "int add(int a, int b = 5) { return a + b; }\n"
             "float scale(float v, float k = 0.5f) { return v * k; }\n"
             "int three(int a = 1, int b = 20, int c = 300) { return a + b + c; }\n"
             "int proto(int x, int y = 7);\n"
             "int proto(int x, int y) { return x * 10 + y; }\n"
             "long long big(long long a, long long b = 4000000000LL) { return a + b; }\n"
             "enum { Base = 100 };\n"
             "int withenum(int a, int b = Base + 1) { return a + b; }\n"
             "int entry(void) {\n"
             "  CK(1, add(1) == 6 && add(1, 2) == 3);\n"
             "  CK(2, scale(8.0f) == 4.0f && scale(8.0f, 2.0f) == 16.0f);\n"
             "  CK(3, three() == 321 && three(2) == 322 && three(2, 3) == 305 && three(2, 3, 4) == 9);\n"
             "  CK(4, proto(3) == 37 && proto(3, 1) == 31);\n"
             "  CK(5, big(1) == 4000000001LL);\n"
             "  CK(6, withenum(1) == 102);\n"
             "  return 0;\n"
             "}\n",
             0);
    run_both(tally, "da_member",
             "struct Timer {\n"
             "  float t; int n;\n"
             "  Timer(float start = 1.5f, int count = 2) { t = start; n = count; }\n"
             "  float step(float dt = 0.25f) { t += dt; n++; return t; }\n"
             "  static int make(int a, int b = 9) { return a * b; }\n"
             "  int get(int extra = 3) const { return n + extra; }\n"
             "};\n"
             "int entry(void) {\n"
             "  Timer a; Timer b(4.0f); Timer c(1.0f, 7);\n"
             "  CK(1, a.t == 1.5f && a.n == 2 && b.t == 4.0f && b.n == 2 && c.n == 7);\n"
             "  CK(2, a.step() == 1.75f && a.step(1.0f) == 2.75f && a.n == 4);\n"
             "  CK(3, Timer::make(2) == 18 && Timer::make(2, 3) == 6);\n"
             "  CK(4, a.get() == 7 && a.get(10) == 14);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_braced_return(Tally& tally) {
    std::printf("test_braced_return\n");
    run_both(tally, "br_agg",
             "struct Rect { float x, y, w, h; };\n"
             "struct P { int a; int b; };\n"
             "struct Q { long long a; int b; };\n"
             "Rect make(float x, float y) { return {x, y, x * 2, y * 2}; }\n"
             "P pair(int a) { return {a, a + 1}; }\n"
             "Q wide(int a) { return {a * 3000000000LL, a}; }\n"
             "P zero() { return {}; }\n"
             "int entry(void) {\n"
             "  Rect r = make(1.0f, 2.0f);\n"
             "  CK(1, r.x == 1.0f && r.y == 2.0f && r.w == 2.0f && r.h == 4.0f);\n"
             "  P p = pair(5); CK(2, p.a == 5 && p.b == 6);\n"
             "  Q q = wide(2); CK(3, q.a == 6000000000LL && q.b == 2);\n"
             "  P z = zero(); CK(4, z.a == 0 && z.b == 0);\n"
             "  return 0;\n"
             "}\n",
             0);
    run_both(tally, "br_ctor",
             "struct V { int x, y; V(int a, int b) { x = a; y = b; } V() { x = 0; y = 0; } };\n"
             "V mk(int a) { return {a, a * 2}; }\n"
             "V plain(int a) { return V(a, 1); }\n"
             "int entry(void) {\n"
             "  V v = mk(4); CK(1, v.x == 4 && v.y == 8);\n"
             "  V w = plain(3); CK(2, w.x == 3 && w.y == 1);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_cond_ref(Tally& tally) {
    std::printf("test_cond_ref\n");
    run_both(tally, "cr_ref",
             "struct T { int v; };\n"
             "const int& pick(const int& a, const int& b, bool first) { return first ? a : b; }\n"
             "int& pick2(int& a, int& b, bool first) { return first ? a : b; }\n"
             "int take(const int& r) { return r + 1; }\n"
             "int entry(void) {\n"
             "  int a = 3, b = 9;\n"
             "  const int& r = a > b ? a : b;\n"
             "  CK(1, r == 9);\n"
             "  CK(2, pick(a, b, true) == 3 && pick(a, b, false) == 9);\n"
             "  pick2(a, b, false) = 50; CK(3, b == 50 && a == 3);\n"
             "  CK(4, take(a < b ? a : b) == 4);\n"
             "  float f = 1.5f, g = 2.5f; const float& m = f > g ? f : g; CK(5, m == 2.5f);\n"
             "  (a < b ? a : b) = 7; CK(6, a == 7);\n"
             "  T t1; T t2; t1.v = 1; t2.v = 2; const T& tr = a > 100 ? t1 : t2; CK(7, tr.v == 2);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_volatile_cast(Tally& tally) {
    std::printf("test_volatile_cast\n");
    run_both(tally, "vc_cast",
             "unsigned char buf[4];\n"
             "unsigned fill(unsigned char* p, int n) {\n"
             "  volatile unsigned char* q = static_cast<volatile unsigned char*>(p);\n"
             "  for (int i = 0; i < n; i++) q[i] = (unsigned char)(i + 1);\n"
             "  return q[0] + q[3];\n"
             "}\n"
             "int entry(void) {\n"
             "  CK(1, fill(buf, 4) == 5);\n"
             "  const volatile unsigned char* c = reinterpret_cast<const volatile unsigned char*>(buf);\n"
             "  CK(2, c[2] == 3);\n"
             "  volatile int v = 4; v += 2; CK(3, v == 6);\n"
             "  const volatile int* cp = static_cast<const volatile int*>(&v); CK(4, *cp == 6);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_using_namespace(Tally& tally) {
    std::printf("test_using_namespace\n");
    run_both(tally, "un_basic",
             "namespace geo { int area(int w, int h) { return w * h; } const int kUnit = 3; struct Pt { int x, y; }; int twice(int v) { return v * 2; } }\n"
             "namespace util { int add(int a, int b) { return a + b; } }\n"
             "using namespace geo;\n"
             "int entry(void) {\n"
             "  using namespace util;\n"
             "  Pt p; p.x = 2; p.y = 5;\n"
             "  CK(1, area(p.x, p.y) == 10);\n"
             "  CK(2, add(1, 2) == 3);\n"
             "  CK(3, twice(kUnit) == 6);\n"
             "  CK(4, geo::area(2, 2) == 4);\n"
             "  return 0;\n"
             "}\n",
             0);
}

int main() {
    Tally tally;
    test_float_arrays(tally);
    test_float_const(tally);
    test_default_args(tally);
    test_braced_return(tally);
    test_cond_ref(tally);
    test_volatile_cast(tally);
    test_using_namespace(tally);
    return tally.finish(7);
}
