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
    const char* files[] = {"lib/write.c", "lib/exit.c", "lib/mem.c", "lib/str.c", "lib/printf.c", "lib/extra.c"};
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

static void diffOne(const std::string& name, const std::string& srcPath) {
    std::string src = readFile(srcPath);
    okaycc::Diagnostics d;
    std::vector<unsigned char> bin = okaycc::compileToElf(src, d, srcPath);
    if (!d.ok() || bin.empty()) {
        okaycc::Diagnostics d2;
        auto obj = okaycc::compileToObject(src, d2, srcPath);
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
    std::string gccCmd = "gcc -std=c11 -O0 -o " + gccb + " " + srcPath + " >" + folder + "/gcc.txt 2>&1";
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
    if (e1 != e2 || s1 != s2) {
        std::fprintf(stderr, "FAIL %s okay-cc exit %d stdout [%s] gcc exit %d stdout [%s]\n", name.c_str(), e1,
                     s1.c_str(), e2, s2.c_str());
        ++okaycc_test::gFail;
        return;
    }
    ++gOk;
}

int main() {
    DIR* dir = opendir("tests/progs");
    if (!dir) {
        std::fprintf(stderr, "FAIL cannot open tests/progs\n");
        return 1;
    }
    int files = 0;
    while (dirent* ent = readdir(dir)) {
        std::string name = ent->d_name;
        if (name.size() < 3 || name.compare(name.size() - 2, 2, ".c") != 0) continue;
        diffOne(name, "tests/progs/" + name);
        ++files;
    }
    closedir(dir);
    if (files == 0) {
        std::fprintf(stderr, "FAIL no programs\n");
        return 1;
    }
    okaycc_test::gPass += gOk;
    std::printf("diff: %d passed, %d failed\n", okaycc_test::gPass, okaycc_test::gFail);
    return okaycc_test::gFail == 0 ? 0 : 1;
}
