// The compiler is ours. These tests check the COFF it writes and, where
// the program returns from entry, the exit code under Wine.

#include "check.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

int wine_exe(const std::vector<uint8_t>& image) {
    char path[] = "/tmp/okayms-cc-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const std::string cmd = "WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                            " >/tmp/okayms-cc-wine.txt 2>&1";
    const int rc = std::system(cmd.c_str());
    unlink(path);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

bool has_bytes(const std::vector<uint8_t>& data, const uint8_t* needle, size_t n) {
    if (n > data.size()) return false;
    for (size_t i = 0; i + n <= data.size(); ++i) {
        if (std::memcmp(data.data() + i, needle, n) == 0) return true;
    }
    return false;
}

okayms::Result<okayms::CompileOutput> compile(const char* source) {
    okayms::CompileInput in;
    in.source = source;
    return okayms::compile_c(in);
}

void test_return_literal(Tally& tally) {
    std::printf("test_return_literal\n");
    const auto compiled = compile("int entry(void) { return 42; }\n");
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  compile: %s\n", compiled.error.c_str());
        return;
    }
    bool saw = false;
    for (const auto& symbol : compiled.value.object.symbols) {
        if (symbol.name == "entry" && symbol.section == 1) saw = true;
    }
    CHECK(tally, saw);
    const uint8_t mov[] = {0xB8, 42, 0, 0, 0};
    CHECK(tally, !compiled.value.object.sections.empty());
    CHECK(tally, has_bytes(compiled.value.object.sections[0].data, mov, sizeof mov));
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  link: %s\n", linked.error.c_str());
        return;
    }
    CHECK(tally, wine_exe(linked.value) == 42);
}

void test_add_and_loop(Tally& tally) {
    std::printf("test_add_and_loop\n");
    const auto add = compile("int entry(void) { return 20 + 22; }\n");
    CHECK(tally, add.ok());
    if (!add.ok()) {
        std::fprintf(stderr, "  compile: %s\n", add.error.c_str());
        return;
    }
    const auto add_exe = okayms::link_executable({add.value.object}, {});
    CHECK(tally, add_exe.ok() && wine_exe(add_exe.value) == 42);

    const char* loop =
        "int entry(void) {\n"
        "  int s;\n"
        "  int i;\n"
        "  s = 0;\n"
        "  i = 1;\n"
        "  while (i <= 10) {\n"
        "    s = s + i;\n"
        "    i = i + 1;\n"
        "  }\n"
        "  return s;\n"
        "}\n";
    const auto summed = compile(loop);
    CHECK(tally, summed.ok());
    if (!summed.ok()) {
        std::fprintf(stderr, "  compile: %s\n", summed.error.c_str());
        return;
    }
    const auto exe = okayms::link_executable({summed.value.object}, {});
    CHECK(tally, exe.ok());
    if (!exe.ok()) {
        std::fprintf(stderr, "  link: %s\n", exe.error.c_str());
        return;
    }
    const int code = wine_exe(exe.value);
    CHECK(tally, code == 55);
    if (code != 55) std::fprintf(stderr, "  wine exit %d\n", code);
}

void test_define_and_include(Tally& tally) {
    std::printf("test_define_and_include\n");
    okayms::CompileInput off;
    off.source = "#ifdef FLAG\nint entry(void){return 7;}\n#else\nint entry(void){return 1;}\n#endif\n";
    const auto a = okayms::compile_c(off);
    CHECK(tally, a.ok());
    if (a.ok()) {
        const auto exe = okayms::link_executable({a.value.object}, {});
        CHECK(tally, exe.ok() && wine_exe(exe.value) == 1);
    }
    off.defines.push_back("FLAG");
    const auto b = okayms::compile_c(off);
    CHECK(tally, b.ok());
    if (b.ok()) {
        const auto exe = okayms::link_executable({b.value.object}, {});
        CHECK(tally, exe.ok() && wine_exe(exe.value) == 7);
    }

    const std::string dir = "/tmp/okayms-include";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    {
        std::ofstream hdr((dir + "/n.h").c_str());
        hdr << "#define ANSWER 42\n";
    }
    okayms::CompileInput inc;
    inc.source = "#include \"n.h\"\nint entry(void){return ANSWER;}\n";
    inc.include_paths.push_back(dir);
    const auto c = okayms::compile_c(inc);
    CHECK(tally, c.ok());
    if (!c.ok()) {
        std::fprintf(stderr, "  include: %s\n", c.error.c_str());
        return;
    }
    const auto exe = okayms::link_executable({c.value.object}, {});
    CHECK(tally, exe.ok() && wine_exe(exe.value) == 42);
}

}  // namespace

void test_driver(Tally& tally, const std::string& driver) {
    std::printf("test_driver\n");
    const std::string dir = "/tmp/okayms-cl";
    std::system(("rm -rf " + dir + " && mkdir -p " + dir).c_str());
    {
        std::ofstream hdr((dir + "/n.h").c_str());
        hdr << "#define ANSWER 42\n";
    }
    {
        std::ofstream src((dir + "/t.c").c_str());
        src << "#include \"n.h\"\n#ifdef FLAG\nint entry(void){return ANSWER;}\n#else\nint entry(void){return 1;}\n#endif\n";
    }
    const std::string cmd = driver + " /c /Fo" + dir + "/t.obj /I" + dir + " /D FLAG " + dir + "/t.c";
    const int rc = std::system((cmd + " >/tmp/okayms-cl-out.txt 2>&1").c_str());
    CHECK(tally, rc != -1 && WIFEXITED(rc) && WEXITSTATUS(rc) == 0);
    if (!(rc != -1 && WIFEXITED(rc) && WEXITSTATUS(rc) == 0)) {
        std::ifstream err("/tmp/okayms-cl-out.txt");
        std::string text((std::istreambuf_iterator<char>(err)), {});
        std::fprintf(stderr, "  cl: %s\n", text.c_str());
        return;
    }
    std::ifstream in((dir + "/t.obj").c_str(), std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    const auto object = okayms::read_coff(bytes.data(), bytes.size());
    CHECK(tally, object.ok());
    if (!object.ok()) return;
    const auto exe = okayms::link_executable({object.value}, {});
    CHECK(tally, exe.ok() && wine_exe(exe.value) == 42);

    const std::string link_cmd = driver + " /Fe" + dir + "/t.exe /I" + dir + " /D FLAG " + dir + "/t.c";
    const int linked = std::system((link_cmd + " >/tmp/okayms-cl-link.txt 2>&1").c_str());
    CHECK(tally, linked != -1 && WIFEXITED(linked) && WEXITSTATUS(linked) == 0);
    std::ifstream exe_in((dir + "/t.exe").c_str(), std::ios::binary);
    std::vector<uint8_t> image((std::istreambuf_iterator<char>(exe_in)), {});
    CHECK(tally, wine_exe(image) == 42);
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    Tally tally;
    test_return_literal(tally);
    test_add_and_loop(tally);
    test_define_and_include(tally);
    test_driver(tally, root + "/tools/okayms/build/okay-cl");
    return tally.finish(4);
}
