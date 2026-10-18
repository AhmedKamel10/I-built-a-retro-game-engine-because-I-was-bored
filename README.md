# I-built-a-retro-game-engine-because-I-was-bored

A tiny software-rendered FPS experiment built from scratch-ish in C++.
It uses OpenGL only to present the final framebuffer, GLFW for window/input, and miniaudio for sound. In other words: a lot of code, a small window, and one deeply suspicious title.

## What it does

- Raycasts a 320×200 framebuffer to draw the world
- Renders enemies, bosses, grenades, HUD, muzzle flash, and damage feedback
- Synthesizes all sound effects at runtime with miniaudio
- Tracks kills, survival time, boss waves, and a simple best-score record

## Controls

- `W` / `A` / `S` / `D` — move
- Mouse — look around
- Left click — shoot
- Right click — throw grenade
- `Enter` — restart after game over
- `Esc` — quit

## Requirements

This project expects the following to be available on your system:

- CMake 3.16 or newer
- A C++17 compiler
- OpenGL
- GLFW 3
- GLEW
- POSIX threads / `Threads::Threads`

### Linux example dependencies

On Debian/Ubuntu-style systems, this is usually enough:

```bash
sudo apt install cmake g++ libglfw3-dev libglew-dev
```

If your distro splits OpenGL headers or loader packages differently, install the equivalents it prefers. Linux loves options almost as much as it loves making you hunt for them.

## Build

From the project root:

```bash
cmake -S . -B build
cmake --build build
```

## Run

After building:

```bash
./build/doom
```

## Project layout

- `main.cpp` — gameplay, rendering, enemies, weapons, HUD, and game loop
- `miniaudio_impl.cpp` — miniaudio implementation unit
- `miniaudio.h` — miniaudio header
- `CMakeLists.txt` — build configuration

## Notes

- The game renders to a low-resolution framebuffer and scales it through OpenGL.
- Audio is synthesized at startup; there are no external sound files to manage.
- The `build/` directory is generated output and should not be committed.

## Repo status

This workspace is now organized so it can be pushed to GitHub as a normal public repository. I can’t publish the repo directly from here, but the files are ready for you to commit and push.
