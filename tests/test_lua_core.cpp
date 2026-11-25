// okay::lua (our own Lua 5.4 interpreter): conformance, hostile input and fuzz.
//
// Standalone (only engine/src/Lua/*.cpp), so it also builds under sanitizers:
//   g++ -std=c++17 -fsanitize=address,undefined -Iengine/include -Itests \
//       -DOKAY_SOURCE_DIR='"."' tests/test_lua_core.cpp engine/src/Lua/*.cpp
// OKAY_LUA_FUZZ_ITERS=20000 runs a longer fuzz. The conformance script is
// tests/lua/conformance.lua (checks written from the reference manual).
#include "test_framework.hpp"
#include "okay/Lua/Lua.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace okay::lua;

#ifndef OKAY_SOURCE_DIR
#define OKAY_SOURCE_DIR "."
#endif

namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Run a chunk; returns everything it printed (one line per print) or "ERROR: msg".
std::string Run(const std::string& src, bool stress = false, std::uint64_t budget = 50'000'000) {
    State L;
    L.OpenLibs();
    L.gcStress = stress;
    L.SetBudget(budget);
    std::string out;
    L.printSink = [&](const std::string& s) { out += s + "\n"; };
    std::string err;
    if (!L.DoString(src, "t.lua", &err)) out += "ERROR: " + err + "\n";
    return out;
}

void Expect(const std::string& src, const std::string& want) {
    std::string got = Run(src);
    if (got != want + "\n") std::cerr << "  for: " << src << "\n  got: " << got << "  want: " << want << "\n";
    CHECK(got == want + "\n");
}

void ConformanceScript(bool stress) {
    std::string src = ReadFile(std::string(OKAY_SOURCE_DIR) + "/tests/lua/conformance.lua");
    CHECK(!src.empty());
    std::string out = Run(src, stress);
    if (out.find("FAIL") != std::string::npos || out.find("conformance done") == std::string::npos)
        std::cerr << out;
    CHECK(out.find("FAIL") == std::string::npos);
    CHECK(out.find("conformance done") != std::string::npos);
}

void Values() {
    // printing numbers: integer vs float subtypes (manual 2.1, 3.4.3)
    Expect("print(1, 1.0, -0.0, 10/2, 3//2, 3.0//2, 2^2, 1e15, 1e16, 0.1)",
           "1\t1.0\t-0.0\t5.0\t1\t1.0\t4.0\t1e+15\t1e+16\t0.1");
    Expect("print(math.maxinteger, math.mininteger, math.maxinteger + 1 == math.mininteger)",
           "9223372036854775807\t-9223372036854775808\ttrue");
    Expect("print(7 % -3, -7 % 3, 7.5 % 2, -7.5 % 2, 5 // 0.0, -5 // 0.0)", "-2\t2\t1.5\t0.5\tinf\t-inf");
    Expect("print(1 < 1.1, 2^63 == math.maxinteger + 1.0, math.maxinteger < 2^63, 3 == 3.0)", "true\ttrue\ttrue\ttrue");
    Expect("print('10' + 5, '0x10' * 1, '1e1' + 0, 10 .. 20)", "15\t16\t10.0\t1020");
    Expect("print(#'abc', #{1, 2, 3}, #{n = 1})", "3\t3\t0");
    Expect("print(type(nil), type(print), type({}), type('x'), type(2), type(coroutine.create(print)))",
           "nil\tfunction\ttable\tstring\tnumber\tthread");
    Expect("print(tostring(true), tonumber('  0x1F  '), tonumber('10', 2), tonumber('ff', 16), tonumber('8', 8))",
           "true\t31\t2\t255\tnil");
    Expect("print(string.format('%5.1f|%-3d|%3s|%x|%g', 2.25, 7, 'ab', 255, 1e300))", "  2.2|7  | ab|ff|1e+300");
}

void Closures() {
    Expect("local t = {} for i = 1, 3 do t[i] = function() return i end end print(t[1](), t[2](), t[3]())",
           "1\t2\t3");
    Expect("local function f() local n = 0 return function() n = n + 1 return n end end "
           "local a, b = f(), f() a() a() print(a(), b())",
           "3\t1");
    Expect("local x = 1 local function g() return x end x = 5 print(g())", "5");
    Expect("local function v(...) return select('#', ...), ... end print(v(nil, nil))", "2\tnil\tnil");
    Expect("print((function(...) local a, b = ... return b end)(1, 2, 3))", "2");
}

