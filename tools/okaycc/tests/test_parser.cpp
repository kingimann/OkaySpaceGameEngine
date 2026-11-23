#include "diag.hpp"
#include "parser.hpp"
#include "harness.hpp"

#include <string>

static int gOk = 0;

static void checkAst(const char* name, const std::string& src, const std::string& want) {
    okaycc::Diagnostics d;
    std::string got = okaycc::astOf(src, d);
    bool ok = true;
    if (!d.ok()) {
        std::fprintf(stderr, "FAIL %s unexpected error\n%s", name, d.format().c_str());
        ok = false;
    }
    if (got != want) {
        std::fprintf(stderr, "FAIL %s\n--- got ---\n%s--- want ---\n%s", name, got.c_str(), want.c_str());
        ok = false;
    }
    if (ok) ++gOk;
    else ++okaycc_test::gFail;
}

static void checkBounded(const char* name, const std::string& src, const char* needle) {
    okaycc::Diagnostics d;
    okaycc::astOf(src, d);
    if (d.items().empty() || d.items().size() > 40 || d.format().find(needle) == std::string::npos) {
        std::fprintf(stderr, "FAIL %s errors %zu missing '%s'\n%s", name, d.items().size(), needle,
                     d.format().c_str());
        ++okaycc_test::gFail;
    } else {
        ++gOk;
    }
}

static void checkErr(const char* name, const std::string& src, const char* err) {
    okaycc::Diagnostics d;
    okaycc::astOf(src, d);
    if (d.format().find(err) == std::string::npos) {
        std::fprintf(stderr, "FAIL %s missing '%s'\n%s", name, err, d.format().c_str());
        ++okaycc_test::gFail;
    } else {
        ++gOk;
    }
}

