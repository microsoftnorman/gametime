# EGG HUNT — a C++ raycaster FPS

**Working title:** EGG HUNT (changeable)
**Pitch:** A Doom-style first-person raycaster, written from scratch in modern C++. You play an Easter Bunny. The enemies are hostile Eggs. Corporate-friendly, seasonal, zero IP exposure.

## Problem statement

Build a demoable C++ video game, fast, that shows off real systems programming (a hand-written raycasting renderer) rather than gluing an engine together — with full test automation and CI. It must run reliably on a laptop in front of a live audience, with no network dependency.

## Key decisions (validated via two rubber-duck rounds with GPT-5.6)

| Decision | Choice | Why |
|---|---|---|
| Rendering | **raylib + hand-written software raycaster** | raylib owns window/input/audio/texture upload; *we* own the DDA raycaster into a 640×360 pixel buffer. Keeps the impressive "real C++" part, kills the build-time footguns. |
| Theme | **Easter Bunny vs. Eggs** | Sidesteps Octocat/GitHub/Doom IP entirely. Corporate safe. |
| Deps | CMake `FetchContent`, pinned to **commit SHA** (not tag) | Reproducible. vcpkg cold-bootstrap is a live-demo hazard. |
| Determinism | **Fixed-point** for gameplay state | Float digests are not stable across MSVC/GCC. Renderer keeps floats; simulation does not. |
| Visual tests | Invariant tests, **not** frame hashes | Frame hashing is brittle across compilers — one-pixel wall shifts nuke the hash. |

## Architecture

```
game_core     (static lib, ZERO raylib dependency)
  ├── sim/       GameState, tick(GameState&, InputFrame), fixed-point math, seeded RNG
  ├── map/       grid map, ASCII parsing, validation
  └── render/    DDA raycaster -> writes explicit 32-bit RGBA into a span

game_app      (exe, links raylib + game_core)
  └── polls raylib input -> InputFrame; owns the clock/accumulator; uploads the
      finished pixel buffer as one texture; turns semantic events (Shot/Hit/
      Pickup/Death) into raylib audio playback.

game_tests    (Catch2, links game_core only — fully headless)
```

**The seam is plain data, not interfaces.** No `IRenderer`, no `IAudioDevice`, no DI framework. `InputFrame` in, pixel span + event list out. raylib's `Color`/`Vector2`/`Texture2D` never appear outside `game_app`.

## Phases

### Phase 0 — Skeleton that runs
CMake 3.20+, C++20. Targets `game_core` / `game_app` / `game_tests`. `CMakePresets.json` for windows-msvc-{debug,release} and linux-gcc-{debug,release}. raylib + Catch2 via FetchContent pinned to commit SHAs, raylib examples/games targets disabled. **CI lands here, not in Phase 4** — GitHub Actions building + testing on windows-msvc and ubuntu-gcc from day one, with the Linux X11/OpenGL dev packages made explicit.
*Done when:* a Release `.exe` opens a window on Windows and CI is green.

### Phase 1 — The vertical slice (this is the product)
The entire risky path, end to end:
- 640×360 pixel buffer, nearest-neighbour scaled to window with aspect-correct letterboxing
- One hardcoded room; DDA raycast walls with procedural textures
- WASD + mouselook; mouse capture, Escape to release, focus-loss handling
- AABB-vs-grid collision with wall sliding
- Fixed timestep accumulator with a max-frames clamp (no spiral of death)
- One billboard Egg enemy, depth-occluded against a per-column wall depth buffer
- Shoot / damage / death / win / lose / restart
- HUD: health, ammo
- Foreground weapon sprite with bob, recoil, muzzle flash *(highest-value visual, per duck)*
- Distance shading / fog *(second-highest value, nearly free)*
- Unit tests for DDA and collision written **alongside** the code, not after

*Done when:* packaged as a self-contained ZIP, launched from a clean directory on a machine with no dev tools, playing a complete polished encounter at a stable 60fps.

