// continue, do/while, a non-constant initializer, a four-dimensional array,
// and structs passed and returned by value.

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
    char path[] = "/tmp/okayms-lang6-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-lang6-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

}  // namespace

static void test_lang6(Tally& tally) {
    std::printf("test_lang6\n");
    okayms::CompileInput in;
    in.source =
        "struct Big { int a; int b; int c; };\n"
        "void bump(struct Big s) { s.a = s.a + 1; }\n"
        "struct Big id(struct Big s) { return s; }\n"
        "int entry(void) {\n"
        "  int n; int i; int y; int x;\n"
        "  n = 0;\n"
        "  for (i = 0; i < 5; i = i + 1) {\n"
        "    if (i == 2) continue;\n"
        "    n = n + i;\n"
        "  }\n"
        "  if (n != 8) return 1;\n"
        "  i = 0; n = 0;\n"
        "  do {\n"
        "    i = i + 1;\n"
        "    if (i == 2) continue;\n"
        "    n = n + i;\n"
        "  } while (i < 4);\n"
        "  if (n != 8) return 2;\n"
        "  y = 10;\n"
        "  x = y + 3;\n"
        "  if (x != 13) return 3;\n"
        "  int grid[2][2][2][2];\n"
        "  grid[1][0][1][1] = 7;\n"
        "  if (grid[1][0][1][1] != 7) return 4;\n"
        "  if (sizeof(grid) != 64) return 5;\n"
        "  struct Big a; struct Big b;\n"
        "  a.a = 4; a.b = 5; a.c = 6;\n"
        "  bump(a);\n"
        "  if (a.a != 4) return 6;\n"
        "  b = id(a);\n"
        "  if (b.a + b.b + b.c != 15) return 7;\n"
        "  return 9;\n"
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
    CHECK(tally, code == 9);
    if (code != 9) std::fprintf(stderr, "  wine %d\n", code);
}

int main() {
    Tally tally;
    test_lang6(tally);
    return tally.finish(1);
}
