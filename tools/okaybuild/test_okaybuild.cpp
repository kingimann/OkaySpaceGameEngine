// Tests for okay-build using tiny synthetic projects under a temp dir. usage: test_okaybuild PATH_TO_okay-build
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <sys/stat.h>
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

int main(int argc, char** argv) {
    if (argc < 2) { puts("usage: test_okaybuild PATH_TO_okay-build"); return 2; }
    gTool = "'" + fs::absolute(argv[1]).string() + "'";
    testBasicAndNoop(); testHeaderTouch(); testDefine(); testMissingHeader(); testCycle(); testLinkOrder();
    testParallelDeterministic(); testCompdbCleanDry(); testManifestErrors(); testMingwCompdb(); testStatShortcut(); testFreshNoopTrust(); testSpacesBuildDirRsp(); testRawActions(); testHashInCommandAndPath(); testImportGenerated();
    testPools(); testGeneratorRestat(); testKeepGoing(); testRegenManifest(); testGraphTargets();
    printf("okaybuild tests: %d checks passed, %d failed\n", gPass, gFail); return gFail ? 1 : 0;
}
