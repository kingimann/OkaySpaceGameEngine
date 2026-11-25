#include "diag.hpp"
#include "files.hpp"
#include "preproc.hpp"
#include "harness.hpp"

#include <string>
#include <vector>

static int gOk = 0;

static void check(const char* name, const std::string& src, const std::string& want,
                  const char* errSub) {
    okaycc::MemoryFiles fs;
    fs.add("/t.c", src);
    okaycc::Diagnostics d;
    std::string got = okaycc::preprocess(fs, "/t.c", {}, d);
    bool ok = true;
    if (errSub) {
        if (d.format().find(errSub) == std::string::npos) {
            std::fprintf(stderr, "FAIL %s: missing error '%s'\n%s", name, errSub, d.format().c_str());
            ok = false;
        }
    } else if (!d.ok()) {
        std::fprintf(stderr, "FAIL %s: unexpected error\n%s", name, d.format().c_str());
        ok = false;
    }
    if (!errSub && got != want) {
        std::fprintf(stderr, "FAIL %s\n--- got ---\n%s--- want ---\n%s", name, got.c_str(), want.c_str());
        ok = false;
    }
    if (ok) ++gOk;
    else ++okaycc_test::gFail;
}

static void checkFiles(const char* name, okaycc::MemoryFiles& fs, const std::string& main,
                       const std::vector<std::string>& inc, const std::string& want,
                       const char* errSub) {
    okaycc::Diagnostics d;
    std::string got = okaycc::preprocess(fs, main, inc, d);
    bool ok = true;
    if (errSub) {
        if (d.format().find(errSub) == std::string::npos) {
            std::fprintf(stderr, "FAIL %s: missing error '%s'\n%s", name, errSub, d.format().c_str());
            ok = false;
        }
    } else if (!d.ok()) {
        std::fprintf(stderr, "FAIL %s: unexpected error\n%s", name, d.format().c_str());
        ok = false;
    }
    if (!errSub && got != want) {
        std::fprintf(stderr, "FAIL %s\n--- got ---\n%s--- want ---\n%s", name, got.c_str(), want.c_str());
        ok = false;
    }
    if (ok) ++gOk;
    else ++okaycc_test::gFail;
}

