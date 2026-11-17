#include "elf.hpp"
#include "link.hpp"
#include "diag.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <vector>

static std::string readFile(const std::string& path) {
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static std::vector<std::vector<unsigned char>> libcObjects() {
    const char* files[] = {"lib/write.c", "lib/exit.c", "lib/mem.c", "lib/str.c"};
    std::vector<std::vector<unsigned char>> objs;
    for (const char* path : files) {
        okaycc::Diagnostics d;
        auto obj = okaycc::compileToObject(readFile(path), d, path);
        if (!d.ok() || obj.empty()) {
            std::fprintf(stderr, "FAIL libc %s\n%s", path, d.format().c_str());
            return {};
        }
        objs.push_back(std::move(obj));
    }
    return objs;
}

int main() {
    auto libc = libcObjects();
    if (libc.empty()) return 1;
    okaycc::Diagnostics d;
    auto prog = okaycc::compileToObject(readFile("tests/progs/libc/use.c"), d, "tests/progs/libc/use.c");
    if (!d.ok() || prog.empty()) {
        std::fprintf(stderr, "FAIL use.c\n%s", d.format().c_str());
        return 1;
    }
    std::vector<std::vector<unsigned char>> objs;
    objs.push_back(std::move(prog));
    for (auto& o : libc) objs.push_back(std::move(o));
    std::string error;
    auto exe = okaycc::linkObjects(objs, error);
    if (exe.empty()) {
        std::fprintf(stderr, "FAIL link %s\n", error.c_str());
        return 1;
    }
    char tmpl[] = "/tmp/okaylibcXXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) return 1;
    std::string folder(dir);
    std::string ours = folder + "/ours";
    std::string gccb = folder + "/gccb";
    std::string o1 = folder + "/o1";
    std::string o2 = folder + "/o2";
    {
        std::ofstream out(ours, std::ios::binary);
        out.write(reinterpret_cast<const char*>(exe.data()), static_cast<std::streamsize>(exe.size()));
    }
    chmod(ours.c_str(), 0755);
    std::string gccCmd = "gcc -std=c11 -O0 -o " + gccb + " tests/progs/libc/use.c >" + folder + "/gcc.txt 2>&1";
    if (std::system(gccCmd.c_str()) != 0) {
        std::fprintf(stderr, "FAIL gcc\n%s\n", readFile(folder + "/gcc.txt").c_str());
        std::system(("rm -rf " + folder).c_str());
        return 1;
    }
    int e1 = std::system((ours + " > " + o1).c_str());
    int e2 = std::system((gccb + " > " + o2).c_str());
    int s1 = WIFEXITED(e1) ? WEXITSTATUS(e1) : -1;
    int s2 = WIFEXITED(e2) ? WEXITSTATUS(e2) : -1;
    std::string a = readFile(o1);
    std::string b = readFile(o2);
    std::system(("rm -rf " + folder).c_str());
    if (s1 != s2 || a != b) {
        std::fprintf(stderr, "FAIL libc exit %d stdout [%s] gcc exit %d stdout [%s]\n", s1, a.c_str(), s2, b.c_str());
        return 1;
    }
    std::printf("libc: 1 passed, 0 failed\n");
    return 0;
}
