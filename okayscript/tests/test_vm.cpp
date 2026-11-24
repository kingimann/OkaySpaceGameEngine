// The bytecode compiler and stack machine (Bytecode.hpp, detail/Vm.ipp): every program gives exactly the output,
// errors and error lines of the tree walker, and constants are folded when compiled.
#include "TestKit.hpp"
#include "okayscript/Bytecode.hpp"
#include "okayscript/Parser.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

std::string RunWith(const std::string& src, bool bytecode, long long loopLimit = 1000000) {
    okayscript::Interpreter in;
    okayscript::InstallStandardLibrary(in);
    in.Rt().useBytecode = bytecode;
    in.GetLimits().maxLoopIterations = loopLimit;
    std::ostringstream o;
    in.SetOutput(&o);
    try {
        // A blank line more for one of the two: `static` variables are shared by runs of the same source text.
        in.Run(src + (bytecode ? "\n" : "\n\n"), "test.okay");
    } catch (const okayscript::SyntaxError& e) {
        o << "ERROR syntax " << e.line << ": " << e.what();
    } catch (const okayscript::RuntimeError& e) {
        o << "ERROR runtime " << e.line << ": " << e.what();
    } catch (const okayscript::ExitRequest& e) {
        o << "EXIT " << e.code;
    }
    in.SetOutput(nullptr);
    std::string out = o.str();
    // Globals after the run, so a wrong variable write shows up too.
    for (const char* g : {"a", "b", "c", "s", "t", "x", "y", "n", "r"})
        if (in.HasGlobal(g)) out += std::string("\n[") + g + "=" + in.GetGlobal(g).AsString() + "]";
    return out;
}

void Same(const std::string& name, const std::string& src, long long loopLimit = 1000000) {
    oktest::g_test = name.c_str();
    const std::string walker = RunWith(src, false, loopLimit), vm = RunWith(src, true, loopLimit);
    oktest::CheckEq(vm, walker, "bytecode", "walker", name.c_str(), 0);
}

std::string Listing(const std::string& src) {
    okayscript::Program prog;
    okayscript::ParseSource(src, prog);
    return okayscript::Disassemble(*okayscript::CompileBody(prog.top));
}

} // namespace

