// okay::js conformance checks, written from ECMA-262 (the section is named on
// each group). Every check prints nothing when it passes and a FAIL line when it
// does not; the last line says how many ran.
let checks = 0, fails = 0;
function eq(name, got, want) {
    checks++;
    const same = (got === want) || (got !== got && want !== want);
    if (!same) { fails++; console.log("FAIL " + name + ": got " + String(got) + ", want " + String(want)); }
}
function ok(name, cond) { eq(name, !!cond, true); }
function throws(name, fn, type) {
    checks++;
    try { fn(); } catch (e) {
        if (!type || e instanceof type) return;
        fails++; console.log("FAIL " + name + ": threw " + e + " (wrong type)"); return;
    }
    fails++; console.log("FAIL " + name + ": did not throw");
}

// ---- 13.6-13.13 operators and precedence
eq("prec1", 1 + 2 * 3, 7);
eq("prec2", (1 + 2) * 3, 9);
eq("prec3", 2 ** 3 ** 2, 512);
eq("prec4", (-2) ** 2, 4);
eq("prec5", 10 - 4 - 3, 3);
eq("prec6", 1 < 2 === true, true);
eq("prec7", 1 + 2 + "3", "33");
eq("prec8", "1" + 2 + 3, "123");
eq("prec9", 7 % 3 * 2, 2);
eq("prec10", 1 | 2 & 3, 3);
eq("prec11", 5 & 3 ^ 1, 0);
eq("prec12", true ? 1 : false ? 2 : 3, 1);
eq("prec13", null ?? "d", "d");
eq("prec14", 0 ?? "d", 0);
eq("prec15", (0 || null) ?? "z", "z");
eq("shift1", -8 >> 1, -4);
eq("shift2", -8 >>> 28, 15);
eq("shift3", 1 << 31, -2147483648);
eq("shift4", 1 << 32, 1);
eq("bitnot", ~5, -6);
eq("mod1", -7 % 3, -1);
eq("mod2", 7.5 % 2, 1.5);
eq("div0", 1 / 0, Infinity);
eq("negzero", 1 / -0, -Infinity);
eq("pow nan", 1 ** NaN, NaN);
eq("comma", (1, 2, 3), 3);
eq("void", void 1, undefined);
eq("typeof1", typeof null, "object");
eq("typeof2", typeof undefinedName, "undefined");
eq("typeof3", typeof function () {}, "function");
eq("typeof4", typeof Symbol(), "symbol");

// ---- 7.1 conversions and 7.2 comparisons
eq("tonum1", +"  42  ", 42);
eq("tonum2", +"0x1F", 31);
eq("tonum3", +"0b101", 5);
eq("tonum4", +"1e3", 1000);
eq("tonum5", +"", 0);
eq("tonum6", +"abc", NaN);
eq("tonum7", +[], 0);
eq("tonum8", +[7], 7);
eq("tonum9", +{}, NaN);
eq("tonum10", +true, 1);
eq("tonum11", +null, 0);
eq("tonum12", +undefined, NaN);
eq("tonum13", +"-Infinity", -Infinity);
eq("tonum14", +"1_000", NaN);
eq("tostr1", String(123), "123");
eq("tostr2", String(0.000001), "0.000001");
eq("tostr3", String(1e-7), "1e-7");
eq("tostr4", String(1.5e300), "1.5e+300");
eq("tostr5", String(123e20), "1.23e+22");
eq("tostr6", String(-1.5), "-1.5");
eq("tostr7", String([1, [2, 3]]), "1,2,3");
eq("tostr8", String({}), "[object Object]");
eq("tostr9", String(null), "null");
eq("tostr10", (25).toString(2), "11001");
eq("tostr11", (255).toString(16), "ff");
eq("tostr12", (0.5).toString(2), "0.1");
eq("loose1", 1 == "1", true);
eq("loose2", null == undefined, true);
eq("loose3", null == 0, false);
eq("loose4", "" == 0, true);
eq("loose5", [1] == 1, true);
eq("loose6", NaN == NaN, false);
eq("loose7", true == 1, true);
eq("strict1", 1 === 1.0, true);
eq("strict2", "a" === "a", true);
eq("strict3", {} === {}, false);
eq("strict4", 0 === -0, true);
eq("cmp1", "a" < "b", true);
eq("cmp2", "B" < "a", true);
eq("cmp3", "10" < "9", true);
eq("cmp4", 10 < 9, false);
eq("cmp5", "10" < 9, false);
eq("cmp6", NaN < 1, false);
eq("cmp7", NaN >= 1, false);
eq("cmp8", null >= 0, true);
eq("cmp9", undefined >= 0, false);
eq("objis", Object.is(-0, 0), false);
eq("objis2", Object.is(NaN, NaN), true);

// ---- 14.3 declarations, 9.1 scoping, TDZ
var v1 = 1;
{ let v1 = 2; eq("blockshadow", v1, 2); }
eq("blockshadow2", v1, 1);
throws("tdz", () => { tdzName; let tdzName = 1; }, ReferenceError);
throws("constassign", () => { const c = 1; c = 2; }, TypeError);
throws("undeclared", () => { notDeclaredAnywhere = 5; }, ReferenceError);
eq("hoist", hoisted(), "h");
function hoisted() { return "h"; }
(function () {
    eq("varhoist", typeof laterVar, "undefined");
    var laterVar = 1;
})();