int main() {
    check("plain", "int a;\n", "int a ;\n", nullptr);
    check("object", "#define X 1\nint a = X;\n", "int a = 1 ;\n", nullptr);
    check("object expr", "#define X 1 + 2\nint a = X;\n", "int a = 1 + 2 ;\n", nullptr);
    check("not a call", "#define F(a) a\nF\n", "F\n", nullptr);
    check("call", "#define F(a, b) a + b\nF(1, 2)\n", "1 + 2\n", nullptr);
    check("nested", "#define A 1\n#define B A\nB\n", "1\n", nullptr);
    check("self", "#define M M\nM\n", "M\n", nullptr);
    check("mutual", "#define M N\n#define N M\nM\n", "M\n", nullptr);
    check("rescan call", "#define ID(x) x\n#define G ID\nG(3)\n", "3\n", nullptr);
    check("arg expand", "#define F(x) x\n#define G 1\nF(G)\n", "1\n", nullptr);
    check("no arg expand on hash", "#define S(x) #x\n#define G 1\nS(G)\n", "\"G\"\n", nullptr);
    check("both hash and value", "#define F(x) #x x\n#define G 1\nF(G)\n", "\"G\" 1\n", nullptr);
    check("stringify spaces", "#define S(x) #x\nS(a + b)\nS(a+b)\n", "\"a + b\"\n\"a+b\"\n", nullptr);
    check("stringify quote", "#define S(x) #x\nS(\"hi\")\n", "\"\\\"hi\\\"\"\n", nullptr);
    check("stringify comment", "#define S(x) #x\nS(a/**/b)\n", "\"a b\"\n", nullptr);
    check("paste", "#define C(a, b) a ## b\nC(foo, bar)\n", "foobar\n", nullptr);
    check("paste empty", "#define C(a, b) a ## b\nC(, y)\nC(x, )\n", "y\nx\n", nullptr);
    check("paste unexpanded", "#define A 1\n#define C(a, b) a ## b\nC(A, A)\n", "AA\n", nullptr);
    check("paste then expand", "#define FOO 1\n#define C(a, b) a ## b\n#define X C(FO, O)\nX\n", "1\n",
          nullptr);
    check("paste chain empty", "#define C(a, b, c) a ## b ## c\nC(1, , 2)\n", "12\n", nullptr);
    check("object paste", "#define X 1 ## 2\nX\n", "12\n", nullptr);
    check("parens arg", "#define F(a, b) a\nF((1, 2), 3)\n", "( 1 , 2 )\n", nullptr);
    check("span lines", "#define F(a, b) a + b\nF(1,\n2)\n", "1 + 2\n", nullptr);
    check("blue", "#define f(a) a\nf(f)(1)\n", "f ( 1 )\n", nullptr);
    check("mm", "#define m(a) a(w)\n#define w 0, 1\nm(m)\n", "m ( 0 , 1 )\n", nullptr);
    check("nested call", "#define ADD(a, b) a + b\nADD(ADD(1, 2), 3)\n", "1 + 2 + 3\n", nullptr);
    check("rescan hides itself",
          "#define x 3\n#define f(a) f(x * (a))\n#undef x\n#define x 2\nf(y+1)\n",
          "f ( 2 * ( y + 1 ) )\n", nullptr);

    check("va", "#define F(a, ...) a | __VA_ARGS__\nF(1, 2, 3)\n", "1 | 2 , 3\n", nullptr);
    check("va empty", "#define E(a, ...) a ## __VA_ARGS__\nE(1,)\nE(1)\n", "1\n1\n", nullptr);
    check("va string", "#define V(a, ...) #__VA_ARGS__\nV(1, 2, 3)\nV(1,)\n", "\"2, 3\"\n\"\"\n",
          nullptr);

    check("undef", "#define X 1\n#undef X\nint a = X;\n", "int a = X ;\n", nullptr);
    check("redef same", "#define X 1 + 2\n#define X 1 + 2\nint a = X;\n", "int a = 1 + 2 ;\n", nullptr);
    check("redef diff", "#define X 1\n#define X 2\nX\n", "1\n", "redefined");

    check("ifdef", "#define FOO 0\n#ifdef FOO\nint yes;\n#endif\n#ifndef FOO\nint no;\n#endif\n",
          "int yes ;\n", nullptr);
    check("ifndef", "#ifndef FOO\nint yes;\n#else\nint no;\n#endif\n", "int yes ;\n", nullptr);
    check("if arith", "#if 1 + 2 * 3 == 7\nint a;\n#endif\n", "int a ;\n", nullptr);
    check("if hex oct", "#if 0x10 + 010 == 24\nint d;\n#endif\n", "int d ;\n", nullptr);
    check("if shift", "#if (1 << 2) == 4 && 8 >> 1 == 4\nint b;\n#endif\n", "int b ;\n", nullptr);
    check("if not", "#if !0\nint c;\n#endif\n", "int c ;\n", nullptr);
    check("if char", "#if '\\n' == 10\nint e;\n#endif\n", "int e ;\n", nullptr);
    check("if unknown", "#if UNKNOWN == 0\nint f;\n#endif\n", "int f ;\n", nullptr);
    check("if defined forms",
          "#define FOO\n#if defined FOO && defined(FOO) && !defined(BAR)\nint y;\n#endif\n", "int y ;\n",
          nullptr);
    check("elif", "#if 0\nint a;\n#elif 0\nint b;\n#elif 1\nint c;\n#else\nint d;\n#endif\n", "int c ;\n",
          nullptr);
    check("else", "#if 0\nint a;\n#else\nint b;\n#endif\n", "int b ;\n", nullptr);
    check("skip error", "#if 0\n#error nope\n#define NO 1\n#endif\nint a = NO;\n", "int a = NO ;\n",
          nullptr);
    check("error fires", "#if 1\n#error boom\n#endif\n", "", "boom");
    check("short circuit", "#if 0 && (1/0)\nint a;\n#else\nint b;\n#endif\n", "int b ;\n", nullptr);
    check("ternary skip", "#if 1 ? 2 : 1/0\nint c;\n#endif\n", "int c ;\n", nullptr);
    check("div0", "#if 1/0\n#endif\n", "", "division by zero");

    check("line", "#define L __LINE__\nL\n__LINE__\n", "2\n3\n", nullptr);
    check("line chain", "#define L __LINE__\n#define M L\nM\n", "3\n", nullptr);
    check("file", "__FILE__\n", "\"/t.c\"\n", nullptr);
    check("file in macro", "#define F __FILE__\nF\n", "\"/t.c\"\n", nullptr);

    check("directive in arg", "#define F(a, b) a + b\nF(1,\n#define X 2\n2)\nint z = X;\n",
          "1 + 2\nint z = 2 ;\n", nullptr);
    check("hash from macro is not a directive", "#define HASH #\nHASH define X 1\nint y = X;\n",
          "# define X 1\nint y = X ;\n", nullptr);
    check("comment before paren", "#define F/**/(x) x\nF(1)\nint y = F;\n",
          "( x ) x ( 1 )\nint y = ( x ) x ;\n", nullptr);
    check("splice makes function", "#define F\\\n(x) x\nF(1)\n", "1\n", nullptr);

    check("bad args", "#define F(a) a\nF(1, 2)\n", "", "argument");
    check("bad paste", "#define C(a, b) a ## b\nC(., .)\n", "", "pasting");
    check("undef line", "#undef __LINE__\n", "", "__LINE__");
    check("define defined", "#define defined 1\n", "", "defined");
    check("unclosed if", "#if 1\nint a;\n", "", "unclosed");

    {
        okaycc::MemoryFiles fs;
        fs.add("/src/main.c", "#include \"a.h\"\nint m;\n");
        fs.add("/src/a.h", "int a;\n");
        fs.add("/inc/a.h", "int angle;\n");
        checkFiles("quote include", fs, "/src/main.c", {"/inc"}, "int a ;\nint m ;\n", nullptr);
    }
    {
        okaycc::MemoryFiles fs;
        fs.add("/src/main.c", "#include <a.h>\nint m;\n");
        fs.add("/src/a.h", "int quote;\n");
        fs.add("/inc/a.h", "int angle;\n");
        checkFiles("angle include", fs, "/src/main.c", {"/inc"}, "int angle ;\nint m ;\n", nullptr);
    }
    {
        okaycc::MemoryFiles fs;
        fs.add("/src/main.c", "#include \"b.h\"\n");
        fs.add("/inc/b.h", "int b;\n");
        checkFiles("quote falls back", fs, "/src/main.c", {"/inc"}, "int b ;\n", nullptr);
    }
    {
        // A header that does not end in a new-line ([lex.phases] p1.2, Math/TwoBoneIK.hpp): the
        // next line of the includer is still a directive (round 25 read "#include" as text).
        okaycc::MemoryFiles fs;
        fs.add("/src/main.c", "#include \"a.h\"\n#include \"b.h\"\nint m;\n");
        fs.add("/src/a.h", "int a; // no new-line");
        fs.add("/src/b.h", "int b;\n");
        checkFiles("no final new-line", fs, "/src/main.c", {}, "int a ;\nint b ;\nint m ;\n", nullptr);
    }
    {
        okaycc::MemoryFiles fs;
        fs.add("/src/main.c", "#define H \"a.h\"\n#include H\n");
        fs.add("/src/a.h", "int a;\n");
        checkFiles("include macro", fs, "/src/main.c", {}, "int a ;\n", nullptr);
    }
    {
        okaycc::MemoryFiles fs;
        fs.add("/src/main.c", "#include \"missing.h\"\n");
        checkFiles("missing include", fs, "/src/main.c", {}, "", "cannot find");
    }
    {
        okaycc::MemoryFiles fs;
        fs.add("/r.h", "#include \"r.h\"\n");
        checkFiles("recursive include", fs, "/r.h", {}, "", "nested too deeply");
    }
    {
        okaycc::MemoryFiles fs;
        fs.add("/src/main.c", "#include \"a.h\"\n");
        fs.add("/src/a.h", "#ifndef A_H\n#define A_H\n#include \"b.h\"\nint a;\n#endif\n");
        fs.add("/src/b.h", "#ifndef B_H\n#define B_H\n#include \"a.h\"\nint b;\n#endif\n");
        checkFiles("include guards", fs, "/src/main.c", {}, "int b ;\nint a ;\n", nullptr);
    }

    {
        okaycc::MemoryFiles fs;
        fs.add("/src/main.c", "#include \"a.h\"\n#include \"a.h\"\n");
        fs.add("/src/a.h", "#pragma once\nint a;\n");
        checkFiles("pragma once", fs, "/src/main.c", {}, "int a ;\n", nullptr);
    }
    {
        okaycc::MemoryFiles fs;
        fs.add("/t.c", "#pragma mystery\nint x;\n");
        okaycc::Diagnostics d;
        std::string got = okaycc::preprocess(fs, "/t.c", {}, d);
        if (!d.ok() || got != "int x ;\n" || d.format().find("ignoring unknown pragma") == std::string::npos) {
            std::fprintf(stderr, "FAIL unknown pragma\n%s--- got ---\n%s", d.format().c_str(), got.c_str());
            ++okaycc_test::gFail;
        } else {
            ++gOk;
        }
    }

    {
        // A long file (more than 100,000 tokens) is not a long macro expansion: the step limit
        // guards one expansion, not the whole translation unit (round 20; large engine headers hit it).
        std::string src = "#define ONE 1\n";
        std::string want;
        for (int i = 0; i < 30000; ++i) {
            src += "int a = ONE ;\n";
            want += "int a = 1 ;\n";
        }
        check("long file", src, want, nullptr);
        // A runaway expansion is still stopped: each level doubles the tokens.
        std::string bomb = "#define A0 x x\n";
        for (int i = 1; i <= 20; ++i) {
            bomb += "#define A" + std::to_string(i) + " A" + std::to_string(i - 1) + " A" +
                    std::to_string(i - 1) + "\n";
        }
        bomb += "A20\n";
        check("expansion bomb", bomb, "", "macro expansion is too long");
    }

    okaycc_test::gPass += gOk;
    std::printf("preproc: %d passed, %d failed\n", okaycc_test::gPass, okaycc_test::gFail);
    return okaycc_test::gFail == 0 ? 0 : 1;
}
