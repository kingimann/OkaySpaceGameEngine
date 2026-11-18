// Windows x64 puts a varargs double in the xmm register and in the matching
// integer register. The second and third doubles are xmm2/r8 and xmm3/r9.
// printf %f reads those integer homes, so two or more %f arguments must all
// print.

#include "check.hpp"
#include "okayms/archive.hpp"
#include "okayms/coff.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

using okayms_test::Tally;

int wine_image(const std::vector<uint8_t>& image, std::string* out) {
    char path[] = "/tmp/okayms-vararg-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-vararg-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (out != nullptr) {
        FILE* f = std::fopen("/tmp/okayms-vararg-wine.txt", "r");
        out->clear();
        if (f) {
            char buf[400];
            while (std::fgets(buf, sizeof buf, f)) *out += buf;
            std::fclose(f);
        }
    }
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
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

static void test_three_doubles(Tally& tally) {
    std::printf("test_three_doubles\n");
    okayms::CompileInput in;
    in.source =
        "typedef char *va_list;\n"
        "int two(int n, ...) {\n"
        "  va_list ap; double a; double b; double c;\n"
        "  va_start(ap, n);\n"
        "  a = va_arg(ap, double);\n"
        "  b = va_arg(ap, double);\n"
        "  c = va_arg(ap, double);\n"
        "  if (!(a > 1.4 && a < 1.6)) return 1;\n"
        "  if (!(b > 2.4 && b < 2.6)) return 2;\n"
        "  if (!(c > 3.4 && c < 3.6)) return 3;\n"
        "  return 9;\n"
        "}\n"
        "int entry(void) { return two(3, 1.5, 2.5, 3.5); }\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) return;
    const int code = wine_image(linked.value, nullptr);
    CHECK(tally, code == 9);
    if (code != 9) std::fprintf(stderr, "  wine %d\n", code);
}

static void test_printf_two_f(Tally& tally, const std::string& cl, const std::string& root) {
    std::printf("test_printf_two_f\n");
    const std::string dir = "/tmp/okayms-vararg";
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
    CHECK(tally, write_text(dir + "/pf.c",
                            "#include \"okayrt.h\"\n"
                            "int main(void) {\n"
                            "  printf(\"%f %f %f\\n\", 1.5, 2.5, 3.5);\n"
                            "  return 0;\n"
                            "}\n"));
    const int built = run(cl + " /I" + rt + " /ENTRY:mainCRTStartup /Fe" + dir + "/pf.exe " + dir + "/pf.c " + dir +
                              "/okayrt.lib " + dir + "/kernel32.lib",
                          dir + "/link.log");
    CHECK(tally, built == 0);
    if (built != 0) {
        std::fprintf(stderr, "%s\n", slurp(dir + "/link.log").c_str());
        return;
    }
    const int code = run("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + dir + "/pf.exe", dir + "/out.txt");
    const std::string out = slurp(dir + "/out.txt");
    CHECK(tally, code == 0);
    CHECK(tally, out == "1.500000 2.500000 3.500000\n");
    if (out != "1.500000 2.500000 3.500000\n") std::fprintf(stderr, "  stdout: [%s]\n", out.c_str());
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    Tally tally;
    test_three_doubles(tally);
    test_printf_two_f(tally, root + "/tools/okayms/build/okay-cl", root);
    return tally.finish(2);
}