// ---- 15.2 closures and per-iteration bindings (14.7.4.4)
{
    const fs = [];
    for (let i = 0; i < 3; i++) fs.push(() => i);
    eq("periter", fs.map(f => f()).join(), "0,1,2");
    const gs = [];
    for (var j = 0; j < 3; j++) gs.push(() => j);
    eq("varloop", gs.map(f => f()).join(), "3,3,3");
    const hs = [];
    for (const k of [7, 8]) hs.push(() => k);
    eq("forofbind", hs[0]() + hs[1](), 15);
    function counter() { let n = 0; return { inc: () => ++n, get: () => n }; }
    const c1 = counter(), c2 = counter();
    c1.inc(); c1.inc(); c2.inc();
    eq("closure", c1.get() * 10 + c2.get(), 21);
    function outer() { let a = 1; function mid() { let b = 2; return () => a + b; } return mid(); }
    eq("closure2", outer()(), 3);
}

// ---- 15.1 parameters
function defaults(a, b = a + 1, ...rest) { return a + b + rest.length; }
eq("default1", defaults(1), 3);
eq("default2", defaults(1, 5, 9, 9), 8);
eq("fnlength", defaults.length, 1);
function args() { return arguments.length + arguments[0]; }
eq("arguments", args(5, 6), 7);
eq("arrowargs", ((...a) => a.length)(1, 2, 3), 3);
eq("restdestruct", (([a, b], {c}) => a + b + c)([1, 2], {c: 3}), 6);

// ---- 13.15.5 destructuring
{
    const [a, , b = 5, ...r] = [1, 2, undefined, 4, 5];
    eq("arrdes", a + b + r.length, 8);
    const {x, y: {z = 9} = {}, ...others} = {x: 1, w: 2, q: 3};
    eq("objdes", x + z + Object.keys(others).length, 12);
    let p = 1, q = 2;
    [p, q] = [q, p];
    eq("swap", p * 10 + q, 21);
    const o = {};
    ({a: o.first, b: o["second"]} = {a: 1, b: 2});
    eq("memberdes", o.first + o.second, 3);
    const {length} = "hello";
    eq("strdes", length, 5);
    throws("desnull", () => { const {a} = null; }, TypeError);
    const [s1, s2] = "ab";
    eq("striter", s1 + s2, "ab");
    const {["comp" + "uted"]: cv} = {computed: 4};
    eq("compdes", cv, 4);
}

// ---- 13.2 literals, spread, templates
{
    const name = "x";
    const o = {name, [name + "1"]: 1, "quoted key": 2, 3: "three", method() { return this.name; },
               get g() { return 10; }, set s(v) { this._s = v; }};
    o.s = 4;
    eq("objlit", o.name + o.x1 + o["quoted key"] + o[3] + o.method() + o.g + o._s, "x12threex104");
    eq("spreadobj", JSON.stringify({...{a: 1, b: 2}, b: 3}), '{"a":1,"b":3}');
    eq("spreadarr", [...[1, 2], ...new Set([3]), ..."ab"].join(), "1,2,3,a,b");
    eq("spreadcall", Math.max(...[1, 5, 3]), 5);
    eq("template", `a${1 + 1}b${"c"}`, "a2bc");
    eq("template2", `line1
line2`.length, 11);
    eq("nested template", `x${`y${1}`}`, "xy1");
    eq("holes", [1, , 3].length, 3);
    eq("holes2", 1 in [1, , 3], false);
    eq("numkeys", Object.keys({b: 1, 2: 1, a: 1, 1: 1}).join(), "1,2,b,a");
    eq("fnname", (function namedFn() {}).name, "namedFn");
    const anon = () => 1;
    eq("inferred name", anon.name, "anon");
    eq("method name", o.method.name, "method");
}

// ---- 14.7 loops, labels, switch
{
    let s = "";
    outer: for (let i = 0; i < 3; i++) {
        for (let j = 0; j < 3; j++) {
            if (j === 1) continue outer;
            if (i === 2) break outer;
            s += i + "" + j + " ";
        }
    }
    eq("labels", s, "00 10 ");
    let n = 0;
    do { n++; } while (n < 5);
    eq("dowhile", n, 5);
    let w = 0;
    while (true) { if (++w > 3) break; }
    eq("while", w, 4);
    const keys = [];
    for (const k in {a: 1, b: 2}) keys.push(k);
    eq("forin", keys.join(), "a,b");
    function sw(x) {
        let r = "";
        switch (x) {
            case 1: r += "one";
            case 2: r += "two"; break;
            default: r += "other";
            case 3: r += "three";
        }
        return r;
    }
    eq("switch1", sw(1), "onetwo");
    eq("switch2", sw(3), "three");
    eq("switch3", sw(9), "otherthree");
    lbl: { s = "in"; break lbl; }
    eq("blocklabel", s, "in");
    const proto = {inherited: 1};
    const child = Object.create(proto);
    child.own = 2;
    const all = [];
    for (const k in child) all.push(k);
    eq("forin proto", all.join(), "own,inherited");
}

// ---- 14.15 try / catch / finally
{
    function tf() {
        try { return "try"; } finally { tfLog.push("finally"); }
    }
    var tfLog = [];
    eq("finally return", tf(), "try");
    eq("finally ran", tfLog.join(), "finally");
    function tf2() { try { throw new Error("x"); } catch (e) { return "caught " + e.message; } finally { tfLog.push("f2"); } }
    eq("catch return", tf2(), "caught x");
    function tf3() { for (let i = 0; i < 3; i++) { try { if (i === 1) break; } finally { tfLog.push("loop" + i); } } return tfLog.slice(-2).join(); }
    eq("break finally", tf3(), "loop0,loop1");
    function tf4() { try { return 1; } finally { return 2; } }
    eq("finally override", tf4(), 2);
    function tf5() {
        let r = "";
        try { try { throw 1; } finally { r += "a"; } } catch (e) { r += "b" + e; }
        return r;
    }
    eq("nested try", tf5(), "ab1");
    let caught;
    try { null.x; } catch (e) { caught = e instanceof TypeError; }
    eq("typeerror", caught, true);
    try { undefinedFunction(); } catch (e) { caught = e.name; }
    eq("referror", caught, "ReferenceError");
    try { throw {code: 5}; } catch ({code}) { caught = code; }
    eq("catch destruct", caught, 5);
    try { throw 1; } catch { caught = "no binding"; }
    eq("optional catch", caught, "no binding");
    const err = new RangeError("bad", {cause: "why"});
    eq("error fields", err.name + ":" + err.message + ":" + err.cause, "RangeError:bad:why");
    ok("error stack", typeof err.stack === "string" && err.stack.indexOf("RangeError") === 0);
    eq("error toString", String(new TypeError("t")), "TypeError: t");
    class MyErr extends Error { constructor(m) { super(m); this.name = "MyErr"; } }
    const me = new MyErr("mine");
    ok("custom error", me instanceof MyErr && me instanceof Error && me.message === "mine" && String(me) === "MyErr: mine");
}

