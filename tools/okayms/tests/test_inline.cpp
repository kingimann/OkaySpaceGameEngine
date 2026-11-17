// /O2 inlines a tiny function whose body is a single return, so the call
// disappears and the constants fold.

#include "check.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

int wine_image(const std::vector<uint8_t>& image) {
    char path[] = "/tmp/okayms-inline-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-inline-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

int relocs_to(const okayms::ObjectFile& object, const std::string& name) {
    uint32_t index = 0;
    bool found = false;
    for (size_t i = 0; i < object.symbols.size(); ++i) {
        if (object.symbols[i].name == name) {
            index = static_cast<uint32_t>(i);
            found = true;
        }
    }
    if (!found) return 0;
    int n = 0;
    for (const auto& section : object.sections) {
        if (section.name != ".text") continue;
        for (const auto& reloc : section.relocations)
            if (reloc.symbol == index) ++n;
    }
    return n;
}

}  // namespace

static void test_inline(Tally& tally) {
    std::printf("test_inline\n");
    okayms::CompileInput in;
    in.opt_level = 2;
    in.optimize = true;
    in.source =
        "int add(int a, int b) { return a + b; }\n"
        "int entry(void) { return add(20, 22); }\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    CHECK(tally, relocs_to(compiled.value.object, "add") == 0);
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) return;
    const int code = wine_image(linked.value);
    CHECK(tally, code == 42);
    if (code != 42) std::fprintf(stderr, "  wine %d\n", code);
}

static void test_inline_once(Tally& tally) {
    std::printf("test_inline_once\n");
    okayms::CompileInput in;
    in.opt_level = 2;
    in.optimize = true;
    in.source =
        "int g;\n"
        "int next(void) { g = g + 1; return g; }\n"
        "int add(int a, int b) {\n"
        "  int s;\n"
        "  s = a + b;\n"
        "  if (s < 0) s = 0;\n"
        "  return s;\n"
        "}\n"
        "int entry(void) {\n"
        "  int n;\n"
        "  g = 0;\n"
        "  n = add(next(), next());\n"
        "  return n * 10 + g;\n"
        "}\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    CHECK(tally, relocs_to(compiled.value.object, "add") == 0);
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) return;
    const int code = wine_image(linked.value);
    CHECK(tally, code == 32);
    if (code != 32) std::fprintf(stderr, "  wine %d\n", code);
}

int main() {
    Tally tally;
    test_inline(tally);
    test_inline_once(tally);
    return tally.finish(2);
}
