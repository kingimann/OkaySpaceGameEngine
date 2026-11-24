// Running OkayScript: variables and scopes, arithmetic, comparisons and logic, control flow, functions, lambdas,
// typed variables, text/list/map methods. Each check runs real code and compares what it printed or left behind.
#include "TestKit.hpp"

using oktest::Eval;
using oktest::Run;
using okayscript::Interpreter;
using okayscript::Value;

int main() {
    TEST("variables: declare, assign, read, globals made by assignment");
    CHECK_EQ(Run("var a = 2\na = a + 3\nprint(a)"), std::string("5\n"));
    CHECK_EQ(Run("b = 7\nprint(b * 2)"), std::string("14\n"));
    CHECK_EQ(Run("print(never_set)"), std::string("0\n"));   // an unset name reads as 0 (the engine's null)
    {
        Interpreter in;
        okayscript::InstallStandardLibrary(in);
        CHECK_EQ(Run("var total = 0\nfor (var i = 1; i <= 4; i++) total += i", &in), std::string(""));
        CHECK_EQ(in.GetGlobal("total").AsNumber(), 10.0);   // state left behind in the globals
        CHECK_EQ(in.GetGlobal("i").AsNumber(), 5.0);
        in.SetGlobal("bonus", Value{5.0});
        CHECK_EQ(Run("total = total + bonus", &in), std::string(""));
        CHECK_EQ(in.GetGlobal("total").AsNumber(), 15.0);
    }

    TEST("scopes: parameters and locals shadow globals; a call's locals end with it");
    CHECK_EQ(Run("var x = 1\nfunction f(x) { x = x + 10; return x }\nprint(f(5), x)"), std::string("15 1\n"));
    CHECK_EQ(Run("var x = 1\nfunction f() { var x = 99; return x }\nprint(f(), x)"), std::string("99 1\n"));
    CHECK_EQ(Run("function f() { var tmp = 3; return tmp }\nf()\nprint(tmp)"), std::string("0\n"));
    CHECK_EQ(Run("var g = 1\nfunction f() { g = 2 }\nf()\nprint(g)"), std::string("2\n"));   // assigning a global from a function
    // Blocks do not open a scope (function-level scoping, as in the engine).
    CHECK_EQ(Run("if (true) { var inner = 4 }\nprint(inner)"), std::string("4\n"));
    // Dynamic scoping (engine rule): a function sees the locals of the function that called it.
    CHECK_EQ(Run("function show() { return who }\nfunction caller() { var who = \"caller\"; return show() }\nprint(caller())"),
             std::string("caller\n"));

    TEST("arithmetic");
    CHECK_EQ(Eval("1 + 2 * 3 - 4 / 2"), std::string("5"));
    CHECK_EQ(Eval("(1 + 2) * 3"), std::string("9"));
    CHECK_EQ(Eval("7 % 3"), std::string("1"));
    CHECK_EQ(Eval("-7 % 3"), std::string("-1"));
    CHECK_EQ(Eval("10 / 4"), std::string("2.5"));
    CHECK_EQ(Eval("1 / 0"), std::string("0"));             // the engine rule: a division by zero gives 0
    CHECK_EQ(Eval("0.1 + 0.2"), std::string("0.3"));
    CHECK_EQ(Eval("2 * -3"), std::string("-6"));
    CHECK_EQ(Eval("16777217 + 1"), std::string("16777218"));   // 64-bit numbers (the engine keeps 32-bit floats)
    CHECK_EQ(Eval("6 & 3"), std::string("2"));
    CHECK_EQ(Eval("6 | 3"), std::string("7"));
    CHECK_EQ(Eval("6 ^ 3"), std::string("5"));
    CHECK_EQ(Eval("\"a\" + 1 + 2"), std::string("a12"));
    CHECK_EQ(Eval("1 + 2 + \"a\""), std::string("3a"));
    CHECK_EQ(Eval("(int)7.9"), std::string("7"));
    CHECK_EQ(Eval("(int)-7.9"), std::string("-7"));
    CHECK_EQ(Eval("(string)5 + 1"), std::string("51"));
    CHECK_EQ(Eval("(float)\"2.5\" * 2"), std::string("5"));
    CHECK_EQ(Run("var n = 5\nn += 2\nn -= 1\nn *= 3\nn /= 2\nn %= 4\nprint(n)"), std::string("1\n"));
    CHECK_EQ(Run("var n = 5\nn++\n++n\nn--\nprint(n)"), std::string("6\n"));

    TEST("comparisons and equality");
    CHECK_EQ(Eval("[1 < 2, 2 < 1, 2 <= 2, 3 >= 4, 5 > 4]"), std::string("[true, false, true, false, true]"));
    CHECK_EQ(Eval("[1 == 1, 1 != 1, \"a\" == \"a\", \"1\" == 1, true == 1]"), std::string("[true, false, true, true, true]"));
    CHECK_EQ(Eval("\"abc\" != \"abd\""), std::string("true"));

    TEST("logic and short-circuiting");
    CHECK_EQ(Eval("[true && false, true || false, !true, !0, !\"\"]"), std::string("[false, true, false, true, true]"));
    CHECK_EQ(Run("var hits = 0\nfunction hit() { hits++; return true }\nvar a = false && hit()\nvar b = true || hit()\nprint(hits, a, b)"),
             std::string("0 false true\n"));
    CHECK_EQ(Run("var hits = 0\nfunction hit() { hits++; return true }\nvar a = true && hit()\nvar b = false || hit()\nprint(hits, a, b)"),
             std::string("2 true true\n"));
    CHECK_EQ(Eval("0 ?? 5"), std::string("5"));
    CHECK_EQ(Eval("3 ?? 5"), std::string("3"));
    CHECK_EQ(Run("var c\nc ?\?= 4\nc ?\?= 9\nprint(c)"), std::string("4\n"));
    CHECK_EQ(Eval("1 > 2 ? \"yes\" : \"no\""), std::string("no"));

    TEST("if / else if / else");
    CHECK_EQ(Run("function grade(s) { if (s >= 90) return \"A\"; else if (s >= 80) return \"B\"; else { return \"C\" } }\n"
                 "print(grade(95), grade(85), grade(10))"), std::string("A B C\n"));
    CHECK_EQ(Run("if (0) print(\"no\")\nif (\"x\") print(\"yes\")"), std::string("yes\n"));

    TEST("while, do-while, for, for-in, foreach");
    CHECK_EQ(Run("var i = 0\nwhile (i < 3) { print(i); i++ }"), std::string("0\n1\n2\n"));
    CHECK_EQ(Run("var i = 10\ndo { print(i); i++ } while (i < 3)"), std::string("10\n"));
    CHECK_EQ(Run("for (var i = 0; i < 6; i += 2) print(i)"), std::string("0\n2\n4\n"));
    CHECK_EQ(Run("for x in [\"a\", \"b\"] print(x)"), std::string("a\nb\n"));
    CHECK_EQ(Run("var sum = 0\nforeach (var v in [1, 2, 3]) sum += v\nprint(sum)"), std::string("6\n"));
    CHECK_EQ(Run("var m = new Dictionary<string, int>()\nm[\"b\"] = 2\nm[\"a\"] = 1\nforeach (var k in m) print(k)"), std::string("a\nb\n"));
    CHECK_EQ(Run("var m = new Dictionary<string, int>()\nm[\"x\"] = 7\nforeach (var kv in m) print(kv.Key, kv.Value)"), std::string("x 7\n"));

    TEST("break and continue");
    CHECK_EQ(Run("for (var i = 0; i < 10; i++) { if (i == 3) break; if (i == 1) continue; print(i) }"), std::string("0\n2\n"));
    CHECK_EQ(Run("var i = 0\nwhile (true) { i++; if (i < 3) continue; break }\nprint(i)"), std::string("3\n"));
    CHECK_EQ(Run("for x in [1, 2, 3, 4] { if (x == 2) continue; if (x == 4) break; print(x) }"), std::string("1\n3\n"));
    CHECK_EQ(Run("for (var i = 0; i < 2; i++) { for (var j = 0; j < 3; j++) { if (j == 1) break; print(i, j) } }"),
             std::string("0 0\n1 0\n"));

    TEST("switch with fall-through and default");
    CHECK_EQ(Run("function f(k) { switch (k) { case 1: print(\"one\"); case 2: print(\"two\"); break; default: print(\"other\") } }\n"
                 "f(1)\nf(2)\nf(9)"), std::string("one\ntwo\ntwo\nother\n"));
    CHECK_EQ(Run("switch (\"b\") { case \"a\": print(1); break; case \"b\": print(2); break }"), std::string("2\n"));

    TEST("functions: arguments, return values, defaults, named arguments, recursion");
    CHECK_EQ(Run("function add(a, b) { return a + b }\nprint(add(2, 3))"), std::string("5\n"));
    CHECK_EQ(Run("print(later(4))\nfunction later(n) { return n * 10 }"), std::string("40\n"));   // hoisted
    CHECK_EQ(Run("function none() { }\nprint(none())"), std::string("0\n"));
    CHECK_EQ(Run("function early(n) { if (n > 0) { return \"pos\" } return \"not\" }\nprint(early(1), early(-1))"), std::string("pos not\n"));
    CHECK_EQ(Run("function f(a, b = 10) { return a + b }\nprint(f(1), f(1, 2))"), std::string("11 3\n"));
    CHECK_EQ(Run("function f(a, b) { return a - b }\nprint(f(b: 1, a: 10))"), std::string("9\n"));
    CHECK_EQ(Run("function f(a) { return a }\nprint(f(1, 2, 3))"), std::string("1\n"));   // extra arguments are ignored
    CHECK_EQ(Run("function f(a, b) { return b }\nprint(f(1))"), std::string("0\n"));      // missing ones are 0
    CHECK_EQ(Run("function fact(n) { if (n <= 1) return 1; return n * fact(n - 1) }\nprint(fact(10))"), std::string("3628800\n"));
    CHECK_EQ(Run("function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2) }\nprint(fib(20))"), std::string("6765\n"));
    CHECK_EQ(Run("function even(n) { return n == 0 ? true : odd(n - 1) }\nfunction odd(n) { return n == 0 ? false : even(n - 1) }\n"
                 "print(even(10), odd(7))"), std::string("true true\n"));
    CHECK_EQ(Run("f(x) { return x * 3 }\nint Twice(int n) { return n * 2 }\nprint(f(2), Twice(4))"), std::string("6 8\n"));
    {
        Interpreter in;
        okayscript::InstallStandardLibrary(in);
        in.Run("function area(w, h) { return w * h }");
        CHECK(in.HasFunction("area"));
        CHECK_EQ(in.Call("area", {Value{3.0}, Value{4.0}}).AsNumber(), 12.0);   // called from C++
        CHECK_EQ(in.FunctionParams("area").size(), (std::size_t)2);
    }

    TEST("lambdas");
    CHECK_EQ(Run("var sq = x => x * x\nprint(sq(9))"), std::string("81\n"));
    CHECK_EQ(Run("var add = (a, b) => { return a + b }\nprint(add(2, 5))"), std::string("7\n"));
    CHECK_EQ(Run("var xs = [1, 2, 3, 4]\nprint(xs.Where(x => x % 2 == 0).Select(x => x * 10))"), std::string("[20, 40]\n"));
    CHECK_EQ(Run("function apply(f, v) { return f(v) }\nprint(apply(n => n + 1, 41))"), std::string("42\n"));

    TEST("declaration words stop at the end of the line (engine parser bug, fixed here)");
    CHECK_EQ(Run("var c\nc ?\?= 4\nprint(c)"), std::string("4\n"));
    CHECK_EQ(Run("var a\nb = 3\nprint(a, b)"), std::string("0 3\n"));
    CHECK_EQ(Run("int n\nn = 2.5\nprint(n)"), std::string("2\n"));

    TEST("typed variables convert or refuse");
    CHECK_EQ(Run("int n = 3.9\nprint(n)\nn = 7.2\nprint(n)"), std::string("3\n7\n"));
    CHECK_EQ(Run("bool b = 1\nstring s = \"x\"\nfloat f = 2\nprint(b, s, f)"), std::string("true x 2\n"));
    CHECK_EQ(Run("int n = 1\nn = \"text\""), std::string("ERROR runtime 2: 'n' is an int, so it can't hold text (use int.Parse(text))"));
    CHECK_EQ(Run("const int MAX = 3\nprint(MAX)\nMAX = 4"), std::string("3\nERROR runtime 3: 'MAX' is const, so it can't change after it's set"));
    CHECK_EQ(Run("int Half(int n) { return n / 2 }\nprint(Half(7))"), std::string("3\n"));   // the return type converts

    TEST("text, lists and maps");
    CHECK_EQ(Run("var s = \"  Hello  \"\nprint(s.Trim().ToUpper(), s.Length, s.Trim().Length)"), std::string("HELLO 9 5\n"));
    CHECK_EQ(Run("var s = \"a,b,c\"\nvar parts = s.Split(\",\")\nprint(parts, parts.Count, s.Replace(\",\", \"-\"), s.IndexOf(\"b\"))"),
             std::string("[a, b, c] 3 a-b-c 2\n"));
    CHECK_EQ(Run("var s = \"abc\"\nprint(s[1], s.Substring(1, 1), s.Contains(\"bc\"), s.StartsWith(\"ab\"))"), std::string("b b true true\n"));
    CHECK_EQ(Run("var xs = []\nxs.Add(3)\nxs.Add(1)\nxs.Add(2)\nxs.Sort()\nprint(xs, xs.Count, xs.Contains(2), xs.IndexOf(3))"),
             std::string("[1, 2, 3] 3 true 2\n"));
    CHECK_EQ(Run("var xs = [5, 6]\nxs[0] = 9\nxs[2] = 7\nxs[1] += 4\nprint(xs, xs[10])"), std::string("[9, 10, 7] 0\n"));
    CHECK_EQ(Run("var a = [1]\nvar b = a\nb.Add(2)\nprint(a)"), std::string("[1, 2]\n"));   // lists are shared
    CHECK_EQ(Run("var m = new Dictionary<string, int> { {\"a\", 1} }\nm[\"b\"] = 2\nm.c = 3\nprint(m, m.Count, m.ContainsKey(\"b\"), m.b)"),
             std::string("{a: 1, b: 2, c: 3} 3 true 2\n"));
    CHECK_EQ(Run("var m = new Dictionary<string, int>()\nm[\"k\"] += 5\nm[\"k\"] += 5\nprint(m[\"k\"], m[\"missing\"])"), std::string("10 0\n"));
    CHECK_EQ(Run("var xs = new int[3]\nprint(xs, len(xs))"), std::string("[0, 0, 0] 3\n"));
    CHECK_EQ(Run("var n = 3.14159\nprint(n.ToString(\"F2\"))"), std::string("3.14\n"));
    CHECK_EQ(Run("var name = \"Ada\"\nvar n = 2\nprint($\"{name} has {n * 2} items, {{braces}}\")"), std::string("Ada has 4 items, {braces}\n"));

    TEST("try / catch / throw");
    CHECK_EQ(Run("try { print(1); throw \"bad\"; print(2) } catch (e) { print(\"caught:\", e) }\nprint(3)"),
             std::string("1\ncaught: bad\n3\n"));
    CHECK_EQ(Run("try { nope() } catch (e) { print(e) }"), std::string("call to undefined function 'nope'\n"));
    CHECK_EQ(Run("function f() { try { return 5 } catch (e) { return 6 } }\nprint(f())"), std::string("5\n"));   // return passes through try

    TEST("top-level return ends the program; main is not called by Run");
    CHECK_EQ(Run("print(1)\nreturn;\nprint(2)"), std::string("1\n"));
    // Without the ';' the next line is the value returned (engine rule): it runs.
    CHECK_EQ(Run("function f() {\n  return\n  5\n}\nprint(f())"), std::string("5\n"));
    TEST_RESULT();
}
