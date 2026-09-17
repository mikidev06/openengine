-- Physics + audio. Run: ./openengine examples/physics
-- Left click: ball, right click: box, space: shake, escape: quit.
local W, H = 800, 600
local world, bounce
local bodies = {}

local function spawn(x, y, kind)
  local b = kind == "box" and world:newRectangle(x, y, math.random(20, 50), math.random(20, 50))
                           or world:newCircle(x, y, math.random(10, 25))
  b:setRestitution(0.4)
  bodies[#bodies + 1] = b
end

function oe.load()
  oe.window.setTitle("openengine physics")
  oe.window.setSize(W, H)
  world = oe.physics.newWorld(0, 900)
  world:newRectangle(W / 2, H - 10, W, 20, "static")
  world:newRectangle(10, H / 2, 20, H, "static")
  world:newRectangle(W - 10, H / 2, 20, H, "static")
  world:newRectangle(W / 2, 380, 300, 16, "static")

  bounce = oe.audio.newSound("bounce.wav")
  world:setCallback(function(a, b, nx, ny)
    local vx, vy = a:getVelocity()
    local wx, wy = b:getVelocity()
    if math.abs((wx - vx) * nx + (wy - vy) * ny) > 150 then bounce:play() end
  end)

  for i = 1, 20 do spawn(math.random(100, W - 100), math.random(0, 200), i % 2 == 0 and "box" or "ball") end
end

function oe.update(dt)
  world:update(dt)
end

function oe.draw()
  oe.graphics.clear(0.1, 0.1, 0.14)
  oe.graphics.setColor(0.4, 0.4, 0.5)
  oe.graphics.rectangle("fill", 0, H - 20, W, 20)
  oe.graphics.rectangle("fill", 0, 0, 20, H)
  oe.graphics.rectangle("fill", W - 20, 0, 20, H)
  oe.graphics.rectangle("fill", W / 2 - 150, 372, 300, 16)

  for _, b in ipairs(bodies) do
    local x, y = b:getPosition()
    if b:getShape() == "circle" then
      oe.graphics.setColor(0.9, 0.6, 0.3)
      oe.graphics.circle("fill", x, y, b:getSize())
    else
      local w, h = b:getSize()
      oe.graphics.setColor(0.3, 0.7, 0.9)
      oe.graphics.rectangle("fill", x - w / 2, y - h / 2, w, h)
    end
  end
  oe.graphics.setColor(1, 1, 1)
  oe.graphics.print("left click: ball  right click: box  space: shake  (" .. #bodies .. " bodies)", 30, 30)
end

function oe.mousepressed(x, y, button)
  spawn(x, y, button == 1 and "ball" or "box")
end

function oe.keypressed(key)
  if key == "escape" then oe.quit() end
  if key == "space" then
    for _, b in ipairs(bodies) do b:setVelocity(math.random(-300, 300), -600) end
  end
end
