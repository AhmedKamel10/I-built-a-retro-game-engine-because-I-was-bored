# doom-but-shit

A first-person shooter written from scratch in C++ with a software renderer. Every pixel is computed on the CPU into one framebuffer, and once per frame that buffer is uploaded as a texture. There is no engine and no graphics library doing the rendering: GLFW opens the window, GLEW loads OpenGL, miniaudio plays samples, and everything else is in this repo.

<!-- screenshot or gif goes here -->

It is not actually Doom ofc. Real Doom is a BSP-tree engine with variable-height sectors, and that is weeks of work I did not do. This is a Wolfenstein-style raycaster that looks like Doom for about two seconds. The name is a joke, and I have decided to stand by it.

##Some screenshots

<img width="350" height="250" alt="Screenshot from 2026-10-18 19-47-23" src="https://github.com/user-attachments/assets/939d2d5a-110b-461f-99d9-a425aeccaeed" />
<img width="350" height="250" alt="Screenshot from 2026-10-18 19-42-06" src="https://github.com/user-attachments/assets/738039e3-1dcb-4749-9101-d2c5abaf4c06" />
<img width="350" height="250" alt="Screenshot from 2026-10-18 19-42-32" src="https://github.com/user-attachments/assets/78e1213b-7c5e-47df-8c19-c53469476a1c" />
<img width="350" height="250" alt="Screenshot from 2026-10-18 19-41-52" src="https://github.com/user-attachments/assets/3e00f577-5519-4235-bf47-962909d305b5" />


## Why this exists

I started it around 3 a.m. after finishing everything I was actually supposed to be doing and realizing I was not going to sleep. My entire computer graphics toolkit at that point was two algorithms from class: Bresenham's line and the midpoint circle. This is what happened next. And again this is something I built in 2 days with barely any previous knowledge.

I wrote the engine by hand because I wanted to understand it, not because it's the fast way to make a shooter. Some of it is clean. Some of it is the first thing that worked. I've tried to be honest about which is which below.

## Play it

| Input | Action |
|---|---|
| Mouse | Look (yaw and pitch). The cursor is captured. |
| W / S | Walk forward / back |
| A / D | Strafe left / right |
| Left click (hold) | Fire: 3-pellet spread, about 7 shots per second |
| Right click | Throw a grenade |
| Enter | On the game over screen: start the next player |
| Esc | Quit |

**The rules.** You have 100 HP. Imps spawn every 1.5 seconds, up to 30 alive at once, hunt you when they can see you, and hit for 15. Survive as long as you can.

**The contest.** When you die, press Enter and hand the mouse to someone else. The game counts Player 1, Player 2, and so on, shows your time to a tenth of a second, and remembers who has the best one. The best time lives in memory only, so quitting forgets it.

**Grenades.** You hold up to 3 and get one back every 5 seconds. The fuse is 2 seconds. Anything within 40 units of the blast takes damage: it kills any imp, and it costs you 20 HP if you are in range too. Right now the blast ignores walls (see Known problems).

## Build

Developed on Ubuntu 24.04. I have not tried Windows or macOS.

```bash
sudo apt install build-essential cmake libglfw3-dev libglew-dev
wget -O miniaudio.h https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/doom
```

- `miniaudio.h` is not checked in. Download it once with the command above.
- `miniaudio_impl.cpp` is two lines (it defines `MINIAUDIO_IMPLEMENTATION` and includes the header). It exists so the huge header only gets compiled once, and the first build is slow because of it. After that, rebuilds only recompile `main.cpp`.
- Use the Release build. The renderer's inner loops are not clever, and an unoptimized build feels it.
- If no audio device can be opened, the game prints a message and runs silently.

## How it works

### 1. Getting pixels on the screen

`fb` is a `std::vector<uint32_t>` of 320x200 pixels in `0xAABBGGRR` order (red is `0xFF0000FF`, which I got wrong more than once). Everything draws into it through a bounds-checked `putPixel`. Each frame:

1. The buffer is uploaded with `glTexSubImage2D` to a texture with `GL_NEAREST` filtering.
2. One fullscreen triangle is drawn. Its vertices come from `gl_VertexID` in the vertex shader, so there are no vertex buffers.
3. The fragment shader samples the texture.

The window is 960x600, so each game pixel is a 3x3 block. OpenGL 3.3 core is used for exactly this and nothing else.

### 2. Lines

`drawline` is integer Bresenham for all eight octants: take `|dx|` and `|dy|`, keep the direction in `xstep` and `ystep`, step along x when the line is flat and along y when it is steep. These days it only draws the muzzle flash, but it was the first thing I wrote and the reason I thought this was possible.

### 3. Walls

