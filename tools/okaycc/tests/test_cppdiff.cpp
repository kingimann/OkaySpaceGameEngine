#include "elf.hpp"
#include "link.hpp"
#include "diag.hpp"
#include "harness.hpp"

#include <dirent.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <ctime>

static int gOk = 0;

static std::string readFile(const std::string& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static int runCapture(const std::string& bin, const std::string& outPath) {
    std::string cmd = bin + " > " + outPath;
    int run = std::system(cmd.c_str());
    if (!WIFEXITED(run)) return -1;
    return WEXITSTATUS(run);
}

static std::vector<std::vector<unsigned char>> loadLibc() {
    const char* files[] = {"lib/write.c", "lib/exit.c", "lib/mem.c", "lib/str.c", "lib/printf.c", "lib/extra.c",
                           "lib/alloc.c", "lib/eh.c", "lib/m.c", "lib/stdio.c", "lib/time.c", "lib/signal.c",
                           "lib/net.c"};
    std::vector<std::string> inc = {"include"};
    std::vector<std::vector<unsigned char>> objs;
    for (const char* path : files) {
        okaycc::Diagnostics d;
        auto obj = okaycc::compileToObject(readFile(path), d, path, inc);
        if (!d.ok() || obj.empty()) return {};
        objs.push_back(std::move(obj));
    }
    return objs;
}

// viaObject: always compile to a relocatable object and link it with our C library (the path the
// engine tests take), not to one executable image.
static void diffOne(const std::string& name, const std::string& srcPath, const std::vector<std::string>& inc = {},
                    bool viaObject = false) {
    std::string src = readFile(srcPath);
    okaycc::Diagnostics d;
    std::vector<unsigned char> bin;
    if (!viaObject) bin = okaycc::compileToElf(src, d, srcPath, inc);
    if (viaObject || !d.ok() || bin.empty()) {
        okaycc::Diagnostics d2;
        auto obj = okaycc::compileToObject(src, d2, srcPath, inc);
        auto libc = loadLibc();
        std::string error;
        if (d2.ok() && !obj.empty() && !libc.empty()) {
            libc.insert(libc.begin(), std::move(obj));
            bin = okaycc::linkObjects(libc, error);
            if (!bin.empty()) d.clear();
        }
    }
    if (!d.ok() || bin.empty()) {
        std::fprintf(stderr, "FAIL %s okay-cc\n%s", name.c_str(), d.format().c_str());
        ++okaycc_test::gFail;
        return;
    }
    char tmpl[] = "/tmp/okaydiffXXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) {
        ++okaycc_test::gFail;
        return;
    }
    std::string folder(dir);
    std::string ours = folder + "/ours";
    std::string gccb = folder + "/gccb";
    std::string out1 = folder + "/o1";
    std::string out2 = folder + "/o2";
    {
        std::ofstream out(ours, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
    }
    if (chmod(ours.c_str(), 0755) != 0) {
        std::system(("rm -rf " + folder).c_str());
        ++okaycc_test::gFail;
        return;
    }
    std::string gccCmd = "g++ -std=c++17 -O0 -w -o " + gccb + " " + srcPath + " >" + folder + "/gcc.txt 2>&1";
    if (std::system(gccCmd.c_str()) != 0) {
        std::fprintf(stderr, "FAIL %s gcc\n%s\n", name.c_str(), readFile(folder + "/gcc.txt").c_str());
        std::system(("rm -rf " + folder).c_str());
        ++okaycc_test::gFail;
        return;
    }
    int e1 = runCapture(ours, out1);
    int e2 = runCapture(gccb, out2);
    std::string s1 = readFile(out1);
    std::string s2 = readFile(out2);
    std::system(("rm -rf " + folder).c_str());
    if (std::getenv("CPPDIFF_ONLY")) std::fprintf(stderr, "--- ours (exit %d)\n%s--- gcc (exit %d)\n%s", e1, s1.c_str(), e2, s2.c_str());
    if (e1 != e2 || s1 != s2) {
        std::fprintf(stderr, "FAIL %s okay-cc exit %d stdout [%s] gcc exit %d stdout [%s]\n", name.c_str(), e1,
                     s1.c_str(), e2, s2.c_str());
        ++okaycc_test::gFail;
        return;
    }
    ++gOk;
}

// Two files, each with a global object: the constructors run before main in link order (b, then
// a) and the destructors after it in reverse. g++ leaves the order across files unspecified, so
// the expected output is written out here.
static void linkTwo() {
    auto libc = loadLibc();
    std::vector<std::vector<unsigned char>> objs;
    for (const char* path : {"tests/progs_cpp_multi/b.cpp", "tests/progs_cpp_multi/a.cpp"}) {
        okaycc::Diagnostics d;
        auto obj = okaycc::compileToObject(readFile(path), d, path);
        if (!d.ok() || obj.empty()) {
            std::fprintf(stderr, "FAIL multi %s\n%s", path, d.format().c_str());
            ++okaycc_test::gFail;
            return;
        }
        objs.push_back(std::move(obj));
    }
    for (auto& o : libc) objs.push_back(std::move(o));
    std::string error;
    auto bin = okaycc::linkObjects(objs, error);
    if (bin.empty()) {
        std::fprintf(stderr, "FAIL multi link: %s\n", error.c_str());
        ++okaycc_test::gFail;
        return;
    }
    char tmpl[] = "/tmp/okaymultiXXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) {
        ++okaycc_test::gFail;
        return;
    }
    std::string folder(dir);
    {
        std::ofstream out(folder + "/prog", std::ios::binary);
        out.write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
    }
    chmod((folder + "/prog").c_str(), 0755);
    int rc = runCapture(folder + "/prog", folder + "/out");
    std::string got = readFile(folder + "/out");
    std::string want = "H(7)\nG(5)\nmain 5 7\n~G(5)\n~H(7)\n";
    std::system(("rm -rf " + folder).c_str());
    if (rc != 0 || got != want) {
        std::fprintf(stderr, "FAIL multi: rc=%d output:\n%s", rc, got.c_str());
        ++okaycc_test::gFail;
        return;
    }
    ++gOk;
}

