// The bytecode compiler and stack machine (Bytecode.hpp, detail/Vm.ipp), the one way OkayScript runs. Every program
// here must give the output, errors, error lines and globals recorded in vm_expected.txt. Those were recorded from the
// tree walker the machine replaced, before the walker was deleted, so the machine keeps its exact behaviour. Listings
// check what the compiler makes: folded constants, fused tests, locals in slots, and no node left without code.
//
// `test_okayscript_lang_vm --record <file>` writes the current outputs to <file> (for a deliberate language change).
#include "TestKit.hpp"
#include "okayscript/Bytecode.hpp"
#include "okayscript/Parser.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace {

std::string RunProgram(const std::string& src, long long loopLimit = 1000000) {
    okayscript::Interpreter in;
    okayscript::InstallStandardLibrary(in);
    in.GetLimits().maxLoopIterations = loopLimit;
    std::ostringstream o;
    in.SetOutput(&o);
    try {
        // A blank line more: `static` variables are shared by runs of the same source text, so no other test's run
        // of the same program may share them.
        in.Run(src + "\n\n", "test.okay");
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
    for (const char* g : {"a", "b", "c", "s", "t", "x", "y", "n", "r", "q", "k", "depth", "only", "score", "top"})
        if (in.HasGlobal(g)) {
            std::string v = in.GetGlobal(g).AsString();
            if (v.size() > 200) v = "<" + std::to_string(v.size()) + " characters>";
            out += std::string("\n[") + g + "=" + v + "]";
        }
    return out;
}

struct Case {
    std::string name, src;
    long long loopLimit = 1000000;
};

std::vector<Case> Programs() {
    std::vector<Case> p = {
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
        {"foreach_flow", "var xs = [1, 2, 3, 4, 5, 6]\nforeach (var v in xs) { if (v == 2) continue; if (v == 5) break; xs.Add(v); print(v); }\n"
                         "print(xs.Count, v)\nvar m = {}\nm.a = 1\nm.b = 2\nforeach (var kv in m) { print(kv.Key, kv.Value); }\n"
                         "foreach (var q in 5) { print(\"never\"); }\nfunction f() { foreach (var z in [7, 8]) { return z; } }\nprint(f())"},
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
        // ---- The nodes stage 1 of the machine still left to the tree walker ----
        {"dotted", "var xs = [1, 2, 3]\nprint(xs.Count)\nvar m = {}\nm.k = 4\nprint(m.k)\nm.k = m.k + 1\nprint(m.k)\n"
                   "function f() { var v = [5, 6]; return v.Count; }\nprint(f(), Math.PI > 3)"},
        {"dotted_assign", "var v = vec3(1, 2, 3)\nv.x = 9\nprint(v)\nvar m = {}\nm.k = 1\nm.k += 1\nprint(m.k)\n"
                          "this.top = 5\nprint(top, this.top)\nfunction g() { var w = vec3(0, 0, 0); w.y = 2; return w; }\nprint(g())"},
        {"member", "var m = {}\nm.a = {}\nm.a.b = 3\nprint(m.a.b, [1, 2, 3].Count, \"abc\".Length)\nvar n = 0\n"
                   "var mm = {}\nmm.k = 2\nprint(n?.Count, (mm).k)\nfunction f() { var d = {}; d.z = 7; return (d).z; }\nprint(f())"},
        {"member_error", "var n = 5\nprint(\"a\")\nprint((n).Nope)"},
        {"member_set_error", "var n = 0\nprint(\"a\")\n(n).k = 1"},
        {"lambdas", "var sq = x => x * x\nvar add = (a, b = 2) => a + b\nprint(sq(4), add(1), add(1, 5))\n"
                    "function apply(f, v) { return f(v); }\nprint(apply(sq, 3))\n"
                    "function mk() { var k = 10; var g = v => v + k; return g(1); }\nprint(mk())\n"
                    "var big = (n) => { var t = 0; for (var i = 0; i < n; i++) { t += i; } return t; }\nprint(big(5))\n"
                    "var named = (p, q = 1) => p - q\nprint(named(q: 5, p: 2))"},
        {"new", "class V2 { public float x; public float y; V2(float a, float b) { x = a; y = b; }\n"
                "  public float Len2() { var s = x * x + y * y; return s; } }\nvar v = new V2(3, 4)\nprint(v.Len2(), v.x)\n"
                "var w = new V2(1, 1)\nw.x += 2\nprint(w.x)\nprint(new V2(1, 2).Len2())\nvar bad = new Nope()"},
        {"switch", "function name(n) {\n  switch (n) {\n    case 1: return \"one\";\n    case 2:\n    case 3: return \"few\";\n"
                   "    default: return \"many\";\n  }\n}\nprint(name(1), name(3), name(9))\nvar t = \"b\"\n"
                   "switch (t) { case \"a\": print(\"A\"); case \"b\": print(\"B\"); case \"c\": print(\"C\"); break; case \"d\": print(\"D\"); }\n"
                   "switch (5) { case 1: print(1); }\nvar k = 0\nswitch (k) { default: print(\"def\"); case 1: print(\"one-fall\"); }\n"
                   "for (var i = 0; i < 3; i++) { switch (i) { case 0: continue; case 1: print(\"i1\"); break; } print(\"loop\", i); }\n"
                   "switch (\"2\") { case 2: print(\"text two\"); break; }\nswitch (1) { case 1: break; print(\"no\"); }\nprint(\"end\")"},
        {"switch_stray_continue", "switch (1) { case 1: print(\"a\"); continue; }\nprint(\"never\")"},
        {"try", "function risky(n) { if (n > 1) throw \"big \" + n; return n; }\nfor (var i = 0; i < 4; i++) {\n"
                "  try { print(risky(i)); if (i == 1) continue; print(\"after\", i); }\n"
                "  catch (e) { print(\"caught\", e); if (i == 3) break; }\n}\n"
                "try { var z = nope(1); } catch (err) { print(err); }\n"
                "function inner() { try { return 5; } catch (e) { return 6; } }\nprint(inner())\n"
                "function deep() { for (var j = 0; j < 3; j++) { try { if (j == 1) break; } catch (e) { } } return j; }\nprint(deep())\n"
                "try { try { throw \"in\"; } catch (a) { throw \"out \" + a; } } catch (b) { print(b); }\n"
                "function thrower() { var local = 1; throw \"from fn\"; }\ntry { thrower(); } catch (e) { print(e, local); }\n"
                "try { throw; } catch (e) { print(e); }\nprint(\"end\")"},
        {"try_nocatch", "try { throw \"silent\"; }\nprint(\"end\")"},
        {"try_error_after", "try { print(1); } catch (e) { }\nprint(2)\nnope()"},
        {"typed_locals", "function f(int a, float b) { int c = a + b; string s = \"v\" + c; bool ok = c; return s + \" \" + ok; }\n"
                         "print(f(2.9, 1.5))\nfunction g() { int w; string e; print(w, e.Length); const int k = 3; k = 4; }\ng()"},
        {"typed_error", "function f() { int a = 1; a = \"x\"; }\nprint(\"a\")\nf()"},
        {"typed_param_error", "function f(int a) { return a; }\nprint(f(2.5))\nprint(f(\"x\"))"},
        {"interp2", "var n = 3\nvar s = $\"{n} items, {n * 2} wheels, {n + 1}!\"\nprint(s)\n"
                    "function f(v) { return $\"<{v}>\"; }\nprint(f(1), f(\"a\"), $\"plain\")"},
        {"casts", "print((int)3.7, (int)-3.7, (float)\"2.5\", (string)5 + 1, (bool)0, (char)65, (int)\"A\", (float)\"x\")"},
        {"index_compound", "var xs = [1, 2, 3]\nxs[0] += 5\nxs[1] *= 3\nxs[2]++\nvar m = {}\nm[\"a\"] += 2\nm[\"s\"] = \"x\"\n"
                           "m[\"s\"] += \"y\"\nprint(xs, m[\"a\"], m[\"s\"], m.Count)\nvar n = 0\nn[0] += 1\nprint(n)\n"
                           "function idx() { print(\"idx\"); return 0; }\nvar q = 5\nq[idx()] = 2\nprint(q[idx()])\n"
                           "xs[idx()] = 9\nprint(xs[idx()])\nxs[idx()] += 1\nq[idx()] += 1\nprint(xs[5], xs[idx() + 1])"},
        {"named_args", "function h(a, b = 1, c = 2) { return a * 100 + b * 10 + c; }\nprint(h(1, c: 5), h(c: 1, a: 2))\n"
                       "function d(a, b = a * 2) { return b; }\nprint(d(4))\nprint(h(1, x: 2))"},
        {"named_builtin", "print(abs(v: 2))"},
        {"events", "function OnHit(n) { print(\"hit\", n); }\nHurt += OnHit\nvar l = x => print(\"l\", x)\nHurt += l\n"
                   "Hurt += (y) => print(\"inline\", y)\nscore = 1\nscore += 2\nscore -= 0.5\nprint(score)\nHurt -= OnHit\n"
                   "var s = \"a\"\ns += \"b\"\nprint(s)"},
        {"dynamic2", "function reader() { return depth; }\nfunction a() { var depth = 1; return reader() + b(); }\n"
                     "function b() { var depth = 2; return reader(); }\nprint(a())\nfunction writer() { depth = 99; }\n"
                     "function c() { var depth = 0; writer(); return depth; }\nprint(c(), depth)\n"
                     "function late() { print(q); var q = 3; print(q); }\nq = \"global\"\nlate()\n"
                     "function cond(f) { if (f) { var only = 1; } return only; }\nprint(cond(true), cond(false))\n"
                     "function setOuter() { k = 5; }\nfunction holder() { var k = 1; setOuter(); return k; }\nprint(holder(), k)"},
        {"recursion_locals", "function fact(n) { var r = 1; if (n > 1) r = n * fact(n - 1); return r; }\nprint(fact(10))"},
        {"deep_recursion", "function r(n) { return r(n + 1); }\nprint(\"go\")\nr(0)"},
        {"foreach_slot", "function sum(xs) { var t = 0; foreach (var x in xs) { t += x; } return t + x; }\nprint(sum([1, 2, 3]))\n"
                         "function keys(m) { var acc = \"\"; foreach (var k in m) { acc += k; } return acc; }\nvar mk = {}\nmk.b = 1\nmk.a = 2\nprint(keys(mk))"},
        {"yield", "function Co() { yield return 1; }\nCo()"},
        {"nullish_member", "var m\nprint(m?.x ?? \"none\", m?.Count, m?.ToString())\nvar s = \"ab\"\nprint(s?.Length, s?.ToUpper())"},
        {"methods", "var s = \"abc\"\nprint(s.ToUpper(), s.Substring(1))\nprint(s.Nope())"},
        {"nested_lists", "var g = [[1, 2], [3, [4, 5]]]\nprint(g, g[1][1][0], g[0].Count)\nvar e = []\nprint(e, e.Count)"},
        {"compound_this", "class C { public int n; public void Inc() { this.n += 1; n += 10; } }\nvar c = new C()\nc.Inc()\nc.Inc()\nprint(c.n)"},
        {"method_locals", "class Acc { public int total; public void AddAll(xs) { var i = 0; while (i < xs.Count) { total += xs[i]; i++; } } }\n"
                          "var acc = new Acc()\nacc.AddAll([1, 2, 3])\nprint(acc.total)"},
        {"globals_from_fn", "function setAll() { a = 1; b = \"two\"; var c = 3; }\nsetAll()\nprint(a, b, c)"},
        {"var_in_loop", "function f() { var t = 0; for (var i = 0; i < 3; i++) { var sq = i * i; t += sq; } return t + sq + i; }\nprint(f())"},
        {"static_typed", "function f() { static string log = \"\"; log += \"x\"; return log; }\nf()\nprint(f())"},
        {"exit", "print(1)\nexit(3)\nprint(2)"},
        {"dict", "var m = new Dictionary<string, int>()\nm[\"a\"] = 1\nm[\"a\"] += 4\nm[\"s\"] = \"x\"\nm[\"s\"] += \"y\"\n"
                 "m.b = 2\nprint(m[\"a\"], m[\"s\"], m.b, m.Count, m.a)\nforeach (var kv in m) { print(kv.Key, kv.Value); }\n"
                 "function keys(d) { var acc = \"\"; foreach (var k in d) { acc += k; } return acc; }\nprint(keys(m))\n"
                 "m.inner = new Dictionary<string, int>()\nm.inner.z = 3\nprint(m.inner.z, (m.inner).z, m?.inner?.z)"},
    };
    p.push_back({"loop_limit", "var n = 0\nwhile (true) {\n  n++;\n}", 1000});
    p.push_back({"for_limit", "for (var i = 0; ; i++) { if (i == 5) print(i); }", 50});
    // Every CLI test script (tests/okayscript_cli) as well.
    for (auto& entry : std::filesystem::directory_iterator(OKS_CLI_CASES)) {
        if (entry.path().extension() != ".okay" || entry.path().stem() == "input") continue;
        std::ifstream f(entry.path());
        std::stringstream ss;
        ss << f.rdbuf();
        p.push_back({"cli:" + entry.path().filename().string(), ss.str()});
    }
    return p;
}

// vm_expected.txt: "#### <name>" on a line of its own, then that program's output up to the next one.
const char* kMark = "#### ";

std::map<std::string, std::string> ReadExpected(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string all = ss.str();
    std::map<std::string, std::string> out;
    std::size_t at = all.find(kMark);
    while (at != std::string::npos) {
        const std::size_t nameEnd = all.find('\n', at);
        if (nameEnd == std::string::npos) break;
        const std::string name = all.substr(at + 5, nameEnd - at - 5);
        std::size_t next = all.find(std::string("\n") + kMark, nameEnd);
        const std::size_t bodyEnd = next == std::string::npos ? all.size() : next;
        std::string body = all.substr(nameEnd + 1, bodyEnd - nameEnd - 1);
        if (next == std::string::npos && !body.empty() && body.back() == '\n') body.pop_back();   // the file's last newline
        out[name] = body;
        at = next == std::string::npos ? next : next + 1;
    }
    return out;
}

std::string Listing(const std::string& src, const char* function = nullptr) {
    okayscript::Program prog;
    okayscript::ParseSource(src, prog);
    if (function) {
        const okayscript::FunctionDecl& fn = prog.functions.at(function);
        return okayscript::Disassemble(*okayscript::CompileFunction(fn.params, fn.body));
    }
    return okayscript::Disassemble(*okayscript::CompileBody(prog.top));
}

bool Has(const std::string& listing, const std::string& part) { return listing.find(part) != std::string::npos; }

} // namespace

