// Round 20: operator overloading, overloading by parameter type, f().x on a struct result, a braced return for classes
// with a destructor, auto, range for, lambdas. Every program returns 0 when all of its checks hold, or the number of the first check
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
    std::printf("r20 program %s\n", name.c_str());
    const std::string dir = "/tmp/okayms-r20";
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


static void test_member_operators(Tally& tally) {
    std::printf("test_member_operators\n");
    okayms::ObjectFile obj;
    run_both(tally, "op_member",
             "struct V {\n"
             "  int x; int y;\n"
             "  V() { x = 0; y = 0; }\n"
             "  V(int a, int b) { x = a; y = b; }\n"
             "  V operator+(const V& o) const { return V(x + o.x, y + o.y); }\n"
             "  V operator-(const V& o) const { return V(x - o.x, y - o.y); }\n"
             "  V operator*(int k) const { return V(x * k, y * k); }\n"
             "  V operator/(int k) const { return V(x / k, y / k); }\n"
             "  V operator-() const { return V(-x, -y); }\n"
             "  bool operator==(const V& o) const { return x == o.x && y == o.y; }\n"
             "  bool operator!=(const V& o) const { return !(*this == o); }\n"
             "  bool operator<(const V& o) const { return x < o.x; }\n"
             "  bool operator>(const V& o) const { return x > o.x; }\n"
             "  V& operator+=(const V& o) { x += o.x; y += o.y; return *this; }\n"
             "  V& operator-=(const V& o) { x -= o.x; y -= o.y; return *this; }\n"
             "  int& operator[](int i) { return i == 0 ? x : y; }\n"
             "  int operator()(int k) const { return x * k + y; }\n"
             "};\n"
             "int entry(void) {\n"
             "  V a(3, 4); V b(1, 2);\n"
             "  V c = a + b; CK(1, c.x == 4 && c.y == 6);\n"
             "  V d = a - b; CK(2, d.x == 2 && d.y == 2);\n"
             "  V e = a * 3; CK(3, e.x == 9 && e.y == 12);\n"
             "  V f = e / 3; CK(4, f.x == 3 && f.y == 4);\n"
             "  V g = -a; CK(5, g.x == -3 && g.y == -4);\n"
             "  CK(6, a == f); CK(7, a != b); CK(8, !(a == b));\n"
             "  CK(9, b < a); CK(10, a > b); CK(11, !(a < b));\n"
             "  a += b; CK(12, a.x == 4 && a.y == 6);\n"
             "  a -= b; CK(13, a.x == 3 && a.y == 4);\n"
             "  CK(14, a[0] == 3 && a[1] == 4);\n"
             "  a[1] = 9; CK(15, a.y == 9);\n"
             "  a[0] += 2; CK(16, a.x == 5);\n"
             "  CK(17, a(2) == 19);\n"
             "  V h = a + b + b; CK(18, h.x == 7 && h.y == 13);\n"
             "  V* p = &a; CK(19, (*p + b).x == 6);\n"
             "  CK(20, ((a + b) * 2).y == 22);\n"
             "  V k; k = a + b; CK(21, k.x == 6);\n"
             "  if (a == a) {} else return 22;\n"
             "  int s = 0; for (int i = 0; i < 3; i++) s += a[0]; CK(23, s == 15);\n"
             "  return 0;\n"
             "}\n",
             0, &obj);
    CHECK(tally, has_symbol(obj, "??HV@@QEBA?AU1@AEBU1@@Z") || has_symbol(obj, "??HV@@QEBA?AUV@@AEBU1@@Z") ||
                     has_symbol(obj, "??HV@@QEAA?AUV@@AEBUV@@@Z") || has_symbol(obj, "??HV@@QEBA?AUV@@AEBUV@@@Z"));
}

