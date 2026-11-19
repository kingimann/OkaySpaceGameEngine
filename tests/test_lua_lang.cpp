// Conformance tests for our own Lua 5.4 (engine/src/Lua). The expected values were written
// from the Lua 5.4 reference manual (not by running another Lua). Also sandbox and limits.
#include "test_framework.hpp"
#include "okay/Lua/Lua.hpp"
#include <cstdio>
#include <string>
#include <vector>

using okay::lua::Interpreter;
using okay::lua::ScriptLimits;

static Interpreter g_I;

// Evaluate an expression list; compare tostring of the results ("a, b").
static void Ex(const std::string& expr, const std::string& want) {
    std::string r, e;
    bool ok = g_I.Eval(expr, &r, &e);
    ++okaytest::g_checks;
    if (!ok || r != want) {
        ++okaytest::g_failures;
        std::cerr << "  FAIL: " << expr << "\n    want: " << want << "\n    got:  " << (ok ? r : "ERROR " + e) << "\n";
    }
}
// Run a function body on its own and compare what it returns.
static void Fn(const std::string& body, const std::string& want) { Ex("(function() " + body + " end)()", want); }
// The expression must raise an error whose text contains `part`.
static void Err(const std::string& expr, const std::string& part) {
    std::string r, e;
    bool ok = g_I.Eval(expr, &r, &e);
    ++okaytest::g_checks;
    if (ok || e.find(part) == std::string::npos) {
        ++okaytest::g_failures;
        std::cerr << "  FAIL: " << expr << "\n    want error containing: " << part << "\n    got: " << (ok ? "OK " + r : e) << "\n";
    }
}
static void ErrFn(const std::string& body, const std::string& part) { Err("(function() " + body + " end)()", part); }
static void SyntaxErr(const std::string& code, const std::string& part) {
    std::string e;
    bool ok = g_I.Check(code, "t", &e);
    ++okaytest::g_checks;
    if (ok || e.find(part) == std::string::npos) {
        ++okaytest::g_failures;
        std::cerr << "  FAIL syntax: " << code << "\n    want: " << part << "\n    got:  " << (ok ? "accepted" : e) << "\n";
    }
}