void Coroutines() {
    Expect("local co = coroutine.create(function(a) local b = coroutine.yield(a * 2) return b + 1 end) "
           "print(coroutine.resume(co, 5)) print(coroutine.resume(co, 10)) print(coroutine.status(co))",
           "true\t10\ntrue\t11\ndead");
    Expect("local g = coroutine.wrap(function() for i = 1, 3 do coroutine.yield(i) end end) "
           "local s = 0 for v in g do s = s + v end print(s)",
           "6");
    // a producer / consumer pair
    Expect("local p = coroutine.wrap(function() for _, w in ipairs({'a', 'b', 'c'}) do coroutine.yield(w) end end) "
           "local r = {} for w in p do r[#r + 1] = w end print(table.concat(r, ','))",
           "a,b,c");
    // yielding inside a Lua __index metamethod works (it runs inside the interpreter loop)
    Expect("local co = coroutine.create(function() local t = setmetatable({}, {__index = function(_, k) return coroutine.yield(k) end}) return t.x end) "
           "print(coroutine.resume(co)) print(coroutine.resume(co, 5))",
           "true\tx\ntrue\t5");
    // yielding inside a metamethod a native calls (tostring -> __tostring) is still reported
    Expect("local co = coroutine.create(function() return tostring(setmetatable({}, {__tostring = function() coroutine.yield() end})) end) "
           "local ok, e = coroutine.resume(co) print(ok, e:find('C%-call boundary') ~= nil)",
           "false\ttrue");
}

void Errors() {
    Expect("print(pcall(function() local t = nil; t.x = 1 end))", "false\tt.lua:1: attempt to index a nil value (local 't')");
    Expect("print(pcall(function() foo.bar() end))", "false\tt.lua:1: attempt to index a nil value (global 'foo')");
    Expect("print(pcall(function() local t = {} t.go() end))", "false\tt.lua:1: attempt to call a nil value (field 'go')");
    Expect("print(pcall(function() return 1 + {} end))", "false\tt.lua:1: attempt to perform arithmetic on a table value");
    Expect("print(pcall(error, 'msg', 0))", "false\tmsg");
    Expect("\n\nerror('third line')", "ERROR: t.lua:3: third line");
    Expect("print(select(2, pcall(string.rep)))", "bad argument #1 to 'rep' (string expected, got no value)");
    Expect("print(select(2, pcall(setmetatable, 1, {})))", "bad argument #1 to 'setmetatable' (table expected, got number)");
    // syntax errors carry line and column
    State L;
    std::vector<SyntaxError> e = L.CheckSyntax("local a = 1\nlocal b = = 2\n", "s.lua");
    CHECK(e.size() == 1);
    if (!e.empty()) {
        CHECK(e[0].line == 2);
        CHECK(e[0].column == 11);
        CHECK(e[0].message == "s.lua:2: unexpected symbol near '='");
    }
    // several errors reported at once (resync at the next statement)
    e = L.CheckSyntax("x = = 1\nlocal y = 2\nfunction f( end\nz = 3\n", "s.lua");
    CHECK(e.size() == 2);
    if (e.size() == 2) { CHECK(e[0].line == 1); CHECK(e[1].line == 3); }
    CHECK(L.CheckSyntax("return 1", "s.lua").empty());
    e = L.CheckSyntax("x = 'abc", "s.lua");
    CHECK(e.size() == 1 && e[0].bare.find("unfinished string") != std::string::npos);
}

