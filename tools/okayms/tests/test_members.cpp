// Class and namespace gaps found by compiling engine headers: default member initializers
// (`float x = 0.25f;`), several members in one declaration (`float x, y;`), volatile, const after a
// '*', a call to a function of the enclosing namespace by its bare name, and an unsized array of
// constant pointers. Each program is also built by MinGW g++ and run under Wine; the exit codes must
// match. Written from ISO/IEC 14882 [class.mem], [dcl.init.aggr], [dcl.type.cv], [basic.lookup.unqual].

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
    std::printf("members program %s\n", name.c_str());
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
    const std::string dir = "/tmp/okayms-members";
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


int main() {
    Tally tally;
    std::printf("test_members\n");
    run_both(tally, "members",
             "namespace app {\n"
             "struct Box { float x = 1.5f, y = 2.0f; int n = 3; bool on = true; int plain; };\n"
             "inline int rank(int v) { return v * 2; }\n"
             "inline int twice(int v) { return rank(v) + rank(v); }\n"
             "struct Quad { float a, b, c, d; };\n"
             "const char* const kNames[] = {\"ab\", \"cde\"};\n"
             "}\n"
             "int entry(void) {\n"
             "  app::Box b;\n"
             "  app::Quad q; q.a = 1.0f; q.d = 4.0f;\n"
             "  volatile int v = 2;\n"
             "  int* const p = &b.n;\n"
             "  return (int)(b.x * 2.0f) + (int)b.y + b.n + b.on + app::twice(3) + (int)q.d + v + *p + app::kNames[1][1] - 100;\n"
             "}\n",
             30);
    // A user constructor runs the default initializers of the members it does not name.
    run_both(tally, "ctor_defaults",
             "struct P { int a = 5; int b = 6; int c; P() : b(7) { c = 1; } };\n"
             "struct Q { int k = 4; };\n"
             "int entry(void) { P p; Q q; Q arr[2]; return p.a + p.b * 10 + p.c * 100 + q.k * 1000 % 256 + arr[1].k; }\n",
             (5 + 70 + 100 + (4000 % 256) + 4) % 256);
    // Reading a float member goes to xmm0 (it used to load the bits into eax and leave xmm0 as it was).
    run_both(tally, "float_member",
             "struct V { float x; float y; };\n"
             "int entry(void) { V v; v.x = 1.5f; v.y = 7.0f; float u = v.x; V* pv = &v; return (int)(u * 4.0f) + (int)v.y + (int)(pv->x * 2.0f); }\n",
             16);
    // A cast of a constant is a constant: usable in a global initializer and an array length.
    run_both(tally, "constcast",
             "int arr[5];\n"
             "int count = (int)(sizeof(arr) / sizeof(arr[0]));\n"
             "int big[(unsigned char)258 + 1];\n"
             "namespace util { template <typename T> T pick(T a, T b) { return a < b ? b : a; } }\n"
             "int entry(void) { return count + (unsigned char)300 + (char)-1 + (int)sizeof(big) / 4 + (bool)7 + util::pick(3, 9); }\n",
             5 + 44 - 1 + 3 + 1 + 9);
    return tally.finish(1);
}
