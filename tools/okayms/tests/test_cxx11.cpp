// C++ spellings found by compiling engine headers: const in member declarations, constexpr, inline,
// noexcept, nullptr, enum class and enum with an underlying type, namespace a::b, using Name = type,
// static_assert, static_cast, and line splicing in macros. Each program is also built
// by MinGW g++ and run under Wine; the exit codes must match. Written from ISO/IEC 14882 [dcl.type.cv],
// [dcl.constexpr], [dcl.enum], [namespace.def], [dcl.typedef], [dcl.pre], [expr.static.cast],
// [dcl.fct.default] and [lex.phases].

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
    std::printf("cxx11 program %s\n", name.c_str());
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
    const std::string dir = "/tmp/okayms-cxx11";
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


static void test_cxx11(Tally& tally) {
    std::printf("test_cxx11\n");
    run_both(tally, "spell",
             "namespace geo::unit {\n"
             "constexpr int kScale = 3;\n"
             "inline int scaled(int v) noexcept { return v * kScale; }\n"
             "}\n"
             "namespace geo { using Id = int; static_assert(sizeof(int) == 4, \"int is 4 bytes\"); }\n"
             "struct Tip { int cat; const char* title; char const *tail; };\n"
             "enum class Mode : int { A, B = 5, C };\n"
             "enum Plain : int { P1 = 2, P2 };\n"
             "int pick(Mode m) { if (m == Mode::B) return 40; return 1; }\n"
             "int entry(void) {\n"
             "  Tip t; t.cat = 2; t.title = \"abc\"; t.tail = \"xyz\";\n"
             "  int* none = nullptr;\n"
             "  geo::Id id = 3;\n"
             "  Mode m = Mode::C;\n"
             "  int r = geo::unit::scaled(id) + t.title[1] - 97 + (int)m + pick(Mode::B) + static_cast<int>(Mode::A) + P2;\n"
             "  if (none == nullptr) r = r + 1;\n"
             "  float f = 2.5f;\n"
             "  r = r + static_cast<int>(f * 2.0f);\n"
             "  return r + (int)sizeof(t.cat) + (t.tail[0] == 'x');\n"
             "}\n",
             70);
    run_both(tally, "splice",
             "#define SUM(a, b) \\\n"
             "    ((a) + \\\n"
             "     (b))\n"
             "int entry(void) { return SUM(1, 2) + SUM(3, 4); }\n",
             10);
}

static void test_pointer_index(Tally& tally) {
    std::printf("test_pointer_index\n");
    // p[i] where p is a pointer field, a pointer element or a pointer returned by a call (it used to
    // read the address of the pointer instead of the pointer).
    run_both(tally, "ptrindex",
             "struct Tip { int cat; char* title; };\n"
             "char* name(void) { return \"xyz\"; }\n"
             "int entry(void) {\n"
             "  Tip t; t.title = \"abc\";\n"
             "  char* names[2]; names[0] = \"ab\"; names[1] = \"qrs\";\n"
             "  Tip* pt = &t;\n"
             "  return (t.title[1] - 97) + (names[1][2] - 114) * 10 + (name()[1] - 120) * 100 + (pt->title[2] - 99) * 3;\n"
             "}\n",
             111);
}

static void test_namespace_symbol(Tally& tally) {
    std::printf("test_namespace_symbol\n");
    okayms::CompileInput in;
    in.source = "namespace a::b { int f(int x) { return x; } }\nint entry(void) { return a::b::f(1); }\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    bool found = false;
    for (const auto& symbol : compiled.value.object.symbols)
        if (symbol.name == "?f@b@a@@YAHH@Z") found = true;
    CHECK(tally, found);
}

int main() {
    Tally tally;
    test_cxx11(tally);
    test_pointer_index(tally);
    test_namespace_symbol(tally);
    return tally.finish(1);
}