void BudgetAndLimits() {
    // an endless loop stops (the budget is not catchable by pcall)
    std::string out = Run("while true do end", false, 100000);
    CHECK(out.find("instruction budget exceeded") != std::string::npos);
    out = Run("print(pcall(function() while true do end end))", false, 100000);
    CHECK(out.find("instruction budget exceeded") != std::string::npos);
    CHECK(out.find("true") == std::string::npos && out.find("false") == std::string::npos);
    out = Run("local co = coroutine.wrap(function() while true do end end) co()", false, 100000);
    CHECK(out.find("instruction budget exceeded") != std::string::npos);
    // a catastrophic pattern costs budget too
    out = Run("string.find(string.rep('a', 40), string.rep('a*', 40) .. 'b')", false, 2'000'000);
    CHECK(out.find("instruction budget exceeded") != std::string::npos);
    // the state stays usable after a fatal stop
    {
        State L;
        L.OpenLibs();
        L.SetBudget(10000);
        std::string err;
        CHECK(!L.DoString("local co = coroutine.wrap(function() local t <close> = setmetatable({}, {__close = function() end}) while true do end end) co()", "a", &err));
        L.SetBudget(10000);
        std::string printed;
        L.printSink = [&](const std::string& s) { printed += s; };
        CHECK(L.DoString("print(1 + 1)", "b", &err));
        CHECK(printed == "2");
    }
    out = Run("local function f() return f() + 1 end f()");
    CHECK(out.find("stack overflow") != std::string::npos);
    out = Run("local function f() return pcall(f) end f() print('ok')");
    CHECK(out.find("ok") != std::string::npos);
    {
        State L;
        L.OpenLibs();
        L.memoryLimit = 16u << 20;
        std::string err;
        CHECK(!L.DoString("local t = {} for i = 1, 1e9 do t[i] = tostring(i) .. 'xxxxxxxxxxxxxxxx' end", "m", &err));
        CHECK(err.find("not enough memory") != std::string::npos);
        L.memoryLimit = 256u << 20;
        L.CollectGarbage();
        CHECK(L.MemoryInUse() < (16u << 20));
    }
    out = Run("print(pcall(string.rep, 'x', 1 << 40))");
    CHECK(out.find("longer than") != std::string::npos);
    out = Run("print(pcall(string.format, '%999d', 1))");
    CHECK(out.find("is not supported") != std::string::npos);
    out = Run("print(pcall(load, string.rep('(', 1000) .. '1' .. string.rep(')', 1000)))");
    CHECK(out.find("ERROR") == std::string::npos);
    out = Run("local t = setmetatable({}, {}) getmetatable(t).__index = t print(pcall(function() return t.x end))");
    CHECK(out.find("false") != std::string::npos);
}

void HostApi() {
    State L;
    L.OpenLibs();
    std::string err;
    CHECK(L.DoString("function add(a, b) return a + b end counter = 0", "h", &err));
    L.Push(L.GetGlobal("add"));
    L.Push(Value::Int(2));
    L.Push(Value::Num(0.5));
    CHECK(L.PCallHost(2, 1, &err));
    Value r = L.Pop();
    CHECK(r.t == Type::Float && r.n == 2.5);
    int ref = L.Ref(Value::Obj(L.NewTable()));
    L.CollectGarbage();
    CHECK(L.GetRef(ref).IsTable());
    L.Unref(ref);
    // a native function
    L.SetGlobal("twice", Value::Obj(L.NewNative([](State& S, int) {
        S.Push(Value::Int(S.CheckInt(0, "twice") * 2));
        return 1;
    }, "twice")));
    std::string printed;
    L.printSink = [&](const std::string& s) { printed += s; };
    CHECK(L.DoString("print(twice(21))", "h2", &err));
    CHECK(printed == "42");
    CHECK(!L.DoString("twice('x')", "h3", &err));
    CHECK(err == "h3:1: bad argument #1 to 'twice' (number expected, got string)");
}

// Whole Lua programs (tests/lua/programs/*.lua) whose printed output must equal the matching
// .expected file line for line: classes and closures, text processing with patterns, coroutines,
// errors with chunk:line messages, numbers / tables / utf8 / goto / os.
void Programs(bool stress) {
    for (const char* name : {"classes", "text", "coroutines", "errors", "data"}) {
        // under gcStress (a collection at every safe point) "errors" would collect at each of its
        // 200 000 overflow frames; the other four still run that way
        if (stress && std::string(name) == "errors") continue;
        const std::string dir = std::string(OKAY_SOURCE_DIR) + "/tests/lua/programs/";
        const std::string src = ReadFile(dir + name + ".lua"), want = ReadFile(dir + name + ".expected");
        CHECK(!src.empty() && !want.empty());
        State L;
        L.OpenLibs();
        L.gcStress = stress;
        L.SetBudget(50'000'000);
        std::string got, err;
        L.printSink = [&](const std::string& s) { got += s + "\n"; };
        if (!L.DoString(src, std::string(name) + ".lua", &err)) got += "ERROR: " + err + "\n";
        L.Close();
        if (got != want) {
            std::istringstream g(got), w(want);
            std::string gl, wl;
            for (int line = 1; std::getline(w, wl); ++line) {
                if (!std::getline(g, gl)) gl = "<missing>";
                if (gl != wl) { std::cerr << "  " << name << " line " << line << ": got  " << gl << "\n    want " << wl << "\n"; break; }
            }
        }
        CHECK(got == want);
    }
}

