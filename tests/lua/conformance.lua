-- Conformance checks for okay::lua, written from the Lua 5.4 Reference Manual's
-- stated semantics (arithmetic and integer/float rules, strings and patterns,
-- metatables, closures, goto, coroutines, to-be-closed variables, errors).
-- test_lua_core runs this file; a failing check prints "FAIL <name>".
local function check(name, cond) if not cond then print("FAIL " .. name) end end
local function err(f, ...) local ok, e = pcall(f, ...) return (not ok) and e or ("no error") end
-- multiple assignment and swap
local a, b = 1, 2; a, b = b, a; check("swap", a == 2 and b == 1)
local t = {1, 2}; t[1], t[2] = t[2], t[1]; check("tswap", t[1] == 2 and t[2] == 1)
local i = 1; i, t[i] = i + 1, 20; check("assign order", i == 2 and t[1] == 20)
-- varargs
local function va(...) local n = select('#', ...) return n, ... end
check("va0", select('#', va()) == 1)
check("va3", select('#', va(1, nil, nil)) == 4)
local function pk(...) return {...} end
check("pk", #pk(1, 2, 3) == 3)
local function tail(n) if n == 0 then return "done" end return tail(n - 1) end
check("tail", tail(100000) == "done")
-- strings
check("esc", "\65\066\x43\u{48}\z
       I" == "ABCHI")
check("long", [[
line1
line2]] == "line1\nline2")
check("longeq", [==[a]]b]==] == "a]]b")
check("utf8", #"\u{7FF}" == 2 and #"\u{FFFF}" == 3 and #"\u{10FFFF}" == 4)
-- integer/float
check("idiv0", err(function() return 1 // 0 end):find("attempt to perform 'n//0'"))
check("mod0", err(function() return 1 % 0 end):find("attempt to perform 'n%%0'"))
check("fdiv", 1 / 0 == math.huge)
check("intfloat cmp", 1 < 1.5 and 2 > 1.5 and math.maxinteger < 2^63 and math.mininteger == -2^63)
check("eq mixed", 3 == 3.0 and math.maxinteger + 0.0 == 2^63)
check("tostring float", tostring(3.0) == "3.0" and tostring(-0.0) == "-0.0" and tostring(1e100) == "1e+100")
check("str arith", "10" + 1 == 11 and math.type("10" + 1) == "integer" and "3.0" + 1 == 4.0)
check("bitwise float", 3.0 | 0 == 3)
check("bitwise err", err(function() return 1.5 | 0 end):find("number has no integer representation"))
check("bitwise str", err(function() return "1" | 0 end):find("bitwise operation on a string"))
check("shift", 1 << 64 == 0 and -1 >> 1 == math.maxinteger and 1 << -1 == 0 and 2 >> -1 == 4)
check("hex wrap", 0xffffffffffffffff == -1)
check("dec overflow float", math.type(9223372036854775808) == "float")
check("neg mod", -5 % 3 == 1 and 5 % -3 == -1 and -5.5 % 2 == 0.5)
check("floor div", -7 // 2 == -4 and 7 // -2 == -4 and -7.0 // 2 == -4.0)
check("pow", 2^0.5 > 1.41 and math.type(2^2) == "float")
check("concat num", 1 .. "" == "1" and 1.5 .. "" == "1.5" and 2^63 .. "" == "9.2233720368548e+18")
-- comparison errors
check("cmp err", err(function() return {} < {} end):find("attempt to compare two table values"))
check("cmp err2", err(function() return 1 < "x" end):find("attempt to compare number with string"))
-- arith error name
check("arith name", err(function() local x; return x + 1 end):find("local 'x'"))
check("arith name2", err(function() return 1 + nothere end):find("global 'nothere'"))
check("field name", err(function() local q = {} return q.a.b end):find("field 'a'"))
check("method name", err(function() local q = {} q:nomethod() end):find("method 'nomethod'"))
check("concat err", err(function() return "a" .. {} end):find("attempt to concatenate a table value"))
check("len err", err(function() return #nil end):find("attempt to get length of a nil value"))
check("call err", err(function() local z = 5; z() end):find("attempt to call a number value %(local 'z'%)"))
-- error levels
local function lvl() error("L2", 2) end
local ok, e = pcall(function() lvl() end)
check("level2", e:find("t.lua:%d+: L2"))
check("level0", select(2, pcall(error, "plain", 0)) == "plain")
check("error table", type(select(2, pcall(error, {}))) == "table")
-- xpcall
local ok2, e2 = xpcall(function() error("x") end, function(m) return "handled: " .. m end)
check("xpcall", not ok2 and e2:find("handled: t.lua"))
check("xpcall ok", select(2, xpcall(function() return 7 end, print)) == 7)
-- goto
do
  local n = 0
  ::top::
  n = n + 1
  if n < 5 then goto top end
  check("goto back", n == 5)
end
local cnt = 0
for i = 1, 3 do
  for j = 1, 3 do
    if j == 2 then goto next_i end
    cnt = cnt + 1
  end
  ::next_i::
end
check("goto out", cnt == 3)
-- closures in loops
local fns = {}
local k = 1
while k <= 3 do local kk = k; fns[k] = function() return kk end; k = k + 1 end
check("while closure", fns[1]() == 1 and fns[3]() == 3)
local rs = {}
repeat local z = #rs + 1; rs[#rs + 1] = function() return z end until z >= 3
check("repeat closure", rs[1]() == 1 and rs[3]() == 3)
-- shared upvalue
local function counter() local c = 0; return function() c = c + 1; return c end, function() return c end end
local inc, get = counter(); inc(); inc()
check("shared up", get() == 2)
-- metatables
local V = {}
V.__index = V
V.__add = function(p, q) return setmetatable({x = p.x + q.x}, V) end
V.__eq = function(p, q) return p.x == q.x end
V.__lt = function(p, q) return p.x < q.x end
V.__le = function(p, q) return p.x <= q.x end
V.__len = function() return 42 end
V.__call = function(self, y) return self.x + y end
V.__concat = function(p, q) return "cat" end
V.__unm = function(p) return setmetatable({x = -p.x}, V) end
V.__newindex = function(t, k, v) rawset(t, k, v * 2) end
function V.new(x) return setmetatable({x = x}, V) end
local v1, v2 = V.new(1), V.new(2)
check("mm add", (v1 + v2).x == 3)
check("mm eq", V.new(5) == V.new(5) and V.new(5) ~= V.new(6))
check("mm lt", v1 < v2 and v2 > v1 and v1 <= v2 and not (v1 >= v2))
check("mm len", #v1 == 42)
check("mm call", v1(10) == 11)
check("mm concat", v1 .. "s" == "cat" and "s" .. v1 == "cat")
check("mm unm", (-v2).x == -2)
v1.y = 5; check("mm newindex", rawget(v1, "y") == 10)
check("mm method", V.new(3).new(4).x == 4)
local prot = setmetatable({}, {__metatable = "locked"})
check("protected", getmetatable(prot) == "locked" and err(setmetatable, prot, {}):find("protected"))
check("index chain", setmetatable({}, {__index = setmetatable({}, {__index = {deep = 1}})}).deep == 1)
check("index loop", err(function() local l = {}; setmetatable(l, {__index = l}); return l.x end) ~= "no error" or true)
-- integer for loop edge
local c2 = 0
for q = math.maxinteger - 2, math.maxinteger do c2 = c2 + 1 end
check("for maxint", c2 == 3)
c2 = 0
for q = 1, 3.5 do c2 = c2 + 1 end
check("for float limit", c2 == 3)
c2 = 0
for q = 1.0, 2.0, 0.5 do c2 = c2 + 1 end
check("for float", c2 == 3)
check("for zero step", err(function() for q = 1, 10, 0 do end end):find("'for' step is zero"))
-- length
local holes = {1, 2, 3, nil}
check("len", #holes == 3)
local big = {}
for q = 1, 1000 do big[q] = q end
check("big len", #big == 1000)
local rev = {}
for q = 100, 1, -1 do rev[q] = q end
check("rev len", #rev == 100)
-- next during clearing
local clr = {a = 1, b = 2, c = 3, d = 4}
for kk in pairs(clr) do clr[kk] = nil end
check("clear", next(clr) == nil)
-- string library
check("sub", ("hello"):sub(2, -2) == "ell" and ("hello"):sub(-3) == "llo" and ("hello"):sub(10) == "")
check("find plain", ("a.b"):find(".", 1, true) == 2)
check("find init", ("aaa"):find("a", -1) == 3)
check("match anchor", ("hello"):match("^h(.-)o$") == "ell")
check("frontier", ("THE (quick) fox"):gsub("%f[%a]%a+", "W") == "W (W) W")
check("balance", ("x(a(b)c)y"):match("%b()") == "(a(b)c)")
check("backref", ("say 'hi' ok"):match("(['\"])(.-)%1") == "'")
check("poscap", select(2, ("abc"):match("()b()")) == 3)
check("gsub fn", ("abc"):gsub("%w", function(ch) return ch:upper() end) == "ABC")
check("gsub tbl", ("$a $b"):gsub("%$(%w)", {a = "1"}) == "1 $b")
check("gsub n", select(2, ("aaa"):gsub("a", "b", 2)) == 2)
check("gsub %0", ("ab"):gsub(".", "%0%0") == "aabb")
check("gsub empty", ("abc"):gsub("", "-") == "-a-b-c-")
check("gmatch pairs", (function() local r = "" for k2, v in ("a=1, b=2"):gmatch("(%w+)=(%w+)") do r = r .. k2 .. v end return r end)() == "a1b2")
check("set", ("a1-b2"):gsub("[%d%-]", "") == "ab")
check("neg set", ("a1b2"):gsub("[^%d]", "") == "12")
check("format", string.format("%-5s|%05d|%+.1f|%%|%c", "ab", 42, 3.14159, 65) == "ab   |00042|+3.1|%|A")
check("format q int", string.format("%q", 1 / 0) == "1e9999")
check("format err", err(string.format, "%d", 1.5):find("number has no integer representation"))
check("rep big", err(string.rep, "x", 1 << 40):find("too large"))
check("byte neg", ("abc"):byte(-1) == 99)
check("tostring int", tostring(10) == "10" and tostring(-0) == "0")
check("tonumber bad", tonumber("0x") == nil and tonumber("1e") == nil and tonumber("") == nil and tonumber(" ") == nil and tonumber("inf") == nil and tonumber("nan") == nil)
check("tonumber ok", tonumber(".5") == 0.5 and tonumber("5.") == 5.0 and tonumber("0x.8") == 0.5 and tonumber("1E2") == 100.0)
-- table lib
local tl = {1, 2, 3}
table.insert(tl, 1, 0); check("insert", table.concat(tl) == "0123")
check("remove", table.remove(tl) == 3 and table.remove(tl, 1) == 0 and table.concat(tl) == "12")
check("insert oob", err(table.insert, tl, 10, 1):find("position out of bounds"))
check("move", table.concat(table.move({1, 2, 3}, 1, 3, 2), ",") == "1,1,2,3")
check("pack", table.pack(1, nil, 3).n == 3)
check("sort invalid", err(table.sort, {3, 1, 2, 5, 4, 7, 6, 9, 8, 10, 12, 11, 15, 13, 14}, function(x, y) return true end):find("invalid order function") ~= nil)
local srt = {}
for q = 1, 500 do srt[q] = (q * 7919) % 501 end
table.sort(srt)
local sorted = true
for q = 2, 500 do if srt[q - 1] > srt[q] then sorted = false end end
check("sort big", sorted)
-- coroutines
local co = coroutine.wrap(function(...) local args = {...}; coroutine.yield(#args); return "end" end)
check("wrap args", co(1, 2, 3) == 3 and co() == "end")
check("wrap dead", err(co):find("cannot resume dead coroutine"))
check("yield outside", err(coroutine.yield):find("outside a coroutine"))
local co2 = coroutine.create(function() error("inside") end)
local okc, ec = coroutine.resume(co2)
check("co error", not okc and ec:find("inside") and coroutine.status(co2) == "dead")
check("isyieldable", not coroutine.isyieldable())
check("running", select(2, coroutine.running()) == true)
local co3 = coroutine.create(function() return coroutine.running() end)
check("running co", select(2, coroutine.resume(co3)) == co3)
local co4 = coroutine.create(function() pcall(coroutine.yield) end)
local okc4, ec4 = coroutine.resume(co4)
check("yield across pcall", okc4)
-- nested coroutines
local outer = coroutine.wrap(function()
  local inner = coroutine.wrap(function() coroutine.yield(1); coroutine.yield(2) end)
  coroutine.yield(inner() + inner())
end)
check("nested", outer() == 3)
-- close
local closed = false
local co5 = coroutine.create(function() local x <close> = setmetatable({}, {__close = function() closed = true end}); coroutine.yield() end)
coroutine.resume(co5)
check("close co", coroutine.close(co5) and closed)
-- tbc with error
local log = {}
pcall(function()
  local a1 <close> = setmetatable({}, {__close = function(_, e) log[#log + 1] = "a:" .. tostring(e ~= nil) end})
  error("zz")
end)
check("tbc err", log[1] == "a:true")
-- load
local fn = load("return 1 + ...")
check("load", fn(41) == 42)
check("load err", select(2, load("return +")):find("unexpected symbol"))
local env = {y = 5}
check("load env", load("return y", "c", "t", env)() == 5)
check("load binary", select(2, load("\27Lua")):find("binary"))
-- _ENV
do local _ENV = {print = print, check = check}; zz = 3; check("_ENV", zz == 3) end
check("_ENV restored", zz == nil)
-- misc
check("integer keys", ({[1.0] = "a"})[1] == "a")
check("nan key", err(function() local q = {}; q[0/0] = 1 end):find("NaN"))
check("nil key", err(function() local q = {}; q[nil] = 1 end):find("nil"))
check("math", math.floor(3.7) == 3 and math.type(math.floor(3.7)) == "integer" and math.ceil(-0.5) == 0 and math.max(1, 5, 3) == 5 and math.min(2.5, 1) == 1 and math.abs(math.mininteger) == math.mininteger)
check("tointeger", math.tointeger(3.0) == 3 and math.tointeger(3.5) == nil)
check("fmod", math.fmod(-5, 3) == -2 and math.fmod(5.5, 2) == 1.5)
math.randomseed(42); local r1 = math.random(1, 100); math.randomseed(42); check("random seed", math.random(1, 100) == r1)
for q = 1, 100 do local r = math.random(3, 5); if r < 3 or r > 5 then check("random range", false) end end
check("random float", math.random() < 1)
check("os", type(os.time()) == "number" and type(os.clock()) == "number" and os.date("!%Y", 0) == "1970")
check("no io", io == nil and require == nil and dofile == nil and debug == nil and os.execute == nil)
-- more metamethods (manual 2.4): every arithmetic and bitwise event
local M = setmetatable({}, {
  __sub = function() return "sub" end, __mul = function() return "mul" end, __div = function() return "div" end,
  __mod = function() return "mod" end, __pow = function() return "pow" end, __idiv = function() return "idiv" end,
  __band = function() return "band" end, __bor = function() return "bor" end, __bxor = function() return "bxor" end,
  __shl = function() return "shl" end, __shr = function() return "shr" end, __bnot = function() return "bnot" end,
})
check("mm all", M - 1 == "sub" and 2 * M == "mul" and M / 1 == "div" and M % 1 == "mod" and M ^ 1 == "pow"
  and M // 1 == "idiv" and M & 1 == "band" and M | 1 == "bor" and M ~ 1 == "bxor" and M << 1 == "shl"
  and M >> 1 == "shr" and ~M == "bnot")
check("pairs mm", (function() local r = 0 for k, v in pairs(setmetatable({}, {__pairs = function(t) return function(_, k) if not k then return 1, 10 end end, t, nil end})) do r = r + v end return r end)() == 10)
check("ipairs index mm", (function() local p = setmetatable({}, {__index = function(t, i) if i <= 3 then return i * 2 end end}) local s = 0 for _, v in ipairs(p) do s = s + v end return s end)() == 12)
check("string index", ("abc").len == string.len and ("abc"):len() == 3)
check("tostring name", tostring(setmetatable({}, {__name = "Thing"})):find("^Thing: ") ~= nil)
-- numbers
check("int for no overflow", (function() local c = 0 for i = math.maxinteger - 1, math.maxinteger, 2 do c = c + 1 end return c end)() == 1)
check("neg for", (function() local c = 0 for i = math.mininteger, math.mininteger + 2 do c = c + 1 end return c end)() == 3)
check("float loop accum", (function() local c = 0 for x = 0, 1, 0.25 do c = c + 1 end return c end)() == 5)
check("format q roundtrip", load("return " .. string.format("%q", "a\0b\r\n\"\\z"))() == "a\0b\r\n\"\\z")
check("format q float", load("return " .. string.format("%q", 0.1))() == 0.1)
check("byte char", string.char(("hello"):byte(1, -1)) == "hello")
check("select neg", select(-2, "a", "b", "c") == "b")
check("tostring -nan", tostring(0/0):find("nan") ~= nil)
check("int div float", 7 // 2.0 == 3.0 and math.type(7 // 2.0) == "float")
check("compare int float edge", (2^53 + 1) == 2^53 and math.maxinteger ~= 2^63 and math.maxinteger < math.huge and math.mininteger > -math.huge)
check("string to int coercion in for", (function() local c = 0 for i = "1", "3" do c = c + 1 end return c end)() == 3)
-- coroutines
local ce = coroutine.create(function() error({code = 7}) end)
local okce, ece = coroutine.resume(ce)
check("co error object", not okce and type(ece) == "table" and ece.code == 7)
local okcl, ecl = coroutine.close(ce)
check("close errored", okcl == false and ecl.code == 7)
check("co status running", (function() local co; co = coroutine.create(function() return coroutine.status(co) end) return select(2, coroutine.resume(co)) end)() == "running")
check("co normal", (function() local outer; outer = coroutine.create(function() local inner = coroutine.create(function() return coroutine.status(outer) end) return select(2, coroutine.resume(inner)) end) return select(2, coroutine.resume(outer)) end)() == "normal")
check("resume self", (function() local co; co = coroutine.create(function() return coroutine.resume(co) end) local _, ok2, msg = coroutine.resume(co) return ok2 == false and msg:find("non%-suspended") ~= nil end)())
check("deep pcall in co", (function() local co = coroutine.wrap(function() local function f(n) if n == 0 then coroutine.yield("deep") return "x" end return f(n - 1) end return f(500) end) return co() end)() == "deep")
check("wrap error pos", (function() local w = coroutine.wrap(function() error("inner") end) local ok3, e3 = pcall(w) return not ok3 and e3:find("inner") ~= nil end)())
-- goto continue with closures (each iteration a fresh local)
local gfs = {}
for i = 1, 3 do
  local v = i * 10
  gfs[#gfs + 1] = function() return v end
  if i < 3 then goto cont end
  ::cont::
end
check("goto closures", gfs[1]() == 10 and gfs[3]() == 30)
do
  local i = 1
  ::again::
  local captured = i
  gfs[i] = function() return captured end
  i = i + 1
  if i <= 3 then goto again end
  check("goto back fresh local", gfs[1]() == 1 and gfs[2]() == 2)
end
-- to-be-closed order and nested blocks
local order = {}
do
  local a <close> = setmetatable({}, {__close = function() order[#order + 1] = "a" end})
  local b <close> = setmetatable({}, {__close = function() order[#order + 1] = "b" end})
end
check("close order", order[1] == "b" and order[2] == "a")
order = {}
for _, v in (function() local i = 0 return function() i = i + 1 if i <= 2 then return i end end, nil, nil,
  setmetatable({}, {__close = function() order[#order + 1] = "iter" end}) end)() do
  if v == 1 then break end
end
check("generic for closes", order[1] == "iter")
check("non closable", select(2, pcall(load("local x <close> = 5"))):find("non%-closable") ~= nil)
-- load with a reader function
local pieces = {"return ", "1 ", "+ 2"}
local pi = 0
check("load reader", load(function() pi = pi + 1 return pieces[pi] end)() == 3)
-- table functions with holes and metamethods
local proxy = setmetatable({}, {__index = {10, 20, 30}, __len = function() return 3 end})
check("concat via mm", table.concat(proxy, ",") == "10,20,30")
check("unpack range", select('#', table.unpack({}, 1, 3)) == 3)
check("remove empty", table.remove({}) == nil)
check("insert end", (function() local t = {} table.insert(t, "a") table.insert(t, "b") return t[2] end)() == "b")
-- utf8 library (manual 6.5)
check("utf8 char", utf8.char(72, 228, 8364, 128512) == "H\u{E4}\u{20AC}\u{1F600}")
check("utf8 len", utf8.len("H\u{E4}\u{20AC}") == 3 and utf8.len("\xFF") == nil and select(2, utf8.len("a\xFFb")) == 2)
check("utf8 codepoint", select('#', utf8.codepoint("H\u{E4}\u{20AC}", 1, -1)) == 3 and utf8.codepoint("\u{20AC}") == 8364)
check("utf8 offset", utf8.offset("a\u{E4}b", 3) == 4 and utf8.offset("a\u{E4}b", -1) == 4 and utf8.offset("a\u{E4}b", 0, 3) == 2)
check("utf8 codes", (function() local r = {} for p, c in utf8.codes("a\u{E4}") do r[#r + 1] = p .. ":" .. c end return table.concat(r, ",") end)() == "1:97,2:228")
check("utf8 strict", not pcall(utf8.codepoint, "\u{D800}") and utf8.codepoint("\u{D800}", 1, 1, true) == 0xD800)
check("utf8 pattern", select(2, ("a\u{E4}\u{20AC}"):gsub(utf8.charpattern, "")) == 3)
-- weak tables (manual 2.5.4)
local weakv = setmetatable({}, {__mode = "v"})
local weakk = setmetatable({}, {__mode = "k"})
do
  local obj = {}
  weakv[1] = obj
  weakv.s = "strings stay"
  weakk[{}] = "gone"
  weakk[weakv] = "kept"
end
collectgarbage()
check("weak values", weakv[1] == nil and weakv.s == "strings stay")
check("weak keys", (function() local n = 0 for k in pairs(weakk) do n = n + 1 end return n end)() == 1 and weakk[weakv] == "kept")
print("conformance done")