The map is a hardcoded 10x10 `int` array, 16 world units per cell. Positions are in world units, and a unit is one pixel of the top-down map I started with.

For each of the 320 screen columns, one ray is cast:

```
ray_angle = view_angle - FOV/2 + (x / 320) * FOV        FOV = 60 degrees
```

`fireray` walks the ray forward **one unit at a time** until it lands in a wall cell, and returns the straight-line distance. Then:

```
wall_height_px = 16 * 200 / distance
```

centered on the horizon, which sits at `H/2 + pitch`. The wall color fades with distance, with a gradient ceiling above and a gradient floor below. Everything 3D in this project comes from that one fact: something twice as far away looks half as tall.

Looking up and down is faked. `pitch` just moves the horizon line, and nothing is actually rotated. It is clamped to a third of the screen height, or the illusion falls apart.

Each column's wall distance is saved in `zbuf[320]`, and sprites use it to know what is in front of what.

### 4. Sprites (enemies and grenades)

Projecting a thing into the view takes four steps:

```
dist   = sqrt(dx^2 + dy^2)                     dx, dy = thing minus player
delta  = atan2(sin(a), cos(a))                 a = atan2(dy, dx) - view_angle, wrapped to [-pi, pi]
column = (delta + FOV/2) / FOV * 320
y      = horizon + (8 - z) * 200 / dist        z = height above the floor
```

The floor sits 8 units below the horizon, because walls are 16 tall and centered on it. Size is world size times `200 / dist`. For each screen column, the sprite is skipped if `dist >= zbuf[x]`, since a wall is in front. Enemies are drawn far to near.

The `atan2(sin, cos)` wrap matters. The view angle just keeps growing as you turn, so after a few spins `a` can be off by whole turns, and every angle comparison in the game goes through that wrap.

**The imp has no image file.** `imp(u, v)` takes a point in sprite space (0 to 1 on both axes) and returns a color, or 0 for transparent. The eyes are ellipses, the brows are thin tilted bands, the teeth are alternating stripes from `floor()`, the ribs are `fmod` stripes, and left-right symmetry comes from using `|x|`. A pixel gets a dark outline if any of its four neighbors is transparent. That costs five evaluations per pixel, which is fine at this size.

Hit imps flash white. Dead ones collapse over 0.3 seconds into a dark flat shape and vanish after about a second. Distant ones are dimmed.

### 5. Shooting

There are no bullets in flight. Each of the three pellets (at angle offsets 0, +0.12 and -0.1 radians) is a hitscan:

1. Cast a ray to find the wall distance.
2. For each living enemy, find its distance `h` and the wrapped angle between the shot and the direction to the enemy.
3. It is a hit if that angle is at most `hitradius / h` (the angular size of a circle at that distance) and the enemy is closer than the wall.
4. The nearest hit wins.

### 6. Enemy AI

The behavior is decided from scratch every frame:

- **Chase:** if the player is within 200 units and a line-of-sight check passes (a ray stepped 5 units at a time against the map), walk straight at them at 30 units per second. Within 18 units, hit for 15 damage on a 1-second cooldown.
- **Wander:** otherwise, move at 20 units per second along a heading, and pick a random new heading when blocked by a wall.

There is no pathfinding and no memory. An imp loses you the instant a corner gets between you, goes back to wandering, and can flicker between the two behaviors at the edge of sight.

The spawner scans the map from a random starting cell and takes the first open cell at least 64 units from the player.

### 7. Grenades

These get real physics. Each has a position, a height `z`, and a velocity `(vx, vy, vz)`:

- Gravity is 60 units/s squared. The launch speed is 100 units/s horizontally and `25 + 0.3 * pitch` vertically, so looking up lobs it farther.
- On the floor (`z <= 2`): if it is falling faster than 10, it bounces with half its vertical speed and 70% of its horizontal speed. Otherwise it rests and rolls with a drag.
- X and Y are tested against walls separately, so it ricochets: that component flips and halves.
- After 2 seconds it explodes.
- It is drawn as a filled circle (`ox^2 + oy^2 <= r^2`) with radius `2 * 200 / dist`, with the same depth test as the other sprites.

### 8. Sound

There are no audio files. At startup, `makeShot()` and friends fill float arrays from formulas:

- **Shot:** a sine sweeping from about 185 Hz down to 45 Hz with an exponential decay, plus low-passed noise and a sharp noise crack.
- **Hit and hurt:** shorter versions of the same idea.
- **Death:** a sawtooth through a one-pole low-pass with falling pitch.
- **Game over:** three falling notes (196, 164.8 and 130.8 Hz) with harmonics.

miniaudio calls my callback on its own thread. It mixes up to 16 voices, and `playSound` takes a free voice under a mutex. They are crude, but they work, and there is nothing to ship alongside the binary.

