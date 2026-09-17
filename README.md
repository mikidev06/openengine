# openengine

A tiny 2D game engine in C (SDL3) that runs games written in Lua.

```sh
make                          # needs sdl3 + lua (pkg-config). LUA=lua5.4 make for 5.4
./openengine examples/pong    # a directory with main.lua, or a .lua file
./openengine examples/platformer
./openengine examples/physics
make test                     # headless self-check
```

## API

Define any of these callbacks:

```lua
function oe.load() end
function oe.update(dt) end
function oe.draw() end
function oe.keypressed(key) end      -- key names are lowercase: "a", "space", "left", "escape"
function oe.keyreleased(key) end
function oe.mousepressed(x, y, button) end
function oe.mousereleased(x, y, button) end
```

| Function | |
|---|---|
| `oe.graphics.clear(r, g, b)` | colors are 0–1 |
| `oe.graphics.setColor(r, g, b, [a])` | also tints images |
| `oe.graphics.rectangle(mode, x, y, w, h)` | mode: `"fill"` or `"line"` |
| `oe.graphics.circle(mode, x, y, radius)` | |
| `oe.graphics.line(x1, y1, x2, y2)` | |
| `oe.graphics.print(text, x, y, [scale])` | built-in 8×8 font |
| `oe.graphics.newImage(path)` | PNG or BMP; `img:getWidth()`, `img:getHeight()` |
| `oe.graphics.draw(img, x, y, [rot], [sx], [sy])` | rotation in radians, around center |
| `oe.keyboard.isDown(key)` | |
| `oe.mouse.getPosition()` / `oe.mouse.isDown(button)` | button 1 = left |
| `oe.window.setTitle(s)` / `setSize(w, h)` / `getSize()` | |
| `oe.getTime()` / `oe.quit()` | |

### Audio

```lua
local s = oe.audio.newSound("hit.wav")   -- WAV only
s:play()            -- restarts if already playing
s:stop()
s:setVolume(0.5)    -- 0..1 (higher amplifies)
s:setLooping(true)
s:isPlaying()
```

### Physics

Circles and axis-aligned rectangles (no rotation). Positions are body centers; units are pixels.

```lua
local world = oe.physics.newWorld(gx, gy)            -- gravity, e.g. 0, 900
local ball  = world:newCircle(x, y, radius)          -- "dynamic" by default
local floor = world:newRectangle(x, y, w, h, "static")

world:update(dt)                                     -- call from oe.update
world:setCallback(function(a, b, nx, ny) end)        -- once per touching pair per update; normal points a -> b

body:getPosition() / setPosition(x, y)
body:getVelocity() / setVelocity(vx, vy)
body:setRestitution(0..1)                            -- bounciness, default 0
body:setFriction(f)                                  -- default 0.2
body:getShape()                                      -- "circle" or "rectangle"
body:getSize()                                       -- radius, or w, h
body:destroy()                                       -- safe inside callbacks
```

Bodies can be table keys, so tag them yourself: `enemies[body] = true`.
