-- Pong. Run: ./openengine examples/pong
local W, H = 800, 600
local PW, PH, SPEED = 12, 90, 420
local left, right, ball, score

local function serve(dir)
  ball = { x = W / 2, y = H / 2, vx = 320 * dir, vy = math.random(-200, 200), r = 8 }
end

function oe.load()
  oe.window.setTitle("openengine pong")
  oe.window.setSize(W, H)
  left, right = { y = H / 2 - PH / 2 }, { y = H / 2 - PH / 2 }
  score = { 0, 0 }
  serve(1)
end

local function move(p, up, down, dt)
  if oe.keyboard.isDown(up) then p.y = p.y - SPEED * dt end
  if oe.keyboard.isDown(down) then p.y = p.y + SPEED * dt end
  p.y = math.max(0, math.min(H - PH, p.y))
end

local function hits(px, p)
  return ball.x + ball.r > px and ball.x - ball.r < px + PW
     and ball.y + ball.r > p.y and ball.y - ball.r < p.y + PH
end

function oe.update(dt)
  move(left, "w", "s", dt)
  move(right, "up", "down", dt)

  ball.x = ball.x + ball.vx * dt
  ball.y = ball.y + ball.vy * dt
  if ball.y < ball.r or ball.y > H - ball.r then ball.vy = -ball.vy end
  ball.y = math.max(ball.r, math.min(H - ball.r, ball.y))

  if ball.vx < 0 and hits(30, left) or ball.vx > 0 and hits(W - 30 - PW, right) then
    ball.vx = -ball.vx * 1.05
    ball.vy = ball.vy + math.random(-80, 80)
  end

  if ball.x < 0 then score[2] = score[2] + 1; serve(1) end
  if ball.x > W then score[1] = score[1] + 1; serve(-1) end
end

function oe.draw()
  oe.graphics.clear(0.08, 0.08, 0.12)
  oe.graphics.setColor(1, 1, 1)
  oe.graphics.rectangle("fill", 30, left.y, PW, PH)
  oe.graphics.rectangle("fill", W - 30 - PW, right.y, PW, PH)
  oe.graphics.circle("fill", ball.x, ball.y, ball.r)
  oe.graphics.setColor(1, 1, 1, 0.3)
  oe.graphics.line(W / 2, 0, W / 2, H)
  oe.graphics.setColor(1, 1, 1)
  oe.graphics.print(score[1] .. "   " .. score[2], W / 2 - 64, 20, 4)
end

function oe.keypressed(key)
  if key == "escape" then oe.quit() end
end
