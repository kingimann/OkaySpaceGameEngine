// The OkayScript lexer: every token kind, its line and column, comments, and lexer errors with their line.
#include "TestKit.hpp"
#include "okayscript/Lexer.hpp"
#include <vector>

using namespace okayscript;

static std::vector<Token> Lex(const std::string& s) { Lexer lx(s); return lx.Scan(); }
static std::vector<Tok> Kinds(const std::string& s) {
    std::vector<Tok> k;
    for (auto& t : Lex(s)) k.push_back(t.type);
    return k;
}
static std::string KindsText(const std::string& s) {
    std::string out;
    for (Tok t : Kinds(s)) { if (!out.empty()) out += ' '; out += TokName(t); }
    return out;
}

int main() {
    TEST("every operator and punctuation token");
    CHECK_EQ(KindsText("+ - * / % = += -= *= /= %= ++ --"),
             std::string("'+' '-' '*' '/' '%' '=' '+=' '-=' '*=' '/=' '%=' '++' '--' end of file"));
    CHECK_EQ(KindsText("== != < > <= >= ! && || & | ^"),
             std::string("'==' '!=' '<' '>' '<=' '>=' '!' '&&' '||' '&' '|' '^' end of file"));
    CHECK_EQ(KindsText("( ) { } [ ] , ; : ? ?? ?\?= ?. . =>"),
             std::string("'(' ')' '{' '}' '[' ']' ',' ';' ':' '?' '?\?' '?\?=' '?.' '.' '=>' end of file"));

    TEST("keywords and identifiers");
    CHECK_EQ(KindsText("var if else while for in function return true false break continue"),
             std::string("'var' 'if' 'else' 'while' 'for' 'in' 'function' 'return' 'true' 'false' 'break' 'continue' end of file"));
    {
        auto t = Lex("score _x foreach switch");
        CHECK_EQ(t.size(), (std::size_t)5);
        CHECK(t[0].type == Tok::Ident && t[0].text == "score");
        CHECK(t[1].type == Tok::Ident && t[1].text == "_x");
        CHECK(t[2].type == Tok::Ident && t[2].text == "foreach");   // statement words the parser reads as identifiers
        CHECK(t[3].type == Tok::Ident && t[3].text == "switch");
    }
    TEST("a dotted name is one identifier token");
    {
        auto t = Lex("xs.Count Math.PI (a).b");
        CHECK(t[0].type == Tok::Ident && t[0].text == "xs.Count");
        CHECK(t[1].type == Tok::Ident && t[1].text == "Math.PI");
        CHECK(t[2].type == Tok::LParen);
        CHECK(t[4].type == Tok::RParen);
        CHECK(t[5].type == Tok::Dot);   // after ')' the '.' is its own token
        CHECK(t[6].type == Tok::Ident && t[6].text == "b");
    }

    TEST("numbers");
    {
        auto t = Lex("42 3.5 .25 7f 2d");
        CHECK(t[0].type == Tok::Number && t[0].number == 42.0);
        CHECK(t[1].type == Tok::Number && t[1].number == 3.5);
        CHECK(t[2].type == Tok::Number && t[2].number == 0.25);
        CHECK(t[3].type == Tok::Number && t[3].number == 7.0 && t[3].suffix);
        CHECK(t[4].type == Tok::Number && t[4].number == 2.0 && t[4].suffix);
        CHECK(!t[0].suffix);
    }
    TEST("strings, escapes, char literals, interpolated strings");
    {
        auto t = Lex("\"a\\tb\\n\\\"q\\\"\" 'x' '\\n' $\"hi {name}\"");
        CHECK(t[0].type == Tok::String);
        CHECK_EQ(t[0].text, std::string("a\tb\n\"q\""));
        CHECK(t[1].type == Tok::String && t[1].text == "x");
        CHECK(t[2].type == Tok::String && t[2].text == "\n");
        CHECK(t[3].type == Tok::InterpString);
        CHECK_EQ(t[3].text, std::string("hi {name}"));
    }
    TEST("comments are skipped: //, /* */ and #");
    CHECK_EQ(KindsText("1 // two\n/* three\nfour */ 5 # six\n7"), std::string("number number number end of file"));

    TEST("lines and columns");
    {
        auto t = Lex("var a = 1\n  b += 2\n\n/* x\n y */ c");
        CHECK_EQ(t[0].line, 1); CHECK_EQ(t[0].column, 1);
        CHECK_EQ(t[1].line, 1); CHECK_EQ(t[1].column, 5);
        CHECK_EQ(t[4].line, 2); CHECK_EQ(t[4].column, 3);   // b
        CHECK_EQ(t[5].line, 2); CHECK_EQ(t[5].column, 5);   // +=
        CHECK_EQ(t[7].line, 5); CHECK_EQ(t[7].text, std::string("c"));
        CHECK_EQ(t.back().type == Tok::End, true);
    }

    TEST("lexer errors carry their line and column");
    {
        auto err = [](const std::string& s, int& line, int& col) {
            try { Lex(s); } catch (const ScriptError& e) { line = e.line; col = e.column; return std::string(e.what()); }
            return std::string("no error");
        };
        int line = 0, col = 0;
        CHECK_EQ(err("var a = 1\nvar s = \"open", line, col), std::string("unterminated string"));
        CHECK_EQ(line, 2); CHECK_EQ(col, 9);
        CHECK_EQ(err("x\n\n  y @ z", line, col), std::string("unexpected character '@'"));
        CHECK_EQ(line, 3); CHECK_EQ(col, 5);
        CHECK_EQ(err("'ab", line, col), std::string("unterminated character literal"));
        CHECK_EQ(line, 1);
        CHECK_EQ(err("\n$\"never closed", line, col), std::string("unterminated string"));
        CHECK_EQ(line, 2);
    }
    TEST("a lexer error from a whole program is a SyntaxError with the line");
    CHECK_EQ(oktest::Run("print(1)\nprint(2)\nvar q = 5 ` 3"), std::string("ERROR syntax 3: unexpected character '`'"));
    TEST_RESULT();
}