int main(int argc, char** argv) {
    const std::vector<Case> programs = Programs();
    if (argc == 3 && std::string(argv[1]) == "--record") {
        std::ofstream f(argv[2], std::ios::binary);
        for (const Case& c : programs) f << kMark << c.name << '\n' << RunProgram(c.src, c.loopLimit) << '\n';
        return 0;
    }

    // ---- Every program gives the recorded output ----
    const std::map<std::string, std::string> expected = ReadExpected(OKS_VM_EXPECTED);
    TEST("expected outputs found");
    CHECK(expected.size() >= 80);
    for (const Case& c : programs) {
        oktest::g_test = c.name.c_str();
        auto it = expected.find(c.name);
        CHECK(it != expected.end());
        if (it == expected.end()) continue;
        oktest::CheckEq(RunProgram(c.src, c.loopLimit), it->second, "bytecode", "recorded", c.name.c_str(), 0);
    }

    // ---- Constant folding ----
    TEST("folding");
    const std::string fold = Listing("var x = 2 * 3 + 1\nvar y = 10 / 0 < 1\nvar z = -(4 - 6)");
    CHECK(Has(fold, "push_num 7"));
    CHECK(Has(fold, "push_bool true"));
    CHECK(Has(fold, "push_num 2"));
    CHECK(!Has(fold, "binary"));
    TEST("no folding of names or text");
    const std::string keep = Listing("var x = a * 2\nvar t = \"a\" + 1");
    CHECK(Has(keep, "load a"));
    CHECK(Has(keep, "binary_k '*' 2"));   // a number on the right is carried by the instruction
    CHECK(Has(keep, "push_str a"));
    CHECK(Has(keep, "binary_k '+' 1"));

    // ---- The hot loops: one instruction for the loop test ----
    TEST("hot loop");
    const std::string loop = Listing("var s = 0\nfor (var i = 0; i < 9; i++) { s = s + i * 2 - 1; }\n"
                                     "function f(a) { return a + 1; }\nwhile (s > 0) { s = f(s) - 2; }");
    CHECK(Has(loop, "compare_jump_k '<' 9"));
    CHECK(Has(loop, "compare_jump_k '>' 0"));

    // ---- A function's locals are slots: parameters first, then its declarations; globals stay by name ----
    TEST("locals in slots");
    const std::string fn = Listing("function f(a, b) { var t = a + b; foreach (var x in [1]) { t += x; } "
                                   "try { } catch (e) { } return t + g; }", "f");
    CHECK(Has(fn, "load_slot 0 a"));
    CHECK(Has(fn, "load_slot 1 b"));
    CHECK(Has(fn, "define_slot 2 t"));
    CHECK(Has(fn, "foreach_start 0 0 3"));   // iterator 0, its variable x in slot 3
    CHECK(Has(fn, "define_slot 4 e"));
    CHECK(Has(fn, "load g"));                // not a local: found by name (a global, or a caller's local)
    CHECK(!Has(fn, "define t"));
    TEST("a static is not a slot");
    const std::string st = Listing("function f() { static int runs = 0; runs++; return runs; }", "f");
    CHECK(Has(st, "static_set_jump runs"));
    CHECK(Has(st, "define_static runs"));
    CHECK(Has(st, "load runs"));
    CHECK(!Has(st, "slot"));
    TEST("top-level declarations are globals, by name");
    const std::string top = Listing("var g = 1\ng = g + 1");
    CHECK(Has(top, "define g"));
    CHECK(Has(top, "store_pop g"));
    CHECK(!Has(top, "slot"));

    // ---- Every node has its own instructions ----
    TEST("instructions for every node");
    const std::string all = Listing(
        "class P { P(a) { } }\nvar v = xs.Count\nthis.hp = 2\nvar m = (o).k\n(o).k = 1\nvar f = x => x\nvar p = new P(1)\n"
        "switch (v) { case 1: break; default: print(1); }\ntry { throw \"e\"; } catch (e) { }\nint n = 3\n"
        "var s = $\"{v}!\"\nvar c = (int)v\nvar w = v ?? 2\nk ??= 1\nxs[i()] += 1\nHit += Fn\nh(a: 1)\nvar u = (o)?.k");
    for (const char* op : {"load_qualified xs.Count", "assign_qualified this.hp", "member k", "member_set k",
                           "make_lambda", "new P/1", "switch_case", "try_begin", "throw", "define_typed -1 n",
                           "append_text", "append_lit !", "cast int", "jump_unless_nullish_keep", "get_name k",
                           "assign_name k", "index_compound", "skip_unless_indexable", "event_var Hit",
                           "compound_cur Hit", "compound_store Hit", "call_named h/1", "jump_if_nullish_null"}) {
        TEST(op);
        CHECK(Has(all, op));
    }

    TEST_RESULT();
}
