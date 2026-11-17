#include "elf.hpp"
#include "link.hpp"
#include "diag.hpp"
#include "harness.hpp"

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

static int runExe(const std::string& bin) {
    int run = std::system(bin.c_str());
    if (!WIFEXITED(run)) return -1;
    return WEXITSTATUS(run);
}

int main() {
    const char* files[] = {"tests/progs/multi/a.c", "tests/progs/multi/b.c", "tests/progs/multi/main.c"};
    std::vector<std::vector<unsigned char>> objs;
    for (const char* path : files) {
        okaycc::Diagnostics d;
        auto obj = okaycc::compileToObject(readFile(path), d, path);
        if (!d.ok() || obj.empty()) {
            std::fprintf(stderr, "FAIL object %s\n%s", path, d.format().c_str());
            return 1;
        }
        objs.push_back(std::move(obj));
    }
    std::string error;
    auto exe = okaycc::linkObjects(objs, error);
    if (exe.empty()) {
        std::fprintf(stderr, "FAIL link %s\n", error.c_str());
        return 1;
    }
    char tmpl[] = "/tmp/okayldXXXXXX";
    char* dir = mkdtemp(tmpl);
    if (!dir) return 1;
    std::string folder(dir);
    std::string ours = folder + "/ours";
    {
        std::ofstream out(ours, std::ios::binary);
        out.write(reinterpret_cast<const char*>(exe.data()), static_cast<std::streamsize>(exe.size()));
    }
    chmod(ours.c_str(), 0755);
    int got = runExe(ours);
    std::string gccb = folder + "/gccb";
    std::string cmd = "gcc -std=c11 -O0 -o " + gccb + " tests/progs/multi/a.c tests/progs/multi/b.c tests/progs/multi/main.c";
    if (std::system(cmd.c_str()) != 0) {
        std::fprintf(stderr, "FAIL gcc\n");
        std::system(("rm -rf " + folder).c_str());
        return 1;
    }
    int want = runExe(gccb);
    std::system(("rm -rf " + folder).c_str());
    if (got != 27 || got != want) {
        std::fprintf(stderr, "FAIL link exit %d gcc %d\n", got, want);
        return 1;
    }
    const char* five[] = {"tests/progs/five/a.c", "tests/progs/five/b.c", "tests/progs/five/c.c",
                          "tests/progs/five/d.c", "tests/progs/five/main.c"};
    std::vector<std::vector<unsigned char>> fiveObjs;
    for (const char* path : five) {
        okaycc::Diagnostics fd;
        auto obj = okaycc::compileToObject(readFile(path), fd, path);
        if (!fd.ok() || obj.empty()) {
            std::fprintf(stderr, "FAIL five %s\n%s", path, fd.format().c_str());
            return 1;
        }
        fiveObjs.push_back(std::move(obj));
    }
    std::string ferr;
    auto fexe = okaycc::linkObjects(fiveObjs, ferr);
    if (fexe.empty()) {
        std::fprintf(stderr, "FAIL five link %s\n", ferr.c_str());
        return 1;
    }
    char ftmpl[] = "/tmp/okayfiveXXXXXX";
    char* fdir = mkdtemp(ftmpl);
    if (!fdir) return 1;
    std::string ffolder(fdir);
    std::string fbin = ffolder + "/p";
    {
        std::ofstream out(fbin, std::ios::binary);
        out.write(reinterpret_cast<const char*>(fexe.data()), static_cast<std::streamsize>(fexe.size()));
    }
    chmod(fbin.c_str(), 0755);
    int fgot = runExe(fbin);
    std::string fgcc = ffolder + "/g";
    std::string fcmd = "gcc -std=c11 -O0 -o " + fgcc +
                       " tests/progs/five/a.c tests/progs/five/b.c tests/progs/five/c.c tests/progs/five/d.c "
                       "tests/progs/five/main.c";
    if (std::system(fcmd.c_str()) != 0) {
        std::fprintf(stderr, "FAIL five gcc\n");
        std::system(("rm -rf " + ffolder).c_str());
        return 1;
    }
    int fwant = runExe(fgcc);
    std::system(("rm -rf " + ffolder).c_str());
    if (fgot != 15 || fgot != fwant) {
        std::fprintf(stderr, "FAIL five exit %d gcc %d\n", fgot, fwant);
        return 1;
    }
    std::printf("link: 2 passed, 0 failed\n");
    return 0;
}