// A real engine source file, unchanged: engine/src/Graphics/Font.cpp (static members, constexpr,
// an unnamed namespace, a two-dimensional table, std::strlen from our own <cstring>) and a driver
// that includes its header. Both are compiled by okay-cc, linked by our linker with our libc, and
// the output must match g++ building the same two files. The inline Font8x8::Pixel is defined in
// both objects (a weak symbol).
// Round 17 adds engine/src/UI/FocusGraph.cpp (std::vector, <algorithm>, lambdas, enum class,
// default member initializers, default arguments, range-for, auto), with our own headers.
static void engineDiff(const char* driver, const char* engineFile, const char* engineFile2 = nullptr) {
    std::vector<const char*> files = {driver};
    if (engineFile) files.push_back(engineFile);  // (none: the driver tests a header-only part)
    if (engineFile2) files.push_back(engineFile2);
    std::vector<std::string> inc = {"../../engine/include", "include"};
    std::vector<std::vector<unsigned char>> objs;
    for (const char* path : files) {
        okaycc::Diagnostics d;
        auto obj = okaycc::compileToObject(readFile(path), d, path, inc);
        if (!d.ok() || obj.empty()) {
            std::fprintf(stderr, "FAIL engine %s\n%s", path, d.format().c_str());
            ++okaycc_test::gFail;
            return;
        }
        objs.push_back(std::move(obj));
    }
    auto libc = loadLibc();
    for (auto& o : libc) objs.push_back(std::move(o));
    std::string error;
    auto bin = okaycc::linkObjects(objs, error);
    if (bin.empty()) {
        std::fprintf(stderr, "FAIL engine link: %s\n", error.c_str());
        ++okaycc_test::gFail;
        return;
    }
    char tmpl[] = "/tmp/okayengXXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) {
        ++okaycc_test::gFail;
        return;
    }
    std::string folder(dir);
    {
        std::ofstream out(folder + "/ours", std::ios::binary);
        out.write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
    }
    chmod((folder + "/ours").c_str(), 0755);
    std::string gccCmd = "g++ -std=c++17 -O0 -w -I ../../engine/include -o " + folder + "/gccb";
    for (const char* f : files) gccCmd += std::string(" ") + f;
    gccCmd += " -lm >" + folder + "/gcc.txt 2>&1";
    if (std::system(gccCmd.c_str()) != 0) {
        std::fprintf(stderr, "FAIL engine gcc\n%s\n", readFile(folder + "/gcc.txt").c_str());
        std::system(("rm -rf " + folder).c_str());
        ++okaycc_test::gFail;
        return;
    }
    int e1 = runCapture(folder + "/ours", folder + "/o1");
    int e2 = runCapture(folder + "/gccb", folder + "/o2");
    std::string s1 = readFile(folder + "/o1");
    std::string s2 = readFile(folder + "/o2");
    std::system(("rm -rf " + folder).c_str());
    if (e1 != e2 || s1 != s2 || s1.empty()) {
        std::fprintf(stderr, "FAIL engine %s okay-cc exit %d [%s] gcc exit %d [%s]\n", engineFile ? engineFile : driver, e1, s1.c_str(), e2,
                     s2.c_str());
        ++okaycc_test::gFail;
        return;
    }
    ++gOk;
}