int main() {
    RUN_SUITE("lua_lang");

    // ---- arithmetic: integers and floats (manual 3.4.1, 3.4.3)
    Ex("1+2", "3"); Ex("7//2", "3"); Ex("-7//2", "-4"); Ex("7%-3", "-2"); Ex("-7%3", "2"); Ex("7.5//2", "3.0");
    Ex("1/2", "0.5"); Ex("3/1", "3.0"); Ex("2^10", "1024.0"); Ex("10//0.0", "inf"); Ex("-10//0.0", "-inf");
    Ex("math.maxinteger+1 == math.mininteger", "true"); Ex("math.mininteger", "-9223372036854775808");
    Err("5//0", "attempt to perform 'n//0'"); Err("5%0", "attempt to perform 'n%%0'");
    Ex("5.0%0 ~= 5.0%0", "true");   // nan
    Ex("3 | 4", "7"); Ex("5 & 3", "1"); Ex("5 ~ 3", "6"); Ex("~0", "-1"); Ex("1 << 63", "-9223372036854775808");
    Ex("1 << 64", "0"); Ex("-1 >> 1", "9223372036854775807"); Ex("1 >> -1", "2"); Ex("3.0 | 0", "3");
    Err("3.5 | 0", "number has no integer representation"); Err("{} | 1", "attempt to perform bitwise operation on a table value");
    Ex("10 == 10.0", "true"); Ex("'10' == 10", "false"); Ex("'10'+5", "15"); Ex("'3'*'4'", "12"); Ex("'0x10'+0", "16");
    Ex("10 .. 20", "1020"); Ex("1 .. ''", "1"); Ex("2^53 == 2^53+1", "true");
    Ex("1 < 2", "true"); Ex("'a' < 'b'", "true"); Ex("'a' < 'B'", "false"); Ex("'' < 'a'", "true"); Ex("1 <= 1.0", "true");
    Ex("math.maxinteger < math.maxinteger + 0.0", "true");
    Err("1 < 'a'", "attempt to compare number with string"); Err("{} < {}", "attempt to compare two table values");
    Err("nil < 1", "attempt to compare nil with number");
    Ex("0x7fffffffffffffff", "9223372036854775807"); Ex("0xffffffffffffffff", "-1"); Ex("9223372036854775807", "9223372036854775807");
    Ex("9223372036854775808", "9.2233720368548e+18"); Ex("1e2", "100.0"); Ex("0x.8", "0.5"); Ex("0xA.8p0", "10.5"); Ex(".5", "0.5");
    Ex("3 // 1", "3"); Ex("2^2", "4.0"); Ex("7 // 2.0", "3.0"); Ex("-0.0", "-0.0"); Ex("1e15", "1e+15"); Ex("1e100", "1e+100");
    Ex("0.1", "0.1"); Ex("100 // 1e0", "100.0"); Ex("255 // 1", "255"); Ex("2^53", "9.007199254741e+15");
    Ex("-2^2", "-4.0"); Ex("2^3^2", "512.0"); Ex("not nil == true", "true"); Ex("1 .. 2 == '12'", "true");
    Ex("-3 % 5", "2"); Ex("5.5 % -2", "-0.5"); Ex("1 // -1", "-1"); Ex("math.mininteger // -1", "-9223372036854775808");
    Ex("math.mininteger % -1", "0");

    // ---- math library
    Ex("math.tointeger(3.0)", "3"); Ex("math.tointeger(3.5)", "nil"); Ex("math.tointeger('8')", "8");
    Ex("math.floor(-3.5)", "-4"); Ex("math.ceil(3.2)", "4"); Ex("math.floor(3)", "3"); Ex("math.max(1,2.5)", "2.5");
    Ex("math.min(3,1,2)", "1"); Ex("math.abs(-3)", "3"); Ex("math.abs(-3.5)", "3.5"); Ex("math.fmod(-7,3)", "-1");
    Ex("math.fmod(7,-3)", "1"); Ex("math.sqrt(16)", "4.0"); Ex("math.huge", "inf"); Ex("-math.huge", "-inf");
    Ex("math.pi", "3.1415926535898"); Ex("math.type(2^31)", "float"); Ex("math.type(1)", "integer"); Ex("math.type('1')", "nil");
    Ex("math.ult(1,-1)", "true"); Ex("math.floor(2^62)", "4611686018427387904"); Ex("math.log(8,2)", "3.0");
    Ex("math.log(100,10)", "2.0"); Ex("math.exp(0)", "1.0"); Ex("math.sin(0)", "0.0"); Ex("math.cos(0)", "1.0");
    Ex("math.atan(1,1) * 4 == math.pi", "true"); Ex("select('#', math.modf(3.7))", "2");
    Fn("math.randomseed(7) local a=math.random(1,10) return a>=1 and a<=10", "true");
    Fn("local ok=true for i=1,200 do local r=math.random() if r<0 or r>=1 then ok=false end end return ok", "true");
    Fn("local ok=true for i=1,200 do local r=math.random(5) if r<1 or r>5 then ok=false end end return ok", "true");
    ErrFn("return math.random(0)", "interval is empty"); Err("math.floor('x')", "number expected, got string");

    // ---- strings
    Ex("#'abc'", "3"); Ex("('abc'):upper()", "ABC"); Ex("('ABC'):lower()", "abc"); Ex("string.sub('hello',2,4)", "ell");
    Ex("('hello'):sub(-3)", "llo"); Ex("('hello'):sub(0)", "hello"); Ex("('hello'):sub(10)", ""); Ex("('hello'):sub(2,-2)", "ell");
    Ex("string.rep('ab',3)", "ababab"); Ex("string.rep('ab',3,',')", "ab,ab,ab"); Ex("('x'):rep(0)", "");
    Ex("string.reverse('abc')", "cba"); Ex("string.byte('A')", "65"); Ex("string.byte('abc',1,3)", "97, 98, 99");
    Ex("string.char(72,73)", "HI"); Ex("string.len('\\0ab')", "3"); Ex("'a\\65\\x42\\u{43}'", "aABC");
    Ex("'tab\\tq'", "tab\tq"); Ex("[[long\nstring]]", "long\nstring"); Ex("[==[a]]b]==]", "a]]b"); Ex("'a\\z   \n   b'", "ab");
    Ex("string.find('hello','l')", "3, 3"); Ex("string.find('hello','xyz')", "nil"); Ex("string.find('hello','l+')", "3, 4");
    Ex("string.find('a.b','.',1,true)", "2, 2"); Ex("string.find('hello','(l)(l)')", "3, 4, l, l");
    Ex("string.find('abc','b',-1)", "nil"); Ex("string.find('abc','',10)", "nil"); Ex("string.find('abc','',4)", "4, 3");
    Ex("string.find('abc','^b')", "nil"); Ex("string.find('abc','c$')", "3, 3"); Ex("string.find('a$b','$b')", "2, 3");
    Ex("string.match('key=val','(%w+)=(%w+)')", "key, val"); Ex("string.match('  trim  ','^%s*(.-)%s*$')", "trim");
    Ex("string.match('2024-01-05','(%d+)-(%d+)-(%d+)')", "2024, 01, 05"); Ex("string.match('abc','()b()')", "2, 3");
    Ex("string.match('hello','%a+')", "hello"); Ex("string.match('f(a(b)c)d','%b()')", "(a(b)c)");
    Ex("string.match('abc','^b')", "nil"); Ex("string.match('abab','(ab)%1')", "ab"); Ex("string.match('x_1','[%a_][%w_]*')", "x_1");
    Ex("string.match('a-b','[a%-]+')", "a-"); Ex("string.match('hello','[^l]+')", "he"); Ex("string.match('0x1F','0[xX](%x+)')", "1F");
    Ex("string.match('THE (quick) fox','%f[%a]%a+',5)", "quick"); Ex("string.match('abc','a.-c')", "abc");
    Ex("string.match('aaa','a-')", ""); Ex("string.match('aaab','a*b')", "aaab"); Ex("string.match('ab','a?b?c?')", "ab");
    Ex("string.match('[x]','%[(.)%]')", "x"); Ex("string.match('a]b','[]]')", "]"); Ex("string.match('5.25','^%-?%d+%.?%d*$')", "5.25");
    Ex("string.gsub('hello world','o','0')", "hell0 w0rld, 2"); Ex("string.gsub('abc','%w','%0%0')", "aabbcc, 3");
    Ex("string.gsub('hello','l','L',1)", "heLlo, 1"); Ex("string.gsub('abc','','-')", "-a-b-c-, 4");
    Ex("string.gsub('hello world','(%w+)','<%1>')", "<hello> <world>, 2"); Ex("string.gsub('1 2 3','%d',function(d) return d*2 end)", "2 4 6, 3");
    Ex("string.gsub('$a $b','%$(%w+)',{a='X',b=false})", "X $b, 2"); Ex("string.gsub('abc','^a','X')", "Xbc, 1");
    Ex("string.gsub('aaa','^a','X')", "Xaa, 1"); Ex("string.gsub('a b','%s+','')", "ab, 1");
    Err("string.gsub('x','x','%2')", "invalid capture index"); Err("string.find('a','[a')", "malformed pattern");
    Err("string.find('a','%')", "malformed pattern"); Err("string.rep()", "bad argument #1 to 'rep'");
    Fn("local t={} for w in string.gmatch('one two  three','%a+') do t[#t+1]=w end return table.concat(t,',')", "one,two,three");
    Fn("local t={} for k,v in string.gmatch('a=1, b=2','(%w+)=(%w+)') do t[#t+1]=k..v end return table.concat(t,';')", "a1;b2");
    Fn("local n=0 for _ in string.gmatch('abc','') do n=n+1 end return n", "4");
    Ex("string.format('%d',3.0)", "3"); Ex("string.format('%5d|%-5d|',42,42)", "   42|42   |"); Ex("string.format('%05.1f',3.14159)", "003.1");
    Ex("string.format('%x',255)", "ff"); Ex("string.format('%X',255)", "FF"); Ex("string.format('%s %s',1,true)", "1 true");
    Ex("string.format('%q','hi')", "\"hi\""); Ex("string.format('%%')", "%"); Ex("string.format('%10.3s|','abcdef')", "       abc|");
    Ex("string.format('%c',65)", "A"); Ex("string.format('%e',1)", "1.000000e+00"); Ex("string.format('%g',0.1)", "0.1");
    Ex("string.format('%s',nil)", "nil"); Ex("string.format('%.3f',2/3)", "0.667"); Ex("string.format('%i',7)", "7");
    Ex("string.format('%o',8)", "10"); Ex("string.format('%+d',5)", "+5"); Ex("string.format('%s',setmetatable({},{__tostring=function() return 'T' end}))", "T");
    Err("string.format('%d',3.5)", "number has no integer representation"); Err("string.format('%d','x')", "number expected, got string");
    Err("string.format('%y',1)", "invalid conversion"); Err("string.format('%d')", "number expected, got no value");
    Ex("tostring(1.0)", "1.0"); Ex("tostring(true)", "true"); Ex("tostring(nil)", "nil"); Ex("tostring(12)", "12");
    Ex("tonumber('0x10')", "16"); Ex("tonumber('1e2')", "100.0"); Ex("tonumber(' 5 ')", "5"); Ex("tonumber('5x')", "nil");
    Ex("tonumber('10',2)", "2"); Ex("tonumber('ff',16)", "255"); Ex("tonumber('')", "nil"); Ex("tonumber('z',36)", "35");
    Ex("tonumber('8',8)", "nil"); Ex("tonumber(12)", "12"); Ex("tonumber('-7')", "-7"); Ex("tonumber('1 2')", "nil");
    Ex("tonumber('inf')", "nil"); Ex("tonumber('nan')", "nil"); Ex("tonumber('0x')", "nil");
    Ex("('x'):len()", "1"); Ex("#('a'..'bc')", "3"); Ex("('%d'):format(5)", "5");

    // ---- tables and the table library
    Ex("#{1,2,3}", "3"); Ex("#{}", "0"); Ex("#{n=1}", "0"); Ex("select('#',table.unpack({1,2,3}))", "3");
    Ex("table.concat({1,2,3},', ')", "1, 2, 3"); Ex("table.concat({},',')", ""); Ex("table.concat({1,2,3},'-',2,3)", "2-3");
    Ex("table.concat({'a','b'})", "ab"); Err("table.concat({1,{},3})", "invalid value (at index 2) in table for 'concat'");
    Fn("local t={1,2,3} table.insert(t,4) table.insert(t,1,0) return table.concat(t,',')", "0,1,2,3,4");
    Fn("local t={1,2,3} local x=table.remove(t) local y=table.remove(t,1) return x..','..y..','..#t", "3,1,1");
    Fn("local t={} return table.remove(t)", "nil");
    Fn("local t={3,1,2} table.sort(t) return table.concat(t)", "123");
    Fn("local t={3,1,2} table.sort(t,function(a,b) return a>b end) return table.concat(t)", "321");
    Fn("local t={'b','c','a'} table.sort(t) return table.concat(t)", "abc");
    Fn("local t={} for i=1,200 do t[i]=(i*37)%101 end table.sort(t) for i=2,200 do if t[i-1]>t[i] then return false end end return true", "true");
    Fn("local p=table.pack(1,nil,3) return p.n", "3"); Ex("table.unpack({1,2,3},2)", "2, 3"); Ex("table.unpack({1,2,3},2,3)", "2, 3");
    Fn("local t=table.move({1,2,3},1,3,2) return table.concat(t,',')", "1,1,2,3");
    Fn("local n=0 for k,v in pairs({a=1,b=2,3}) do n=n+1 end return n", "3");
    Fn("local n=0 for i,v in ipairs({1,2,nil,4}) do n=n+1 end return n", "2");
    Fn("local t={a=1,b=2,c=3} for k in pairs(t) do t[k]=nil end return next(t)", "nil");
    Fn("local t={} t[1.0]='a' return t[1]", "a"); Fn("local t={} t[2^53]=1 return t[2^53]", "1");
    ErrFn("local t={} t[nil]=1", "table index is nil"); ErrFn("local t={} t[0/0]=1", "table index is NaN");
    Fn("local t={} return t[nil]", "nil");
    Err("table.insert({},5,1)", "position out of bounds"); Err("table.insert({},1,2,3)", "wrong number of arguments");
    Err("table.insert(nil,1)", "bad argument #1 to 'insert' (table expected, got nil)");
    Fn("local t={10,20,30} local s=0 for i,v in ipairs(t) do s=s+i*v end return s", "140");
    Fn("local t={x=1} t.y=2 t['z']=3 return t.x+t.y+t.z", "6");
    Fn("local t={[1]='a',[2]='b','c'} return t[1]", "c");
    Fn("local t={1,2,3,nil} return #t", "3");
    Fn("local function f() return 1,2,3 end local t={f(),f()} return #t", "4");
    Fn("local t={n=1,[10]=2,3} return t.n+t[10]+t[1]", "6");
    Fn("local t={} for i=1,100 do t[i]=i end for i=1,100 do t[i]=nil end return #t", "0");
    Fn("local t={} for i=100,1,-1 do t[i]=i end return #t", "100");
    Fn("local t={} t[3]=3 t[2]=2 t[1]=1 return #t", "3");

    // ---- functions, closures, varargs, multiple results
    Fn("local function f(...) return select('#',...) end return f(nil,nil)", "2");
    Fn("local function f(...) local a,b=... return b end return f(1,2,3)", "2");
    Fn("local function f() return 1,2 end return (f())", "1");
    Fn("local a,b=1,2 a,b=b,a return a..b", "21");
    Fn("local function f() return 1,2,3 end return #{f()}, #{f(),10}", "3, 2");
    Fn("local function f(a,b) return a,b end return f(1)", "1, nil");
    Fn("local function f(a) return a end return f(1,2,3)", "1");
    Fn("local function fact(n) if n<=1 then return 1 end return n*fact(n-1) end return fact(10)", "3628800");
    Fn("local function fib(n) if n<2 then return n end return fib(n-1)+fib(n-2) end return fib(20)", "6765");
    Fn("local t={} for i=1,3 do t[i]=function() return i end end return t[1]()..t[2]()..t[3]()", "123");
    Fn("local function mk() local n=0 return function() n=n+1 return n end, function() return n end end local inc,get=mk() inc() inc() return get()", "2");
    Fn("local fs={} local i=1 while i<=3 do local j=i fs[i]=function() return j end i=i+1 end return fs[1]()..fs[3]()", "13");
    Fn("local o={v=5} function o:get() return self.v end return o:get()", "5");
    Fn("local o={v=5} function o.get(self) return self.v end return o:get()", "5");
    Fn("local s=0 for i=1,3 do for j=1,3 do if j==2 then break end s=s+1 end end return s", "3");
    Fn("local x=1 do local x=2 end return x", "1"); Fn("local x=1 local x=x+1 return x", "2");
    Fn("local f=function(...) return ... end return f(1,2,3)", "1, 2, 3");
    Fn("local function f(...) local t={...} return #t end return f(1,2,3,4)", "4");
    Fn("local function f(...) return (select('#',...)) end return f()", "0");
    Fn("local a <const> = 5 return a+1", "6");
    Fn("return (function(a,b,...) return a,b,select('#',...) end)(1,2,3,4)", "1, 2, 2");
    Ex("select('#')", "0"); Ex("select(2,'a','b','c')", "b, c"); Ex("select(-1,'a','b')", "b"); Err("select(0,1)", "index out of range");
    Fn("local function f(n) if n==0 then return 'done' end return f(n-1) end return f(1000)", "done");
    Fn("local t=setmetatable({},{__index=function(t,k) return k*2 end}) return t[21]", "42");
    Fn("local a={} local b={} return a==b, a==a", "false, true");
    Fn("local s=0 for i=1,10 do if i%2==0 then goto_skip=true else s=s+i end end return s", "25");
    Fn("local function f() end return f()", "");
    Fn("return nil", "nil"); Fn("return", "");

    // ---- control flow
    Fn("local s='' for i=3,1,-1 do s=s..i end return s", "321");
    Fn("for i=1,0 do error('no') end return 'ok'", "ok");
    Fn("local n=0 for x=0,1,0.25 do n=n+1 end return n", "5");
    Fn("local n=0 for i=math.maxinteger-1,math.maxinteger do n=n+1 end return n", "2");
    Fn("local n=0 for i=math.mininteger,math.mininteger+2 do n=n+1 end return n", "3");
    Fn("local n=0 for i=1,3 do local i=i*2 n=n+i end return n", "12");
    Fn("local s=0 for i=1.0,3 do s=s+i end return s", "6.0");
    Fn("local t={} for i=1,3 do t[#t+1]=math.type(i) end return t[1]", "integer");
    ErrFn("for i=1,10,0 do end", "'for' step is zero"); ErrFn("for i='a',2 do end", "'for' initial value must be a number");
    ErrFn("for i=1,{} do end", "'for' limit must be a number");
    Fn("local i=0 repeat local j=i i=i+1 until j>=2 return i", "3");
    Fn("local i=0 while true do i=i+1 if i>=5 then break end end return i", "5");
    Fn("local r='' for k,v in next,{10} do r=r..k..v end return r", "110");
    Fn("local x if false then x=1 elseif nil then x=2 else x=3 end return x", "3");
    Fn("return 1 and 2, nil and 1, false or 'x', nil or false, 1 or error('no')", "2, nil, x, false, 1");
    Fn("local a=nil local b=a and a.x return b", "nil");
    Fn("return not 0, not nil, not not 1", "false, true, true");

    // ---- metatables (manual 2.4)
    Fn("local mt={__add=function(a,b) return a.v+b.v end} local x=setmetatable({v=1},mt) return x+setmetatable({v=2},mt)", "3");
    Fn("local mt={__index={foo=7}} return setmetatable({},mt).foo", "7");
    Fn("local log={} local t=setmetatable({}, {__newindex=function(t,k,v) rawset(t,k,v*2) end}) t.a=5 return t.a", "10");
    Fn("local t=setmetatable({}, {__call=function(self,a,b) return a+b end}) return t(2,3)", "5");
    Fn("return tostring(setmetatable({}, {__tostring=function() return 'hi' end}))", "hi");
    Fn("return #setmetatable({}, {__len=function() return 99 end})", "99");
    Fn("local mt={__eq=function(a,b) return true end} return setmetatable({},mt)==setmetatable({},mt)", "true");
    Fn("local mt={__lt=function(a,b) return a.v<b.v end,__le=function(a,b) return a.v<=b.v end} local a,b=setmetatable({v=1},mt),setmetatable({v=2},mt) return a<b, a<=b, a>b, a>=b", "true, true, false, false");
    Fn("local mt={__concat=function(a,b) return 'C' end} return setmetatable({},mt)..'x', 'x'..setmetatable({},mt)", "C, C");
    Fn("local mt={__unm=function(a) return 'neg' end} return -setmetatable({},mt)", "neg");
    Fn("local mt={__div=function() return 'd' end,__mod=function() return 'm' end,__idiv=function() return 'i' end,__pow=function() return 'p' end,__band=function() return 'b' end,__shl=function() return 's' end} local o=setmetatable({},mt) return o/1,o%1,o//1,o^1,o&1,o<<1", "d, m, i, p, b, s");
    Fn("local mt={__index=function(t,k) return k end} local t=setmetatable({a=1},mt) return t.a, t.b, rawget(t,'b')", "1, b, nil");
    Fn("local A={} A.__index=A function A.new() return setmetatable({n=1},A) end function A:inc() self.n=self.n+1 return self end local B=setmetatable({},{__index=A}) B.__index=B local b=setmetatable({n=5},B) return b:inc().n", "6");
    Fn("local t=setmetatable({},{__metatable='locked'}) return getmetatable(t)", "locked");
    ErrFn("local t=setmetatable({},{__metatable='locked'}) setmetatable(t,{})", "cannot change a protected metatable");
    Fn("return getmetatable('').__index == string", "true");
    Fn("local t={} return rawequal(t,t), rawlen({1,2}), rawlen('abc'), rawget({5},1)", "true, 2, 3, 5");
    Fn("local mt={} mt.__index=mt local t=setmetatable({},mt) return t.x", "nil");
    Fn("local n=0 local t=setmetatable({}, {__pairs=function(t) return function(_,k) if not k then return 1,'a' end end,t,nil end}) for k,v in pairs(t) do n=n+1 end return n", "1");

    // ---- errors and pcall
    Ex("pcall(error,'x')", "false, x"); Ex("pcall(error,'x',0)", "false, x"); Ex("select('#',pcall(error))", "2");
    Ex("pcall(function() return 1,2 end)", "true, 1, 2"); Ex("pcall(pcall,error,'e')", "true, false, e");
    Fn("local ok,e=pcall(function() error('boom') end) return e", "(eval):1: boom");
    Fn("local ok,e=pcall(function() error('boom',0) end) return e", "boom");
    Fn("local ok,e=pcall(function() error({code=7}) end) return type(e), e.code", "table, 7");
    Fn("local ok,e=pcall(function() error() end) return ok, e", "false, nil");
    Fn("return xpcall(function() error('x',0) end, function(m) return 'h:'..m end)", "false, h:x");
    Fn("return xpcall(function(a,b) return a+b end, print, 1, 2)", "true, 3");
    Ex("pcall(assert,false,'m')", "false, m"); Ex("select('#',assert(1,2,3))", "3"); Ex("pcall(assert,nil)", "false, assertion failed!");
    Ex("pcall(nil)", "false, attempt to call a nil value"); Err("pcall()", "bad argument #1 to 'pcall' (value expected)");
    ErrFn("local x=nil return x.y", "attempt to index a nil value (local 'x')");
    ErrFn("return undefinedvar.y", "attempt to index a nil value (global 'undefinedvar')");
    ErrFn("local t={} return t.a.b", "attempt to index a nil value (field 'a')");
    ErrFn("undefinedfn()", "attempt to call a nil value (global 'undefinedfn')");
    ErrFn("local t={} t.go()", "attempt to call a nil value (field 'go')"); ErrFn("local t={} t:go()", "attempt to call a nil value (method 'go')");
    ErrFn("local x return x+1", "attempt to perform arithmetic on a nil value (local 'x')");
    ErrFn("return 1+{}", "attempt to perform arithmetic on a table value"); ErrFn("return 'abc'+1", "attempt to perform arithmetic on a string value");
    ErrFn("local x return 'a'..x", "attempt to concatenate a nil value (local 'x')"); ErrFn("local n=5 return #n", "attempt to get length of a number value (local 'n')");
    ErrFn("local a='x' a.y=1", "attempt to index a string value"); ErrFn("return (1)()", "attempt to call a number value");
    ErrFn("local t=nil t.x=1", "attempt to index a nil value (local 't')");
    ErrFn("error('with position')", "(eval):1: with position");
    ErrFn("local x=-nil", "attempt to perform arithmetic on a nil value");
    std::string e;
    CHECK(!g_I.Run("local a = 1\nlocal b = nil\nreturn b.c", "mod.lua", &e));
    CHECK(e == "mod.lua:3: attempt to index a nil value (local 'b')");
    CHECK(!g_I.Run("function f()\n  error('deep')\nend\nf()", "mod.lua", &e));
    CHECK(e == "mod.lua:2: deep");
    CHECK(!g_I.Run("local function f() error('lvl2', 2) end\nlocal function g()\n  f()\nend\ng()", "mod.lua", &e));
    CHECK(e == "mod.lua:3: lvl2");
    CHECK(!g_I.Run("error('top', 1)", "mod.lua", &e)); CHECK(e == "mod.lua:1: top");
    CHECK(!g_I.Run("error({})", "mod.lua", &e)); CHECK(e.find("error object is a table value") != std::string::npos);
    CHECK(!g_I.Run("local t = {}\nt.x.y = 1", "m", &e)); CHECK(e == "m:2: attempt to index a nil value (field 'x')");

    // ---- syntax errors: plain words with line numbers
    SyntaxErr("x = = 1", "t:1: unexpected symbol near '='");
    SyntaxErr("if x then\n y = 1\n", "t:3: 'end' expected (to close 'if' at line 1) near <eof>");
    SyntaxErr("while true do\n", "'end' expected (to close 'while' at line 1) near <eof>");
    SyntaxErr("function f()\n", "'end' expected (to close 'function' at line 1) near <eof>");
    SyntaxErr("for i=1,2 do", "'end' expected near <eof>"); SyntaxErr("x = (1", "')' expected near <eof>");
    SyntaxErr("x = 'abc", "unfinished string"); SyntaxErr("x = [[abc", "unfinished long string");
    SyntaxErr("--[[ never closed", "unfinished long comment"); SyntaxErr("x = 1 +", "unexpected symbol near <eof>");
    SyntaxErr("local 5 = 1", "<name> expected near '5'"); SyntaxErr("x = 3x", "malformed number near '3x'");
    SyntaxErr("return return", "unexpected symbol near 'return'"); SyntaxErr("f(", "unexpected symbol near <eof>");
    SyntaxErr("x = {1,2", "'}' expected near <eof>"); SyntaxErr("local x <const> = 1; x = 2", "attempt to assign to const variable 'x'");
    SyntaxErr("break", "break outside a loop"); SyntaxErr("x = '\\q'", "invalid escape sequence");
    SyntaxErr("x = 1 2", "unexpected symbol near '2'"); SyntaxErr("local function() end", "<name> expected");
    SyntaxErr("::a::", "not supported"); SyntaxErr("goto a", "not supported");
    SyntaxErr("function f(...) end function g() return ... end", "cannot use '...' outside a vararg function");
    SyntaxErr("x = '\\300'", "decimal escape too large");
    CHECK(g_I.Check("x = 1 -- comment\n--[==[ long\ncomment ]==] y = 2", "t", &e));
    CHECK(g_I.Check("#!/usr/bin/lua\nx=1", "t", &e));
    CHECK(g_I.Check("local t = {f = function() end, [1]=2; 3}", "t", &e));
    CHECK(g_I.Check("f{1} f'x' f[[x]] a.b:c(1)", "t", &e));
    CHECK(g_I.Check("return function(...) local a,b = ... end", "t", &e));
    std::string deep = "x = "; for (int i = 0; i < 1000; ++i) deep += "("; deep += "1"; for (int i = 0; i < 1000; ++i) deep += ")";
    CHECK(!g_I.Check(deep, "t", &e)); CHECK(e.find("too many syntax levels") != std::string::npos);

    // ---- coroutines (manual 2.6, 6.2)
    Fn("local co=coroutine.wrap(function(a) local b=coroutine.yield(a+1) return b*2 end) return co(1), co(10)", "2, 20");
    Fn("local co=coroutine.create(function() coroutine.yield(1) end) local s={} s[#s+1]=coroutine.status(co) coroutine.resume(co) s[#s+1]=coroutine.status(co) coroutine.resume(co) s[#s+1]=coroutine.status(co) return table.concat(s,',')", "suspended,suspended,dead");
    Fn("local co=coroutine.create(function() return 1 end) coroutine.resume(co) return coroutine.resume(co)", "false, cannot resume dead coroutine");
    Fn("local co=coroutine.create(function() error('bad') end) return coroutine.resume(co)", "false, (eval):1: bad");
    Fn("local co=coroutine.create(function(a,b) local c=coroutine.yield(a+b) local d,e=coroutine.yield(c*2) return d+e end) local _,x=coroutine.resume(co,1,2) local _,y=coroutine.resume(co,10) local _,z=coroutine.resume(co,3,4) return x,y,z", "3, 20, 7");
    Fn("local function gen(n) return coroutine.wrap(function() for i=1,n do coroutine.yield(i) end end) end local s=0 for v in gen(5) do s=s+v end return s", "15");
    Fn("local co=coroutine.wrap(function() local function inner() coroutine.yield('deep') end inner() return 'end' end) return co(), co()", "deep, end");
    Fn("local co=coroutine.create(function() local ok,e=pcall(function() coroutine.yield('in') error('x',0) end) coroutine.yield(e) return 'fin' end) local _,a=coroutine.resume(co) local _,b=coroutine.resume(co) local _,c=coroutine.resume(co) return a,b,c", "in, x, fin");
    Fn("return coroutine.isyieldable()", "false"); Fn("return coroutine.wrap(function() return coroutine.isyieldable() end)()", "true");
    Fn("local co,main=coroutine.running() return type(co), main", "thread, true");
    ErrFn("coroutine.yield(1)", "attempt to yield from outside a coroutine");
    Fn("local co=coroutine.wrap(function() error('E',0) end) local ok,e=pcall(co) return ok,e", "false, E");
    Fn("local co co=coroutine.create(function() return coroutine.status(co) end) return select(2,coroutine.resume(co))", "running");
    Fn("local c1,c2 c1=coroutine.create(function() coroutine.resume(c2) end) c2=coroutine.create(function() return coroutine.status(c1) end) coroutine.resume(c1) return coroutine.status(c1)", "dead");
    Fn("local co=coroutine.create(function() coroutine.yield() end) coroutine.resume(co) return coroutine.close(co), coroutine.status(co)", "true, dead");
    Fn("local co=coroutine.wrap(function(...) return select('#',...) end) return co(nil,nil,nil)", "3");
    Fn("local co=coroutine.create(function() local t=setmetatable({}, {__index=function() coroutine.yield() end}) return t.x end) return coroutine.resume(co)", "false, (eval):1: attempt to yield across a native call boundary");
    Fn("local sum=0 local cos={} for i=1,50 do cos[i]=coroutine.wrap(function() for j=1,3 do coroutine.yield(i*j) end end) end for r=1,3 do for i=1,50 do sum=sum+cos[i]() end end return sum", "7650");

    // ---- utf8 (manual 6.5)
    Ex("utf8.char(72, 228, 8364, 128512)", "H\xC3\xA4\xE2\x82\xAC\xF0\x9F\x98\x80"); Ex("utf8.len('h\\u{E9}llo')", "5"); Ex("utf8.len('\\xff')", "nil, 1");
    Ex("utf8.codepoint('\\u{20AC}')", "8364"); Ex("utf8.codepoint('abc', 1, -1)", "97, 98, 99"); Ex("utf8.offset('a\\u{E9}b', 3)", "4");
    Ex("utf8.offset('a\\u{E9}b', -1)", "4"); Ex("#utf8.charpattern", "14"); Err("utf8.codepoint('\\xff')", "invalid UTF-8 code");
    Fn("local n=0 for p,c in utf8.codes('a\\u{E9}\\u{20AC}') do n=n+c end return n", "8694");
    Fn("local t={} for c in ('h\\u{E9}y'):gmatch(utf8.charpattern) do t[#t+1]=#c end return table.concat(t,',')", "1,2,1");

    // ---- load, environment, sandbox surface
    Ex("load('return 1+1')()", "2"); Ex("select('#',load('x ='))", "2"); Ex("(load('x ='))", "nil");
    Fn("local f,e=load('x = = 1','mychunk') return e", "[string \"mychunk\"]:1: unexpected symbol near '='");
    Fn("local f=load('syntax error here') return f", "nil");
    Fn("local parts={'return ','4','2'} local i=0 local f=load(function() i=i+1 return parts[i] end) return f()", "42");
    Ex("type(os.time())", "number"); Ex("type(os.clock())", "number"); Ex("os.getenv('HOME')", "nil"); Ex("type(os.date('%Y'))", "string");
    Ex("os.execute", "nil"); Ex("os.exit", "nil"); Ex("os.remove", "nil"); Ex("os.rename", "nil"); Ex("os.tmpname", "nil");
    Ex("io.open", "nil"); Ex("io.popen", "nil"); Ex("io.lines", "nil"); Ex("dofile", "nil"); Ex("loadfile", "nil"); Ex("require", "nil");
    Ex("debug", "nil"); Ex("package", "nil"); Ex("string.dump", "nil"); Ex("collectgarbage('count') > 0", "true");
    Ex("_VERSION", "Lua 5.4"); Ex("_G._G == _G", "true"); Fn("x_global_test = 5 return _G.x_global_test", "5");
    Fn("local out={} local save=print print=function(...) out[#out+1]=select('#',...) end print(1,2,3) print=save return out[1]", "3");
    Ex("tostring(print):sub(1,9)", "function:"); Ex("tostring({}):sub(1,6)", "table:"); Ex("type(print)", "function");
    Ex("type(coroutine.create(print))", "thread");

    // ---- limits and the sandbox
    {
        Interpreter I;
        ScriptLimits lim; lim.instructions = 100000; lim.seconds = 0;
        I.SetLimits(lim);
        std::string err;
        CHECK(!I.Run("while true do end", "loop", &err));
        CHECK(err.find("too many instructions") != std::string::npos);
        CHECK(err.find("loop:1:") == 0);
        // pcall cannot swallow the budget error, and the interpreter is usable afterwards
        CHECK(!I.Run("pcall(function() while true do end end) after = 1", "loop", &err));
        CHECK(I.GlobalText("after") == "nil");
        CHECK(I.Run("ok = 'fine'", "next", &err)); CHECK(I.GlobalText("ok") == "fine");
        CHECK(!I.Run("local function f() return f() + 1 end f()", "rec", &err));
        CHECK(err.find("stack overflow") != std::string::npos);
        CHECK(I.Run("local ok, e = pcall(function() local function f() return f() + 1 end return f() end) r = tostring(ok) .. ':' .. tostring(e):match('stack overflow')", "rec", &err));
        CHECK(I.GlobalText("r") == "false:stack overflow");
        CHECK(!I.Run("string.rep('x', 1e10)", "mem", &err)); CHECK(err.find("not enough memory") != std::string::npos);
        CHECK(I.Run("r = select(2, pcall(string.rep, 'x', 1e10)):match('not enough memory')", "mem", &err));
        CHECK(I.GlobalText("r") == "not enough memory");
    }
    {
        Interpreter I;
        ScriptLimits lim; lim.instructions = 0; lim.seconds = 0.05;
        I.SetLimits(lim);
        std::string err;
        CHECK(!I.Run("while true do end", "t", &err));
        CHECK(err.find("ran too long") != std::string::npos);
    }
    {
        Interpreter I;
        ScriptLimits lim; lim.instructions = 0; lim.seconds = 0; lim.memoryBytes = 8u << 20;
        I.SetLimits(lim);
        std::string err;
        CHECK(!I.Run("local t = {} local i = 0 while true do i = i + 1 t[i] = string.rep('x', 1000) .. i end", "mem", &err));
        CHECK(err.find("not enough memory") != std::string::npos);
        // garbage is collected: churn far more than the cap, no error
        CHECK(I.Run("for i = 1, 300000 do local t = {i, tostring(i), {i}} end done = true", "churn", &err));
        CHECK(I.GlobalText("done") == "true");
        CHECK(I.MemoryUsed() < (8u << 20));
        CHECK(I.Run("local keep = {} for i = 1, 20000 do keep[i] = {i} end collectgarbage() local s = 0 for i = 1, 20000 do s = s + keep[i][1] end total = s", "keep", &err));
        CHECK(I.GlobalText("total") == "200010000");
    }
    {   // closures, strings and coroutines survive collections that happen in the middle of work
        Interpreter I;
        std::string err;
        CHECK(I.Run(
            "local fs = {} for i = 1, 2000 do local s = 'v' .. i fs[i] = function() return s end if i % 100 == 0 then collectgarbage() end end\n"
            "local cos = {} for i = 1, 100 do cos[i] = coroutine.create(function(x) local keep = {x} coroutine.yield(x) collectgarbage() return keep[1] * 2 end) coroutine.resume(cos[i], i) end\n"
            "collectgarbage()\n"
            "a = fs[1777]() local sum = 0 for i = 1, 100 do local _, v = coroutine.resume(cos[i]) sum = sum + v end b = sum", "gc", &err));
        CHECK(I.GlobalText("a") == "v1777"); CHECK(I.GlobalText("b") == "10100");
    }
    {   // print goes to the sink
        Interpreter I;
        std::vector<std::string> out;
        I.SetOutput([&](const std::string& s) { out.push_back(s); });
        std::string err;
        CHECK(I.Run("print('a', 1, nil, true) io.write('x', 2, '\\n') io.stdout:write('y')", "p", &err));
        CHECK(out.size() == 3); if (out.size() == 3) { CHECK(out[0] == "a\t1\tnil\ttrue"); CHECK(out[1] == "x2\n"); CHECK(out[2] == "y"); }
    }
    {   // deep recursion within limits works (non-tail)
        Interpreter I; std::string err, r;
        CHECK(I.Eval("(function() local function d(n) if n == 0 then return 0 end return 1 + d(n - 1) end return d(5000) end)()", &r, &err));
        CHECK(r == "5000");
    }
    TEST_MAIN_RESULT();
}
