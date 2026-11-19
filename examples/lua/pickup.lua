-- Lua example: a pickup that bobs up and down, spins, and is collected when the Player gets close.
-- The global variables below are the script's public fields: the Inspector shows them and a saved
-- scene or save game keeps them.
speed = 90          -- spin, in degrees per second
height = 0.5        -- how far it bobs up and down
pick_distance = 1.5 -- how close the Player must be
age = 0
collected = false

function Start()
  origin = transform.position
  Log.info("Pickup ready at " .. tostring(origin))
end

function Update(dt)
  age = age + dt
  transform.position = origin + Vector3.new(0, math.sin(age * 2) * height, 0)
  transform:Rotate(Vector3.new(0, speed * dt, 0))

  local player = Find("Player")
  if player and not collected then
    local d = Vector3.Distance(player.transform.position, origin)
    if d < pick_distance then
      collected = true
      Log.info("Picked up!")
      Destroy(gameObject, 0.1)
    end
  end
end