// ---- 10.1, 20.1 prototypes, this, property descriptors
{
    function Animal(name) { this.name = name; }
    Animal.prototype.speak = function () { return this.name + " speaks"; };
    const a = new Animal("cat");
    eq("ctor fn", a.speak(), "cat speaks");
    ok("instanceof fn", a instanceof Animal && !(a instanceof Array));
    eq("proto chain", Object.getPrototypeOf(a) === Animal.prototype, true);
    eq("constructor prop", a.constructor === Animal, true);
    const obj = {val: 1, get() { return this.val; }};
    const g = obj.get;
    throws("lost this", () => g(), TypeError);
    eq("call", g.call({val: 7}), 7);
    eq("apply", Math.max.apply(null, [1, 9, 2]), 9);
    const bound = g.bind({val: 3});
    eq("bind", bound(), 3);
    eq("bind args", ((a, b) => a - b).bind(null, 10)(3), 7);
    const arrowThis = {v: 5, f() { return [1].map(() => this.v)[0]; }};
    eq("arrow this", arrowThis.f(), 5);
    const frozen = Object.freeze({x: 1});
    throws("frozen set", () => { frozen.x = 2; }, TypeError);
    ok("isFrozen", Object.isFrozen(frozen));
    const d = {};
    Object.defineProperty(d, "hidden", {value: 42, enumerable: false});
    eq("defineProperty", d.hidden + Object.keys(d).length, 42);
    throws("readonly", () => { d.hidden = 1; }, TypeError);
    const desc = Object.getOwnPropertyDescriptor(d, "hidden");
    ok("descriptor", desc.value === 42 && !desc.writable && !desc.enumerable && !desc.configurable);
    let gs = 0;
    const acc = {};
    Object.defineProperty(acc, "p", {get() { return gs * 2; }, set(v) { gs = v; }, enumerable: true, configurable: true});
    acc.p = 21;
    eq("accessor", acc.p, 42);
    ok("hasOwn", acc.hasOwnProperty("p") && Object.hasOwn(acc, "p") && !acc.hasOwnProperty("toString"));
    ok("in", "p" in acc && "toString" in acc && !("nope" in acc));
    eq("delete", delete acc.p && !("p" in acc), true);
    const nul = Object.create(null);
    eq("null proto", Object.getPrototypeOf(nul), null);
    eq("assign", JSON.stringify(Object.assign({a: 1}, {b: 2}, null, {a: 3})), '{"a":3,"b":2}');
    eq("entries", JSON.stringify(Object.entries({x: 1, y: [2]})), '[["x",1],["y",[2]]]');
    eq("fromEntries", JSON.stringify(Object.fromEntries([["a", 1], ["b", 2]])), '{"a":1,"b":2}');
    const sealed = Object.seal({s: 1});
    sealed.s = 2;
    throws("sealed add", () => { sealed.t = 1; }, TypeError);
    eq("sealed", sealed.s, 2);
    eq("tostring tag", Object.prototype.toString.call([]), "[object Array]");
    eq("tostring null", Object.prototype.toString.call(null), "[object Null]");
}

// ---- 15.7 classes
{
    class Shape {
        static count = 0;
        sides = 0;
        constructor(name) { this.name = name; Shape.count++; }
        describe() { return `${this.name} has ${this.sides} sides`; }
        get label() { return "[" + this.name + "]"; }
        set label(v) { this.name = v; }
        static create(n) { return new this(n); }
        toString() { return "Shape " + this.name; }
    }
    class Square extends Shape {
        sides = 4;
        constructor(size) { super("square"); this.size = size; }
        area() { return this.size * this.size; }
        describe() { return super.describe() + " and area " + this.area(); }
        static create(n) { return super.create(n); }
    }
    const sq = new Square(3);
    eq("class method", sq.describe(), "square has 4 sides and area 9");
    eq("class getter", sq.label, "[square]");
    sq.label = "box";
    eq("class setter", sq.name, "box");
    eq("static field", Shape.count, 1);
    eq("static inherit", Square.create(2) instanceof Square, true);
    eq("class tostring", `${sq}`, "Shape box");
    ok("class instanceof", sq instanceof Square && sq instanceof Shape && sq instanceof Object);
    throws("class call", () => Shape("x"), TypeError);
    eq("class methods not enumerable", Object.keys(Shape.prototype).length, 0);
    class Base { constructor() { this.kind = new.target.name; } }
    class Derived extends Base {}
    eq("new.target", new Derived().kind, "Derived");
    throws("super not called", () => { class Bad extends Base { constructor() {} } new Bad(); }, ReferenceError);
    const Anon = class {};
    eq("anon class name", Anon.name, "Anon");
}

