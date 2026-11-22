// sizeof, ++/--, compound assignment, const, static, #if/#elif, bitfields,
// ternary, comma, and goto. Written from ISO/IEC 9899.

#include "check.hpp"
#include "okayms/compile.hpp"
#include "okayms/coff.hpp"
#include "okayms/link.hpp"

#include <cstdio>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

int wine_image(const std::vector<uint8_t>& image) {
    char path[] = "/tmp/okayms-gaps-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-gaps-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

void expect_code(Tally& tally, const char* source, int want) {
    okayms::CompileInput in;
    in.source = source;
    const auto compiled = okayms::compile_c(in);
    if (!compiled.ok()) {
        CHECK(tally, false);
        std::fprintf(stderr, "  compile: %s\n", compiled.error.c_str());
        return;
    }
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    if (!linked.ok()) {
        CHECK(tally, false);
        std::fprintf(stderr, "  link: %s\n", linked.error.c_str());
        return;
    }
    const int code = wine_image(linked.value);
    CHECK(tally, code == want);
    if (code != want) std::fprintf(stderr, "  wine %d\n", code);
}

}  // namespace

static void test_ops_and_sizeof(Tally& tally) {
    std::printf("test_ops_and_sizeof\n");
    expect_code(tally,
                "struct S { int a; char b; };\n"
                "int entry(void) { int a[4]; return sizeof(int) + sizeof(a) + sizeof(struct S); }\n",
                28);
    expect_code(tally, "int entry(void) { int x; x = 3; ++x; return x++; }\n", 4);
    expect_code(tally, "int entry(void) { int x; x = 5; --x; return x; }\n", 4);
    expect_code(tally, "int entry(void) { int x; x = 10; x += 3; x *= 2; return x; }\n", 26);
    expect_code(tally, "int entry(void) { int x; x = 1; return x > 0 ? 6 : 7; }\n", 6);
    expect_code(tally, "int entry(void) { return (1, 2, 3); }\n", 3);
    expect_code(tally,
                "int entry(void) { int n; n = 1; goto skip; n = 9; skip: return n; }\n",
                1);
}

static void test_const_static_bits_if(Tally& tally) {
    std::printf("test_const_static_bits_if\n");
    expect_code(tally, "const int x = 4;\nint entry(void) { return x + 3; }\n", 7);
    okayms::CompileInput bad;
    bad.source = "const int x = 1;\nint entry(void) { x = 2; return x; }\n";
    const auto rejected = okayms::compile_c(bad);
    CHECK(tally, !rejected.ok());
    CHECK(tally, !rejected.ok() && rejected.error.find("const") != std::string::npos);

    expect_code(tally,
                "int bump(void) { static int n = 1; n = n + 1; return n; }\n"
                "int entry(void) { return bump() + bump(); }\n",
                5);
    okayms::CompileInput hidden;
    hidden.source = "static int hidden(void) { return 9; }\nint entry(void) { return hidden(); }\n";
    const auto compiled = okayms::compile_c(hidden);
    CHECK(tally, compiled.ok());
    if (compiled.ok()) {
        bool saw = false;
        for (const auto& symbol : compiled.value.object.symbols) {
            if (symbol.name == "hidden" && symbol.storage == okayms::kClassStatic) saw = true;
        }
        CHECK(tally, saw);
        const auto linked = okayms::link_executable({compiled.value.object}, {});
        CHECK(tally, linked.ok());
        if (linked.ok()) CHECK(tally, wine_image(linked.value) == 9);
    }

    expect_code(tally,
                "struct Flags { unsigned a : 3; unsigned b : 5; };\n"
                "int entry(void) { struct Flags f; f.a = 5; f.b = 3; return f.a + f.b; }\n",
                8);
    expect_code(tally,
                "#define A 1\n#if A\nint entry(void) { return 7; }\n#else\nint entry(void) { return 0; }\n#endif\n",
                7);
    expect_code(tally,
                "#define A 0\n#if A\nint entry(void) { return 1; }\n#elif 1\nint entry(void) { return 8; }\n#endif\n",
                8);
    expect_code(tally,
                "#if defined(NOPE)\nint entry(void) { return 0; }\n#else\nint entry(void) { return 3; }\n#endif\n",
                3);
}

int main() {
    Tally tally;
    test_ops_and_sizeof(tally);
    test_const_static_bits_if(tally);
    return tally.finish(2);
}
