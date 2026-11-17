// Tests for okay-build using tiny synthetic projects under a temp dir. usage: test_okaybuild PATH_TO_okay-build
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <sys/wait.h>
namespace fs = std::filesystem;
using std::string;
static string gTool; static int gFail = 0, gPass = 0; static fs::path gRoot;
static void write(const string& rel, const string& txt) { fs::create_directories((gRoot / rel).parent_path()); std::ofstream(gRoot / rel, std::ios::trunc) << txt; }
static string sh(const string& args = "", int* rc = nullptr) {
    string cmd = "cd '" + gRoot.string() + "' && " + gTool + " " + args + " 2>&1"; string out; char b[4096];
    FILE* p = popen(cmd.c_str(), "r"); size_t n; while ((n = fread(b, 1, sizeof b, p)) > 0) out.append(b, n);
    int st = pclose(p); if (rc) *rc = WEXITSTATUS(st); return out;
}
static int ran(const string& out) { auto p = out.find("ran "); return p == string::npos ? -1 : atoi(out.c_str() + p + 4); }
static void check(bool c, const string& what, const string& ctx = "") { if (c) ++gPass; else { ++gFail; printf("  FAIL: %s\n%s\n", what.c_str(), ctx.c_str()); } }
static void fresh(const string& name) { gRoot = fs::temp_directory_path() / ("okaybuild_test_" + name); fs::remove_all(gRoot); fs::create_directories(gRoot); }
static void sleepMs() { std::this_thread::sleep_for(std::chrono::milliseconds(20)); }

static void basicProject() {
    write("inc/util.h", "#pragma once\nint util();\n");
    write("src/util.cpp", "#include \"util.h\"\nint util(){return 7;}\n");
    write("src/main.cpp", "#include \"util.h\"\n#include <cstdio>\nint main(){printf(\"%d\\n\",util());return util()==7?0:1;}\n");
    write("build.okay", "target util lib\n sources = src/util.cpp\n export_include = inc\nend\ntarget app exe\n sources = src/main.cpp\n deps = util\nend\n");
}

