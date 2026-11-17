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
    write("tests/test_math.cpp", "extern \"C\" int ufbx_add(); int crypto(); int plat(); int core(); int main(){return crypto()+plat()+core()+ufbx_add()==10?0:1;}\n");
    int rc; string o = sh("native . -o build.okay", &rc);
    check(rc == 0 && o.find("test_math") != string::npos, "native writes a manifest", o);
    string man = readFile(gRoot / "build.okay");
    check(man.find("target okay_engine lib") != string::npos && man.find("target okay_crypto lib") != string::npos, "engine and crypto targets", man);
    check(man.find("ModelImportAssimp") == string::npos && man.find("OwnWin32") == string::npos, "optional files are left out", man);
    check(man.find("tests/test_math.cpp") != string::npos && man.find("ufbx.c") != string::npos, "test_math and ufbx are included", man);
    o = sh("-j2 test_math", &rc);
    check(rc == 0, "the generated manifest builds test_math", o);
    check(system(("'" + (gRoot / "build/test_math").string() + "' >/dev/null").c_str()) == 0, "test_math runs");
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

int main(int argc, char** argv) {
    if (argc < 2) { puts("usage: test_okaybuild PATH_TO_okay-build"); return 2; }
    gTool = "'" + fs::absolute(argv[1]).string() + "'";
    testBasicAndNoop(); testHeaderTouch(); testDefine(); testMissingHeader(); testCycle(); testLinkOrder();
    testParallelDeterministic(); testCompdbCleanDry(); testManifestErrors(); testMingwCompdb(); testMingwSlice(); testStatShortcut(); testFreshNoopTrust(); testSpacesBuildDirRsp(); testRawActions(); testHashInCommandAndPath(); testImportGenerated();
    testPools(); testGeneratorRestat(); testKeepGoing(); testRegenManifest(); testSharedInput(); testImportedCompdb(); testWindowsHost(); testModules(); testAuditHelp(); testConfigsAndToolchains(); testCompilerCache(); testReport(); testOwnTests(); testMemoryLimit(); testCancelAndRecover(); testMsvcFlags(); testJobserver(); testCacheLimit(); testPackages(); testPackage(); testHistoryAndChecks(); testBinaryState(); testFifoJobserver(); testReportDiff(); testDistLayout(); testNativeManifest(); testImportedCache(); testGraphTargets(); testToolchainAuto(); testSharedCache(); testReportCacheAndJson(); testTestFilterTimeoutOutput();
    printf("okaybuild tests: %d checks passed, %d failed\n", gPass, gFail); return gFail ? 1 : 0;
}
