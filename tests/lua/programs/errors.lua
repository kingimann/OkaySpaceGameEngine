-- Errors: messages with chunk:line, levels, error objects, xpcall handlers, errors from the
-- libraries, and syntax errors reported by load with their line.
local function check_positive(n)
  if type(n) ~= "number" then error("expected a number, got " .. type(n), 2) end
  if n <= 0 then error({code = "NEG", value = n}) end
  return n
end
print(pcall(check_positive, 5))
print(pcall(function() return check_positive("x") end))
local ok, e = pcall(check_positive, -3)
print(ok, e.code, e.value)
print(pcall(error))
print(pcall(error, "no position", 0))
print(pcall(error, nil))

local function divide(a, b)
  if b == 0 then error("division by zero") end
  return a // b
end
print(xpcall(divide, function(m) return "handled: " .. m end, 7, 0))
print(xpcall(divide, function(m) return "handled: " .. m end, 7, 2))

local function nested() local t = {} return t.a.b end
print(pcall(nested))
print(pcall(function() return 1 + nil end))
print(pcall(function() local s = "abc" return s + 1 end))
print(pcall(function() return #nil end))
print(pcall(function() return {} < {} end))
print(pcall(function() return math.floor({}) end))
print(pcall(function() return ("x"):rep(1, 2, 3) .. nil end))
print(pcall(function() undefined_function(1) end))
print(pcall(function() local obj = {} obj:missing() end))
print(pcall(string.format, "%d", 1.5))
print(pcall(function() return 1 // 0 end))
print(pcall(function() return tostring() end))
print(pcall(setmetatable, {}, 1))

print(load("x = = 1", "=cfg"))
print(load("local a = 1\nlocal b = \n\nif then end", "=multi"))
print(load("return 'unfinished", "=str"))
print(load("for i = 1 do end", "=for"))
print(load("goto nowhere", "=goto"))
print(load("x = 1", "=bin", "b"))
local f = load("local a, b = ... return a * b", "=mul")
print(f(6, 7))
print(pcall(load("error('inside chunk')", "=chunk")))
print(select("#", pcall(error)))
local depth = 0
local function recurse() depth = depth + 1 return recurse() + 1 end
local rok, rerr = pcall(recurse)
print(rok, rerr:match("stack overflow") ~= nil, depth > 1000)