// State::Close runs the finalizers still pending, newest first (manual 2.5.3, lua_close).
void CloseFinalizers() {
    std::string printed;
    {
        State L;
        L.OpenLibs();
        L.printSink = [&](const std::string& s) { printed += s + ";"; };
        std::string err;
        CHECK(L.DoString("a = setmetatable({}, {__gc = function() print('a') end})\n"
                         "b = setmetatable({}, {__gc = function() print('b') error('late') end})\n"
                         "setmetatable({}, {__gc = function() print('c') end})", "fin", &err));
        L.Close();
        L.Close();   // twice: nothing runs again
    }
    CHECK(printed == "c;b;warning: error in __gc metamethod (fin:2: late);a;" ||
          printed == "b;warning: error in __gc metamethod (fin:2: late);a;c;");
    if (printed.find("a;") == std::string::npos) std::cerr << "  close finalizers printed: " << printed << "\n";
}

// Mutation fuzz: valid snippets with bytes flipped, inserted and dropped, plus token soup.
void Fuzz() {
    const char* corpus[] = {
        "local t = {1, 2, x = 3} for k, v in pairs(t) do t[k] = v * 2 end return #t",
        "local function f(a, ...) return select('#', ...), a end print(f(1, 2, 3))",
        "local s = ('abc'):rep(3) return s:gsub('%w', {a = 'z'}):find('z+', 2)",
        "goto l do local x = 1 end ::l:: local co = coroutine.wrap(function() coroutine.yield(1) end) return co()",
        "local mt = {__index = function(t, k) return k end, __add = function() return 1 end} return setmetatable({}, mt) + 1",
        "local a <const> = 5 local b <close> = nil repeat a = a until true return a // 2, a % 2, a ^ 2, 1 << a",
        "return string.format('%q %5.2f %d', 'x\\n', 1.5, 3), [[long\nstring]], 0x1p4, 1e-3",
        "local x = 0 while x < 10 do x = x + 1 if x % 2 == 0 then goto c end ::c:: end return x",
        "return pcall(error, {}), xpcall(function() error('e') end, function(m) return m end)",
        "local t = {} for i = 10, 1, -1 do t[i] = i end table.sort(t, function(a, b) return a > b end) return table.concat(t, ',')",
        // the loop-frame paths: method lookup, concat chains, __close on exits, ephemerons, string.pack
        "local o = setmetatable({}, {__index = function(t, k) coroutine.yield(k) return function(s, x) return x end end}) "
        "local c = coroutine.wrap(function() return o:m(1), 'a' .. 'b' .. setmetatable({}, {__concat = function() return 'c' end}) end) return c(), c()",
        "local co = coroutine.wrap(function() for i = 1, 3 do local x <close> = setmetatable({}, {__close = function() coroutine.yield(i) end}) "
        "if i == 2 then break end end return 'r' end) return co(), co(), co()",
        "local e = setmetatable({}, {__mode = 'k'}) for i = 1, 20 do local k = {} e[k] = {k} end collectgarbage() "
        "return string.unpack('<i4 z', string.pack('<i4 z', 7, 'hi'))",
    };
    const char* tokens[] = {"local", "function", "end", "(", ")", "{", "}", "[", "]", "=", "==", "..", "...", "x", "y",
                            "1", "2.5", "'s'", "[[l]]", "if", "then", "else", "for", "in", "do", "while", "return",
                            "goto", "::", "::l", "<close>", "<const>", ",", ";", ":", ".", "#", "-", "~", "//", "^",
                            "nil", "true", "not", "and", "or", "repeat", "until", "break", "\n", "--", "--[[", "]]"};
    int iters = 3000;
    if (const char* e = std::getenv("OKAY_LUA_FUZZ_ITERS")) iters = std::max(1, std::atoi(e));
    std::mt19937 rng(12345);
    int ran = 0, okRuns = 0;
    for (int it = 0; it < iters; ++it) {
        std::string src;
        if (it % 4 == 3) {
            int n = (int)(rng() % 40);
            for (int k = 0; k < n; ++k) { src += tokens[rng() % (sizeof tokens / sizeof tokens[0])]; src += ' '; }
        } else {
            src = corpus[rng() % (sizeof corpus / sizeof corpus[0])];
            int muts = 1 + (int)(rng() % 4);
            for (int m = 0; m < muts && !src.empty(); ++m) {
                std::size_t pos = rng() % src.size();
                switch (rng() % 4) {
                    case 0: src[pos] = (char)(rng() % 256); break;
                    case 1: src.insert(pos, 1, (char)(32 + rng() % 95)); break;
                    case 2: src.erase(pos, 1 + rng() % 3); break;
                    default: src.insert(pos, tokens[rng() % (sizeof tokens / sizeof tokens[0])]); break;
                }
            }
        }
        State L;
        L.OpenLibs();
        L.SetBudget(200000);
        L.memoryLimit = 32u << 20;
        L.gcStress = (it % 16) == 0;
        L.printSink = [](const std::string&) {};
        std::string err;
        if (L.DoString(src, "fuzz", &err)) ++okRuns;
        ++ran;
    }
    CHECK(ran == iters);
    std::cout << "  fuzz: " << ran << " inputs, " << okRuns << " ran without error\n";
}

