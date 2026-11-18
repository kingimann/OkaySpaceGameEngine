// Tests for okay-build using tiny synthetic projects under a temp dir. usage: test_okaybuild PATH_TO_okay-build
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
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
static long long mtimeNs(const fs::path& p) { struct stat st{}; if (stat(p.c_str(), &st) != 0) return -1; return (long long)st.st_mtim.tv_sec * 1000000000ll + st.st_mtim.tv_nsec; }
static string readFile(const fs::path& p) { std::ifstream f(p); return string((std::istreambuf_iterator<char>(f)), {}); }

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
static void testMingwSlice() {
    printf("test: mingw toolchain links a library and an exe\n");
    if (system("x86_64-w64-mingw32-g++-win32 -v >/dev/null 2>&1") != 0) { printf("  skipped (no mingw)\n"); return; }
    fresh("mwslice");
    write("add.cpp", "int add(int a, int b){return a+b;}\n");
    write("mul.cpp", "int mul(int a, int b){return a*b;}\n");
    write("main.cpp", "int add(int,int); int mul(int,int); int main(){return add(2,3)==5 && mul(2,3)==6 ? 0 : 1;}\n");
    write("build.okay",
          "toolchain mingw\n  flags = -std=c++17 -O0\nend\n"
          "target math lib\n  sources = add.cpp mul.cpp\nend\n"
          "target app exe\n  sources = main.cpp\n  deps = math\nend\n");
    int rc; string o = sh("-j2", &rc);
    check(rc == 0 && fs::exists(gRoot / "build/app.exe") && fs::exists(gRoot / "build/libmath.a"), "mingw lib and exe linked", o);
    FILE* f = popen(("file '" + (gRoot / "build/app.exe").string() + "'").c_str(), "r");
    char b[256] = {}; if (f) { size_t n = fread(b, 1, sizeof b - 1, f); pclose(f); b[n] = 0; }
    check(std::string(b).find("PE32+") != std::string::npos, "exe is a Windows PE32+ program", b);
}

static void testMingwCompdb() {
    printf("test: mingw toolchain preset (plan only, compdb)\n"); fresh("mingw"); write("a.cpp", "int main(){}\n");
    write("build.okay", "toolchain mingw\n flags = -std=c++17\nend\ntarget a exe\n sources = a.cpp\nend\n");
    string o = sh("--dry-run"); check(o.find("x86_64-w64-mingw32-g++-win32") != string::npos && o.find("build/a.exe") != string::npos, "uses mingw compiler and .exe", o);
    if (system("which x86_64-w64-mingw32-g++-win32 >/dev/null 2>&1") == 0) { int rc; o = sh("", &rc); check(rc == 0 && fs::exists(gRoot / "build/a.exe"), "real mingw cross build produced a.exe", o); }
}

static void testStatShortcut() {
    printf("test: no-op does not hash old inputs (stat shortcut); same-mtime edit with new size still caught\n"); fresh("stat"); basicProject();
    auto old = fs::file_time_type::clock::now() - std::chrono::hours(1);
    for (auto f : {"inc/util.h", "src/util.cpp", "src/main.cpp"}) fs::last_write_time(gRoot / f, old);
    sh(); string o = sh("-v"); check(o.find("trusted size+mtime for") != string::npos, "stats line present", o);
    int hashed = atoi(o.c_str() + o.find("hashed ") + 7); check(hashed <= 4, "no-op hashed at most the build outputs, sources trusted by stat", o);
    string o2 = sh("--no-stat -v"); check(atoi(o2.c_str() + o2.find("hashed ") + 7) > hashed, "--no-stat hashes more", o2);
    write("inc/util.h", "#pragma once\nint util();\nint other();\n"); fs::last_write_time(gRoot / "inc/util.h", old);
    o = sh("--explain"); check(ran(o) >= 2 && o.find("util.h changed") != string::npos, "edit with restored mtime but new size is caught", o);
}
static void testFreshNoopTrust() {
    printf("test: no-op immediately after a build does not re-read inputs with nanosecond mtimes\n");
    fresh("fresh"); basicProject();
    int rc; string o = sh("-j2", &rc); check(rc == 0, "build", o);
    o = sh("-v");
    check(ran(o) == 0, "immediate no-op", o);
    int hashed = atoi(o.c_str() + o.find("hashed ") + 7);
    struct stat st{}; stat((gRoot / "src/util.cpp").c_str(), &st);
    if (st.st_mtim.tv_nsec != 0) check(hashed == 0, "fresh inputs trusted by size and mtime", o);
    else check(hashed >= 0, "coarse timestamps still finish the no-op", o);
}

static void testSpacesBuildDirRsp() {
    printf("test: paths with spaces, --build-dir, response file for long link lines\n"); fresh("sp");
    write("my src/a b.cpp", "int ab(){return 2;}\n"); write("my src/main.cpp", "int ab(); int main(){return ab()==2?0:1;}\n");
    write("build.okay", "toolchain\n rsp_threshold = 40\nend\ntarget lib1 lib\n sources = \"my src/a b.cpp\"\nend\ntarget app exe\n sources = \"my src/main.cpp\"\n deps = lib1\nend\n");
    string bd = "--build-dir '" + (gRoot / "out dir").string() + "'";
    int rc; string o = sh(bd, &rc); check(rc == 0 && ran(o) == 4, "builds with spaces", o);
    check(fs::exists(gRoot / "out dir/build/app") && !fs::exists(gRoot / ".okaybuild"), "outputs and state live in the build dir");
    check(fs::exists(gRoot / "out dir/.okaybuild/state") && fs::exists(gRoot / "out dir/.okaybuild/rsp"), "state and response files in build dir");
    check(system(("'" + (gRoot / "out dir/build/app").string() + "'").c_str()) == 0, "program linked through response file runs");
    o = sh(bd); check(ran(o) == 0, "no-op in build dir", o);
}
static void testRawActions() {
    printf("test: raw action blocks (importer form): edges, depfile, dir, selection\n"); fresh("raw");
    write("src/x.cpp", "#include \"x.h\"\nint x(){return X;}\n"); write("src/x.h", "#define X 3\n"); write("src/m.cpp", "int x(); int main(){return x()==3?0:1;}\n");
    string rd = (gRoot / "run").string(), sx = (gRoot / "src/x.cpp").string(), sm = (gRoot / "src/m.cpp").string(); fs::create_directories(gRoot / "run");
    write("build.okay", "action\n dir = \"" + rd + "\"\n cmd = g++ -MD -MF x.o.d -c " + sx + " -o x.o\n output = x.o\n depfile = x.o.d\n inputs = " + sx + "\nend\n"
          "action\n dir = \"" + rd + "\"\n cmd = g++ -c " + sm + " -o m.o\n output = m.o\n inputs = " + sm + "\nend\n"
          "action\n dir = \"" + rd + "\"\n cmd = g++ m.o x.o -o prog\n output = prog\n inputs = m.o x.o\nend\n");
    int rc; string o = sh("-j2 prog", &rc); check(rc == 0 && ran(o) == 3, "3 raw steps with inferred link edge", o);
    check(system(("'" + rd + "/prog'").c_str()) == 0, "prog runs");
    check(ran(sh()) == 0, "no-op");
    write("src/x.h", "#define X 4\n"); o = sh("--explain"); check(ran(o) == 2 && o.find("x.h changed") != string::npos, "depfile header change rebuilds x.o and relinks", o);
    o = sh("x.o --dry-run"); check(ran(o) == 0, "selecting one output works", o);
}

// Bug: the manifest reader cut every line at the first '#', so a command that prints a
// preprocessor line, and a quoted path that contains '#', were both truncated.
static void testHashInCommandAndPath() {
    printf("test: '#' inside a command and inside a quoted path is kept\n");
    fresh("hashc");
    write("a#b.cpp", "int main(){return 0;}\n");
    write("build.okay",
          "action\n"
          "  cmd = sh -c \"printf '#define V 3\\n' > gen.h\"\n"
          "  output = gen.h\n"
          "end\n"
          "target app exe\n"
          "  sources = \"a#b.cpp\"\n"
          "  include = .\n"
          "end\n");
    int rc; string o = sh("-j2", &rc);
    check(rc == 0, "build with hash in cmd and path", o);
    string h; { std::ifstream f(gRoot / "gen.h"); h.assign(std::istreambuf_iterator<char>(f), {}); }
    check(h.find("#define V 3") != string::npos, "command kept the hash character", h + o);
    check(fs::exists(gRoot / "build/app"), "quoted source path with a hash compiled");
}

static void testImportGenerated() {
    printf("test: importer keeps order-only edges for generated files (real CMake+Ninja configure of a tiny project)\n");
    if (system("cmake --version >/dev/null 2>&1 && ninja --version >/dev/null 2>&1") != 0) { printf("  skipped (no cmake/ninja)\n"); return; }
    fresh("gen");
    write("CMakeLists.txt", "cmake_minimum_required(VERSION 3.16)\nproject(g CXX)\n"
          "add_custom_command(OUTPUT gen.h COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/gen.h.in gen.h DEPENDS ${CMAKE_SOURCE_DIR}/gen.h.in)\n"
          "add_library(lib1 STATIC a.cpp gen.h)\ntarget_include_directories(lib1 PUBLIC ${CMAKE_CURRENT_BINARY_DIR})\n"
          "add_executable(app main.cpp)\ntarget_link_libraries(app lib1)\n");
    write("gen.h.in", "#define GEN 5\n"); write("a.cpp", "#include \"gen.h\"\nint a(){return GEN;}\n"); write("main.cpp", "#include \"gen.h\"\nint a(); int main(){return a()==GEN?0:1;}\n");
    string cfg = "cd '" + gRoot.string() + "' && cmake -S . -B nb -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null 2>&1";
    check(system(cfg.c_str()) == 0, "tiny CMake project configures");
    string o = sh("import nb --run-dir run -o imp.okay", nullptr); string man; { std::ifstream f(gRoot / "imp.okay"); man.assign(std::istreambuf_iterator<char>(f), {}); }
    check(o.find("custom") != string::npos && o.find("1 custom output(s) found") != string::npos, "custom command and its output were identified", o + man);
    check(man.find("after =") != string::npos && man.find("gen.h") != string::npos, "compiles wait for gen.h (order-only edge kept)", man);
    int rc; o = sh("-f imp.okay -j4", &rc); check(rc == 0, "imported build succeeds (generated header exists before compile)", o);
    check(system(("'" + gRoot.string() + "/run/app'").c_str()) == 0, "built program runs");
    check(ran(sh("-f imp.okay")) == 0, "no-op");
    write("gen.h.in", "#define GEN 9\n"); o = sh("-f imp.okay --explain"); check(o.find("CC ") == string::npos && ran(o) >= 3, "changing the generator input regenerates and rebuilds dependents", o);
    check(system(("'" + gRoot.string() + "/run/app'").c_str()) == 0, "program still correct with GEN=9");
}

static void testPools() {
    printf("test: a pool limits how many commands overlap\n");
    fresh("pool");
    write("tick.sh",
          "#!/bin/sh\n"
          "dir=$1; out=$2; limit=$3\n"
          "exec 9>>\"$dir/lock\"\n"
          "flock 9\n"
          "n=$(cat \"$dir/cur\" 2>/dev/null || echo 0)\n"
          "n=$((n+1)); echo \"$n\" > \"$dir/cur\"\n"
          "m=$(cat \"$dir/max\" 2>/dev/null || echo 0)\n"
          "if [ \"$n\" -gt \"$m\" ]; then echo \"$n\" > \"$dir/max\"; fi\n"
          "if [ \"$n\" -gt \"$limit\" ]; then echo OVERLAP >> \"$dir/bad\"; fi\n"
          "flock -u 9\n"
          "sleep 0.4\n"
          "flock 9\n"
          "n=$(cat \"$dir/cur\"); echo $((n-1)) > \"$dir/cur\"\n"
          "flock -u 9\n"
          "echo ok > \"$out\"\n");
    string man = "pool heavy\n  depth = 1\nend\n";
    for (int i = 0; i < 3; ++i) man += "action\n  cmd = sh tick.sh . o" + std::to_string(i) + " 1\n  output = o" + std::to_string(i) + "\n  pool = heavy\nend\n";
    write("build.okay", man);
    int rc; string o = sh("-j4", &rc);
    check(rc == 0, "depth 1 build succeeds", o);
    check(!fs::exists(gRoot / "bad"), "depth 1 never runs two commands at once", readFile(gRoot / "bad") + o);
    check(fs::exists(gRoot / "o0") && fs::exists(gRoot / "o1") && fs::exists(gRoot / "o2"), "all three outputs exist");

    fresh("pool2");
    write("tick.sh",
          "#!/bin/sh\n"
          "dir=$1; out=$2; limit=$3\n"
          "exec 9>>\"$dir/lock\"\n"
          "flock 9\n"
          "n=$(cat \"$dir/cur\" 2>/dev/null || echo 0)\n"
          "n=$((n+1)); echo \"$n\" > \"$dir/cur\"\n"
          "m=$(cat \"$dir/max\" 2>/dev/null || echo 0)\n"
          "if [ \"$n\" -gt \"$m\" ]; then echo \"$n\" > \"$dir/max\"; fi\n"
          "if [ \"$n\" -gt \"$limit\" ]; then echo OVERLAP >> \"$dir/bad\"; fi\n"
          "flock -u 9\n"
          "sleep 0.4\n"
          "flock 9\n"
          "n=$(cat \"$dir/cur\"); echo $((n-1)) > \"$dir/cur\"\n"
          "flock -u 9\n"
          "echo ok > \"$out\"\n");
    man = "pool heavy\n  depth = 2\nend\n";
    for (int i = 0; i < 4; ++i) man += "action\n  cmd = sh tick.sh . o" + std::to_string(i) + " 2\n  output = o" + std::to_string(i) + "\n  pool = heavy\nend\n";
    write("build.okay", man);
    o = sh("-j4", &rc);
    check(rc == 0 && !fs::exists(gRoot / "bad"), "depth 2 stays within 2", readFile(gRoot / "bad") + o);
    int mx = atoi(readFile(gRoot / "max").c_str());
    check(mx == 2, "depth 2 actually runs two commands together", "max=" + std::to_string(mx));
}

static void testGeneratorRestat() {
    printf("test: generator with declared outputs, and restat leaves dependents alone\n");
    fresh("genr");
    write("spec.txt", "base\n");
    write("gen.sh", "#!/bin/sh\nprintf 'int gen(){return 5;}\\n' > gen.cpp\nprintf '#pragma once\\n#define G 5\\n' > gen.h\n");
    write("main.cpp", "#include \"gen.h\"\nint gen(); int main(){return gen()==G?0:1;}\n");
    write("build.okay",
          "action\n"
          "  cmd = sh gen.sh\n"
          "  outputs = gen.cpp gen.h\n"
          "  inputs = spec.txt\n"
          "  restat = 1\n"
          "end\n"
          "target app exe\n"
          "  sources = gen.cpp main.cpp\n"
          "  needs = gen.h\n"
          "  include = .\n"
          "end\n");
    int rc; string o = sh("-j2", &rc);
    check(rc == 0 && ran(o) == 4, "generator, two compiles and the link", o);
    check(system(("'" + (gRoot / "build/app").string() + "'").c_str()) == 0, "generated program runs");
    long long mt = mtimeNs(gRoot / "gen.h");
    write("spec.txt", "base\n# ignored by the generator\n");
    o = sh("--explain", &rc);
    check(rc == 0 && ran(o) == 1, "only the generator reruns when its output bytes do not change", o);
    check(mtimeNs(gRoot / "gen.h") == mt && mt > 0, "restat put the output mtime back", "before " + std::to_string(mt) + " after " + std::to_string(mtimeNs(gRoot / "gen.h")));
    check(system(("'" + (gRoot / "build/app").string() + "'").c_str()) == 0, "program still runs");
}

static void testKeepGoing() {
    printf("test: -k keeps building steps that do not depend on the failure\n");
    fresh("keep");
    write("build.okay",
          "action\n  cmd = echo ok > good.txt\n  output = good.txt\nend\n"
          "action\n  cmd = echo no > bad.txt; exit 1\n  output = bad.txt\nend\n"
          "action\n  cmd = echo dep > dep.txt\n  output = dep.txt\n  inputs = bad.txt\nend\n");
    int rc; string o = sh("-j1", &rc);
    check(rc == 1, "a failed step exits 1", o);
    check(!fs::exists(gRoot / "good.txt"), "without -k an independent step does not run", o);
    check(!fs::exists(gRoot / "dep.txt"), "a step that needs the failed output does not run", o);
    o = sh("-j1 -k", &rc);
    check(rc == 1, "-k still exits 1", o);
    check(fs::exists(gRoot / "good.txt"), "-k runs the independent step", o);
    check(!fs::exists(gRoot / "dep.txt"), "-k still skips the dependent of the failure", o);
}

static int countLines(const fs::path& p) { std::ifstream f(p); int n = 0; string s; while (std::getline(f, s)) ++n; return n; }
static int progPrint(const fs::path& p) {
    string cmd = "'" + p.string() + "'"; FILE* f = popen(cmd.c_str(), "r"); if (!f) return -1;
    char b[64] = {}; size_t n = fread(b, 1, sizeof b - 1, f); pclose(f); b[n] = 0; return atoi(b);
}
static void testRegenManifest() {
    printf("test: regenerate the manifest when a declared input changes\n");
    fresh("regen");
    write("main.cpp", "#include <cstdio>\n#ifndef VAL\n#define VAL 0\n#endif\nint main(){printf(\"%d\\n\", VAL); return 0;}\n");
    write("flag.txt", "3\n"); write("extra.txt", "a\n");
    write("gen.sh",
          "#!/bin/sh\n"
          "v=$(tr -d '[:space:]' < flag.txt)\n"
          "echo R >> regen.log\n"
          "printf 'build.okay: flag.txt extra.txt\\n' > build.okay.d\n"
          "cat > build.okay << EOF\n"
          "buildfile\n"
          "  cmd = sh gen.sh\n"
          "  inputs = flag.txt\n"
          "  depfile = build.okay.d\n"
          "end\n"
          "target app exe\n"
          "  sources = main.cpp\n"
          "  flags = -DVAL=${v}\n"
          "end\n"
          "EOF\n");
    write("build.okay",
          "buildfile\n"
          "  cmd = sh gen.sh\n"
          "  inputs = flag.txt\n"
          "  depfile = build.okay.d\n"
          "end\n"
          "target app exe\n"
          "  sources = main.cpp\n"
          "  flags = -DVAL=3\n"
          "end\n");
    int rc; string o = sh("-j2", &rc);
    check(rc == 0 && progPrint(gRoot / "build/app") == 3, "first build runs the program with VAL 3", o);
    check(countLines(gRoot / "regen.log") == 1, "generator ran once", o);
    o = sh("", &rc);
    check(rc == 0 && ran(o) == 0 && countLines(gRoot / "regen.log") == 1, "unchanged inputs do not regenerate or rebuild", o);
    write("flag.txt", "9\n");
    o = sh("--explain", &rc);
    check(rc == 0 && progPrint(gRoot / "build/app") == 9, "changed flag regenerates and the program prints 9", o);
    check(countLines(gRoot / "regen.log") == 2, "generator ran because flag.txt changed");
    write("extra.txt", "b\n");
    o = sh("", &rc);
    check(rc == 0 && ran(o) == 0 && countLines(gRoot / "regen.log") == 3, "depfile input change regenerates without a rebuild", o);
}

