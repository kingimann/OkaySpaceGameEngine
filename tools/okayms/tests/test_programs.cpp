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

void test_sort_and_list(Tally& tally, const std::string& cl, const std::string& dir) {
    std::printf("test_sort_and_list\n");
    CHECK(tally, write_text(dir + "/sort.c",
                            "int entry(void) {\n"
                            "  int a[5]; int i; int j; int t;\n"
                            "  a[0] = 5; a[1] = 1; a[2] = 4; a[3] = 2; a[4] = 3;\n"
                            "  for (i = 0; i < 5; i = i + 1) {\n"
                            "    for (j = 0; j < 4; j = j + 1) {\n"
                            "      if (a[j] > a[j + 1]) {\n"
                            "        t = a[j]; a[j] = a[j + 1]; a[j + 1] = t;\n"
                            "      }\n"
                            "    }\n"
                            "  }\n"
                            "  return a[0] + a[4] * 10;\n"
                            "}\n"));
    CHECK(tally, run(cl + " /Fe" + dir + "/sort.exe " + dir + "/sort.c", "/tmp/okayms-prog-sort.txt") == 0);
    const int sorted = run("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + dir + "/sort.exe",
                           "/tmp/okayms-prog-sort-wine.txt");
    CHECK(tally, sorted == 51);
    if (sorted != 51) std::fprintf(stderr, "  sort wine %d\n%s\n", sorted, slurp("/tmp/okayms-prog-sort.txt").c_str());

    CHECK(tally, write_text(dir + "/list.c",
                            "struct Node { int value; int more; struct Node *next; };\n"
                            "int entry(void) {\n"
                            "  struct Node a; struct Node b; struct Node c;\n"
                            "  struct Node *p; int s; int left;\n"
                            "  a.value = 10; b.value = 20; c.value = 12;\n"
                            "  a.next = &b; b.next = &c; c.next = &a;\n"
                            "  s = 0; left = 3; p = &a;\n"
                            "  while (left > 0) {\n"
                            "    s = s + (*p).value;\n"
                            "    p = (*p).next;\n"
                            "    left = left - 1;\n"
                            "  }\n"
                            "  return s;\n"
                            "}\n"));
    CHECK(tally, run(cl + " /Fe" + dir + "/list.exe " + dir + "/list.c", "/tmp/okayms-prog-list.txt") == 0);
    const int summed = run("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + dir + "/list.exe",
                           "/tmp/okayms-prog-list-wine.txt");
    CHECK(tally, summed == 42);
    if (summed != 42) std::fprintf(stderr, "  list wine %d\n%s\n", summed, slurp("/tmp/okayms-prog-list.txt").c_str());
}

void test_reverse(Tally& tally, const std::string& cl, const std::string& dir) {
    std::printf("test_reverse\n");
    build_kernel(dir);
    CHECK(tally, write_text(dir + "/rev.c",
                            "__declspec(dllimport) void *GetStdHandle(unsigned int n);\n"
                            "__declspec(dllimport) int WriteFile(void *h, void *buf, unsigned int n, unsigned int *wrote, void *ov);\n"
                            "__declspec(dllimport) void ExitProcess(unsigned int code);\n"
                            "int entry(void) {\n"
                            "  char buf[8]; char *s; int i; int n; unsigned int wrote;\n"
                            "  s = \"abc\"; n = 3;\n"
                            "  for (i = 0; i < n; i = i + 1) buf[i] = s[n - 1 - i];\n"
                            "  buf[n] = 10;\n"
                            "  WriteFile(GetStdHandle(0xFFFFFFF5), buf, 4, &wrote, 0);\n"
                            "  ExitProcess(0);\n"
                            "  return 0;\n"
                            "}\n"));
    CHECK(tally, run(cl + " /Fe" + dir + "/rev.exe " + dir + "/rev.c " + dir + "/kernel32.lib",
                     "/tmp/okayms-prog-rev.txt") == 0);
    const int code = run("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + dir + "/rev.exe",
                         "/tmp/okayms-prog-rev-wine.txt");
    const std::string out = slurp("/tmp/okayms-prog-rev-wine.txt");
    CHECK(tally, code == 0);
    CHECK(tally, out == "cba\n");
    if (out != "cba\n") std::fprintf(stderr, "  stdout: [%s]\n%s\n", out.c_str(), slurp("/tmp/okayms-prog-rev.txt").c_str());
}

void test_multi(Tally& tally, const std::string& cl, const std::string& dir) {
    std::printf("test_multi\n");
    CHECK(tally, write_text(dir + "/add3.c",
                            "__declspec(dllexport) int add3(int a, int b, int c) { return a + b + c; }\n"
                            "int entry(void) { return 1; }\n"));
    CHECK(tally, write_text(dir + "/util.c",
                            "int add3(int a, int b, int c);\n"
                            "int scale(int n) { return add3(n, n, n); }\n"));
    CHECK(tally, write_text(dir + "/main.c",
                            "int scale(int n);\n"
                            "int entry(void) { return scale(14); }\n"));
    CHECK(tally, run(cl + " /LD /Fe" + dir + "/add3.dll " + dir + "/add3.c", "/tmp/okayms-prog-dll.txt") == 0);
    okayms::Archive lib;
    okayms::ArchiveMember member;
    member.name = "add3";
    member.is_import = true;
    member.import.symbol = "add3";
    member.import.dll = "add3.dll";
    member.import.import_type = okayms::kImportCode;
    member.import.name_type = okayms::kImportByName;
    lib.members.push_back(member);
    const auto lib_bytes = okayms::write_archive(lib);
    CHECK(tally, lib_bytes.ok());
    if (!lib_bytes.ok()) return;
    CHECK(tally, write_bytes(dir + "/add3.lib", lib_bytes.value));
    CHECK(tally, run(cl + " /c /Fo" + dir + "/util.obj " + dir + "/util.c", "/tmp/okayms-prog-util.txt") == 0);
    CHECK(tally, run(cl + " /c /Fo" + dir + "/main.obj " + dir + "/main.c", "/tmp/okayms-prog-main.txt") == 0);
    CHECK(tally, run(cl + " /Fe" + dir + "/multi.exe " + dir + "/main.obj " + dir + "/util.obj " + dir + "/add3.lib",
                     "/tmp/okayms-prog-multi.txt") == 0);
    const int code = run("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + dir + "/multi.exe",
                         "/tmp/okayms-prog-multi-wine.txt");
    CHECK(tally, code == 42);
    if (code != 42) std::fprintf(stderr, "  multi wine %d\n%s\n", code, slurp("/tmp/okayms-prog-multi.txt").c_str());
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
    test_sort_and_list(tally, root + "/tools/okayms/build/okay-cl", dir);
    test_reverse(tally, root + "/tools/okayms/build/okay-cl", dir);
    test_multi(tally, root + "/tools/okayms/build/okay-cl", dir);
    return tally.finish(5);
}
