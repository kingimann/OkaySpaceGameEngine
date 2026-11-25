-- Spinner: turns around Y. "speed" is a setting you can change in the Inspector.
local Spinner = {
    speed = 90,   -- degrees per second
}

function Spinner:Update(dt)
    self.transform:Rotate(0, self.speed * dt, 0)
end

return Spinner
