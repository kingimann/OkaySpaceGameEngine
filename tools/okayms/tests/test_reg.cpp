// /O1 keeps a local in a callee-saved register and records the push.
// /O2 propagates constants and drops a store that the next store replaces.

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
    for (size_t i = 0; i + n <= data.size(); ++i)
        if (std::memcmp(data.data() + i, needle, n) == 0) return true;
    return false;
}

int wine_image(const std::vector<uint8_t>& image) {
    char path[] = "/tmp/okayms-reg-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-reg-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

}  // namespace

static void test_regalloc(Tally& tally) {
    std::printf("test_regalloc\n");
    okayms::CompileInput in;
    in.optimize = true;
    in.opt_level = 1;
    in.source =
        "int inc(int x) { return x + 1; }\n"
        "int entry(void) {\n"
        "  int n; int i;\n"
        "  n = 0;\n"
        "  for (i = 0; i < 4; i = i + 1) n = inc(n);\n"
        "  return n;\n"
        "}\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    bool saw_push = false;
    bool saw_unwind = false;
    const uint8_t rbx = 0x30;
    for (const auto& section : compiled.value.object.sections) {
        if (section.name == ".text" && !section.data.empty() && section.data[0] == 0x53) saw_push = true;
        if (section.name == ".xdata") saw_unwind = has_bytes(section.data, &rbx, 1);
    }
    CHECK(tally, saw_push);
    CHECK(tally, saw_unwind);
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) return;
    const int code = wine_image(linked.value);
    CHECK(tally, code == 4);
    if (code != 4) std::fprintf(stderr, "  wine %d\n", code);
}

static void test_o2(Tally& tally) {
    std::printf("test_o2\n");
    okayms::CompileInput in;
    in.optimize = true;
    in.opt_level = 2;
    in.source = "int entry(void) { int x; x = 1; x = 40; return x + 2; }\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    const uint8_t one[] = {0xB8, 1, 0, 0, 0};
    const uint8_t forty_two[] = {0xB8, 42, 0, 0, 0};
    bool saw_one = false;
    bool saw_answer = false;
    for (const auto& section : compiled.value.object.sections) {
        if (section.name != ".text") continue;
        saw_one = has_bytes(section.data, one, sizeof one);
        saw_answer = has_bytes(section.data, forty_two, sizeof forty_two);
    }
    CHECK(tally, !saw_one);
    CHECK(tally, saw_answer);
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) return;
    CHECK(tally, wine_image(linked.value) == 42);
}

int main() {
    Tally tally;
    test_regalloc(tally);
    test_o2(tally);
    return tally.finish(2);
}
