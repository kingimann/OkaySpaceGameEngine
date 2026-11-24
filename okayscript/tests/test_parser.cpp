// The OkayScript parser: operator precedence and grouping, the shape of statements, and parse errors with lines.
#include "TestKit.hpp"
#include "okayscript/Lexer.hpp"
#include "okayscript/Parser.hpp"

using namespace okayscript;

static std::string E(const std::string& src) {
    Lexer lx(src);
    Parser p(lx.Scan());
    ExprPtr e = p.ParseExpression();
    return e->Dump();
}
static std::string P(const std::string& src) {
    Program prog;
    ParseSource(src, prog);
    return DumpList(prog.top);
}
static std::string Err(const std::string& src) {
    Program prog;
    try { ParseSource(src, prog); } catch (const SyntaxError& e) { return std::to_string(e.line) + ": " + e.what(); }
    return "no error";
}

int main() {
    TEST("arithmetic precedence and left grouping");
    CHECK_EQ(E("1 + 2 * 3"), std::string("(+ 1 (* 2 3))"));
    CHECK_EQ(E("(1 + 2) * 3"), std::string("(* (+ 1 2) 3)"));
    CHECK_EQ(E("10 - 4 - 3"), std::string("(- (- 10 4) 3)"));
    CHECK_EQ(E("8 / 2 % 3 * 4"), std::string("(* (% (/ 8 2) 3) 4)"));
    CHECK_EQ(E("-2 * -x"), std::string("(* (neg 2) (neg x))"));
    TEST("comparison, equality, logic and bit precedence");
    CHECK_EQ(E("a + 1 < b * 2"), std::string("(< (+ a 1) (* b 2))"));
    CHECK_EQ(E("a < b == c > d"), std::string("(== (< a b) (> c d))"));
    CHECK_EQ(E("a || b && c"), std::string("(|| a (&& b c))"));
    CHECK_EQ(E("!a && b == c"), std::string("(&& (! a) (== b c))"));
    CHECK_EQ(E("a | b ^ c & d"), std::string("(| a (^ b (& c d)))"));
    CHECK_EQ(E("a == b & c"), std::string("(& (== a b) c)"));
    TEST("ternary, ?? and assignment");
    CHECK_EQ(E("a ? b : c ? d : e"), std::string("(? a b (? c d e))"));
    CHECK_EQ(E("a ?? b ?? c"), std::string("(?? a (?? b c))"));
    CHECK_EQ(E("a || b ? 1 : 2"), std::string("(? (|| a b) 1 2)"));
    CHECK_EQ(E("x = y = 3"), std::string("(= x (= y 3))"));
    CHECK_EQ(E("x += 2 * 3"), std::string("(+= x (* 2 3))"));   // += is also how events subscribe
    CHECK_EQ(E("x++"), std::string("(= x (+ x 1))"));
    CHECK_EQ(E("--x"), std::string("(= x (- x 1))"));
    CHECK_EQ(E("x ?\?= 5"), std::string("(?\?= x 5)"));
    TEST("postfix: calls, methods, index, members, casts");
    CHECK_EQ(E("f(1, g(2), b: 3)"), std::string("(call f 1 (call g 2) b:3)"));
    CHECK_EQ(E("xs[i + 1][0]"), std::string("([] ([] xs (+ i 1)) 0)"));
    CHECK_EQ(E("xs[0] = 4"), std::string("([]= xs 0 4)"));
    CHECK_EQ(E("xs[0] *= 4"), std::string("([]*= xs 0 4)"));
    CHECK_EQ(E("(a + b).ToString()"), std::string("(method (+ a b) ToString)"));
    CHECK_EQ(E("s.Trim().Length"), std::string("(. (call s.Trim) Length)"));   // `s.Trim` is one dotted name
    CHECK_EQ(E("(int)x / 2"), std::string("(/ (cast int x) 2)"));
    CHECK_EQ(E("[1, \"a\", true]"), std::string("(list 1 \"a\" true)"));
    CHECK_EQ(E("$\"n={n + 1}!\""), std::string("(interp \"n=\" (+ n 1) \"!\")"));
    CHECK_EQ(E("x => x * 2"), std::string("(lambda (x) (block (return (* x 2))))"));
    CHECK_EQ(E("(a, b) => a + b"), std::string("(lambda (a b) (block (return (+ a b))))"));
    CHECK_EQ(E("new List<int>()"), std::string("(call __new_list)"));
    CHECK_EQ(E("new int[3]"), std::string("(call __new_array 3)"));

    TEST("statement shapes");
    CHECK_EQ(P("var a = 1; int b; const string s = \"x\""),
             std::string("(block (var a 1) (var an int b nil) (var a string const s \"x\"))"));
    CHECK_EQ(P("if (a) b() else if (c) d() else { e() }"),
             std::string("(block (if a (block (call b)) (block (if c (block (call d)) (block (call e))))))"));
    CHECK_EQ(P("while (i < 3) i++"), std::string("(block (while (< i 3) (block (= i (+ i 1)))))"));
    CHECK_EQ(P("for (var i = 0; i < 3; i++) { continue }"),
             std::string("(block (for (var i 0) (< i 3) (= i (+ i 1)) (block (continue))))"));
    CHECK_EQ(P("for x in xs print(x)"), std::string("(block (foreach x xs (block (call print x))))"));
    CHECK_EQ(P("foreach (var x in xs) { break }"), std::string("(block (foreach x xs (block (break))))"));
    CHECK_EQ(P("do { i++ } while (i < 2)"), std::string("(block (do (block (= i (+ i 1))) (< i 2)))"));
    CHECK_EQ(P("switch (k) { case 1: a(); break; default: b() }"),
             std::string("(block (switch k (case 1 (block (call a) (break))) (default (block (call b)))))"));
    CHECK_EQ(P("try { throw \"x\" } catch (e) { print(e) }"),
             std::string("(block (try (block (throw \"x\")) (catch e (block (call print e)))))"));
    CHECK_EQ(P("int a = 1, b = 2"), std::string("(block (multi (var an int a 1) (var an int b 2)))"));
    TEST("functions are hoisted out of the top level, in all three spellings");
    {
        Program prog;
        ParseSource("function add(a, b) { return a + b }\nsq(x) { return x * x }\nint Half(int n, int d = 2) { return n / d }\nprint(add(1, 2))", prog);
        CHECK_EQ(prog.functions.size(), (std::size_t)3);
        CHECK_EQ(prog.top.size(), (std::size_t)1);
        CHECK_EQ(prog.functions.at("add").params.size(), (std::size_t)2);
        CHECK_EQ(DumpList(prog.functions.at("add").body), std::string("(block (return (+ a b)))"));
        CHECK_EQ(prog.functions.at("sq").line, 2);
        CHECK_EQ((int)prog.functions.at("Half").returnKind, (int)KInt);
        CHECK((bool)prog.functions.at("Half").defaults[1]);
        CHECK_EQ(prog.functions.at("Half").line, 3);
    }
    TEST("enums become numbers");
    CHECK_EQ(P("enum State { Idle, Run = 5, Jump }\nprint(State.Jump)"), std::string("(block (call print 6))"));
    TEST("statements and expressions remember their line");
    {
        Program prog;
        ParseSource("var a = 1\n\nprint(a)\nif (a)\n  b()", prog);
        CHECK_EQ(prog.top[0]->line, 1);
        CHECK_EQ(prog.top[1]->line, 3);
        CHECK_EQ(prog.top[2]->line, 4);
        auto* iff = dynamic_cast<IfStmt*>(prog.top[2].get());
        CHECK(iff != nullptr);
        if (iff) CHECK_EQ(iff->thenB[0]->line, 5);
    }

    TEST("parse errors carry the line");
    CHECK_EQ(Err("var a = 1\nvar b = (2 + 3\nprint(b)"), std::string("3: parse error: expected ')', got 'print'"));
    CHECK_EQ(Err("print(1)\nvar = 4"), std::string("2: parse error: expected variable name, got '='"));
    CHECK_EQ(Err("x = 1\n\nif x > 1 { }"), std::string("3: parse error: expected '(', got 'x'"));
    CHECK_EQ(Err("function f() {\n  return 1\n"), std::string("3: parse error: expected '}', got end of file"));
    CHECK_EQ(Err("a = 1 +\n"), std::string("2: parse error: unexpected end of file"));
    CHECK_EQ(Err("1 = 2"), std::string("1: invalid assignment target"));
    CHECK_EQ(Err("f(a, a) { }"), std::string("1: parameter 'a' is already in this function"));
    CHECK_EQ(Err("print(1)\nprint($\"{1 + }\")"), std::string("2: parse error: unexpected end of file"));
    {
        std::string deep(300, '(');
        CHECK_EQ(Err("x = " + deep + "1"), std::string("1: parse error: expression nested too deeply"));
    }
    TEST_RESULT();
}