static void test_free_operators(Tally& tally) {
    std::printf("test_free_operators\n");
    run_both(tally, "op_free",
             "struct V { int x; int y; };\n"
             "V mk(int a, int b) { V v; v.x = a; v.y = b; return v; }\n"
             "V operator+(const V& a, const V& b) { return mk(a.x + b.x, a.y + b.y); }\n"
             "V operator*(int k, const V& a) { return mk(a.x * k, a.y * k); }\n"
             "V operator*(const V& a, int k) { return mk(a.x * k + 1, a.y * k + 1); }\n"
             "V operator-(const V& a) { return mk(-a.x, -a.y); }\n"
             "bool operator==(const V& a, const V& b) { return a.x == b.x && a.y == b.y; }\n"
             "bool operator!=(const V& a, const V& b) { return !(a == b); }\n"
             "V& operator+=(V& a, const V& b) { a.x += b.x; a.y += b.y; return a; }\n"
             "int operator<<(const V& a, int s) { return a.x << s; }\n"
             "int entry(void) {\n"
             "  V a = mk(3, 4); V b = mk(1, 2);\n"
             "  V c = a + b; CK(1, c.x == 4 && c.y == 6);\n"
             "  V d = 2 * a; CK(2, d.x == 6 && d.y == 8);\n"
             "  V e = a * 2; CK(3, e.x == 7 && e.y == 9);\n"
             "  V f = -a; CK(4, f.x == -3);\n"
             "  CK(5, a == a); CK(6, a != b);\n"
             "  a += b; CK(7, a.x == 4 && a.y == 6);\n"
             "  CK(8, (a << 2) == 16);\n"
             "  V g = a + b + b; CK(9, g.x == 6);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_incdec_conv(Tally& tally) {
    std::printf("test_incdec_conv\n");
    run_both(tally, "op_incdec",
             "struct C {\n"
             "  int n;\n"
             "  C() { n = 0; }\n"
             "  C& operator++() { n += 1; return *this; }\n"
             "  C operator++(int) { C old; old.n = n; n += 1; return old; }\n"
             "  C& operator--() { n -= 1; return *this; }\n"
             "  C operator--(int) { C old; old.n = n; n -= 1; return old; }\n"
             "  operator int() const { return n; }\n"
             "  explicit operator bool() const { return n != 0; }\n"
             "};\n"
             "int entry(void) {\n"
             "  C c; CK(1, c.n == 0);\n"
             "  ++c; CK(2, c.n == 1);\n"
             "  C d = c++; CK(3, d.n == 1 && c.n == 2);\n"
             "  --c; CK(4, c.n == 1);\n"
             "  C e = c--; CK(5, e.n == 1 && c.n == 0);\n"
             "  c++; c++; CK(6, c.n == 2);\n"
             "  int v = (int)c; CK(7, v == 2);\n"
             "  int w = c; CK(8, w == 2);\n"
             "  CK(9, c + 1 == 3);\n"
             "  if (c) {} else return 10;\n"
             "  C z; if (z) return 11;\n"
             "  CK(12, static_cast<int>(c) == 2);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_overload_types(Tally& tally) {
    std::printf("test_overload_types\n");
    okayms::ObjectFile obj;
    run_both(tally, "ov_free",
             "int f(int x) { return x + 1; }\n"
             "int f(double x) { return (int)(x * 2); }\n"
             "int f(char* s) { return s[0]; }\n"
             "int f(int a, int b) { return a * b; }\n"
             "struct P { int v; };\n"
             "int f(P p) { return p.v + 100; }\n"
             "int f(P* p) { return p->v + 200; }\n"
             "int g(long long x) { return 1; }\n"
             "int g(float x) { return 2; }\n"
             "int entry(void) {\n"
             "  char buf[2]; buf[0] = 65; buf[1] = 0;\n"
             "  P p; p.v = 5;\n"
             "  CK(1, f(3) == 4);\n"
             "  CK(2, f(2.5) == 5);\n"
             "  CK(3, f(buf) == 65);\n"
             "  CK(4, f(3, 4) == 12);\n"
             "  CK(5, f(p) == 105);\n"
             "  CK(6, f(&p) == 205);\n"
             "  CK(7, g(5LL) == 1);\n"
             "  CK(8, g(1.5f) == 2);\n"
             "  return 0;\n"
             "}\n",
             0, &obj);
    CHECK(tally, has_symbol(obj, "?f@@YAHH@Z"));
    CHECK(tally, has_symbol(obj, "?f@@YAHN@Z"));
    CHECK(tally, has_symbol(obj, "?f@@YAHPEAD@Z"));
    CHECK(tally, has_symbol(obj, "?f@@YAHHH@Z"));
    CHECK(tally, has_symbol(obj, "?f@@YAHUP@@@Z"));
    CHECK(tally, has_symbol(obj, "?f@@YAHPEAUP@@@Z"));
    run_both(tally, "ov_member",
             "struct S {\n"
             "  int a;\n"
             "  int put(int x) { return x; }\n"
             "  int put(double x) { return 2; }\n"
             "  int put(const S& s) { return s.a; }\n"
             "  int put(int x, int y) { return x + y; }\n"
             "  int put(char* s) { return 9; }\n"
             "};\n"
             "int entry(void) {\n"
             "  S s; s.a = 7; char c[1];\n"
             "  CK(1, s.put(4) == 4); CK(2, s.put(1.5) == 2); CK(3, s.put(s) == 7);\n"
             "  CK(4, s.put(1, 2) == 3); CK(5, s.put(c) == 9);\n"
             "  S* p = &s; CK(6, p->put(5) == 5 && p->put(2.0) == 2);\n"
             "  return 0;\n"
             "}\n",
             0, &obj);
    CHECK(tally, has_symbol(obj, "?put@S@@QEAAHH@Z"));
    CHECK(tally, has_symbol(obj, "?put@S@@QEAAHN@Z"));
    CHECK(tally, has_symbol(obj, "?put@S@@QEAAHAEBUS@@@Z") || has_symbol(obj, "?put@S@@QEAAHAEAUS@@@Z"));
}

static void test_operator_more(Tally& tally) {
    std::printf("test_operator_more\n");
    run_both(tally, "op_more",
             "struct A {\n"
             "  int v[3]; int calls;\n"
             "  A() { v[0] = 0; v[1] = 0; v[2] = 0; calls = 0; }\n"
             "  int& operator[](int i) { calls += 1; return v[i]; }\n"
             "  const int& operator[](int i) const { return v[i] + 100 > 0 ? v[i] : v[0]; }\n"
             "  A& operator=(const A& o) { v[0] = o.v[0] + 1; v[1] = o.v[1]; v[2] = o.v[2]; return *this; }\n"
             "  A operator<<(int n) const { A r; r.v[0] = v[0] << n; r.v[1] = v[1] << n; return r; }\n"
             "  A operator%(int n) const { A r; r.v[0] = v[0] % n; return r; }\n"
             "  A operator&(const A& o) const { A r; r.v[0] = v[0] & o.v[0]; return r; }\n"
             "  A operator|(const A& o) const { A r; r.v[0] = v[0] | o.v[0]; return r; }\n"
             "  A operator^(const A& o) const { A r; r.v[0] = v[0] ^ o.v[0]; return r; }\n"
             "  bool operator<=(const A& o) const { return v[0] <= o.v[0]; }\n"
             "  bool operator>=(const A& o) const { return v[0] >= o.v[0]; }\n"
             "  bool operator!() const { return v[0] == 0; }\n"
             "  A operator~() const { A r; r.v[0] = ~v[0]; return r; }\n"
             "  A& operator*=(int k) { v[0] *= k; return *this; }\n"
             "  A& operator/=(int k) { v[0] /= k; return *this; }\n"
             "};\n"
             "double operator*(const A& a, double d) { return a.v[0] * d; }\n"
             "int entry(void) {\n"
             "  A a; a[0] = 12; a[1] = 5;\n"
             "  CK(1, a.calls == 2 && a.v[0] == 12);\n"
             "  const A& c = a; CK(2, c[1] == 5 && a.calls == 2);\n"
             "  A b; b = a; CK(3, b.v[0] == 13 && b.v[1] == 5);\n"
             "  A s = a << 2; CK(4, s.v[0] == 48 && s.v[1] == 20);\n"
             "  A m = a % 5; CK(5, m.v[0] == 2);\n"
             "  A x = a & b; CK(6, x.v[0] == (12 & 13));\n"
             "  A y = a | b; CK(7, y.v[0] == (12 | 13));\n"
             "  A z = a ^ b; CK(8, z.v[0] == (12 ^ 13));\n"
             "  CK(9, a <= b); CK(10, b >= a); CK(11, !(b <= a));\n"
             "  A e; CK(12, !e); CK(13, !(!a));\n"
             "  A n = ~a; CK(14, n.v[0] == ~12);\n"
             "  a *= 2; CK(15, a.v[0] == 24);\n"
             "  a /= 4; CK(16, a.v[0] == 6);\n"
             "  CK(17, a * 1.5 == 9.0);\n"
             "  a[2] = a[0] + a[1]; CK(18, a.v[2] == 11);\n"
             "  return 0;\n"
             "}\n",
             0);
    run_both(tally, "op_dtor",
             "int live = 0;\n"
             "struct N {\n"
             "  int v;\n"
             "  N(int a) { v = a; live += 1; }\n"
             "  N(const N& o) { v = o.v; live += 1; }\n"
             "  ~N() { live -= 1; }\n"
             "  N operator+(const N& o) const { return N(v + o.v); }\n"
             "  bool operator==(const N& o) const { return v == o.v; }\n"
             "};\n"
             "int entry(void) {\n"
             "  {\n"
             "    N a(1); N b(2);\n"
             "    N c = a + b; CK(1, c.v == 3);\n"
             "    N d = a + b + c; CK(2, d.v == 6);\n"
             "    CK(3, (a + b) == c);\n"
             "  }\n"
             "  CK(4, live == 0);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_struct_results(Tally& tally) {
    std::printf("test_struct_results\n");
    run_both(tally, "sr_field",
             "struct P { int x; int y; double d; };\n"
             "P mk(int a, int b) { P p; p.x = a; p.y = b; p.d = 2.5; return p; }\n"
             "struct Q { P in; int z; };\n"
             "Q mq(int a) { Q q; q.in = mk(a, a + 1); q.z = a * 2; return q; }\n"
             "struct W { int k; P get() const { return mk(k, k * 3); } int twice() const { return k * 2; } };\n"
             "int entry(void) {\n"
             "  CK(1, mk(3, 4).x == 3);\n"
             "  CK(2, mk(3, 4).y == 4);\n"
             "  CK(3, mk(3, 4).d == 2.5);\n"
             "  int s = mk(1, 2).x + mk(10, 20).y; CK(4, s == 21);\n"
             "  CK(5, mq(5).in.y == 6 && mq(5).z == 10);\n"
             "  W w; w.k = 7; CK(6, w.get().y == 21 && w.get().x == 7);\n"
             "  P* pp = 0; P p = mk(8, 9); pp = &p; CK(7, pp->x == 8);\n"
             "  int loop = 0; for (int i = 0; i < 4; i++) loop += mk(i, 1).x; CK(8, loop == 6);\n"
             "  return 0;\n"
             "}\n",
             0);
    run_both(tally, "sr_dtor_return",
             "int live = 0;\n"
             "struct N {\n"
             "  int v;\n"
             "  N(int a) { v = a; live += 1; }\n"
             "  N(int a, int b) { v = a + b; live += 1; }\n"
             "  N(const N& o) { v = o.v; live += 1; }\n"
             "  ~N() { live -= 1; }\n"
             "};\n"
             "N named(int k) { N r(k); return r; }\n"
             "N temp(int k) { return N(k); }\n"
             "N braced(int a, int b) { return {a, b}; }\n"
             "N pick(int c) { N a(1); N b(2); if (c) return a; return b; }\n"
             "int seen(int k) { return named(k).v + temp(k).v + braced(k, 1).v; }\n"
             "int entry(void) {\n"
             "  {\n"
             "    N a = named(3); CK(1, a.v == 3 && live == 1);\n"
             "    N b = temp(4); CK(2, b.v == 4 && live == 2);\n"
             "    N c = braced(5, 6); CK(3, c.v == 11 && live == 3);\n"
             "    N d = pick(1); N e = pick(0); CK(4, d.v == 1 && e.v == 2 && live == 5);\n"
             "  }\n"
             "  CK(5, live == 0);\n"
             "  CK(6, seen(2) == 2 + 2 + 3);\n"
             "  CK(7, live == 0);\n"
             "  CK(8, named(9).v == 9 && live == 1);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_auto_range(Tally& tally) {
    std::printf("test_auto_range\n");
    run_both(tally, "auto_basic",
             "struct P { int x; int y; };\n"
             "P mk(int a, int b) { P p; p.x = a; p.y = b; return p; }\n"
             "double half(int v) { return v / 2.0; }\n"
             "int entry(void) {\n"
             "  auto a = 5; auto d = 2.5; auto s = \"hi\"; auto p = &a; auto q = a + 1; auto f = 1.5f;\n"
             "  CK(1, a == 5 && d == 2.5 && s[0] == 104 && *p == 5 && q == 6 && f == 1.5f);\n"
             "  auto m = mk(3, 4); CK(2, m.x == 3 && m.y == 4);\n"
             "  auto h = half(7); CK(3, h == 3.5);\n"
             "  auto ll = 3000000000LL; CK(4, ll == 3000000000LL && ll > 2147483647LL);\n"
             "  auto& r = a; r = 9; CK(5, a == 9);\n"
             "  const auto k = a * 2; CK(6, k == 18);\n"
             "  auto* pp = &m; CK(7, pp->x == 3);\n"
             "  auto c = 'x'; CK(8, c == 120);\n"
             "  auto u = 7u; CK(9, u == 7u);\n"
             "  int arr[3]; arr[0] = 4; auto e = arr; CK(10, e[0] == 4);\n"
             "  auto t1 = 1, t2 = 2; CK(11, t1 + t2 == 3);\n"
             "  return 0;\n"
             "}\n",
             0);
    run_both(tally, "range_for",
             "struct P { int x; int y; };\n"
             "int g[4] = { 1, 2, 3, 4 };\n"
             "struct H { int a[3]; int total() { int t = 0; for (int v : a) t += v; return t; } };\n"
             "int entry(void) {\n"
             "  int arr[3]; arr[0] = 1; arr[1] = 2; arr[2] = 3;\n"
             "  int sum = 0; for (int v : arr) sum += v; CK(1, sum == 6);\n"
             "  for (auto& r : arr) r = r * 2; CK(2, arr[2] == 6 && arr[0] == 2);\n"
             "  int t = 0; for (auto v : g) t += v; CK(3, t == 10);\n"
             "  P ps[2]; ps[0].x = 1; ps[0].y = 2; ps[1].x = 3; ps[1].y = 4;\n"
             "  int u = 0; for (const P& e : ps) u += e.x * e.y; CK(4, u == 14);\n"
             "  for (P e : ps) u += e.x; CK(5, u == 18);\n"
             "  int n = 0; for (int v : arr) { if (v == 4) continue; if (v == 6) break; n += v; } CK(6, n == 2);\n"
             "  double ds[2]; ds[0] = 0.5; ds[1] = 1.5; double dt = 0; for (double v : ds) dt += v; CK(7, dt == 2.0);\n"
             "  H h; h.a[0] = 5; h.a[1] = 6; h.a[2] = 7; CK(8, h.total() == 18);\n"
             "  int cnt = 0; for (int a : arr) for (int b : arr) cnt += a < b; CK(9, cnt == 3);\n"
             "  return 0;\n"
             "}\n",
             0);
}

static void test_lambdas(Tally& tally) {
    std::printf("test_lambdas\n");
    run_both(tally, "lam_plain",
             "int apply(int (*f)(int), int v) { return f(v); }\n"
             "int twice(int (*f)(int, int), int a, int b) { return f(a, b) * 2; }\n"
             "int entry(void) {\n"
             "  auto sq = [](int x) { return x * x; };\n"
             "  CK(1, sq(5) == 25);\n"
             "  CK(2, apply(sq, 4) == 16);\n"
             "  CK(3, apply([](int x) { return x + 1; }, 4) == 5);\n"
             "  auto d = [](double v) { return v * 0.5; }; CK(4, d(5.0) == 2.5);\n"
             "  auto v = []() { return 42; }(); CK(5, v == 42);\n"
             "  auto add = [](int a, int b) -> int { return a + b; }; CK(6, twice(add, 3, 4) == 14);\n"
             "  auto none = [](int x) { x += 1; }; none(3);\n"
             "  auto cmp = [](int a, int b) { return a < b; }; CK(7, cmp(1, 2) && !cmp(2, 1));\n"
             "  int arr[3]; arr[0] = 1; arr[1] = 2; arr[2] = 3;\n"
             "  auto sum = [](int* p, int n) { int t = 0; for (int i = 0; i < n; i++) t += p[i]; return t; };\n"
             "  CK(8, sum(arr, 3) == 6);\n"
             "  int t = 0; for (int e : arr) t += sq(e); CK(9, t == 14);\n"
             "  auto fp = [](int x) { return x * 3; }; int (*q)(int) = fp; CK(10, q(2) == 6);\n"
             "  return 0;\n"
             "}\n",
             0);
    run_both(tally, "lam_capture",
             "struct P { int x; int y; };\n"
             "template <typename F> int callit(F f, int v) { return f(v); }\n"
             "int entry(void) {\n"
             "  int k = 10;\n"
             "  auto addk = [k](int x) { return x + k; };\n"
             "  CK(1, addk(5) == 15);\n"
             "  k = 99; CK(2, addk(5) == 15);\n"
             "  int n = 0;\n"
             "  auto inc = [&n](int d) { n += d; };\n"
             "  inc(3); inc(4); CK(3, n == 7);\n"
             "  int j = 2;\n"
             "  auto both = [=, &n](int x) { n = x + j; return j * 2; };\n"
             "  CK(4, both(1) == 4 && n == 3);\n"
             "  auto all = [&](int x) { n += x; j += x; return n + j; };\n"
             "  CK(5, all(5) == 15 && n == 8 && j == 7);\n"
             "  P p; p.x = 3; p.y = 4;\n"
             "  auto lenp = [p](int s) { return (p.x * p.x + p.y * p.y) * s; };\n"
             "  CK(6, lenp(2) == 50);\n"
             "  auto bump = [&p]() { p.x += 10; };\n"
             "  bump(); CK(7, p.x == 13);\n"
             "  double sc = 2.5;\n"
             "  auto scale = [sc](double v) { return v * sc; };\n"
             "  CK(8, scale(2.0) == 5.0);\n"
             "  CK(9, callit(addk, 1) == 11);\n"
             "  CK(10, callit([&n](int v) { return v + n; }, 1) == 9);\n"
             "  int total = 0;\n"
             "  for (int i = 0; i < 4; i++) { auto addi = [i, &total]() { total += i; }; addi(); }\n"
             "  CK(11, total == 6);\n"
             "  int arr[3]; arr[0] = 1; arr[1] = 2; arr[2] = 3;\n"
             "  auto first = [&arr](int i) { return arr[i]; };\n"
             "  CK(12, first(2) == 3);\n"
             "  auto sumall = [&]() { int t = 0; for (int i = 0; i < 3; i++) t += arr[i]; return t; };\n"
             "  CK(13, sumall() == 6);\n"
             "  auto isbig = [k](int v) { return v > k; };\n"
             "  CK(14, isbig(100) && !isbig(5));\n"
             "  return 0;\n"
             "}\n",
             0);
}

int main() {
    Tally tally;
    test_member_operators(tally);
    test_free_operators(tally);
    test_incdec_conv(tally);
    test_overload_types(tally);
    test_operator_more(tally);
    test_struct_results(tally);
    test_auto_range(tally);
    test_lambdas(tally);
    return tally.finish(8);
}
