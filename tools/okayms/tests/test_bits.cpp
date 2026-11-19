// bool and the bit operators: `bool` (one byte, a stored value is 0 or 1), `true`, `false`, the binary
// operators & | ^ << >> and their assignment forms, the unary ~, constant folding of them, and fold
// expressions over them. Each program is also built by MinGW g++ and run under Wine; the exit codes must
// match. Written from ISO/IEC 14882 [basic.fundamental], [conv.bool], [expr.shift], [expr.bit.and],
// [expr.xor], [expr.or], [expr.prim.fold].

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
    std::printf("bits program %s\n", name.c_str());
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
    const std::string dir = "/tmp/okayms-bits";
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


static void test_bool(Tally& tally) {
    std::printf("test_bool\n");
    run_both(tally, "bool1",
             "struct S { bool a; bool b; int c; };\n"
             "bool g = true;\n"
             "bool arr[3];\n"
             "bool flip(bool x) { return !x; }\n"
             "bool norm(int v) { return v; }\n"
             "int val(bool x) { return x + 0; }\n"
             "int entry(void) {\n"
             "  bool a = true; bool b = false; int n = 0;\n"
             "  if (a && !b) n = n + 1;\n"
             "  bool c = 5; n = n + c;\n"
             "  bool d = 256; n = n + d * 10;\n"
             "  if (norm(512)) n = n + 20;\n"
             "  n = n + val(256) * 40;\n"
             "  n = n + (int)(bool)768 * 3;\n"
             "  S s; s.a = true; s.b = 7; n = n + s.b + sizeof(S);\n"
             "  arr[1] = 3; n = n + arr[1] + arr[0];\n"
             "  if (g) n = n + 1;\n"
             "  if (flip(g)) n = n - 100;\n"
             "  bool e = a == b; n = n + e;\n"
             "  bool f = n > 3; n = n + f;\n"
             "  return n;\n"
             "}\n",
             87);
    // The decorated name of a bool parameter is _N.
    okayms::CompileInput in;
    in.source = "namespace N { int f(bool x) { return x; } }\nint entry(void) { return N::f(true); }\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (compiled.ok()) CHECK(tally, has_symbol(compiled.value.object, "?f@N@@YAH_N@Z"));
    else std::fprintf(stderr, "  %s\n", compiled.error.c_str());
}

static void test_bits(Tally& tally) {
    std::printf("test_bits\n");
    run_both(tally, "bits1",
             "int sh(int a, int n) { return a << n; }\n"
             "int sr(int a, int n) { return a >> n; }\n"
             "unsigned usr(unsigned a, int n) { return a >> n; }\n"
             "int entry(void) {\n"
             "  int a = 12; int r = 0;\n"
             "  r = r + (a & 5);\n"
             "  r = r + ((a | 3) ^ 1);\n"
             "  r = r + (a << 2);\n"
             "  r = r + (a >> 1);\n"
             "  r = r + (1 | 2 ^ 3 & 4);\n"
             "  r = r + (1 << 2 + 1);\n"
             "  r = r + ((a & 4) == 4);\n"
             "  r = r + (~a & 15);\n"
             "  r = r + sh(3, 4) + sr(-64, 3) + 20;\n"
             "  r = r + (usr(0x80000000u, 31)) * 2;\n"
             "  int x = 1; x <<= 4; x |= 3; x &= 0x1D; x ^= 6; x >>= 1;\n"
             "  r = r + x;\n"
             "  return r;\n"
             "}\n",
             160);
    // A shift closing two template argument lists is still two '>'.
    run_both(tally, "bits_tmpl",
             "template <typename T> struct Box { T v; };\n"
             "template <typename T> struct Pair { T a; T b; };\n"
             "int entry(void) { Pair<Box<int>> p; p.a.v = 6; p.b.v = 2; Pair<Box<int> > q; q.a.v = 1; return (p.a.v >> 1) + (p.b.v << 2) + q.a.v; }\n",
             12);
}

static void test_const_bits(Tally& tally) {
    std::printf("test_const_bits\n");
    run_both(tally, "bitsconst",
             "int g = (1 << 4) | 3;\n"
             "int a[1 << 3];\n"
             "enum { E = 1 << 2, F = E | 1 };\n"
             "int entry(void) {\n"
             "  switch (g & 3) {\n"
             "    case 1 << 1 | 1: return sizeof(a) + (0xF0 >> 4) + (6 ^ 3) + (~0 & 7) + (-16 >> 2) + 10 + F;\n"
             "  }\n"
             "  return 1;\n"
             "}\n",
             70);
}

static void test_fold_bits(Tally& tally) {
    std::printf("test_fold_bits\n");
    run_both(tally, "foldbits",
             "template <typename... Ts> int orall(Ts... xs) { return (xs | ...); }\n"
             "template <typename... Ts> int andall(Ts... xs) { return (... & xs); }\n"
             "template <typename... Ts> int xorall(Ts... xs) { return (0 ^ ... ^ xs); }\n"
             "template <typename... Ts> int shl(Ts... xs) { return (1 << ... << xs); }\n"
             "template <typename... Ts> int shr(Ts... xs) { return (xs >> ...); }\n"
             "int entry(void) { return orall(1, 2, 4) + andall(7, 5, 13) + xorall(1, 2, 3, 4) + shl(1, 2) + shr(64, 4, 1); }\n",
             40);
}

int main() {
    Tally tally;
    test_bool(tally);
    test_bits(tally);
    test_const_bits(tally);
    test_fold_bits(tally);
    return tally.finish(1);
}
