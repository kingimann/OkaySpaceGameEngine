// Exceptions (throw, try, catch, rethrow, unwinding with destructors) with a frame-list runtime
// (runtime/eh.c), written from ISO/IEC 14882 [except]. It is not the MSVC exception model (no
// .pdata handler, no ThrowInfo): code compiled here and code compiled by MinGW g++ cannot throw to
// each other. Each program is also built by MinGW g++ (with exceptions) and run under Wine; the
// exit codes must match.

#include "check.hpp"
#include "okayms/coff.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
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

std::string slurp(const std::string& path) {
    std::ifstream in(path.c_str());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool compile_runtime(const std::string& root, const char* name, okayms::ObjectFile* out) {
    okayms::CompileInput in;
    const std::string rt = root + "/tools/okayms/runtime";
    in.source = slurp(rt + "/" + name);
    in.name = name;
    in.include_paths.push_back(rt);
    const auto compiled = okayms::compile_c(in);
    if (!compiled.ok()) {
        std::fprintf(stderr, "  runtime %s: %s\n", name, compiled.error.c_str());
        return false;
    }
    *out = compiled.value.object;
    return true;
}

// Builds `source` with the runtime and runs it under Wine; when MinGW g++ exists the same source
// (plus a main that calls entry) must give the same exit code.
void run_both(Tally& tally, const std::string& root, const std::string& name, const std::string& source, int want) {
    std::printf("exception program %s\n", name.c_str());
    okayms::CompileInput in;
    in.source = source;
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    okayms::ObjectFile eh;
    okayms::ObjectFile mem;
    CHECK(tally, compile_runtime(root, "eh.c", &eh));
    CHECK(tally, compile_runtime(root, "mem.c", &mem));
    okayms::LinkOptions options;
    for (const char* fn : {"ExitProcess", "GetStdHandle", "WriteFile", "GetProcessHeap", "HeapAlloc", "HeapFree"})
        options.imports.push_back({"kernel32.dll", fn, 0});
    const auto linked = okayms::link_executable({compiled.value.object, eh, mem}, options);
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  %s\n", linked.error.c_str());
        return;
    }
    const std::string dir = "/tmp/okayms-exc";
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
    const int built = run_shell("x86_64-w64-mingw32-g++ -static -o " + gpp_exe + " " + src + " 2>" + dir + "/" + name + ".log");
    CHECK(tally, built == 0);
    if (built != 0) return;
    const int ref = wine_exit(gpp_exe);
    CHECK(tally, ref == code);
    if (ref != code) std::fprintf(stderr, "  g++ exit %d, okay exit %d\n", ref, code);
}

}  // namespace

static void test_basic(Tally& tally, const std::string& root) {
    std::printf("test_basic\n");
    run_both(tally, root, "basic",
             "int trace;\n"
             "void t(int d) { trace = trace * 5 + d; }\n"
             "struct Err {\n"
             "  int code;\n"
             "  Err(int c) : code(c) { }\n"
             "};\n"
             "struct D : Err {\n"
             "  int extra;\n"
             "  D(int c, int e) : Err(c), extra(e) { }\n"
             "};\n"
             "struct G {\n"
             "  int id;\n"
             "  G(int n) : id(n) { t(1); }\n"
             "  ~G() { t(2); }\n"
             "};\n"
             "struct H {\n"
             "  G a;\n"
             "  G b;\n"
             "  H(int fail) : a(1), b(2) {\n"
             "    t(3);\n"
             "    if (fail) throw 77;\n"
             "  }\n"
             "  ~H() { t(4); }\n"
             "};\n"
             "int level3(int x) {\n"
             "  G g(x);\n"
             "  if (x == 1) throw 11;\n"
             "  if (x == 2) {\n"
             "    Err e(22);\n"
             "    throw e;\n"
             "  }\n"
             "  if (x == 3) {\n"
             "    D d(33, 3);\n"
             "    throw d;\n"
             "  }\n"
             "  return x;\n"
             "}\n"
             "int level2(int x) {\n"
             "  G g(10);\n"
             "  return level3(x) + 100;\n"
             "}\n"
             "int run(int x) {\n"
             "  try {\n"
             "    G local(5);\n"
             "    return level2(x);\n"
             "  } catch (int v) {\n"
             "    t(3);\n"
             "    return -1;\n"
             "  } catch (Err &e) {\n"
             "    t(4);\n"
             "    return -e.code;\n"
             "  }\n"
             "}\n"
             "int entry(void) {\n"
             "  int r = 0;\n"
             "  int i;\n"
             "  for (i = 0; i <= 3; i = i + 1) r = r + run(i);\n"
             "  try {\n"
             "    try {\n"
             "      D d(5, 6);\n"
             "      throw d;\n"
             "    } catch (D &dd) {\n"
             "      t(1);\n"
             "      throw;\n"
             "    }\n"
             "  } catch (Err e) {\n"
             "    r = r + e.code;\n"
             "  }\n"
             "  try {\n"
             "    try {\n"
             "      throw 7;\n"
             "    } catch (Err &e) {\n"
             "      r = r + 1000;\n"
             "    }\n"
             "  } catch (...) {\n"
             "    r = r + 1;\n"
             "  }\n"
             "  try {\n"
             "    H h(1);\n"
             "    r = r + 5000;\n"
             "  } catch (int c) {\n"
             "    r = r + c;\n"
             "  }\n"
             "  try {\n"
             "    H h2(0);\n"
             "    r = r + 2;\n"
             "  } catch (int c) {\n"
             "    r = r + 5000;\n"
             "  }\n"
             "  int k;\n"
             "  for (k = 0; k < 3; k = k + 1) {\n"
             "    try {\n"
             "      if (k == 1) throw k;\n"
             "      r = r + 10;\n"
             "    } catch (int v) {\n"
             "      r = r + v;\n"
             "    }\n"
             "  }\n"
             "  return (r + trace) % 256;\n"
             "}\n",
             184);
}

