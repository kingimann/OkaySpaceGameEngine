-- Coroutines: producer/consumer, generators with wrap, status changes, errors inside, close, yield
-- inside pcall and inside nested coroutines.
local function producer(items)
  return coroutine.create(function()
    for _, item in ipairs(items) do coroutine.yield(item) end
    return "done"
  end)
end
local function filter(src, pred)
  return coroutine.create(function()
    while true do
      local ok, v = coroutine.resume(src)
      if coroutine.status(src) == "dead" then return v end
      if pred(v) then coroutine.yield(v) end
    end
  end)
end
local f = filter(producer({1, 2, 3, 4, 5, 6}), function(x) return x % 2 == 0 end)
local got = {}
while true do
  local ok, v = coroutine.resume(f)
  if coroutine.status(f) == "dead" then got[#got + 1] = "(" .. tostring(v) .. ")" break end
  got[#got + 1] = v
end
print(table.concat(got, " "))

local function range(n, step)
  return coroutine.wrap(function() for i = 1, n, step or 1 do coroutine.yield(i, i * i) end end)
end
for i, sq in range(7, 3) do io_write = (io_write or "") .. i .. ":" .. sq .. " " end
print(io_write)

local co = coroutine.create(function(a)
  print("got", a)
  local b, c = coroutine.yield(a * 2)
  print("got", b, c)
  local ok, e = pcall(function()
    local x = coroutine.yield("in pcall")
    error("failed with " .. x)
  end)
  print("pcall", ok, e)
  return "finished"
end)
print(coroutine.status(co))
print(coroutine.resume(co, 5))
print(coroutine.resume(co, "x", "y"))
print(coroutine.resume(co, "boom"))
print(coroutine.status(co), coroutine.resume(co))

local bad = coroutine.create(function() local t = nil; return t.field end)
print(coroutine.resume(bad))
local wrapped = coroutine.wrap(function() error({code = 7}) end)
local ok, e = pcall(wrapped)
print(ok, type(e), e.code)

local inner_log = {}
local outer = coroutine.wrap(function()
  local inner = coroutine.wrap(function()
    for i = 1, 2 do inner_log[#inner_log + 1] = "inner" .. i; coroutine.yield(i) end
  end)
  coroutine.yield("o" .. inner())
  coroutine.yield("o" .. inner())
  return "end"
end)
print(outer(), outer(), outer(), table.concat(inner_log, ","))

local self_co
self_co = coroutine.create(function()
  print(coroutine.running() == self_co, coroutine.isyieldable(), coroutine.status(self_co))
  coroutine.yield()
end)
coroutine.resume(self_co)
print(coroutine.close(self_co), coroutine.status(self_co))
print(coroutine.resume(coroutine.running()))
print(select("#", coroutine.running()), coroutine.isyieldable())
local deep = coroutine.wrap(function(...)
  local args = table.pack(...)
  while true do args = table.pack(coroutine.yield(args.n)) end
end)
print(deep(1, 2, 3), deep(), deep(nil, nil))