// ---- 27.5 generators and 27.1 iteration
{
    function* gen(n) { for (let i = 0; i < n; i++) yield i * 2; return "done"; }
    eq("gen spread", [...gen(4)].join(), "0,2,4,6");
    const it = gen(2);
    eq("gen next", JSON.stringify([it.next(), it.next(), it.next(), it.next()]),
       '[{"value":0,"done":false},{"value":2,"done":false},{"value":"done","done":true},{"done":true}]');
    function* echo() { let got = yield 1; while (true) got = yield got * 10; }
    const e = echo();
    e.next();
    eq("gen send", e.next(5).value + e.next(7).value, 120);
    function* withFinally() { try { yield 1; yield 2; } finally { genLog.push("cleanup"); } }
    var genLog = [];
    for (const v of withFinally()) { if (v === 1) break; }
    eq("gen return on break", genLog.join(), "cleanup");
    const g2 = withFinally();
    g2.next();
    eq("gen return()", JSON.stringify(g2.return(9)), '{"value":9,"done":true}');
    function* thrower() { try { yield 1; } catch (err) { yield "caught " + err; } }
    const g3 = thrower();
    g3.next();
    eq("gen throw()", g3.throw("boom").value, "caught boom");
    function* inner() { yield 1; yield 2; return 3; }
    function* outerGen() { const r = yield* inner(); yield r; }
    eq("yield*", [...outerGen()].join(), "1,2,3");
    const iterable = {[Symbol.iterator]() { let i = 0; return {next: () => ({value: i, done: i++ >= 3})}; }};
    eq("custom iterable", Array.from(iterable).join(), "0,1,2");
    const [first, second] = gen(10);
    eq("destructure generator", first + second, 2);
    const fib = function* () { let [a, b] = [0, 1]; for (;;) { yield a; [a, b] = [b, a + b]; } };
    const fibs = [];
    for (const f of fib()) { if (f > 50) break; fibs.push(f); }
    eq("infinite gen", fibs.join(), "0,1,1,2,3,5,8,13,21,34");
    const obj = {*items() { yield "a"; yield "b"; }};
    eq("gen method", [...obj.items()].join(""), "ab");
    eq("entries iter", [...["x", "y"].entries()].join("|"), "0,x|1,y");
}

// ---- 23.1 Array
{
    const a = [3, 1, 2];
    eq("push", a.push(4, 5), 5);
    eq("pop", a.pop(), 5);
    eq("shift", a.shift(), 3);
    eq("unshift", a.unshift(9), 4);
    eq("arr after", a.join(), "9,1,2,4");
    eq("slice", [1, 2, 3, 4].slice(1, -1).join(), "2,3");
    const sp = [1, 2, 3, 4, 5];
    eq("splice removed", sp.splice(1, 2, "a", "b", "c").join(), "2,3");
    eq("splice result", sp.join(), "1,a,b,c,4,5");
    eq("reduce", [1, 2, 3].reduce((s, v) => s + v, 10), 16);
    eq("reduceRight", ["a", "b", "c"].reduceRight((s, v) => s + v), "cba");
    eq("find", [1, 5, 10].find(v => v > 3), 5);
    eq("findIndex", [1, 5, 10].findIndex(v => v > 7), 2);
    eq("findLast", [1, 5, 10, 2].findLast(v => v > 3), 10);
    eq("indexOf", [1, 2, NaN].indexOf(NaN), -1);
    eq("includes", [1, 2, NaN].includes(NaN), true);
    eq("lastIndexOf", [1, 2, 1].lastIndexOf(1), 2);
    eq("some/every", [1, 2].some(v => v > 1) && ![1, 2].every(v => v > 1), true);
    eq("concat", [1].concat([2, [3]], 4).length, 4);
    eq("sort default", [10, 9, 1, 100].sort().join(), "1,10,100,9");
    eq("sort cmp", [10, 9, 1, 100].sort((x, y) => x - y).join(), "1,9,10,100");
    const stable = [{k: 1, v: "a"}, {k: 0, v: "b"}, {k: 1, v: "c"}, {k: 0, v: "d"}];
    eq("sort stable", stable.sort((x, y) => x.k - y.k).map(o => o.v).join(""), "bdac");
    eq("sort undefined last", [3, undefined, 1].sort().join(), "1,3,");
    eq("reverse", [1, 2, 3].reverse().join(), "3,2,1");
    eq("flat", [1, [2, [3, [4]]]].flat(2).join(), "1,2,3,4");
    eq("flatMap", [1, 2].flatMap(v => [v, v * 10]).join(), "1,10,2,20");
    eq("fill", new Array(3).fill(7).join(), "7,7,7");
    eq("Array.from len", Array.from({length: 3}, (_, i) => i * i).join(), "0,1,4");
    eq("Array.of", Array.of(7).length, 1);
    eq("isArray", Array.isArray([]) && !Array.isArray({}), true);
    eq("at", [1, 2, 3].at(-1), 3);
    eq("keys", [...["a", "b"].keys()].join(), "0,1");
    const big = [];
    big[100] = 1;
    eq("sparse length", big.length, 101);
    big.length = 2;
    eq("truncate", big.length + (big[100] === undefined ? 1 : 0), 3);
    const cyc = [1];
    cyc.push(cyc);
    eq("cyclic join", cyc.join(), "1,");
    eq("toSorted", [3, 1, 2].toSorted().join() + [1, 2].toReversed().join(), "1,2,32,1");
    eq("forEach", (() => { let s = 0; [1, 2, 3].forEach(v => s += v); return s; })(), 6);
    eq("copyWithin", [1, 2, 3, 4, 5].copyWithin(0, 3).join(), "4,5,3,4,5");
    throws("bad length", () => new Array(-1), RangeError);
    eq("array tostring", [1, [2, 3], null, undefined].toString(), "1,2,3,,");
}