static string capture(const string& cmd, int* rc = nullptr) {
    string out; char b[4096]; FILE* p = popen(cmd.c_str(), "r"); if (!p) { if (rc) *rc = 127; return "popen failed"; }
    size_t n; while ((n = fread(b, 1, sizeof b, p)) > 0) out.append(b, n);
    int st = pclose(p); if (rc) *rc = WEXITSTATUS(st); return out;
}
// Cross-compiling okay-build itself used to fail at sys/wait.h. The Windows host must compile with the
// win32 MinGW g++ and, under Wine, run cmd.exe commands (echo, a dependency edge, a failing step).
static void testConfigsAndToolchains() {
    printf("test: named configs, typed options, config.okay, toolchain probe\n");
    fresh("cfg");
    write("main.cpp",
          "#include <cstdio>\nint main(){\n"
          "#if defined(REL)\nputs(\"release\");\n#elif defined(DBG)\nputs(\"debug\");\n#else\nputs(\"none\");\n#endif\n"
          "#ifdef WITH_TESTS\nputs(\"tests\");\n#endif\nreturn 0;}\n");
    write("build.okay",
          "option tests bool = off\n"
          "config debug\n optimize = 0\n debug = on\n define = DBG\nend\n"
          "config release\n optimize = 2\n debug = off\n define = REL\nend\n"
          "config asan\n flags = -DOKAY_ASAN=1\nend\n"
          "if tests\n define = WITH_TESTS\nend\n"
          "target app exe\n sources = main.cpp\nend\n");
    int rc; string o = sh("--config release", &rc);
    check(rc == 0, "release config builds", o);
    o = readFile(gRoot / "build/app"); // not the program output
    int prc = 0; string prog = capture("'" + (gRoot / "build/app").string() + "'", &prc);
    check(prc == 0 && prog.find("release") != string::npos && prog.find("debug") == string::npos, "release config selects REL", prog);
    o = sh("--config debug --opt tests=on", &rc);
    check(rc == 0, "debug config with option builds", o);
    prog = capture("'" + (gRoot / "build/app").string() + "'", &prc);
    check(prog.find("debug") != string::npos && prog.find("tests") != string::npos, "debug config and bool option both apply", prog);
    write("config.okay", "config = release\noption tests = on\n");
    o = sh("", &rc);
    check(rc == 0, "config.okay builds", o);
    prog = capture("'" + (gRoot / "build/app").string() + "'", &prc);
    check(prog.find("release") != string::npos && prog.find("tests") != string::npos, "config.okay picks the config and the option", prog);
    o = sh("--config no-such", &rc);
    check(rc == 2 && o.find("no-such") != string::npos, "unknown config is an error", o);
    write("badopt.okay", "option level int = no\ntarget app exe\n sources = main.cpp\nend\n");
    o = sh("-f badopt.okay", &rc);
    check(rc == 2 && o.find("level") != string::npos, "int option rejects a non-number", o);
    fs::path bin = gRoot / "fakebin"; fs::create_directories(bin);
    for (const char* n : {"gcc", "g++", "clang", "clang++", "x86_64-w64-mingw32-g++-win32"}) {
        write(("fakebin/" + string(n)).c_str(), ""); fs::permissions(gRoot / "fakebin" / n, fs::perms::owner_all);
    }
    fs::create_directories(gRoot / "fakevs/VC/Tools/MSVC/14.40/bin/Hostx64/x64");
    write("fakevs/VC/Tools/MSVC/14.40/bin/Hostx64/x64/cl.exe", "");
    write("fakevs/VC/Tools/MSVC/14.40/bin/Hostx64/x64/link.exe", "");
    fs::permissions(gRoot / "fakevs/VC/Tools/MSVC/14.40/bin/Hostx64/x64/cl.exe", fs::perms::owner_all);
    fs::permissions(gRoot / "fakevs/VC/Tools/MSVC/14.40/bin/Hostx64/x64/link.exe", fs::perms::owner_all);
    o = sh("toolchains --search '" + bin.string() + "' --msvc-root '" + (gRoot / "fakevs").string() + "'", &rc);
    check(rc == 0, "toolchains exits 0", o);
    check(o.find("gcc\tfound") != string::npos && o.find((bin / "gcc").string()) != string::npos, "gcc found in the probe dir", o);
    check(o.find("clang\tfound") != string::npos && o.find((bin / "clang++").string()) != string::npos, "clang found in the probe dir", o);
    check(o.find("mingw\tfound") != string::npos && o.find("x86_64-w64-mingw32-g++-win32") != string::npos, "mingw found in the probe dir", o);
    check(o.find("msvc\tfound") != string::npos && o.find("cl.exe") != string::npos && o.find("link.exe") != string::npos, "msvc cl.exe and link.exe found under the root", o);
    check(o.find("host=posix") != string::npos, "this machine reports a posix host", o);
    check(o.find((gRoot / "fakevs/VC/Tools/MSVC/14.40/bin/Hostx64/x64/cl.exe").string()) != string::npos, "msvc compiler path is under the given root", o);
    fs::path exe = fs::temp_directory_path() / "okay-build-win-host.exe";
    if (fs::exists(exe) && system("command -v wine >/dev/null 2>&1") == 0) {
        string wo = capture("WINEPREFIX=$HOME/.wine-okay WINEDEBUG=-all wine '" + exe.string() + "' toolchains 2>&1", &rc);
        check(rc == 0 && wo.find("host=windows") != string::npos && wo.find("msvc\t") != string::npos, "cross-compiled probe runs under wine and names the windows host", wo);
    }
}