### 9. HUD and text

There is no font file either. The font is 3x5 pixels per glyph, stored as strings of 15 `0`/`1` characters, and only the letters the game actually prints exist. The game over screen darkens and reddens the framebuffer and draws on top of it.

## Code layout

It is one `main.cpp` of well over a thousand lines, in this order: globals, audio synthesis, drawing primitives, map and collision and `fireray`, input, shooting, wall rendering, the imp and its drawing, grenades, enemy AI, HUD and font, game over and restart, then the main loop. I will split it when it annoys me enough, and it hasn't yet.

The main loop runs input, enemy update, grenade update, timers, spawning, and the death check (all skipped on the game over screen), then draws walls, enemies, grenades, gun, muzzle flash, damage tint and HUD, then uploads the frame. All movement and timers are scaled by `dt`, so speed does not depend on frame rate.

## Tuning knobs

| What | Where | Value |
|---|---|---|
| Fire interval | `SHOT_TIME` | 0.14 s |
| Player speed | `movespeed` | 50 units/s |
| Mouse sensitivity | `mouse_yaw_sens`, `mouse_pitch_sens` | 0.0025 rad/px, 0.4 px/px |
| Enemy chase / wander speed | `updateEnemies`, `moveenemies` | 30 / 20 units/s |
| Spawn interval and cap | `main`, `spawnenemies` | 1.5 s, 30 alive |
| Gravity, grenade radius | `GRAVITY`, `GRENADE_R` | 60, 2 |
| Grenade regen and max | `updategrenade` | 0.2 per second, 3 |
| Blast radius | `boom` | 40 units |

## Things that bit me

- **Row/column versus x/y.** `map[row][col]` is `map[y][x]`. My spawner used them swapped, so enemies appeared at the transposed cell, which is sometimes inside a wall. An enemy inside a wall can't be seen or shot, but it still counts toward the cap of 30. The symptom was fewer and fewer enemies, with no error.
- **Y points down.** Positive angles turn clockwise, and every picture from class is upside down.
- **A loop variable shadowed a parameter.** `moveenemies(e)` looped over all enemies with another `e`, so every wandering enemy moved every enemy. With 15 of them, everything walked at 15 times speed and bounced off walls frantically.
- **An uninitialized `bool`.** `grenade.active` had no default, so grenades were randomly deleted the frame they were thrown.
- **Angles grow forever.** After spinning in circles, comparing a view angle with `atan2` output gave nonsense, which produced "hits" nowhere near the target. Everything now goes through the sin/cos wrap.
- **A huge first mouse delta.** After sitting on the game over screen, the first frame back measured the whole time the mouse had moved and spun the view. Restart now resyncs the cursor.

## Known problems

- **No fisheye correction.** Wall distance is straight-line, not perpendicular, so flat walls bow at the screen edges. Multiplying by `cos(ray_angle - view_angle)` fixes it, and I haven't done it yet.
- **Ray marching is one unit per step.** A DDA grid traversal would be exact and much faster. Wall distances are quantized to about one unit, so far walls show stepping, and 320 rays per frame is the hot spot of the whole program.
- **The vertical scale constant (200) is eyeballed.** One derived from the 60 degree FOV would be about 277.
- **The grenade blast ignores walls** and has no falloff: everything inside the radius takes the same damage.
- **Hitboxes are smaller than sprites.** The hit radius is 2 units and the imp is about 10 wide, so a shot that visibly lands on an arm can miss. I haven't decided whether that is a bug or difficulty.
- **Enemies have no pathfinding and no memory,** and they collide as points, so their sprites overlap walls.
- **`rand()` is never seeded,** so the same enemies spawn the same way every run.
- Hardcoded map, no textures, one wall color, one enemy type, Linux only, tested on one machine.

## Next

Fix the fisheye, then a DDA, then textured walls using the exact hit position. After that: a BFS flow field so enemies can path around corners, an explosion effect and sound, and walls that stop grenade blasts.

## If you want to build one

Worth reading: Lode Vandevenne's raycasting tutorial (lodev.org), Fabien Sanglard's *Game Engine Black Book: DOOM*, and learnopengl.com for the OpenGL boilerplate.

## How this got made

I used AI help (Claude) throughout. It helped with the boilerplate, the imp sprite and the gun drawing, the bitmap font and HUD, the game over screen, the sound synthesis, and a lot of code review. Several of the bugs above were found by it reading my code. The rest, including the line algorithm, the map and movement, the raycasting, hit detection, spawning, enemy behavior, and the grenades, I wrote and debugged myself.

If you read the code and spot something I should have caught, I would like to hear it.

— Ahmed