// Patterns (manual 6.4.1) and the string functions that use them (6.4), written from the
// manual's text and its examples: anchors, %b, %f, captures, position captures, back
// references, the repeat marks * + - ? and the character classes and sets.
void Patterns() {
    // the manual's own examples (string.find, string.gsub, string.gmatch, 6.4.1)
    Expect("print(string.find('hello world', 'o w'))", "5\t7");
    Expect("print(string.gsub('hello world', '(%w+)', '%1 %1'))", "hello hello world world\t2");
    Expect("print(string.gsub('hello world', '%w+', '%0 %0', 1))", "hello hello world\t1");
    Expect("print(string.gsub('hello world from Lua', '(%w+)%s*(%w+)', '%2 %1'))", "world hello Lua from\t2");
    Expect("print(string.gsub('4+5 = $return 4+5$', '%$(.-)%$', function(s) return load(s)() end))", "4+5 = 9\t1");
    Expect("local t = {name = 'lua', version = '5.4'} print(string.gsub('$name-$version.tar.gz', '%$(%w+)', t))",
           "lua-5.4.tar.gz\t2");
    Expect("local r = {} for w in string.gmatch('hello world from Lua', '%a+') do r[#r + 1] = w end print(table.concat(r, '|'))",
           "hello|world|from|Lua");
    Expect("local r = {} for k, v in string.gmatch('from=world, to=Lua', '(%w+)=(%w+)') do r[#r + 1] = k .. ':' .. v end "
           "print(table.concat(r, ' '))", "from:world to:Lua");
    Expect("print(string.find('flaaap', '()aa()'))", "3\t4\t3\t5");
    Expect("print(string.match(' xay  z', '(a*(.)%w(%s*))'))", " x\t \t");
    Expect("print(string.match('aaxy  z', '(a*(.)%w(%s*))'))", "aaxy  \tx\t  ");
    // anchors: '^' only at the start (and never in gmatch), '$' only at the end
    Expect("print(('hello'):find('^h'), ('hello'):find('^e'), ('hello'):find('o$'))", "1\tnil\t5\t5");
    Expect("print(('a$b'):find('$b'), ('a^b'):find('a^'), ('x'):match('^$'), (''):match('^$'))", "2\t1\tnil\t");
    Expect("local n = 0 for _ in ('^a^a'):gmatch('^a') do n = n + 1 end print(n)", "2");
    Expect("print(('aaa'):gsub('^a', 'b'))", "baa\t1");
    // repeats: greedy *, +, lazy -, optional ?
    Expect("print(('<a><b>'):match('<(.-)>'), ('<a><b>'):match('<(.*)>'))", "a\ta><b");
    Expect("print(('color colour colouur'):gsub('colou?r', 'X'))", "X X colouur\t2");
    Expect("print(('aaab'):match('a+'), ('b'):match('a+'), ('b'):match('a*'), ('aaab'):match('a-b'))", "aaa\tnil\t\taaab");
    Expect("print(('abc'):gsub('x*', '-'))", "-a-b-c-\t4");
    Expect("print(('  trim me  '):match('^%s*(.-)%s*$') .. '|')", "trim me|");
    // classes and their complements
    Expect("print(('a1 B_\\t.'):gsub('%a', ''), ('a1 B_\\t.'):gsub('%A', ''))", "1 _\t.\taB\t5");
    Expect("print(('a1 B_.'):gsub('%d', '#'), ('x9F g'):gsub('%x', '#'), ('aB'):gsub('%l', 'l'), ('aB'):gsub('%u', 'u'))",
           "a# B_.\tx## g\tlB\tau\t1");
    Expect("print(('a,b.c!'):gsub('%p', ''), ('a b\\tc'):gsub('%s', ''), ('a_1 ?'):gsub('%w', ''), ('a\\1b'):gsub('%c', ''))",
           "abc\tabc\t_ ?\tab\t1");
    Expect("print(('a b'):gsub('%g', '*'), ('1.5%'):gsub('%.', ','), ('50%'):gsub('%%', ' percent'))",
           "* *\t1,5%\t50 percent\t1");
    // sets: ranges, classes inside, complement, ']' first, '-' at the end
    Expect("print(('x_1 y2'):match('[%a_][%w_]*'), ('017389'):match('[0-7]+'), ('a]b'):match('[]]'), ('a-b'):match('[b-]+'))",
           "x_1\t0173\t]\t-b");
    Expect("print(('hello world'):match('[^%s]+'), ('abc123'):match('[^%a]+'), ('a^b'):match('[%^]'))", "hello\t123\t^");
    // %b balanced, including the same character twice
    Expect("print(('x(a(b)c)y'):match('%b()'), ('if [[a]] [b'):match('%b[]'), ('say \"hi\" ok'):match('%b\"\"'))",
           "(a(b)c)\t[[a]]\t\"hi\"");
    Expect("print(('(((x'):match('%b()'), ('f(a)(b)'):gsub('%b()', '_'))", "nil\tf__\t2");
    // %f frontier: the subject ends count as '\\0'
    Expect("print(('THE (quick) fox'):gsub('%f[%a]%a+', 'W'))", "W (W) W\t3");
    Expect("print(('THE (quick) fox'):find('%f[%a]%a+%f[%A]'), ('ab'):find('%f[%c]'))", "1\t3\t2");
    Expect("print(('hello world'):gsub('%f[%w]%w', string.upper))", "Hello World\t2");
    // back references
    Expect("print(('say \\'hi\\' and \"ho\"'):gsub('([\"\\'])(.-)%1', '<%2>'))", "say <hi> and <ho>\t2");
    Expect("print(('abab cdcd xyxz'):gsub('(%a%a)%1', '[%1]'))", "[ab] [cd] xyxz\t2");
    Expect("print(('aa'):find('()%1'))", "nil");
    // find returns positions then captures; init, plain, and an empty pattern
    Expect("print(('key=val'):find('(%w+)=(%w+)'), ('a.b'):find('.', 1, true), ('aaa'):find('a', -1), ('abc'):find('', 10))",
           "1\t2\t3\tnil");
    Expect("print(('key=val'):find('(%w+)=(%w+)'))", "1\t7\tkey\tval");
    Expect("print(('abc'):match('.', 2), ('abc'):match('()', 4), ('abc'):find('', 4))", "b\t4\t4\t3");
    Expect("print(('abc'):gsub('%w', '%0%0'), ('abc'):gsub('', '-'), ('ab'):gsub('.', {a = 1, b = false}))",
           "aabbcc\t-a-b-c-\t1b\t2");
    // 32 captures are allowed, more are an error
    Expect("print(select('#', ('x'):rep(32):match(('(x)'):rep(32))))", "32");
    // pattern mistakes are reported in our own words, before matching
    Expect("print(pcall(string.find, 'a', '%'))", "false\tbad pattern: it ends with a '%' that escapes nothing");
    Expect("print(pcall(string.find, 'a', '[a'))", "false\tbad pattern: a '[' set has no closing ']'");
    Expect("print(pcall(string.find, 'a', '(a'))", "false\tbad pattern: a '(' in the pattern has no matching ')'");
    Expect("print(pcall(string.match, 'a', 'a)'))", "false\tbad pattern: a ')' with no '(' before it");
    Expect("print(pcall(string.find, 'a', '%b('))", "false\tbad pattern: '%b' needs two characters after it");
    Expect("print(pcall(string.find, 'a', '%fa'))", "false\tbad pattern: '%f' must be followed by a set in '[ ]'");
    Expect("print(pcall(string.find, 'a', '(a%1)'))",
           "false\tbad pattern: '%1' names a capture that is not complete at that point");
    Expect("print(pcall(string.find, 'a', ('(x)'):rep(33)))", "false\tbad pattern: more than 32 captures");
    Expect("print(pcall(string.gsub, 'a', 'a', '%2'))", "false\tcapture %2 does not exist (the pattern has no captures)");
    Expect("print(pcall(string.gsub, 'a', 'a', '%x'))",
           "false\tgsub: in the replacement string a '%' must be followed by a digit or another '%'");
    // the utf8 library's pattern goes through the same code (a '\\0' inside a set)
    Expect("local n = 0 for c in ('a\\u{E4}\\u{20AC}'):gmatch(utf8.charpattern) do n = n + 1 end print(n)", "3");
}

