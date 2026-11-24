// The bytecode compiler and stack machine (Bytecode.hpp, detail/Vm.ipp), the one way OkayScript runs. Every program
// here must give the output, errors, error lines and globals recorded in vm_expected.txt. Those were recorded from the
// tree walker the machine replaced, before the walker was deleted, so the machine keeps its exact behaviour. Listings
// check what the compiler makes: folded constants, fused tests, locals in slots, and no node left without code.
//
// `test_okayscript_lang_vm --record <file>` writes the current outputs to <file> (for a deliberate language change).
#include "TestKit.hpp"
#include "okayscript/Bytecode.hpp"
#include "okayscript/Parser.hpp"
#include "okayscript/detail/Support.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace {

// import "x.okay" in these programs: found under the CLI cases folder, as the CLI finds it next to the file.
bool LoadCase(const std::string& path, const std::string& from, std::string& resolved, std::string& source, std::string& error) {
    std::filesystem::path p(path);
    if (!from.empty() && from != "test.okay") p = std::filesystem::path(from).parent_path() / p;
    resolved = p.lexically_normal().generic_string();
    std::ifstream f(std::filesystem::path(OKS_CLI_CASES) / resolved, std::ios::binary);
    if (!f) { error = "can't read '" + resolved + "'"; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    source = ss.str();
    return true;
}

std::string RunProgram(const std::string& src, long long loopLimit = 1000000) {
    okayscript::Interpreter in;
    okayscript::InstallStandardLibrary(in);
    in.SetModuleLoader(LoadCase);
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
    // ---- Language stage 2 (docs/scripting/okayscript_vm.md): integers, destructuring, closures, try/finally and
    // thrown values, generators and iterators, map literals, strings, classes, calling any value; then the library ----
    std::vector<Case> lang2 = {
        {"ints", "var a = 9007199254740993L\nprint(a, a + 1, a * 2, -a, a == 9007199254740993L, a == 9007199254740992L)\n"
                 "print(7L / 2, -7L / 2, 7L % 3, -7L % 3, 7L / 0, 7L % 0, 5L / 2.0, 5L + 0.5, 3L < 4, 2L == 2)\n"
                 "print(9223372036854775807L + 1, (long)\"123456789012345\", (long)3.9, (int)7L, typeof(1L), typeof(1))\n"
                 "var n = 0L\nfor (var i = 0; i < 10; i++) { n += i; }\nprint(n, typeof(n), n & 6L, n | 1L, n ^ 3L)\n"
                 "int k = 5L\nfloat f = 5L\nprint(typeof(k), typeof(f), [1, 2, 3][1L], 16777217L, 16777217)"},
        {"ints_bad", "print(1)\nvar x = 1.5L"},
        {"destructure", "var [a, b] = [1, 2]\nprint(a, b)\n[a, b] = [b, a]\nprint(a, b)\nvar m = {\"x\": 3, y: 4}\nvar {x, y} = m\n"
                        "var {x: px, z} = m\nprint(x, y, px, z)\nfunction f() { var [p, _, q] = [7, 8, 9]; return p + q; }\n"
                        "print(f())\nvar [s, t, u] = [1]\nprint(s, t, u)\nfunction g(pair) { var [k, v] = pair; return k + \"=\" + v; }\n"
                        "print(g([\"a\", 1]))\nforeach (var kv in [[1, 2], [3, 4]]) { var [l, r] = kv; print(l * r); }"},
        {"destructure_bad", "var [a, a] = [1, 2]"},
        {"closures", "function counter() { var n = 0; return () => { n += 1; return n; }; }\nvar c1 = counter()\nvar c2 = counter()\n"
                     "print(c1(), c1(), c1(), c2())\n"
                     "function shared() { var v = 1; var get = () => v; var set = (x) => { v = x; }; return [get, set]; }\n"
                     "var [g, st] = shared()\nst(42)\nprint(g())\nfunction mk(base) { return (x) => (y) => base + x + y; }\n"
                     "print(mk(100)(10)(1))\nfunction later() { var fs = []; for (var i = 0; i < 3; i++) { fs.Add(() => i); } return fs; }\n"
                     "var fs = later()\nprint(fs[0](), fs[2]())\nfunction acc(int start) { int total = start; return (d) => { total += d; return total; }; }\n"
                     "var ac = acc(10)\nprint(ac(2.9), ac(1))\nfunction outer() { var k = 5; function_k = () => k; k = 6; }\nouter()\nprint(function_k())\n"
                     "var top = 1\nvar readTop = () => top\ntop = 2\nprint(readTop())\n"
                     "function shadowed() { var q = \"captured\"; return () => q; }\nfunction caller(f) { var q = \"caller's\"; return f(); }\n"
                     "print(caller(shadowed()))\nprint([1, 2, 3].Select(v => v * top))\nfunction self() { var fact = (n) => n < 2 ? 1 : n * fact(n - 1); return fact; }\n"
                     "print(self()(5))"},
        {"finally", "function t1() { try { print(\"body\"); return 1; } finally { print(\"fin1\"); } }\nprint(t1())\n"
                    "function t2() { try { throw \"boom\"; } catch (e) { print(\"caught\", e); return 2; } finally { print(\"fin2\"); } }\nprint(t2())\n"
                    "for (var i = 0; i < 3; i++) { try { if (i == 1) continue; if (i == 2) break; print(\"i\", i); } finally { print(\"fin i\", i); } }\n"
                    "function t4() { try { throw \"deep\"; } finally { print(\"fin4\"); } }\ntry { t4(); } catch (e) { print(\"t4:\", e); }\n"
                    "function t5() { try { throw \"a\"; } catch (e) { throw \"b \" + e; } finally { print(\"fin5\"); } }\ntry { t5(); } catch (e) { print(\"t5:\", e); }\n"
                    "try { print(\"no error\"); } finally { print(\"fin6\"); }\n"
                    "function nested() { try { try { return \"r\"; } finally { print(\"inner fin\"); } } finally { print(\"outer fin\"); } }\nprint(nested())\n"
                    "function fin_throws() { try { return 1; } finally { throw \"from finally\"; } }\ntry { fin_throws(); } catch (e) { print(e); }\n"
                    "var n = 0\nwhile (true) { try { n++; if (n == 3) break; } finally { print(\"loop fin\", n); } }\nprint(\"end\", n)"},
        {"finally_uncaught", "try { print(1); throw \"stop\"; } finally { print(\"cleanup\"); }\nprint(\"never\")"},
        {"thrown_values", "try { throw [1, 2]; } catch (e) { print(\"list\", e, e.Count); }\ntry { throw 42; } catch (e) { print(e + 1); }\n"
                          "try { throw {\"code\": 7}; } catch (e) { print(e.code); }\ntry { nope(); } catch (Exception e) { print(e.message, e.line, e.value); }\n"
                          "try {\n  throw \"text\";\n} catch (Error e) { print(e.message, e.line, e.value); }\n"
                          "function thrower() { throw {\"why\": \"bad\"}; }\ntry { thrower(); } catch (Error e) { print(e.value.why, e.message.Length > 0, e.line); }"},
        {"thrown_uncaught", "print(1)\nthrow [1, 2]"},
        {"generators", "function g() { yield 1; yield 2; yield 3; }\nfor x in g() { print(\"gen\", x); }\nvar it = g()\n"
                       "print(it.next(), it.next(), it.done(), it.next(), it.done(), it.next())\n"
                       "function nat(n) { var i = 0; while (true) { if (i >= n) return; yield i * i; i++; } }\nprint(nat(5).to_list())\n"
                       "var fib = () => { var a = 0; var b = 1; while (true) { yield a; var t = a + b; a = b; b = t; } }\n"
                       "var f = fib()\nvar out = []\nfor (var k = 0; k < 10; k++) { out.Add(f.next()); }\nprint(out)\n"
                       "function withTry() { try { yield 1; throw \"x\"; } catch (e) { yield \"caught \" + e; } finally { yield \"fin\"; } }\nprint(withTry().to_list())\n"
                       "function loops() { foreach (var q in [10, 20]) { for (var j = 0; j < 2; j++) { yield q + j; } } }\nprint(loops().to_list())\n"
                       "function evens(xs) { foreach (var x in xs) { if (x % 2 == 0) yield x; } }\nvar total = 0\nforeach (var e in evens(range(10))) { total += e; }\nprint(total)\n"
                       "function bad() { yield 1; nope(); }\nvar b = bad()\nprint(b.next())\ntry { b.next(); } catch (err) { print(\"gen error:\", err); }\nprint(b.done())"},
        {"gen_top", "yield 1"},
        {"iterators", "class Countdown { var n = 3; public bool MoveNext() { n--; return n >= 0; } public int Current { get { return n; } } }\n"
                      "foreach (var c in new Countdown()) { print(\"c\", c); }\n"
                      "class Bag { var items = [\"a\", \"b\"]; public Countdown GetEnumerator() { return new Countdown(); } }\n"
                      "foreach (var x in new Bag()) { print(\"bag\", x); }\n"
                      "class Gen { public int limit = 2; public Steps GetEnumerator() { return Steps(limit); } }\nfunction Steps(n) { for (var i = 0; i < n; i++) { yield \"s\" + i; } }\n"
                      "foreach (var s in new Gen()) { print(s); }"},
        {"maplits", "var m = {\"a\": 1, b: [1, 2], \"c d\": {inner: true}}\nprint(m.a, m.b, m[\"c d\"].inner, m.Count, m.keys())\n"
                    "var e = {}\nprint(e.Count, typeof(e))\nprint({x: 1 + 2, y: \"t\"})\nfunction mk(v) { return {value: v, twice: v * 2}; }\nprint(mk(4).twice)"},
        {"strings", "var t = \"\"\"\nline one\n  \"two\"\n\"\"\"\nprint(t)\nprint(@\"C:\\path \"\"q\"\"\")\nvar n = 5\nprint($\"\"\"n is\n{n * 2}\"\"\")\n"
                    "print(format(\"{0:.2f}|{1:>6}|{1:<6}|{2:^7}|{3:x}|{3:X}|{3:b}|{4:,}|{5:+.1f}|{6:08.3f}|{7:.0%}|{8:e}\", 3.14159, \"ab\", \"mid\", 255, 1234567, 2.5, -3.5, 0.256, 12345.678))\n"
                    "print(format(\"{} and {} and {}\", 1, \"two\"), format(\"{{literal}} {name} {0}\", \"x\"), \"{0}-{1}\".format(\"a\", \"b\"))\n"
                    "print(\"a,b,,c\".split(\",\"), \" pad \".trim() + \"|\", \"Hello\".upper(), \"Hello\".lower(), \"hello\".find(\"ll\"), \"hello\".find(\"z\"))\n"
                    "print(\"hello\".starts_with(\"he\"), \"hello\".ends_with(\"lo\"), \"abc\".replace(\"b\", \"BB\"), \", \".join([1, 2, 3]), \"ab\".repeat(3), \"hé!\".chars(), \"abc\".reverse())\n"
                    "print(split(\"x y z\"), join([\"p\", \"q\"], \"/\"), replace(\"aaa\", \"a\", \"b\"), trim(\"  t  \"), upper(\"u\"), starts_with(\"abc\", \"ab\"), ends_with(\"abc\", \"bc\"), str_find(\"abc\", \"c\"))"},
        {"strings_bad", "print(1)\nvar t = \"\"\"never closed"},
        {"classes2", "class Animal {\n  public string name;\n  public static int count = 0;\n  Animal(string n) { name = n; count += 1; }\n"
                     "  public string Speak() { return name + \" makes a sound\"; }\n  public string Hello() { return \"I am \" + name + \": \" + Speak(); }\n"
                     "  public static string Kind() { return \"animal\"; }\n}\nclass Dog : Animal {\n  public int tricks = 2;\n"
                     "  Dog(string n) { super(n); tricks = 5; }\n  public string Speak() { return name + \" barks (\" + super.Speak() + \")\"; }\n}\n"
                     "class Puppy : Dog { public string Speak() { return \"tiny \" + super.Speak(); } }\n"
                     "var a = new Animal(\"cat\")\nvar d = new Dog(\"rex\")\nvar p = new Puppy(\"bit\")\nprint(a.Speak())\nprint(d.Speak(), d.tricks)\n"
                     "print(d.Hello())\nprint(p.Speak(), p.tricks)\nprint(Animal.count, Animal.Kind(), Dog.Kind())\nAnimal.count = 10\nprint(Animal.count)"},
        {"operators", "class V2 {\n  public float x; public float y;\n  V2(float a, float b) { x = a; y = b; }\n"
                      "  public static V2 operator +(V2 a, V2 b) { return new V2(a.x + b.x, a.y + b.y); }\n"
                      "  public static V2 operator *(V2 a, float k) { return new V2(a.x * k, a.y * k); }\n"
                      "  public static bool operator ==(V2 a, V2 b) { return a.x == b.x && a.y == b.y; }\n"
                      "  public static bool operator <(V2 a, V2 b) { return a.Len < b.Len; }\n"
                      "  public string ToString() { return \"(\" + x + \", \" + y + \")\"; }\n"
                      "  public float Len { get { return sqrt(x * x + y * y); } }\n  private float _s = 1;\n"
                      "  public float Scale { get => _s; set { _s = value * 2; } }\n  public float Area { get => x * y; }\n}\n"
                      "var v = new V2(1, 2) + new V2(2, 2)\nprint(v.x, v.y, $\"{v}\", \"v=\" + v, v == new V2(3, 4), v.Len, v * 2)\n"
                      "if (new V2(1, 1) < v) print(\"smaller\")\nv.Scale = 5\nprint(v.Scale, v.Area)\nv.Area = 3"},
        {"call_values", "var fs = [x => x + 1, x => x * 10]\nprint(fs[0](1), fs[1](2))\nvar m = {f: (a, b) => a - b}\nprint(m.f(5, 3), (m.f)(9, 1))\n"
                        "function pick(n) { return n > 0 ? (x => \"pos \" + x) : (x => \"neg \" + x); }\nprint(pick(1)(\"a\"), pick(-1)(\"b\"))\n"
                        "function sum(xs) { var t = 0; foreach (var x in xs) { t += x; } return t + 1000; }\nprint(sum([1, 2]))"},
        {"lib_math", "print(abs(-2), floor(2.7), ceil(2.1), round(2.5), trunc(-2.7), trunc(7L), pow(2, 10), log(8, 2), log2(8), log10(1000), cbrt(27))\n"
                     "print(min(3, 4), max(3, 4), clamp(5, 0, 3), sqrt(16), pi() > 3.14159, hypot(3, 4), sign(-3), fract(2.25), mod(-1, 3), snap(7, 5))\n"
                     "print(deg2rad(180) > 3.14, rad2deg(pi()), lerp(0, 10, 0.5), inverse_lerp(0, 10, 5), remap(5, 0, 10, 0, 100), approach(0, 10, 3), avg(1, 2, 3))\n"
                     "rand_seed(42)\nvar r1 = [rand(), rand(5, 6), randi(1, 100), chance(0.5), choose([\"a\", \"b\", \"c\"])]\nrand_seed(42)\n"
                     "var r2 = [rand(), rand(5, 6), randi(1, 100), chance(0.5), choose([\"a\", \"b\", \"c\"])]\n"
                     "print(to_json(r1) == to_json(r2), r1[0] >= 0 && r1[0] < 1, r1[1] >= 5 && r1[1] < 6, r1[2] >= 1 && r1[2] < 100)\n"
                     "rand_seed(7L)\nvar sh = shuffle(range(10))\nprint(sh.Count, sum(sh), set_of(sh).Count)"},
        {"lib_collections", "var xs = [5, 3, 9, 1]\nprint(xs.map(x => x * 2), xs.filter(x => x > 2), xs.reduce((a, b) => a + b), xs.reduce((a, b) => a + b, 100))\n"
                            "print(xs.find(x => x > 4), xs.find_index(x => x == 9), xs.slice(1, 3), xs.slice(-2), xs.any(x => x > 8), xs.all(x => x > 0))\n"
                            "print(sort([3, 1, 2]), sort([\"b\", \"a\"]), sort([1, 5, 3], (a, b) => b - a), xs.copy().sort(), xs.copy().reverse(), xs, xs.contains(9), xs.index_of(9))\n"
                            "print(zip([1, 2, 3], [\"a\", \"b\"]), enumerate([\"x\", \"y\"]), flatten([[1, 2], [3, [4]]]), range(5), range(2, 10, 3), slice([1, 2, 3], 1))\n"
                            "var s = set_of([1, 2, 2, 3, 1])\nprint(s, set_add(s, 4), set_add(s, 2), set_has(s, 3), set_remove(s, 1), s)\n"
                            "print(set_union([1, 2], [2, 3]), set_intersect([1, 2, 3], [2, 3, 4]), set_diff([1, 2, 3], [2]))\n"
                            "var m = {a: 1, b: 2}\nprint(m.keys(), m.values(), m.items(), m.has(\"a\"), m.get(\"z\", \"def\"), m.remove(\"a\"), m.keys(), m.set(\"c\", 3).keys())\n"
                            "print(map_keys(m), map_get(m, \"c\"), map_has(m, \"c\"), count([1, 2]), first([4, 5]), last([4, 5]), sum([1, 2, 3]), min_of([4, 2]), max_of([4, 2]))\n"
                            "function dbl(x) { return x * 2; }\nprint(map_fn([1, 2], \"dbl\"), map_fn([1, 2], x => x + 1), filter_fn([1, 2, 3], x => x > 1), reduce_fn([1, 2, 3], (a, b) => a + b, 0))"},
        {"lib_json", "var m = from_json(\"{\\\"a\\\": 1, \\\"b\\\": [true, null, \\\"x\\\\u00e9\\\\n\\\"], \\\"big\\\": 9007199254740993, \\\"f\\\": 1.5, \\\"e\\\": {}}\")\n"
                     "print(m.a, m.b, m.big, m.f, typeof(m.big), m.keys())\nprint(to_json(m))\nprint(to_json({list: [1, {k: \"v\"}], s: \"q\\\"t\"}, 2))\n"
                     "print(json_parse(json_stringify([1, \"two\", [3]])), to_json(\"tab\\tnew\\n\"), from_json(\"[1, 2\"), to_json(5L), to_json(1.25))"},
        {"lib_text", "print(ord(\"A\"), chr(65), chr(9731), ord(chr(128512)), utf8_len(\"héllo\"), str_len(\"héllo\"), utf8_chars(\"aé\"), ord(\"\"))\n"
                     "print(base64_encode(\"Hello, World!\"), base64_decode(\"SGVsbG8sIFdvcmxkIQ==\"), base64_encode(\"\"), base64_encode(\"a\"), base64_encode(\"ab\"), base64_decode(\"!!\"))\n"
                     "print(hex_encode(\"hi\"), hex_decode(\"6869\"), hex_decode(\"zz\"), url_encode(\"a b&c=d/é\"), url_decode(\"a%20b%26c+d%zz\"))\n"
                     "print(pad_left(\"7\", 3, \"0\"), pad_right(\"ab\", 4) + \"|\", capitalize(\"word\"), title_case(\"two words\"), repeat(\"-\", 3), substr(\"abcdef\", 2, 3), char_at(\"abc\", 1))"},
        {"lib_time", "var tm = now()\nprint(typeof(tm), tm > 1700000000L, now_ms() > 1700000000000L, clock() >= 0, typeof(clock()))\n"
                     "print(date_text(0), date_text(951782400), date_text(-86400), date_text(4102444800L), date_text(1234567890))"},
    };
    for (auto& c : lang2) p.push_back(c);
    p.push_back({"print_cycle", "var l = []\nl.Add(l)\nl.Add(1)\nprint(l)\nvar m = new Dictionary<string, int>()\nm.self = m\nm.k = 2\nprint(m)\n"
                                "var deep = []\nfor (var i = 0; i < 500; i++) { deep = [deep]; }\nprint(deep.ToString().Length > 0)"});
    p.push_back({"loop_limit", "var n = 0\nwhile (true) {\n  n++;\n}", 1000});
    p.push_back({"for_limit", "for (var i = 0; ; i++) { if (i == 5) print(i); }", 50});
    // Every CLI test script (tests/okayscript_cli) as well.
    for (auto& entry : std::filesystem::directory_iterator(OKS_CLI_CASES)) {
        // input reads standard input; system uses the CLI's own files, sleep and env (InstallSystemLibrary)
        if (entry.path().extension() != ".okay" || entry.path().stem() == "input" || entry.path().stem() == "system") continue;
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

    // ---- Language stage 2: the new instructions ----
    TEST("stage 2 instructions");
    const std::string two = Listing(
        "var big = 5L\nvar [a, b] = xs\nvar {k} = m\nimport \"lib.okay\" as lib\nvar mm = {q: 1}\n"
        "try { f(); } catch (Error e) { } finally { g(); }\ntry { f(); } finally { g(); }");
    for (const char* op : {"push_int 5", "unpack 0", "unpack 1", "unpack 0 k", "import lib.okay", "make_map 1", "rethrow",
                           "try_begin"}) {
        TEST(op);
        CHECK(Has(two, op));
    }
    TEST("a generator body");
    const std::string gen = Listing("function g(n) { var i = 0; while (i < n) { yield i; i++; } }", "g");
    CHECK(Has(gen, "gen_yield"));
    CHECK(Has(gen, "load_slot 1 i"));
    TEST("a local a lambda captures lives in the box, by name; the others stay slots");
    const std::string box = Listing("function f(a, b) { var c = 1; var d = 2; return () => a + c; }", "f");
    CHECK(Has(box, "define c"));          // captured: declared by name into the frame's box
    CHECK(Has(box, "define_slot"));       // d is still a slot
    CHECK(!Has(box, "define_slot 0 c"));
    {
        okayscript::Program prog;
        okayscript::ParseSource("function f(a, b) { var c = 1; var d = 2; return () => a + c; }", prog);
        const okayscript::FunctionDecl& fn = prog.functions.at("f");
        auto chunk = okayscript::CompileFunction(fn.params, fn.body);
        CHECK_EQ(chunk->paramSlot[0], -1);   // a: captured
        CHECK(chunk->paramSlot[1] >= 0);     // b: a slot
        CHECK_EQ(chunk->boxNames.size(), (std::size_t)2);   // a and c
        CHECK(!chunk->generator);
    }
    TEST("the engine's number rules stay: no Int without a value type that has them (123L is a float there)");
    CHECK(!okayscript::detail::HasInt<float>::value);
    CHECK(okayscript::detail::HasInt<okayscript::Value>::value);

    TEST_RESULT();
}
