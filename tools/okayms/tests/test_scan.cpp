// Linear scan reuses a register for locals whose lives do not overlap, and
// it can use the callee-saved registers past rbx, rsi and rdi.

#include "check.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

int wine_image(const std::vector<uint8_t>& image) {
    char path[] = "/tmp/okayms-scan-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-scan-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

const std::vector<uint8_t>* text_of(const okayms::ObjectFile& object) {
    for (const auto& section : object.sections)
        if (section.name == ".text") return &section.data;
    return nullptr;
}

int prologue_saves(const std::vector<uint8_t>& text) {
    int n = 0;
    for (size_t i = 0; i + 2 < text.size(); ++i) {
        if (text[i] == 0x48 && text[i + 1] == 0x89 && text[i + 2] == 0xE5) break;
        if (text[i] == 0x53 || text[i] == 0x56 || text[i] == 0x57) ++n;
    }
    return n;
}

}  // namespace

static void test_reuse(Tally& tally) {
    std::printf("test_reuse\n");
    okayms::CompileInput in;
    in.optimize = true;
    in.opt_level = 1;
    in.source =
        "int entry(void) {\n"
        "  int s;\n"
        "  s = 1;\n"
        "  { int a; a = 2; s = s + a; }\n"
        "  { int b; b = 3; s = s + b; }\n"
        "  { int c; c = 4; s = s + c; }\n"
        "  { int d; d = 5; s = s + d; }\n"
        "  return s;\n"
        "}\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    const std::vector<uint8_t>* text = text_of(compiled.value.object);
    CHECK(tally, text != nullptr && prologue_saves(*text) == 2);
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) return;
    const int code = wine_image(linked.value);
    CHECK(tally, code == 15);
    if (code != 15) std::fprintf(stderr, "  wine %d saves %d\n", code, text ? prologue_saves(*text) : -1);
}

static void test_five(Tally& tally) {
    std::printf("test_five\n");
    okayms::CompileInput in;
    in.optimize = true;
    in.opt_level = 1;
    in.source =
        "int entry(void) {\n"
        "  int a; int b; int c; int d; int e;\n"
        "  a = 1; b = 2; c = 3; d = 4; e = 5;\n"
        "  return a + b + c + d + e;\n"
        "}\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) return;
    const std::vector<uint8_t>* text = text_of(compiled.value.object);
    bool saw_r12 = false;
    if (text != nullptr) {
        for (size_t i = 0; i + 1 < text->size(); ++i)
            if ((*text)[i] == 0x41 && (*text)[i + 1] == 0x54) saw_r12 = true;
    }
    CHECK(tally, saw_r12);
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) return;
    const int code = wine_image(linked.value);
    CHECK(tally, code == 15);
    if (code != 15) std::fprintf(stderr, "  wine %d\n", code);
}

int main() {
    Tally tally;
    test_reuse(tally);
    test_five(tally);
    return tally.finish(2);
}
