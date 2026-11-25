// okay::js (our own JavaScript engine): spec behaviours, conformance, hostile input and fuzz.
//
// Standalone (only engine/src/JavaScript/Js*.cpp), so it also builds under sanitizers:
//   g++ -std=c++17 -fsanitize=address,undefined -Iengine/include -Itests \
//       -DOKAY_SOURCE_DIR='"."' tests/test_js_core.cpp engine/src/JavaScript/Js*.cpp
// OKAY_JS_FUZZ_ITERS=20000 runs a longer fuzz. The conformance script is
// tests/javascript/conformance.js (checks written from ECMA-262, section by section).
#include "test_framework.hpp"
#include "okay/JavaScript/JavaScript.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using namespace okay::js;

#ifndef OKAY_SOURCE_DIR
#define OKAY_SOURCE_DIR "."
#endif

namespace {

std::string FileText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct Options {
    bool stress = false;                 ///< collect at every safe point
    std::uint64_t budget = 50'000'000;
    std::size_t memory = 0;              ///< 0 = the default limit
};

// Everything a script printed (one line per console.log), then "ERROR: <message>" when it failed.
std::string Run(const std::string& src, Options o = {}) {
    Runtime rt;
    rt.gcStress = o.stress;
    rt.SetBudget(o.budget);
    if (o.memory) rt.memoryLimit = o.memory;
    std::string printed;
    rt.printSink = [&printed](const std::string& line) { printed += line + "\n"; };
    std::string why;
    if (!rt.Run(src, "t.js", &why)) printed += "ERROR: " + why + "\n";
    return printed;
}
std::string Run(const std::string& src, bool stress, std::uint64_t budget = 50'000'000, std::size_t memLimit = 0) {
    Options o;
    o.stress = stress;
    o.budget = budget;
    o.memory = memLimit;
    return Run(src, o);
}

// The script prints exactly `want` (lines joined with \n).
void Expect(const std::string& src, const std::string& want) {
    const std::string got = Run(src, Options{});
    const bool same = got == want + "\n";
    if (!same) std::cerr << "  script: " << src << "\n  printed: " << got << "  expected: " << want << "\n";
    CHECK(same);
}
// The script fails with an error that says `part`.
void ExpectError(const std::string& src, const std::string& part) {
    const std::string got = Run(src, Options{});
    const bool ok = got.find("ERROR: ") != std::string::npos && got.find(part) != std::string::npos;
    if (!ok) std::cerr << "  script: " << src << "\n  printed: " << got << "  expected an error saying: " << part << "\n";
    CHECK(ok);
}

void ConformanceScript(bool stress) {
    std::string src = FileText(std::string(OKAY_SOURCE_DIR) + "/tests/javascript/conformance.js");
    CHECK(!src.empty());
    Runtime rt;
    rt.gcStress = stress;
    rt.SetBudget(200'000'000);
    std::string out;
    rt.printSink = [&](const std::string& s) { out += s + "\n"; };
    std::string err;
    bool ok = rt.Run(src, "conformance.js", &err);
    if (!ok) std::cerr << "  conformance error: " << err << "\n";
    CHECK(ok);
    Value after = rt.GetGlobal("afterJobs");
    CHECK(rt.IsCallable(after));
    if (rt.IsCallable(after)) CHECK(rt.CallHost(after, Value(), {}, nullptr, &err));
    if (out.find("FAIL") != std::string::npos || out.find("failures 0") == std::string::npos) std::cerr << out;
    CHECK(out.find("FAIL") == std::string::npos);
    CHECK(out.find("conformance checks") != std::string::npos);
    CHECK(out.find("async checks done, failures 0") != std::string::npos);
}

// 13.6-13.13: precedence and associativity, 7.1 / 7.2: conversions and comparisons
void Operators() {
    Expect("console.log(1 + 2 * 3 - 4 / 2, 2 ** 3 ** 2, (-2) ** 2, 7 % -3, -7 % 3)", "5 512 4 1 -1");
    Expect("console.log(1 < 2 < 3, 3 > 2 > 1, 1 == '1', 1 === '1', null == undefined, null === undefined)",
           "true false true false true false");
    Expect("console.log(1 + '2', '3' - 1, '4' * '2', [] + [], [] + {}, true + 1, null + 1, undefined + 1)",
           "12 2 8  [object Object] 2 1 NaN");
    Expect("console.log(5 & 3 | 8 ^ 2, ~0, -1 >>> 0, 1 << 33, -16 >> 2)", "11 -1 4294967295 2 -4");
    Expect("console.log(typeof 1, typeof 'a', typeof {}, typeof null, typeof undefined, typeof (() => 1), typeof Symbol())",
           "number string object object undefined function symbol");
    Expect("let a = 0; const r = a || 'x'; const s = a ?? 'y'; const t = a && 'z'; console.log(r, s, t)", "x 0 0");
    Expect("console.log(0.1 + 0.2, 1 / 3, 100 / 3, 2 ** 53, 2 ** 53 + 1, 1e21, 1e-7, 123e-20, -0, 0 / 0)",
           "0.30000000000000004 0.3333333333333333 33.333333333333336 9007199254740992 9007199254740992 1e+21 1e-7 1.23e-18 0 NaN");
    Expect("console.log(Number.MAX_SAFE_INTEGER, Number.MIN_VALUE, Number.MAX_VALUE, Number.EPSILON)",
           "9007199254740991 5e-324 1.7976931348623157e+308 2.220446049250313e-16");
    Expect("console.log((1234.5678).toFixed(2), (0.000001234).toPrecision(2), (1e21).toFixed(2), (-1.5).toFixed(0), (255).toString(36))",
           "1234.57 0.0000012 1e+21 -2 73");
    Expect("console.log('b' > 'a', 'B' > 'a', '2' > '10', 2 > '10', [2] > 1, null >= 0, undefined == 0)",
           "true false true false true true false");
}

// 15.2 closures, 9.1 scopes, 14.7.4.4 per-iteration bindings
void Closures() {
    Expect("const f = []; for (let i = 0; i < 3; i++) f.push(() => i); console.log(f.map(g => g()).join())", "0,1,2");
    Expect("const f = []; for (var i = 0; i < 3; i++) f.push(() => i); console.log(f.map(g => g()).join())", "3,3,3");
    Expect("function mk() { let n = 0; return [() => ++n, () => n]; } const [inc, get] = mk(); inc(); inc(); console.log(get())", "2");
    Expect("let x = 'outer'; { let x = 'inner'; console.log(x); } console.log(x)", "inner\nouter");
    Expect("function f() { return typeof g; function g() {} } console.log(f())", "function");
    Expect("const a = (function () { let s = 0; for (let i = 0; i < 5; i++) { let j = i * 2; s += (() => j)(); } return s; })(); console.log(a)", "20");
    ExpectError("{ console.log(z); let z = 1; }", "ReferenceError: 'z' is used before it is initialized");
    ExpectError("const c = 1; c = 2;", "TypeError: assignment to the constant 'c'");
    ExpectError("undeclaredThing = 1;", "ReferenceError: 'undeclaredThing' is not defined");
    ExpectError("let dup = 1; let dup = 2;", "SyntaxError");
}

// 10.1 ordinary objects, prototypes, this binding (10.2.1.2 OrdinaryCallBindThis)
void Prototypes() {
    Expect("function P(x) { this.x = x; } P.prototype.get = function () { return this.x; }; const p = new P(4);"
           "console.log(p.get(), p instanceof P, Object.getPrototypeOf(p) === P.prototype, p.constructor === P)",
           "4 true true true");
    Expect("class A { constructor() { this.v = 1; } m() { return 'A' + this.v; } static s() { return 'S'; } }"
           "class B extends A { constructor() { super(); this.v = 2; } m() { return 'B' + super.m(); } }"
           "const b = new B(); console.log(b.m(), B.s(), b instanceof A, Object.keys(b).join())",
           "BA2 S true v");
    Expect("const o = { name: 'o', f() { return this.name; }, g: () => typeof this }; const h = o.f;"
           "console.log(o.f(), o.g(), h.call({name: 'c'}))", "o undefined c");
    Expect("const base = { hi() { return 'hi ' + this.n; } }; const d = Object.create(base); d.n = 'd'; console.log(d.hi(), 'hi' in d, d.hasOwnProperty('hi'))",
           "hi d true false");
    Expect("const o = {}; Object.defineProperty(o, 'x', { get() { return 7; }, enumerable: false }); console.log(o.x, Object.keys(o).length, JSON.stringify(Object.getOwnPropertyDescriptor(o, 'x')))",
           "7 0 {\"enumerable\":false,\"configurable\":false}");
    Expect("class C { #p = 1; }", "ERROR: t.js:1: SyntaxError: #private class members are not supported");
    Expect("class Q { a = 1; b = this.a + 1; static c = 3; } const q = new Q(); console.log(q.a, q.b, Q.c)", "1 2 3");
    Expect("class V { get len() { return 3; } set len(v) { this._l = v; } } const v = new V(); v.len = 9; console.log(v.len, v._l)", "3 9");
}

// 22.1 strings, 23.1 arrays, 25.5 JSON, 24.1 Map / 24.2 Set
void Library() {
    Expect("console.log([3, 1, 10, 2].sort().join(), [3, 1, 10, 2].sort((a, b) => a - b).join(), [1, 2, 3].reverse().join())",
           "1,10,2,3 1,2,3,10 3,2,1");
    Expect("const a = [1, 2, 3, 4, 5]; console.log(a.splice(1, 2).join(), a.join(), a.slice(-2).join(), a.indexOf(4), a.includes(9))",
           "2,3 1,4,5 4,5 1 false");
    Expect("console.log([1, [2, [3]]].flat(Infinity).join(), Array.from('abc').join('-'), Array.of(1, 2).length, [..."
           "new Set([1, 1, 2])].join())", "1,2,3 a-b-c 2 1,2");
    Expect("console.log('  Abc  '.trim().toUpperCase(), 'a-b-c'.split('-').length, 'x'.padStart(3, '.'), 'ab'.repeat(2), 'hello'.at(-1))",
           "ABC 3 ..x abab o");
    Expect("console.log('aXbXc'.replaceAll('X', '_'), 'abc'.replace('b', m => m.toUpperCase()), 'abc'.indexOf('c'), 'abc'.slice(1, 2))",
           "a_b_c aBc 2 b");
    Expect("const t = JSON.stringify({a: [1, {b: 'c'}], d: null, e: undefined}); console.log(t, JSON.parse(t).a[1].b)",
           "{\"a\":[1,{\"b\":\"c\"}],\"d\":null} c");
    Expect("console.log(JSON.stringify([new Map(), 'é\\u0001'], null, 1))", "[\n {},\n \"é\\u0001\"\n]");
    Expect("const m = new Map(); m.set('a', 1).set('b', 2); m.delete('a'); console.log(m.size, [...m].join(';'), m.get('b'))",
           "1 b,2 2");
    Expect("console.log(Math.max(1, 3, 2), Math.min(), Math.round(-0.5), Math.sign(-0), Math.trunc(9.9), Math.hypot(5, 12))",
           "3 Infinity 0 0 9 13");
    Expect("console.log(parseInt('101', 2), parseInt('  12abc'), parseFloat('.5e1'), Number('0o17'), Number('12px'), isNaN('x'))",
           "5 12 5 15 NaN true");
    Expect("console.log(String(Symbol('s')), Symbol.for('a') === Symbol.for('a'), Object(1) instanceof Number)", "Symbol(s) true true");
}

// 14.15 exceptions with stack information and lines
void Exceptions() {
    Expect("try { null.f; } catch (e) { console.log(e instanceof TypeError, e.message); }", "true can not read 'f' of null");
    Expect("function a() { b(); } function b() { throw new Error('deep'); }\ntry { a(); } catch (e) { console.log(e.stack.split('\\n').slice(0, 3).join('|')); }",
           "Error: deep|  in b at t.js:1|  in a at t.js:1");
    Expect("try { throw 1; } catch (e) { console.log('c', e); } finally { console.log('f'); }", "c 1\nf");
    Expect("function f() { try { return 'a'; } finally { console.log('fin'); } } console.log(f())", "fin\na");
    Expect("function g() { for (const x of [1, 2, 3]) { try { if (x === 2) return x; } finally { console.log('f' + x); } } } console.log(g())",
           "f1\nf2\n2");
    ExpectError("\n\nfunction f() {\n  return undefined.x;\n}\nf();", "t.js:4: TypeError: can not read 'x' of undefined");
    ExpectError("let o = {}; o.method();", "TypeError: 'o.method' is undefined, not a function");
    ExpectError("throw new RangeError('out');", "t.js:1: RangeError: out");
    ExpectError("throw 'a string';", "uncaught a string");
    Expect("class E extends Error { constructor(m) { super(m); this.name = 'E'; } } try { throw new E('x'); } catch (e) { console.log(String(e), e instanceof E, e instanceof Error); }",
           "E: x true true");
}

// 27.5 generators, 27.2 promises and 27.7 async functions
void Generators() {
    Expect("function* g() { const x = yield 1; console.log('got', x); yield x * 2; } const it = g(); console.log(it.next().value); console.log(it.next(5).value); console.log(it.next().done)",
           "1\ngot 5\n10\ntrue");
    Expect("function* inner() { yield 'a'; return 'r'; } function* outer() { const v = yield* inner(); yield v; } console.log([...outer()].join())", "a,r");
    Expect("function* g() { try { yield 1; yield 2; } finally { console.log('closed'); } } for (const v of g()) { console.log(v); break; }",
           "1\nclosed");
    Expect("async function f() { console.log('a'); await null; console.log('c'); return 'd'; } f().then(console.log); console.log('b');",
           "a\nb\nc\nd");
    Expect("Promise.all([1, Promise.resolve(2), new Promise(r => r(3))]).then(v => console.log(v.join()));", "1,2,3");
    Expect("(async () => { try { await Promise.reject(new Error('no')); } catch (e) { console.log('caught', e.message); } })();", "caught no");
    Expect("const order = []; Promise.resolve().then(() => order.push(1)); queueMicrotask(() => order.push(2)); order.push(0); Promise.resolve().then(() => console.log(order.join()));",
           "0,1,2");
}

// 12 and 13-16: syntax errors carry the line and column
void SyntaxErrors() {
    Runtime rt;
    auto first = [&](const std::string& src) {
        auto e = rt.CheckSyntax(src, "s");
        return e.empty() ? SyntaxError{} : e[0];
    };
    SyntaxError e = first("let a = 1;\nlet b = = 2;");
    CHECK(e.line == 2);
    CHECK(e.column == 9);
    e = first("function f( {");
    CHECK(e.line == 1 && !e.message.empty());
    e = first("'abc");
    CHECK(e.message.find("never closed") != std::string::npos);
    e = first("x = /re/;");
    CHECK(e.message.find("regular expression") != std::string::npos);
    e = first("a ?? b || c");
    CHECK(e.message.find("??") != std::string::npos);
    e = first("-2 ** 2");
    CHECK(e.message.find("**") != std::string::npos);
    e = first("with (a) {}");
    CHECK(e.message.find("strict") != std::string::npos);
    e = first("break;");
    CHECK(e.message.find("break") != std::string::npos);
    e = first("function f(a, a) {}");
    CHECK(e.message.find("twice") != std::string::npos);
    e = first("({a = 1});");
    CHECK(e.message.find("destructuring") != std::string::npos);
    e = first("let x = 010;");
    CHECK(e.message.find("octal") != std::string::npos);
    CHECK(rt.CheckSyntax("const ok = [1, 2].map(x => x * 2);\nexport default class A { m() { return ok; } }", "s").empty());
    CHECK(rt.CheckSyntax("let a = 1, b = 2;\na\n++b", "s").empty());   // ASI: a; ++b;
    CHECK(rt.CheckSyntax("let r = a\n(b)", "s").empty());              // no ASI before '(' (a call)
}

struct Hostile {
    const char* src;
    const char* want;   ///< part of the error, "" for any error, "OK" when it must finish without one
};

// Hostile one-liners: each stops within the budget with a clear error, never freezes or crashes.
void HostileOneLiners() {
    static const char deep[] =
        "x = ((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((("
        "((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((1)))))"
        "))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))"
        "))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))";
    const Hostile cases[] = {
        {"while (1) {}", "instruction budget"},
        {"for (;;) { try { throw 1; } catch (e) {} }", "instruction budget"},
        {"for (;;) { try { null.x; } catch (e) {} }", "instruction budget"},
        {"let s = 'x'; while (true) s += s;", ""},
        {"'x'.repeat(1e9)", "RangeError"},
        {"'ab'.repeat(2 ** 27)", ""},
        {"let a = []; while (1) a.push(a.length);", ""},
        {"[].concat.apply([], new Array(1e8))", "RangeError"},
        {"new Array(2 ** 31).join('x')", "instruction budget"},
        {"let a = new Array(5e6).fill(0); a.sort((x, y) => x - y)", "instruction budget"},
        {"function f() { f(); } f()", "RangeError"},
        {"function f() { return [1].map(f); } f()", "RangeError"},
        {"let o = {}; o.o = o; JSON.stringify(o)", "TypeError"},
        {"JSON.parse('['.repeat(100000))", "SyntaxError"},
        {"eval('1')", "EvalError"},
        {"Function('return 1')()", "EvalError"},
        {"let p = Promise.resolve(); (function g() { p = p.then(g); })()", "instruction budget"},
        {"async function f() { while (1) await 0; } f()", "instruction budget"},
        {"function* g() { while (1) yield; } for (const x of g());", "instruction budget"},
        {"let s = ''; for (let i = 0; i < 1e9; i++) s += 'ab'", ""},
        {"new Array(1e7).fill(1).map(x => x)", "instruction budget"},
        {"let a = [1]; for (;;) a = [a]", ""},
        {"'x'.padStart(2 ** 30)", "RangeError"},
        {"Array.from({length: 1e9})", "RangeError"},
        {"[...Array(1e8).keys()]", "instruction budget"},
        {"let m = new Map(); for (let i = 0; ; i++) m.set(i, i)", ""},
        {"let o = {}; for (let i = 0; ; i++) o['k' + i] = i", ""},
        {"const a = []; a.length = 4294967295; a.sort();", "RangeError"},
        {"let a = []; a[0] = a; a.flat(Infinity)", "RangeError"},
        {"let a = []; a[0] = a; String(a) + JSON.stringify([a.join()])", "OK"},
        {deep, "nested too deeply"},
        {"Object.defineProperty(Array.prototype, 0, { get() { return 1; } }); [,].sort()", "TypeError"},
        {"class A extends null {} new A()", "TypeError"},
        {"const big = 'x'.repeat(1 << 20); let arr = []; for (;;) arr.push(big + arr.length)", ""},
        {"let s = 'a'.repeat(1 << 16); for (;;) s = s.split('').reverse().join('')", "instruction budget"},
        {"Array.prototype[Symbol.iterator] = function* () { while (1) yield 1; }; [...[1]]", "instruction budget"},
        {"const o = { get x() { return this.x; } }; o.x", "RangeError"},
        {"const o = { valueOf() { return o + 1; } }; o + 1", "RangeError"},
        {"let a = [1, 2, 3]; a.sort(() => { a.push(1); return 1; })", "OK"},
        {"Promise.resolve().then(function f() { return Promise.resolve().then(f); })", ""},   // budget or memory, whichever first
    };
    for (const Hostile& h : cases) {
        auto t0 = std::chrono::steady_clock::now();
        std::string out = Run(h.src, false, 2'000'000, 64u << 20);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        bool okRun = std::string(h.want) == "OK";
        bool stopped = okRun ? out.find("ERROR") == std::string::npos : out.find("ERROR: ") != std::string::npos;
        bool right = okRun || !*h.want || out.find(h.want) != std::string::npos;
        if (!stopped || !right || ms > 10000) std::cerr << "  hostile: " << h.src << "\n    -> " << out << "    (" << ms << " ms)\n";
        CHECK(stopped);
        CHECK(right);
        CHECK(ms < 10000);
    }
}

// The memory limit stops a script that keeps everything it makes.
void MemoryLimit() {
    std::string out = Run("const keep = []; for (;;) keep.push(new Array(1000).fill(keep.length));", false, 0, 24u << 20);
    if (out.find("more memory than allowed") == std::string::npos) std::cerr << "  memory: " << out;
    CHECK(out.find("more memory than allowed") != std::string::npos);
    // garbage that is not kept is collected, so the same work with nothing kept finishes
    out = Run("let n = 0; for (let i = 0; i < 20000; i++) { const a = new Array(1000).fill(i); n += a.length; } console.log(n)", false, 0, 24u << 20);
    if (out != "20000000\n") std::cerr << "  memory 2: " << out;
    CHECK(out == "20000000\n");
}

// The collector at every safe point, with library calls that call back while they hold values (the class of
// bug the Lua review found in table.sort): sort, map, reduce, JSON revivers, Map.forEach, getters, spread.
void Rooting() {
    const char* src =
        "const a = []; for (let i = 0; i < 200; i++) a.push({v: (i * 37) % 101, s: 'x' + i});\n"
        "a.sort((x, y) => { const junk = [x.s + y.s, {t: 1}]; if (junk.length > 5) a.length = 0; return x.v - y.v; });\n"
        "let okSort = true; for (let i = 1; i < a.length; i++) if (a[i - 1].v > a[i].v) okSort = false;\n"
        "const m = a.map(o => ({w: o.s + '!'})).filter(o => o.w.length > 2).reduce((s, o) => s + o.w.length, 0);\n"
        "const j = JSON.parse(JSON.stringify(a), (k, v) => typeof v === 'string' ? v + '?' : v);\n"
        "const mp = new Map(a.map(o => [o.s, o])); let cnt = 0; mp.forEach((v, k) => { mp.delete(k); cnt++; if (cnt < 300) mp.set(k + 'n', {}); });\n"
        "const g = { get big() { return Array.from({length: 50}, (_, i) => 'g' + i); } }; const sp = [...g.big, ...'abc'];\n"
        "function* gen() { for (let i = 0; i < 50; i++) yield {i, s: String(i)}; } const gs = [...gen()].map(o => o.s).join('');\n"
        "console.log(okSort, a.length, m, j[0].s.endsWith('?'), cnt, sp.length, gs.length);\n";
    std::string normal = Run(src, false);
    std::string stressed = Run(src, true);
    if (normal != stressed || normal.find("ERROR") != std::string::npos) std::cerr << "  normal: " << normal << "  stress: " << stressed;
    CHECK(normal == stressed);
    CHECK(normal.find("true 200") == 0);
}

// The host API: CallHost, refs, globals, host classes with get / set hooks.
double g_boxValue = 1;
void HostApi() {
    Runtime rt;
    std::string out, err;
    rt.printSink = [&](const std::string& s) { out += s + "\n"; };
    HostClass box;
    box.name = "Box";
    box.get = [](Runtime&, HostObj*, String* k, Value& o) {
        if (Utf16To8(k->s) != "v") return false;
        o = Value::Num(g_boxValue);
        return true;
    };
    box.set = [](Runtime& rt, HostObj*, String* k, const Value& v) {
        if (Utf16To8(k->s) != "v") rt.ThrowType("Box has only v");
        g_boxValue = rt.ToNumber(v);
        return true;
    };
    std::uint32_t cls = rt.AddHostClass(box);
    rt.SetGlobal("box", Value::Obj(rt.NewHost(cls)));
    Value exported;
    bool ran = rt.Run("box.v = box.v + 41; console.log(box.v, `${box.v}`);\n"
                      "export default function twice(x) { return x * 2; }", "h.js", &err, &exported);
    if (!ran) std::cerr << "  host: " << err << "\n";
    CHECK(ran);
    CHECK(g_boxValue == 42);
    CHECK(out == "42 42\n");
    CHECK(rt.IsCallable(exported));
    int ref = rt.Ref(exported);
    rt.Collect();
    Value r;
    CHECK(rt.CallHost(rt.GetRef(ref), Value(), {Value::Num(21)}, &r, &err));
    CHECK(r.IsNum() && r.n == 42);
    CHECK(!rt.Run("box.w = 1;", "h3.js", &err));
    CHECK(err.find("Box has only v") != std::string::npos);
    rt.Unref(ref);
    // a budget stop is reported and the runtime keeps working afterwards
    rt.SetBudget(10000);
    CHECK(!rt.Run("for (;;) {}", "h4.js", &err));
    CHECK(err.find("instruction budget") != std::string::npos);
    rt.SetBudget(10000);
    CHECK(rt.Run("console.log('after');", "h5.js", &err));
    CHECK(out.find("after") != std::string::npos);
}

// Mutation fuzz: random edits of valid snippets must parse or fail cleanly, and run within the budget.
void Fuzz() {
    const char* seeds[] = {
        "let a = [1, 2, 3].map(x => x * 2).filter(x => x > 2); console.log(a.join());",
        "class A { constructor(x) { this.x = x; } get y() { return this.x + 1; } } const o = new A(1); o.y;",
        "function* g() { yield 1; yield* [2, 3]; } for (const v of g()) { if (v > 2) break; }",
        "const {a, b: [c, ...d] = [], ...e} = {a: 1, b: [2, 3, 4], f: 5}; `${a}${c}${d}`;",
        "try { JSON.parse('{\"x\": [1, 2, {\"y\": null}]}'); } catch (e) { } finally { 1; }",
        "async function f() { await 1; return 2; } f().then(v => v + 1);",
        "switch (3) { case 1: break; case 3: { let q = 1; } default: ; } outer: for (;;) { break outer; }",
        "const m = new Map([[1, 'a']]); m.set({}, []).forEach((v, k) => { typeof k; });",
        "let s = 'abc'.split('').reverse().join('').padStart(5).toUpperCase(); s.at(-1) ?? s?.x?.y;",
    };
    const char* frag[] = {"(", ")", "{", "}", "[", "]", "=>", "yield", "await", "...", "`${", "'", "\"", "/", "*", ",",
                          ";", "\n", "class", "function", "new", "super", "this", "?.", "??", "#", "\\u", "0x", "1e"};
    int iters = 400;
    if (const char* e = std::getenv("OKAY_JS_FUZZ_ITERS")) iters = std::atoi(e);
    std::mt19937 rng(12345);
    int ran = 0, failed = 0;
    for (int i = 0; i < iters; ++i) {
        std::string s = seeds[rng() % (sizeof(seeds) / sizeof(seeds[0]))];
        int edits = 1 + (int)(rng() % 4);
        for (int k = 0; k < edits && !s.empty(); ++k) {
            std::size_t at = rng() % s.size();
            switch (rng() % 4) {
                case 0: s.erase(at, 1 + rng() % 3); break;
                case 1: s.insert(at, frag[rng() % (sizeof(frag) / sizeof(frag[0]))]); break;
                case 2: s.insert(at, s.substr(rng() % s.size(), rng() % 8)); break;
                default: s[at] = (char)(32 + rng() % 95); break;
            }
        }
        std::string out = Run(s, i % 7 == 0, 200'000, 32u << 20);
        ++ran;
        if (out.find("ERROR") != std::string::npos) ++failed;
    }
    CHECK(ran == iters);
    std::cout << "  fuzz: " << ran << " runs, " << failed << " ended in an error (expected for broken input)\n";
}

} // namespace

int main() {
    RUN_SUITE("js_core");
    Operators();
    Closures();
    Prototypes();
    Library();
    Exceptions();
    Generators();
    SyntaxErrors();
    ConformanceScript(false);
    ConformanceScript(true);
    HostileOneLiners();
    MemoryLimit();
    Rooting();
    HostApi();
    Fuzz();
    TEST_MAIN_RESULT();
}
