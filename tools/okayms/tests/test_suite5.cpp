// A text-adventure state machine, a sorted list built with malloc, and an
// ASCII mandelbrot drawn with doubles. Each is built with okay-cl and run
// under Wine.

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

static void test_adventure_list_mandelbrot(Tally& tally, const std::string& cl, const std::string& rt, const std::string& dir) {
    std::printf("test_adventure_list_mandelbrot\n");
    CHECK(tally, write_text(dir + "/adv.c",
                            "#include \"okayrt.h\"\n"
                            "int room;\n"
                            "int lamp;\n"
                            "void look(void) {\n"
                            "  if (room == 0) puts(\"hall\");\n"
                            "  else puts(\"cave\");\n"
                            "}\n"
                            "void go(int dir) {\n"
                            "  if (dir == 1 && room == 0) { room = 1; puts(\"cave\"); }\n"
                            "  else if (dir == 2 && room == 1) { room = 0; puts(\"hall\"); }\n"
                            "  else puts(\"no\");\n"
                            "}\n"
                            "void take(void) {\n"
                            "  if (room == 1 && lamp == 0) { lamp = 1; puts(\"lamp\"); }\n"
                            "  else puts(\"no\");\n"
                            "}\n"
                            "int main(void) {\n"
                            "  room = 0; lamp = 0;\n"
                            "  look();\n"
                            "  go(1);\n"
                            "  take();\n"
                            "  go(2);\n"
                            "  if (lamp) puts(\"lit\");\n"
                            "  else puts(\"dark\");\n"
                            "  return 0;\n"
                            "}\n"));
    compile_run(tally, cl, rt, dir, dir + "/adv.c", "hall\ncave\nlamp\nhall\nlit\n");

    CHECK(tally, write_text(dir + "/list.c",
                            "#include \"okayrt.h\"\n"
                            "struct Item { int value; struct Item *next; };\n"
                            "struct Item *head;\n"
                            "void add(int v) {\n"
                            "  struct Item *node; struct Item *prev; struct Item *cur;\n"
                            "  node = malloc(sizeof(struct Item));\n"
                            "  node->value = v;\n"
                            "  node->next = 0;\n"
                            "  prev = 0;\n"
                            "  cur = head;\n"
                            "  while (cur != 0 && cur->value < v) { prev = cur; cur = cur->next; }\n"
                            "  if (prev == 0) { node->next = head; head = node; }\n"
                            "  else { node->next = cur; prev->next = node; }\n"
                            "}\n"
                            "int main(void) {\n"
                            "  struct Item *cur; int first;\n"
                            "  head = 0;\n"
                            "  add(3); add(1); add(4); add(2);\n"
                            "  cur = head; first = 1;\n"
                            "  while (cur != 0) {\n"
                            "    if (first == 0) printf(\" \");\n"
                            "    printf(\"%d\", cur->value);\n"
                            "    first = 0;\n"
                            "    cur = cur->next;\n"
                            "  }\n"
                            "  printf(\"\\n\");\n"
                            "  return 0;\n"
                            "}\n"));
    compile_run(tally, cl, rt, dir, dir + "/list.c", "1 2 3 4\n");

    CHECK(tally, write_text(dir + "/mandel.c",
                            "#include \"okayrt.h\"\n"
                            "int main(void) {\n"
                            "  int y; int x;\n"
                            "  for (y = 0; y < 8; y = y + 1) {\n"
                            "    for (x = 0; x < 24; x = x + 1) {\n"
                            "      double cr; double ci; double zr; double zi;\n"
                            "      int n; int ink;\n"
                            "      cr = -2.0 + 3.0 * x / 23.0;\n"
                            "      ci = -1.0 + 2.0 * y / 7.0;\n"
                            "      zr = 0.0; zi = 0.0; ink = 32;\n"
                            "      for (n = 0; n < 16; n = n + 1) {\n"
                            "        double nzr; double nzi;\n"
                            "        nzr = zr * zr - zi * zi + cr;\n"
                            "        nzi = 2.0 * zr * zi + ci;\n"
                            "        zr = nzr; zi = nzi;\n"
                            "        if (zr * zr + zi * zi > 4.0) { ink = 46; n = 16; }\n"
                            "      }\n"
                            "      if (ink == 32) ink = 35;\n"
                            "      printf(\"%c\", ink);\n"
                            "    }\n"
                            "    printf(\"\\n\");\n"
                            "  }\n"
                            "  return 0;\n"
                            "}\n"));
    compile_run(tally, cl, rt, dir, dir + "/mandel.c",
                "........................\n"
                "..............##........\n"
                "...........########.....\n"
                "......#############.....\n"
                "......#############.....\n"
                "...........########.....\n"
                "..............##........\n"
                "........................\n");
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    const std::string dir = "/tmp/okayms-suite5";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    Tally tally;
    CHECK(tally, build_libs(root + "/tools/okayms/build/okay-cl", root, dir));
    test_adventure_list_mandelbrot(tally, root + "/tools/okayms/build/okay-cl", root + "/tools/okayms/runtime", dir);
    return tally.finish(1);
}
