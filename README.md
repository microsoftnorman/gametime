# EGG HUNT

A Doom-style first-person raycaster in C++20. You are the Easter Bunny. The eggs
are hostile. Clear the warren, collect the jellybeans, reach the basket.

No game engine, no 3D API — the world is drawn one screen column at a time into a
640x360 pixel buffer by a DDA raycaster, exactly the way Wolfenstein 3D and Doom
did it. raylib is used only to open a window, blit that buffer, and play audio.

## Play

| Key | Action |
|---|---|
| `W` `A` `S` `D` | Move / strafe |
| Mouse | Look |
| `Shift` | Sprint |
| Left click / `Ctrl` | Fire the Carrot Blaster |
| `R` | Restart |
| `Esc` | Release the mouse, then quit |

Kill all five eggs, grab the jellybeans, and step into the basket to win. Let the
eggs reach you too often and you lose.

## Build

Requires CMake 3.20+ and a C++20 compiler. raylib and Catch2 are fetched
automatically by CMake — there is nothing to install by hand.

```sh
cmake --preset windows-msvc-release   # or linux-gcc-release
cmake --build --preset windows-msvc-release
```

The binary lands in `out/build/<preset>/game_app.exe`.

> **Windows note:** these presets use Ninja with `cl`, so run them from a shell
> where `vcvars64.bat` has already been called. If `cmake` is not on your `PATH`,
> invoke it by full path rather than prepending to `PATH` inside the same `cmd`
> line — `%PATH%` is expanded before `vcvars64.bat` runs, so
> `set PATH=...;%PATH%` silently discards the entire Visual Studio environment
> and you get a confusing "unable to find Ninja" or "cannot find the compiler".

On Windows the CRT is linked statically, so `game_app.exe` runs on a clean
machine with no Visual C++ Redistributable installed.

## Test

```sh
ctest --test-dir out/build/<preset> --output-on-failure
```

66 tests covering the raycaster, collision and movement speeds, enemy AI, sprite
projection and occlusion, HUD, procedural audio, and frame-exact replay
determinism. The simulation is fixed-point and deterministic: `game_core` has no
floating-point state, no wall-clock reads, and no raylib dependency, so every
test runs headless.

## Layout

```
src/core/     simulation + software renderer  (no raylib, fully testable)
src/app/      window, input, audio playback    (the only raylib consumer)
tests/        one test file per subsystem
```

`src/core` never includes raylib. That boundary is what makes the whole game
testable without a display, and it is worth preserving.

## Docs

- [`requirements.md`](requirements.md) — full product and engineering requirements
- [`mvp.md`](mvp.md) — the playable slice, tuning tables, and level layout
