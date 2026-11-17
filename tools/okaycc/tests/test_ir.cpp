#include "diag.hpp"
#include "ir.hpp"
#include "harness.hpp"

#include <string>

static int gOk = 0;

static void checkIr(const char* name, const std::string& src, const std::string& want) {
    okaycc::Diagnostics d;
    std::string got = okaycc::irText(src, d);
    bool ok = true;
    if (!d.ok()) {
        std::fprintf(stderr, "FAIL %s error\n%s", name, d.format().c_str());
        ok = false;
    }
    if (got != want) {
        std::fprintf(stderr, "FAIL %s\n--- got ---\n%s--- want ---\n%s", name, got.c_str(), want.c_str());
        ok = false;
    }
    if (ok) ++gOk;
    else ++okaycc_test::gFail;
}

int main() {
    checkIr("fold", "int main(){return 40+2;}\n",
            "func main\n"
            "entry:\n"
            "  ret 42\n");

    checkIr("fact",
            "int fact(int n) {\n"
            "  int r;\n"
            "  r = 1;\n"
            "  while (n > 1) {\n"
            "    r = r * n;\n"
            "    n = n - 1;\n"
            "  }\n"
            "  return r;\n"
            "}\n"
            "int main() {\n"
            "  return fact(5);\n"
            "}\n",
            "func fact\n"
            "entry:\n"
            "  mov v1, 1\n"
            "  jmp while0_cond\n"
            "while0_cond:\n"
            "  cgt v2, v0, 1\n"
            "  br v2, while0_body, while0_end\n"
            "while0_body:\n"
            "  mul v3, v1, v0\n"
            "  mov v1, v3\n"
            "  sub v4, v0, 1\n"
            "  mov v0, v4\n"
            "  jmp while0_cond\n"
            "while0_end:\n"
            "  ret v1\n"
            "func main\n"
            "entry:\n"
            "  call v0, fact(5)\n"
            "  ret v0\n");

    checkIr("pointer", "int main(){int x; int *p; x=7; p=&x; *p=9; return x;}\n",
            "func main\n"
            "entry:\n"
            "  lea v2, v0\n"
            "  store v2, 7, 4\n"
            "  lea v3, v0\n"
            "  mov v1, v3\n"
            "  store v1, 9, 4\n"
            "  lea v4, v0\n"
            "  load v5, v4, 4\n"
            "  ret v5\n");

    okaycc_test::gPass += gOk;
    std::printf("ir: %d passed, %d failed\n", okaycc_test::gPass, okaycc_test::gFail);
    return okaycc_test::gFail == 0 ? 0 : 1;
}
