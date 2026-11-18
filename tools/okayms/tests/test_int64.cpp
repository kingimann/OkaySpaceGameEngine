// 64-bit integers: long long, unsigned long long, __int64, int64_t, uint64_t, size_t. Every program returns 0
// when all of its checks hold, or the number of the first check that failed. Each is built by okay-cl at
// /O0, /O1 and /O2 and run under Wine; MinGW g++ builds the same text and its exit code must match.
// Written from ISO/IEC 14882 [basic.fundamental], [conv.integral], [conv.fpint], [expr.arith.conv],
// [expr.shift], [lex.icon] and the Windows x64 calling convention (LLP64: long is 32 bits).

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

bool has_symbol(const okayms::ObjectFile& object, const std::string& name) {
    for (const auto& symbol : object.symbols)
        if (symbol.name == name) return true;
    return false;
}

const char* kChecks = "#define CK(n, c) if (!(c)) return n;\n";

// Builds `source` at each optimization level and runs it; MinGW g++ runs the same text.
void run_both(Tally& tally, const std::string& name, const std::string& source, int want,
              okayms::ObjectFile* object_out = nullptr) {
    std::printf("int64 program %s\n", name.c_str());
    const std::string dir = "/tmp/okayms-int64";
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

static void test_arith(Tally& tally) {
    std::printf("test_arith\n");
    run_both(tally, "ll_arith",
             "long long add(long long a, long long b) { return a + b; }\n"
             "long long mul(long long a, long long b) { return a * b; }\n"
             "long long sub(long long a, long long b) { return a - b; }\n"
             "long long dv(long long a, long long b) { return a / b; }\n"
             "long long md(long long a, long long b) { return a % b; }\n"
             "int entry(void) {\n"
             "  long long a = 3000000000LL;\n"
             "  CK(1, sizeof(long long) == 8);\n"
             "  CK(2, a > 2147483647LL);\n"
             "  CK(3, a + a == 6000000000LL);\n"
             "  CK(4, add(a, a) == 6000000000LL);\n"
             "  CK(5, mul(a, 3) == 9000000000LL);\n"
             "  CK(6, sub(1, a) == -2999999999LL);\n"
             "  CK(7, dv(a * 4, 3) == 4000000000LL);\n"
             "  CK(8, md(a, 7) == 3000000000LL % 7);\n"
             "  CK(9, md(a, 7) == 4);\n"
             "  long long b = a; b += 5; b -= 2; b *= 2; b /= 3; b %= 1000000007LL;\n"
             "  CK(10, b == ((3000000003LL * 2) / 3) % 1000000007LL);\n"
             "  CK(11, b == 2000000002LL % 1000000007LL);\n"
             "  long long n = -a;\n"
             "  CK(12, n == -3000000000LL && n < 0);\n"
             "  CK(13, -n == a);\n"
             "  CK(14, dv(-7, 2) == -3 && md(-7, 2) == -1);\n"
             "  long long c = 5; c++; ++c; c--;\n"
             "  CK(15, c == 6);\n"
             "  long long big = 4294967295LL; big++;\n"
             "  CK(16, big == 4294967296LL);\n"
             "  CK(17, (int)(big >> 32) == 1);\n"
             "  CK(18, (int)big == 0);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_unsigned(Tally& tally) {
    std::printf("test_unsigned\n");
    run_both(tally, "ull",
             "typedef unsigned long long u64;\n"
             "u64 half(u64 v) { return v / 2; }\n"
             "u64 rem(u64 v, u64 d) { return v % d; }\n"
             "int entry(void) {\n"
             "  u64 m = 0xFFFFFFFFFFFFFFFFull;\n"
             "  CK(1, m > 5);\n"
             "  CK(2, half(m) == 0x7FFFFFFFFFFFFFFFull);\n"
             "  CK(3, (m >> 63) == 1);\n"
             "  CK(4, rem(m, 10) == 5);\n"
             "  CK(5, m + 1 == 0);\n"
             "  CK(6, (m >> 60) == 15);\n"
             "  long long s = -1;\n"
             "  CK(7, s > 1ULL);\n"
             "  CK(8, (u64)s == m);\n"
             "  CK(9, s >> 10 == -1);\n"
             "  CK(10, (s << 4) == -16);\n"
             "  unsigned int u = 0xFFFFFFFFu;\n"
             "  long long w = u;\n"
             "  CK(11, w == 4294967295LL);\n"
             "  int i = -5;\n"
             "  long long x = i;\n"
             "  CK(12, x == -5 && x < 0);\n"
             "  u64 y = i;\n"
             "  CK(13, y == 0xFFFFFFFFFFFFFFFBull);\n"
             "  CK(14, (-1 < 0u) == 0);\n"
             "  CK(15, (-1LL < 0u) == 1);\n"
             "  CK(16, (unsigned)(m & 0xFFFF) == 65535u);\n"
             "  u64 big = 1ULL << 63;\n"
             "  CK(17, big > 0 && big / 2 == (1ULL << 62));\n"
             "  CK(18, (big | 1) == 0x8000000000000001ull);\n"
             "  CK(19, (m ^ big) == 0x7FFFFFFFFFFFFFFFull);\n"
             "  CK(20, ~0ull == m);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_literals(Tally& tally) {
    std::printf("test_literals\n");
    run_both(tally, "ll_lit",
             "int entry(void) {\n"
             "  CK(1, 4294967296 > 0 && 4294967296 / 2 == 2147483648);\n"
             "  CK(2, (int)(4294967296 >> 32) == 1);\n"
             "  CK(3, 0x80000000 > 0);\n"
             "  CK(4, 2147483648 > 0);\n"
             "  CK(5, sizeof(4294967296) == 8);\n"
             "  CK(6, sizeof(1) == 4 && sizeof(1u) == 4 && sizeof(1ll) == 8 && sizeof(1ull) == 8);\n"
             "  CK(7, 0x7FFFFFFFFFFFFFFFLL > 0);\n"
             "  CK(8, 0xFFFFFFFFFFFFFFFFULL > 0);\n"
             "  CK(9, (-0x7FFFFFFFFFFFFFFFLL - 1) < 0);\n"
             "  CK(10, 1LL << 40 == 1099511627776LL);\n"
             "  CK(11, 100000 * 100000LL == 10000000000LL);\n"
             "  CK(12, (5000000000LL & 0xFFFFFFFF) == 705032704);\n"
             "  long long k = 077777777777;\n"
             "  CK(13, k == 8589934591LL);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_memory(Tally& tally) {
    std::printf("test_memory\n");
    run_both(tally, "ll_mem",
             "struct S { char c; long long v; int i; };\n"
             "struct T { int a; unsigned long long b; };\n"
             "long long g = 5000000000LL;\n"
             "unsigned long long ug = 0xFFFFFFFFFFFFFFFFull;\n"
             "long long arr[4];\n"
             "long long table[3] = { 1LL << 33, -2, 3 };\n"
             "long long sum(long long* p, int n) { long long t = 0; for (int i = 0; i < n; i++) t += p[i]; return t; }\n"
             "void put(long long* p, long long v) { *p = v; }\n"
             "long long& ref(long long& r) { r += 1; return r; }\n"
             "int entry(void) {\n"
             "  CK(1, sizeof(S) == 24 && sizeof(T) == 16);\n"
             "  CK(2, g == 5000000000LL && ug > 1);\n"
             "  g += 1; CK(3, g == 5000000001LL);\n"
             "  for (int i = 0; i < 4; i++) arr[i] = (long long)i * 3000000000LL;\n"
             "  CK(4, arr[3] == 9000000000LL && arr[1] == 3000000000LL);\n"
             "  CK(5, sum(arr, 4) == 18000000000LL);\n"
             "  CK(6, table[0] == 8589934592LL && table[1] == -2 && table[2] == 3);\n"
             "  S s; s.c = 1; s.v = 1LL << 50; s.i = 7;\n"
             "  CK(7, s.v == 1125899906842624LL && s.i == 7 && s.c == 1);\n"
             "  S* sp = &s; sp->v += 1; CK(8, sp->v == 1125899906842625LL);\n"
             "  long long loc = 0; put(&loc, 77000000000LL); CK(9, loc == 77000000000LL);\n"
             "  ref(loc); CK(10, loc == 77000000001LL);\n"
             "  long long* p = arr; CK(11, *(p + 2) == 6000000000LL);\n"
             "  CK(12, (p + 3) - p == 3);\n"
             "  long long loc2[2]; loc2[0] = -1; loc2[1] = 1LL << 62;\n"
             "  CK(13, loc2[0] == -1 && loc2[1] > 0);\n"
             "  int neg = -1; CK(14, arr[neg + 1] == 0);\n"
             "  long long* q = &arr[2]; CK(15, q[neg] == arr[1]);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_convert(Tally& tally) {
    std::printf("test_convert\n");
    run_both(tally, "ll_conv",
             "long long wide(int x) { return x; }\n"
             "int narrow(long long x) { return (int)x; }\n"
             "long long fromd(double d) { return (long long)d; }\n"
             "double tod(long long v) { return (double)v; }\n"
             "unsigned long long uwide(unsigned u) { return u; }\n"
             "int entry(void) {\n"
             "  CK(1, wide(-1) == -1);\n"
             "  CK(2, wide(2147483647) + 1 == 2147483648LL);\n"
             "  CK(3, narrow(4294967297LL) == 1);\n"
             "  CK(4, narrow(-1LL) == -1);\n"
             "  CK(5, fromd(3e10) == 30000000000LL);\n"
             "  CK(6, fromd(-2.9) == -2);\n"
             "  CK(7, tod(30000000000LL) == 3e10);\n"
             "  CK(8, (int)(tod(1LL << 40) / 1099511627776.0) == 1);\n"
             "  CK(9, uwide(0xFFFFFFFFu) == 4294967295ull);\n"
             "  double d = 5000000000LL; CK(10, d == 5e9);\n"
             "  float f = 3000000000LL; CK(11, (long long)f == 3000000000LL);\n"
             "  long long z = 2.5e9; CK(12, z == 2500000000LL);\n"
             "  bool b = 4294967296LL; CK(13, b == 1);\n"
             "  bool c = 0LL; CK(14, c == 0);\n"
             "  long long h = (1LL << 32); if (h) {} else return 15;\n"
             "  CK(16, !(h == 0) && !!h);\n"
             "  CK(17, (h ? 3 : 4) == 3);\n"
             "  CK(18, (h && 1) == 1 && (0LL || h) == 1);\n"
             "  char c8 = (char)h; CK(19, c8 == 0);\n"
             "  short sh = (short)(h + 3); CK(20, sh == 3);\n"
             "  unsigned long long ud = 15000000000ull; double dd = (double)ud; CK(21, dd == 1.5e10);\n"
             "  unsigned long long big = 0x8000000000000000ull; double bd = (double)big; CK(22, bd == 9223372036854775808.0);\n"
             "  unsigned u32 = 4000000000u; double ud32 = u32; CK(23, ud32 == 4e9);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_mixed(Tally& tally) {
    std::printf("test_mixed\n");
    run_both(tally, "ll_mixed",
             "typedef long long i64;\n"
             "int cond(i64 v, int a, int b) { return v ? a : b; }\n"
             "i64 pick(int c, int small, i64 big) { return c ? small : big; }\n"
             "int entry(void) {\n"
             "  i64 a = 10000000000LL; int i = 3; unsigned char uc = 200; short sh = -2;\n"
             "  CK(1, a + i == 10000000003LL);\n"
             "  CK(2, i + a == 10000000003LL);\n"
             "  CK(3, a * sh == -20000000000LL);\n"
             "  CK(4, a + uc == 10000000200LL);\n"
             "  CK(5, cond(a, 1, 2) == 1 && cond(0, 1, 2) == 2);\n"
             "  CK(6, pick(1, -1, a) == -1);\n"
             "  CK(7, pick(0, -1, a) == a);\n"
             "  CK(8, (a > i) && (i < a) && (a != i) && !(a <= i));\n"
             "  CK(9, a / i == 3333333333LL);\n"
             "  CK(10, (a >> i) == 1250000000LL);\n"
             "  CK(11, (i << 40) == 0 || 1);\n"
             "  CK(12, ((i64)i << 40) == 3298534883328LL);\n"
             "  CK(13, (a & 0xFF) == (10000000000LL & 255));\n"
             "  int shift_count = 33; CK(14, (1LL << shift_count) == 8589934592LL);\n"
             "  i64 count = 33; CK(15, (1 << (int)(count - 32)) == 2);\n"
             "  i64 bits = 0; bits |= 1LL << 35; bits ^= 1LL << 34; bits &= ~(1LL << 34); bits <<= 1; bits >>= 2;\n"
             "  CK(16, bits == (1LL << 34));\n"
             "  int lo = 0; lo += a; CK(17, lo == 1410065408);\n"
             "  int dn = 100; dn *= 3LL; CK(18, dn == 300);\n"
             "  i64 sw = 1; switch ((int)sw) { case 1: sw = 9000000000LL; break; default: sw = 0; }\n"
             "  CK(19, sw == 9000000000LL);\n"
             "  i64 loop = 0; for (i64 k = 0; k < 5; k++) loop += k * 1000000000LL;\n"
             "  CK(20, loop == 10000000000LL);\n"
             "  i64 w = 0; while (w < 3000000000LL) w += 1000000000LL;\n"
             "  CK(21, w == 3000000000LL);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_names(Tally& tally) {
    std::printf("test_names\n");
    okayms::CompileInput in;
    in.source =
        "namespace N { long long f(long long x) { return x; } unsigned long long g(unsigned long long a, __int64 b, "
        "unsigned __int64 c) { return a; } }\n"
        "int entry(void) { return (int)N::f(1) + (int)N::g(1, 2, 3); }\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    CHECK(tally, has_symbol(compiled.value.object, "?f@N@@YA_J_J@Z"));
    CHECK(tally, has_symbol(compiled.value.object, "?g@N@@YA_K_K_J_K@Z"));
}

static void test_std(Tally& tally) {
    std::printf("test_std\n");
    run_both(tally, "ll_std",
             "#include <cstdint>\n"
             "#include <cstddef>\n"
             "std::uint64_t mix(std::uint64_t h, std::uint8_t byte) { return (h ^ byte) * 1099511628211ull; }\n"
             "int entry(void) {\n"
             "  std::uint64_t h = 14695981039346656037ull;\n"
             "  const char* s = \"abc\";\n"
             "  for (int i = 0; i < 3; i++) h = mix(h, (std::uint8_t)s[i]);\n"
             "  CK(1, h == 0xE71FA2190541574Bull);\n"
             "  std::int64_t d = -9000000000LL; CK(2, d < 0 && d / 1000000000 == -9);\n"
             "  std::size_t n = sizeof(h); CK(3, n == 8);\n"
             "  std::size_t big = 1; big <<= 40; CK(4, big == 1099511627776ull);\n"
             "  CK(5, sizeof(std::size_t) == 8 && sizeof(std::ptrdiff_t) == 8);\n"
             "  CK(6, sizeof(std::uint64_t) == 8 && sizeof(std::int64_t) == 8);\n"
             "  int ints[3]; std::ptrdiff_t diff = &ints[2] - &ints[0]; CK(7, diff == 2);\n"
             "  CK(8, sizeof(int) - 5 > 0);\n"
             "  return 0;\n"
             "}\n",
             0);
}

int main() {
    Tally tally;
    test_arith(tally);
    test_unsigned(tally);
    test_literals(tally);
    test_memory(tally);
    test_convert(tally);
    test_mixed(tally);
    test_names(tally);
    test_std(tally);
    return tally.finish(8);
}
