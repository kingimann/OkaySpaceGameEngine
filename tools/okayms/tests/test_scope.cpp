// A name in a nested block is a different variable from the outer one.
// The inner slot must not overwrite the outer slot, and sizeof must see
// the variable that is in scope.

#include "check.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <cstdio>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

int wine_image(const std::vector<uint8_t>& image) {
    char path[] = "/tmp/okayms-scope-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-scope-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

}  // namespace

static void test_shadow(Tally& tally) {
    std::printf("test_shadow\n");
    okayms::CompileInput in;
    in.source =
        "int shade(int x) {\n"
        "  int y;\n"
        "  y = x + 1;\n"
        "  { int x; x = 5; y = y + x; }\n"
        "  return y * 10 + x;\n"
        "}\n"
        "int entry(void) {\n"
        "  int x; int y; int n;\n"
        "  x = 1; y = 0;\n"
        "  { int x; x = 4; y = x; }\n"
        "  if (x * 10 + y != 14) return 1;\n"
        "  x = 1;\n"
        "  { char x[8]; n = sizeof(x); }\n"
        "  if (sizeof(x) != 4) return 2;\n"
        "  if (x != 1) return 3;\n"
        "  if (n != 8) return 4;\n"
        "  if (shade(3) != 93) return 5;\n"
        "  return 14;\n"
        "}\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) return;
    const int code = wine_image(linked.value);
    CHECK(tally, code == 14);
    if (code != 14) std::fprintf(stderr, "  wine %d\n", code);
}

int main() {
    Tally tally;
    test_shadow(tally);
    return tally.finish(1);
}
