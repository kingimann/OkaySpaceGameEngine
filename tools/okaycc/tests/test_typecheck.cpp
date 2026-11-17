#include "diag.hpp"
#include "parser.hpp"
#include "harness.hpp"

#include <string>

static int gOk = 0;

static void ok(const char* name, const std::string& src) {
    okaycc::Diagnostics d;
    okaycc::checkSource(src, d);
    if (!d.ok()) {
        std::fprintf(stderr, "FAIL %s unexpected error\n%s", name, d.format().c_str());
        ++okaycc_test::gFail;
    } else {
        ++gOk;
    }
}

static void bad(const char* name, const std::string& src, const char* err) {
    okaycc::Diagnostics d;
    okaycc::checkSource(src, d);
    if (d.format().find(err) == std::string::npos) {
        std::fprintf(stderr, "FAIL %s missing '%s'\n%s", name, err, d.format().c_str());
        ++okaycc_test::gFail;
    } else {
        ++gOk;
    }
}

int main() {
    ok("arith", "int main(){int x; char c; x = 1; c = 2; x = c; c = x; return x + c;}\n");
    ok("pointers", "int main(){int x; int *p; p = 0; p = &x; *p = x; return *p;}\n");
    ok("ptr arith", "int main(){int *p; int *q; int d; p = p + 1; d = p - q; return d;}\n");
    ok("array", "int main(){int a[2]; a[0] = 1; return a[1];}\n");
    ok("string", "int main(){char *s; s = \"hi\"; return s[0];}\n");
    ok("call", "int f(int x){return x;} int main(){return f(1);}\n");
    ok("void fn", "void g(){return;} int main(){g(); return 0;}\n");
    ok("shadow", "int main(){int x; x = 1; {int x; x = 2;} return x;}\n");
    ok("for scope", "int main(){int s; s = 0; for(int i = 0; i < 3; i = i + 1) s = s + i; return s;}\n");
    ok("break in loop", "int main(){while(1){break;} return 0;}\n");
    ok("null", "int main(){int *p; p = 0; return 0;}\n");
    ok("global const", "int x = 1; int *p = 0; char *s = \"hi\";\n");

    bad("undeclared", "int main(){return y;}\n", "undeclared");
    bad("redef", "int main(){int x; int x; return x;}\n", "redefined");
    bad("param redef", "int f(int x, int x){return x;}\n", "redefined");
    bad("use after block", "int main(){{int x; x = 1;} return x;}\n", "undeclared");
    bad("deref int", "int main(){int x; return *x;}\n", "not a pointer");
    bad("call count", "int f(){return 1;} int main(){return f(1);}\n", "argument");
    bad("return type", "int f(){int *p; p = 0; return p;}\n", "return");
    bad("assign ptr to int", "int main(){int x; int *p; p = 0; x = p; return x;}\n", "cannot assign");
    bad("assign int to ptr", "int main(){int *p; p = 1; return 0;}\n", "cannot assign");
    bad("break", "int main(){break; return 0;}\n", "break");
    bad("void var", "void x;\n", "void");
    bad("global not const", "int x = 1; int y = x;\n", "constant");
    bad("ptr sub assign", "int main(){int *p; int *q; p = p - q; return 0;}\n", "cannot assign");

    okaycc_test::gPass += gOk;
    std::printf("typecheck: %d passed, %d failed\n", okaycc_test::gPass, okaycc_test::gFail);
    return okaycc_test::gFail == 0 ? 0 : 1;
}
