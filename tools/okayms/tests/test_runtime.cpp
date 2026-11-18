// The runtime is C compiled by okay-cl into a static library: memcpy, memset,
// strlen, strcmp, printf on WriteFile, and mainCRTStartup.

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

}  // namespace

static void test_runtime(Tally& tally, const std::string& cl, const std::string& root) {
    std::printf("test_runtime\n");
    const std::string dir = "/tmp/okayms-rt";
    const std::string rt = root + "/tools/okayms/runtime";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    okayms::Archive archive;
    for (const char* name : {"string", "printf", "startup", "mem"}) {
        const std::string src = rt + "/" + name + ".c";
        const std::string obj = dir + "/" + name + ".obj";
        CHECK(tally, run(cl + " /I" + rt + " /c /Fo" + obj + " " + src, dir + "/" + name + ".log") == 0);
        if (tally.fail != 0) {
            std::fprintf(stderr, "%s\n", slurp(dir + "/" + std::string(name) + ".log").c_str());
            return;
        }
        std::ifstream in(obj.c_str(), std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto object = okayms::read_coff(bytes.data(), bytes.size());
        CHECK(tally, object.ok());
        if (!object.ok()) return;
        okayms::ArchiveMember member;
        member.name = std::string(name) + ".obj";
        member.object = object.value;
        archive.members.push_back(member);
    }
    const auto lib = okayms::write_archive(archive);
    CHECK(tally, lib.ok());
    if (!lib.ok()) return;
    CHECK(tally, write_bytes(dir + "/okayrt.lib", lib.value));

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

    CHECK(tally, write_text(dir + "/hi.c",
                            "#include \"okayrt.h\"\n"
                            "int main(void) {\n"
                            "  char buf[8];\n"
                            "  memcpy(buf, \"ab\", 3);\n"
                            "  if (strcmp(buf, \"ab\") != 0) return 1;\n"
                            "  memset(buf, 65, 2);\n"
                            "  if (buf[0] != 65 || buf[1] != 65) return 2;\n"
                            "  if (strlen(\"hi\") != 2) return 3;\n"
                            "  char *p; char small[16];\n"
                            "  p = malloc(16);\n"
                            "  if (p == 0) return 4;\n"
                            "  strcpy(p, \"go\");\n"
                            "  strcat(p, \"!\");\n"
                            "  puts(p);\n"
                            "  snprintf(small, 8, \"%d\", atoi(\"-3\") * -4);\n"
                            "  printf(\"%d %s %x %c %f\\n\", -7, \"ok\", 255, 65, 1.5 + 2.5);\n"
                            "  printf(\"%s\\n\", small);\n"
                            "  free(p);\n"
                            "  return 0;\n"
                            "}\n"));
    const int built = run(cl + " /I" + rt + " /ENTRY:mainCRTStartup /Fe" + dir + "/hi.exe " + dir + "/hi.c " + dir +
                              "/okayrt.lib " + dir + "/kernel32.lib",
                          dir + "/link.log");
    CHECK(tally, built == 0);
    if (built != 0) {
        std::fprintf(stderr, "%s\n", slurp(dir + "/link.log").c_str());
        return;
    }
    const int code = run("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + dir + "/hi.exe", dir + "/out.txt");
    const std::string out = slurp(dir + "/out.txt");
    CHECK(tally, code == 0);
    CHECK(tally, out == "go!\n-7 ok ff A 4.000000\n12\n");
    if (out != "go!\n-7 ok ff A 4.000000\n12\n") std::fprintf(stderr, "  stdout: [%s]\n", out.c_str());
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    Tally tally;
    test_runtime(tally, root + "/tools/okayms/build/okay-cl", root);
    return tally.finish(1);
}
