// Global objects, arrays of objects and static local objects with constructors and destructors.
// The constructors of the globals run first thing in main (or entry), in declaration order; a
// static local is built the first time its declaration is reached; the destructors run when main
// returns, static locals first, then the globals, latest first. The last destructor calls
// ExitProcess with the trace, so the exit code shows the whole order. Each program is also built
// by MinGW g++ and run under Wine; the exit codes must match.
// Written from ISO/IEC 14882 [basic.start.init], [basic.start.term], [stmt.dcl] and [class.ctor].

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
    std::printf("globals program %s\n", name.c_str());
    okayms::CompileInput in;
    in.source = source;
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    if (object_out != nullptr) *object_out = compiled.value.object;
    okayms::LinkOptions options;
    options.imports.push_back({"kernel32.dll", "ExitProcess", 0});
    const auto linked = okayms::link_executable({compiled.value.object}, options);
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  %s\n", linked.error.c_str());
        return;
    }
    const std::string dir = "/tmp/okayms-globals";
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
             "#ifdef __MINGW32__\n"
             "#include <windows.h>\n"
             "#else\n"
             "#ifdef __GNUC__\n"
             "#include <stdlib.h>\n"
             "#define ExitProcess exit\n"
             "#else\n"
             "__declspec(dllimport) void ExitProcess(unsigned int code);\n"
             "#endif\n"
             "#endif\n"
             "int trace;\n"
             "void t(int d) { trace = trace * 5 + d; }\n"
             "struct A {\n"
             "  int id;\n"
             "  A(int x) : id(x) { t(1); }\n"
             "  A() : id(0) { t(2); }\n"
             "  ~A() {\n"
             "    t(3);\n"
             "    if (id == 1) ExitProcess(trace % 256);\n"
             "  }\n"
             "};\n"
             "A first(1);\n"
             "A pair[2];\n"
             "int counter(void) {\n"
             "  static A s;\n"
             "  return 1;\n"
             "}\n"
             "int entry(void) {\n"
             "  A loc[2];\n"
             "  counter();\n"
             "  counter();\n"
             "  return trace % 256;\n"
             "}\n",
             141);
}

int main() {
    Tally tally;
    test_order(tally);
    return tally.finish(1);
}
