-- Headless self-check: make test
local function near(a, b, eps) return math.abs(a - b) <= (eps or 1) end

function oe.load()
  -- A ball dropped on a static floor comes to rest on top of it.
  local world = oe.physics.newWorld(0, 900)
  local floor = world:newRectangle(400, 500, 800, 20, "static")
  local ball = world:newCircle(400, 100, 10)
  local hits = 0
  world:setCallback(function(a, b, nx, ny)
    assert(a == floor and b == ball or a == ball and b == floor, "callback got the wrong bodies")
    assert(near(math.abs(ny), 1, 1e-3) and near(nx, 0, 1e-3), "normal should be vertical")
    hits = hits + 1
  end)
  for _ = 1, 180 do world:update(1 / 60) end
  local _, y = ball:getPosition()
  assert(near(y, 480, 1), "ball should rest on floor, y=" .. y)
  assert(hits > 0, "collision callback never fired")

  -- A box stacked on another box rests on top of it.
  world:setCallback(nil)
  local lower = world:newRectangle(200, 470, 40, 40)
  local upper = world:newRectangle(200, 300, 40, 40)
  for _ = 1, 240 do world:update(1 / 60) end
  local _, ly = lower:getPosition()
  local _, uy = upper:getPosition()
  assert(near(ly, 470, 2) and near(uy, 430, 3), ("stack: lower=%g upper=%g"):format(ly, uy))

  -- A bouncy ball bounces back up.
  local w2 = oe.physics.newWorld(0, 0)
  w2:newRectangle(0, 100, 100, 10, "static")
  local b = w2:newCircle(0, 50, 5)
  b:setRestitution(1)
  b:setVelocity(0, 200)
  for _ = 1, 30 do w2:update(1 / 60) end
  local _, vy = b:getVelocity()
  assert(near(vy, -200, 5), "elastic bounce, vy=" .. vy)

  -- Destroying bodies inside the callback is safe, and destroyed bodies stop colliding.
  local w3 = oe.physics.newWorld(0, 0)
  local wall = w3:newRectangle(0, 0, 10, 100, "static")
  local balls = {}
  for i = 1, 5 do balls[i] = w3:newCircle(0, (i - 3) * 15, 6) end
  local calls = 0
  w3:setCallback(function(x, y) calls = calls + 1; (x == wall and y or x):destroy() end)
  w3:update(1 / 60)
  assert(calls >= 1)
  collectgarbage()
  calls = 0
  w3:update(1 / 60)
  assert(calls == 0, "destroyed bodies should not collide")

  assert(not pcall(world.newCircle, world, 0, 0, -1), "negative radius should error")
  assert(not pcall(world.newCircle, world, 0, 0, 1, "kinematic"), "bad type should error")

  oe.graphics.print("x", 1, 2)  -- 3 args: regression for tolstring clobbering arg 4

  -- Audio.
  local snd = oe.audio.newSound("examples/physics/bounce.wav")
  snd:setVolume(0.5)
  snd:play()
  assert(snd:isPlaying())
  snd:stop()
  assert(not snd:isPlaying())
  snd:setLooping(true)
  snd:play()
  assert(not pcall(oe.audio.newSound, "missing.wav"))
  looping = snd
end

local t = 0
function oe.update(dt)
  t = t + dt
  if t > 0.5 then  -- longer than bounce.wav: still playing only because it loops
    assert(looping:isPlaying(), "looping sound stopped")
    looping:setLooping(false)
    looping:stop()
    print("all checks passed")
    oe.quit()
  end
end
