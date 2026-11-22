// UWOP_ALLOC_LARGE and saved nonvolatile registers, from the x64
// exception-handling document. A frame bigger than 128 bytes uses the
// large allocation op, and the 112-byte compiler cap is gone.

#include "check.hpp"

#include <cstring>
#include "okayms/compile.hpp"
#include "okayms/link.hpp"
#include "okayms/unwind.hpp"

#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

void test_alloc_large_and_saved_reg(Tally& tally) {
    std::printf("test_alloc_large_and_saved_reg\n");
    okayms::UnwindPrologue prolog;
    prolog.size = 11;
    prolog.frame_register = 5;
    prolog.alloc = 0x200;
    prolog.alloc_at = 11;
    prolog.frame_at = 4;
    prolog.pushes.push_back({5, 1});
    const auto large = okayms::encode_unwind(prolog);
    CHECK(tally, large.ok());
    if (!large.ok()) {
        std::fprintf(stderr, "  %s\n", large.error.c_str());
        return;
    }
    const uint8_t expect_large[] = {0x01, 0x0B, 0x04, 0x05, 0x0B, 0x01, 0x40, 0x00, 0x04, 0x03, 0x01, 0x50};
    CHECK(tally, large.value.size() == sizeof expect_large);
    CHECK(tally, std::memcmp(large.value.data(), expect_large, sizeof expect_large) == 0);

    prolog.size = 12;
    prolog.alloc_at = 12;
    prolog.frame_at = 5;
    prolog.pushes.clear();
    prolog.pushes.push_back({3, 1});
    prolog.pushes.push_back({5, 2});
    const auto saved = okayms::encode_unwind(prolog);
    CHECK(tally, saved.ok());
    if (!saved.ok()) return;
    const uint8_t expect_saved[] = {0x01, 0x0C, 0x05, 0x05, 0x0C, 0x01, 0x40, 0x00, 0x05, 0x03, 0x02, 0x50, 0x01, 0x30, 0x00, 0x00};
    CHECK(tally, saved.value.size() == sizeof expect_saved);
    CHECK(tally, std::memcmp(saved.value.data(), expect_saved, sizeof expect_saved) == 0);
}

void test_big_frame_runs(Tally& tally) {
    std::printf("test_big_frame_runs\n");
    okayms::CompileInput in;
    in.source =
        "int entry(void) {\n"
        "  int a[40];\n"
        "  a[0] = 1;\n"
        "  a[39] = 7;\n"
        "  return a[39];\n"
        "}\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  compile: %s\n", compiled.error.c_str());
        return;
    }
    bool saw_large = false;
    for (const auto& section : compiled.value.object.sections) {
        if (section.name != ".xdata") continue;
        for (size_t i = 0; i + 1 < section.data.size(); ++i) {
            if ((section.data[i + 1] & 0x0F) == 0x01) saw_large = true;
        }
    }
    CHECK(tally, saw_large);
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) return;
    char path[] = "/tmp/okayms-big-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    CHECK(tally, fd >= 0);
    if (fd < 0) return;
    const ssize_t wrote = write(fd, linked.value.data(), linked.value.size());
    close(fd);
    CHECK(tally, wrote == static_cast<ssize_t>(linked.value.size()));
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-big-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    int code = -1;
    if (rc != -1 && WIFEXITED(rc)) code = WEXITSTATUS(rc);
    CHECK(tally, code == 7);
    if (code != 7) std::fprintf(stderr, "  wine %d\n", code);
}

}  // namespace

int main() {
    Tally tally;
    test_alloc_large_and_saved_reg(tally);
    test_big_frame_runs(tally);
    return tally.finish(2);
}
