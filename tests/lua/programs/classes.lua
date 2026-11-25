-- Classes with inheritance through __index, closures as private state, varargs and multiple returns.
local Animal = {}
Animal.__index = Animal
function Animal.new(name, sound)
  local self = setmetatable({}, Animal)
  self.name, self.sound = name, sound
  return self
end
function Animal:speak() return self.name .. " says " .. self.sound end
function Animal:__tostring() return "Animal(" .. self.name .. ")" end
Animal.__tostring = Animal.__tostring

local Dog = setmetatable({}, {__index = Animal})
Dog.__index = Dog
Dog.__tostring = function(d) return "Dog(" .. d.name .. ", " .. d.tricks .. " tricks)" end
function Dog.new(name)
  local self = Animal.new(name, "woof")
  self.tricks = 0
  return setmetatable(self, Dog)
end
function Dog:learn(...)
  local n = select("#", ...)
  self.tricks = self.tricks + n
  return n, self.tricks
end

local a = Animal.new("Cat", "meow")
local d = Dog.new("Rex")
print(a:speak())
print(d:speak())
print(d:learn("sit", "roll", "beg"))
print(tostring(a), tostring(d))
print(getmetatable(d) == Dog, rawget(Dog, "speak") == nil, Dog.speak == Animal.speak)

local function account(balance)
  local log = {}
  local function deposit(n) balance = balance + n; log[#log + 1] = "+" .. n end
  local function withdraw(n)
    if n > balance then return nil, "insufficient funds" end
    balance = balance - n; log[#log + 1] = "-" .. n
    return balance
  end
  return {deposit = deposit, withdraw = withdraw, balance = function() return balance end,
          history = function() return table.concat(log, " ") end}
end
local acc = account(100)
acc.deposit(50)
print(acc.withdraw(30))
print(acc.withdraw(500))
print(acc.balance(), acc.history())

local function stats(...)
  local t = {...}
  local sum, min, max = 0, math.huge, -math.huge
  for _, v in ipairs(t) do sum = sum + v; min = math.min(min, v); max = math.max(max, v) end
  return sum, sum / #t, min, max
end
print(stats(4, 8, 15, 16, 23, 42))
local s, avg = stats(1, 2)
print(s, avg, (stats(10)))
print(math.type(s), math.type(avg))
