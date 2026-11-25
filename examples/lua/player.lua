-- Player: W A S D (or a controller stick) move the ball; Space jumps it up and down.
local Player = {
    speed = 6,
    jumpHeight = 2,
}

function Player:Start()
    self.home = self.transform.position
    self.hop = 0
    Debug.Log("Lua player ready on " .. self.gameObject.name)
end

function Player:Update(dt)
    local move = Vector3.new(Input.GetAxis("Horizontal"), 0, Input.GetAxis("Vertical"))
    self.transform:Translate(move * (self.speed * dt))
    if Input.GetKeyDown("space") and self.hop <= 0 then self.hop = 1 end
    if self.hop > 0 then
        self.hop = math.max(0, self.hop - dt * 2)
        local p = self.transform.position
        p.y = self.home.y + math.sin(self.hop * math.pi) * self.jumpHeight
        self.transform.position = p
    end
end

return Player