int main() {
    checkAst("empty", "", "(tu)\n");
    checkAst("main", "int main(){return 0;}\n",
             "(tu\n"
             "  (fn main\n"
             "    (type int)\n"
             "    (params)\n"
             "    (body\n"
             "      (block\n"
             "        (return\n"
             "          (int 0)\n"
             "        )\n"
             "      )\n"
             "    )\n"
             "  )\n"
             ")\n");

    checkAst("precedence", "int f(){return a=b=c+d*e;}\n",
             "(tu\n"
             "  (fn f\n"
             "    (type int)\n"
             "    (params)\n"
             "    (body\n"
             "      (block\n"
             "        (return\n"
             "          (assign = (name a) (assign = (name b) (bin + (name c) (bin * (name d) (name e)))))\n"
             "        )\n"
             "      )\n"
             "    )\n"
             "  )\n"
             ")\n");

    checkAst("unary postfix", "int f(){return *p++;}\n",
             "(tu\n"
             "  (fn f\n"
             "    (type int)\n"
             "    (params)\n"
             "    (body\n"
             "      (block\n"
             "        (return\n"
             "          (unary * (post ++ (name p)))\n"
             "        )\n"
             "      )\n"
             "    )\n"
             "  )\n"
             ")\n");

    checkAst("prefix", "int f(){return ++*p;}\n",
             "(tu\n"
             "  (fn f\n"
             "    (type int)\n"
             "    (params)\n"
             "    (body\n"
             "      (block\n"
             "        (return\n"
             "          (pre ++ (unary * (name p)))\n"
             "        )\n"
             "      )\n"
             "    )\n"
             "  )\n"
             ")\n");

    checkAst("decls", "int x; int *p; char a[4]; char *s = \"hi\"; int f(int x); int (*fp)(int x);\n",
             "(tu\n"
             "  (var x\n"
             "    (type int)\n"
             "  )\n"
             "  (var p\n"
             "    (type (ptr int))\n"
             "  )\n"
             "  (var a\n"
             "    (type (arr char (int 4)))\n"
             "  )\n"
             "  (var s\n"
             "    (type (ptr char))\n"
             "    (init\n"
             "      (string \"hi\")\n"
             "    )\n"
             "  )\n"
             "  (fdecl f\n"
             "    (type int)\n"
             "    (params\n"
             "      (p x int)\n"
             "    )\n"
             "  )\n"
             "  (var fp\n"
             "    (type (ptr (fn int (p x int))))\n"
             "  )\n"
             ")\n");

    checkAst("grouped ptr", "int (*a)[3]; int *b[3];\n",
             "(tu\n"
             "  (var a\n"
             "    (type (ptr (arr int (int 3))))\n"
             "  )\n"
             "  (var b\n"
             "    (type (arr (ptr int) (int 3)))\n"
             "  )\n"
             ")\n");

    checkAst("dangling else", "int f(){if(a)if(b)c;else d;}\n",
             "(tu\n"
             "  (fn f\n"
             "    (type int)\n"
             "    (params)\n"
             "    (body\n"
             "      (block\n"
             "        (if\n"
             "          (name a)\n"
             "          (if\n"
             "            (name b)\n"
             "            (expr\n"
             "              (name c)\n"
             "            )\n"
             "            (else\n"
             "              (expr\n"
             "                (name d)\n"
             "              )\n"
             "            )\n"
             "          )\n"
             "        )\n"
             "      )\n"
             "    )\n"
             "  )\n"
             ")\n");

    checkAst("loops", "int f(){while(a)break; for(int i=0;i<n;i=i+1)continue;}\n",
             "(tu\n"
             "  (fn f\n"
             "    (type int)\n"
             "    (params)\n"
             "    (body\n"
             "      (block\n"
             "        (while\n"
             "          (name a)\n"
             "          (break)\n"
             "        )\n"
             "        (for\n"
             "          (init\n"
             "            (var i\n"
             "              (type int)\n"
             "              (init\n"
             "                (int 0)\n"
             "              )\n"
             "            )\n"
             "          )\n"
             "          (cond (bin < (name i) (name n)))\n"
             "          (step (assign = (name i) (bin + (name i) (int 1))))\n"
             "          (continue)\n"
             "        )\n"
             "      )\n"
             "    )\n"
             "  )\n"
             ")\n");

    checkAst("call index cond", "int f(){return a[i]?g(1,x):0;}\n",
             "(tu\n"
             "  (fn f\n"
             "    (type int)\n"
             "    (params)\n"
             "    (body\n"
             "      (block\n"
             "        (return\n"
             "          (cond (index (name a) (name i)) (call (name g) (int 1) (name x)) (int 0))\n"
             "        )\n"
             "      )\n"
             "    )\n"
             "  )\n"
             ")\n");

    checkAst("logic", "int f(){return a&&b||c==d;}\n",
             "(tu\n"
             "  (fn f\n"
             "    (type int)\n"
             "    (params)\n"
             "    (body\n"
             "      (block\n"
             "        (return\n"
             "          (bin || (bin && (name a) (name b)) (bin == (name c) (name d)))\n"
             "        )\n"
             "      )\n"
             "    )\n"
             "  )\n"
             ")\n");

    checkAst("macro then parse", "#define N 1\nint x = N;\n",
             "(tu\n"
             "  (var x\n"
             "    (type int)\n"
             "    (init\n"
             "      (int 1)\n"
             "    )\n"
             "  )\n"
             ")\n");

    checkAst("cast and comma", "int f(){return (int)*p, 2;}\n",
             "(tu\n"
             "  (fn f\n"
             "    (type int)\n"
             "    (params)\n"
             "    (body\n"
             "      (block\n"
             "        (return\n"
             "          (comma (cast int (unary * (name p))) (int 2))\n"
             "        )\n"
             "      )\n"
             "    )\n"
             "  )\n"
             ")\n");

    checkBounded("cpp bail",
                 "namespace N {\nusing namespace std;\n}\n",
                 "expected a declaration");
    {
        std::string deep = "int f(){return ";
        for (int i = 0; i < 80; ++i) deep += "(";
        deep += "1";
        for (int i = 0; i < 80; ++i) deep += ")";
        deep += ";}\n";
        checkBounded("deep parens", deep, "nesting is too deep");
    }

    checkErr("missing semi", "int x\n", "';'");
    checkErr("not in subset", "_Atomic x;\n", "not in this subset");  // inline is accepted since round 16
    checkErr("bad paren", "int f( {\n}\n", "')'");

    okaycc_test::gPass += gOk;
    std::printf("parser: %d passed, %d failed\n", okaycc_test::gPass, okaycc_test::gFail);
    return okaycc_test::gFail == 0 ? 0 : 1;
}