// One line in a script's Update must not freeze a frame: each of these ends with the budget
// error, quickly, because the library charges the work it does.
void HostileOneLiners() {
    const char* lines[] = {
        "string.rep('', 2^40)",                                                       // empty pieces, 2^40 copies
        "string.find(string.rep('a', 1e5), '[' .. string.rep('b', 1e5) .. ']')",      // a huge set at every position
        "string.find(string.rep('a', 1e5), '(a+)%1b')",                               // long back reference compares
        "string.find(string.rep('a', 1e6), string.rep('a', 1e5) .. 'b', 1, true)",   // a plain needle that almost matches
        "local s = string.rep('a', 1e6) for i = 1, 1e7 do s:upper() end",            // byte work in a cheap loop
        "local s = string.rep('a', 1e6) for i = 1, 1e7 do s:sub(2) end",
        "local s = string.rep('a', 1e6) for i = 1, 1e7 do s:gsub('^b', '') end",
        "local s = string.rep('a', 1e6) for i = 1, 1e7 do utf8.len(s) end",
        "local s = string.rep('a', 1e6) for i = 1, 1e7 do tostring(s) end",
        "local s = string.rep('a', 1e6) for i = 1, 1e7 do string.pack('z', s) end",
        "local src = string.rep('x = 1 ', 1e5) for i = 1, 1e6 do load(src) end",    // compiling a big chunk again and again
        "local a = string.rep('a', 1e7) local b = a .. '' for i = 1, 1e7 do local _ = a == b end",   // long equal strings
        "local a = string.rep('a', 1e7) local b = a .. '' for i = 1, 1e7 do local _ = a < b end",
        "local s = string.rep(' ', 1e7) .. '1' for i = 1, 1e7 do local _ = s + 0 end",   // a padded numeral read each time
        "table.sort(setmetatable({}, {__len = function() return 2^31 - 2 end}))",     // a fake length: no 32 GB copy
        "table.concat(setmetatable({}, {__index = tostring}), '', 1, 1e9)",            // a native __index per item
    };
    for (const char* line : lines) {
        const auto t0 = std::chrono::steady_clock::now();
        std::string out = Run(line, false, 5'000'000);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const bool stopped = out.find("instruction budget exceeded") != std::string::npos;
        if (!stopped || secs >= 3.0) std::cerr << "  hostile: " << line << " -> " << out << "  (" << secs << " s)\n";
        CHECK(stopped);
        CHECK(secs < 3.0);   // a hang would take minutes; 3 s leaves room for slow sanitizer runs
    }
}

} // namespace

int main() {
    RUN_SUITE("lua_core");
    Values();
    Closures();
    Coroutines();
    Errors();
    BudgetAndLimits();
    Patterns();
    HostileOneLiners();
    HostApi();
    Programs(false);
    Programs(true);
    CloseFinalizers();
    ConformanceScript(false);
    ConformanceScript(true);   // collect at every safe point
    Fuzz();
    TEST_MAIN_RESULT();
}