// ---- 22.1 String
{
    const s = "Hello, World";
    eq("charAt", s.charAt(4) + s[0], "oH");
    eq("charCodeAt", s.charCodeAt(0), 72);
    eq("indexOf", s.indexOf("o") + s.indexOf("o", 5) + s.indexOf("zz"), 4 + 8 - 1);
    eq("lastIndexOf", s.lastIndexOf("o"), 8);
    eq("slice", s.slice(-5) + s.slice(0, 5), "WorldHello");
    eq("substring", s.substring(7, 0), "Hello, ");
    eq("substr", s.substr(-5, 3), "Wor");
    eq("upper/lower", "aBc".toUpperCase() + "AbC".toLowerCase(), "ABCabc");
    eq("trim", "  x  ".trim() + "|" + "  x".trimStart() + "|" + "x  ".trimEnd(), "x|x|x");
    eq("pad", "5".padStart(3, "0") + "5".padEnd(3, "ab"), "0055ab");
    eq("repeat", "ab".repeat(3), "ababab");
    eq("split", "a,b,,c".split(",").length, 4);
    eq("split limit", "a,b,c".split(",", 2).join("|"), "a|b");
    eq("split empty", "abc".split("").join("-"), "a-b-c");
    eq("replace", "aaa".replace("a", "b"), "baa");
    eq("replaceAll", "aaa".replaceAll("a", "b"), "bbb");
    eq("replace fn", "x1x".replace("1", m => "[" + m + "]"), "x[1]x");
    eq("replace pattern", "abc".replace("b", "$&$&"), "abbc");
    eq("startsWith", s.startsWith("Hell") && s.endsWith("ld") && s.includes(", "), true);
    eq("at", s.at(-1), "d");
    eq("concat", "a".concat(1, null), "a1null");
    eq("utf16 length", "😀".length, 2);
    eq("codePointAt", "😀".codePointAt(0), 128512);
    eq("fromCodePoint", String.fromCodePoint(128512) === "😀", true);
    eq("string iter", [..."a😀b"].length, 3);
    eq("fromCharCode", String.fromCharCode(72, 105), "Hi");
    eq("localeCompare", "a".localeCompare("b"), -1);
    eq("string compare", "abc" < "abd", true);
    eq("number fmt", (3.14159).toFixed(2) + " " + (0.5).toFixed(0) + " " + (2.5).toFixed(0) + " " + (1.005).toFixed(2), "3.14 1 3 1.00");
    eq("toPrecision", (123.456).toPrecision(4) + " " + (0.00001).toPrecision(1) + " " + (123456).toPrecision(2), "123.5 0.00001 1.2e+5");
    eq("toExponential", (12345).toExponential(2), "1.23e+4");
    eq("parseInt", parseInt("42px") + parseInt("ff", 16) + parseInt("0x10"), 42 + 255 + 16);
    eq("parseFloat", parseFloat("3.5e2abc"), 350);
    eq("Number()", Number("12") + Number(""), 12);
    eq("isInteger", Number.isInteger(5) && !Number.isInteger(5.5), true);
}

// ---- 21.3 Math
eq("math", Math.floor(-1.5) + Math.ceil(1.2) + Math.round(2.5) + Math.round(-2.5) + Math.abs(-3) + Math.trunc(-4.7), -2 + 2 + 3 - 2 + 3 - 4);
eq("math max empty", Math.max(), -Infinity);
eq("math min nan", Math.min(1, NaN), NaN);
eq("math sign", Math.sign(-3), -1);
eq("math hypot", Math.hypot(3, 4), 5);
ok("math random", (() => { for (let i = 0; i < 100; i++) { const r = Math.random(); if (r < 0 || r >= 1) return false; } return true; })());
eq("math pow", Math.pow(2, 10), 1024);
eq("imul", Math.imul(0xffffffff, 5), -5);
eq("clz32", Math.clz32(1), 31);

// ---- 25.5 JSON
{
    const v = {a: [1, "two", true, null], b: {c: 1.5, d: "q\"\n"}, u: undefined, f() {}};
    const text = JSON.stringify(v);
    eq("stringify", text, '{"a":[1,"two",true,null],"b":{"c":1.5,"d":"q\\"\\n"}}');
    eq("roundtrip", JSON.stringify(JSON.parse(text)), text);
    eq("stringify indent", JSON.stringify({a: [1]}, null, 2), '{\n  "a": [\n    1\n  ]\n}');
    eq("stringify replacer", JSON.stringify({a: 1, b: 2}, ["b"]), '{"b":2}');
    eq("stringify fn replacer", JSON.stringify({a: 1, b: 2}, (k, val) => typeof val === "number" ? val * 2 : val), '{"a":2,"b":4}');
    eq("toJSON", JSON.stringify({x: {toJSON() { return "X"; }}}), '{"x":"X"}');
    eq("parse reviver", JSON.parse('{"a":1,"b":[2]}', (k, val) => typeof val === "number" ? val + 1 : val).b[0], 3);
    eq("parse unicode", JSON.parse('"\\u0041\\n"'), "A\n");
    eq("stringify special", JSON.stringify([NaN, Infinity, undefined, () => 1]), "[null,null,null,null]");
    throws("parse bad", () => JSON.parse("{a:1}"), SyntaxError);
    throws("parse trailing", () => JSON.parse("[1,]"), SyntaxError);
    const cyc = {};
    cyc.self = cyc;
    throws("stringify cycle", () => JSON.stringify(cyc), TypeError);
    eq("stringify top", JSON.stringify("s") + JSON.stringify(1) + JSON.stringify(null), '"s"1null');
}

