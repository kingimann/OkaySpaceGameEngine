// Copy constructors: a reference parameter, T x(y) and T x = y run the user copy constructor,
// a class passed by value is copied by it (the copy is destroyed after the statement), and a class
// without one gets an implicit copy constructor that copies its base and members.
// Each program is also built by MinGW g++ under Wine and the exit codes must match.
// Written from ISO/IEC 14882 [class.copy.ctor], [dcl.ref], [class.temporary].
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

// Builds `source` with okay-cl's compiler and runs it. Returns the exit code
// or -1. When MinGW g++ exists the same source (plus a main that calls entry)
// is built and run too; the two codes must match and equal `want`.
void run_both(Tally& tally, const std::string& name, const std::string& source, int want,
              okayms::ObjectFile* object_out = nullptr) {
    std::printf("copy program %s\n", name.c_str());
    okayms::CompileInput in;
    in.source = source;
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    if (object_out != nullptr) *object_out = compiled.value.object;
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  %s\n", linked.error.c_str());
        return;
    }
    const std::string dir = "/tmp/okayms-copy";
    run_shell("mkdir -p " + dir);
    const std::string ok_exe = dir + "/" + name + "-ok.exe";
    CHECK(tally, write_bytes(ok_exe, linked.value));
    const int code = wine_exit(ok_exe);
    CHECK(tally, code == want);
    if (code != want) std::fprintf(stderr, "  okay exit %d, wanted %d\n", code, want);
    if (run_shell("command -v x86_64-w64-mingw32-g++ >/dev/null") != 0) {
        std::printf("skipping g++ comparison: MinGW g++ is not installed\n");
        return;
    }
    const std::string src = dir + "/" + name + ".cpp";
    {
        std::ofstream out(src.c_str());
        out << source << "\nint main() { return entry(); }\n";
    }
    const std::string gpp_exe = dir + "/" + name + "-gpp.exe";
    const int built = run_shell("x86_64-w64-mingw32-g++ -static -fno-exceptions -fno-rtti -o " + gpp_exe + " " + src +
                                " 2>" + dir + "/" + name + ".log");
    CHECK(tally, built == 0);
    if (built != 0) return;
    const int ref = wine_exit(gpp_exe);
    CHECK(tally, ref == code);
    if (ref != code) std::fprintf(stderr, "  g++ exit %d, okay exit %d\n", ref, code);
}

}  // namespace

static void test_copy(Tally& tally) {
    std::printf("test_copy\n");
    run_both(tally, "copy",
             "int trace;\n"
             "void t(int d) { trace = trace * 5 + d; }\n"
             "struct A {\n"
             "  int v;\n"
             "  A(int x) : v(x) { t(1); }\n"
             "  A(const A &o) : v(o.v + 1) { t(2); }\n"
             "  ~A() { t(3); }\n"
             "};\n"
             "struct P { int x; int y; };\n"
             "int sum(const P &p) { return p.x + p.y; }\n"
             "void setx(P &p, int n) { p.x = n; }\n"
             "int take(A a) { return a.v; }\n"
             "struct H {\n"
             "  A a;\n"
             "  int z;\n"
             "  H(int q) : a(q), z(q * 2) { }\n"
             "};\n"
             "struct D2 : A {\n"
             "  int w;\n"
             "  D2(int q) : A(q), w(1) { }\n"
             "};\n"
             "int takeH(H x) { return x.a.v + x.z; }\n"
             "int entry(void) {\n"
             "  int r = 0;\n"
             "  P p;\n"
             "  p.x = 1;\n"
             "  p.y = 2;\n"
             "  setx(p, 5);\n"
             "  r = sum(p);\n"
             "  A a(10);\n"
             "  A b(a);\n"
             "  A c = b;\n"
             "  r = r + take(c);\n"
             "  H h(5);\n"
             "  H h2 = h;\n"
             "  r = r + h2.a.v + takeH(h);\n"
             "  D2 d(3);\n"
             "  D2 e = d;\n"
             "  r = r + e.v + c.v;\n"
             "  return (r + trace) % 256;\n"
             "}\n",
             151);
}

static void test_cond(Tally& tally) {
    std::printf("test_cond\n");
    run_both(tally, "cond",
             "int trace;\n"
             "void t(int d) { trace = trace * 5 + d; }\n"
             "struct A {\n"
             "  int v;\n"
             "  A(int x) : v(x) { t(1); }\n"
             "  A(const A &o) : v(o.v + 1) { t(2); }\n"
             "  ~A() { t(3); }\n"
             "};\n"
             "int val(A a) { t(4); return a.v; }\n"
             "int entry(void) {\n"
             "  A a(1);\n"
             "  int r = 0;\n"
             "  if (val(a) > 1) {\n"
             "    t(1);\n"
             "    r = 1;\n"
             "  }\n"
             "  int n = 0;\n"
             "  while (val(a) < 4 && n < 2) {\n"
             "    n = n + 1;\n"
             "    t(2);\n"
             "  }\n"
             "  return (trace + r * 3 + n) % 256;\n"
             "}\n",
             135);
}

// References: bound to temporaries (an expression or a returned object), and returned from functions.
static void test_refs(Tally& tally) {
    std::printf("test_refs\n");
    run_both(tally, "refs",
             "int sum(const int &a, const int &b) { return a + b; }\n"
             "void bump(int &r) { r = r + 1; }\n"
             "struct A {\n"
             "  int v;\n"
             "  A(int x) : v(x) { }\n"
             "};\n"
             "int peek(const A &a) { return a.v; }\n"
             "A makeA(int n) {\n"
             "  A a(n);\n"
             "  return a;\n"
             "}\n"
             "struct P { int x; int y; };\n"
             "P gp;\n"
             "P &theGlobal() { return gp; }\n"
             "int &fieldX(P &p) { return p.x; }\n"
             "struct Counter {\n"
             "  int n;\n"
             "  Counter &inc() { n = n + 1; return *this; }\n"
             "  Counter &add(int k) { n = n + k; return *this; }\n"
             "  int get() const { return n; }\n"
             "};\n"
             "int entry(void) {\n"
             "  int x = 3;\n"
             "  int r = sum(x + 1, 10);\n"
             "  if (sum(r, 2) > 5) r = r + 1;\n"
             "  bump(x);\n"
             "  P a;\n"
             "  a.x = 1;\n"
             "  a.y = 2;\n"
             "  fieldX(a) = 7;\n"
             "  theGlobal().x = 11;\n"
             "  theGlobal().y = 12;\n"
             "  Counter c;\n"
             "  c.n = 0;\n"
             "  c.inc().inc().add(10).inc();\n"
             "  return r + x + peek(makeA(4)) + a.x + gp.x + gp.y + c.get();\n"
             "}\n",
             66);
}

int main() {
    Tally tally;
    test_copy(tally);
    test_cond(tally);
    test_refs(tally);
    return tally.finish(3);
}