static void testBasicAndNoop() {
    printf("test: build, run, no-op rebuild\n"); fresh("basic"); basicProject();
    int rc; string o = sh("-j2", &rc); check(rc == 0 && ran(o) == 4, "first build runs 4 steps", o);
    check(system(("'" + (gRoot / "build/app").string() + "' >/dev/null").c_str()) == 0, "built exe runs and returns 0");
    o = sh("", &rc); check(rc == 0 && ran(o) == 0, "no-op rebuild runs 0 steps", o);
    o = sh("--dry-run", &rc); check(ran(o) == 0, "dry run on clean tree runs 0", o);
}
static void testHeaderTouch() {
    printf("test: touching a header rebuilds only dependents; content hash ignores touch\n"); fresh("hdr"); basicProject();
    sh("");
    sleepMs(); fs::last_write_time(gRoot / "inc/util.h", fs::file_time_type::clock::now());
    string o = sh(); check(ran(o) == 0, "mtime-only touch rebuilds nothing", o);
    write("inc/util.h", "#pragma once\nint util();\n// changed\n");
    o = sh("--explain"); check(ran(o) == 3 || ran(o) == 2, "header edit rebuilds the two compiles (+ maybe link)", o);
    check(o.find("util.h changed") != string::npos, "explain names the header", o);
    o = sh(); check(ran(o) == 0, "then no-op again", o);
}
static void testDefine() {
    printf("test: changed define rebuilds that target only\n"); fresh("def"); basicProject(); sh("");
    write("build.okay", "target util lib\n sources = src/util.cpp\n export_include = inc\n define = FOO=1\nend\ntarget app exe\n sources = src/main.cpp\n deps = util\nend\n");
    string o = sh("--explain"); check(o.find("command line changed") != string::npos, "explain says command line changed", o);
    check(o.find("CC src/util.cpp") != string::npos && o.find("CC src/main.cpp") == string::npos, "only util.cpp recompiled", o);
}
static void testMissingHeader() {
    printf("test: missing header gives a clear error\n"); fresh("miss"); basicProject(); write("src/main.cpp", "#include \"nope.h\"\nint main(){}\n");
    int rc; string o = sh("-j1", &rc); check(rc == 1, "exit code 1", o);
    check(o.find("nope.h") != string::npos && o.find("FAILED") != string::npos, "error names header and FAILED", o);
    write("src/main.cpp", "int main(){}\n"); write("build.okay", "target app exe\n sources = src/main.cpp src/ghost.cpp\nend\n");
    o = sh("", &rc); check(rc == 2 && o.find("src/ghost.cpp") != string::npos && o.find("does not exist") != string::npos, "missing source reported at plan time", o);
}
static void testCycle() {
    printf("test: cyclic dependency detected\n"); fresh("cyc"); write("a.cpp", "int a(){return 1;}\n");
    write("build.okay", "target a lib\n sources = a.cpp\n deps = b\nend\ntarget b lib\n sources = a.cpp\n deps = c\nend\ntarget c lib\n sources = a.cpp\n deps = a\nend\n");
    int rc; string o = sh("", &rc); check(rc == 2 && o.find("cycle") != string::npos && o.find("a -> b -> c -> a") != string::npos, "cycle path printed", o);
}
static void testLinkOrder() {
    printf("test: static library link order (a needs b needs c, deps listed backwards)\n"); fresh("order");
    write("c.cpp", "int c(){return 3;}\n"); write("b.cpp", "int c(); int b(){return c()+1;}\n"); write("a.cpp", "int b(); int a(){return b()+1;}\n");
    write("m.cpp", "int a(); int main(){return a()==5?0:1;}\n");
    write("build.okay", "target c lib\n sources = c.cpp\nend\ntarget b lib\n sources = b.cpp\n deps = c\nend\ntarget a lib\n sources = a.cpp\n deps = b\nend\ntarget m exe\n sources = m.cpp\n deps = c b a\nend\n");
    int rc; string o = sh("", &rc); check(rc == 0, "links with correct order", o);
    check(system(("'" + (gRoot / "build/m").string() + "'").c_str()) == 0, "result runs");
}
static void testParallelDeterministic() {
    printf("test: 40-file project, -j1 and -j4 give identical output files\n"); fresh("par");
    string man = "target big exe\n sources ="; string mainBody = "int main(){int s=0;";
    string decls;
    for (int i = 0; i < 40; ++i) { write("s/f" + std::to_string(i) + ".cpp", "#include \"h.h\"\nint f" + std::to_string(i) + "(){return " + std::to_string(i) + "+K;}\n"); man += " s/f" + std::to_string(i) + ".cpp"; decls += "int f" + std::to_string(i) + "();"; mainBody += "s+=f" + std::to_string(i) + "();"; }
    write("h.h", "#define K 1\n"); write("s/main.cpp", decls + mainBody + "return s==820?0:1;}\n"); man += " s/main.cpp\n include = .\nend\n"; write("build.okay", man);
    int rc; string o = sh("-j1", &rc); check(rc == 0 && ran(o) == 42, "-j1 builds 42 steps", o);
    string h1; { std::ifstream f(gRoot / "build/big", std::ios::binary); h1.assign(std::istreambuf_iterator<char>(f), {}); }
    sh("clean"); o = sh("-j4", &rc); check(rc == 0 && ran(o) == 42, "-j4 builds 42 steps", o);
    string h2; { std::ifstream f(gRoot / "build/big", std::ios::binary); h2.assign(std::istreambuf_iterator<char>(f), {}); }
    check(h1 == h2 && !h1.empty(), "binaries are byte-identical");
    check(system(("'" + (gRoot / "build/big").string() + "'").c_str()) == 0, "binary runs correctly");
}
static void testCompdbCleanDry() {
    printf("test: compdb, clean, dry run\n"); fresh("misc"); basicProject();
    string o = sh("--dry-run"); check(ran(o) == 4 && !fs::exists(gRoot / "build"), "dry run lists 4 steps, builds nothing", o);
    sh("compdb"); std::ifstream f(gRoot / "compile_commands.json"); string j((std::istreambuf_iterator<char>(f)), {});
    check(j.find("\"file\": \"src/util.cpp\"") != string::npos && j.find("\"arguments\"") != string::npos && j.find("-Iinc") != string::npos, "compile_commands.json has entries incl. exported include", j);
    sh(""); sh("clean"); check(!fs::exists(gRoot / "build/app") && !fs::exists(gRoot / ".okaybuild"), "clean removes outputs and state");
}
static void testManifestErrors() {
    printf("test: manifest errors carry line numbers\n"); fresh("err"); write("a.cpp", "int main(){}\n");
    write("build.okay", "target a exe\n sources = a.cpp\n bogus = 1\nend\n"); int rc; string o = sh("", &rc);
    check(rc == 2 && o.find("build.okay:3: unknown target key 'bogus'") != string::npos, "unknown key with line", o);
    write("build.okay", "target a exe\n sources = a.cpp\n deps = zzz\nend\n"); o = sh("", &rc);
    check(rc == 2 && o.find("unknown target 'zzz'") != string::npos, "unknown dep", o);
}
static void testMingwCompdb() {
    printf("test: mingw toolchain preset (plan only, compdb)\n"); fresh("mingw"); write("a.cpp", "int main(){}\n");
    write("build.okay", "toolchain mingw\n flags = -std=c++17\nend\ntarget a exe\n sources = a.cpp\nend\n");
    string o = sh("--dry-run"); check(o.find("x86_64-w64-mingw32-g++-win32") != string::npos && o.find("build/a.exe") != string::npos, "uses mingw compiler and .exe", o);
    if (system("which x86_64-w64-mingw32-g++-win32 >/dev/null 2>&1") == 0) { int rc; o = sh("", &rc); check(rc == 0 && fs::exists(gRoot / "build/a.exe"), "real mingw cross build produced a.exe", o); }
}

int main(int argc, char** argv) {
    if (argc < 2) { puts("usage: test_okaybuild PATH_TO_okay-build"); return 2; }
    gTool = "'" + fs::absolute(argv[1]).string() + "'";
    testBasicAndNoop(); testHeaderTouch(); testDefine(); testMissingHeader(); testCycle(); testLinkOrder();
    testParallelDeterministic(); testCompdbCleanDry(); testManifestErrors(); testMingwCompdb();
    printf("okaybuild tests: %d checks passed, %d failed\n", gPass, gFail); return gFail ? 1 : 0;
}