// ---- 24.1 / 24.2 Map and Set
{
    const m = new Map([["a", 1], [2, "b"]]);
    m.set(NaN, "nan").set(-0, "zero");
    eq("map get", m.get("a") + m.get(2) + m.get(NaN) + m.get(0), "1bnanzero");
    eq("map size", m.size, 4);
    eq("map delete", m.delete("a") && !m.has("a") && m.size === 3, true);
    eq("map order", [...m.keys()].map(String).join(), "2,NaN,0");
    const key = {};
    m.set(key, "obj");
    eq("map object key", m.get(key) + (m.get({}) === undefined), "objtrue");
    let seen = "";
    m.forEach((val, k) => { seen += typeof k; });
    eq("map forEach", seen, "numbernumbernumberobject");
    const s = new Set([1, 2, 2, 3, "3"]);
    eq("set size", s.size, 4);
    s.delete(2);
    eq("set values", [...s].join(), "1,3,3");
    const live = new Set([1, 2, 3]);
    const out = [];
    for (const v of live) { out.push(v); if (v === 1) { live.delete(2); live.add(4); } }
    eq("set live iteration", out.join(), "1,3,4");
    eq("map entries", JSON.stringify([...new Map([[1, 2]]).entries()]), "[[1,2]]");
    throws("map no new", () => Map(), TypeError);
}

// ---- 20.4 Symbols
{
    const s1 = Symbol("desc"), s2 = Symbol("desc");
    ok("symbol unique", s1 !== s2 && s1.toString() === "Symbol(desc)" && s1.description === "desc");
    ok("symbol for", Symbol.for("k") === Symbol.for("k") && Symbol.keyFor(Symbol.for("k")) === "k");
    const o = {[s1]: 1, normal: 2};
    eq("symbol keys hidden", Object.keys(o).length + Object.getOwnPropertySymbols(o).length, 2);
    throws("symbol to string", () => "" + s1, TypeError);
    const prim = {[Symbol.toPrimitive](hint) { return hint === "number" ? 42 : "str"; }};
    eq("toPrimitive", +prim + `${prim}`, "42str");
    eq("valueOf", ({valueOf() { return 5; }}) * 2, 10);
}

// ---- 27.2 Promise and 27.7 async functions (results are checked after the jobs run)
var asyncLog = [];
{
    Promise.resolve(1).then(v => asyncLog.push("then " + v));
    asyncLog.push("sync");
    new Promise((res, rej) => rej(new Error("no"))).catch(e => asyncLog.push("catch " + e.message));
    async function add(a, b) { await null; return a + b; }
    async function run() {
        const x = await add(1, 2);
        const [y, z] = await Promise.all([add(3, 4), 5]);
        try { await Promise.reject("bad"); } catch (e) { asyncLog.push("async caught " + e); }
        asyncLog.push("async " + (x + y + z));
        return "finished";
    }
    run().then(v => asyncLog.push(v));
    const arrowAsync = async () => { throw new TypeError("arrow"); };
    arrowAsync().catch(e => asyncLog.push("rejected " + e.name));
    Promise.allSettled([Promise.reject(1), 2]).then(r => asyncLog.push(r.map(x => x.status).join("/")));
    Promise.race([new Promise(() => {}), Promise.resolve("fast")]).then(v => asyncLog.push("race " + v));
    Promise.any([Promise.reject(1), Promise.resolve("any")]).then(v => asyncLog.push(v));
    eq("async order sync", asyncLog.join(), "sync");
}

// ---- 13.3.9 optional chaining
{
    const o = {a: {b: {c: 1}}, f() { return "f"; }, arr: [1]};
    eq("optchain", o?.a?.b?.c, 1);
    eq("optchain miss", o.x?.y.z, undefined);
    eq("optcall", o.f?.() + o.g?.(), "fundefined");
    eq("optindex", o.arr?.[0], 1);
    eq("optnull", null?.x, undefined);
    let count = 0;
    const nothing = null;
    nothing?.[count++];
    eq("optshort", count, 0);
    eq("logical assign", (() => { let a = null; a ??= 5; let b = 1; b &&= 2; let c = 0; c ||= 3; return a + b + c; })(), 10);
}

// ---- 7.3 getters on the prototype chain and instanceof with Symbol.hasInstance
{
    class Even { static [Symbol.hasInstance](n) { return n % 2 === 0; } }
    ok("hasInstance", 2 instanceof Even && !(3 instanceof Even));
    const base = {get twice() { return this.v * 2; }};
    const kid = Object.create(base);
    kid.v = 4;
    eq("proto getter receiver", kid.twice, 8);
}

// ---- more: iterator closing, arrays with holes and big indices, classes, numbers, strings, JSON,
// destructuring order, closures over catch and switch, errors, symbols, bound constructors (sections as above)
// labelled continue through for-of closes nothing and keeps going
{ let s = ""; outer: for (const a of [1, 2, 3]) { for (const b of "xy") { if (b === "y") continue outer; s += a + b; } } eq("labelled for-of", s, "1x2x3x"); }
// break out of nested for-of closes both iterators
{ const log = []; function* g(n) { try { yield 1; yield 2; } finally { log.push(n); } }
  out: for (const a of g("a")) { for (const b of g("b")) { break out; } } eq("nested close", log.join(), "b,a"); }
// return from a for-of inside a function closes the iterator
{ const log = []; function* g() { try { yield 1; } finally { log.push("closed"); } } function f() { for (const x of g()) return x; } eq("return closes", f() + log.join(), "1closed"); }
// arrays: holes, for-in over arrays, length non-writable
{ const a = [1, , 3]; const keys = []; for (const k in a) keys.push(k); eq("forin array", keys.join(), "0,2");
  const b = [1, 2, 3]; Object.defineProperty(b, "length", {writable: false}); throws("fixed length push", () => b.push(4), TypeError); eq("fixed length", b.length, 3);
  const c = []; c[5] = 1; eq("hole count", c.filter(() => true).length, 1); eq("hole join", c.join("-"), "-----1");
  eq("array index string", [10, 20]["1"], 20); const d = [1, 2, 3]; delete d[1]; eq("delete hole", 1 in d, false); eq("delete length", d.length, 3);
  const big = []; big[2 ** 31] = "x"; eq("big index length", big.length, 2 ** 31 + 1); eq("big index read", big[2 ** 31], "x");
  big.length = 0; eq("big truncate", big[2 ** 31], undefined); }