static pid_t launchBuild() {
    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        if (chdir(gRoot.c_str()) != 0) _exit(127);
        int fd = open("cancel.log", O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd >= 0) { dup2(fd, 1); dup2(fd, 2); }
        string bin = gTool;
        if (bin.size() >= 2 && bin.front() == '\'' && bin.back() == '\'') bin = bin.substr(1, bin.size() - 2);
        execl(bin.c_str(), bin.c_str(), "-j2", (char*)nullptr);
        _exit(127);
    }
    setpgid(pid, pid);
    return pid;
}
static void testCancelAndRecover() {
    printf("test: Ctrl+C stops a build, and a killed build resumes finished steps\n");
    fresh("kill9");
    write("build.okay",
          "action\n  cmd = echo x > fast\n  output = fast\nend\n"
          "action\n  cmd = sleep 30 && echo x > slow\n  output = slow\nend\n");
    pid_t pid = launchBuild();
    check(pid > 0, "started a build to kill");
    bool saw = false;
    for (int i = 0; i < 50; ++i) {
        if (fs::exists(gRoot / ".okaybuild/state.journal")) { std::ifstream j(gRoot / ".okaybuild/state.journal"); string s((std::istreambuf_iterator<char>(j)), {}); if (s.find("fast") != string::npos) { saw = true; break; } }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    kill(-pid, SIGKILL); int st = 0; waitpid(pid, &st, 0);
    check(saw, "the finished step was journaled before the kill");
    int rc; string o = sh("--dry-run -v", &rc);
    check(rc == 0 && o.find("[up to date]") != string::npos && o.find("fast") != string::npos && ran(o) == 1, "after kill -9 the finished step is kept and the other would run", o);
    fresh("sigint");
    write("build.okay", "action\n  cmd = sleep 30 && echo x > slow\n  output = slow\nend\n");
    pid = launchBuild();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto t0 = std::chrono::steady_clock::now();
    kill(-pid, SIGINT);
    bool reaped = false;
    for (int i = 0; i < 30; ++i) { if (waitpid(pid, &st, WNOHANG) == pid) { reaped = true; break; } std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
    if (!reaped) { kill(-pid, SIGKILL); waitpid(pid, &st, 0); }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    check(WIFEXITED(st) && WEXITSTATUS(st) == 130 && ms < 3000, "Ctrl+C stops the sleep and exits 130", "status=" + std::to_string(WIFEXITED(st) ? WEXITSTATUS(st) : -1) + " ms=" + std::to_string(ms) + "\n" + readFile(gRoot / "cancel.log"));
    check(!fs::exists(gRoot / "slow"), "the cancelled step did not produce its output");
}

static void testMemoryLimit() {
    printf("test: a memory limit keeps heavy steps from overlapping\n");
    auto three = [&](const string& name) {
        fresh(name);
        string man;
        for (int i = 0; i < 3; ++i)
            man += "action\n  cmd = sleep 0.3 && echo x > o" + std::to_string(i) + "\n  output = o" + std::to_string(i) + "\n  memory = 60\nend\n";
        write("build.okay", man);
    };
    three("memlim");
    auto t0 = std::chrono::steady_clock::now();
    int rc; string o = sh("--memory 100 -j4", &rc);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    check(rc == 0 && ms >= 700, "three steps of 60 stay serial under a limit of 100", o + "\nms=" + std::to_string(ms));
    three("memfree");
    t0 = std::chrono::steady_clock::now();
    o = sh("-j4", &rc);
    ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    check(rc == 0 && ms < 550, "without a limit the same steps overlap", o + "\nms=" + std::to_string(ms));
    fresh("memtoobig");
    write("build.okay", "action\n  cmd = echo x > big\n  output = big\n  memory = 500\nend\n");
    o = sh("--memory 100", &rc);
    check(rc == 2 && o.find("500") != string::npos && o.find("100") != string::npos, "a step bigger than the limit is refused", o);
}

static void testOwnTests() {
    printf("test: okay-build test discovers executables and runs them in parallel\n");
    fresh("trun");
    write("ok.cpp", "int main(){return 0;}\n");
    write("bad.cpp", "int main(){return 3;}\n");
    write("app.cpp", "int main(){return 0;}\n");
    write("s1.cpp", "#include <time.h>\nint main(){struct timespec t{0,300000000}; nanosleep(&t,0); return 0;}\n");
    write("s2.cpp", "#include <time.h>\nint main(){struct timespec t{0,300000000}; nanosleep(&t,0); return 0;}\n");
    write("build.okay",
          "target test_ok exe\n sources = ok.cpp\n test = on\nend\n"
          "target test_bad exe\n sources = bad.cpp\n test = on\nend\n"
          "target test_s1 exe\n sources = s1.cpp\n test = on\nend\n"
          "target test_s2 exe\n sources = s2.cpp\n test = on\nend\n"
          "target app exe\n sources = app.cpp\nend\n");
    int rc; string o = sh("test -j2", &rc);
    check(rc == 1, "a failing test makes the command exit 1", o);
    check(o.find("PASS") != string::npos && o.find("test_ok") != string::npos, "a passing test is named", o);
    check(o.find("FAIL") != string::npos && o.find("test_bad") != string::npos && o.find("exit 3") != string::npos, "a failing test reports exit 3", o);
    check(o.find("4 tests, 1 failed") != string::npos, "the summary counts four tests and one failure", o);
    check(o.find("CC app.cpp") == string::npos && o.find("PASS app") == string::npos, "the ordinary exe is not a test", o);
    auto t0 = std::chrono::steady_clock::now();
    o = sh("test -j2", &rc);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    check(rc == 1 && ms < 550, "the two 300 ms tests overlap", o + "\nms=" + std::to_string(ms));
}

static void testReport() {
    printf("test: okay-build report times steps, cache rate, and slowest steps\n");
    fresh("rep");
    write("a.cpp", "int a(){return 1;}\n");
    write("b.cpp", "int a(); int main(){return a()==1?0:1;}\n");
    write("build.okay", "target a lib\n sources = a.cpp\nend\ntarget b exe\n sources = b.cpp\n deps = a\nend\n");
    int rc; string o = sh("-j1", &rc);
    check(rc == 0, "build before the report", o);
    o = sh("report", &rc);
    check(rc == 0 && o.find("slowest") != string::npos && o.find("a.cpp") != string::npos, "report lists a step and the slowest steps", o);
    check(o.find("cache") != string::npos && o.find("miss") != string::npos && o.find(" ms") != string::npos, "report has cache misses and milliseconds", o);
    fs::remove(gRoot / ".okaybuild/obj/b/b.cpp.o");
    fs::remove(gRoot / ".okaybuild/obj/b/b.cpp.o.d");
    o = sh("-j1", &rc);
    check(rc == 0 && o.find("cache hit") != string::npos, "second build restores b.cpp from the cache", o);
    o = sh("report", &rc);
    check(rc == 0 && o.find("hit") != string::npos && o.find("%") != string::npos, "report shows the cache hit rate", o);
}

static void testCompilerCache() {
    printf("test: compiler cache hits, misses, and invalidation\n");
    fresh("cache");
    write("cxxwrap", "#!/bin/sh\necho \"$0 $*\" >> runs.log\nexec g++ \"$@\"\n");
    fs::permissions(gRoot / "cxxwrap", fs::perms::owner_all);
    write("main.cpp", "int main(){return 0;}\n");
    write("build.okay", "toolchain\n cxx = ./cxxwrap\n cc = ./cxxwrap\nend\ntarget app exe\n sources = main.cpp\nend\n");
    int rc; string o = sh("-j1", &rc);
    check(rc == 0 && o.find("cache miss") != string::npos, "first compile is a cache miss", o);
    int lines = countLines(gRoot / "runs.log");
    check(lines == 3, "miss runs preprocess, compile, and link", o + "\nlines=" + std::to_string(lines));
    fs::remove(gRoot / ".okaybuild/obj/app/main.cpp.o");
    fs::remove(gRoot / ".okaybuild/obj/app/main.cpp.o.d");
    o = sh("-j1", &rc);
    check(rc == 0 && o.find("cache hit") != string::npos, "deleted object is restored from the cache", o);
    check(countLines(gRoot / "runs.log") == lines + 1, "a hit preprocesses and does not compile", "lines=" + std::to_string(countLines(gRoot / "runs.log")));
    check(system(("'" + (gRoot / "build/app").string() + "'").c_str()) == 0, "restored program runs");
    write("main.cpp", "int main(){return 1;}\n");
    o = sh("-j1", &rc);
    check(rc == 0 && o.find("cache miss") != string::npos, "a changed source is a cache miss", o);
    write("cxxwrap", "#!/bin/sh\n# identity changed\necho \"$0 $*\" >> runs.log\nexec g++ \"$@\"\n");
    fs::permissions(gRoot / "cxxwrap", fs::perms::owner_all);
    fs::remove(gRoot / ".okaybuild/obj/app/main.cpp.o");
    fs::remove(gRoot / ".okaybuild/obj/app/main.cpp.o.d");
    int before = countLines(gRoot / "runs.log");
    o = sh("-j1", &rc);
    check(rc == 0 && o.find("cache miss") != string::npos, "a changed compiler identity misses", o);
    check(countLines(gRoot / "runs.log") >= before + 2, "identity miss compiles again", "lines=" + std::to_string(countLines(gRoot / "runs.log")));
}

static void testAuditHelp() {
    printf("test: --help names the audit note and the commands that exist\n");
    fresh("audit"); write("a.cpp", "int main(){return 0;}\n");
    write("build.okay", "target a exe\n sources = a.cpp\nend\n");
    string o = sh("--help");
    check(o.find("audit: docs/own_build_plan.md") != string::npos, "help points at the audit", o);
    check(o.find("modules") != string::npos && o.find("compdb") != string::npos && o.find("import") != string::npos, "help lists the commands the audit calls present", o);
}

static void testModules() {
    printf("test: module files generate a manifest that builds\n");
    fresh("mods");
    write("util.cpp", "int util(){return 4;}\n");
    write("main.cpp", "int util(); int main(){return util()==4?0:1;}\n");
    write("mods/util.okaymodule", "name = util\nkind = lib\nsources = util.cpp\n");
    write("mods/app.okaymodule", "name = app\nkind = exe\nsources = main.cpp\ndeps = util\n");
    int rc; string o = sh("modules mods -o build.okay", &rc);
    check(rc == 0 && o.find("2 module") != string::npos, "two modules written", o);
    string man = readFile(gRoot / "build.okay");
    check(man.find("target util lib") != string::npos && man.find("deps = util") != string::npos, "manifest has the lib and the dependency", man);
    o = sh("-j2", &rc);
    check(rc == 0 && ran(o) == 4, "generated manifest builds", o);
    check(system(("'" + (gRoot / "build/app").string() + "'").c_str()) == 0, "program from modules runs");
    write("mods/app.okaymodule", "name = app\nkind = exe\nsources = main.cpp\ndeps = missing\n");
    o = sh("modules mods -o build.okay", &rc);
    check(rc == 2 && o.find("missing") != string::npos, "unknown module dependency is an error", o);
}

static void testSharedInput() {
    printf("test: one changed input rebuilds every action that lists it\n");
    fresh("share");
    write("h.h", "#define V 1\n");
    string man;
    for (int i = 0; i < 6; ++i) {
        write("o" + std::to_string(i) + ".in", "x\n");
        man += "action\n  cmd = cp h.h o" + std::to_string(i) + ".out\n  output = o" + std::to_string(i) + ".out\n  inputs = h.h\nend\n";
    }
    write("build.okay", man);
    int rc; string o = sh("-j2", &rc); check(rc == 0 && ran(o) == 6, "six copies", o);
    check(ran(sh()) == 0, "shared input no-op");
    write("h.h", "#define V 2\n");
    o = sh("--explain"); check(ran(o) == 6, "editing the shared input rebuilds all six", o);
}

static void testImportedCompdb() {
    printf("test: compdb lists imported compile commands, not the link\n");
    fresh("icdb");
    write("src/a.cpp", "int a(){return 1;}\n");
    string src = (gRoot / "src/a.cpp").string();
    string dir = gRoot.string();
    write("build.okay",
          "action\n  dir = \"" + dir + "\"\n  cmd = g++ -Wall -c " + src + " -o a.o\n  output = a.o\n  inputs = " + src + "\nend\n"
          "action\n  dir = \"" + dir + "\"\n  cmd = g++ a.o -o prog\n  output = prog\n  inputs = a.o\nend\n");
    int rc; string o = sh("compdb", &rc);
    string j = readFile(gRoot / "compile_commands.json");
    check(rc == 0 && j.find("a.cpp") != string::npos && j.find("-Wall") != string::npos, "imported compile is in compile_commands.json", o + j);
    check(j.find("prog") == string::npos, "the link step is not a compile command", j);
}

static void testWindowsHost() {
    printf("test: okay-build compiles as a Windows program and runs under wine\n");
    fs::path cpp = fs::path(__FILE__).parent_path() / "okay-build.cpp";
    if (!fs::exists(cpp)) { printf("  skipped (okay-build.cpp not beside the test)\n"); return; }
    if (system("x86_64-w64-mingw32-g++-win32 -v >/dev/null 2>&1") != 0) { printf("  skipped (no mingw)\n"); return; }
    fs::path exe = fs::temp_directory_path() / "okay-build-win-host.exe";
    string compile = "x86_64-w64-mingw32-g++-win32 -std=c++17 -O2 -static-libgcc -static-libstdc++ \"" + cpp.string() + "\" -o \"" + exe.string() + "\" 2>&1";
    int rc; string o = capture(compile, &rc);
    check(rc == 0 && fs::exists(exe), "win32 MinGW cross-compile of okay-build", o);
    if (rc != 0) return;
    FILE* ff = popen(("file '" + exe.string() + "'").c_str(), "r");
    char fb[256] = {}; if (ff) { size_t n = fread(fb, 1, sizeof fb - 1, ff); pclose(ff); fb[n] = 0; }
    check(std::string(fb).find("PE32+") != std::string::npos, "Windows okay-build is PE32+", fb);
    if (system("command -v wine >/dev/null 2>&1") != 0) { printf("  wine not installed, runtime skipped\n"); return; }
    fresh("winhost");
    write("build.okay",
          "pool link\n  depth = 1\nend\n"
          "action\n  cmd = echo hi> hello.txt\n  output = hello.txt\nend\n"
          "action\n  cmd = echo side> side.txt\n  output = side.txt\n  inputs = hello.txt\nend\n");
    string run = "cd '" + gRoot.string() + "' && WINEDEBUG=-all WINEPREFIX=$HOME/.wine-okay wine '" + exe.string() + "' -j2 2>&1";
    o = capture(run, &rc);
    check(rc == 0 && ran(o) == 2, "wine ran both cmd steps", o);
    check(readFile(gRoot / "hello.txt").find("hi") != string::npos, "cmd.exe wrote hello.txt", readFile(gRoot / "hello.txt") + o);
    check(readFile(gRoot / "side.txt").find("side") != string::npos, "dependent step ran after its input", readFile(gRoot / "side.txt"));
    write("build.okay",
          "action\n  cmd = echo ok> good.txt\n  output = good.txt\nend\n"
          "action\n  cmd = exit /b 1\n  output = bad.txt\nend\n"
          "action\n  cmd = echo dep> dep.txt\n  output = dep.txt\n  inputs = bad.txt\nend\n");
    fs::remove(gRoot / "good.txt");
    o = capture("cd '" + gRoot.string() + "' && WINEDEBUG=-all WINEPREFIX=$HOME/.wine-okay wine '" + exe.string() + "' -j1 -k 2>&1", &rc);
    check(rc != 0 && fs::exists(gRoot / "good.txt") && !fs::exists(gRoot / "dep.txt"), "wine -k runs the independent step and skips the dependent", o);
}

static void testFifoJobserver() {
    printf("test: a GNU make fifo jobserver limits this build\n");
    fresh("fifo");
    write("slot.sh",
          "#!/bin/sh\ndir=\"$1\"\nout=\"$2\"\nmkdir -p \"$dir\"\nexec 9>>\"$dir/lock\"\nflock 9\n"
          "cur=$(cat \"$dir/count\" 2>/dev/null || echo 0)\ncur=$((cur+1))\necho \"$cur\" > \"$dir/count\"\n"
          "max=$(cat \"$dir/max\" 2>/dev/null || echo 0)\n"
          "if [ \"$cur\" -gt \"$max\" ]; then echo \"$cur\" > \"$dir/max\"; fi\nflock -u 9\nsleep 0.25\n"
          "exec 9>>\"$dir/lock\"\nflock 9\ncur=$(cat \"$dir/count\")\necho $((cur-1)) > \"$dir/count\"\nflock -u 9\n"
          "echo ok > \"$out\"\n");
    fs::permissions(gRoot / "slot.sh", fs::perms::owner_all);
    string man;
    for (int i = 0; i < 4; ++i) man += "action\n  cmd = ./slot.sh '" + (gRoot / "slots").string() + "' o" + std::to_string(i) + "\n  output = o" + std::to_string(i) + "\nend\n";
    write("build.okay", man);
    string fifo = (gRoot / "jobs.fifo").string();
    mkfifo(fifo.c_str(), 0600);
    int fd = open(fifo.c_str(), O_RDWR);
    check(fd >= 0, "fifo opened");
    char tok = '+'; check(write(fd, &tok, 1) == 1, "one token in the fifo");
    string tool = gTool;
    if (tool.size() >= 2 && tool.front() == '\'' && tool.back() == '\'') tool = tool.substr(1, tool.size() - 2);
    string cmd = "cd '" + gRoot.string() + "' && MAKEFLAGS='--jobserver-auth=fifo:" + fifo + "' '" + tool + "' -j8 2>&1";
    int rc = 0; string o = capture(cmd, &rc);
    int mx = 0; { std::ifstream f(gRoot / "slots/max"); f >> mx; }
    close(fd);
    check(rc == 0 && mx >= 1 && mx <= 2, "fifo auth keeps four jobs inside two slots", o + "\nmax=" + std::to_string(mx));
}

static void testReportDiff() {
    printf("test: okay-build report diff names steps that appeared\n");
    fresh("rdiff");
    write("a.cpp", "int main(){return 0;}\n");
    write("build.okay", "target app exe\n sources = a.cpp\nend\n");
    int rc; string o = sh("-j1", &rc);
    check(rc == 0, "first build", o);
    write("b.cpp", "int b(){return 1;}\n");
    write("build.okay", "target app exe\n sources = a.cpp b.cpp\nend\n");
    o = sh("-j1", &rc);
    check(rc == 0, "second build", o);
    o = sh("report diff 1 2", &rc);
    check(rc == 0 && o.find("only 2:") != string::npos && o.find("b.cpp") != string::npos, "diff shows the new source", o);
}

static void testDistLayout() {
    printf("test: package dist matches the Windows distribution layout\n");
    fresh("distl");
    string script = readFile(fs::path(__FILE__).parent_path().parent_path().parent_path() / "scripts/build-windows-dist.sh");
    check(script.find("OkaySpace.exe") != string::npos && script.find("EXAMPLE_DIRS=") != string::npos, "read the dist script");
    string examples = script.substr(script.find("EXAMPLE_DIRS="));
    auto q1 = examples.find('"'); auto q2 = examples.find('"', q1 + 1);
    examples = examples.substr(q1 + 1, q2 - q1 - 1);
    string firstEx = examples.substr(0, examples.find(' '));
    write("fake/OkaySpace.exe", "launcher\n");
    write("fake/SDL2.dll", "dll\n");
    write("fake/OkayEngine.exe", "editor\n");
    write("fake/OkaySpacePlayer.exe", "player\n");
    write("fake/README.txt", "hi\n");
    write("fake/textures/grid.png", "png\n");
    write("fake/fonts/DejaVuSans.ttf", "font\n");
    write("fake/examples/" + firstEx + "/scene.okayscene", "scene\n");
    write("build.okay",
          "dist\n launcher = fake/OkaySpace.exe\n dll = fake/SDL2.dll\n"
          " tool = fake/OkayEngine.exe\n tool = fake/OkaySpacePlayer.exe\n"
          " version = 9.9.9\n textures = fake/textures\n fonts = fake/fonts\n"
          " example = fake/examples/" + firstEx + "\n readme = fake/README.txt\nend\n");
    int rc; string o = sh("package", &rc);
    check(rc == 0, "package dist exits 0", o);
    std::vector<string> got;
    for (auto& e : fs::recursive_directory_iterator(gRoot / "dist")) if (e.is_regular_file())
        got.push_back(fs::relative(e.path(), gRoot / "dist").generic_string());
    std::sort(got.begin(), got.end());
    std::vector<string> expect = {"Assets/Fonts/DejaVuSans.ttf", "Assets/Textures/grid.png", "OkaySpace.exe", "README.txt", "SDL2.dll",
                             "Tools/OkayEngine.exe", "Tools/OkaySpacePlayer.exe", "Tools/SDL2.dll", "Tools/VERSION.txt", "VERSION.txt",
                             "examples/" + firstEx + "/scene.okayscene"};
    std::sort(expect.begin(), expect.end());
    check(got == expect, "layout matches the script's top exe, Tools DLL, version, assets and example", o);
}

static void testImportedCache() {
    printf("test: an imported gcc -c command is restored from the cache\n");
    fresh("icache");
    write("a.cpp", "int a(){return 3;}\n");
    string src = (gRoot / "a.cpp").string();
    string obj = (gRoot / "a.o").string();
    write("build.okay",
          "action\n  cmd = g++ -c " + src + " -o " + obj + "\n  output = " + obj + "\n  inputs = " + src + "\nend\n");
    int rc; string o = sh("-j1", &rc);
    check(rc == 0 && o.find("cache miss") != string::npos, "first imported compile is a miss", o);
    fs::remove(gRoot / "a.o");
    o = sh("-j1", &rc);
    check(rc == 0 && o.find("cache hit") != string::npos && fs::exists(gRoot / "a.o"), "deleted object is restored from the cache", o);
}

static void testNativeManifest() {
    printf("test: okay-build native writes a manifest from the source tree\n");
    fresh("native");
    write("engine/src/Crypto/A.cpp", "int crypto(){return 1;}\n");
    write("engine/src/Platform/Platform.cpp", "int plat(){return 2;}\n");
    write("engine/src/Platform/OwnWin32.cpp", "int win(){return 0;}\n");
    write("engine/src/Render/ModelImportAssimp.cpp", "int nope(){return 0;}\n");
    write("engine/src/Core/Core.cpp", "int core(){return 3;}\n");
    write("engine/third_party/ufbx/ufbx.c", "int ufbx_add(){return 4;}\n");
    write("engine/include/okay/Math/Math.hpp", "#pragma once\n");
    write("shared/Shared.hpp", "#pragma once\n");
    write("tests/test_math.cpp", "extern \"C\" int ufbx_add(); int crypto(); int plat(); int core(); int main(){return crypto()+plat()+core()+ufbx_add()==10?0:1;}\n");
    int rc; string o = sh("native . -o build.okay", &rc);
    check(rc == 0 && o.find("test_math") != string::npos, "native writes a manifest", o);
    string man = readFile(gRoot / "build.okay");
    check(man.find(" engine/third_party/ufbx shared /usr/include/SDL2") != string::npos, "the shared/ header directory is an include path", man);
    check(man.find("target okay_engine lib") != string::npos && man.find("target okay_crypto lib") != string::npos, "engine and crypto targets", man);
    check(man.find("ModelImportAssimp") == string::npos && man.find("OwnWin32") == string::npos, "optional files are left out", man);
    check(man.find("tests/test_math.cpp") != string::npos && man.find("ufbx.c") != string::npos, "test_math and ufbx are included", man);
    o = sh("-j2 test_math", &rc);
    check(rc == 0, "the generated manifest builds test_math", o);
    check(system(("'" + (gRoot / "build/test_math").string() + "' >/dev/null").c_str()) == 0, "test_math runs");
    { // member order follows engine/CMakeLists.txt when it lists the files
        write("engine/src/Crypto/B.cpp", "int cb(){return 0;}\n");
        write("engine/CMakeLists.txt", "add_library(okay_crypto STATIC\n  src/Crypto/B.cpp\n  src/Crypto/A.cpp\n)\n");
        sh("native . -o b2.okay");
        string m2 = readFile(gRoot / "b2.okay");
        auto pb = m2.find("engine/src/Crypto/B.cpp"), pa = m2.find("engine/src/Crypto/A.cpp");
        check(pb != string::npos && pa != string::npos && pb < pa, "crypto sources follow the order engine/CMakeLists.txt lists them", m2);
        fs::remove(gRoot / "engine/CMakeLists.txt"); sh("native . -o b3.okay"); string m3 = readFile(gRoot / "b3.okay");
        check(m3.find("engine/src/Crypto/A.cpp") < m3.find("engine/src/Crypto/B.cpp"), "without that file the order is by name", m3);
        fs::remove(gRoot / "engine/src/Crypto/B.cpp");
        write("engine/src/Platform/SdlBackend.cpp", "int sdlb(){return 0;}\n");
        write("cmake/OkayPlatform.cmake", "list(APPEND _srcs src/Platform/SdlBackend.cpp)\nlist(APPEND _srcs src/Platform/Platform.cpp)\n");
        sh("native . -o b5.okay"); string m5 = readFile(gRoot / "b5.okay");
        check(m5.find("engine/src/Platform/SdlBackend.cpp") < m5.find("engine/src/Platform/Platform.cpp"), "platform sources follow cmake/OkayPlatform.cmake", m5);
        fs::remove(gRoot / "cmake/OkayPlatform.cmake"); fs::remove(gRoot / "engine/src/Platform/SdlBackend.cpp");
        write("CMakeLists.txt", "cmake_minimum_required(VERSION 3.16)\nproject(Fake\n    VERSION 1.2.3\n    LANGUAGES CXX)\n");
        sh("native . -o b4.okay"); string m4 = readFile(gRoot / "b4.okay");
        check(m4.find("\"OKAY_ENGINE_VERSION=\\\"1.2.3\\\"\"") != string::npos && m4.find("-Wno-narrowing") != string::npos, "the engine version from project(... VERSION x) becomes a define", m4);
        fs::remove(gRoot / "CMakeLists.txt");
    }
    check(man.find("path_map = on") != string::npos, "the native manifest turns path_map on", man);
    check(man.find("account_defines = on") != string::npos, "the native manifest asks for the account defines", man);
}

static void testAccountDefines() {
    printf("test: account_defines reads the environment or config/public-account.json and never prints the values\n"); fresh("acct");
    unsetenv("OKAY_DEFAULT_ACCOUNT_URL"); unsetenv("OKAY_DEFAULT_ACCOUNT_KEY");
    write("a.cpp", "#include <cstdio>\n#ifndef OKAY_DEFAULT_ACCOUNT_KEY\n#define OKAY_DEFAULT_ACCOUNT_KEY \"none\"\n#endif\n#ifndef OKAY_DEFAULT_ACCOUNT_URL\n#define OKAY_DEFAULT_ACCOUNT_URL \"none\"\n#endif\nint main(){printf(\"%s %s\\n\", OKAY_DEFAULT_ACCOUNT_URL, OKAY_DEFAULT_ACCOUNT_KEY); return 0;}\n");
    write("build.okay", "account_defines = on\ntarget app exe\n sources = a.cpp\nend\n");
    int rc; string o = sh("-j1", &rc);
    check(rc == 0 && o.find("account defines: url no, key no (from none)") != string::npos, "nothing set: says so and builds", o);
    write("config/public-account.json", "{\n  \"url\": \"https://file.invalid/u\",\n  \"key\": \"filekey_AAAA\"\n}\n");
    o = sh("-j1", &rc);
    check(rc == 0 && o.find("(from config/public-account.json)") != string::npos && o.find("filekey_AAAA") == string::npos && o.find("file.invalid") == string::npos, "the json file is used and its values are not printed", o);
    o = capture("'" + (gRoot / "build/app").string() + "'", &rc);
    check(o == "https://file.invalid/u filekey_AAAA\n", "the program sees the json values", o);
    setenv("OKAY_DEFAULT_ACCOUNT_URL", "https://env.invalid/u", 1); setenv("OKAY_DEFAULT_ACCOUNT_KEY", "envkey_BBBB", 1);
    o = sh("-j1 -v", &rc);
    check(rc == 0 && o.find("(from environment)") != string::npos && o.find("envkey_BBBB") == string::npos && o.find("env.invalid") == string::npos && o.find("<hidden>") != string::npos, "the environment wins; -v shows <hidden>, not the values", o);
    o = capture("'" + (gRoot / "build/app").string() + "'", &rc);
    check(o == "https://env.invalid/u envkey_BBBB\n", "the program sees the environment values", o);
    write("a.cpp", readFile(gRoot / "a.cpp") + "// changed\n");
    o = sh("-n -v --no-cache", &rc);
    check(o.find("envkey_BBBB") == string::npos && o.find("<hidden>") != string::npos, "a dry run hides them too", o);
    sh("compdb");
    string cdb = readFile(gRoot / "compile_commands.json");
    check(cdb.find("envkey_BBBB") == string::npos && cdb.find("env.invalid") == string::npos && cdb.find("<hidden>") != string::npos, "compile_commands.json does not hold the values", cdb);
    write("a.cpp", "#error broken\n");
    o = sh("-j1", &rc);
    check(rc != 0 && o.find("broken") != string::npos && o.find("envkey_BBBB") == string::npos && o.find("env.invalid") == string::npos, "a failing compile does not echo the command with the values", o);
    unsetenv("OKAY_DEFAULT_ACCOUNT_URL"); unsetenv("OKAY_DEFAULT_ACCOUNT_KEY");
}

static void testRestoredObjectsStayTrusted() {
    printf("test: objects restored from the cache do not make every later no-op re-read them\n"); fresh("restore"); basicProject();
    sh("-j2");
    write("inc/util.h", "#pragma once\nint util();\n// a comment: same preprocessed text, so the compiles are cache hits\n");
    int rc; string o = sh("-j2", &rc); check(rc == 0 && o.find("cache hit") != string::npos, "the comment edit is served from the cache", o);
    o = sh("-v", &rc); check(o.find("hashed 0 input file(s)") != string::npos, "the next no-op reads no file again", o);
    o = sh("-v", &rc); check(ran(o) == 0 && o.find("hashed 0 input file(s)") != string::npos, "and neither does the one after", o);
}
static void testBinaryState() {
    printf("test: compact binary state, with the old text format still accepted\n");
    fresh("bstate");
    write("a.cpp", "int main(){return 0;}\n");
    write("build.okay", "target app exe\n sources = a.cpp\nend\n");
    int rc; string o = sh("-j1", &rc);
    check(rc == 0, "build before reading state", o);
    string st = readFile(gRoot / ".okaybuild/state");
    check(st.size() >= 4 && st.compare(0, 4, "OKB1") == 0, "state file is the binary OKB1 format");
    o = sh("-j1", &rc);
    check(rc == 0 && ran(o) == 0, "binary state keeps the next build a no-op", o);
    { std::ofstream f(gRoot / ".okaybuild/state", std::ios::trunc); f << "okaybuild-state 4\n"; }
    o = sh("-j1", &rc);
    check(rc == 0 && ran(o) > 0, "a text state file is still accepted", o);
}

static void testHistoryAndChecks() {
    printf("test: report history keeps builds, and test output yields check counts\n");
    fresh("hist");
    write("a.cpp", "int main(){return 0;}\n");
    write("build.okay", "target app exe\n sources = a.cpp\nend\n");
    int rc; string o = sh("-j1", &rc);
    check(rc == 0, "first build for history", o);
    o = sh("report history", &rc);
    check(rc == 0 && o.find("history: 1") != string::npos, "one build is remembered", o);
    write("a.cpp", "int main(){return 1;}\n");
    o = sh("-j1", &rc);
    check(rc == 0, "second build for history", o);
    o = sh("report history", &rc);
    check(rc == 0 && o.find("history: 2") != string::npos, "two builds are remembered", o);
    fresh("checks");
    write("t.cpp", "#include <cstdio>\nint main(){printf(\"4 checks, 1 failed\\n\"); return 1;}\n");
    write("build.okay", "target test_counts exe\n sources = t.cpp\n test = on\nend\n");
    o = sh("test -j1", &rc);
    check(rc == 1 && o.find("checks 4") != string::npos && o.find("failed 1") != string::npos, "a test's own check line is reported", o);
}

static void testPackage() {
    printf("test: okay-build package lays out an exe, runtime files and assets\n");
    fresh("pack");
    write("main.cpp", "int main(){return 0;}\n");
    write("side.dll", "dll\n");
    write("extras/readme.txt", "hello\n");
    write("extras/nested/a.txt", "a\n");
    write("build.okay",
          "toolchain\n exe_suffix = .exe\nend\n"
          "target app exe\n sources = main.cpp\n runtime = side.dll\n assets = extras\nend\n");
    int rc; string o = sh("package -j1", &rc);
    check(rc == 0 && fs::exists(gRoot / "dist/app/app.exe") && fs::exists(gRoot / "dist/app/side.dll"), "exe and runtime sit side by side", o);
    check(readFile(gRoot / "dist/app/extras/nested/a.txt") == "a\n", "asset folders are copied with their structure");
    string man = readFile(gRoot / "dist/app/PACKAGE.txt");
    check(man.find("okay-package 1") != string::npos && man.find("app.exe") != string::npos && man.find("side.dll") != string::npos && man.find("extras/nested/a.txt") != string::npos, "PACKAGE.txt lists what was copied", man);
}

static void testPackages() {
    printf("test: local packages resolve into okay.lock and conflicts are errors\n");
    fresh("pkg");
    write("a.cpp", "int main(){return 0;}\n");
    write("vendor/foo/README", "foo\n");
    write("vendor/bar/README", "bar\n");
    write("build.okay",
          "package foo\n path = vendor/foo\n version = 1.2\nend\n"
          "package bar\n path = vendor/bar\n version = 2.0\n depend = foo 1.2\nend\n"
          "target app exe\n sources = a.cpp\nend\n");
    int rc; string o = sh("-j1", &rc);
    check(rc == 0, "a build with packages exits 0", o);
    string lock = readFile(gRoot / "okay.lock");
    check(lock.find("okay-lock 1") != string::npos && lock.find("foo\t1.2\tvendor/foo") != string::npos && lock.find("bar\t2.0\tvendor/bar") != string::npos, "okay.lock records name, version and path", lock);
    o = sh("-j1", &rc);
    check(rc == 0 && ran(o) == 0, "the same lock builds again", o);
    write("build.okay",
          "package foo\n path = vendor/foo\n version = 1.3\nend\n"
          "package bar\n path = vendor/bar\n version = 2.0\n depend = foo 1.3\nend\n"
          "target app exe\n sources = a.cpp\nend\n");
    o = sh("-j1", &rc);
    check(rc == 2 && o.find("1.2") != string::npos && o.find("1.3") != string::npos, "a changed version disagrees with the lock", o);
    write("build.okay",
          "package foo\n path = vendor/foo\n version = 1.2\nend\n"
          "package bar\n path = vendor/bar\n version = 2.0\n depend = foo 9\nend\n"
          "target app exe\n sources = a.cpp\nend\n");
    fs::remove(gRoot / "okay.lock");
    o = sh("-j1", &rc);
    check(rc == 2 && o.find("needs foo 9") != string::npos && o.find("foo is 1.2") != string::npos, "a dependency on another version is a conflict", o);
    write("build.okay",
          "package foo\n path = vendor/foo\n version = 1.2\nend\n"
          "package bar\n path = vendor/bar\n version = 2.0\n depend = foo 1.2\nend\n"
          "target app exe\n sources = a.cpp\nend\n");
    fs::remove(gRoot / "okay.lock");
    o = sh("-j1", &rc);
    check(rc == 0 && readFile(gRoot / "okay.lock").find("\t") != string::npos, "lock is rewritten", o);
    write("vendor/foo/README", "foo changed\n");
    o = sh("-j1", &rc);
    check(rc == 2 && o.find("files changed") != string::npos && o.find("foo") != string::npos, "a changed vendored file is refused", o);
}

static void testCacheLimit() {
    printf("test: cache size limit evicts the least recently used entry\n");
    fresh("cmax");
    write("a.cpp", "int a(){return 1;}\n");
    write("b.cpp", "int b(){return 2;}\n");
    write("c.cpp", "int a(); int b(); int main(){return a()+b()==3?0:1;}\n");
    write("build.okay", "target app exe\n sources = a.cpp b.cpp c.cpp\nend\n");
    int rc; string o = sh("-j1 --cache-max 1", &rc);
    check(rc == 0, "build with a tiny cache limit", o);
    int entries = 0;
    if (fs::exists(gRoot / ".okaybuild/cache")) for (auto& e : fs::directory_iterator(gRoot / ".okaybuild/cache")) if (e.is_directory()) ++entries;
    check(entries == 1, "only the newest cache entry is kept", "entries=" + std::to_string(entries));
    o = sh("cache stats", &rc);
    check(rc == 0 && o.find("entries: 1") != string::npos && o.find("limit: 1") != string::npos, "cache stats shows the entry and the limit", o);
    fs::remove(gRoot / ".okaybuild/obj/app/a.cpp.o");
    fs::remove(gRoot / ".okaybuild/obj/app/b.cpp.o");
    fs::remove(gRoot / ".okaybuild/obj/app/c.cpp.o");
    fs::remove(gRoot / ".okaybuild/obj/app/a.cpp.o.d");
    fs::remove(gRoot / ".okaybuild/obj/app/b.cpp.o.d");
    fs::remove(gRoot / ".okaybuild/obj/app/c.cpp.o.d");
    o = sh("-j1", &rc);
    check(rc == 0 && o.find("cache hit") != string::npos && o.find("cache miss") != string::npos, "the kept entry hits and the evicted ones miss", o);
    o = sh("cache clean", &rc);
    check(rc == 0, "cache clean exits 0", o);
    o = sh("cache stats", &rc);
    check(rc == 0 && o.find("entries: 0") != string::npos, "cache stats is empty after clean", o);
    check(!fs::exists(gRoot / ".okaybuild/cache"), "the cache directory is gone");
}

static void testJobserver() {
    printf("test: a jobserver keeps nested okay-build runs inside -j\n");
    auto tree = [&](const string& name) {
        fresh(name);
        write("slot.sh",
              "#!/bin/sh\n"
              "dir=\"$1\"\nout=\"$2\"\nmkdir -p \"$dir\"\n"
              "exec 9>>\"$dir/lock\"\nflock 9\n"
              "cur=$(cat \"$dir/count\" 2>/dev/null || echo 0)\ncur=$((cur+1))\necho \"$cur\" > \"$dir/count\"\n"
              "max=$(cat \"$dir/max\" 2>/dev/null || echo 0)\n"
              "if [ \"$cur\" -gt \"$max\" ]; then echo \"$cur\" > \"$dir/max\"; fi\n"
              "flock -u 9\nsleep 0.3\n"
              "exec 9>>\"$dir/lock\"\nflock 9\n"
              "cur=$(cat \"$dir/count\")\necho $((cur-1)) > \"$dir/count\"\nflock -u 9\n"
              "echo ok > \"$out\"\n");
        fs::permissions(gRoot / "slot.sh", fs::perms::owner_all);
        string slot = (gRoot / "slots").string();
        string child;
        for (int i = 0; i < 4; ++i) {
            string o = "c" + std::to_string(i) + ".out";
            child += "action\n  cmd = ./slot.sh '" + slot + "' " + o + "\n  output = " + o + "\nend\n";
        }
        write("child.okay", child);
        string tool = gTool;
        if (tool.size() >= 2 && tool.front() == '\'' && tool.back() == '\'') tool = tool.substr(1, tool.size() - 2);
        write("parent.okay",
              "action\n  cmd = '" + tool + "' -f child.okay -j8\n  output = one.done\nend\n"
              "action\n  cmd = '" + tool + "' -f child.okay -j8 && echo ok > two.done\n  output = two.done\nend\n"
              "action\n  cmd = echo ok > one.done\n  output = one.done\nend\n");
    };
    // The first child action must produce one.done. Fix: both children echo their own output.
    auto tree2 = [&](const string& name) {
        fresh(name);
        write("slot.sh",
              "#!/bin/sh\n"
              "dir=\"$1\"\nout=\"$2\"\nmkdir -p \"$dir\"\n"
              "exec 9>>\"$dir/lock\"\nflock 9\n"
              "cur=$(cat \"$dir/count\" 2>/dev/null || echo 0)\ncur=$((cur+1))\necho \"$cur\" > \"$dir/count\"\n"
              "max=$(cat \"$dir/max\" 2>/dev/null || echo 0)\n"
              "if [ \"$cur\" -gt \"$max\" ]; then echo \"$cur\" > \"$dir/max\"; fi\n"
              "flock -u 9\nsleep 0.3\n"
              "exec 9>>\"$dir/lock\"\nflock 9\n"
              "cur=$(cat \"$dir/count\")\necho $((cur-1)) > \"$dir/count\"\nflock -u 9\n"
              "echo ok > \"$out\"\n");
        fs::permissions(gRoot / "slot.sh", fs::perms::owner_all);
        string slot = (gRoot / "slots").string();
        string child;
        for (int i = 0; i < 4; ++i) {
            string o = "c" + std::to_string(i) + ".out";
            child += "action\n  cmd = ./slot.sh '" + slot + "' " + o + "\n  output = " + o + "\nend\n";
        }
        write("child.okay", child);
        string tool = gTool;
        if (tool.size() >= 2 && tool.front() == '\'' && tool.back() == '\'') tool = tool.substr(1, tool.size() - 2);
        write("parent.okay",
              "action\n  cmd = mkdir -p one && cp child.okay slot.sh one/ && cd one && '" + tool + "' -f child.okay -j8 && echo ok > ../one.done\n  output = one.done\nend\n"
              "action\n  cmd = mkdir -p two && cp child.okay slot.sh two/ && cd two && '" + tool + "' -f child.okay -j8 && echo ok > ../two.done\n  output = two.done\nend\n");
    };
    tree2("js");
    int rc; string o = sh("-f parent.okay -j2 --jobserver", &rc);
    int mx = 0; { std::ifstream f(gRoot / "slots/max"); f >> mx; }
    check(rc == 0 && mx > 0 && mx <= 2, "nested builds share a pool of 2", o + "\nmax=" + std::to_string(mx));
    tree2("noj");
    o = sh("-f parent.okay -j2", &rc);
    int mx2 = 0; { std::ifstream f(gRoot / "slots/max"); f >> mx2; }
    check(rc == 0 && mx2 >= 4, "without a jobserver the children overlap", o + "\nmax=" + std::to_string(mx2));
    (void)tree;
}

static void testMsvcFlags() {
    printf("test: MSVC flag spelling, /showIncludes deps, response files\n");
    fresh("msvc");
    write("fakecl",
          "#!/bin/sh\n"
          "printf 'ARG %s\\n' \"$@\" >> argv.log\n"
          "for a in \"$@\"; do\n"
          "  case \"$a\" in\n"
          "    @*) python3 -c 'import sys; d=open(sys.argv[1],\"rb\").read();\n"
          "t=d[2:].decode(\"utf-16-le\") if d.startswith(b\"\\xff\\xfe\") else d.decode(\"utf-8\", \"replace\")\n"
          "open(\"argv.log\",\"a\").write(\"\".join(\"RSP \"+ln+\"\\n\" for ln in t.splitlines()))' \"${a#@}\" ;;\n"
          "  esac\n"
          "done\n"
          "out=\n"
          "scan() { case \"$1\" in /Fo*) out=${1#/Fo} ;; /Fe*) out=${1#/Fe} ;; /OUT:*) out=${1#/OUT:} ;; esac; }\n"
          "for a in \"$@\"; do scan \"$a\"; case \"$a\" in @*) python3 -c 'import sys; d=open(sys.argv[1],\"rb\").read(); t=d[2:].decode(\"utf-16-le\") if d.startswith(b\"\\xff\\xfe\") else d.decode(\"utf-8\",\"replace\"); open(\"rsp.decoded\",\"w\").write(t.replace(\"\\r\",\"\"))' \"${a#@}\"; while IFS= read -r line; do scan \"$line\"; done < rsp.decoded ;; esac; done\n"
          "echo 'Note: including file: inc/util.h'\n"
          "echo 'Note: including file:  inc/nested.h'\n"
          "if [ -n \"$out\" ]; then mkdir -p \"$(dirname \"$out\")\"; printf 'obj\\n' > \"$out\"; fi\n"
          "exit 0\n");
    fs::permissions(gRoot / "fakecl", fs::perms::owner_all);
    fs::copy(gRoot / "fakecl", gRoot / "link.exe"); fs::permissions(gRoot / "link.exe", fs::perms::owner_all);
    fs::copy(gRoot / "fakecl", gRoot / "lib.exe"); fs::permissions(gRoot / "lib.exe", fs::perms::owner_all);
    write("inc/util.h", "#define V 1\n");
    write("inc/nested.h", "#define N 1\n");
    write("main.cpp", "int main(){return 0;}\n");
    write("build.okay",
          "toolchain msvc\n cc = ./fakecl\n cxx = ./fakecl\n ld = ./link.exe\n ar = ./lib.exe\n rsp_threshold = 40\nend\n"
          "config debug\n optimize = 0\n debug = on\n define = MODE=debug\nend\n"
          "target app exe\n sources = main.cpp\n include = inc\n define = NAME=app\n libs = user32\n link_flags = -Llibs\nend\n");
    int rc; string o = sh("--config debug -j1 --no-cache", &rc);
    check(rc == 0, "fake cl.exe build exits 0", o);
    string args = readFile(gRoot / "argv.log");
    check(args.find("/showIncludes") != string::npos && args.find("/Od") != string::npos && args.find("/Zi") != string::npos, "compile uses /showIncludes /Od /Zi", args);
    check(args.find("/DMODE=debug") != string::npos && args.find("/DNAME=app") != string::npos && args.find("/Iinc") != string::npos, "defines and includes use /D and /I", args);
    check(args.find("/Fo") != string::npos, "object path uses /Fo", args);
    check(args.find("/OUT:") != string::npos && args.find("/LIBPATH:libs") != string::npos && args.find("user32.lib") != string::npos, "link uses /OUT: /LIBPATH: and name.lib", args);
    check(args.find("RSP ") != string::npos, "a long MSVC command goes through a response file", args);
    bool bom = false;
    if (fs::exists(gRoot / ".okaybuild/rsp")) for (auto& e : fs::directory_iterator(gRoot / ".okaybuild/rsp")) {
        std::ifstream f(e.path(), std::ios::binary); unsigned char b[2] = {}; f.read((char*)b, 2);
        if (b[0] == 0xFF && b[1] == 0xFE) {
            string rest((std::istreambuf_iterator<char>(f)), {});
            string decoded; for (size_t i = 0; i + 1 < rest.size(); i += 2) decoded.push_back(rest[i]);
            bom = decoded.find("/showIncludes") != string::npos || decoded.find("/OUT:") != string::npos || decoded.find("/Fo") != string::npos;
        }
    }
    check(bom, "msvc response file is UTF-16LE with a BOM and decodes to flags");
    write("inc/util.h", "#define V 2\n");
    o = sh("-j1 --no-cache", &rc);
    check(rc == 0 && ran(o) >= 1, "a header named by /showIncludes is a dependency", o);
    write("build.okay",
          "toolchain msvc\n cc = ./fakecl\n cxx = ./fakecl\n ld = ./fakecl\n ar = ./lib.exe\nend\n"
          "target app exe\n sources = main.cpp\nend\n");
    fs::remove(gRoot / "argv.log");
    o = sh("-j1 --no-cache", &rc);
    args = readFile(gRoot / "argv.log");
    check(rc == 0 && args.find("/Fe") != string::npos, "cl.exe as the linker uses /Fe", args + o);
}

static void testGraphTargets() {
    printf("test: targets and graph listings, including the link pool\n");
    fresh("graph"); basicProject();
    string o = sh("targets");
    check(o.find("util\tlib") != string::npos && o.find("app\texe") != string::npos, "targets lists the lib and the exe", o);
    o = sh("graph app");
    check(o.find("\tLD\t") != string::npos && o.find("pool=link") != string::npos, "graph shows the link step in the link pool", o);
    check(o.find("src/util.cpp") != string::npos && o.find("deps ") != string::npos, "graph of app includes the library object and edges", o);
    write("build.okay",
          "action\n  cmd = echo hi > note.txt\n  output = note.txt\n  pool = console\nend\n"
          "target app exe\n  sources = missing.cpp\nend\n");
    write("missing.cpp", "int main(){return 0;}\n");
    o = sh("graph");
    check(o.find("pool=console") != string::npos, "console pool is shown", o);
    write("build.okay", "action\n  cmd = echo x > a.txt\n  output = a.txt\n  pool = nope\nend\n");
    int rc; o = sh("", &rc);
    check(rc == 2 && o.find("unknown pool") != string::npos, "unknown pool is an error", o);
}

static void testToolchainAuto() {
    printf("test: toolchain auto and --toolchain pick a detected compiler\n");
    fresh("tcauto");
    write("main.cpp", "int main(){return 0;}\n");
    fs::path bin = gRoot / "probe"; fs::create_directories(bin);
    string gxx = capture("command -v g++", nullptr); while (!gxx.empty() && (gxx.back() == '\n')) gxx.pop_back();
    string gcc = capture("command -v gcc", nullptr); while (!gcc.empty() && (gcc.back() == '\n')) gcc.pop_back();
    fs::create_symlink(gxx, bin / "g++"); fs::create_symlink(gcc, bin / "gcc");
    write("build.okay", "toolchain auto\nend\ntarget app exe\n sources = main.cpp\nend\n");
    int rc; string o = sh("-j1 --search '" + bin.string() + "'", &rc);
    check(rc == 0 && fs::exists(gRoot / "build/app"), "toolchain auto builds with a detected gcc", o);
    o = sh("compdb --search '" + bin.string() + "'", &rc);
    check(readFile(gRoot / "compile_commands.json").find((bin / "g++").string()) != string::npos, "the build uses the detected compiler path", readFile(gRoot / "compile_commands.json"));
    // a hand-set cc is kept
    write("build.okay", "toolchain auto\n cxx = /opt/mine/g++\nend\ntarget app exe\n sources = main.cpp\nend\n");
    o = sh("compdb --search '" + bin.string() + "'", &rc);
    check(readFile(gRoot / "compile_commands.json").find("/opt/mine/g++") != string::npos, "a cxx set by hand wins over detection");
    // --toolchain overrides the manifest, msvc comes from --msvc-root
    write("build.okay", "target app exe\n sources = main.cpp\nend\n");
    fs::create_directories(gRoot / "fakevs/VC/Tools/MSVC/14.40/bin/Hostx64/x64");
    for (const char* n : {"cl.exe", "link.exe", "lib.exe"}) { write(string("fakevs/VC/Tools/MSVC/14.40/bin/Hostx64/x64/") + n, ""); fs::permissions(gRoot / "fakevs/VC/Tools/MSVC/14.40/bin/Hostx64/x64" / n, fs::perms::owner_all); }
    o = sh("compdb --toolchain msvc --msvc-root '" + (gRoot / "fakevs").string() + "'", &rc);
    string db = readFile(gRoot / "compile_commands.json");
    check(rc == 0 && db.find("fakevs/VC/Tools/MSVC/14.40/bin/Hostx64/x64/cl.exe") != string::npos && db.find(".obj") != string::npos, "--toolchain msvc uses the detected cl.exe and .obj", o + db);
    o = sh("compdb --toolchain msvc", &rc);
    check(rc == 2 && o.find("msvc") != string::npos && o.find("not found") != string::npos, "a toolchain that is not installed is an error", o);
    o = sh("compdb --toolchain nosuch", &rc);
    check(rc == 2 && o.find("unknown toolchain") != string::npos, "an unknown toolchain name is an error", o);
    write("build.okay", "toolchain auto\nend\ntarget app exe\n sources = main.cpp\nend\n");
    o = capture("cd '" + gRoot.string() + "' && PATH=/nonexistent " + gTool + " compdb 2>&1", &rc);
    check(rc == 2 && o.find("no compiler found") != string::npos, "toolchain auto with nothing installed says so", o);
}

static void testSharedCache() {
    printf("test: a shared cache dir serves a second build tree, and a damaged entry is a miss\n");
    fresh("sharedcache");
    write("main.cpp", "int main(){return 0;}\n");
    write("build.okay", "target app exe\n sources = main.cpp\nend\n");
    string cd = (gRoot / "shared").string();
    int rc; string o = sh("-j1 --build-dir b1 --cache-dir '" + cd + "'", &rc);
    check(rc == 0 && o.find("cache miss") != string::npos, "first tree misses and fills the shared cache", o);
    check(fs::exists(gRoot / "shared/index") && !fs::exists(gRoot / "b1/.okaybuild/cache"), "entries live in the shared dir, not the build tree");
    o = sh("-j1 --build-dir b2 --cache-dir '" + cd + "'", &rc);
    check(rc == 0 && o.find("cache hit") != string::npos && o.find("cache miss") == string::npos, "a different build tree hits the same entry", o);
    check(system(("'" + (gRoot / "b2/build/app").string() + "'").c_str()) == 0, "program from the second tree runs");
    // environment variable does the same as the option
    o = capture("cd '" + gRoot.string() + "' && OKAY_CACHE_DIR='" + cd + "' " + gTool + " -j1 --build-dir b3 2>&1", &rc);
    check(rc == 0 && o.find("cache hit") != string::npos, "OKAY_CACHE_DIR selects the shared cache", o);
    // damage the stored object: the entry must be refused and rebuilt, not copied
    string key; for (auto& e : fs::directory_iterator(gRoot / "shared")) if (e.is_directory()) key = e.path().filename().string();
    check(!key.empty(), "found a cache entry");
    { std::ofstream f(gRoot / "shared" / key / "obj", std::ios::trunc | std::ios::binary); f << "broken"; }
    o = sh("-j1 --build-dir b4 --cache-dir '" + cd + "'", &rc);
    check(rc == 0 && o.find("cache miss") != string::npos && o.find("cache hit") == string::npos, "a truncated entry is a miss", o);
    o = sh("-j1 --build-dir b5 --cache-dir '" + cd + "'", &rc);
    check(rc == 0 && o.find("cache hit") != string::npos, "the entry was replaced by a good one", o);
    o = sh("cache stats --cache-dir '" + cd + "'", &rc);
    check(rc == 0 && o.find("entries: 1") != string::npos, "cache stats reads the shared dir", o);
}

static void testReportCacheAndJson() {
    printf("test: report shows cache changes in a diff, totals by kind, and a json form\n");
    fresh("rjson");
    write("a.cpp", "int main(){return 0;}\n");
    write("build.okay", "target app exe\n sources = a.cpp\nend\n");
    int rc; string o = sh("-j1", &rc);
    check(rc == 0, "first build", o);
    fs::remove(gRoot / ".okaybuild/obj/app/a.cpp.o"); fs::remove(gRoot / ".okaybuild/obj/app/a.cpp.o.d");
    o = sh("-j1", &rc);
    check(rc == 0 && o.find("cache hit") != string::npos, "second build restores the object from the cache", o);
    o = sh("report diff 1 2", &rc);
    check(rc == 0 && o.find("cache: a.cpp miss -> hit") != string::npos, "diff names a miss that became a hit", o);
    o = sh("report", &rc);
    check(rc == 0 && o.find("by kind:") != string::npos && o.find("CC\t1 step(s)") != string::npos, "report totals time by step kind", o);
    o = sh("report json", &rc);
    check(rc == 0 && o.find("{\"steps\": 1") != string::npos && o.find("\"hits\": 1") != string::npos && o.find("\"kind\": \"CC\"") != string::npos && o.find("\"cache\": \"hit\"") != string::npos, "report json carries steps, hits and per-step cache state", o);
}

static void testTestFilterTimeoutOutput() {
    printf("test: okay-build test can pick programs, shows a failing test's output, and enforces --timeout\n");
    fresh("tfilter");
    write("ok.cpp", "int main(){return 0;}\n");
    write("bad.cpp", "#include <cstdio>\nint main(){puts(\"expected 4 got 5\"); return 3;}\n");
    write("hang.cpp", "#include <unistd.h>\nint main(){sleep(30); return 0;}\n");
    write("app.cpp", "int main(){return 0;}\n");
    write("build.okay",
          "target test_ok exe\n sources = ok.cpp\n test = on\nend\n"
          "target test_bad exe\n sources = bad.cpp\n test = on\nend\n"
          "target test_hang exe\n sources = hang.cpp\n test = on\nend\n"
          "target app exe\n sources = app.cpp\nend\n");
    int rc; string o = sh("test test_ok -j2", &rc);
    check(rc == 0 && o.find("PASS test_ok") != string::npos && o.find("1 tests, 0 failed") != string::npos && o.find("test_bad") == string::npos, "only the named test runs", o);
    o = sh("test app -j2", &rc);
    check(rc == 0 && o.find("PASS app") != string::npos, "any exe can be named", o);
    o = sh("test nosuch", &rc);
    check(rc == 2 && o.find("no test program named 'nosuch'") != string::npos, "an unknown name is an error", o);
    o = sh("test test_bad", &rc);
    check(rc == 1 && o.find("FAIL test_bad exit 3") != string::npos && o.find("| expected 4 got 5") != string::npos, "a failing test shows what it printed", o);
    auto t0 = std::chrono::steady_clock::now();
    o = sh("test test_hang --timeout 1", &rc);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    check(rc == 1 && o.find("FAIL test_hang exit 124") != string::npos && o.find("timed out after 1 s") != string::npos, "a hanging test is killed and reported", o);
    check(secs < 15, "the timeout stopped it long before sleep(30) ended", std::to_string(secs));
}

static void testJunitShardVersions() {
    printf("test: test --junit writes XML, --shard I/N splits the tests, toolchains --versions prints versions\n");
    fresh("tjunit");
    write("ok.cpp", "int main(){return 0;}\n");
    write("bad.cpp", "#include <cstdio>\nint main(){puts(\"x < y & \\\"z\\\"\"); return 3;}\n");
    write("build.okay",
          "target test_a exe\n sources = ok.cpp\n test = on\nend\n"
          "target test_b exe\n sources = bad.cpp\n test = on\nend\n"
          "target test_c exe\n sources = ok.cpp\n test = on\nend\n"
          "target test_d exe\n sources = ok.cpp\n test = on\nend\n");
    int rc; string o = sh("test --junit r.xml -j2", &rc);
    string x = readFile(gRoot / "r.xml");
    check(rc == 1 && x.find("<testsuites tests=\"4\" failures=\"1\">") != string::npos, "junit file counts tests and failures", x);
    check(x.find("<testcase name=\"test_a\" classname=\"okay-build\"/>") != string::npos, "a passing test is an empty testcase", x);
    check(x.find("<failure message=\"exit 3\">x &lt; y &amp; &quot;z&quot;") != string::npos, "a failure carries its escaped output", x);
    o = sh("test --shard 1/2 -j2", &rc);
    check(rc == 0 && o.find("shard 1/2 runs 2 of 4") != string::npos && o.find("PASS test_a") != string::npos && o.find("test_b") == string::npos && o.find("PASS test_c") != string::npos, "shard 1/2 runs the 1st and 3rd sorted tests", o);
    o = sh("test --shard 2/2 -j2", &rc);
    check(rc == 1 && o.find("FAIL test_b") != string::npos && o.find("PASS test_d") != string::npos && o.find("test_a") == string::npos, "shard 2/2 runs the rest", o);
    o = sh("test --shard 3/2", &rc);
    check(rc == 2 && o.find("--shard wants I/N") != string::npos, "a bad shard is an error", o);
    o = sh("test --shard 5/8 -j2", &rc);
    check(rc == 0 && o.find("0 tests, 0 failed") != string::npos, "an empty shard passes", o);
    o = sh("toolchains --versions", &rc);
    check(rc == 0 && (o.find("\tversion=") != string::npos || o.find("found") == string::npos), "found toolchains show a version", o);
}

static void testMsvcVersionChoice() {
    printf("test: several MSVC installs: highest version by number, --msvc-version pins one\n"); fresh("msvcver");
    for (const char* v : {"14.9.1", "14.10.2", "14.2.0"}) {
        string d = string("vs/VC/Tools/MSVC/") + v + "/bin/Hostx64/x64/";
        for (const char* n : {"cl.exe", "link.exe"}) { write(d + n, ""); fs::permissions(gRoot / d / n, fs::perms::owner_all); }
    }
    string root = "--msvc-root '" + (gRoot / "vs").string() + "'";
    int rc; string o = sh("toolchains --versions " + root, &rc);
    check(rc == 0 && o.find("MSVC/14.10.2/") != string::npos && o.find("version=14.10.2") != string::npos, "14.10.2 beats 14.9.1 (numeric, not text, order)", o);
    o = sh("toolchains --versions --msvc-version 14.9 " + root, &rc);
    check(rc == 0 && o.find("version=14.9.1") != string::npos, "a pinned prefix picks that install", o);
    o = sh("toolchains --msvc-version 15 " + root, &rc);
    check(rc == 0 && o.find("msvc\tmissing") != string::npos, "a pin that matches nothing leaves msvc missing", o);
}

static void testToolHelpers() {
    printf("test: okay-build tool copy, hash, pack, unpack, list\n"); fresh("tool");
    write("src/a.txt", "alpha\n"); write("src/sub/b.bin", string("b\0b", 3)); write("src/sub/deep/c.txt", "");
    int rc; string o = sh("tool pack src out/p.okpk", &rc);
    check(rc == 0 && o.find("3 file(s)") != string::npos, "pack reports the files", o);
    string first = readFile(gRoot / "out/p.okpk");
    sh("tool pack src out/p2.okpk");
    check(first == readFile(gRoot / "out/p2.okpk") && first.compare(0, 4, "OKPK") == 0, "packing twice gives identical bytes", "");
    o = sh("tool list out/p.okpk", &rc);
    check(rc == 0 && o.find("\ta.txt") != string::npos && o.find("\tsub/b.bin") != string::npos && o.find("\t3\tsub/b.bin") != string::npos, "list shows size and path", o);
    o = sh("tool unpack out/p.okpk back", &rc);
    check(rc == 0 && readFile(gRoot / "back/a.txt") == "alpha\n" && readFile(gRoot / "back/sub/b.bin") == string("b\0b", 3) && fs::exists(gRoot / "back/sub/deep/c.txt"), "unpack restores the folder", o);
    o = sh("tool pack src src/self.okpk", &rc);
    sh("tool pack src src/self.okpk");
    o = sh("tool list src/self.okpk", &rc);
    check(o.find("self.okpk") == string::npos, "a pack inside its own folder does not contain itself", o);
    { string d = first; d[d.size() - 1] ^= 1; std::ofstream(gRoot / "out/bad.okpk", std::ios::binary) << d; }
    o = sh("tool unpack out/bad.okpk bad", &rc);
    check(rc == 2 && o.find("damaged") != string::npos, "a flipped byte is caught", o);
    { string d = first.substr(0, first.size() - 3); std::ofstream(gRoot / "out/cut.okpk", std::ios::binary) << d; }
    o = sh("tool unpack out/cut.okpk cut", &rc);
    check(rc == 2 && o.find("cut short") != string::npos, "a cut file is caught", o);
    { string d = "OKPK"; d += string("\1\0\0\0\1\0\0\0", 8); d += string("\5\0\0\0../x", 9); d += string("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16); std::ofstream(gRoot / "out/evil.okpk", std::ios::binary) << d; }
    o = sh("tool unpack out/evil.okpk evil", &rc);
    check(rc == 2 && o.find("unsafe path") != string::npos && !fs::exists(gRoot / "x"), "a path that climbs out is refused", o);
    o = sh("tool copy src/a.txt c/d/a.copy", &rc);
    check(rc == 0 && readFile(gRoot / "c/d/a.copy") == "alpha\n" && !fs::exists(gRoot / "c/d/a.copy.partial"), "copy makes folders and leaves no partial", o);
    o = sh("tool hash src/a.txt src/a.txt", &rc);
    check(rc == 0 && o.substr(0, 16) == o.substr(o.find('\n') + 1, 16) && o.find("  src/a.txt") == 16, "hash prints 16 hex digits and the name", o);
    o = sh("tool copy nosuch x", &rc);
    check(rc == 2 && o.find("cannot read") != string::npos, "copy of a missing file fails", o);
    // from an importer: pack a folder as a tracked step
    string tool = gTool;
    write("build.okay", "importer pk\n pattern = src/a.txt\n output = out/{stem}.k\n cmd = '" + tool + "' tool copy {input} {output}\nend\n");
    o = sh("-j1", &rc);
    check(rc == 0 && readFile(gRoot / "out/a.k") == "alpha\n", "an importer can use tool copy", o);
}

static void testCompressedCache() {
    printf("test: --cache-compress stores smaller entries, hits restore the exact object, damage is a miss\n"); fresh("zcache");
    string body; for (int i = 0; i < 400; ++i) body += "static const int v" + std::to_string(i) + " = " + std::to_string(i * 7) + ";\n";
    write("a.cpp", body + "int main(){return v3 - 21;}\n");
    write("build.okay", "target app exe\n sources = a.cpp\nend\n");
    string cd = (gRoot / "zc").string();
    int rc; string o = sh("-j1 --cache-dir '" + cd + "' --cache-compress", &rc);
    check(rc == 0, "build with compression works", o);
    string exe1 = readFile(gRoot / "build/app");
    uint64_t packedBytes = 0, rawBytes = 0;
    for (auto& e : fs::directory_iterator(cd)) if (e.is_directory()) {
        packedBytes += fs::file_size(e.path() / "obj");
        check(readFile(e.path() / "meta").find("\nlz\n") != string::npos, "meta marks the entry as compressed", "");
    }
    for (auto& e : fs::recursive_directory_iterator(gRoot / ".okaybuild/obj")) if (e.is_regular_file() && e.path().extension() == ".o") rawBytes += fs::file_size(e.path());
    check(packedBytes > 0 && packedBytes * 2 < rawBytes, "the compressed object is less than half the raw one", std::to_string(packedBytes) + " vs " + std::to_string(rawBytes));
    fs::remove_all(gRoot / "build"); fs::remove_all(gRoot / ".okaybuild/obj"); fs::remove(gRoot / ".okaybuild/state");
    o = sh("-j1 --cache-dir '" + cd + "'", &rc);
    check(rc == 0 && o.find("cache hit") != string::npos && readFile(gRoot / "build/app") == exe1, "a hit restores a byte-identical program (no flag needed to read)", o);
    // damage: flip one byte in the stored object. It must be a miss that recompiles, never a bad object.
    for (auto& e : fs::directory_iterator(cd)) if (e.is_directory()) { string d = readFile(e.path() / "obj"); d[d.size() / 2] ^= 0x55; std::ofstream(e.path() / "obj", std::ios::binary | std::ios::trunc) << d; }
    fs::remove_all(gRoot / "build"); fs::remove_all(gRoot / ".okaybuild/obj"); fs::remove(gRoot / ".okaybuild/state");
    o = sh("-j1 --cache-dir '" + cd + "' --cache-compress", &rc);
    check(rc == 0 && o.find("cache hit") == string::npos && readFile(gRoot / "build/app") == exe1, "a damaged entry is a miss and the rebuild is identical", o);
    // empty and incompressible inputs through the importer-less path: round trip via a source with random-ish bytes
    string noise; unsigned x = 12345; for (int i = 0; i < 3000; ++i) { x = x * 1103515245u + 12345u; noise += "0x" + std::to_string(x >> 8) + ","; }
    write("b.cpp", "const unsigned long long t[] = {" + noise + "0};\nint main(){return t[0] != 0 ? 0 : 1;}\n");
    write("build.okay", "target app exe\n sources = b.cpp\nend\n");
    o = sh("-j1 --cache-dir '" + cd + "' --cache-compress", &rc);
    string exe2 = readFile(gRoot / "build/app");
    fs::remove_all(gRoot / "build"); fs::remove_all(gRoot / ".okaybuild/obj"); fs::remove(gRoot / ".okaybuild/state");
    o = sh("-j1 --cache-dir '" + cd + "'", &rc);
    check(rc == 0 && o.find("cache hit") != string::npos && readFile(gRoot / "build/app") == exe2, "noisy data survives a compressed round trip", o);
}

static void testLzFormat() {
    printf("test: OKLZ compression round-trips edge cases and refuses damage\n"); fresh("lz");
    string noise; unsigned x = 7; for (int i = 0; i < 70000; ++i) { x = x * 1103515245u + 12345u; noise += (char)(x >> 16); }
    string mixed; for (int i = 0; i < 300; ++i) mixed += "abcabcabc" + std::to_string(i % 17) + string(i % 40, 'q');
    struct C { const char* name; string data; } cases[] = {{"empty", ""}, {"one", "x"}, {"three", "abc"}, {"four", "aaaa"}, {"zeros", string(1 << 20, '\0')},
        {"noise", noise}, {"mixed", mixed}, {"far", noise.substr(0, 60000) + noise.substr(0, 60000)}};
    for (auto& c : cases) {
        std::ofstream(gRoot / "in", std::ios::binary | std::ios::trunc) << c.data;
        int rc1, rc2; sh(string("tool z in z.bin"), &rc1); sh("tool unz z.bin out", &rc2);
        check(rc1 == 0 && rc2 == 0 && readFile(gRoot / "out") == c.data, string("round trip: ") + c.name, "");
        if (string(c.name) == "zeros") check(fs::file_size(gRoot / "z.bin") < 8000, "a megabyte of zeros packs small", std::to_string(fs::file_size(gRoot / "z.bin")));
    }
    // every truncation and a spread of single-byte flips of a real block must be refused or (only if the flip hits nothing that matters) never crash
    std::ofstream(gRoot / "in", std::ios::binary | std::ios::trunc) << mixed; sh("tool z in z.bin");
    string z = readFile(gRoot / "z.bin"); int refused = 0, tried = 0;
    for (size_t cut = 0; cut < z.size(); cut += z.size() / 40 + 1) { std::ofstream(gRoot / "t.bin", std::ios::binary | std::ios::trunc) << z.substr(0, cut); int rc; sh("tool unz t.bin o2", &rc); ++tried; if (rc == 2) ++refused; }
    check(refused == tried, "every truncated block is refused", std::to_string(refused) + "/" + std::to_string(tried));
    refused = tried = 0;
    for (size_t at = 0; at < z.size(); at += z.size() / 40 + 1) { string d = z; d[at] ^= 0x20; std::ofstream(gRoot / "t.bin", std::ios::binary | std::ios::trunc) << d; int rc; sh("tool unz t.bin o2", &rc); ++tried; if (rc == 2 || (rc == 0 && readFile(gRoot / "o2") == mixed)) ++refused; } // a flipped distance inside a repeated run can legitimately give the same bytes
    check(refused == tried, "a flipped byte is refused or still gives the exact original (never wrong bytes, never a crash)", std::to_string(refused) + "/" + std::to_string(tried));
}

static void testQuotedDefine() {
    printf("test: a define may hold a double quote when written as \\\"\n"); fresh("qdef");
    write("a.cpp", "#include <cstdio>\nint main(){puts(GREETING); return 0;}\n");
    write("build.okay", "target app exe\n sources = a.cpp\n define = \"GREETING=\\\"hello world\\\"\"\nend\n");
    int rc; string o = sh("-j1", &rc);
    check(rc == 0, "a define with escaped quotes compiles", o);
    o = capture("'" + (gRoot / "build/app").string() + "'", &rc);
    check(o == "hello world\n", "the program sees the string", o);
    write("build.okay", "target app exe\n sources = a.cpp\n define = GREETING=\\\"hi\\\"\nend\n");
    o = sh("-j1", &rc);
    check(rc == 0 && ran(o) >= 1, "changing it rebuilds", o);
}

static void testMeasuredMemory() {
    printf("test: --memory-mb uses each step's measured peak from the last run\n"); fresh("memmeas");
    write("hog.cpp", "#include <cstdio>\n#include <cstdlib>\n#include <cstring>\n#include <fcntl.h>\n#include <unistd.h>\n"
        "static void note(const char* s){ int fd = open(\"log.txt\", O_WRONLY|O_APPEND|O_CREAT, 0644); if (write(fd, s, 2) < 0) {} close(fd); }\n"
        "int main(int c, char** v){ size_t mb = (size_t)atoi(v[1]); char* p = (char*)malloc(mb << 20); memset(p, 1, mb << 20); note(\"s\\n\"); usleep(400000); note(\"e\\n\");\n"
        " FILE* f = fopen(v[2], \"w\"); fputs(p[100] == 1 ? \"x\" : \"y\", f); fclose(f); return 0; }\n");
    int rc0 = system(("cd '" + gRoot.string() + "' && g++ -O0 hog.cpp -o hog").c_str()); check(rc0 == 0, "hog compiles");
    string man; for (int i = 0; i < 3; ++i) man += "action\n  cmd = ./hog 150 o" + std::to_string(i) + "\n  output = o" + std::to_string(i) + "\nend\n";
    write("build.okay", man);
    auto maxConc = [&]() { string l = readFile(gRoot / "log.txt"); int cur = 0, mx = 0; for (size_t i = 0; i + 1 < l.size(); i += 2) { cur += l[i] == 's' ? 1 : -1; mx = std::max(mx, cur); } return mx; };
    int rc; string o = sh("--memory-mb 400 -j3", &rc);
    check(rc == 0 && maxConc() == 3, "first run has nothing measured, so all three overlap", o + "\nconc=" + std::to_string(maxConc()));
    string mem = readFile(gRoot / ".okaybuild/memory");
    long kb = 0; { auto at = mem.find("o0\t"); if (at != string::npos) kb = atol(mem.c_str() + at + 3); }
    check(mem.compare(0, 18, "okaybuild-memory 1") == 0 && kb >= 150 * 1024 && kb < 400 * 1024, "the measured peak of o0 is saved and near 150 MiB", mem);
    for (int i = 0; i < 3; ++i) fs::remove(gRoot / ("o" + std::to_string(i)));
    fs::remove(gRoot / "log.txt");
    o = sh("--memory-mb 400 -j3", &rc);
    check(rc == 0 && maxConc() == 2, "second run: 3 x ~150 MiB does not fit in 400, so at most two overlap", o + "\nconc=" + std::to_string(maxConc()));
    for (int i = 0; i < 3; ++i) fs::remove(gRoot / ("o" + std::to_string(i)));
    fs::remove(gRoot / "log.txt");
    o = sh("-j3", &rc);
    check(rc == 0 && maxConc() == 3, "without --memory-mb the measurement is not used", o);
    o = sh("--memory-mb 0", &rc);
    check(rc == 2 && o.find("--memory-mb wants") != string::npos, "a bad value is an error", o);
}

static void testPathMap() {
    printf("test: toolchain path_map = on makes objects independent of where the tree sits\n");
    auto mk = [&](const string& name, bool on) {
        fresh(name);
        write("inc/h.hpp", "#pragma once\ninline const char* hfile(){ return __FILE__; }\n");
        write("a.cpp", "#include \"h.hpp\"\n#include <cstdio>\nint main(){ printf(\"%s %s\\n\", __FILE__, hfile()); return 0; }\n");
        write("build.okay", string("toolchain\n") + (on ? " path_map = on\n" : "") + "end\ntarget app exe\n sources = a.cpp\n include = inc\nend\n");
    };
    mk("pmA", true); fs::path a = gRoot;
    int rc; string o = sh("-j1", &rc); check(rc == 0, "tree A builds", o);
    string objA = readFile(a / ".okaybuild/obj/app/a.cpp.o");
    o = capture("'" + (a / "build/app").string() + "'", &rc);
    check(o == "/okay-src/a.cpp /okay-src/inc/h.hpp\n", "__FILE__ is the mapped absolute spelling in the .cpp and the header", o);
    mk("pmB", true); fs::path b = gRoot; string cd = (a.parent_path() / "okaybuild_test_pmcache").string(); fs::remove_all(cd);
    sh("-j1 --cache-dir '" + cd + "' --no-stat", &rc);
    check(readFile(b / ".okaybuild/obj/app/a.cpp.o") == objA, "the same sources in another folder give a byte-identical object", "");
    gRoot = a; fs::remove_all(a / "build"); fs::remove_all(a / ".okaybuild");
    sh("-j1 --cache-dir '" + cd + "'", &rc); gRoot = b; fs::remove_all(b / ".okaybuild"); fs::remove_all(b / "build");
    o = sh("-j1 --cache-dir '" + cd + "'", &rc);
    check(rc == 0 && o.find("cache hit") != string::npos, "the second folder hits the first folder's cache entry", o);
    fs::remove_all(cd);
    mk("pmOff", false);
    sh("-j1", &rc); o = capture("'" + (gRoot / "build/app").string() + "'", &rc);
    check(o == "a.cpp inc/h.hpp\n", "without path_map the relative spelling stays", o);
}

static int countEntries(const fs::path& d) { int n = 0; std::error_code ec; if (!fs::is_directory(d, ec)) return -1; for (auto& e : fs::directory_iterator(d)) if (e.is_directory() && e.path().filename().string().find(".tmp") == string::npos) ++n; return n; }
static void testRemoteCacheFolder() {
    printf("test: --remote-cache DIR shares cache entries through a folder (same keys), pull and push, read-only, damage\n");
    string base = (fs::temp_directory_path() / "okaybuild_test_remote").string(); fs::remove_all(base);
    string R = base + "/shared", cA = base + "/cacheA", cB = base + "/cacheB", cC = base + "/cacheC";
    auto tree = [&](const string& name, const string& body) { fresh(name); write("a.cpp", body); write("build.okay", "target app exe\n sources = a.cpp\nend\n"); };
    tree("rcA", "int main(){return 3;}\n");
    int rc; string o = sh("-j1 --cache-dir '" + cA + "'", &rc);
    check(rc == 0 && countEntries(R) == -1, "without --remote-cache nothing is written to the shared folder", o);
    fs::remove_all(cA); fs::remove_all(gRoot / ".okaybuild"); fs::remove_all(gRoot / "build");
    o = sh("-j1 --cache-dir '" + cA + "' --remote-cache '" + R + "'", &rc);
    check(rc == 0 && countEntries(R) == 1, "a miss compiles and publishes one entry to the shared folder", o + "\n" + std::to_string(countEntries(R)));
    string exeA = readFile(gRoot / "build/app");
    tree("rcB", "int main(){return 3;}\n");
    o = sh("-j1 --cache-dir '" + cB + "' --remote-cache '" + R + "' -v", &rc);
    check(rc == 0 && o.find("cache hit") != string::npos && o.find("remote") != string::npos, "another tree with an empty local cache gets a remote hit", o);
    check(readFile(gRoot / "build/app") == exeA, "the program from the remote hit is identical", "");
    check(countEntries(cB) == 1, "the entry is pulled into the local cache too", std::to_string(countEntries(cB)));
    tree("rcC", "int main(){return 4;}\n");
    o = sh("-j1 --cache-dir '" + cC + "' --remote-cache '" + R + "' --remote-cache-readonly", &rc);
    check(rc == 0 && countEntries(R) == 1 && countEntries(cC) == 1, "read-only: compiles, keeps a local entry, publishes nothing", o);
    // damage the remote entry: it must be ignored (a miss), the build still works, and a normal push repairs it
    string key; for (auto& e : fs::directory_iterator(R)) if (e.is_directory()) key = e.path().filename().string();
    { std::ofstream(fs::path(R) / key / "obj", std::ios::binary | std::ios::trunc) << "x"; }
    tree("rcD", "int main(){return 3;}\n");
    o = sh("-j1 --cache-dir '" + base + "/cacheD' --remote-cache '" + R + "'", &rc);
    check(rc == 0 && o.find("cache hit") == string::npos && readFile(gRoot / "build/app") == exeA, "a damaged remote entry is a miss and the rebuild is identical", o);
    tree("rcE", "int main(){return 3;}\n");
    o = sh("-j1 --cache-dir '" + base + "/cacheE' --remote-cache '" + R + "' -v", &rc);
    check(rc == 0 && o.find("cache hit") != string::npos, "the damaged entry was replaced by the push", o);
    // environment variable and an unusable folder
    tree("rcF", "int main(){return 3;}\n");
    setenv("OKAY_REMOTE_CACHE", R.c_str(), 1);
    o = sh("-j1 --cache-dir '" + base + "/cacheF' -v", &rc); unsetenv("OKAY_REMOTE_CACHE");
    check(rc == 0 && o.find("remote") != string::npos, "OKAY_REMOTE_CACHE works like the option", o);
    tree("rcG", "int main(){return 3;}\n");
    write("notafolder", "x");
    o = sh("-j1 --cache-dir '" + base + "/cacheG' --remote-cache '" + (gRoot / "notafolder").string() + "'", &rc);
    check(rc == 0 && o.find("remote cache") != string::npos, "an unusable shared folder is a warning, not a failure", o);
    fs::remove_all(base);
}

static void testImporters() {
    printf("test: importer blocks rebuild only changed assets, settings change rebuilds all, failures leave no output\n"); fresh("imp");
    write("tools/conv.sh", "echo \"$1\" >> imports.log\nif grep -q FAIL \"$1\"; then echo half > \"$2\"; exit 1; fi\n{ printf '%s|' \"$3\"; cat \"$1\"; } > \"$2\"\n");
    auto manifest = [&](const string& settings) {
        write("build.okay", "importer text\n pattern = assets/*.txt\n output = out/{stem}.bin\n cmd = sh tools/conv.sh {input} {output} {settings}\n settings = " + settings + "\n inputs = tools/conv.sh\nend\n");
    };
    write("assets/a.txt", "AAA\n"); write("assets/b.txt", "BBB\n"); write("assets/c.txt", "CCC\n"); write("assets/skip.png", "x\n");
    manifest("q1");
    int rc; string o = sh("-j2", &rc);
    check(rc == 0 && ran(o) == 3, "first build imports the 3 .txt files only", o);
    check(readFile(gRoot / "out/b.bin") == "q1|BBB\n", "importer output has settings and content", readFile(gRoot / "out/b.bin"));
    check(!fs::exists(gRoot / "out/skip.bin") && !fs::exists(gRoot / "out/b.partial.bin"), "non-matching file ignored, no partial file left");
    o = sh("--explain", &rc); check(rc == 0 && ran(o) == 0, "no-op imports nothing", o);
    fs::remove(gRoot / "imports.log");
    write("assets/b.txt", "BBB2\n"); o = sh("--explain", &rc);
    check(ran(o) == 1 && o.find("assets/b.txt changed") != string::npos, "only the changed asset is rebuilt", o);
    check(readFile(gRoot / "imports.log") == "assets/b.txt\n", "importer command ran for b only", readFile(gRoot / "imports.log"));
    fs::remove(gRoot / "imports.log"); manifest("q2"); o = sh("--explain");
    check(ran(o) == 3 && o.find("command line changed") != string::npos, "changed importer settings rebuild all", o);
    check(readFile(gRoot / "out/a.bin") == "q2|AAA\n", "new settings applied");
    fs::remove(gRoot / "imports.log"); fs::remove(gRoot / "out/a.bin"); o = sh("--explain");
    check(ran(o) == 1 && o.find("out/a.bin is missing") != string::npos, "a deleted output rebuilds only it", o);
    fs::remove(gRoot / "imports.log"); write("assets/d.txt", "DDD\n"); o = sh();
    check(ran(o) == 1 && fs::exists(gRoot / "out/d.bin"), "a new asset is picked up", o);
    write("tools/conv.sh", "echo \"$1\" >> imports.log\n{ printf '%s|' \"$3\"; cat \"$1\"; } > \"$2\"\n"); o = sh("--explain");
    check(ran(o) == 4 && o.find("tools/conv.sh changed") != string::npos, "a changed importer tool rebuilds everything", o);
    // a failing importer leaves nothing behind, and a later fix works
    write("tools/conv.sh", "if grep -q FAIL \"$1\"; then echo half > \"$2\"; exit 1; fi\n{ printf '%s|' \"$3\"; cat \"$1\"; } > \"$2\"\n");
    write("assets/c.txt", "FAIL\n"); o = sh("-j1", &rc);
    check(rc == 1 && o.find("FAILED") != string::npos, "failing importer fails the build", o);
    check(!fs::exists(gRoot / "out/c.bin") && !fs::exists(gRoot / "out/c.partial.bin"), "no half-written output after a failure");
    write("assets/c.txt", "CCC3\n"); o = sh("", &rc);
    check(rc == 0 && readFile(gRoot / "out/c.bin") == "q2|CCC3\n", "fixing the asset builds it", o);
    // recursive pattern, {dir}, depfile-free; unknown keys and missing fields are errors
    fresh("imp2"); write("tools/conv.sh", "cat \"$1\" > \"$2\"\n");
    write("assets/x/y.dat", "Y\n"); write("assets/z.dat", "Z\n");
    write("build.okay", "importer d\n pattern = assets/**/*.dat\n output = out/{dir}/{name}.o\n cmd = sh tools/conv.sh {input} {output}\nend\n");
    o = sh("-j2", &rc); check(rc == 0 && readFile(gRoot / "out/x/y.dat.o") == "Y\n" && readFile(gRoot / "out/z.dat.o") == "Z\n", "** walks sub directories and {dir} keeps them", o);
    o = sh("targets", &rc); check(o.find("out/x/y.dat.o") != string::npos || rc == 0, "targets lists without crashing", o);
    write("build.okay", "importer d\n pattern = a/*.x\n cmd = true\nend\n");
    o = sh("", &rc); check(rc == 2 && o.find("has no output") != string::npos, "importer without output is an error", o);
    write("build.okay", "importer d\n pattern = a/*.x\n output = same.bin\n cmd = true\nend\n");
    o = sh("", &rc); check(rc == 2 && o.find("every file would write the same output") != string::npos, "one output for every file is an error", o);
}
static int countOf(const string& hay, const string& needle) { int n = 0; for (size_t p = 0; (p = hay.find(needle, p)) != string::npos; p += needle.size()) ++n; return n; }
static void testPerConfigDirsAndTrace() {
    printf("test: outdir with {config} keeps each configuration apart, and --trace writes Chrome trace JSON\n"); fresh("cfgdir"); basicProject();
    write("build.okay", "outdir = build/{config}\nconfig debug\n optimize = 0\nend\nconfig release\n optimize = 2\nend\n"
          "target util lib\n sources = src/util.cpp\n export_include = inc\nend\ntarget app exe\n sources = src/main.cpp\n deps = util\nend\n");
    int rc; string o = sh("-j2 --config debug", &rc);
    check(rc == 0 && ran(o) == 4 && fs::exists(gRoot / "build/debug/app") && !fs::exists(gRoot / "build/release"), "debug builds into build/debug only", o);
    check(fs::exists(gRoot / ".okaybuild/obj/debug/util/src/util.cpp.o"), "objects live under .okaybuild/obj/debug");
    o = sh("-j2 --config release", &rc);
    check(rc == 0 && ran(o) == 4 && fs::exists(gRoot / "build/release/app") && fs::exists(gRoot / "build/debug/app"), "release builds beside it and leaves debug alone", o);
    o = sh("--config debug", &rc); check(ran(o) == 0, "going back to debug rebuilds nothing", o);
    o = sh("", &rc); check(ran(o) == 0, "no --config uses the first configuration (debug), still up to date", o);
    o = sh("clean --config release", &rc); check(!fs::exists(gRoot / "build/release/app") && fs::exists(gRoot / "build/debug/app"), "clean --config release removes only that program folder", o);
    fresh("nocfg"); basicProject();
    write("build.okay", "outdir = out/{config}\ntarget util lib\n sources = src/util.cpp\n export_include = inc\nend\ntarget app exe\n sources = src/main.cpp\n deps = util\nend\n");
    o = sh("", &rc); check(rc == 0 && fs::exists(gRoot / "out/default/app"), "without any config block the folder is named default", o);
    // trace
    fresh("trace"); basicProject();
    o = sh("-j2 --trace out/trace.json", &rc);
    string t = readFile(gRoot / "out/trace.json");
    check(rc == 0 && t.find("\"traceEvents\"") != string::npos && countOf(t, "\"ph\":\"X\"") == 4, "trace has one complete event per step", t);
    check(t.find("\"cat\":\"CC\"") != string::npos && t.find("\"cat\":\"LD\"") != string::npos && t.find("src/util.cpp") != string::npos, "events carry kind and step name", t);
    check(t.find("\"dur\":") != string::npos && t.find("\"ts\":") != string::npos && t.find("\"tid\":1") != string::npos, "events have ts, dur and a worker row", t);
    check(!fs::exists(gRoot / "out/trace.json.tmp") && countOf(t, "{") == countOf(t, "}") && countOf(t, "[") == countOf(t, "]"), "no temp file, braces balance");
    o = sh("--trace out/noop.json", &rc); t = readFile(gRoot / "out/noop.json");
    check(ran(o) == 0 && t.find("\"traceEvents\"") != string::npos && countOf(t, "\"ph\":\"X\"") == 0, "a no-op run writes a trace with no steps", t);
    write("src/main.cpp", "#include \"nope.h\"\nint main(){}\n"); o = sh("-j1 --trace out/fail.json", &rc);
    t = readFile(gRoot / "out/fail.json"); check(rc == 1 && countOf(t, "\"ph\":\"X\"") >= 1, "a failed build still writes its trace", o + t);
    // importer outputs can be waited on with a target's needs
    fresh("impneeds"); write("assets/a.txt", "1\n"); write("tools/c.sh", "cat \"$1\" > \"$2\"\n"); write("src/m.cpp", "int main(){return 0;}\n");
    write("build.okay", "importer t\n pattern = assets/*.txt\n output = out/{stem}.bin\n cmd = sh tools/c.sh {input} {output}\nend\ntarget m exe\n sources = src/m.cpp\n needs = out/a.bin\nend\n");
    o = sh("-j2 m", &rc); check(rc == 0 && fs::exists(gRoot / "out/a.bin") && fs::exists(gRoot / "build/m"), "a target can need an importer output", o);
}

// ---- the CMake reader (`okay-build cmake`) and `okay-build ctest`
static void cmakeProject() {
    write("CMakeLists.txt",
        "cmake_minimum_required(VERSION 3.16)\n"
        "project(Demo VERSION 1.2.3 LANGUAGES CXX)\n"
        "set(CMAKE_CXX_STANDARD 17)\n"
        "option(DEMO_FANCY \"fancy\" OFF)\n"
        "option(DEMO_BREAK \"stop\" OFF)\n"
        "if(DEMO_BREAK)\n  message(FATAL_ERROR \"asked to stop\")\nendif()\n"
        "add_compile_options(-Wall)\n"
        "set(DEMO_VERSION_DEFINE DEMO_VERSION=\"${PROJECT_VERSION}\")   # a quote inside an unquoted word\n"
        "if(DEMO_FANCY)\n  set(_extra src/fancy.cpp)\nelse()\n  set(_extra \"\")\nendif()\n"
        "add_library(core STATIC src/core.cpp src/c_part.c ${_extra})\n"
        "target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)\n"
        "target_compile_definitions(core PUBLIC CORE_PUBLIC=1 PRIVATE CORE_PRIV=2)\n"
        "add_library(demo::core ALIAS core)\n"
        "add_library(viaiface INTERFACE)\n"
        "target_compile_definitions(viaiface INTERFACE VIA_IFACE=3)\n"
        "set_source_files_properties(src/core.cpp PROPERTIES COMPILE_DEFINITIONS \"${DEMO_VERSION_DEFINE}\")\n"
        "list(APPEND _l a b c)\nlist(LENGTH _l _n)\nstring(REGEX REPLACE \"^D(.*)$\" \"\\\\1\" _x \"Demo\")\n"
        "set(_good 0)\nif(\"${_x}\" STREQUAL \"emo\" AND _n EQUAL 3 AND NOT DEFINED NOPE AND (DEMO_FANCY OR NOT DEMO_FANCY) AND \"x${_n}\" MATCHES \"^x[0-9]$\")\n  set(_good 1)\nendif()\n"
        "add_executable(app src/main.cpp)\n"
        "target_link_libraries(app PRIVATE demo::core viaiface)\nset_target_properties(app PROPERTIES OUTPUT_NAME renamed_app)\n"
        "target_compile_definitions(app PRIVATE CHECK_GOOD=${_good})\n"
        "add_library(extra INTERFACE)\ntarget_include_directories(extra INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/incx)\n"
        "find_path(_sysinc stdio.h)\ntarget_include_directories(app PRIVATE ${_sysinc} \"$<TARGET_PROPERTY:extra,INTERFACE_INCLUDE_DIRECTORIES>\")\n"
        "target_compile_definitions(app PRIVATE OUT_DIR=\"${CMAKE_CURRENT_BINARY_DIR}/x\")\n"
        "add_executable(ghost src/ghost.cpp)\n"
        "enable_testing()\n"
        "add_subdirectory(tests)\n");
    write("include/core.h", "#pragma once\nconst char* core_version();\nint core_pub();\n");
    write("src/core.cpp", "#include \"core.h\"\n#ifndef CORE_PRIV\n#error private define missing\n#endif\nextern \"C\" int c_part();\nconst char* core_version(){return DEMO_VERSION;}\nint core_pub(){return CORE_PRIV + c_part() - 1;}\n");
    write("incx/extra.h", "#pragma once\n#define FROM_EXTRA 4\n");
    write("src/c_part.c", "int c_part(void){return 1;}\n");
    write("src/fancy.cpp", "int fancy(){return 9;}\n");
    write("src/main.cpp", "#include \"core.h\"\n#include <extra.h>\n#include <cstring>\n#if FROM_EXTRA != 4\n#error include folder from TARGET_PROPERTY missing\n#endif\n#if CHECK_GOOD != 1\n#error the if() conditions were evaluated wrongly\n#endif\n#if VIA_IFACE != 3 || CORE_PUBLIC != 1\n#error interface or public definitions did not arrive\n#endif\nint main(){return std::strcmp(core_version(), \"1.2.3\") == 0 && core_pub() == 2 && std::strstr(OUT_DIR, \"/ctest/x\") != nullptr ? 0 : 1;}\n");
    write("tests/CMakeLists.txt",
        "function(demo_test name)\n  add_executable(test_${name} test_${name}.cpp)\n  target_link_libraries(test_${name} PRIVATE demo::core)\n  add_test(NAME ${name} COMMAND test_${name} ${ARGN})\nendfunction()\n"
        "demo_test(ok)\ndemo_test(args one two)\ndemo_test(cwd)\ndemo_test(fail)\nset_tests_properties(fail PROPERTIES WILL_FAIL TRUE)\n"
        "demo_test(skip)\nset_tests_properties(skip PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)\n"
        "add_test(NAME shell COMMAND sh -c \"exit 0\")\n"
        "foreach(_n a b)\n  add_test(NAME loop_${_n} COMMAND sh -c \"test ${_n} = ${_n}\")\nendforeach()\n"
        "add_test(NAME really_fails COMMAND sh -c \"echo boom; exit 3\")\n"
        "add_test(NAME by_path COMMAND $<TARGET_FILE:test_ok>)\nadd_test(NAME app_runs COMMAND app)\n"
        "add_test(NAME env_ok COMMAND sh -c \"test \\\"$DEMO_VAR\\\" = hello\")\nset_tests_properties(env_ok PROPERTIES ENVIRONMENT \"DEMO_VAR=hello\" LABELS \"fast;env\")\n"
        "add_test(NAME first_step COMMAND sh -c \"echo one > order.txt\")\nadd_test(NAME second_step COMMAND sh -c \"grep -q one order.txt && echo two >> order.txt\")\n"
        "set_tests_properties(second_step PROPERTIES DEPENDS first_step LABELS slowish)\n"
        "add_test(NAME slow COMMAND sh -c \"sleep 5\")\nset_tests_properties(slow PROPERTIES TIMEOUT 1)\n");
    write("tests/test_ok.cpp", "#include \"core.h\"\n#include <cstring>\nint main(){return std::strcmp(core_version(), \"1.2.3\") == 0 ? 0 : 1;}\n");
    write("tests/test_args.cpp", "#include <cstring>\nint main(int c, char** v){return c == 3 && !std::strcmp(v[1], \"one\") && !std::strcmp(v[2], \"two\") ? 0 : 1;}\n");
    write("tests/test_cwd.cpp", "#include <cstdio>\nint main(){FILE* f = std::fopen(\"cwd_marker\", \"w\"); if (!f) return 1; std::fclose(f); return 0;}\n");
    write("tests/test_fail.cpp", "int main(){return 1;}\n");
    write("tests/test_skip.cpp", "int main(){return 77;}\n");
}
static void testCmakeReader() {
    printf("test: okay-build cmake reads CMakeLists.txt itself; ctest runs the add_test lines\n");
    fresh("cmakeread"); cmakeProject();
    int rc; string o = sh("cmake . -o build.okay --audit", &rc);
    check(rc == 0 && o.find("written") != string::npos, "cmake writes a manifest", o);
    string man = readFile(gRoot / "build.okay");
    check(man.find("target core lib") != string::npos && man.find("target app exe") != string::npos && man.find("target test_args exe") != string::npos, "libraries and programs (also from a function in a subfolder)", man);
    check(man.find("target demo::core") == string::npos && man.find("target viaiface") == string::npos, "an ALIAS and an INTERFACE library are not targets of their own", man);
    check(man.find("file_define = src/core.cpp \"DEMO_VERSION=\\\"1.2.3\\\"\"") != string::npos, "a per-file definition with quotes is written", man);
    check(man.find("-Wall") != string::npos && man.find("-std=gnu++17") != string::npos, "directory options and the language standard", man);
    check(man.find(" /usr/include ") == string::npos && man.find(" /usr/include\n") == string::npos, "the compiler's own include folder is not passed as -I", man);
    check(man.find("{bin}/x") != string::npos, "CMAKE_CURRENT_BINARY_DIR reads {bin}", man);
    check(man.find("fancy.cpp") == string::npos, "an option that is off leaves its source out", man);
    check(o.find("skipped target   ghost: missing source src/ghost.cpp") != string::npos, "audit names the target it could not write", o);
    check(man.find("target ghost") == string::npos, "a target with a missing source is not written", man);
    check(o.find("tests declared") != string::npos && o.find("options           2 declared") != string::npos, "audit counts tests and options", o);
    o = sh("cmake . -o fancy.okay --opt DEMO_FANCY=ON", &rc); string fm = readFile(gRoot / "fancy.okay");
    check(rc == 0 && fm.find("src/fancy.cpp") != string::npos, "--opt NAME=VALUE changes an option the way -D does", fm);
    o = sh("cmake . -o stop.okay --opt DEMO_BREAK=ON", &rc);
    check(rc == 2 && o.find("asked to stop") != string::npos, "message(FATAL_ERROR) stops the read with its text", o);
    o = sh("cmake . -o rel.okay --build-type Release", &rc); string rm = readFile(gRoot / "rel.okay");
    check(rm.find("-O3") != string::npos && rm.find("NDEBUG") != string::npos, "--build-type Release adds -O3 and NDEBUG", rm);
    sh("-f build.okay compdb");
    { string cdb = readFile(gRoot / "compile_commands.json");
      auto at = cdb.find("\"file\": \"src/c_part.c\""); auto e = at == string::npos ? at : cdb.find('}', at);
      string row = at == string::npos ? "" : cdb.substr(at, e - at);
      check(at != string::npos && row.find("-std=") == string::npos && row.find("-Wall") != string::npos, "a C file does not get the C++ standard flag but keeps the other flags", row); }
    // build and run the tests
    o = sh("-f build.okay -j2 app", &rc); check(rc == 0, "the generated manifest builds the program (checks defines and if() at compile time)", o);
    check(system(("'" + (gRoot / "build/renamed_app").string() + "'").c_str()) == 0, "the program runs under its OUTPUT_NAME");
    o = sh("-f build.okay ctest -j2 '^(ok|args|cwd|fail|skip|shell|loop_.|by_path|app_runs)$'", &rc);
    check(rc == 0, "ctest passes the passing tests", o);
    check(o.find("PASS ok") != string::npos && o.find("PASS args") != string::npos && o.find("PASS loop_a") != string::npos && o.find("PASS by_path") != string::npos, "PASS lines", o);
    check(o.find("PASS app_runs") != string::npos, "a test that names a program by its target runs the renamed file", o);
    check(o.find("PASS fail") != string::npos, "WILL_FAIL: a failing command is a pass", o);
    check(o.find("SKIP skip") != string::npos && o.find("1 skipped") != string::npos, "SKIP_RETURN_CODE gives a skip", o);
    check(fs::exists(gRoot / "build/ctest/tests/cwd_marker"), "the default working folder is <build>/ctest/<folder>", o);
    o = sh("-f build.okay ctest -R 'env_ok|step' -j4 --junit out/j.xml", &rc);
    check(rc == 0 && o.find("PASS env_ok") != string::npos && o.find("PASS second_step") != string::npos, "ENVIRONMENT reaches the test and DEPENDS orders two tests even with -j4", o);
    { string x = readFile(gRoot / "out/j.xml"); check(x.find("<testsuite name=\"ctest\" tests=\"3\" failures=\"0\"") != string::npos && x.find("name=\"first_step\"") != string::npos && x.find("time=\"") != string::npos, "--junit writes one testcase per ctest with its time", x); }
    o = sh("-f build.okay ctest -R step -E second", &rc); check(rc == 0 && o.find("1 tests") != string::npos && o.find("first_step") != string::npos && o.find("second_step") == string::npos, "-E drops what matches", o);
    o = sh("-f build.okay ctest -L slowish", &rc); check(rc == 0 && o.find("1 tests") != string::npos && o.find("second_step") != string::npos, "-L picks tests by label", o);
    o = sh("-f build.okay ctest -L fast -L slowish", &rc); check(rc == 0 && o.find("2 tests") != string::npos, "-L may be given twice (either label)", o);
    write("loop.okay", "ctest a\n cmd = true\n depends = b\nend\nctest b\n cmd = true\n depends = a\nend\n"); o = sh("-f loop.okay ctest", &rc);
    check(rc == 2 && o.find("depend on each other") != string::npos, "a DEPENDS loop is an error, not a hang", o);
    write("fail.okay", "ctest f\n cmd = sh -c \"echo bad; exit 2\"\nend\nctest s\n cmd = sh -c \"exit 77\"\n skip_code = 77\nend\n"); sh("-f fail.okay ctest --junit out/k.xml");
    { string x = readFile(gRoot / "out/k.xml"); check(x.find("<failure message=\"exit 2\">") != string::npos && x.find("bad") != string::npos && x.find("<skipped") != string::npos, "junit has failure text and skipped", x); }
    o = sh("-f build.okay ctest really_fails", &rc);
    check(rc == 1 && o.find("FAIL really_fails exit 3") != string::npos && o.find("| boom") != string::npos, "a failing test shows exit code and output", o);
    o = sh("-f build.okay ctest slow", &rc);
    check(rc == 1 && o.find("timed out after 1 s") != string::npos, "TIMEOUT kills a test that runs too long", o);
    o = sh("-f build.okay ctest nomatchhere", &rc);
    check(rc == 2 && o.find("no ctest matches") != string::npos, "a filter that matches nothing is an error", o);
    // ctest block errors
    write("bad.okay", "ctest t\n  cmd = true\n  bogus = 1\nend\n"); o = sh("-f bad.okay ctest", &rc);
    check(rc == 2 && o.find("unknown ctest key") != string::npos, "unknown ctest key is an error", o);
    write("bad2.okay", "target a exe\n sources = src/main.cpp\n file_flag = src/main.cpp\nend\n"); o = sh("-f bad2.okay graph", &rc);
    check(rc == 2 && o.find("file_flag takes a source path") != string::npos, "file_flag needs words", o);
    // a manifest with file_flag / file_define / file_include by hand: only that file gets them
    write("h/x.h", "#pragma once\n#define FROM_H 5\n");
    write("one.cpp", "#include <x.h>\n#ifndef ONLY_ONE\n#error per-file define missing\n#endif\nint one(){return FROM_H;}\n");
    write("two.cpp", "#ifdef ONLY_ONE\n#error per-file define leaked\n#endif\nint two(){return 2;}\n");
    write("m.cpp", "int one(); int two(); int main(){return one()+two()==7?0:1;}\n");
    write("pf.okay", "target m exe\n sources = one.cpp two.cpp m.cpp\n file_define = one.cpp ONLY_ONE=1\n file_include = one.cpp h\n file_flag = two.cpp -Wall -Wextra\nend\n");
    o = sh("-f pf.okay -j2", &rc); check(rc == 0, "per-file define and include apply to that file only", o);
}

static void testCmakeSyntax() {
    printf("test: okay-build cmake: comments, bracket arguments, macros, functions, lists, conditions\n");
    fresh("cmakesyntax");
    write("CMakeLists.txt",
        "# a line comment\n#[[ a block\ncomment (with parens) ]]\nproject(S VERSION 4.5)\n"
        "set(RAW [=[raw \"text\" ${NOT_EXPANDED} ; stays]=])\n"
        "macro(m1 x)\n  set(OUT_${x} yes)\nendmacro()\nm1(foo)\n"
        "function(f1 v)\n  set(INNER local)\n  set(${v} made PARENT_SCOPE)\nendfunction()\nf1(RES)\n"
        "foreach(i RANGE 3)\n  list(APPEND R ${i})\nendforeach()\nlist(LENGTH R n)\nlist(GET R -1 last)\n"
        "string(TOUPPER \"abc\" up)\nstring(REPLACE \"-\" \"_\" under \"a-b-c\")\nstring(SUBSTRING \"abcdef\" 1 3 sub)\n"
        "set(parts \"x;y;z\")\nforeach(p IN LISTS parts)\n  if(p STREQUAL \"y\")\n    break()\n  endif()\n  set(before_y ${p})\nendforeach()\n"
        "if(NOT DEFINED INNER AND NOT DEFINED NOPE AND \"${R}\" STREQUAL \"0;1;2;3\" AND (1 LESS 2) AND 3 VERSION_GREATER 2.9 AND \"x\" IN_LIST parts)\n  set(ALLGOOD ok)\nelse()\n  set(ALLGOOD bad)\nendif()\n"
        "if(0)\n  set(WRONG_BRANCH 1)\nelseif(ON)\n  set(ELSEIF_OK 1)\nelse()\n  set(WRONG_BRANCH 1)\nendif()\n"
        "add_executable(t m.cpp)\n"
        "target_compile_definitions(t PRIVATE N=${n} LAST=${last} MACRO=${OUT_foo} FUNC=${RES} UP=${up} UNDER=${under} SUB=${sub} BEFORE=${before_y} ALL=${ALLGOOD} ELIF=${ELSEIF_OK} \"RAWTXT=${RAW}\" VER=${PROJECT_VERSION})\n");
    write("m.cpp", "int main(){}\n");
    int rc; string o = sh("cmake . -o b.okay --audit", &rc); string man = readFile(gRoot / "b.okay");
    check(rc == 0, "reads", o);
    for (const char* want : {"N=4", "LAST=3", "MACRO=yes", "FUNC=made", "UP=ABC", "UNDER=a_b_c", "SUB=bcd", "BEFORE=x", "ALL=ok", "ELIF=1", "VER=4.5"})
        check(man.find(string(" ") + want) != string::npos, string("define ") + want, man);
    check(man.find("WRONG_BRANCH") == string::npos, "no wrong branch ran");
    check(man.find("raw \\\"text\\\" ${NOT_EXPANDED} ; stays") != string::npos || man.find("raw \\\"text\\\" ${NOT_EXPANDED}") != string::npos, "a [[bracket]] argument is not expanded", man);
    write("CMakeLists.txt", "add_executable(t m.cpp\n"); o = sh("cmake . -o c.okay", &rc);
    check(rc == 2 && o.find("missing ')'") != string::npos, "a missing parenthesis is an error with the line", o);
    write("CMakeLists.txt", "if(ON)\nadd_executable(t m.cpp)\n"); o = sh("cmake . -o c.okay", &rc);
    check(rc == 2 && o.find("no endif") != string::npos, "an if without endif is an error", o);
}

static void testSharedObjectLibs() {
    printf("test: shared and object libraries (manifest and from CMake)\n");
    fresh("sharedobj");
    write("sh.cpp", "int shared_value(){return 40;}\n");
    write("ob.cpp", "int object_value(){return 2;}\nstruct Reg{Reg(){}} reg_unused;\n");
    write("st.cpp", "int static_value(){return 1;}\n");
    write("m.cpp", "int shared_value(); int object_value(); int static_value(); int main(){return shared_value()+object_value()+static_value()==43?0:1;}\n");
    write("build.okay", "target sh shared\n sources = sh.cpp\nend\ntarget ob object\n sources = ob.cpp\nend\ntarget st lib\n sources = st.cpp\nend\ntarget m exe\n sources = m.cpp\n deps = sh ob st\nend\n");
    int rc; string o = sh("-j2", &rc);
    check(rc == 0 && fs::exists(gRoot / "build/libsh.so") && fs::exists(gRoot / "build/libob.a"), "a shared library makes a .so and an object library an archive", o);
    check(system(("cd / && env -u LD_LIBRARY_PATH '" + (gRoot / "build/m").string() + "' >/dev/null").c_str()) == 0, "the program finds the .so through its rpath with no LD_LIBRARY_PATH");
    string cdb; sh("compdb"); cdb = readFile(gRoot / "compile_commands.json");
    check(cdb.find("-fPIC") != string::npos, "shared library sources get -fPIC", cdb);
    write("bad.okay", "target x weird\nend\n"); o = sh("-f bad.okay graph", &rc); check(rc == 2 && o.find("exe|lib|shared|object") != string::npos, "unknown kind is an error", o);
    // from CMake
    write("CMakeLists.txt", "project(P)\nadd_library(sh SHARED sh.cpp)\nadd_library(ob OBJECT ob.cpp)\nadd_library(st STATIC st.cpp)\nadd_executable(m m.cpp)\ntarget_link_libraries(m PRIVATE sh ob st)\n");
    o = sh("cmake . -o c.okay", &rc); string man = readFile(gRoot / "c.okay");
    check(rc == 0 && man.find("target sh shared") != string::npos && man.find("target ob object") != string::npos && man.find("target st lib") != string::npos, "cmake writes shared and object libraries", man);
    fs::remove_all(gRoot / "build"); fs::remove_all(gRoot / ".okaybuild");
    o = sh("-f c.okay -j2", &rc); check(rc == 0 && system(("cd / && env -u LD_LIBRARY_PATH '" + (gRoot / "build/m").string() + "'").c_str()) == 0, "the CMake-derived manifest builds and runs", o);
}

static void testFetchAndSha256() {
    printf("test: SHA-256, pinned fetch (git commit and tarball), FetchContent from CMake\n");
    fresh("fetch");
    write("abc.txt", "abc"); write("empty.txt", ""); { std::ofstream f(gRoot / "million.txt", std::ios::binary); string chunk(1000, 'a'); for (int i = 0; i < 1000; ++i) f << chunk; }
    string o = sh("tool sha256 abc.txt empty.txt million.txt");
    check(o.find("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  abc.txt") != string::npos, "SHA-256 of abc matches the published value", o);
    check(o.find("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  empty.txt") != string::npos, "SHA-256 of nothing matches", o);
    check(o.find("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0  million.txt") != string::npos, "SHA-256 of a million a's matches (many blocks)", o);
    // a tarball with a pin
    write("src/pkg-1.0/lib.cpp", "int from_pkg(){return 6;}\n"); write("src/pkg-1.0/pkg.h", "#pragma once\n");
    int rc; sh("tool pack src out/not_used.okpk"); // (just proves tool still works beside sha256)
    string tarcmd = "cd '" + gRoot.string() + "/src' && tar -czf ../pkg.tar.gz pkg-1.0"; check(system(tarcmd.c_str()) == 0, "tar available for the test");
    string pin = sh("tool sha256 pkg.tar.gz"); pin = pin.substr(0, 64);
    write("t.okay", "fetch pkg\n url = file://" + gRoot.string() + "/pkg.tar.gz\n sha256 = " + pin + "\n dir = deps/pkg\nend\ntarget a lib\n sources = deps/pkg/lib.cpp\nend\n");
    o = sh("-f t.okay build", &rc); check(rc != 0 && o.find("run okay-build fetch pkg") != string::npos, "a build never downloads and says how to", o);
    o = sh("-f t.okay fetch", &rc); check(rc == 0 && fs::exists(gRoot / "deps/pkg/lib.cpp") && o.find("sha256 checked") != string::npos, "fetch downloads, checks and unpacks (strip 1)", o);
    o = sh("-f t.okay fetch", &rc); check(rc == 0 && o.find("already in") != string::npos, "a second fetch does nothing", o);
    o = sh("-f t.okay build", &rc); check(rc == 0, "the build then works", o);
    string bad = pin; bad[0] = bad[0] == '0' ? '1' : '0';
    write("t2.okay", "fetch pkg2\n url = file://" + gRoot.string() + "/pkg.tar.gz\n sha256 = " + bad + "\n dir = deps/pkg2\nend\n");
    o = sh("-f t2.okay fetch", &rc); check(rc == 2 && o.find("nothing was unpacked") != string::npos && !fs::exists(gRoot / "deps/pkg2"), "a wrong pin refuses and leaves no folder", o);
    write("t3.okay", "fetch p3\n url = file://" + gRoot.string() + "/pkg.tar.gz\nend\n"); o = sh("-f t3.okay graph", &rc); check(rc == 2 && (o.find("needs either git") != string::npos || o.find("sha256") != string::npos), "url without sha256 is refused at load", o);
    // git: a local repository, pinned by commit
    string g = "cd '" + gRoot.string() + "' && rm -rf repo && mkdir repo && cd repo && git init -q && git config user.email a@b && git config user.name n && echo 'int g(){return 1;}' > g.cpp && git add g.cpp && git commit -q -m one && git config uploadpack.allowAnySHA1InWant true";
    if (system(g.c_str()) == 0) {
        string commit = sh("tool sha256 abc.txt"); (void)commit; FILE* p = popen(("cd '" + gRoot.string() + "/repo' && git rev-parse HEAD").c_str(), "r"); char b[64] = {}; size_t n = fread(b, 1, 40, p); pclose(p); string c(b, n);
        write("g.okay", "fetch gitpkg\n git = file://" + gRoot.string() + "/repo\n commit = " + c + "\n dir = deps/gitpkg\nend\n");
        o = sh("-f g.okay fetch", &rc); check(rc == 0 && fs::exists(gRoot / "deps/gitpkg/g.cpp") && !fs::exists(gRoot / "deps/gitpkg/.git") && o.find("git commit checked") != string::npos, "git fetch by commit, checked, history dropped", o);
        write("g2.okay", "fetch gitbad\n git = file://" + gRoot.string() + "/repo\n commit = main\n dir = deps/gitbad\nend\n");
        o = sh("-f g2.okay fetch", &rc); check(rc == 2 && o.find("40 hex digits") != string::npos, "a branch name is not a pin", o);
    }
    // FetchContent in CMake files becomes a fetch block
    write("CMakeLists.txt", "project(F)\ninclude(FetchContent)\nFetchContent_Declare(pkg URL file://" + gRoot.string() + "/pkg.tar.gz URL_HASH SHA256=" + pin + ")\nFetchContent_MakeAvailable(pkg)\nadd_library(l STATIC ${pkg_SOURCE_DIR}/lib.cpp)\ntarget_include_directories(l PUBLIC ${pkg_SOURCE_DIR})\n");
    fs::remove_all(gRoot / "_deps");
    o = sh("cmake . -o c.okay --audit", &rc); string man = readFile(gRoot / "c.okay");
    check(rc == 0 && man.find("fetch pkg") != string::npos && man.find("sha256 = " + pin) != string::npos && man.find("dir = _deps/pkg-src") != string::npos && man.find("target l lib") != string::npos && man.find("sources = _deps/pkg-src/lib.cpp") != string::npos, "FetchContent_Declare gives a fetch block and the target is written", man + o);
    o = sh("-f c.okay fetch", &rc); check(rc == 0 && fs::exists(gRoot / "_deps/pkg-src/lib.cpp"), "and fetch then works from it", o);
    o = sh("-f c.okay -j2", &rc); check(rc == 0, "and it builds", o);
}

static void testOnlyAffectedTestsRebuild() {
    printf("test: touching one header rebuilds only the programs that include it, then ctest runs without building\n");
    fresh("affected");
    write("a.h", "#pragma once\ninline int av(){return 1;}\n"); write("b.h", "#pragma once\ninline int bv(){return 2;}\n");
    write("ta.cpp", "#include \"a.h\"\nint main(){return av()==1?0:1;}\n"); write("tb.cpp", "#include \"b.h\"\nint main(){return bv()==2?0:1;}\n");
    write("build.okay", "target ta exe\n sources = ta.cpp\nend\ntarget tb exe\n sources = tb.cpp\nend\nctest ta\n cmd = {target:ta}\nend\nctest tb\n cmd = {target:tb}\nend\n");
    int rc; string o = sh("-j2 ctest", &rc); check(rc == 0 && ran(o) == 4, "first ctest builds both programs (2 compiles, 2 links) and runs them", o);
    o = sh("-j2 ctest", &rc); check(rc == 0 && ran(o) == 0, "second ctest builds nothing", o);
    sleepMs(); write("a.h", "#pragma once\ninline int av(){return 1;} // edited\n");
    o = sh("-j2 --explain ctest", &rc); check(rc == 0 && ran(o) == 1 && o.find("CC ta.cpp") != string::npos && o.find("CC tb.cpp") == string::npos && o.find("LD") == string::npos, "only the compile of the program that includes the edited header runs (a comment-only edit is a cache hit with the same object, so no relink)", o);
    write("a.h", "#pragma once\ninline int av(){return 99;}\n");
    o = sh("-j2 --explain ctest", &rc); check(ran(o) == 2 && o.find("CC tb.cpp") == string::npos && o.find("LD tb") == string::npos, "a real edit recompiles and relinks only ta", o);
    o = sh("-j2 ctest", &rc); check(rc == 1 && o.find("FAIL ta") != string::npos && o.find("PASS tb") != string::npos, "the rebuilt program's result is what ctest reports", o);
}

static void testGeneratedSourcesAndRegen() {
    printf("test: configure-time generated sources, and the manifest regenerates itself when a CMake file or a file it read changes\n");
    fresh("gensrc");
    write("data/greeting.txt", "hello");
    write("CMakeLists.txt",
        "project(G)\nfile(READ ${CMAKE_SOURCE_DIR}/data/greeting.txt _g)\n"
        "set(_cpp ${CMAKE_CURRENT_BINARY_DIR}/gen/greet.cpp)\n"
        "file(WRITE ${_cpp}.new \"const char* greeting(){return \\\"${_g}\\\";}\\n\")\n"
        "execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_cpp}.new ${_cpp})\n"
        "add_executable(g main.cpp ${_cpp})\n");
    write("main.cpp", "#include <cstring>\nconst char* greeting();\nint main(){return std::strcmp(greeting(), \"hello\") == 0 ? 0 : 1;}\n");
    int rc; string o = sh("cmake . -o build.okay", &rc); string man = readFile(gRoot / "build.okay");
    check(rc == 0 && man.find("_deps/okaybuild-gen/gen/greet.cpp") != string::npos && fs::exists(gRoot / "_deps/okaybuild-gen/gen/greet.cpp"), "a file written with file(WRITE) under the binary folder becomes a real source", o + man);
    check(man.find("buildfile") != string::npos && man.find("data/greeting.txt") != string::npos, "the files the CMake code read are the regeneration inputs", man);
    o = sh("-f build.okay -j2", &rc); check(rc == 0 && system(("'" + (gRoot / "build/g").string() + "'").c_str()) == 0, "the generated source builds and runs", o);
    o = sh("-f build.okay -j2", &rc); check(rc == 0 && ran(o) == 0 && o.find("regenerating") == string::npos, "nothing to do the second time, no regeneration", o);
    sleepMs(); write("data/greeting.txt", "bye");
    o = sh("-f build.okay -j2", &rc); check(o.find("regenerating build.okay") != string::npos && ran(o) >= 1, "a changed file the CMake code read regenerates the manifest and rebuilds the generated source", o);
    check(system(("'" + (gRoot / "build/g").string() + "'").c_str()) != 0, "and the program now says bye");
    sleepMs(); write("CMakeLists.txt", readFile(gRoot / "CMakeLists.txt") + "add_executable(extra main.cpp ${_cpp})\n");
    o = sh("-f build.okay targets", &rc); check(o.find("extra") != string::npos, "an edited CMakeLists.txt adds the new target by itself", o);
}
int main(int argc, char** argv) {
    if (argc < 2) { puts("usage: test_okaybuild PATH_TO_okay-build"); return 2; }
    gTool = "'" + fs::absolute(argv[1]).string() + "'";
    if (const char* only = getenv("OKB_ONLY")) { // OKB_ONLY=cmake runs just the CMake reader, ctest and library tests (quick while working on them)
        if (string(only) == "cmake") { testCmakeReader(); testCmakeSyntax(); testSharedObjectLibs(); testFetchAndSha256(); testOnlyAffectedTestsRebuild(); testGeneratedSourcesAndRegen(); printf("okaybuild tests (cmake only): %d checks passed, %d failed\n", gPass, gFail); return gFail ? 1 : 0; }
    }
    testBasicAndNoop(); testHeaderTouch(); testDefine(); testMissingHeader(); testCycle(); testLinkOrder();
    testParallelDeterministic(); testCompdbCleanDry(); testManifestErrors(); testMingwCompdb(); testMingwSlice(); testStatShortcut(); testFreshNoopTrust(); testSpacesBuildDirRsp(); testRawActions(); testHashInCommandAndPath(); testImportGenerated();
    testPools(); testGeneratorRestat(); testKeepGoing(); testRegenManifest(); testSharedInput(); testImportedCompdb(); testWindowsHost(); testModules(); testAuditHelp(); testConfigsAndToolchains(); testCompilerCache(); testReport(); testOwnTests(); testMemoryLimit(); testCancelAndRecover(); testMsvcFlags(); testJobserver(); testCacheLimit(); testPackages(); testPackage(); testHistoryAndChecks(); testBinaryState(); testFifoJobserver(); testReportDiff(); testDistLayout(); testNativeManifest(); testImportedCache(); testGraphTargets(); testToolchainAuto(); testSharedCache(); testReportCacheAndJson(); testTestFilterTimeoutOutput();
    testRestoredObjectsStayTrusted();
    testImporters();
    testPerConfigDirsAndTrace();
    testJunitShardVersions();
    testMsvcVersionChoice();
    testToolHelpers();
    testCompressedCache();
    testLzFormat();
    testQuotedDefine();
    testAccountDefines();
    testMeasuredMemory();
    testPathMap();
    testRemoteCacheFolder();
    testCmakeReader();
    testCmakeSyntax();
    testSharedObjectLibs();
    testFetchAndSha256();
    testOnlyAffectedTestsRebuild();
    testGeneratedSourcesAndRegen();
    printf("okaybuild tests: %d checks passed, %d failed\n", gPass, gFail); return gFail ? 1 : 0;
}
