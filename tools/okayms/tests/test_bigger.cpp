// Larger programs on the runtime: quicksort, a hash table, a small
// expression parser, and a program built from three objects plus a DLL.

#include "check.hpp"
#include "okayms/archive.hpp"
#include "okayms/coff.hpp"

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

bool build_libs(const std::string& cl, const std::string& root, const std::string& dir) {
    const std::string rt = root + "/tools/okayms/runtime";
    okayms::Archive archive;
    for (const char* name : {"string", "printf", "startup"}) {
        const std::string obj = dir + "/" + name + ".obj";
        if (run(cl + " /I" + rt + " /c /Fo" + obj + " " + rt + "/" + name + ".c", dir + "/" + name + ".log") != 0)
            return false;
        std::ifstream in(obj.c_str(), std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto object = okayms::read_coff(bytes.data(), bytes.size());
        if (!object.ok()) return false;
        okayms::ArchiveMember member;
        member.name = std::string(name) + ".obj";
        member.object = object.value;
        archive.members.push_back(member);
    }
    const auto lib = okayms::write_archive(archive);
    if (!lib.ok() || !write_bytes(dir + "/okayrt.lib", lib.value)) return false;
    okayms::Archive kernel;
    for (const char* name : {"ExitProcess", "GetStdHandle", "WriteFile"}) {
        okayms::ArchiveMember member;
        member.name = name;
        member.is_import = true;
        member.import.symbol = name;
        member.import.dll = "kernel32.dll";
        kernel.members.push_back(member);
    }
    const auto kbytes = okayms::write_archive(kernel);
    return kbytes.ok() && write_bytes(dir + "/kernel32.lib", kbytes.value);
}

bool compile_run(Tally& tally, const std::string& cl, const std::string& rt, const std::string& dir,
                 const std::string& src, const std::string& extra, const std::string& expect, int code_want) {
    const int built = run(cl + " /I" + rt + " /ENTRY:mainCRTStartup /Fe" + dir + "/prog.exe " + src + " " + extra + " " +
                              dir + "/okayrt.lib " + dir + "/kernel32.lib",
                          dir + "/link.log");
    CHECK(tally, built == 0);
    if (built != 0) {
        std::fprintf(stderr, "%s\n", slurp(dir + "/link.log").c_str());
        return false;
    }
    const int code = run("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + dir + "/prog.exe", dir + "/out.txt");
    const std::string out = slurp(dir + "/out.txt");
    CHECK(tally, code == code_want);
    CHECK(tally, out == expect);
    if (code != code_want || out != expect)
        std::fprintf(stderr, "  wine %d stdout [%s]\n%s\n", code, out.c_str(), slurp(dir + "/link.log").c_str());
    return code == code_want && out == expect;
}

}  // namespace

static void test_sort_hash_calc(Tally& tally, const std::string& cl, const std::string& rt, const std::string& dir) {
    std::printf("test_sort_hash_calc\n");
    CHECK(tally, write_text(dir + "/sort.c",
                            "#include \"okayrt.h\"\n"
                            "void swap(int *a, int *b) { int t; t = *a; *a = *b; *b = t; }\n"
                            "int part(int *a, int lo, int hi) {\n"
                            "  int pivot; int i; int j;\n"
                            "  pivot = a[hi]; i = lo;\n"
                            "  for (j = lo; j < hi; j = j + 1) {\n"
                            "    if (a[j] < pivot) { swap(&a[i], &a[j]); i = i + 1; }\n"
                            "  }\n"
                            "  swap(&a[i], &a[hi]);\n"
                            "  return i;\n"
                            "}\n"
                            "void sort(int *a, int lo, int hi) {\n"
                            "  int p;\n"
                            "  if (lo >= hi) return;\n"
                            "  p = part(a, lo, hi);\n"
                            "  sort(a, lo, p - 1);\n"
                            "  sort(a, p + 1, hi);\n"
                            "}\n"
                            "int main(void) {\n"
                            "  int a[6]; int i;\n"
                            "  a[0] = 5; a[1] = 1; a[2] = 4; a[3] = 2; a[4] = 9; a[5] = 3;\n"
                            "  sort(a, 0, 5);\n"
                            "  for (i = 0; i < 6; i = i + 1) printf(\"%d \", a[i]);\n"
                            "  printf(\"\\n\");\n"
                            "  return 0;\n"
                            "}\n"));
    compile_run(tally, cl, rt, dir, dir + "/sort.c", "", "1 2 3 4 5 9 \n", 0);

    CHECK(tally, write_text(dir + "/hash.c",
                            "#include \"okayrt.h\"\n"
                            "struct Slot { int used; int key; int value; };\n"
                            "struct Slot table[8];\n"
                            "void put(int key, int value) {\n"
                            "  unsigned int h; int n;\n"
                            "  h = key % 8; n = 0;\n"
                            "  while (n < 8) {\n"
                            "    if (table[h].used == 0 || table[h].key == key) {\n"
                            "      table[h].used = 1; table[h].key = key; table[h].value = value;\n"
                            "      return;\n"
                            "    }\n"
                            "    h = (h + 1) % 8;\n"
                            "    n = n + 1;\n"
                            "  }\n"
                            "}\n"
                            "int get(int key) {\n"
                            "  unsigned int h; int n;\n"
                            "  h = key % 8; n = 0;\n"
                            "  while (n < 8) {\n"
                            "    if (table[h].used == 1 && table[h].key == key) return table[h].value;\n"
                            "    h = (h + 1) % 8;\n"
                            "    n = n + 1;\n"
                            "  }\n"
                            "  return 0;\n"
                            "}\n"
                            "int main(void) {\n"
                            "  put(3, 10); put(11, 20); put(4, 12);\n"
                            "  printf(\"%d\\n\", get(3) + get(11) + get(4));\n"
                            "  return 0;\n"
                            "}\n"));
    compile_run(tally, cl, rt, dir, dir + "/hash.c", "", "42\n", 0);

    CHECK(tally, write_text(dir + "/calc.c",
                            "#include \"okayrt.h\"\n"
                            "char *p;\n"
                            "int parse_expr(void);\n"
                            "int parse_factor(void) {\n"
                            "  int v;\n"
                            "  if (p[0] == '(') { p = p + 1; v = parse_expr(); p = p + 1; return v; }\n"
                            "  v = 0;\n"
                            "  while (p[0] >= '0' && p[0] <= '9') { v = v * 10 + (p[0] - '0'); p = p + 1; }\n"
                            "  return v;\n"
                            "}\n"
                            "int parse_term(void) {\n"
                            "  int v; char op;\n"
                            "  v = parse_factor();\n"
                            "  while (p[0] == '*' || p[0] == '/') {\n"
                            "    op = p[0]; p = p + 1;\n"
                            "    if (op == '*') v = v * parse_factor();\n"
                            "    else v = v / parse_factor();\n"
                            "  }\n"
                            "  return v;\n"
                            "}\n"
                            "int parse_expr(void) {\n"
                            "  int v; char op;\n"
                            "  v = parse_term();\n"
                            "  while (p[0] == '+' || p[0] == '-') {\n"
                            "    op = p[0]; p = p + 1;\n"
                            "    if (op == '+') v = v + parse_term();\n"
                            "    else v = v - parse_term();\n"
                            "  }\n"
                            "  return v;\n"
                            "}\n"
                            "int main(void) {\n"
                            "  int v;\n"
                            "  p = \"(2+3)*4\";\n"
                            "  v = parse_expr();\n"
                            "  printf(\"%d\\n\", v);\n"
                            "  return 0;\n"
                            "}\n"));
    compile_run(tally, cl, rt, dir, dir + "/calc.c", "", "20\n", 0);
}

static void test_three_and_dll(Tally& tally, const std::string& cl, const std::string& rt, const std::string& dir) {
    std::printf("test_three_and_dll\n");
    CHECK(tally, write_text(dir + "/a.c", "int from_a(void) { return 1; }\n"));
    CHECK(tally, write_text(dir + "/b.c", "int from_b(void) { return 2; }\n"));
    CHECK(tally, write_text(dir + "/d.c",
                            "__declspec(dllexport) int from_d(void) { return 39; }\n"
                            "int entry(void) { return 1; }\n"));
    CHECK(tally, write_text(dir + "/c.c",
                            "#include \"okayrt.h\"\n"
                            "int from_a(void);\n"
                            "int from_b(void);\n"
                            "__declspec(dllimport) int from_d(void);\n"
                            "int main(void) {\n"
                            "  printf(\"%d\\n\", from_a() + from_b() + from_d());\n"
                            "  return 0;\n"
                            "}\n"));
    CHECK(tally, run(cl + " /c /Fo" + dir + "/a.obj " + dir + "/a.c", dir + "/a.log") == 0);
    CHECK(tally, run(cl + " /c /Fo" + dir + "/b.obj " + dir + "/b.c", dir + "/b.log") == 0);
    CHECK(tally, run(cl + " /LD /Fe" + dir + "/addend.dll " + dir + "/d.c", dir + "/d.log") == 0);
    okayms::Archive imp;
    okayms::ArchiveMember member;
    member.name = "from_d";
    member.is_import = true;
    member.import.symbol = "from_d";
    member.import.dll = "addend.dll";
    imp.members.push_back(member);
    const auto bytes = okayms::write_archive(imp);
    CHECK(tally, bytes.ok() && write_bytes(dir + "/addend.lib", bytes.value));
    compile_run(tally, cl, rt, dir, dir + "/c.c", dir + "/a.obj " + dir + "/b.obj " + dir + "/addend.lib", "42\n", 0);
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    const std::string dir = "/tmp/okayms-big";
    const std::string cl = root + "/tools/okayms/build/okay-cl";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    Tally tally;
    CHECK(tally, build_libs(cl, root, dir));
    test_sort_hash_calc(tally, cl, root + "/tools/okayms/runtime", dir);
    test_three_and_dll(tally, cl, root + "/tools/okayms/runtime", dir);
    return tally.finish(2);
}