### Phase 2 — Feel, in priority order (cut from the bottom)
1. Procedural PCM audio — **hard 1–2 hour timebox**, generated once at startup (shot = noise burst + descending square chirp; hit = filtered click; pickup = rising arpeggio; death = descending noisy tone). If it sounds bad after the timebox, swap in a few CC0 sounds. "Zero assets" is not worth hurting the demo.
2. Hit feedback — enemy flash, squash, knockback, screen shake, damage vignette
3. Sprite animation + blob shadows under enemies
4. ASCII map loaded from an embedded string, validated aggressively at startup
5. Ranged enemy (Rotten Egg lobber) — *optional*
6. Golden Egg boss — *optional, and test it early since oversized sprites expose projection/sorting bugs*

### Phase 3 — Full test automation
- **Unit:** DDA hit-cell/side/perpendicular-distance invariants; collision resolution; damage/health state machine; map validation rejecting malformed maps; seeded-RNG reproducibility
- **Renderer invariants:** in-bounds writes only, finite depths, correct pixel byte order (a classic `UpdateTexture` channel-swap bug)
- **Deterministic replay test:** input recorded **per simulation tick** (axes as small ints, mouse as integer counts, buttons as bit flags, edge-triggered actions consumed once) → fixed timestep → canonical digest over explicitly-sized little-endian fields: version, tick, map identity + mutable tiles, player state, all entities **sorted by stable integer ID**, RNG state, pending events. Never hash raw structs, padding, `size_t`, or container iteration order. On failure, dump the field-level diff — a bare hash mismatch is miserable to debug.
- CTest wiring; replay infrastructure lands **before** the extra enemy archetypes

### Phase 4 — Hardening & demo-proofing
- clang-format check in CI; ASan/UBSan on Linux only
- Verify static linkage / MSVC runtime — an `.exe` that silently needs an absent VC++ redist is not self-contained
- Windows Release ZIP as a CI artifact **and** a GitHub Release (Actions artifacts expire)
- Backup gameplay video + known-good prebuilt binary on the presentation laptop and a USB stick — **not committed to Git history**
- Optional stretch: floor/ceiling texture casting (expensive to debug — last)

## Demo-day rules
- **Never** download a dependency or do a cold build during the presentation.
- Arrive with a warm build tree, a packaged ZIP, a known-good binary, and a backup video.
- Assume no conference Wi-Fi.

## Notes & risks
- Biggest time sink is **making combat feel good**, not the raycasting math. Budget accordingly.
- One polished 90-second encounter beats three unfinished levels. Cut Phase 2 items from the bottom without guilt.
- Depth must be *perpendicular* camera distance, not raw ray length, or you get fish-eye.
- Sprite painter-sorting fails on large intersecting sprites — avoid that arrangement rather than building a per-pixel sprite Z-buffer.
- No Doom assets, no GitHub branding, no ripped textures or sounds. Track licenses for every dependency.

## Appendix — task backlog

Ordered by dependency. Items marked *optional* are the first things to cut if the schedule tightens.

| # | Task | Phase |
|---|---|---|
| 1 | Scaffold the CMake project skeleton | 0 |
| 2 | Stand up CI on day one (windows-msvc + ubuntu-gcc) | 0 |
| 3 | Open a window and blit a pixel buffer | 1 |
| 4 | Implement the DDA raycaster | 1 |
| 5 | Add movement, mouselook and collision | 1 |
| 6 | Write DDA and collision tests alongside the code | 1 |
| 7 | Add the first billboard Egg enemy | 1 |
| 8 | Close the combat loop (shoot/damage/win/lose/restart + HUD) | 1 |
| 9 | Add the highest-value visual polish (weapon sprite, fog) | 1 |
| 10 | Package and clean-machine test the vertical slice | 1 |
| 11 | Synthesise sound effects as PCM in code (timeboxed) | 2 |
| 12 | Add hit feedback and game feel | 2 |
| 13 | Load levels from an embedded ASCII map | 2 |
| 14 | Build the deterministic replay test harness | 3 |
| 15 | Test renderer invariants instead of frame hashes | 3 |
| 16 | Add the ranged Rotten Egg enemy *(optional)* | 2 |
| 17 | Add the Golden Egg boss *(optional)* | 2 |
| 18 | Harden the build with linting and sanitizers | 4 |
| 19 | Demo-proof the release | 4 |
