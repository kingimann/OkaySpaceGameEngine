// Integer + - * / % of two constants becomes one number before code is emitted.

#include "check.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

int wine_image(const std::vector<uint8_t>& image) {
    char path[] = "/tmp/okayms-fold-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-fold-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

bool text_has(const okayms::ObjectFile& object, const uint8_t* bytes, size_t n) {
    for (const okayms::Section& section : object.sections) {
        if (section.name != ".text" || section.data.size() < n) continue;
        for (size_t i = 0; i + n <= section.data.size(); ++i) {
            if (std::memcmp(section.data.data() + i, bytes, n) == 0) return true;
        }
    }
    return false;
}

}  // namespace

static void test_fold_arithmetic(Tally& tally) {
    std::printf("test_fold_arithmetic\n");
    okayms::CompileInput in;
    in.source = "int entry(void) { return (20 + 22) * 2; }\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    const uint8_t imm[] = {0xB8, 84, 0, 0, 0};
    CHECK(tally, text_has(compiled.value.object, imm, 5));
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) return;
    CHECK(tally, wine_image(linked.value) == 84);

    in.source = "int entry(void) { return 7 - 3; }\n";
    const auto sub = okayms::compile_c(in);
    CHECK(tally, sub.ok());
    if (!sub.ok()) return;
    const uint8_t four[] = {0xB8, 4, 0, 0, 0};
    CHECK(tally, text_has(sub.value.object, four, 5));

    in.source = "int entry(void) { return 10 % 3; }\n";
    const auto mod = okayms::compile_c(in);
    CHECK(tally, mod.ok());
    if (!mod.ok()) return;
    const uint8_t one[] = {0xB8, 1, 0, 0, 0};
    CHECK(tally, text_has(mod.value.object, one, 5));

    in.source = "int entry(void) { return 1 / 0; }\n";
    const auto bad = okayms::compile_c(in);
    CHECK(tally, !bad.ok());
    CHECK(tally, !bad.ok() && bad.error.find("division by zero") != std::string::npos);
}

int main() {
    Tally tally;
    test_fold_arithmetic(tally);
    return tally.finish(1);
}
