#include "diag.hpp"
#include "lexer.hpp"
#include "token.hpp"
#include "harness.hpp"

#include <cmath>
#include <string>
#include <vector>

using okaycc::Tok;
using okaycc::Token;

struct Exp {
    Tok kind;
    const char* text;
    int line;
    int col;
    bool lead = false;
    bool checkNum = false;
    uint64_t uval = 0;
    int base = 10;
    bool uns = false;
    int longRank = 0;
    bool checkChar = false;
    uint32_t ch = 0;
    const char* decoded = nullptr;
};

static Exp T(Tok k, const char* text, int line, int col, bool lead = false) {
    Exp e;
    e.kind = k;
    e.text = text;
    e.line = line;
    e.col = col;
    e.lead = lead;
    return e;
}

static Exp Kw(const char* text, int line, int col, bool lead = false) {
    return T(Tok::Keyword, text, line, col, lead);
}
static Exp Id(const char* text, int line, int col, bool lead = false) {
    return T(Tok::Ident, text, line, col, lead);
}
static Exp Pu(const char* text, int line, int col, bool lead = false) {
    return T(Tok::Punct, text, line, col, lead);
}
static Exp Nl(int line, int col, bool lead = false) { return T(Tok::Newline, "\n", line, col, lead); }

static Exp Int(const char* text, uint64_t v, int base, int line, int col, bool uns = false,
               int longRank = 0, bool lead = false) {
    Exp e = T(Tok::Int, text, line, col, lead);
    e.checkNum = true;
    e.uval = v;
    e.base = base;
    e.uns = uns;
    e.longRank = longRank;
    return e;
}

static Exp Ch(const char* text, uint32_t v, int line, int col, bool lead = false) {
    Exp e = T(Tok::CharLit, text, line, col, lead);
    e.checkChar = true;
    e.ch = v;
    return e;
}

static Exp Str(const char* text, const char* decoded, int line, int col, bool lead = false) {
    Exp e = T(Tok::StringLit, text, line, col, lead);
    e.decoded = decoded;
    return e;
}

static void run(const char* name, const std::string& src, const std::vector<Exp>& want,
                const char* errSub) {
    okaycc::Diagnostics d;
    okaycc::Lexer lx("t.c", src, d);
    std::vector<Token> got;
    for (;;) {
        Token t = lx.next();
        if (t.kind == Tok::End) break;
        got.push_back(std::move(t));
    }
    bool ok = true;
    if (errSub) {
        if (d.format().find(errSub) == std::string::npos) {
            std::fprintf(stderr, "FAIL %s: error missing '%s'\n%s", name, errSub, d.format().c_str());
            ok = false;
        }
    } else if (!d.ok()) {
        std::fprintf(stderr, "FAIL %s: unexpected error\n%s", name, d.format().c_str());
        ok = false;
    }
    if (errSub) {
        if (ok) ++okaycc_test::gPass;
        else ++okaycc_test::gFail;
        return;
    }
    if (got.size() != want.size()) {
        std::fprintf(stderr, "FAIL %s: %zu tokens, want %zu\n", name, got.size(), want.size());
        for (const Token& t : got) std::fprintf(stderr, "  %s\n", okaycc::formatToken(t).c_str());
        ok = false;
    } else {
        for (std::size_t i = 0; i < want.size(); ++i) {
            const Token& g = got[i];
            const Exp& w = want[i];
            if (g.kind != w.kind || g.text != w.text || g.line != w.line || g.column != w.col ||
                g.leadSpace != w.lead) {
                std::fprintf(stderr, "FAIL %s token %zu: got %s\n", name, i,
                             okaycc::formatToken(g).c_str());
                ok = false;
                break;
            }
            if (w.checkNum && (g.uval != w.uval || g.base != w.base || g.isUnsigned != w.uns ||
                               g.longRank != w.longRank)) {
                std::fprintf(stderr, "FAIL %s token %zu number fields: %s\n", name, i,
                             okaycc::formatToken(g).c_str());
                ok = false;
                break;
            }
            if (w.checkChar && g.charValue != w.ch) {
                std::fprintf(stderr, "FAIL %s token %zu char value %u want %u\n", name, i, g.charValue,
                             w.ch);
                ok = false;
                break;
            }
            if (w.decoded && g.decoded != w.decoded) {
                std::fprintf(stderr, "FAIL %s token %zu decoded mismatch\n", name, i);
                ok = false;
                break;
            }
        }
    }
    if (ok) {
        ++okaycc_test::gPass;
    } else {
        ++okaycc_test::gFail;
    }
}

static void runOk(const char* name, const std::string& src, const std::vector<Exp>& want) {
    run(name, src, want, nullptr);
}

static void checkFloat(const char* src, double want) {
    okaycc::Diagnostics d;
    okaycc::Lexer lx("t.c", src, d);
    Token t = lx.next();
    bool ok = t.kind == Tok::Float && d.ok() && std::fabs(t.fval - want) < 1e-9;
    if (!ok) {
        std::fprintf(stderr, "FAIL float %s got %g kind %s err %s\n", src, t.fval,
                     okaycc::formatToken(t).c_str(), d.format().c_str());
        ++okaycc_test::gFail;
    } else {
        ++okaycc_test::gPass;
    }
}

