// A nested for that calls another function on every iteration. If the
// back-edge included the prologue, or a call grew the frame, 1600 calls
// would walk off the stack instead of returning 7.

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
    char path[] = "/tmp/okayms-loop-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-loop-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

}  // namespace

static void test_nested_for(Tally& tally) {
    std::printf("test_nested_for\n");
    okayms::CompileInput in;
    in.source =
        "int bump(int x) { return x + 1; }\n"
        "int entry(void) {\n"
        "  int r; int c; int s;\n"
        "  s = 0;\n"
        "  for (r = 0; r < 40; r = r + 1)\n"
        "    for (c = 0; c < 40; c = c + 1)\n"
        "      s = s + bump(r + c);\n"
        "  if (s != 64000) return 1;\n"
        "  return 7;\n"
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
    CHECK(tally, code == 7);
    if (code != 7) std::fprintf(stderr, "  wine %d\n", code);
}

int main() {
    Tally tally;
    test_nested_for(tally);
    return tally.finish(1);
}
