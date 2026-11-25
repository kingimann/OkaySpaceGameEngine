-- Bobber: a coroutine moves the cube up, waits, and moves it down again, forever.
local Bobber = { height = 1.5, seconds = 1 }

-- yields until t seconds have passed; returns the time it waited
local function wait(t)
    local waited = coroutine.yield()
    while waited < t do waited = waited + coroutine.yield() end
    return waited
end

function Bobber:Start()
    local start = self.transform.position
    self.job = coroutine.wrap(function()
        while true do
            local t = 0
            while t < self.seconds do
                t = t + wait(0)
                local k = math.min(t / self.seconds, 1)
                self.transform.position = start + Vector3.up * (k * self.height)
            end
            wait(0.5)
            self.transform.position = start
            wait(0.5)
        end
    end)
end

function Bobber:Update(dt)
    self.job(dt)
end

return Bobber
