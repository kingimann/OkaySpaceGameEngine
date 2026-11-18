// Multi-dimensional arrays, initializer lists, character escapes, and an
// enum used as a switch case. Locals and globals both take { } lists.

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
    char path[] = "/tmp/okayms-init-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-init-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

}  // namespace

static void test_inits(Tally& tally) {
    std::printf("test_inits\n");
    okayms::CompileInput in;
    in.source =
        "struct Pair { int x; int y; };\n"
        "enum Color { RED = 1, BLUE = 3 };\n"
        "int glob[2][2] = {{1, 2}, {3, 4}};\n"
        "struct Pair gp = {3, 9};\n"
        "int entry(void) {\n"
        "  int grid[2][3] = {{1, 2, 3}, {4, 5, 6}};\n"
        "  struct Pair p = {10, 20};\n"
        "  int n;\n"
        "  if (grid[1][2] != 6) return 1;\n"
        "  if (sizeof(grid) != 24) return 2;\n"
        "  if (p.x + p.y != 30) return 3;\n"
        "  if (glob[1][1] != 4) return 4;\n"
        "  if (gp.x + gp.y != 12) return 5;\n"
        "  if ('A' != 65 || '\\n' != 10) return 6;\n"
        "  if (\"A\\x42\\n\"[1] != 'B') return 7;\n"
        "  if (\"A\\x42\\n\"[2] != 10) return 8;\n"
        "  n = BLUE;\n"
        "  switch (n) {\n"
        "    case RED: return 9;\n"
        "    case BLUE: break;\n"
        "    default: return 10;\n"
        "  }\n"
        "  return 11;\n"
        "}\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  %s\n", linked.error.c_str());
        return;
    }
    const int code = wine_image(linked.value);
    CHECK(tally, code == 11);
    if (code != 11) std::fprintf(stderr, "  wine %d\n", code);
}

int main() {
    Tally tally;
    test_inits(tally);
    return tally.finish(1);
}
