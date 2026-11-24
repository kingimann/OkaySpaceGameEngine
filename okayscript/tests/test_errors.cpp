// Syntax and runtime errors: the right kind and the right line, including errors deep inside function calls, loops,
// lambdas and interpolated strings, and that an error leaves the interpreter usable.
#include "TestKit.hpp"

using oktest::Run;
using okayscript::Interpreter;

int main() {
    TEST("syntax errors stop the program before anything runs");
    CHECK_EQ(Run("print(\"never printed\")\nvar x = ;"), std::string("ERROR syntax 2: parse error: unexpected ';'"));
    CHECK_EQ(Run("print(1)\nwhile (true {\n}"), std::string("ERROR syntax 2: parse error: expected ')', got '{'"));
    CHECK_EQ(Run("print(1)\nprint(2 3)"), std::string("ERROR syntax 2: parse error: expected ')', got number"));
    CHECK_EQ(Run("\n\n\nvar s = \"unterminated"), std::string("ERROR syntax 4: unterminated string"));
    CHECK_EQ(Run("function f() {\n  return 1\n}\nfunction g( {\n}"), std::string("ERROR syntax 4: parse error: expected parameter name, got '{'"));

    TEST("runtime errors carry the line of the statement that failed");
    CHECK_EQ(Run("print(1)\nprint(2)\nmissing(3)\nprint(4)"), std::string("1\n2\nERROR runtime 3: call to undefined function 'missing'"));
    CHECK_EQ(Run("function a() {\n  b()\n}\nfunction b() {\n  var x = 1\n  nope(x)\n}\na()"),
             std::string("ERROR runtime 6: call to undefined function 'nope'"));   // the innermost line, not the caller's
    CHECK_EQ(Run("var i = 0\nwhile (i < 5) {\n  i++\n  if (i == 3)\n    throw \"three\"\n}"), std::string("ERROR runtime 5: three"));
    CHECK_EQ(Run("var f = x => {\n  return x.Nope()\n}\nf(1)"), std::string("ERROR runtime 2: no method 'Nope' on 'x' (number)"));
    CHECK_EQ(Run("var xs = [1]\n\nprint(xs[0].Bogus)"), std::string("ERROR runtime 3: no property 'Bogus' on this value (number)"));
    CHECK_EQ(Run("var xs = [1]\n\nprint((xs).Bogus)"), std::string("ERROR runtime 3: no property 'Bogus' on this value (list)"));
    CHECK_EQ(Run("var xs = [1]\nprint(xs.Bogus)"), std::string("0\n"));   // a dotted name nobody knows reads as 0 (engine rule)
    CHECK_EQ(Run("var xs = [1]\nxs.Explode()"), std::string("ERROR runtime 2: no method 'Explode' on 'xs' (list)"));
    CHECK_EQ(Run("function f(a) { }\nf(b: 1)"), std::string("ERROR runtime 2: f() has no parameter 'b'"));
    CHECK_EQ(Run("function down(n) {\n  return down(n + 1)\n}\ndown(0)"),
             std::string("ERROR runtime 2: down() calls itself too deeply (endless recursion?)"));
    CHECK_EQ(Run("var s = 1\nprint($\"value {s.Nope()}\")"), std::string("ERROR runtime 2: no method 'Nope' on 's' (number)"));
    CHECK_EQ(Run("int n = 1\nfunction f() {\n  n = \"x\"\n}\nf()"), std::string("ERROR runtime 3: 'n' is an int, so it can't hold text (use int.Parse(text))"));

    TEST("the loop limit is a runtime error (when set)");
    {
        Interpreter in;
        okayscript::InstallStandardLibrary(in);
        in.GetLimits().maxLoopIterations = 100;
        CHECK_EQ(Run("var n = 0\nwhile (true) n++", &in), std::string("ERROR runtime 2: while loop exceeded iteration limit (100)"));
        in.GetLimits().maxLoopIterations = 0;   // 0 = no limit
        CHECK_EQ(Run("var n = 0\nwhile (n < 2000000) n++\nprint(n)", &in), std::string("2000000\n"));
    }
    TEST("after an error the interpreter still works, with globals kept");
    {
        Interpreter in;
        okayscript::InstallStandardLibrary(in);
        CHECK_EQ(Run("var kept = 5\nfunction f() { var local = 1; boom() }\nf()", &in), std::string("ERROR runtime 2: call to undefined function 'boom'"));
        CHECK_EQ(in.Rt().scopes.size(), (std::size_t)1);   // the failed call's scope was removed
        CHECK_EQ(Run("print(kept, local)", &in), std::string("5 0\n"));
        CHECK_EQ(Run("print(", &in), std::string("ERROR syntax 1: parse error: unexpected end of file"));
        CHECK_EQ(Run("print(kept + 1)", &in), std::string("6\n"));
    }
    TEST("a runtime error from C++ Call has the line inside the function");
    {
        Interpreter in;
        okayscript::InstallStandardLibrary(in);
        in.Run("function f(x) {\n  if (x > 1)\n    throw \"too big\"\n  return x\n}");
        CHECK_EQ(in.Call("f", {okayscript::Value{1.0}}).AsNumber(), 1.0);
        int line = 0;
        std::string msg;
        try { in.Call("f", {okayscript::Value{5.0}}); } catch (const okayscript::RuntimeError& e) { line = e.line; msg = e.what(); }
        CHECK_EQ(line, 3);
        CHECK_EQ(msg, std::string("too big"));
    }
    TEST("FormatError: file:line: message");
    CHECK_EQ(okayscript::FormatError("game.okay", okayscript::RuntimeError("boom", 7)), std::string("game.okay:7: boom"));
    CHECK_EQ(okayscript::FormatError("game.okay", okayscript::RuntimeError("boom", 0)), std::string("game.okay: boom"));
    TEST_RESULT();
}
