// float and double use SSE. Windows x64 passes them in xmm0-xmm3 and
// returns them in xmm0. printf %f prints six digits after the point.

#include "check.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <cstdio>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

int wine_image(const std::vector<uint8_t>& image, std::string* out) {
    char path[] = "/tmp/okayms-float-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-float-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (out != nullptr) {
        FILE* f = std::fopen("/tmp/okayms-float-wine.txt", "r");
        if (f) {
            char buf[200];
            if (std::fgets(buf, sizeof buf, f)) *out = buf;
            std::fclose(f);
        }
    }
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

}  // namespace

static void test_double_arith(Tally& tally) {
    std::printf("test_double_arith\n");
    okayms::CompileInput in;
    in.source =
        "double add(double a, double b) { return a + b; }\n"
        "int entry(void) {\n"
        "  double x; float f;\n"
        "  x = add(1.5, 2.5);\n"
        "  f = 1.5f;\n"
        "  if (!(x > 3.9 && x < 4.1)) return 1;\n"
        "  if ((int)(f * 2.0f) != 3) return 2;\n"
        "  return 4;\n"
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
    const int code = wine_image(linked.value, nullptr);
    CHECK(tally, code == 4);
    if (code != 4) std::fprintf(stderr, "  wine %d\n", code);
}

int main() {
    Tally tally;
    test_double_arith(tally);
    return tally.finish(1);
}
