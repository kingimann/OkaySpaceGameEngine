// new and delete: malloc then the constructor, the destructor then free, a null pointer does
// nothing, and a virtual destructor reached through the scalar deleting destructor in the table.
// Each program is also built by MinGW g++ under Wine and the exit codes must match.
// Written from ISO/IEC 14882 [expr.new], [expr.delete] and the public MSVC decoration description.
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
    std::printf("new program %s\n", name.c_str());
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
    const std::string dir = "/tmp/okayms-new";
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

static void test_newdel(Tally& tally) {
    std::printf("test_newdel\n");
    run_both(tally, "newdel",
             "#ifdef __GNUC__\n"
             "#include <stdlib.h>\n"
             "#else\n"
             "char pool[2048];\n"
             "int used;\n"
             "void *malloc(unsigned int n) { char *p; p = pool + used; used = used + ((n + 15) / 16) * 16; return (void *)p; }\n"
             "void free(void *p) { }\n"
             "#endif\n"
             "\n"
             "int g;\n"
             "struct N { int v; N(int a) : v(a) { g = g + a; } ~N() { g = g * 2; } };\n"
             "int entry(void) {\n"
             "  N *p = new N(5);\n"
             "  N *q = new N(7);\n"
             "  int r = p->v + q->v;\n"
             "  delete p;\n"
             "  delete q;\n"
             "  int *z = new int;\n"
             "  *z = 3;\n"
             "  r = r + *z;\n"
             "  delete z;\n"
             "  N *none = 0;\n"
             "  delete none;\n"
             "  return r + g;\n"
             "}\n",
             63);
}

static void test_vdel(Tally& tally) {
    std::printf("test_vdel\n");
    run_both(tally, "vdel",
             "#ifdef __GNUC__\n"
             "#include <stdlib.h>\n"
             "#else\n"
             "char pool[2048];\n"
             "int used;\n"
             "void *malloc(unsigned int n) { char *p; p = pool + used; used = used + ((n + 15) / 16) * 16; return (void *)p; }\n"
             "void free(void *p) { }\n"
             "#endif\n"
             "\n"
             "int g;\n"
             "struct B {\n"
             "  int b;\n"
             "  B(int x) : b(x) { g = g + 1; }\n"
             "  virtual ~B() { g = g + 10; }\n"
             "  virtual int f() { return 1; }\n"
             "};\n"
             "struct D : B {\n"
             "  D() : B(2) { g = g + 100; }\n"
             "  ~D() { g = g + 1000; }\n"
             "  int f() { return 5; }\n"
             "};\n"
             "int entry(void) {\n"
             "  B *p = new D();\n"
             "  int r = p->f();\n"
             "  delete p;\n"
             "  {\n"
             "    D local;\n"
             "    r = r + local.f();\n"
             "  }\n"
             "  return (g + r) % 256;\n"
             "}\n",
             184);
}

int main() {
    Tally tally;
    test_newdel(tally);
    test_vdel(tally);
    return tally.finish(2);
}
