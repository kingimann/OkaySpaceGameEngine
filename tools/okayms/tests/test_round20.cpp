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

int main() {
    Tally tally;
    test_member_operators(tally);
    test_free_operators(tally);
    test_incdec_conv(tally);
    test_overload_types(tally);
    test_operator_more(tally);
    return tally.finish(5);
}