// classes: static inheritance, accessors, symbols as method names, generator methods, super in object literals
{ class A { static base = "A"; static who() { return this.base; } *[Symbol.iterator]() { yield 1; yield 2; } }
  class B extends A { static base = "B"; }
  eq("static this", B.who(), "B"); eq("class iter", [...new B()].join(), "1,2");
  const proto = { hi() { return "proto"; } }; const obj = { __hi() { return super.hi() + "!"; } }; Object.setPrototypeOf(obj, proto);
  eq("literal super", obj.__hi(), "proto!"); }
// getters / setters on classes, Object.entries on class instances, instanceof on primitives
{ class P { constructor() { this._v = 1; } get v() { return this._v; } set v(x) { this._v = x * 2; } static get s() { return "s"; } }
  const p = new P(); p.v = 5; eq("accessor", p.v, 10); eq("static getter", P.s, "s"); eq("entries", JSON.stringify(Object.entries(p)), '[["_v",10]]');
  eq("prim instanceof", 1 instanceof Number, false); }
// async ordering and errors
var order = [];
(async function () { order.push("a1"); await undefined; order.push("a2"); await Promise.resolve(); order.push("a3"); })();
Promise.resolve().then(() => order.push("p1")).then(() => order.push("p2"));
order.push("sync");
// numbers
eq("toString frac", (0.1).toString(3).slice(0, 6), "0.0022");
eq("toFixed neg", (-1.005).toFixed(2), "-1.00");
eq("toFixed big", (123.456).toFixed(10), "123.4560000000");
eq("toPrecision e", (1e-7).toPrecision(2), "1.0e-7");
eq("exp small", (0).toExponential(2), "0.00e+0");
eq("number to string", String(-1e-7) + String(1e100) + String(2 ** -1074), "-1e-71e+1005e-324");
eq("parse numbers", Number("  0x1f  ") + Number("1e-2") * 100 + Number(".5") * 2, 33);
eq("int conversions", (2 ** 32 + 5) | 0, 5);
eq("uint", (-1) >>> 0, 4294967295);
eq("mod neg zero", Object.is(-4 % 2, -0), true);
eq("round", Math.round(0.49999999999999994) + Math.round(-0.5000000000000001), -1);
// strings
eq("unicode upper", "straße".toUpperCase(), "STRASSE");
eq("greek", "ΑΒΓ".toLowerCase(), "αβγ");
eq("split sep", "a, b, c".split(", ").length, 3);
eq("surrogate slice", "😀x".slice(2), "x");
eq("replace $", "price".replace("price", "$$5"), "$5");
eq("template escapes", `A\x42\n`.length, 3);
eq("raw newline", `a
b`.charCodeAt(1), 10);
// JSON
eq("json nested", JSON.stringify({a: {b: {c: [1, {d: 2}]}}}), '{"a":{"b":{"c":[1,{"d":2}]}}}');
eq("json unicode", JSON.stringify(" \ud800"), '" \\ud800"');
eq("json numbers", JSON.parse("[1e2, -0.5, 0]").join(), "100,-0.5,0");
eq("json keys order", JSON.stringify(JSON.parse('{"b":1,"a":2,"1":3}')), '{"1":3,"b":1,"a":2}');
// destructuring and default evaluation order
{ let log = []; const f = (a = log.push("a"), b = log.push("b")) => a + b; f(); eq("default order", log.join(), "a,b");
  const {x = 1, y = x + 1} = {}; eq("default refers earlier", y, 2);
  const [first = "d", ...[second, third]] = [undefined, 2, 3]; eq("nested rest", first + second + third, "d23"); }
// getter on the prototype chain sees the receiver; setters inherited
{ const base = { set v(x) { this._v = x; } }; const o = Object.create(base); o.v = 3; eq("inherited setter", o._v + Object.keys(o).join(), "3_v"); }
// closures over catch parameters and switch cases
{ const fs = []; for (let i = 0; i < 2; i++) { try { throw i; } catch (e) { fs.push(() => e); } } eq("catch closure", fs[0]() + fs[1](), 1);
  function sw(x) { switch (x) { case 1: { let y = "one"; return () => y; } default: return () => "other"; } } eq("switch closure", sw(1)(), "one"); }
// typeof on TDZ and const in for-of
throws("typeof tdz", () => { typeof q; let q; }, ReferenceError);
{ let total = 0; for (const [k, v] of Object.entries({a: 1, b: 2})) total += v; eq("const destructure loop", total, 3); }
// error subclass name and stack, cause
{ class AppError extends Error { get name() { return "AppError"; } } const e = new AppError("boom", {cause: 1}); eq("error subclass", `${e}`, "AppError: boom"); eq("cause", e.cause, 1); }
// symbol keys survive Object.assign and spread
{ const s = Symbol("k"); const o = {[s]: 1}; eq("symbol assign", Object.assign({}, o)[s] + {...o}[s], 2); }
// Map with object keys and iteration after clear
{ const m = new Map([[{}, 1]]); const it = m.keys(); m.clear(); eq("map clear iter", it.next().done, true); }
// Function.prototype.bind with new
{ function P(a, b) { this.s = a + b; } const B = P.bind(null, 1); eq("bound new", new B(2).s, 3); eq("bound name", B.name, "bound P"); }

