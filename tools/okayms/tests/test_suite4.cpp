// A console Game of Life, a small JSON tokenizer, and a double matrix
// multiply. Each is built with okay-cl and the runtime and run under Wine.

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
    for (const char* name : {"string", "printf", "startup", "mem"}) {
        const std::string obj = dir + "/" + name + ".obj";
        if (run(cl + " /I" + rt + " /c /Fo" + obj + " " + rt + "/" + name + ".c", dir + "/" + name + ".log") != 0) return false;
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
    for (const char* name : {"ExitProcess", "GetStdHandle", "WriteFile", "GetProcessHeap", "HeapAlloc", "HeapFree"}) {
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

void compile_run(Tally& tally, const std::string& cl, const std::string& rt, const std::string& dir, const std::string& src,
                 const std::string& expect) {
    const std::string exe = src + ".exe";
    const std::string log = src + ".log";
    const std::string out_path = src + ".out";
    const int built = run(cl + " /I" + rt + " /ENTRY:mainCRTStartup /Fe" + exe + " " + src + " " + dir + "/okayrt.lib " +
                              dir + "/kernel32.lib",
                          log);
    CHECK(tally, built == 0);
    if (built != 0) {
        std::fprintf(stderr, "%s\n", slurp(log).c_str());
        return;
    }
    const int code = run("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + exe, out_path);
    const std::string out = slurp(out_path);
    CHECK(tally, code == 0);
    CHECK(tally, out == expect);
    if (code != 0 || out != expect) std::fprintf(stderr, "  %s wine %d [%s]\n", src.c_str(), code, out.c_str());
}

}  // namespace

static void test_life_json_matrix(Tally& tally, const std::string& cl, const std::string& rt, const std::string& dir) {
    std::printf("test_life_json_matrix\n");
    CHECK(tally, write_text(dir + "/life.c",
                            "#include \"okayrt.h\"\n"
                            "char g[25];\n"
                            "char nxt[25];\n"
                            "int at(int r, int c) {\n"
                            "  if (r < 0 || c < 0 || r > 4 || c > 4) return 0;\n"
                            "  return g[r * 5 + c] == '#';\n"
                            "}\n"
                            "void step(void) {\n"
                            "  int i; int r; int c; int k;\n"
                            "  i = 0;\n"
                            "  while (i < 25) {\n"
                            "    r = i / 5; c = i % 5;\n"
                            "    k = at(r - 1, c - 1) + at(r - 1, c) + at(r - 1, c + 1);\n"
                            "    k = k + at(r, c - 1) + at(r, c + 1);\n"
                            "    k = k + at(r + 1, c - 1) + at(r + 1, c) + at(r + 1, c + 1);\n"
                            "    if (g[i] == '#' && (k == 2 || k == 3)) nxt[i] = '#';\n"
                            "    else if (g[i] != '#' && k == 3) nxt[i] = '#';\n"
                            "    else nxt[i] = '.';\n"
                            "    i = i + 1;\n"
                            "  }\n"
                            "  i = 0;\n"
                            "  while (i < 25) { g[i] = nxt[i]; i = i + 1; }\n"
                            "}\n"
                            "int main(void) {\n"
                            "  int i; int row;\n"
                            "  i = 0;\n"
                            "  while (i < 25) { g[i] = '.'; i = i + 1; }\n"
                            "  g[7] = '#'; g[12] = '#'; g[17] = '#';\n"
                            "  step();\n"
                            "  row = 0;\n"
                            "  while (row < 5) {\n"
                            "    int c; c = 0;\n"
                            "    while (c < 5) { printf(\"%c\", g[row * 5 + c]); c = c + 1; }\n"
                            "    printf(\"\\n\");\n"
                            "    row = row + 1;\n"
                            "  }\n"
                            "  return 0;\n"
                            "}\n"));
    compile_run(tally, cl, rt, dir, dir + "/life.c", ".....\n.....\n.###.\n.....\n.....\n");

    CHECK(tally, write_text(dir + "/json.c",
                            "#include \"okayrt.h\"\n"
                            "int main(void) {\n"
                            "  char *s; char tok[16]; int i; int n;\n"
                            "  s = \"{\\\"a\\\": 12}\";\n"
                            "  i = 0;\n"
                            "  while (s[i] != 0) {\n"
                            "    if (s[i] == ' ') i = i + 1;\n"
                            "    else if (s[i] == '\"') {\n"
                            "      i = i + 1; n = 0;\n"
                            "      while (s[i] != '\"' && s[i] != 0 && n < 14) { tok[n] = s[i]; n = n + 1; i = i + 1; }\n"
                            "      tok[n] = 0; if (s[i] == '\"') i = i + 1;\n"
                            "      puts(tok);\n"
                            "    } else if (s[i] >= '0' && s[i] <= '9') {\n"
                            "      n = 0;\n"
                            "      while (s[i] >= '0' && s[i] <= '9' && n < 14) { tok[n] = s[i]; n = n + 1; i = i + 1; }\n"
                            "      tok[n] = 0; puts(tok);\n"
                            "    } else {\n"
                            "      printf(\"%c\\n\", s[i]);\n"
                            "      i = i + 1;\n"
                            "    }\n"
                            "  }\n"
                            "  return 0;\n"
                            "}\n"));
    compile_run(tally, cl, rt, dir, dir + "/json.c", "{\na\n:\n12\n}\n");

    CHECK(tally, write_text(dir + "/mat.c",
                            "#include \"okayrt.h\"\n"
                            "int main(void) {\n"
                            "  double a0; double a1; double a2; double a3;\n"
                            "  double b0; double b1; double b2; double b3;\n"
                            "  double c0; double c1; double c2; double c3;\n"
                            "  a0 = 1.0; a1 = 2.0; a2 = 3.0; a3 = 4.0;\n"
                            "  b0 = 5.0; b1 = 6.0; b2 = 7.0; b3 = 8.0;\n"
                            "  c0 = a0 * b0 + a1 * b2;\n"
                            "  c1 = a0 * b1 + a1 * b3;\n"
                            "  c2 = a2 * b0 + a3 * b2;\n"
                            "  c3 = a2 * b1 + a3 * b3;\n"
                            "  printf(\"%f\\n\", c0);\n"
                            "  printf(\"%f\\n\", c1);\n"
                            "  printf(\"%f\\n\", c2);\n"
                            "  printf(\"%f\\n\", c3);\n"
                            "  return 0;\n"
                            "}\n"));
    compile_run(tally, cl, rt, dir, dir + "/mat.c", "19.000000\n22.000000\n43.000000\n50.000000\n");
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    const std::string dir = "/tmp/okayms-suite4";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    Tally tally;
    CHECK(tally, build_libs(root + "/tools/okayms/build/okay-cl", root, dir));
    test_life_json_matrix(tally, root + "/tools/okayms/build/okay-cl", root + "/tools/okayms/runtime", dir);
    return tally.finish(1);
}
