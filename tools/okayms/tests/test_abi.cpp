// A struct bigger than 8 bytes is passed as a pointer to a copy the caller
// made. MinGW's smash writes through that pointer. The caller's object must
// stay unchanged, and take must still see the original fields.

#include "check.hpp"
#include "okayms/coff.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

using okayms_test::Tally;

int run(const std::string& cmd) {
    const int rc = std::system(cmd.c_str());
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

int wine_image(const std::vector<uint8_t>& image) {
    char path[] = "/tmp/okayms-abi-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-abi-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

}  // namespace

static void test_mingw_struct(Tally& tally) {
    std::printf("test_mingw_struct\n");
    if (run("command -v x86_64-w64-mingw32-gcc >/dev/null") != 0) {
        std::printf("skipping struct interop: MinGW gcc is not installed\n");
        return;
    }
    const std::string dir = "/tmp/okayms-abi";
    run("rm -rf " + dir + " && mkdir -p " + dir);
    {
        std::ofstream out((dir + "/smash.c").c_str());
        out << "struct Big { int a; int b; int c; };\n"
               "void smash(struct Big s) { s.a = 99; }\n"
               "int take(struct Big s) { return s.a + s.b + s.c; }\n";
    }
    CHECK(tally, run("x86_64-w64-mingw32-gcc -c -O0 -o " + dir + "/smash.o " + dir + "/smash.c") == 0);
    std::ifstream in((dir + "/smash.o").c_str(), std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto mingw = okayms::read_coff(bytes.data(), bytes.size());
    CHECK(tally, mingw.ok());
    if (!mingw.ok()) return;
    okayms::CompileInput input;
    input.source =
        "struct Big { int a; int b; int c; };\n"
        "void smash(struct Big s);\n"
        "int take(struct Big s);\n"
        "int entry(void) {\n"
        "  struct Big s;\n"
        "  s.a = 1; s.b = 2; s.c = 3;\n"
        "  smash(s);\n"
        "  if (s.a != 1) return 1;\n"
        "  return take(s);\n"
        "}\n";
    const auto compiled = okayms::compile_c(input);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    const auto linked = okayms::link_executable({compiled.value.object, mingw.value}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  %s\n", linked.error.c_str());
        return;
    }
    const int code = wine_image(linked.value);
    CHECK(tally, code == 6);
    if (code != 6) std::fprintf(stderr, "  wine %d\n", code);
}

int main() {
    Tally tally;
    test_mingw_struct(tally);
    return tally.finish(1);
}
