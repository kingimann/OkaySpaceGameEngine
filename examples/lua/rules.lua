-- Rules: no mesh, only settings. Change them in the Inspector; Start prints them to the Console.
local Rules = {
    title = "Lua demo",
    lives = 3,
    startPosition = Vector3.new(0, 0.5, 2),
    hardMode = false,
}

function Rules:Start()
    print(string.format("%s: %d lives, start at %s, hard mode %s",
        self.title, self.lives, tostring(self.startPosition), tostring(self.hardMode)))
end

return Rules