static void test_unwind(Tally& tally, const std::string& root) {
    std::printf("test_unwind\n");
    run_both(tally, root, "unwind",
             "int trace;\n"
             "void t(int d) { trace = trace * 5 + d; }\n"
             "struct Guard {\n"
             "  int id;\n"
             "  Guard(int n) : id(n) { t(1); }\n"
             "  ~Guard() { t(2); }\n"
             "};\n"
             "struct Cell {\n"
             "  int v;\n"
             "  Cell() : v(7) { t(3); }\n"
             "  ~Cell() { t(4); }\n"
             "};\n"
             "struct Info { int a; };\n"
             "int viaReturn(int x) {\n"
             "  Guard g(1);\n"
             "  try {\n"
             "    Guard inner(2);\n"
             "    if (x > 0) return x * 2;\n"
             "    throw x;\n"
             "  } catch (int v) {\n"
             "    t(1);\n"
             "    return -v;\n"
             "  }\n"
             "}\n"
             "int loop(void) {\n"
             "  int sum = 0;\n"
             "  int i;\n"
             "  for (i = 0; i < 6; i = i + 1) {\n"
             "    try {\n"
             "      Guard g(10 + i);\n"
             "      if (i == 1) continue;\n"
             "      if (i == 4) break;\n"
             "      if (i == 2) throw i;\n"
             "      sum = sum + i;\n"
             "    } catch (int v) {\n"
             "      sum = sum + 100;\n"
             "    }\n"
             "  }\n"
             "  return sum;\n"
             "}\n"
             "void inner(void) {\n"
             "  Guard g(20);\n"
             "  throw 5;\n"
             "}\n"
             "void middle(void) {\n"
             "  try {\n"
             "    inner();\n"
             "  } catch (...) {\n"
             "    t(3);\n"
             "    throw;\n"
             "  }\n"
             "}\n"
             "void arrays(void) {\n"
             "  Cell cells[2];\n"
             "  throw 9;\n"
             "}\n"
             "int entry(void) {\n"
             "  int r = viaReturn(3) + viaReturn(0);\n"
             "  r = r + loop();\n"
             "  try {\n"
             "    middle();\n"
             "  } catch (int v) {\n"
             "    r = r + v;\n"
             "  }\n"
             "  Info stat;\n"
             "  stat.a = 9;\n"
             "  try {\n"
             "    throw &stat;\n"
             "  } catch (Info *p) {\n"
             "    r = r + p->a;\n"
             "  }\n"
             "  try {\n"
             "    try {\n"
             "      throw 1;\n"
             "    } catch (int v) {\n"
             "      throw v + 10;\n"
             "    }\n"
             "  } catch (int v) {\n"
             "    r = r + v;\n"
             "  }\n"
             "  try {\n"
             "    arrays();\n"
             "  } catch (int v) {\n"
             "    r = r + v;\n"
             "  }\n"
             "  return (r + trace) % 256;\n"
             "}\n",
             91);
}

// An exception nobody catches ends the program with the abort exit code (3).
static void test_uncaught(Tally& tally, const std::string& root) {
    std::printf("test_uncaught\n");
    run_both(tally, root, "uncaught",
             "void f(void) { throw 1; }\n"
             "int entry(void) {\n"
             "  f();\n"
             "  return 0;\n"
             "}\n",
             3);
}

int main(int argc, char** argv) {
    Tally tally;
    const std::string root = argc > 1 ? argv[1] : ".";
    test_basic(tally, root);
    test_unwind(tally, root);
    test_uncaught(tally, root);
    return tally.finish(3);
}
