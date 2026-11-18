// A loop that calls, with more integer locals than the callee-saved
// registers. The extras live in caller-saved registers and are written
// back to the stack around the call. The sum is still right.

#include "check.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

int wine_image(const std::vector<uint8_t>& image) {
    char path[] = "/tmp/okayms-spill-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-spill-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

int count_triple(const std::vector<uint8_t>& text, uint8_t a, uint8_t b, uint8_t c) {
    int n = 0;
    for (size_t i = 0; i + 2 < text.size(); ++i)
        if (text[i] == a && text[i + 1] == b && text[i + 2] == c) ++n;
    return n;
}

}  // namespace

static void test_spill(Tally& tally) {
    std::printf("test_spill\n");
    okayms::CompileInput in;
    in.optimize = true;
    in.opt_level = 1;
    in.source =
        "int tick(int x) {\n"
        "  int p; int q; int r; int s; int t; int u; int v; int w;\n"
        "  p = 1; q = 1; r = 1; s = 1; t = 1; u = 1; v = 1; w = x;\n"
        "  return p + q + r + s + t + u + v + w;\n"
        "}\n"
        "int entry(void) {\n"
        "  int a; int b; int c; int d; int e; int f; int g; int h; int i;\n"
        "  a = 1; b = 1; c = 1; d = 1; e = 1; f = 1; g = 1; h = 1;\n"
        "  for (i = 0; i < 3; i = i + 1) a = tick(a);\n"
        "  return a + b + c + d + e + f + g + h;\n"
        "}\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    const std::vector<uint8_t>* text = nullptr;
    for (const auto& section : compiled.value.object.sections)
        if (section.name == ".text") text = &section.data;
    // mov [rbp+disp32], r8d. That is the spill of a local kept in r8.
    CHECK(tally, text != nullptr && count_triple(*text, 0x44, 0x89, 0x85) >= 1);
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) return;
    const int code = wine_image(linked.value);
    CHECK(tally, code == 29);
    if (code != 29) std::fprintf(stderr, "  wine %d\n", code);
}

int main() {
    Tally tally;
    test_spill(tally);
    return tally.finish(1);
}
