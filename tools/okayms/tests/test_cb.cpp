// A callback sort, and a dense switch compiled as a jump table.

#include "check.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

int wine_image(const std::vector<uint8_t>& image) {
    char path[] = "/tmp/okayms-cb-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-cb-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

int expect(Tally& tally, const char* source, int code, bool table) {
    okayms::CompileInput in;
    in.source = source;
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return 1;
    }
    if (table) {
        bool saw = false;
        for (const auto& section : compiled.value.object.sections) {
            if (section.name != ".text") continue;
            for (size_t i = 0; i + 1 < section.data.size(); ++i)
                if (section.data[i] == 0xFF && section.data[i + 1] == 0xE0) saw = true;
        }
        CHECK(tally, saw);
    }
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) return 1;
    const int got = wine_image(linked.value);
    CHECK(tally, got == code);
    if (got != code) std::fprintf(stderr, "  wine %d\n", got);
    return 0;
}

}  // namespace

static void test_qsort(Tally& tally) {
    std::printf("test_qsort\n");
    expect(tally,
           "void sort(int *a, int n, int (*less)(int, int)) {\n"
           "  int i; int j; int tmp;\n"
           "  for (i = 0; i < n; i = i + 1) {\n"
           "    for (j = i + 1; j < n; j = j + 1) {\n"
           "      if (less(a[j], a[i])) { tmp = a[i]; a[i] = a[j]; a[j] = tmp; }\n"
           "    }\n"
           "  }\n"
           "}\n"
           "int ascending(int x, int y) { return x < y; }\n"
           "int entry(void) {\n"
           "  int a[4];\n"
           "  a[0] = 3; a[1] = 1; a[2] = 4; a[3] = 2;\n"
           "  sort(a, 4, ascending);\n"
           "  if (a[0] != 1 || a[1] != 2 || a[2] != 3 || a[3] != 4) return 1;\n"
           "  return 7;\n"
           "}\n",
           7, false);
}

static void test_jump(Tally& tally) {
    std::printf("test_jump\n");
    expect(tally,
           "int entry(void) {\n"
           "  int n; n = 3;\n"
           "  switch (n) {\n"
           "    case 0: return 1;\n"
           "    case 1: return 2;\n"
           "    case 2: return 3;\n"
           "    case 3: return 4;\n"
           "    default: return 9;\n"
           "  }\n"
           "}\n",
           4, true);
}

int main() {
    Tally tally;
    test_qsort(tally);
    test_jump(tally);
    return tally.finish(2);
}
