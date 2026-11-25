-- Numbers, tables, utf8, goto and the safe os functions: integer/float subtypes, conversions,
-- table.sort/insert/remove/move/unpack, a tiny serializer with sorted keys, utf8 iteration.
print(7 // 2, 7.0 // 2, 7 / 2, 7 % 3, -7 % 3, 7 % -3, 2^10, 10 // 0.0)
print(math.maxinteger + 1 == math.mininteger, math.maxinteger + 1.0, 1e308 * 10, -(0/0) ~= -(0/0))
print(math.type(3), math.type(3.0), math.type("3"), math.tointeger(5.0), math.tointeger("8"), 3 == 3.0)
print(0x7fffffffffffffff, 0xff, 1e3, 5 // 1, 5.5 // 1, string.format("%.0f", 0.5), string.format("%.0f", 1.5))
print(tonumber("  42  "), tonumber("4e2"), tonumber("0x1p-1"), tonumber("abc"), tonumber("11", 2), tonumber("zz", 36))
print(10 == "10", "10" + 0 == 10, 1 < 2, "a" < "b", math.ult(-1, 1), 3 | 4, 6 & 3, 5 ~ 1, ~0, 1 << 62, 256 >> 4)
print(math.abs(-5), math.max(3, 9, 1), math.min(2.5, -1), math.floor(-2.5), math.ceil(2.1), math.fmod(7, 3), math.sqrt(2) > 1.41)
for i = 3, 1, -1 do io_out = (io_out or "") .. i end
for x = 0.5, 1.5, 0.5 do io_out = io_out .. " " .. x end
print(io_out)

local list = {"pear", "apple", "fig", "banana", "cherry"}
table.sort(list)
print(table.concat(list, ","))
table.sort(list, function(a, b) return #a < #b or (#a == #b and a < b) end)
print(table.concat(list, ","))
table.insert(list, "kiwi"); table.insert(list, 1, "date")
print(#list, list[1], list[#list], table.remove(list), table.remove(list, 1), #list)
print(table.unpack({1, 2, 3}))
print(table.concat(table.move({1, 2, 3, 4}, 2, 4, 1), " "), table.pack("a", nil, "c").n)
print(select("#", table.unpack({}, 1, 2)), table.concat({}, "x"), table.concat({1, 2.5, "s"}, ";"))

local function serialize(v, indent)
  indent = indent or ""
  if type(v) == "table" then
    local keys = {}
    for k in pairs(v) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
    local out = {}
    for _, k in ipairs(keys) do out[#out + 1] = indent .. "  " .. tostring(k) .. " = " .. serialize(v[k], indent .. "  ") end
    return "{\n" .. table.concat(out, ",\n") .. "\n" .. indent .. "}"
  elseif type(v) == "string" then return string.format("%q", v)
  else return tostring(v) end
end
print(serialize({name = "Rex", age = 3, weight = 12.5, tags = {"a", "b"}, ok = true}))

local s = "Grüße, 世界!"
print(#s, utf8.len(s), utf8.char(0x48, 0x49), utf8.codepoint(s, 3), utf8.offset(s, 4))
local cps = {}
for p, c in utf8.codes("añ€") do cps[#cps + 1] = p .. "=" .. c end
print(table.concat(cps, " "), utf8.len("\xC3"), #utf8.char(0x10FFFF))

local i = 1
::top::
if i <= 3 then i = i + 1 goto top end
print("goto loop", i)
for _, v in ipairs({1, 2, 3, 4}) do
  if v % 2 == 0 then goto continue end
  print("odd", v)
  ::continue::
end
print(type(os.time()), type(os.clock()), os.clock() >= 0, os.time({year = 2000, month = 1, day = 1, hour = 0}) > 0)
print(os.date("!%Y-%m-%d %H:%M:%S", 86400 + 3661), os.difftime(10, 4), os.date("!*t", 0).year)
print(io, os.execute, os.exit, os.getenv, os.remove, os.rename, require, dofile, loadfile, package, debug)
