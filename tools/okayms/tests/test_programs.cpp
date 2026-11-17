// Several programs built with okay-cl and run under Wine.

#include "check.hpp"
#include "okayms/archive.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <vector>

namespace {

using okayms_test::Tally;

bool write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path.c_str());
    if (!out) return false;
    out << text;
    return static_cast<bool>(out);
}

bool write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

int run(const std::string& cmd, const std::string& out_path) {
    const int rc = std::system((cmd + " >" + out_path + " 2>&1").c_str());
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

std::string slurp(const std::string& path) {
    std::ifstream in(path.c_str());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void build_kernel(const std::string& dir) {
    okayms::Archive archive;
    for (const char* name : {"ExitProcess", "GetStdHandle", "WriteFile"}) {
        okayms::ArchiveMember member;
        member.name = name;
        member.is_import = true;
        member.import.symbol = name;
        member.import.dll = "kernel32.dll";
        archive.members.push_back(member);
    }
    const auto bytes = okayms::write_archive(archive);
    write_bytes(dir + "/kernel32.lib", bytes.value);
}

void test_sub_and_if(Tally& tally, const std::string& cl, const std::string& dir) {
    std::printf("test_sub_and_if\n");
    CHECK(tally, write_text(dir + "/math.c", "int entry(void) { return 50 - 8; }\n"));
    CHECK(tally, run(cl + " /Fe" + dir + "/sub.exe " + dir + "/math.c", "/tmp/okayms-prog-sub.txt") == 0);
    const int sub = run("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + dir + "/sub.exe",
                        "/tmp/okayms-prog-sub-wine.txt");
    CHECK(tally, sub == 42);
    if (sub != 42) std::fprintf(stderr, "  sub wine %d\n%s\n", sub, slurp("/tmp/okayms-prog-sub.txt").c_str());

    CHECK(tally, write_text(dir + "/branch.c",
                            "int entry(void) {\n"
                            "  int x;\n"
                            "  x = 2;\n"
                            "  if (x > 1) return 9;\n"
                            "  return 4;\n"
                            "}\n"));
    CHECK(tally, run(cl + " /Fe" + dir + "/if.exe " + dir + "/branch.c", "/tmp/okayms-prog-if.txt") == 0);
    const int branched = run("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + dir + "/if.exe",
                             "/tmp/okayms-prog-if-wine.txt");
    CHECK(tally, branched == 9);
    if (branched != 9) std::fprintf(stderr, "  if wine %d\n", branched);
}

void test_hello(Tally& tally, const std::string& cl, const std::string& dir) {
    std::printf("test_hello\n");
    build_kernel(dir);
    CHECK(tally, write_text(dir + "/hello.c",
                            "__declspec(dllimport) void *GetStdHandle(unsigned int n);\n"
                            "__declspec(dllimport) int WriteFile(void *h, void *buf, unsigned int n, unsigned int *wrote, void *ov);\n"
                            "__declspec(dllimport) void ExitProcess(unsigned int code);\n"
                            "int entry(void) {\n"
                            "  unsigned int wrote;\n"
                            "  WriteFile(GetStdHandle(0xFFFFFFF5), \"hi\\n\", 3, &wrote, 0);\n"
                            "  ExitProcess(0);\n"
                            "  return 0;\n"
                            "}\n"));
    const int built = run(cl + " /Fe" + dir + "/hello.exe " + dir + "/hello.c " + dir + "/kernel32.lib",
                          "/tmp/okayms-prog-hello.txt");
    CHECK(tally, built == 0);
    if (built != 0) {
        std::fprintf(stderr, "  build: %s\n", slurp("/tmp/okayms-prog-hello.txt").c_str());
        return;
    }
    const int code = run("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + dir + "/hello.exe",
                         "/tmp/okayms-prog-hello-wine.txt");
    const std::string out = slurp("/tmp/okayms-prog-hello-wine.txt");
    CHECK(tally, code == 0);
    CHECK(tally, out == "hi\n");
    if (out != "hi\n") std::fprintf(stderr, "  stdout: %s\n", out.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    const std::string dir = "/tmp/okayms-programs";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    Tally tally;
    test_sub_and_if(tally, root + "/tools/okayms/build/okay-cl", dir);
    test_hello(tally, root + "/tools/okayms/build/okay-cl", dir);
    return tally.finish(2);
}