int main() {
    runOk("kw and ident", "int main_1;",
          {Kw("int", 1, 1), Id("main_1", 1, 5, true), Pu(";", 1, 11)});

    runOk("keywords", "return if else while for char void sizeof _Bool",
          {Kw("return", 1, 1), Kw("if", 1, 8, true), Kw("else", 1, 11, true), Kw("while", 1, 16, true),
           Kw("for", 1, 22, true), Kw("char", 1, 26, true), Kw("void", 1, 31, true),
           Kw("sizeof", 1, 36, true), Kw("_Bool", 1, 43, true)});

    runOk("not a keyword", "integer returning", {Id("integer", 1, 1), Id("returning", 1, 9, true)});

    runOk("decimal", "0 7 42",
          {Int("0", 0, 8, 1, 1), Int("7", 7, 10, 1, 3, false, 0, true),
           Int("42", 42, 10, 1, 5, false, 0, true)});

    runOk("hex and oct", "0x10 0Xff 010",
          {Int("0x10", 16, 16, 1, 1), Int("0Xff", 255, 16, 1, 6, false, 0, true),
           Int("010", 8, 8, 1, 11, false, 0, true)});

    runOk("suffixes", "1u 2U 3l 4L 5ll 6LL 7ul 8lu 9ull 10llu",
          {Int("1u", 1, 10, 1, 1, true, 0), Int("2U", 2, 10, 1, 4, true, 0, true),
           Int("3l", 3, 10, 1, 7, false, 1, true), Int("4L", 4, 10, 1, 10, false, 1, true),
           Int("5ll", 5, 10, 1, 13, false, 2, true), Int("6LL", 6, 10, 1, 17, false, 2, true),
           Int("7ul", 7, 10, 1, 21, true, 1, true), Int("8lu", 8, 10, 1, 25, true, 1, true),
           Int("9ull", 9, 10, 1, 29, true, 2, true), Int("10llu", 10, 10, 1, 34, true, 2, true)});

    run("bad octal", "08", {}, "octal");
    run("bad hex", "0x", {}, "hexadecimal");
    run("bad suffix", "123abc", {}, "suffix");
    run("mixed long", "1Ll", {}, "mixed case");
    run("lul", "1lul", {}, "suffix");
    run("too big", "18446744073709551616", {}, "too large");

    runOk("max u64", "18446744073709551615", {Int("18446744073709551615", UINT64_MAX, 10, 1, 1)});

    runOk("floats spellings", "1.5 .5 1. 1e2 1e-2 1.5E+2 2.f 3.L",
          {T(Tok::Float, "1.5", 1, 1), T(Tok::Float, ".5", 1, 5, true), T(Tok::Float, "1.", 1, 8, true),
           T(Tok::Float, "1e2", 1, 11, true), T(Tok::Float, "1e-2", 1, 15, true),
           T(Tok::Float, "1.5E+2", 1, 20, true), T(Tok::Float, "2.f", 1, 27, true),
           T(Tok::Float, "3.L", 1, 31, true)});

    checkFloat("1.5", 1.5);
    checkFloat(".5", 0.5);
    checkFloat("1.", 1.0);
    checkFloat("1e2", 100.0);
    checkFloat("1e-2", 0.01);
    checkFloat("1.5E+2", 150.0);
    checkFloat("0x1.8p1", 3.0);
    checkFloat("0x1p+4", 16.0);
    checkFloat("0x.1p0", 0.0625);
    checkFloat("0x1.0p-1", 0.5);

    runOk("hex float token", "0x1.8p1", {T(Tok::Float, "0x1.8p1", 1, 1)});
    runOk("hex int then float", "0x1.2",
          {Int("0x1", 1, 16, 1, 1), T(Tok::Float, ".2", 1, 4)});

    run("bad exp", "1e+", {}, "exponent");
    runOk("hex int dot is a second token", "0x1.0",
          {Int("0x1", 1, 16, 1, 1), T(Tok::Float, ".0", 1, 4)});
    run("bad p exp", "0x1p", {}, "exponent");
    run("bad hex float digits", "0x.p1", {}, "no digits");

    runOk("chars", "'a' '\\n' '\\'' '\\\\' '\\x41' '\\101'",
          {Ch("'a'", 'a', 1, 1), Ch("'\\n'", 10, 1, 5, true), Ch("'\\''", '\'', 1, 10, true),
           Ch("'\\\\'", '\\', 1, 15, true), Ch("'\\x41'", 0x41, 1, 20, true),
           Ch("'\\101'", 65, 1, 27, true)});

    runOk("multi char", "'AB'", {Ch("'AB'", (static_cast<uint32_t>('A') << 8) | 'B', 1, 1)});

    runOk("strings", "\"hi\" \"a\\nb\" \"quote:\\\"\"",
          {Str("\"hi\"", "hi", 1, 1), Str("\"a\\nb\"", "a\nb", 1, 6, true),
           Str("\"quote:\\\"\"", "quote:\"", 1, 13, true)});

    runOk("u8 prefix", "u8\"A\" u\"B\" U\"C\" L\"D\"",
          {Str("u8\"A\"", "A", 1, 1), Str("u\"B\"", "B", 1, 7, true), Str("U\"C\"", "C", 1, 12, true),
           Str("L\"D\"", "D", 1, 17, true)});

    runOk("ucn string", "\"\\u0041\"", {Str("\"\\u0041\"", "A", 1, 1)});

    run("empty char", "''", {}, "empty character");
    run("bad escape", "\"\\q\"", {}, "unknown escape");
    run("unterminated string", "\"abc", {}, "unterminated string");
    run("unterminated char", "'a", {}, "unterminated character");
    run("unterminated comment", "/* hi", {}, "unterminated block comment");

    runOk("punct munch", "<<= >>= ... -> && || ++ -- <= >= == != ## %:%:",
          {Pu("<<=", 1, 1), Pu(">>=", 1, 5, true), Pu("...", 1, 9, true), Pu("->", 1, 13, true),
           Pu("&&", 1, 16, true), Pu("||", 1, 19, true), Pu("++", 1, 22, true), Pu("--", 1, 25, true),
           Pu("<=", 1, 28, true), Pu(">=", 1, 31, true), Pu("==", 1, 34, true), Pu("!=", 1, 37, true),
           Pu("##", 1, 40, true), Pu("%:%:", 1, 43, true)});

    runOk("two dots", "..", {Pu(".", 1, 1), Pu(".", 1, 2)});
    runOk("hash not paste", "#", {Pu("#", 1, 1)});

    runOk("line comment", "a // hi\nb",
          {Id("a", 1, 1), Nl(1, 8, true), Id("b", 2, 1)});

    runOk("block comment keeps line", "a/*\nb*/c", {Id("a", 1, 1), Id("c", 2, 4, true)});

    runOk("comment is not nested", "/* /* */ int", {Kw("int", 1, 10, true)});

    runOk("splice ident", "in\\\nt x", {Kw("int", 1, 1), Id("x", 2, 3, true)});

    runOk("splice in string", "\"a\\\nb\"", {Str("\"ab\"", "ab", 1, 1)});

    runOk("columns", "int x;\n",
          {Kw("int", 1, 1), Id("x", 1, 5, true), Pu(";", 1, 6), Nl(1, 7)});

    runOk("tab column", "\tint", {Kw("int", 1, 9, true)});

    runOk("leading space", "  x", {Id("x", 1, 3, true)});

    runOk("digraphs", "<: :> <% %> %:",
          {Pu("<:", 1, 1), Pu(":>", 1, 4, true), Pu("<%", 1, 7, true), Pu("%>", 1, 10, true),
           Pu("%:", 1, 13, true)});

    run("stray", "@", {}, "stray");

    runOk("dot splits two floats", "1.2.3",
          {T(Tok::Float, "1.2", 1, 1), T(Tok::Float, ".3", 1, 4)});
    runOk("slash assign", "a/=b", {Id("a", 1, 1), Pu("/=", 1, 2), Id("b", 1, 4)});
    runOk("only a comment", "/* nothing */", {});
    runOk("wide char", "L'A' u'B'",
          {Ch("L'A'", 'A', 1, 1), Ch("u'B'", 'B', 1, 6, true)});

    {
        okaycc::Diagnostics d;
        okaycc::Lexer lx("h.c", "#include \"a.h\"\n", d);
        Token hash = lx.next();
        Token inc = lx.next();
        EXPECT(hash.kind == Tok::Punct && hash.text == "#");
        EXPECT(inc.kind == Tok::Ident && inc.text == "include");
        std::string name;
        bool angle = true;
        int line = 0, col = 0;
        bool ok = lx.readHeaderName(name, angle, line, col);
        EXPECT(ok);
        EXPECT(!angle);
        EXPECT_EQ(name, "a.h");
        Token nl = lx.next();
        EXPECT(nl.kind == Tok::Newline);
        EXPECT(d.ok());
    }
    {
        okaycc::Diagnostics d;
        okaycc::Lexer lx("h.c", "<stdio.h>", d);
        std::string name;
        bool angle = false;
        int line = 0, col = 0;
        bool ok = lx.readHeaderName(name, angle, line, col);
        EXPECT(ok);
        EXPECT(angle);
        EXPECT_EQ(name, "stdio.h");
    }
    {
        okaycc::Diagnostics d;
        okaycc::Lexer lx("h.c", "u8 id", d);
        Token a = lx.next();
        Token b = lx.next();
        EXPECT(a.kind == Tok::Ident && a.text == "u8");
        EXPECT(b.kind == Tok::Ident && b.text == "id");
    }

    return okaycc_test::done("lexer");
}
