-- Text processing with the string library: patterns, captures, gsub with functions and tables, format.
local text = [[
The quick brown fox jumps over the lazy dog.
The dog sleeps; the fox runs!  Numbers: 42, 3.14, -7 and 0x1F.
]]
local freq = {}
for word in text:gmatch("%a+") do
  local w = word:lower()
  freq[w] = (freq[w] or 0) + 1
end
local words = {}
for w in pairs(freq) do words[#words + 1] = w end
table.sort(words, function(a, b) if freq[a] ~= freq[b] then return freq[a] > freq[b] end return a < b end)
for i = 1, 4 do print(string.format("%-6s %d", words[i], freq[words[i]])) end

local nums = {}
for n in text:gmatch("%-?%d+%.?%d*") do nums[#nums + 1] = n end
print(table.concat(nums, "|"))
print(text:match("Numbers: (%d+), ([%d%.]+)"))
print((text:gsub("%s+", " ")):sub(1, 30) .. "...")
print(("hello world"):gsub("o", "0", 1))
print(("snake_case_name"):gsub("_(%a)", function(c) return c:upper() end))
print(("$x + $y = $z"):gsub("%$(%a)", {x = 1, y = 2, z = "three"}))
print(("  padded  "):match("^%s*(.-)%s*$") .. "!")
print(string.format("%5.2f|%-8s|%03d|%x|%q", 3.14159, "left", 7, 3054, "say \"hi\"\n"))
print(("a,b,,c"):find(",,", 1, true), ("key = value"):match("^(%w+)%s*=%s*(%w+)$"))
local parts = {}
for field in ("one;two;;four"):gmatch("([^;]*)") do parts[#parts + 1] = "[" .. field .. "]" end
print(table.concat(parts))
print(("%d%%"):format(50), ("abc"):rep(2, "-"), ("Hello"):byte(1, 3))
print(string.char(76, 117, 97), ("lua"):upper():reverse(), #"\u{263A}", ("x"):len())
print(("THE (quick) fox"):find("%((%a+)%)"))
print(("f(a(b)c)d"):match("%b()"), ("hello"):find("l+"), ("hello"):match(".-(l+)(.*)"))
print(("one two  three"):gsub("(%w+)", "<%1>"))
print(("abc"):gsub(".", {a = 1, b = false}), pcall(string.rep, "x", -1))
