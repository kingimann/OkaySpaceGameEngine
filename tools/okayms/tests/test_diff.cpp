// Compile the same program with okay-cl and with MinGW gcc, run both under
// Wine, and compare the exit code and stdout. If MinGW is not installed the
// test skips and still passes.

#include "check.hpp"
#include "okayms/archive.hpp"
#include "okayms/coff.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <vector>

namespace {

using okayms_test::Tally;

int run(const std::string& cmd, const std::string& out_path) {
    const int rc = std::system((cmd + " >" + out_path + " 2>&1").c_str());
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

std::string slurp(const std::string& path) {
    std::ifstream in(path.c_str());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

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

bool mingw_gcc() { return run("command -v x86_64-w64-mingw32-gcc >/dev/null", "/tmp/okayms-diff-which.txt") == 0; }

struct RunResult {
    int code = -1;
    std::string out;
};

RunResult wine_run(const std::string& exe) {
    RunResult result;
    const std::string out_path = exe + ".out";
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + exe + " >" + out_path +
                                 " 2>" + exe + ".err")
                                    .c_str());
    if (rc != -1 && WIFEXITED(rc)) result.code = WEXITSTATUS(rc);
    result.out = slurp(out_path);
    return result;
}

void compare_one(Tally& tally, const std::string& dir, const std::string& cl, const std::string& rt, const std::string& name,
                 const std::string& source, bool with_runtime, int expect) {
    std::printf("diff %s\n", name.c_str());
    const std::string src = dir + "/" + name + ".c";
    CHECK(tally, write_text(src, source));
    const std::string gcc_exe = dir + "/" + name + "-gcc.exe";
    const std::string ok_exe = dir + "/" + name + "-ok.exe";
    const int gcc = run("x86_64-w64-mingw32-gcc -std=c99 -o " + gcc_exe + " " + src, dir + "/" + name + "-gcc.log");
    CHECK(tally, gcc == 0);
    if (gcc != 0) {
        std::fprintf(stderr, "%s\n", slurp(dir + "/" + name + "-gcc.log").c_str());
        return;
    }
    std::string cl_cmd = cl + " /I" + dir;
    if (with_runtime) cl_cmd += " /I" + rt + " /ENTRY:mainCRTStartup";
    else cl_cmd += " /ENTRY:main";
    cl_cmd += " /Fe" + ok_exe + " " + src;
    if (with_runtime) cl_cmd += " " + dir + "/okayrt.lib " + dir + "/kernel32.lib";
    const int built = run(cl_cmd, dir + "/" + name + "-ok.log");
    CHECK(tally, built == 0);
    if (built != 0) {
        std::fprintf(stderr, "%s\n", slurp(dir + "/" + name + "-ok.log").c_str());
        return;
    }
    const RunResult a = wine_run(gcc_exe);
    const RunResult b = wine_run(ok_exe);
    // MinGW's printf writes CR LF. okay-cl's printf writes the LF it was given.
    auto text = [](std::string value) {
        std::string kept;
        for (char c : value)
            if (c != '\r') kept.push_back(c);
        return kept;
    };
    const std::string ga = text(a.out);
    const std::string oa = text(b.out);
    CHECK(tally, a.code == b.code);
    CHECK(tally, ga == oa);
    CHECK(tally, b.code == expect);
    if (a.code != b.code || ga != oa || b.code != expect) {
        std::fprintf(stderr, "  gcc exit %d [%s]\n  okay exit %d [%s]\n", a.code, ga.c_str(), b.code, oa.c_str());
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    Tally tally;
    if (!mingw_gcc()) {
        std::printf("skipping differential tests: x86_64-w64-mingw32-gcc is not installed\n");
        return tally.finish(1);
    }
    const std::string dir = "/tmp/okayms-diff";
    const std::string rt = root + "/tools/okayms/runtime";
    const std::string cl = root + "/tools/okayms/build/okay-cl";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    CHECK(tally, write_text(dir + "/stdio.h", "int printf(char *fmt, ...);\n"));
    okayms::Archive archive;
    for (const char* name : {"string", "printf", "startup", "mem"}) {
        const std::string obj = dir + "/" + name + ".obj";
        if (run(cl + " /I" + rt + " /c /Fo" + obj + " " + rt + "/" + name + ".c", dir + "/" + name + ".log") != 0) {
            std::fprintf(stderr, "%s\n", slurp(dir + "/" + std::string(name) + ".log").c_str());
            CHECK(tally, false);
            return tally.finish(2);
        }
        std::ifstream in(obj.c_str(), std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto object = okayms::read_coff(bytes.data(), bytes.size());
        if (!object.ok()) {
            CHECK(tally, false);
            return tally.finish(2);
        }
        okayms::ArchiveMember member;
        member.name = std::string(name) + ".obj";
        member.object = object.value;
        archive.members.push_back(member);
    }
    const auto lib = okayms::write_archive(archive);
    CHECK(tally, lib.ok() && write_bytes(dir + "/okayrt.lib", lib.value));
    okayms::Archive kernel;
    for (const char* name : {"ExitProcess", "GetStdHandle", "WriteFile", "GetProcessHeap", "HeapAlloc", "HeapFree"}) {
        okayms::ArchiveMember member;
        member.name = name;
        member.is_import = true;
        member.import.symbol = name;
        member.import.dll = "kernel32.dll";
        kernel.members.push_back(member);
    }
    const auto kbytes = okayms::write_archive(kernel);
    CHECK(tally, kbytes.ok() && write_bytes(dir + "/kernel32.lib", kbytes.value));

    compare_one(tally, dir, cl, rt, "sum",
                "int main(void) {\n"
                "  int i; int s;\n"
                "  s = 0;\n"
                "  for (i = 1; i <= 10; i = i + 1) s = s + i;\n"
                "  return s;\n"
                "}\n",
                false, 55);
    compare_one(tally, dir, cl, rt, "print",
                "#include <stdio.h>\n"
                "int main(void) {\n"
                "  printf(\"%d %s %x %f\\n\", -7, \"ok\", 255, 1.5);\n"
                "  printf(\"%4d %-4s %02x\\n\", 7, \"ab\", 10);\n"
                "  return 4;\n"
                "}\n",
                true, 4);
    compare_one(tally, dir, cl, rt, "cont",
                "int main(void) {\n"
                "  int n; int i; n = 0;\n"
                "  for (i = 0; i < 5; i = i + 1) { if (i == 2) continue; n = n + i; }\n"
                "  return n;\n"
                "}\n",
                false, 8);
    compare_one(tally, dir, cl, rt, "dowhile",
                "int main(void) {\n"
                "  int i; int n; i = 0; n = 0;\n"
                "  do { i = i + 1; if (i == 2) continue; n = n + i; } while (i < 4);\n"
                "  return n;\n"
                "}\n",
                false, 8);
    compare_one(tally, dir, cl, rt, "nest",
                "int main(void) {\n"
                "  int i; int j; int s; s = 0;\n"
                "  for (i = 0; i < 3; i = i + 1)\n"
                "    for (j = 0; j < 3; j = j + 1) s = s + i + j;\n"
                "  return s;\n"
                "}\n",
                false, 18);
    compare_one(tally, dir, cl, rt, "arr",
                "int main(void) {\n"
                "  int a[4]; a[0] = 1; a[3] = 6; return a[0] + a[3];\n"
                "}\n",
                false, 7);
    compare_one(tally, dir, cl, rt, "rank",
                "int main(void) {\n"
                "  int g[2][2][2][2]; g[1][0][1][1] = 9; return g[1][0][1][1] + sizeof(g);\n"
                "}\n",
                false, 73);
    compare_one(tally, dir, cl, rt, "ptr",
                "int main(void) {\n"
                "  int x; int *p; x = 6; p = &x; *p = 11; return x;\n"
                "}\n",
                false, 11);
    compare_one(tally, dir, cl, rt, "fact",
                "int f(int n) { if (n < 2) return 1; return n * f(n - 1); }\n"
                "int main(void) { return f(5); }\n",
                false, 120);
    compare_one(tally, dir, cl, rt, "sw",
                "int main(void) {\n"
                "  int n; n = 2;\n"
                "  switch (n) { case 1: return 1; case 2: return 20; default: return 3; }\n"
                "}\n",
                false, 20);
    compare_one(tally, dir, cl, rt, "glob",
                "int g;\n"
                "int bump(void) { g = g + 1; return g; }\n"
                "int main(void) { g = 3; return bump() + bump(); }\n",
                false, 9);
    compare_one(tally, dir, cl, rt, "shadow",
                "int main(void) {\n"
                "  int x; int y; x = 1; y = 0; { int x; x = 4; y = x; } return x * 10 + y;\n"
                "}\n",
                false, 14);
    compare_one(tally, dir, cl, rt, "comma",
                "int main(void) { int x; x = (1, 2, 7); return x; }\n",
                false, 7);
    compare_one(tally, dir, cl, rt, "logic",
                "int main(void) {\n"
                "  int a; a = 1; if (a && 0) return 1; if (0 || a) return 6; return 2;\n"
                "}\n",
                false, 6);
    compare_one(tally, dir, cl, rt, "tern",
                "int main(void) { int a; a = 0; return a ? 1 : 8; }\n",
                false, 8);
    compare_one(tally, dir, cl, rt, "st",
                "struct P { int x; int y; };\n"
                "int main(void) { struct P a; struct P b; a.x = 3; a.y = 4; b = a; return b.x * 10 + b.y; }\n",
                false, 34);
    compare_one(tally, dir, cl, rt, "hello",
                "#include <stdio.h>\n"
                "int main(void) { printf(\"hi %d\\n\", 3); return 0; }\n",
                true, 0);
    return tally.finish(16);
}
