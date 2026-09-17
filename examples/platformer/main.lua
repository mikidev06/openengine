-- Platformer. Run: ./openengine examples/platformer
-- Arrows/A-D: move, space/up/W: jump, R: restart, escape: quit. Collect every coin.
local W, H, TILE = 800, 600, 32
local SPEED, JUMP, GRAVITY = 260, 620, 1500
local COYOTE = 0.1  -- seconds you can still jump after walking off a ledge

-- # = ground, C = coin, P = player start
local LEVEL = {
  "                                                                ",
  "                                                                ",
  "                                                                ",
  "                                                                ",
  "                     C  C                              C        ",
  "                    ######                            ####      ",
  "                                                                ",
  "               C                                 C              ",
  "              ####                              ####            ",
  "                                                                ",
  "         C                   C                                  ",
  "        ####                ####          ####                  ",
  "                                                                ",
  "    C                               C                      C    ",
  "   ####         ##        ####     ####    ####           ####  ",
  "                                                                ",
  " P                      C                                       ",
  "#########  ############   ##############  #########  ###########",
}
local LEVEL_W = #LEVEL[1] * TILE

local world, player, start, ground, coins, left, camX, sinceGround, won
local jumpSound, coinSound = oe.audio.newSound("jump.wav"), oe.audio.newSound("coin.wav")

local function isGround(body) return ground[body] end

local function restart()
  world = oe.physics.newWorld(0, GRAVITY)
  ground, coins, camX, sinceGround, won = {}, {}, 0, math.huge, false

  for row, line in ipairs(LEVEL) do
    local y = (row - 1) * TILE
    -- Merge each run of # into one rectangle so the player can't snag on tile seams.
    for s, e in line:gmatch("()#+()") do
      local w = (e - s) * TILE
      ground[world:newRectangle((s - 1) * TILE + w / 2, y + TILE / 2, w, TILE, "static")] = { x = (s - 1) * TILE, y = y, w = w }
    end
    for col in line:gmatch("()C") do
      coins[#coins + 1] = { x = (col - 1) * TILE + TILE / 2, y = y + TILE / 2 }
    end
    local p = line:find("P")
    if p then start = { x = (p - 1) * TILE + TILE / 2, y = y + TILE / 2 } end
  end
  left = #coins

  -- Invisible walls at the level edges.
  world:newRectangle(-10, H / 2, 20, H * 2, "static")
  world:newRectangle(LEVEL_W + 10, H / 2, 20, H * 2, "static")

  player = world:newRectangle(start.x, start.y, 22, 30)
  player:setFriction(0)  -- we set horizontal speed directly; friction would make walls sticky

  world:setCallback(function(a, b, nx, ny)
    -- Normal points a -> b, so the player is standing on ground when it points up at them.
    if (b == player and isGround(a) and ny < -0.5) or (a == player and isGround(b) and ny > 0.5) then
      sinceGround = 0
    end
  end)
end

function oe.load()
  oe.window.setTitle("openengine platformer")
  oe.window.setSize(W, H)
  restart()
end

local function down(...)
  for _, k in ipairs({ ... }) do if oe.keyboard.isDown(k) then return true end end
  return false
end

function oe.update(dt)
  local dir = (down("right", "d") and 1 or 0) - (down("left", "a") and 1 or 0)
  local _, vy = player:getVelocity()
  player:setVelocity(dir * SPEED, vy)

  sinceGround = sinceGround + dt
  world:update(dt)

  local px, py = player:getPosition()
  if py > H + 100 then  -- fell in a pit
    player:setPosition(start.x, start.y)
    player:setVelocity(0, 0)
  end

  for _, c in ipairs(coins) do
    if not c.taken and math.abs(px - c.x) < 22 and math.abs(py - c.y) < 26 then
      c.taken = true
      left = left - 1
      won = left == 0
      coinSound:play()
    end
  end

  camX = math.max(0, math.min(LEVEL_W - W, px - W / 2))
end

function oe.keypressed(key)
  if key == "escape" then oe.quit() end
  if key == "r" then restart() end
  if (key == "space" or key == "up" or key == "w") and sinceGround < COYOTE then
    local vx = player:getVelocity()
    player:setVelocity(vx, -JUMP)
    sinceGround = math.huge
    jumpSound:play()
  end
end

function oe.keyreleased(key)
  -- Let go early for a short hop.
  local vx, vy = player:getVelocity()
  if (key == "space" or key == "up" or key == "w") and vy < 0 then player:setVelocity(vx, vy * 0.45) end
end

function oe.draw()
  oe.graphics.clear(0.45, 0.7, 0.95)

  oe.graphics.setColor(0.35, 0.25, 0.2)
  for _, g in pairs(ground) do oe.graphics.rectangle("fill", g.x - camX, g.y, g.w, TILE) end
  oe.graphics.setColor(0.3, 0.75, 0.3)
  for _, g in pairs(ground) do oe.graphics.rectangle("fill", g.x - camX, g.y, g.w, 6) end

  local bob = math.sin(oe.getTime() * 4) * 3
  for _, c in ipairs(coins) do
    if not c.taken then
      oe.graphics.setColor(1, 0.85, 0.2)
      oe.graphics.circle("fill", c.x - camX, c.y + bob, 8)
      oe.graphics.setColor(0.8, 0.6, 0.1)
      oe.graphics.circle("line", c.x - camX, c.y + bob, 8)
    end
  end

  local px, py = player:getPosition()
  oe.graphics.setColor(0.9, 0.25, 0.3)
  oe.graphics.rectangle("fill", px - 11 - camX, py - 15, 22, 30)
  oe.graphics.setColor(1, 1, 1)
  oe.graphics.rectangle("fill", px - 5 - camX, py - 9, 4, 6)
  oe.graphics.rectangle("fill", px + 3 - camX, py - 9, 4, 6)

  oe.graphics.setColor(0, 0, 0, 0.5)
  oe.graphics.print("coins left: " .. left, 17, 17, 2)
  oe.graphics.setColor(1, 1, 1)
  oe.graphics.print("coins left: " .. left, 16, 16, 2)
  if won then
    oe.graphics.print("YOU WIN! press R", W / 2 - 192, H / 2 - 40, 3)
  end
end
