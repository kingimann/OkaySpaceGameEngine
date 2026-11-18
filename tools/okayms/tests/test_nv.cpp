// A function that copies a large struct uses rbx, rsi and rdi. The prologue
// saves them and the unwind codes name the same registers. Wine checks that
// the epilogue restores the frame.

#include "check.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

bool has_bytes(const std::vector<uint8_t>& data, const uint8_t* needle, size_t n) {
    if (data.size() < n) return false;
    for (size_t i = 0; i + n <= data.size(); ++i) {
        if (std::memcmp(data.data() + i, needle, n) == 0) return true;
    }
    return false;
}

int wine_image(const std::vector<uint8_t>& image) {
    char path[] = "/tmp/okayms-nv-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-nv-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

}  // namespace

static void test_saved_nonvolatiles(Tally& tally) {
    std::printf("test_saved_nonvolatiles\n");
    okayms::CompileInput in;
    in.source =
        "struct Big { int a; int b; int c; };\n"
        "int clobber(struct Big b) { return b.a + b.b + b.c; }\n"
        "int entry(void) {\n"
        "  int keep; struct Big b;\n"
        "  keep = 30;\n"
        "  b.a = 4; b.b = 5; b.c = 3;\n"
        "  keep = keep + clobber(b);\n"
        "  return keep;\n"
        "}\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    const uint8_t pushes[] = {0x53, 0x56, 0x57, 0x55};
    bool saw_text = false;
    bool saw_unwind = false;
    for (const auto& section : compiled.value.object.sections) {
        if (section.name == ".text") saw_text = has_bytes(section.data, pushes, sizeof pushes);
        if (section.name == ".xdata") {
            const uint8_t rbx = 0x30;
            const uint8_t rsi = 0x60;
            const uint8_t rdi = 0x70;
            saw_unwind = has_bytes(section.data, &rbx, 1) && has_bytes(section.data, &rsi, 1) &&
                         has_bytes(section.data, &rdi, 1);
        }
    }
    CHECK(tally, saw_text);
    CHECK(tally, saw_unwind);
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) return;
    const int code = wine_image(linked.value);
    CHECK(tally, code == 42);
    if (code != 42) std::fprintf(stderr, "  wine %d\n", code);
}

int main() {
    Tally tally;
    test_saved_nonvolatiles(tally);
    return tally.finish(1);
}
