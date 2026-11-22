// typedef, arrow, enum, union, function pointers, varargs, large-struct
// returns, and char/short/unsigned widening. Integer promotions follow
// ISO/IEC 9899. The hidden return pointer and the varargs home area follow
// the Windows x64 calling convention.

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
    char path[] = "/tmp/okayms-types-XXXXXX.exe";
    const int fd = mkstemps(path, 4);
    if (fd < 0) return -1;
    if (write(fd, image.data(), image.size()) != static_cast<ssize_t>(image.size())) {
        close(fd);
        return -1;
    }
    close(fd);
    const int rc = std::system(("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + std::string(path) +
                                 " >/tmp/okayms-types-wine.txt 2>&1")
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

static void test_typedef_enum_union_arrow(Tally& tally) {
    std::printf("test_typedef_enum_union_arrow\n");
    expect_code(tally,
                "typedef int I;\n"
                "typedef unsigned char Byte;\n"
                "I entry(void) { Byte b; b = 200; return b; }\n",
                200);
    expect_code(tally,
                "typedef struct Pair { int a; int b; } Pair;\n"
                "int entry(void) { Pair p; p.a = 20; p.b = 22; return p.a + p.b; }\n",
                42);
    expect_code(tally,
                "struct Node { int value; struct Node *next; };\n"
                "int entry(void) {\n"
                "  struct Node a; struct Node *p;\n"
                "  a.value = 40; a.next = 0; p = &a;\n"
                "  return p->value + 2;\n"
                "}\n",
                42);
    expect_code(tally,
                "enum Color { Red = 1, Green, Blue = 10 };\n"
                "int entry(void) { return Green + Blue; }\n",
                12);
    expect_code(tally,
                "union U { int n; char b[4]; };\n"
                "int entry(void) { union U u; u.n = 65; return u.b[0]; }\n",
                65);
}

static void test_fnptr_varargs_sret(Tally& tally) {
    std::printf("test_fnptr_varargs_sret\n");
    expect_code(tally,
                "int add(int a, int b) { return a + b; }\n"
                "int entry(void) {\n"
                "  int (*table[2])(int, int);\n"
                "  table[0] = add; table[1] = add;\n"
                "  return table[0](20, 22);\n"
                "}\n",
                42);
    expect_code(tally,
                "typedef char *va_list;\n"
                "int sum(int n, ...) {\n"
                "  va_list ap; int i; int s; int v;\n"
                "  va_start(ap, n);\n"
                "  s = 0;\n"
                "  for (i = 0; i < n; i = i + 1) { v = va_arg(ap, int); s = s + v; }\n"
                "  va_end(ap);\n"
                "  return s;\n"
                "}\n"
                "int entry(void) { return sum(3, 10, 20, 12); }\n",
                42);
    expect_code(tally,
                "struct Big { int a; int b; int c; };\n"
                "struct Big make(int n) {\n"
                "  struct Big b; b.a = n; b.b = n + 1; b.c = n + 2; return b;\n"
                "}\n"
                "int entry(void) {\n"
                "  struct Big b; b = make(10); return b.a + b.b + b.c;\n"
                "}\n",
                33);
}

static void test_widening(Tally& tally) {
    std::printf("test_widening\n");
    expect_code(tally,
                "int entry(void) { char c; c = 255; if (c < 0) return 1; return 0; }\n",
                1);
    expect_code(tally,
                "int entry(void) { unsigned char u; u = 255; if (u < 0) return 9; return u == 255; }\n",
                1);
    expect_code(tally,
                "int entry(void) { short s; s = -2; if (s < 0) return 7; return 0; }\n",
                7);
    expect_code(tally,
                "int entry(void) { unsigned short s; s = 40000; return s > 30000; }\n",
                1);
    expect_code(tally,
                "int entry(void) { unsigned int n; n = 1; return n > -1; }\n",
                0);
}

int main() {
    Tally tally;
    test_typedef_enum_union_arrow(tally);
    test_fnptr_varargs_sret(tally);
    test_widening(tally);
    return tally.finish(3);
}
