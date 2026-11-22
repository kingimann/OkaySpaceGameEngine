// snprintf and printf share one formatter. %f %x %c %u and a width or flag
// must agree, including a field that is wider than the number.

#include "check.hpp"
#include "okayms/archive.hpp"
#include "okayms/coff.hpp"

#include <cstdio>
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

}  // namespace

static void test_shared_format(Tally& tally, const std::string& cl, const std::string& root) {
    std::printf("test_shared_format\n");
    const std::string dir = "/tmp/okayms-format";
    const std::string rt = root + "/tools/okayms/runtime";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    okayms::Archive archive;
    for (const char* name : {"string", "printf", "startup", "mem"}) {
        const std::string obj = dir + "/" + name + ".obj";
        if (run(cl + " /I" + rt + " /c /Fo" + obj + " " + rt + "/" + name + ".c", dir + "/" + name + ".log") != 0) {
            std::fprintf(stderr, "%s\n", slurp(dir + "/" + std::string(name) + ".log").c_str());
            CHECK(tally, false);
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
    CHECK(tally, write_text(dir + "/fmt.c",
                            "#include \"okayrt.h\"\n"
                            "int main(void) {\n"
                            "  char buf[64];\n"
                            "  char via[64];\n"
                            "  snprintf(buf, 64, \"%f %x %c %u\", 1.5, 255, 65, 5);\n"
                            "  printf(\"%s\\n\", buf);\n"
                            "  snprintf(buf, 64, \"%4d %-4d %02x %+d\", 7, 7, 10, 3);\n"
                            "  printf(\"%s\\n\", buf);\n"
                            "  printf(\"%4d %-4d %02x %+d\\n\", 7, 7, 10, 3);\n"
                            "  snprintf(via, 4, \"%s\", \"hello\");\n"
                            "  printf(\"%s\\n\", via);\n"
                            "  return 0;\n"
                            "}\n"));
    const int built = run(cl + " /I" + rt + " /ENTRY:mainCRTStartup /Fe" + dir + "/fmt.exe " + dir + "/fmt.c " + dir +
                              "/okayrt.lib " + dir + "/kernel32.lib",
                          dir + "/link.log");
    CHECK(tally, built == 0);
    if (built != 0) {
        std::fprintf(stderr, "%s\n", slurp(dir + "/link.log").c_str());
        return;
    }
    const int code = run("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + dir + "/fmt.exe", dir + "/out.txt");
    const std::string out = slurp(dir + "/out.txt");
    const char* expect = "1.500000 ff A 5\n   7 7    0a +3\n   7 7    0a +3\nhel\n";
    CHECK(tally, code == 0);
    CHECK(tally, out == expect);
    if (out != expect) std::fprintf(stderr, "  stdout: [%s]\n", out.c_str());
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    Tally tally;
    test_shared_format(tally, root + "/tools/okayms/build/okay-cl", root);
    return tally.finish(1);
}
