# openengine

A tiny 2D game engine in C (SDL3) that runs games written in Lua. PNG decoding and OGG
audio are handled by the vendored, dependency-free `stb_image`/`stb_vorbis` (in `vendor/`)
— no extra libraries to install beyond SDL3 and Lua.

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
function oe.gamepadpressed(button) end   -- button names per SDL's gamepad DB: "a", "leftshoulder", "dpup", ...
function oe.gamepadreleased(button) end
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
| `oe.graphics.newQuad(x, y, w, h)` | a pixel-space sub-rectangle of an image, for spritesheets |
| `oe.graphics.draw(img, x, y, [rot], [sx], [sy], [quad])` | rotation in radians, around center |
| `oe.keyboard.isDown(key)` | |
| `oe.mouse.getPosition()` / `oe.mouse.isDown(button)` | button 1 = left |
| `oe.gamepad.isDown(button)` / `getAxis(axis)` / `isConnected()` | first connected gamepad only |
| `oe.window.setTitle(s)` / `setSize(w, h)` / `getSize()` | |
| `oe.getTime()` / `oe.quit()` | |

### Camera

A stack of translate/rotate/scale transforms applied to everything drawn after it (not `clear`). Reset to identity at the start of every frame, before `oe.draw` runs.

```lua
oe.graphics.push()              -- save the current transform
oe.graphics.translate(dx, dy)
oe.graphics.rotate(radians)
oe.graphics.scale(s)            -- uniform; e.g. camera zoom
oe.graphics.pop()               -- restore
oe.graphics.origin()            -- reset to identity directly
```

Typical use is a scrolling/zooming camera:

```lua
function oe.draw()
  oe.graphics.push()
  oe.graphics.scale(zoom)
  oe.graphics.translate(-camera.x, -camera.y)
  -- draw the world in world coordinates here
  oe.graphics.pop()
  -- draw HUD here, unaffected by the camera
end
```

`oe.graphics.print` follows camera position and zoom but not rotation (SDL's debug text can't be rotated).

### Sprite sheets

`oe.graphics.newQuad` carves a pixel-space rectangle out of an image; pass it as `draw`'s last argument to draw just that region — one frame of a spritesheet, say:

```lua
local sheet = oe.graphics.newImage("sheet.png")
local frames = {}
for i = 0, 3 do frames[i + 1] = oe.graphics.newQuad(i * 16, 0, 16, 16) end
oe.graphics.draw(sheet, x, y, 0, 1, 1, frames[frame])
```

### Gamepad

The first gamepad connected at startup (or plugged in later) is used; there's no per-player selection. Button and axis names follow SDL's gamepad database, e.g. `"a"`, `"b"`, `"leftshoulder"`, `"dpup"`, `"leftx"`, `"lefty"`, `"lefttrigger"`.

```lua
oe.gamepad.isDown("a")        -- boolean
oe.gamepad.getAxis("leftx")   -- -1..1 (triggers: 0..1)
oe.gamepad.isConnected()
```

### Audio

```lua
local s = oe.audio.newSound("hit.wav")   -- WAV or OGG
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

Dynamic bodies broadphase through a uniform grid (thousands are fine); static bodies
(walls, floors) are checked directly against every dynamic body, since there are
usually few of them. The solver is single-pass with no iteration or sleeping, so very
deep stacks (dozens of bodies piled in one spot) can still sink through each other —
same as any simple impulse solver; switch to Box2D if a game needs stable deep stacks.