// ---- 22.2 regular expressions: syntax, flags, groups, lookahead, backreferences, exec / lastIndex, and the
// String methods that take a RegExp
eq("test", /ab+c/.test("xabbbcx"), true);
eq("test2", /^ab+c$/.test("xabbbcx"), false);
eq("exec groups", /(\d+)-(\d+)/.exec("tel 12-345").slice(1).join(), "12,345");
eq("exec index", /\d/.exec("ab3").index, 2);
eq("named", /(?<y>\d{4})-(?<m>\d\d)/.exec("2026-11").groups.m, "11");
eq("icase", /HELLO/i.test("say hello"), true);
eq("class", /[a-c]+/.exec("xxbcaz")[0], "bca");
eq("neg class", /[^0-9]+/.exec("12ab34")[0], "ab");
eq("escapes", /\w+\s\w+/.exec("  foo bar  ")[0], "foo bar");
eq("dot", /a.c/.test("a\nc"), false);
eq("dotall", /a.c/s.test("a\nc"), true);
eq("multiline", "a\nb".match(/^b$/m)[0], "b");
eq("lazy", /<.+?>/.exec("<a><b>")[0], "<a>");
eq("greedy", /<.+>/.exec("<a><b>")[0], "<a><b>");
eq("counted", /^a{2,3}$/.test("aaaa"), false);
eq("counted2", /^a{2,}$/.test("aaaa"), true);
eq("alt", /cat|dog/.exec("hotdog")[0], "dog");
eq("backref", /(\w)\1/.exec("abccd")[0], "cc");
eq("lookahead", /\d+(?=px)/.exec("10em 20px")[0], "20");
eq("neg lookahead", /\d+(?![\dp])/.exec("20px 30em")[0], "30");
eq("word boundary", "the cat sat".match(/\bc\w+/)[0], "cat");
eq("global match", "a1b22c333".match(/\d+/g).join(), "1,22,333");
eq("no match", "abc".match(/\d/), null);
eq("replace global", "a-b-c".replace(/-/g, "+"), "a+b+c");
eq("replace groups", "John Smith".replace(/(\w+)\s(\w+)/, "$2, $1"), "Smith, John");
eq("replace named", "2026-11".replace(/(?<y>\d+)-(?<m>\d+)/, "$<m>/$<y>"), "11/2026");
eq("replace fn", "a1b2".replace(/\d/g, d => d * 2), "a2b4");
eq("replaceAll rx", "aXbX".replaceAll(/x/gi, "_"), "a_b_");
throws("replaceAll needs g", () => "a".replaceAll(/a/, "b"), TypeError);
eq("split rx", "a1b22c".split(/\d+/).join("|"), "a|b|c");
eq("split captures", "a1b".split(/(\d)/).join("|"), "a|1|b");
eq("split empty rx", "abc".split(/(?:)/).join("|"), "a|b|c");
eq("search", "hello world".search(/o\sw/), 4);
eq("matchAll", [..."a1b2".matchAll(/[a-z](\d)/g)].map(m => m[1]).join(), "1,2");
eq("lastIndex", (() => { const r = /a/g; r.test("aa"); return r.lastIndex; })(), 1);
eq("sticky", /b/y.test("ab"), false);
eq("toString", String(/a\/b/gi), "/a\\/b/gi");
eq("source", new RegExp("x+", "g").source + new RegExp("x+", "g").flags, "x+g");
eq("ctor copy", new RegExp(/ab/g).global, true);
eq("unicode", /^.$/u.test("😀"), true);
eq("non unicode", /^.$/.test("😀"), false);
eq("unicode escape", /\u{1F600}/u.test("😀"), true);
eq("empty loop", /(a*)*b/.test("aaab"), true);
eq("empty alt", /(|a)+b/.test("ab"), true);
eq("division still works", (10 / 2) / 5, 1);
let x = 6, y = 2, g = 3; eq("division names", x / y / g, 1);
eq("regex after return", (function () { return /z/.test("z"); })(), true);
eq("in array", [/a/, /b/].length, 2);
eq("class with slash", /[/]/.test("/"), true);
eq("case-insensitive class", /[A-Z]/i.test("q"), true);
eq("digits shorthand", /^\D+$/.test("abc"), true);
throws("bad regex", () => new RegExp("("), SyntaxError);
throws("bad flags", () => new RegExp("a", "zz"), SyntaxError);
throws("lookbehind", () => new RegExp("(?<=a)b"), SyntaxError);
eq("json-ish", /^\s*\{.*\}\s*$/.test(' {"a":1} '), true);
eq("email-ish", /^[\w.+-]+@[\w-]+\.[\w.]+$/.test("a.b+c@d-e.fg"), true);

// ---- eval and Function are not available (sandbox)
throws("no eval", () => eval("1"), EvalError);
throws("no Function", () => Function("return 1"), EvalError);

console.log("conformance checks " + checks + ", failures " + fails);

// called by the test after the promise jobs ran
function afterJobs() {
    eq("async order (one job per await, 27.7.5.3)", order.join(), "a1,sync,a2,p1,a3,p2");
    const log = asyncLog.join("|");
    console.log("async log: " + log);
    const want = ["sync", "then 1", "catch no", "async caught bad", "async 15", "finished", "rejected TypeError",
                  "rejected/fulfilled", "race fast", "any"];
    for (const w of want) ok("async has " + w, asyncLog.includes(w));
    ok("async sync first", asyncLog[0] === "sync");
    ok("async order", asyncLog.indexOf("async 15") < asyncLog.indexOf("finished"));
    eq("async count", asyncLog.length, want.length);
    console.log("async checks done, failures " + fails);
}