// A message instead of a link failure: new T[n] for a class with no default constructor.
static void expectNewArrayError() {
    okaycc::Diagnostics d;
    okaycc::compileToObject("struct A { int v; A(int x) : v(x) {} };\nint main() { A *a = new A[3]; return 0; }\n", d,
                            "newarr.cpp");
    if (d.ok() || d.format().find("needs a default constructor") == std::string::npos) {
        std::fprintf(stderr, "FAIL new[] without a default constructor gave: %s\n", d.format().c_str());
        ++okaycc_test::gFail;
        return;
    }
    ++gOk;
}

// Round 23: a static_assert whose condition the compiler works out to false is an error.
static void expectStaticAssert() {
    okaycc::Diagnostics d;
    okaycc::compileToObject("template <class T, class U> struct S { static constexpr bool value = false; };\n"
                            "template <class T> struct S<T, T> { static constexpr bool value = true; };\n"
                            "static_assert(S<int, int>::value, \"fine\");\n"
                            "struct A { static_assert(!S<int, int>::value, \"boom\"); };\n"
                            "int main() { return 0; }\n",
                            d, "sa.cpp");
    if (d.ok() || d.format().find("static assertion failed: boom") == std::string::npos ||
        d.format().find("fine") != std::string::npos) {
        std::fprintf(stderr, "FAIL static_assert gave: %s\n", d.format().c_str());
        ++okaycc_test::gFail;
        return;
    }
    ++gOk;
}

// Round 27: a chain of operators is checked in time that grows with its length, not twice over per
// link (chain27.cpp took 65 seconds with round 26's okay-cc built with -O2; now well under one).
static void compileTime() {
    const char* path = "tests/progs_cpp_std/chain27.cpp";
    std::string src = readFile(path);
    okaycc::Diagnostics d;
    std::clock_t t0 = std::clock();
    auto obj = okaycc::compileToObject(src, d, path, {"include"});
    double secs = double(std::clock() - t0) / CLOCKS_PER_SEC;
    if (!d.ok() || obj.empty() || secs > 10) {
        std::fprintf(stderr, "FAIL compile time of chain27.cpp: %.1f s\n%s", secs, d.format().c_str());
        ++okaycc_test::gFail;
        return;
    }
    ++gOk;
}

