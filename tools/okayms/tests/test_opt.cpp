// /O1 drops an if whose condition is a constant. /O2 is accepted and does
// not do that.

#include "check.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

const char* kSource =
    "int dead(void) { return 9; }\n"
    "int entry(void) {\n"
    "  if (0) return dead();\n"
    "  if (1) return 4;\n"
    "  return 2;\n"
    "}\n";

int wine_image(const std::vector<uint8_t>& image) {
    char path[] = "/tmp/okayms-opt-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-opt-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

int calls_dead(const okayms::ObjectFile& object) {
    int n = 0;
    for (const auto& symbol : object.symbols) {
        if (symbol.name != "dead") continue;
        for (const auto& section : object.sections) {
            if (section.name != ".text") continue;
            for (const auto& reloc : section.relocations) {
                if (reloc.symbol == static_cast<uint32_t>(&symbol - object.symbols.data())) ++n;
            }
        }
    }
    return n;
}

}  // namespace

static void test_o1(Tally& tally) {
    std::printf("test_o1\n");
    okayms::CompileInput in;
    in.source = kSource;
    const auto plain = okayms::compile_c(in);
    CHECK(tally, plain.ok());
    if (!plain.ok()) return;
    CHECK(tally, calls_dead(plain.value.object) == 1);

    in.optimize = true;
    const auto opt = okayms::compile_c(in);
    CHECK(tally, opt.ok());
    if (!opt.ok()) return;
    CHECK(tally, calls_dead(opt.value.object) == 0);
    const auto linked = okayms::link_executable({opt.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) return;
    CHECK(tally, wine_image(linked.value) == 4);
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    Tally tally;
    test_o1(tally);
    std::printf("test_o1_flag\n");
    const std::string cl = root + "/tools/okayms/build/okay-cl";
    const int rc = std::system(("mkdir -p /tmp/okayms-opt && printf '%s' 'int entry(void) { if (0) return 1; return 4; }' > /tmp/okayms-opt/t.c && " +
                                 cl + " /O1 /Fe/tmp/okayms-opt/t.exe /tmp/okayms-opt/t.c > /tmp/okayms-opt/log 2>&1")
                                    .c_str());
    CHECK(tally, rc != -1 && WIFEXITED(rc) && WEXITSTATUS(rc) == 0);
    if (!(rc != -1 && WIFEXITED(rc) && WEXITSTATUS(rc) == 0)) {
        std::FILE* f = std::fopen("/tmp/okayms-opt/log", "r");
        if (f) {
            char buf[400];
            while (std::fgets(buf, sizeof buf, f)) std::fputs(buf, stderr);
            std::fclose(f);
        }
    }
    return tally.finish(2);
}
