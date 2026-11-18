// Language additions in okay-cl: loops, switch, arrays, structs, pointers,
// function-like macros, and globals. Each program returns through entry
// and Wine checks the code. Strings live in .rdata.

#include "check.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <cstdio>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

int wine_image(const std::vector<uint8_t>& image) {
    char path[] = "/tmp/okayms-lang-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-lang-wine.txt 2>&1")
                                    .c_str());
    unlink(path);
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

int run_src(const char* source, std::string* error) {
    okayms::CompileInput in;
    in.source = source;
    const auto compiled = okayms::compile_c(in);
    if (!compiled.ok()) {
        *error = compiled.error;
        return -1;
    }
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    if (!linked.ok()) {
        *error = linked.error;
        return -1;
    }
    return wine_image(linked.value);
}

void expect_code(Tally& tally, const char* source, int want) {
    std::string error;
    const int code = run_src(source, &error);
    CHECK(tally, code == want);
    if (code != want) std::fprintf(stderr, "  got %d (%s)\n", code, error.c_str());
}

void test_loops_and_switch(Tally& tally) {
    std::printf("test_loops_and_switch\n");
    expect_code(tally,
                "int entry(void) {\n"
                "  int i; int s; s = 0;\n"
                "  for (i = 1; i <= 10; i = i + 1) s = s + i;\n"
                "  return s;\n"
                "}\n",
                55);
    expect_code(tally,
                "int entry(void) {\n"
                "  int i; i = 0;\n"
                "  do { i = i + 1; } while (i < 5);\n"
                "  return i;\n"
                "}\n",
                5);
    expect_code(tally,
                "int entry(void) {\n"
                "  int x; x = 2;\n"
                "  switch (x) {\n"
                "    case 1: return 10;\n"
                "    case 2: return 20;\n"
                "    default: return 30;\n"
                "  }\n"
                "}\n",
                20);
}

void test_arrays_pointers_globals(Tally& tally) {
    std::printf("test_arrays_pointers_globals\n");
    expect_code(tally,
                "int entry(void) {\n"
                "  int a[4]; int i; int s;\n"
                "  a[0] = 1; a[1] = 2; a[2] = 3; a[3] = 4;\n"
                "  s = 0;\n"
                "  for (i = 0; i < 4; i = i + 1) s = s + a[i];\n"
                "  return s;\n"
                "}\n",
                10);
    expect_code(tally,
                "int entry(void) {\n"
                "  int x; int *p;\n"
                "  x = 1; p = &x; *p = 42;\n"
                "  return *(p + 0);\n"
                "}\n",
                42);
    expect_code(tally,
                "int g = 7;\n"
                "int z;\n"
                "int entry(void) { z = g + 1; return z; }\n",
                8);
    okayms::CompileInput in;
    in.source = "int entry(void) { return \"hi\"[0]; }\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) return;
    bool saw = false;
    for (const auto& section : compiled.value.object.sections) {
        if (section.name == ".rdata" && section.data.size() >= 3 && section.data[0] == 'h' && section.data[1] == 'i')
            saw = true;
    }
    CHECK(tally, saw);
}

void test_struct_and_macro(Tally& tally) {
    std::printf("test_struct_and_macro\n");
    expect_code(tally,
                "struct Pair { int a; int b; };\n"
                "int sum(struct Pair p) { return p.a + p.b; }\n"
                "int entry(void) {\n"
                "  struct Pair p;\n"
                "  p.a = 20; p.b = 22;\n"
                "  return sum(p);\n"
                "}\n",
                42);
    expect_code(tally,
                "struct Big { int a; int b; int c; };\n"
                "int first(struct Big b) { return b.a + b.c; }\n"
                "int entry(void) {\n"
                "  struct Big b;\n"
                "  b.a = 10; b.b = 0; b.c = 32;\n"
                "  return first(b);\n"
                "}\n",
                42);
    expect_code(tally,
                "#define ADD(a, b) ((a)+(b))\n"
                "int entry(void) { return ADD(20, 22); }\n",
                42);
}

}  // namespace

int main() {
    Tally tally;
    test_loops_and_switch(tally);
    test_arrays_pointers_globals(tally);
    test_struct_and_macro(tally);
    return tally.finish(3);
}
