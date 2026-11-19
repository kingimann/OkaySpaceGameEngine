// Constructors and destructors for local objects: member initializer lists, base
// constructors, members with their own constructors, an implicit constructor and destructor,
// the table pointer set by each constructor, and destructors that run at the end of a block, before
// return, break and continue. The COFF names follow the MSVC decoration (??0 and ??1), and the
// programs give the same exit code as the same source built by MinGW g++ and run under Wine.
// Written from ISO/IEC 14882 [class.ctor], [class.dtor], [class.base.init] and the public MSVC
// name decoration description.

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

// Builds `source` with okay-cl's compiler and runs it. Returns the exit code
// or -1. When MinGW g++ exists the same source (plus a main that calls entry)
// is built and run too; the two codes must match and equal `want`.
void run_both(Tally& tally, const std::string& name, const std::string& source, int want,
              okayms::ObjectFile* object_out = nullptr) {
    std::printf("ctor program %s\n", name.c_str());
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
    const std::string dir = "/tmp/okayms-ctor";
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

static void test_order(Tally& tally) {
    std::printf("test_order\n");
    run_both(tally, "order",
             "int trace;\n"
             "void t(int d) { trace = trace * 4 + d; }\n"
             "struct A { int v; A(int x) : v(x) { t(1); } ~A() { t(2); } };\n"
             "struct B {\n"
             "  A a;\n"
             "  A b;\n"
             "  int z;\n"
             "  B() : a(1), b(2), z(7) { t(3); }\n"
             "  ~B() { t(3); }\n"
             "};\n"
             "int entry(void) {\n"
             "  {\n"
             "    A x(5);\n"
             "    B y;\n"
             "    t(1);\n"
             "  }\n"
             "  return trace % 256;\n"
             "}\n",
             234);
}

static void test_ret(Tally& tally) {
    std::printf("test_ret\n");
    run_both(tally, "ret",
             "int g;\n"
             "struct R { int k; R(int a) : k(a) { g = g + a; } ~R() { g = g * 2; } };\n"
             "int f(int n) {\n"
             "  R r(n);\n"
             "  if (n > 2) {\n"
             "    R s(10);\n"
             "    return g + 1;\n"
             "  }\n"
             "  return 0;\n"
             "}\n"
             "int entry(void) {\n"
             "  int a = f(3);\n"
             "  return a + g;\n"
             "}\n",
             66);
}

static void test_loop(Tally& tally) {
    std::printf("test_loop\n");
    run_both(tally, "loop",
             "int cnt;\n"
             "struct D { D() { cnt = cnt + 1; } ~D() { cnt = cnt + 10; } };\n"
             "int entry(void) {\n"
             "  int i;\n"
             "  for (i = 0; i < 3; i = i + 1) {\n"
             "    D d;\n"
             "    if (i == 1) continue;\n"
             "    if (i == 2) break;\n"
             "  }\n"
             "  return cnt;\n"
             "}\n",
             33);
}

static void test_derived(Tally& tally) {
    std::printf("test_derived\n");
    run_both(tally, "derived",
             "int trace;\n"
             "void t(int d) { trace = trace * 4 + d; }\n"
             "struct Base {\n"
             "  int b;\n"
             "  Base(int x) : b(x) { t(1); }\n"
             "  virtual int f() { return 1; }\n"
             "  ~Base() { t(2); }\n"
             "};\n"
             "struct Der : Base {\n"
             "  int d;\n"
             "  Der() : Base(4), d(9) { t(3); }\n"
             "  int f() { return d + b; }\n"
             "  ~Der() { t(1); }\n"
             "};\n"
             "struct Plain { int q; };\n"
             "struct Holder : Plain {\n"
             "  Der inner;\n"
             "  int w;\n"
             "};\n"
             "int entry(void) {\n"
             "  int r = 0;\n"
             "  {\n"
             "    Der x;\n"
             "    Base *p = &x;\n"
             "    r = x.f() + p->f();\n"
             "    t(3);\n"
             "  }\n"
             "  {\n"
             "    Holder h;\n"
             "    h.w = 2;\n"
             "    r = r + h.w;\n"
             "  }\n"
             "  return (r + trace) % 256;\n"
             "}\n",
             146);
}

static void test_names(Tally& tally) {
    std::printf("test_names\n");
    okayms::ObjectFile object;
    run_both(tally, "names",
             "struct P { int v; P(int a) : v(a) {} ~P() {} int get() { return v; } };\n"
             "int entry(void) { P p(3); return p.get() + 4; }\n",
             7, &object);
    CHECK(tally, has_symbol(object, "??0P@@QEAA@H@Z"));
    CHECK(tally, has_symbol(object, "??1P@@QEAA@XZ"));
    CHECK(tally, has_symbol(object, "?get@P@@QEAAHXZ"));
}

int main() {
    Tally tally;
    test_names(tally);
    test_order(tally);
    test_ret(tally);
    test_loop(tally);
    test_derived(tally);
    return tally.finish(5);
}