int main() {
    DIR* dir = opendir("tests/progs_cpp");
    if (!dir) {
        std::fprintf(stderr, "FAIL cannot open tests/progs_cpp\n");
        return 1;
    }
    int files = 0;
    while (dirent* ent = readdir(dir)) {
        std::string name = ent->d_name;
        if (name.size() < 5 || name.compare(name.size() - 4, 4, ".cpp") != 0) continue;
        const char* only = std::getenv("CPPDIFF_ONLY");
        if (only && name != only) continue;
        diffOne(name, "tests/progs_cpp/" + name);
        ++files;
    }
    closedir(dir);
    // Programs that include standard headers: okay-cc reads ours (include/), g++ its own.
    if (DIR* sd = opendir("tests/progs_cpp_std")) {
        while (dirent* ent = readdir(sd)) {
            std::string name = ent->d_name;
            if (name.size() < 5 || name.compare(name.size() - 4, 4, ".cpp") != 0) continue;
            const char* only = std::getenv("CPPDIFF_ONLY");
            if (only && name != only) continue;
            diffOne(name, "tests/progs_cpp_std/" + name, {"include"});
            ++files;
        }
        closedir(sd);
    }
    if (!std::getenv("CPPDIFF_ONLY")) {
        linkTwo();
        expectNewArrayError();
        expectStaticAssert();
        compileTime();
        engineDiff("tests/progs_cpp_engine/font_main.cpp", "../../engine/src/Graphics/Font.cpp");
        engineDiff("tests/progs_cpp_engine/focus_main.cpp", "../../engine/src/UI/FocusGraph.cpp");
        // Round 18: Mip.cpp and the Texture.cpp it calls (vector<Surface>, member templates, our libm).
        engineDiff("tests/progs_cpp_engine/mip_main.cpp", "../../engine/src/Texture/Mip.cpp",
                   "../../engine/src/Texture/Texture.cpp");
        engineDiff("tests/progs_cpp_engine/buildinfo_main.cpp", "../../engine/src/Core/BuildInfo.cpp");
        // Round 19: MipBudget.cpp (priority_queue, nested lambdas, log2) and DynamicTree.cpp
        // (lambdas in member functions, F&& member templates, initializer-list constructors).
        engineDiff("tests/progs_cpp_engine/mipbudget_main.cpp", "../../engine/src/Optimize/MipBudget.cpp");
        engineDiff("tests/progs_cpp_engine/dyntree_main.cpp", "../../engine/src/Physics/Core/DynamicTree.cpp");
        engineDiff("tests/progs_cpp_engine/spatialfx_main.cpp", "../../engine/src/Audio/SpatialFx.cpp");
        // Round 20: GestureRecognizer.cpp (if-condition declarations, remove_if and erase of a range).
        engineDiff("tests/progs_cpp_engine/gesture_main.cpp", "../../engine/src/Input/GestureRecognizer.cpp");
        engineDiff("tests/progs_cpp_engine/bc_main.cpp", "../../engine/src/Texture/Bc.cpp");
        // Round 21:
        engineDiff("tests/progs_cpp_engine/wav_main.cpp", "../../engine/src/Audio/WavDecoder.cpp");
        // Round 22: the streams through an object file. Its vtables (.rodata) sat between .data
        // items, and their relocations went to the wrong section: every virtual slot was null.
        diffOne("stream22.cpp (object)", "tests/progs_cpp_std/stream22.cpp", {"include"}, true);
        engineDiff("tests/progs_cpp_engine/manifest_main.cpp", "../../engine/src/Modules/ModuleManifest.cpp");
        // Round 23: Core/Log.hpp, the first header of about a hundred engine files.
        engineDiff("tests/progs_cpp_engine/log_main.cpp", nullptr);
        // Round 24: Math/Quat.hpp (round 23: "cannot assign this value" in Quat::Euler).
        engineDiff("tests/progs_cpp_engine/quat_main.cpp", nullptr);
        // Round 25: Math/Easing.hpp, and a class whose constructor and destructor are defined in
        // another file (arrays of it call them through the helpers made at the end of the class).
        engineDiff("tests/progs_cpp_engine/easing_main.cpp", nullptr);
        engineDiff("tests/progs_cpp_multi/later25_main.cpp", "tests/progs_cpp_multi/later25_def.cpp");
    }
    if (files == 0) {
        std::fprintf(stderr, "FAIL no programs\n");
        return 1;
    }
    okaycc_test::gPass += gOk;
    std::printf("cppdiff: %d passed, %d failed\n", okaycc_test::gPass, okaycc_test::gFail);
    return okaycc_test::gFail == 0 ? 0 : 1;
}