int main() {
    // ---- Programs that use every statement and expression, compared with the walker ----
    const std::vector<std::pair<std::string, std::string>> programs = {
        {"arith", "var s = 0\nfor (var i = 0; i < 100; i++) { s = s + i * 2 - 1; }\nprint(s)\nprint(7 / 0, 7 % 0, -3 % 2)"},
        {"text", "var t = \"\"\nfor (var i = 0; i < 12; i++) { t = t + \"a\" + (i % 3); }\nprint(t, t.Length)\nprint(1 + \"2\", \"x\" + 1.5)"},
        {"compare", "print(1 < 2, 2 <= 2, 3 > 4, 4 >= 5, 1 == 1, 1 != 1, \"a\" == \"a\", \"1\" == 1, true == 1)"},
        {"logic", "var n = 0\nfunction t() { n = n + 1; return true; }\nfunction f() { n = n + 10; return false; }\n"
                  "print(f() && t(), t() || f(), t() && t(), f() || f(), !t(), 0 && t(), \"x\" || f())\nprint(n)"},
        {"ternary", "var x = 5\nprint(x > 3 ? \"big\" : \"small\", x < 3 ? 1 : x == 5 ? 2 : 3)"},
        {"while", "var n = 0\nwhile (n < 10) { n++; if (n == 3) continue; if (n == 7) break; print(n); }\nprint(\"end\", n)"},
        {"do", "var n = 0\ndo { n += 2; if (n == 4) continue; print(n); } while (n < 9)\nprint(n)"},
        {"for", "for (var i = 0; i < 10; i++) { if (i % 2 == 0) continue; if (i > 7) break; print(i); }"},
        {"nested", "for (var i = 0; i < 3; i++) { for (var j = 0; j < 3; j++) { if (j == i) break; print(i, j); } }"},
        {"switch_in_loop", "for (var i = 0; i < 5; i++) {\n switch (i) { case 1: print(\"one\"); break; case 2: continue; "
                           "default: print(\"d\", i); }\n print(\"after\", i);\n}"},
        {"try_in_loop", "var n = 0\nwhile (true) { n++; try { if (n == 3) break; if (n == 2) throw \"boom\"; } "
                        "catch (e) { print(\"caught\", e); } }\nprint(n)"},
        {"calls", "function f(a) { return a + 1; }\nfunction g(a, b = 10) { return a * b; }\nvar s = 0\n"
                  "for (var i = 0; i < 20; i++) { s = f(s); }\nprint(s, g(2), g(2, 3), g(b: 4, a: 5))"},
        {"recursion", "function fib(n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }\nprint(fib(15))"},
        {"dynamic_scope", "function inner() { return k + 1; }\nfunction outer() { var k = 41; return inner(); }\n"
                          "print(outer())\nprint(k)"},
        {"shadow", "var x = 1\nfunction f() { print(x); var x = 2; print(x); x = 3; print(x); }\nf()\nprint(x)"},
        {"lambda", "var a = 3\nvar add = (v) => v + a\nprint(add(4))\nfunction h() { var a = 100; return add(1); }\nprint(h())\n"
                   "var r = [1, 2, 3].Select(v => v * a)\nprint(r)"},
        {"lists", "var xs = []\nfor (var i = 0; i < 5; i++) { xs.Add(i * i); }\nxs[2] = 99\nvar s = 0\n"
                  "foreach (var v in xs) { s = s + v; }\nprint(xs, s, xs.Count, xs[10])"},
        {"maps", "var m = {}\nm[\"a\"] = 1\nm.b = 2\nforeach (var k in m) { print(k, m[k]); }\nprint(m.Count)"},
        {"typed", "int n = 3.7\nprint(n)\nn = 9.9\nprint(n)\nconst string s = \"k\"\nprint(s)"},
        {"const_error", "const int c = 1\nprint(c)\nc = 2\nprint(\"never\")"},
        {"error_line", "var s = 0\nfunction f(x) {\n  return x.Nope();\n}\nprint(\"before\")\ns = f(1)\nprint(\"after\")"},
        {"throw_line", "print(1)\nfor (var i = 0; i < 3; i++) {\n  if (i == 2) throw \"stop at \" + i;\n}"},
        {"stray_break", "print(1)\nbreak\nprint(2)"},
        {"top_return", "print(1)\nif (true) return\nprint(2)"},
        {"fn_stray", "function f() { print(\"in\"); continue; print(\"no\"); }\nf()\nprint(\"out\")"},
        {"statics", "function f() { static int runs = 0; runs++; return runs; }\nf()\nf()\nprint(f())"},
        {"classes", "class P { public int hp = 5; public void Hit(int d) { hp = hp - d; } public int Hp() { return hp; } }\n"
                    "var p = new P()\np.Hit(2)\nprint(p.Hp(), p.hp)"},
        {"interp", "var x = 4\nprint($\"x is {x * 2} and {x > 3}\")"},
        {"unset", "print(nothing, nothing + 1, -nothing, !nothing)"},
        {"vectors", "var v = vec3(1, 2, 3)\nprint(v * 2, v + v, -v, v / 0)"},
        {"undefined_call", "print(1)\nnope(2)"},
        {"compound", "var a = 10\na += 5\na -= 1\na *= 2\na /= 4\na %= 4\nprint(a)\nvar b = 1\nb++\nb--\n++b\nprint(b)"},
        {"nullish", "var y = 0\nvar c = y ?? 7\nprint(c)\nvar z\nz ??= 3\nprint(z)"},
    };
    for (auto& p : programs) Same(p.first, p.second);

    // The loop limit: the same error, on the same line, after the same output.
    Same("loop_limit", "var n = 0\nwhile (true) {\n  n++;\n}", 1000);
    Same("for_limit", "for (var i = 0; ; i++) { if (i == 5) print(i); }", 50);

    // Every CLI test script (tests/okayscript_cli) as well.
    int files = 0;
    for (auto& entry : std::filesystem::directory_iterator(OKS_CLI_CASES)) {
        if (entry.path().extension() != ".okay" || entry.path().stem() == "input") continue;
        std::ifstream f(entry.path());
        std::stringstream ss;
        ss << f.rdbuf();
        Same(entry.path().filename().string(), ss.str());
        ++files;
    }
    TEST("cli cases found");
    CHECK(files >= 8);

    // ---- Constant folding ----
    TEST("folding");
    const std::string fold = Listing("var x = 2 * 3 + 1\nvar y = 10 / 0 < 1\nvar z = -(4 - 6)");
    CHECK(fold.find("push_num 7") != std::string::npos);
    CHECK(fold.find("push_bool true") != std::string::npos);
    CHECK(fold.find("push_num 2") != std::string::npos);
    CHECK(fold.find("binary") == std::string::npos);
    TEST("no folding of names or text");
    const std::string keep = Listing("var x = a * 2\nvar t = \"a\" + 1");
    CHECK(keep.find("load a") != std::string::npos);
    CHECK(keep.find("binary_k '*' 2") != std::string::npos);   // a number on the right is carried by the instruction
    CHECK(keep.find("push_str a") != std::string::npos);
    CHECK(keep.find("binary_k '+' 1") != std::string::npos);

    // ---- The hot loops compile to bytecode, not to the walker ----
    TEST("hot loop has no walker steps");
    const std::string loop = Listing("var s = 0\nfor (var i = 0; i < 9; i++) { s = s + i * 2 - 1; }\n"
                                     "function f(a) { return a + 1; }\nwhile (s > 0) { s = f(s) - 2; }");
    CHECK(loop.find("eval") == std::string::npos);
    CHECK(loop.find("exec") == std::string::npos);
    CHECK(loop.find("compare_jump_k '<' 9") != std::string::npos);   // the loop test is one instruction
    CHECK(loop.find("compare_jump_k '>' 0") != std::string::npos);

    TEST_RESULT();
}
