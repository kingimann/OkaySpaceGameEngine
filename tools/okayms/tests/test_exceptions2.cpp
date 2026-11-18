// Exceptions, round 16: a thrown class object is copy-constructed by its copy constructor (one copy,
// like C++17: a temporary C(args) is not copied again), C(args) builds a temporary, and throw is an
// expression (c ? 1 : throw E()). Each program is also built by MinGW g++ and run under Wine; the
// exit codes must match. Written from ISO/IEC 14882 [except.throw], [expr.throw], [expr.type.conv].

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
    std::printf("exception2 program %s\n", name.c_str());
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
    const std::string dir = "/tmp/okayms-exc2";
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

static void test_copy_thrown(Tally& tally, const std::string& root) {
    std::printf("test_copy_thrown\n");
    run_both(tally, root, "copythrown",
             "int copies;\n"
             "int dtors;\n"
             "struct E {\n"
             "  int code;\n"
             "  E(int c) : code(c) { }\n"
             "  E(const E &o) : code(o.code + 10) { copies = copies + 1; }\n"
             "  ~E() { dtors = dtors + 1; }\n"
             "};\n"
             "int f(int x) {\n"
             "  E e(x);\n"
             "  if (x > 0) throw e;\n"
             "  return 0;\n"
             "}\n"
             "int entry(void) {\n"
             "  int r = 0;\n"
             "  try { f(1); } catch (E &e) { r = e.code; }\n"
             "  return r * 10 + copies * 3 + dtors;\n"
             "}\n",
             115);
}

static void test_throw_temp(Tally& tally, const std::string& root) {
    std::printf("test_throw_temp\n");
    run_both(tally, root, "thrownemp",
             "struct E { int code; E(int c) : code(c) { } };\n"
             "int f(int x) { if (x) throw E(x + 4); return 0; }\n"
             "int entry(void){ int r = 0; try { f(2); } catch (E &e) { r = e.code; } return r; }\n",
             6);
}

static void test_throw_expr(Tally& tally, const std::string& root) {
    std::printf("test_throw_expr\n");
    run_both(tally, root, "throwexpr",
             "int copies;\n"
             "struct E {\n"
             "  int code;\n"
             "  E(int c) : code(c) { }\n"
             "  E(const E &o) : code(o.code + 100) { copies = copies + 1; }\n"
             "};\n"
             "int pick(int x) { return x > 5 ? x : throw x * 2; }\n"
             "int pick2(int *p) { return p ? *p : throw 7; }\n"
             "int viaE(int x) {\n"
             "  E e(x);\n"
             "  return x > 0 ? 1 : throw e;\n"
             "}\n"
             "int viaE2(int x) { return x > 0 ? 1 : throw E(x + 40); }\n"
             "int entry(void) {\n"
             "  int r = 0;\n"
             "  try { r = r + pick(9); } catch (int v) { r = r + 1000; }\n"
             "  try { r = r + pick(3); } catch (int v) { r = r + v; }\n"
             "  int zero = 0;\n"
             "  try { r = r + pick2(&zero); r = r + pick2(0); } catch (int v) { r = r + v * 10; }\n"
             "  try { viaE(-1); } catch (E &e) { r = r + e.code; }\n"
             "  try { viaE2(-1); } catch (E &e) { r = r + e.code; }\n"
             "  try { try { throw 5; } catch (int v) { int q = v > 100 ? 1 : throw; } } catch (int v) { r = r + v; }\n"
             "  return r + copies * 3;\n"
             "}\n",
             231);
}

static void test_throw_prvalue_copy(Tally& tally, const std::string& root) {
    std::printf("test_throw_prvalue_copy\n");
    run_both(tally, root, "prvcopy",
             "int copies;\n"
             "struct E {\n"
             "  int code;\n"
             "  E(int c) : code(c) { }\n"
             "  E(const E &o) : code(o.code + 100) { copies = copies + 1; }\n"
             "};\n"
             "int viaE2(int x) { return x > 0 ? 1 : throw E(x + 40); }\n"
             "int entry(void) {\n"
             "  int r = 0;\n"
             "  try { viaE2(-1); } catch (E &e) { r = r + e.code; }\n"
             "  return r + copies * 3;\n"
             "}\n",
             39);
}

static void test_throw_expr_named(Tally& tally, const std::string& root) {
    std::printf("test_throw_expr_named\n");
    run_both(tally, root, "exprnamed",
             "int copies;\n"
             "struct E {\n"
             "  int code;\n"
             "  E(int c) : code(c) { }\n"
             "  E(const E &o) : code(o.code + 100) { copies = copies + 1; }\n"
             "};\n"
             "int viaE(int x) {\n"
             "  E e(x);\n"
             "  return x > 0 ? 1 : throw e;\n"
             "}\n"
             "int entry(void) {\n"
             "  int r = 0;\n"
             "  try { viaE(-1); } catch (E &e) { r = r + e.code; }\n"
             "  return r + copies * 3;\n"
             "}\n",
             102);
}

int main(int argc, char** argv) {
    Tally tally;
    const std::string root = argc > 1 ? argv[1] : ".";
    test_copy_thrown(tally, root);
    test_throw_temp(tally, root);
    test_throw_expr(tally, root);
    test_throw_prvalue_copy(tally, root);
    test_throw_expr_named(tally, root);
    return tally.finish(1);
}
