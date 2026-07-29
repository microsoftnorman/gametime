#include "core/framebuffer.h"
#include "core/hud.h"
#include "core/raycast.h"
#include "core/sprites.h"
#include "core/state.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// render_frame is the only renderer the game actually calls, and until this
// file existed no test called it at all. Every layer was verified in isolation
// and nothing owned the assembly, so deleting render_sprites from render_frame
// -- drawing walls, weapon and HUD but never a single enemy -- passed all 71
// tests. The whole point of the game can go missing with CI green.

namespace {

struct Target {
    std::vector<uint32_t> pixels;
    std::vector<float> depth;
    eh::Framebuffer framebuffer;

    Target()
        : pixels(static_cast<std::size_t>(eh::Framebuffer::W) * eh::Framebuffer::H, 0u),
          depth(static_cast<std::size_t>(eh::Framebuffer::W), 0.0f),
          framebuffer{pixels.data(), depth.data()} {}
};

// A scene in which every layer has something to draw: real map geometry from
// reset(), an egg placed just ahead of wherever the player actually spawns, and
// a live muzzle flash so the weapon is in its most distinct state.
eh::GameState scene() {
    eh::GameState state;
    eh::reset(state, 0x5eed1234u);
    state.screen = eh::Screen::Playing;
    state.muzzle_flash = 3;

    constexpr float TWO_PI = 2.0f * 3.14159265358979323846f;
    const float angle = static_cast<float>(state.player.angle) * TWO_PI / 65536.0f;
    const float x = eh::fx_to_float(state.player.x) + std::cos(angle) * 1.2f;
    const float y = eh::fx_to_float(state.player.y) + std::sin(angle) * 1.2f;

    eh::Entity egg;
    egg.id = 9001;
    egg.type = eh::EntityType::Egg;
    egg.x = eh::fx_from_float(x);
    egg.y = eh::fx_from_float(y);
    egg.alive = true;
    state.entities.push_back(egg);
    return state;
}

// The weapon layer alone. Both channels gs.muzzle_flash drives -- the flash art and the
// recoil offset -- live here and nothing else in the frame reads that field, so rendering
// this layer by itself makes "the weapon is flashing" a question about pixels rather than
// about the field's value.
std::vector<uint32_t> weapon_pixels(const eh::GameState &gs) {
    Target target;
    eh::render_weapon(gs, target.framebuffer);
    return target.pixels;
}

int differing_pixels(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
    int count = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            ++count;
        }
    }
    return count;
}

// A wall's absolute vertical scale was pinned by exactly one number, and that number is a
// RATIO. `render: walls and sprites share one horizon but disagree on vertical scale` fits
// row = horizon + scale/depth for each renderer and then asserts sprite_scale/wall_scale is
// between 1.15 and 1.35. Measured against that suite:
//
//   every wall 50% taller (walls only)          1 test failed -- the divergence bound
//   every wall 50% taller AND every sprite too  no wall test failed at all
//
// The second is the real hazard. The ratio survives any change the two renderers make
// together, so nothing pinned how tall a wall actually is. Worse, that divergence bound is
// the one assertion in the suite we already expect to be rewritten: which renderer's scale
// is correct is an open playtest question, and resolving it by retuning either side
// re-baselines the bound and deletes the last guard on walls with it.
//
// This pins the wall scale absolutely and geometrically, so it survives that rewrite:
// (H/2 - wall_top) * depth == H/2 says a wall one tile away exactly fills the screen and
// shrinks in exact inverse proportion to distance. It names no sprite and reproduces no
// projection arithmetic.
eh::GameState scale_corridor(int wall_column) {
    constexpr int ROWS = 5;
    constexpr int COLUMNS = 44;

    eh::GameState game;
    eh::reset(game, 0x5eed1234u);
    game.screen = eh::Screen::Playing;
    game.entities.clear();

    eh::Map &map = game.level.map;
    map.width = COLUMNS;
    map.height = ROWS;
    map.tiles.assign(static_cast<std::size_t>(COLUMNS) * ROWS, eh::Tile::Floor);
    for (int x = 0; x < COLUMNS; ++x) {
        map.tiles[static_cast<std::size_t>(x)] = eh::Tile::WallPantry;
        map.tiles[static_cast<std::size_t>((ROWS - 1) * COLUMNS + x)] = eh::Tile::WallPantry;
    }
    for (int y = 0; y < ROWS; ++y) {
        map.tiles[static_cast<std::size_t>(y * COLUMNS)] = eh::Tile::WallPantry;
        map.tiles[static_cast<std::size_t>(y * COLUMNS + wall_column)] = eh::Tile::WallPantry;
    }

    game.player.x = eh::fx_from_float(1.5f);
    game.player.y = eh::fx_from_float(2.5f);
    game.player.angle = eh::angle_from_deg(0.0);
    return game;
}

int wall_top_row(const Target &frame, const Target &reference) {
    constexpr int CENTER = eh::Framebuffer::W / 2;
    for (int y = 0; y < eh::Framebuffer::H; ++y) {
        const auto offset =
            static_cast<std::size_t>(y) * eh::Framebuffer::W + static_cast<std::size_t>(CENTER);
        if (frame.pixels[offset] != reference.pixels[offset]) {
            return y;
        }
    }
    return -1;
}

// Every wall fixture in the suite keeps the player at least a tile from the wall, so the
// near field -- the case where you walk right up to something -- was never rendered. The
// raycaster clamps near depth and ceilings the projected height for exactly this case, and
// measured against the shipped suite none of that code was pinned by anything:
//
//   near clamp raised to a full tile (walls never closer)   passed 136/136
//   near clamp raised to half a tile                        passed 136/136
//   projected height ceiling cut to one screen height       passed 136/136
//
// The first means you could press against a wall and it would simply stop growing.
//
// Note the near-field code cannot be probed by substituting MIN_DEPTH's own value: it is
// 0.0001f, so a "clamp removed" mutant that writes 0.0001f is a no-op that passes for the
// wrong reason. The mutants above change behaviour; that one did not.
eh::GameState near_corridor(float player_x) {
    constexpr int ROWS = 5;
    constexpr int COLUMNS = 12;
    constexpr int WALL_COLUMN = 4;

    eh::GameState game;
    eh::reset(game, 0x5eed1234u);
    game.screen = eh::Screen::Playing;
    game.entities.clear();

    eh::Map &map = game.level.map;
    map.width = COLUMNS;
    map.height = ROWS;
    map.tiles.assign(static_cast<std::size_t>(COLUMNS) * ROWS, eh::Tile::Floor);
    for (int x = 0; x < COLUMNS; ++x) {
        map.tiles[static_cast<std::size_t>(x)] = eh::Tile::WallPantry;
        map.tiles[static_cast<std::size_t>((ROWS - 1) * COLUMNS + x)] = eh::Tile::WallPantry;
    }
    for (int y = 0; y < ROWS; ++y) {
        map.tiles[static_cast<std::size_t>(y * COLUMNS)] = eh::Tile::WallPantry;
        map.tiles[static_cast<std::size_t>(y * COLUMNS + WALL_COLUMN)] = eh::Tile::WallPantry;
    }

    game.player.x = eh::fx_from_float(player_x);
    game.player.y = eh::fx_from_float(2.5f);
    game.player.angle = eh::angle_from_deg(0.0);
    return game;
}

// Longest run of identical pixels down one column. A texel row magnified by proximity
// covers more screen rows, so this grows as the player closes on the wall.
int longest_constant_run(const Target &frame, int column) {
    int longest = 1;
    int run = 1;
    for (int y = 1; y < eh::Framebuffer::H; ++y) {
        const auto above =
            static_cast<std::size_t>(y - 1) * eh::Framebuffer::W + static_cast<std::size_t>(column);
        const auto here =
            static_cast<std::size_t>(y) * eh::Framebuffer::W + static_cast<std::size_t>(column);
        if (frame.pixels[above] == frame.pixels[here]) {
            ++run;
        } else {
            longest = std::max(longest, run);
            run = 1;
        }
    }
    return std::max(longest, run);
}

// An egg attacks when it is within EGG_ATTACK_RANGE -- 0.75 tiles (entities.cpp) -- so the
// frames that matter most in this game are the ones where an egg is right in your face. No
// fixture in the suite ever rendered a sprite closer than about two tiles, and measured against
// the shipped suite the entire near field of the billboard renderer was unpinned:
//
//   NEAR_PLANE 0.05 -> 1.00 (an egg inside one tile vanishes)     passed 137/137
//   NEAR_PLANE 0.05 -> 1.75                                        passed 137/137
//   projected height ceilinged at one screen                       passed 137/137
//   projected width ceilinged at one screen                        passed 137/137
//   sprite depth clamped to 1.0 tile, so it stops growing          passed 137/137
//   NEAR_PLANE routed through a named local (control)              passed 137/137
//
// The first is the sharpest: an egg would blink out of existence exactly as it closed to hurt
// you, and CI stayed green. Raising it to 2.0 finally killed five tests, which locates every
// sprite fixture in the suite at two tiles or farther.
//
// It survived because the one fixture that does place an egg near the player -- scene(), at 1.2
// tiles -- has its sprite layer guarded only by `walls.pixels != with_sprites.pixels`, and
// reset() populates the level with its own distant entities. Those satisfy the guard on their
// own, so the near egg it deliberately places can vanish without failing anything.
//
// The first version of this test caught four of those five and still let the width ceiling
// through 138/138: a ceiling compresses the growth curve without inverting it, so an ordering
// assertion cannot see it. That is why the magnitude assertion at contact range is here.
eh::GameState egg_corridor(float depth) {
    constexpr int ROWS = 5;
    constexpr int COLUMNS = 34;
    constexpr float PLAYER_X = 1.5f;
    constexpr float PLAYER_Y = 2.5f;

    eh::GameState game;
    eh::reset(game, 0x5eed1234u);
    game.screen = eh::Screen::Playing;
    game.entities.clear();

    eh::Map &map = game.level.map;
    map.width = COLUMNS;
    map.height = ROWS;
    map.tiles.assign(static_cast<std::size_t>(COLUMNS) * ROWS, eh::Tile::Floor);
    for (int x = 0; x < COLUMNS; ++x) {
        map.tiles[static_cast<std::size_t>(x)] = eh::Tile::WallPantry;
        map.tiles[static_cast<std::size_t>((ROWS - 1) * COLUMNS + x)] = eh::Tile::WallPantry;
    }
    for (int y = 0; y < ROWS; ++y) {
        map.tiles[static_cast<std::size_t>(y * COLUMNS)] = eh::Tile::WallPantry;
        map.tiles[static_cast<std::size_t>(y * COLUMNS + COLUMNS - 1)] = eh::Tile::WallPantry;
    }

    game.player.x = eh::fx_from_float(PLAYER_X);
    game.player.y = eh::fx_from_float(PLAYER_Y);
    game.player.angle = eh::angle_from_deg(0.0);

    eh::Entity egg;
    egg.id = 9001;
    egg.type = eh::EntityType::Egg;
    egg.x = eh::fx_from_float(PLAYER_X + depth);
    egg.y = eh::fx_from_float(PLAYER_Y);
    egg.alive = true;
    game.entities.push_back(egg);
    return game;
}

} // namespace

// The wall camera and the billboard camera each build their own basis from the same player
// angle, and nothing made them agree. Freezing the sprite basis at east left 93/93 green: the
// walls turned with the player and the eggs did not. Mirroring the sprite plane was caught only
// by a 3-pixel rounding artifact at a single heading, which is luck, not coverage.
//
// This tracks one fixed world feature -- the open east corridor -- through both renderers while
// the camera sweeps across it. The deepest wall column and a billboard placed down the same
// corridor must travel together.
TEST_CASE("render: walls and sprites sweep together when the camera turns") {
    constexpr int TOLERANCE = 40;
    constexpr std::array<int, 5> HEADINGS{-24, -12, 0, 12, 24};

    std::array<int, HEADINGS.size()> wall_columns{};
    std::array<int, HEADINGS.size()> sprite_columns{};

    for (std::size_t index = 0; index < HEADINGS.size(); ++index) {
        const int heading = HEADINGS[index];
        CAPTURE(heading);

        eh::GameState state;
        eh::reset(state, 0x5eed1234u);
        state.screen = eh::Screen::Playing;
        state.entities.clear();
        state.player.angle = eh::angle_from_deg(static_cast<double>(heading));

        Target walls;
        eh::render_walls(state, walls.framebuffer);

        // The far end of the corridor is the deepest thing in view; its columns locate the
        // world direction the camera is looking down.
        float deepest = -1.0f;
        for (float sample : walls.depth) {
            if (sample < 1000.0f) {
                deepest = std::max(deepest, sample);
            }
        }
        REQUIRE(deepest > 0.0f);

        long long column_sum = 0;
        long long column_count = 0;
        for (int column = 0; column < eh::Framebuffer::W; ++column) {
            if (walls.depth[static_cast<std::size_t>(column)] >= deepest - 0.25f) {
                column_sum += column;
                ++column_count;
            }
        }
        REQUIRE(column_count > 0);
        wall_columns[index] = static_cast<int>(column_sum / column_count);

        // One egg parked down that same corridor, drawn over the same walls.
        eh::Entity egg;
        egg.id = 9001;
        egg.type = eh::EntityType::Egg;
        egg.x = static_cast<eh::fx>(state.player.x + eh::fx_from_int(8));
        egg.y = state.player.y;
        egg.alive = true;
        state.entities.push_back(egg);

        Target both;
        eh::render_walls(state, both.framebuffer);
        eh::render_sprites(state, both.framebuffer);

        int left = eh::Framebuffer::W;
        int right = -1;
        for (int y = 0; y < eh::Framebuffer::H; ++y) {
            for (int x = 0; x < eh::Framebuffer::W; ++x) {
                const std::size_t offset =
                    static_cast<std::size_t>(y) * eh::Framebuffer::W + static_cast<std::size_t>(x);
                if (both.pixels[offset] != walls.pixels[offset]) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                }
            }
        }
        REQUIRE(right >= left);
        sprite_columns[index] = (left + right) / 2;

        CAPTURE(wall_columns[index], sprite_columns[index]);
        REQUIRE(std::abs(wall_columns[index] - sprite_columns[index]) <= TOLERANCE);
    }

    // Both really do sweep across the screen, so the agreement above is not two frozen cameras
    // agreeing with each other.
    REQUIRE(wall_columns.front() - wall_columns.back() > 300);
    REQUIRE(sprite_columns.front() - sprite_columns.back() > 300);
}

TEST_CASE("render: the frame draws every layer, with the HUD last") {
    const eh::GameState state = scene();

    Target walls;
    eh::render_walls(state, walls.framebuffer);

    Target with_sprites;
    eh::render_walls(state, with_sprites.framebuffer);
    eh::render_sprites(state, with_sprites.framebuffer);

    Target with_weapon;
    eh::render_walls(state, with_weapon.framebuffer);
    eh::render_sprites(state, with_weapon.framebuffer);
    eh::render_weapon(state, with_weapon.framebuffer);

    Target composed;
    eh::render_walls(state, composed.framebuffer);
    eh::render_sprites(state, composed.framebuffer);
    eh::render_weapon(state, composed.framebuffer);
    eh::render_hud(state, composed.framebuffer);

    // Non-vacuity. Equality against a hand-built composition proves nothing if
    // some layer contributes no pixels, so each one must visibly change the
    // frame in this scene before its presence is worth asserting.
    CHECK(walls.pixels != with_sprites.pixels);
    CHECK(with_sprites.pixels != with_weapon.pixels);
    CHECK(with_weapon.pixels != composed.pixels);

    // The production orchestration must equal that composition exactly. This
    // is what fails if a layer is dropped, duplicated, or reordered.
    Target produced;
    eh::render_frame(state, produced.framebuffer);
    REQUIRE(produced.pixels == composed.pixels);
}

TEST_CASE("render: HUD-last ordering is observable, not an unenforced convention") {
    const eh::GameState state = scene();

    Target hud_last;
    eh::render_walls(state, hud_last.framebuffer);
    eh::render_sprites(state, hud_last.framebuffer);
    eh::render_weapon(state, hud_last.framebuffer);
    eh::render_hud(state, hud_last.framebuffer);

    Target hud_before_weapon;
    eh::render_walls(state, hud_before_weapon.framebuffer);
    eh::render_sprites(state, hud_before_weapon.framebuffer);
    eh::render_hud(state, hud_before_weapon.framebuffer);
    eh::render_weapon(state, hud_before_weapon.framebuffer);

    // The HUD session asked that render_hud stay last. That only means
    // something if the weapon would otherwise cover it, so prove the two
    // orders differ before asserting which one production uses.
    REQUIRE(hud_last.pixels != hud_before_weapon.pixels);

    Target produced;
    eh::render_frame(state, produced.framebuffer);
    CHECK(produced.pixels == hud_last.pixels);
    CHECK(produced.pixels != hud_before_weapon.pixels);
}

// Nothing connected input to pixels. tick() was exercised in test_replay and
// test_smoke, render_frame() only here, and no test called both: the loop the
// player actually experiences -- press W, the world moves -- was never run end
// to end. The player workstream said as much, reporting that its game_app check
// was launch-only and could not prove visible world motion.

TEST_CASE("render: walking forward moves the rendered world by the distance walked") {
    eh::GameState game;
    eh::InputFrame start;
    start.buttons = eh::InputFrame::Start;
    eh::tick(game, start);
    REQUIRE(game.screen == eh::Screen::Playing);

    // Face the long corridor so the wall ahead has room to approach.
    game.player.angle = eh::angle_from_deg(0.0);
    const float start_x = eh::fx_to_float(game.player.x);

    Target before;
    eh::render_frame(game, before.framebuffer);
    const float depth_before = before.depth[eh::Framebuffer::W / 2];
    REQUIRE(depth_before > 2.0f);

    eh::InputFrame walking;
    walking.move_y = 1;
    for (int step = 0; step < 30; ++step) {
        eh::tick(game, walking);
    }
    REQUIRE(game.screen == eh::Screen::Playing);

    Target after;
    eh::render_frame(game, after.framebuffer);
    const float depth_after = after.depth[eh::Framebuffer::W / 2];

    const float walked = eh::fx_to_float(game.player.x) - start_x;
    REQUIRE(walked > 0.5f);

    // The magnitude is the assertion. "The frame changed" would also pass if the
    // renderer read a stale camera, scaled the world wrongly, or if input reached
    // the simulation but never the screen. The wall must close by exactly the
    // distance the simulation actually travelled.
    REQUIRE(std::fabs((depth_before - depth_after) - walked) < 0.01f);
    REQUIRE(before.pixels != after.pixels);
}

TEST_CASE("render: idle input leaves the camera exactly where it was") {
    eh::GameState game;
    eh::InputFrame start;
    start.buttons = eh::InputFrame::Start;
    eh::tick(game, start);
    game.player.angle = eh::angle_from_deg(0.0);

    Target before;
    eh::render_frame(game, before.framebuffer);
    const float depth_before = before.depth[eh::Framebuffer::W / 2];

    const eh::InputFrame idle;
    for (int step = 0; step < 30; ++step) {
        eh::tick(game, idle);
    }

    Target after;
    eh::render_frame(game, after.framebuffer);

    // Eggs keep chasing, so the picture legitimately changes. The camera must not.
    // This is the polarity and drift guard for the test above: a renderer that
    // advanced on its own, or input read with the wrong sign, would move this.
    REQUIRE(after.depth[eh::Framebuffer::W / 2] == depth_before);
    REQUIRE(eh::fx_to_float(game.player.x) == Catch::Approx(3.5f));
}

// `render: walls and sprites sweep together when the camera turns` proves the two renderers agree
// about *yaw*. It reads columns and never looks at a row, so it says nothing about the other half
// of the shared camera: where the floor is, and how fast things shrink toward it.
//
// Nothing made them agree there. `raycast.cpp` sizes a wall as `Framebuffer::H / distance` -- a
// vertical scale of 360 -- around `HORIZON = Framebuffer::H / 2`. `sprites.cpp` independently
// computes `projection = (W * 0.5) / tan(FOV/2)` = 492.8 and places feet at
// `horizon + CAMERA_HEIGHT * projection / depth` around its own `H * 0.5f`. Two modules, two
// sessions, one undeclared decision -- the vertical twin of the bug the yaw sweep was written for.
// Measured: raising the sprite horizon to 0.35H, floating every egg 54 pixels off the ground,
// passed 113/113; halving CAMERA_HEIGHT, sinking them into it, also passed 113/113.
//
// The oracle avoids reproducing either basis. A wall of unit height standing on the floor has its
// bottom edge *at* the floor, so the wall renderer's own output supplies the floor row at a given
// depth: render with a near wall, render again with it pushed back, and the lowest row that
// changed is the base of the near wall. The egg's feet are read the same way, by diffing its
// silhouette against the same reference. Both are read from pixels; neither is computed.
//
// Three depths establish that each renderer's rows really do follow `horizon + k / depth`, which
// is what makes the two derived quantities meaningful:
//
//   walls   251 / 207 / 190 rows at 2.5 / 6.5 / 16.5 tiles  ->  horizon 179.1, scale 179.7
//   sprites 269 / 214 / 193 rows at the same depths         ->  horizon 179.4, scale 223.9
//
// The horizons agree to within a third of a pixel: that half of the camera really is shared, and
// this test now says so. The scales do not, and cannot both be right -- an egg's feet sink below
// the floor it stands on by 44/depth pixels, 18 of them at 2.5 tiles. The ratio should be 1.0 and
// measures 1.25.
//
// That is left as a measured, bounded fact rather than quietly retuned here. Which renderer is
// wrong is an art decision -- the wall formula fixes a vertical FOV that its own horizontal FOV
// does not, so "fixing" sprites to match would change how tall every egg looks -- and it needs a
// human looking at the screen, exactly as the footfall cadence did. Bounding the ratio keeps the
// divergence from growing silently while that decision is open, and forces any fix to come here
// and state itself.
TEST_CASE("render: walls and sprites share one horizon but disagree on vertical scale") {
    constexpr int ROWS = 5; // a 3-tile-tall corridor: borders at y = 0 and y = 4
    constexpr int COLUMNS = 34;
    constexpr float PLAYER_X = 1.5f;
    constexpr float PLAYER_Y = 2.5f;
    constexpr int CENTER = eh::Framebuffer::W / 2;
    constexpr int REFERENCE_WALL = 30; // 28.5 tiles: farther than anything measured

    // Wall columns chosen so each face lands a whole number of tiles from the player.
    constexpr std::array<int, 3> WALL_COLUMNS{4, 8, 18};
    constexpr std::array<float, 3> DEPTHS{2.5f, 6.5f, 16.5f};

    auto corridor = [&](int wall_column) {
        eh::GameState game;
        eh::reset(game, 0x5eed1234u);
        game.screen = eh::Screen::Playing;
        game.entities.clear();

        eh::Map &map = game.level.map;
        map.width = COLUMNS;
        map.height = ROWS;
        map.tiles.assign(static_cast<std::size_t>(COLUMNS) * ROWS, eh::Tile::Floor);
        const auto index = [COLUMNS](int x, int y) {
            return static_cast<std::size_t>(y * COLUMNS + x);
        };
        for (int x = 0; x < COLUMNS; ++x) {
            map.tiles[index(x, 0)] = eh::Tile::WallPantry;
            map.tiles[index(x, ROWS - 1)] = eh::Tile::WallPantry;
        }
        for (int y = 0; y < ROWS; ++y) {
            map.tiles[index(0, y)] = eh::Tile::WallPantry;
            map.tiles[index(wall_column, y)] = eh::Tile::WallPantry;
        }

        game.player.x = eh::fx_from_float(PLAYER_X);
        game.player.y = eh::fx_from_float(PLAYER_Y);
        game.player.angle = eh::angle_from_deg(0.0); // straight down the corridor
        return game;
    };

    Target reference;
    eh::render_walls(corridor(REFERENCE_WALL), reference.framebuffer);

    // Lowest row of the centre column this frame changed relative to the reference. For a nearer
    // wall that is the base of the wall; for an added egg it is the sole of its foot.
    auto lowest_changed_row = [&](const Target &frame) {
        for (int y = eh::Framebuffer::H - 1; y >= 0; --y) {
            const auto offset = static_cast<std::size_t>(y) * eh::Framebuffer::W + CENTER;
            if (frame.pixels[offset] != reference.pixels[offset]) {
                return y;
            }
        }
        return -1;
    };

    std::array<double, 3> floor_rows{};
    std::array<double, 3> feet_rows{};
    for (std::size_t i = 0; i < DEPTHS.size(); ++i) {
        Target walls;
        eh::render_walls(corridor(WALL_COLUMNS[i]), walls.framebuffer);
        floor_rows[i] = lowest_changed_row(walls);

        eh::GameState game = corridor(REFERENCE_WALL);
        eh::Entity egg;
        egg.id = 9001;
        egg.type = eh::EntityType::Egg;
        egg.x = eh::fx_from_float(PLAYER_X + DEPTHS[i]);
        egg.y = eh::fx_from_float(PLAYER_Y);
        egg.alive = true;
        game.entities.push_back(egg);

        Target both;
        eh::render_walls(game, both.framebuffer);
        eh::render_sprites(game, both.framebuffer);
        feet_rows[i] = lowest_changed_row(both);

        INFO("depth " << DEPTHS[i] << " floor " << floor_rows[i] << " feet " << feet_rows[i]);
        REQUIRE(floor_rows[i] > 0);
        REQUIRE(feet_rows[i] > 0);
    }

    // Non-vacuity: both quantities must really climb toward the horizon with distance, so nothing
    // below can be satisfied by rows that never moved.
    REQUIRE(floor_rows[0] - floor_rows[2] > 20);
    REQUIRE(feet_rows[0] - feet_rows[2] > 20);

    // row = horizon + scale / depth, solved from the nearest and farthest samples.
    auto fit = [&](const std::array<double, 3> &rows) {
        const double inverse_near = 1.0 / DEPTHS[0];
        const double inverse_far = 1.0 / DEPTHS[2];
        const double scale = (rows[0] - rows[2]) / (inverse_near - inverse_far);
        return std::pair<double, double>{rows[0] - scale * inverse_near, scale};
    };
    const auto [wall_horizon, wall_scale] = fit(floor_rows);
    const auto [sprite_horizon, sprite_scale] = fit(feet_rows);

    // The middle depth was not used to fit either line, so it independently confirms that both
    // renderers really are inverse-linear in depth and these two numbers mean something.
    const double wall_predicted = wall_horizon + wall_scale / DEPTHS[1];
    const double sprite_predicted = sprite_horizon + sprite_scale / DEPTHS[1];
    INFO("middle samples " << floor_rows[1] << "/" << feet_rows[1] << " predicted "
                           << wall_predicted << "/" << sprite_predicted);
    CHECK(std::abs(floor_rows[1] - wall_predicted) <= 2.0);
    CHECK(std::abs(feet_rows[1] - sprite_predicted) <= 2.0);

    INFO("wall horizon " << wall_horizon << " scale " << wall_scale << " | sprite horizon "
                         << sprite_horizon << " scale " << sprite_scale);

    // The horizon is genuinely shared. Moving either renderer's horizon breaks this.
    CHECK(std::abs(wall_horizon - sprite_horizon) <= 2.0);

    // The vertical scale is not shared. This bound records the divergence as it stands so it
    // cannot grow unnoticed; it is not an endorsement. A correct engine would assert 1.0 here.
    const double divergence = sprite_scale / wall_scale;
    INFO("vertical scale divergence " << divergence << " (should be 1.0)");
    CHECK(divergence > 1.15);
    CHECK(divergence < 1.35);
}

// The background is the layer every other renderer test reads as its reference and none of them
// measures. `fill_background` paints the ceiling and floor of the whole world every frame, and no
// test named it, the ceiling, or the gradient. It also holds a third private copy of the horizon:
// `raycast.cpp` writes `constexpr int HORIZON = Framebuffer::H / 2` inside fill_background,
// re-derives `Framebuffer::H / 2` inline when it centres a wall, and `sprites.cpp` writes
// `Framebuffer::H * 0.5f` a third time. The test above ties the second and third together.
// Nothing tied the first to anything.
//
// Measured, each mutant isolated and build-gated, all against a green 122/122:
//
//   the gradient seam moved to H/3, 60 rows off the row walls centre on   passed 122/122
//   the sky drawn below the horizon and the ground above it               passed 122/122
//   the ceiling and floor gradients exchanged                             passed 122/122
//
// The second renders the world upside down and the suite could not tell.
//
// The oracle reproduces no arithmetic. Out-of-bounds map reads return a wall, so an open field is
// a room whose walls stand as far off as the field is wide: at 400 tiles the enclosing wall is one
// pixel tall and the gradient shows through on every other row, while at 24 tiles the same wall is
// thirty pixels tall. Rows that differ between the two are the wall band, and its midpoint is the
// row the wall renderer centres on -- read out of pixels, not computed. The painted horizon is
// read the same way, as the row where the column stops being warm and turns cool.
//
// Warm and cool rather than exact colours: the ceiling runs brown (R > B) and the floor slate blue
// (B > R). That is the art direction rather than a palette, so retinting either within its own
// temperature leaves this green, while exchanging them does not.
TEST_CASE("render: the sky sits above the ground and meets it where walls are centred") {
    constexpr int CENTER = eh::Framebuffer::W / 2;

    auto open_field = [](int size) {
        eh::GameState game;
        eh::reset(game, 0x5eed1234u);
        game.screen = eh::Screen::Playing;
        game.entities.clear();

        eh::Map &map = game.level.map;
        map.width = size;
        map.height = size;
        map.tiles.assign(static_cast<std::size_t>(size) * size, eh::Tile::Floor);

        game.player.x = eh::fx_from_float(static_cast<float>(size) * 0.5f);
        game.player.y = eh::fx_from_float(static_cast<float>(size) * 0.5f);
        game.player.angle = eh::angle_from_deg(0.0);
        return game;
    };
    const auto warm = [](uint32_t color) {
        return static_cast<int>((color >> 16) & 0xffu) > static_cast<int>(color & 0xffu);
    };

    Target far_field;
    eh::render_walls(open_field(400), far_field.framebuffer);
    const auto row = [&](int y) {
        return far_field.pixels[static_cast<std::size_t>(y) * eh::Framebuffer::W + CENTER];
    };

    SECTION("the sky is one warm block on top and the ground one cool block beneath it") {
        CHECK(warm(row(0)));
        CHECK_FALSE(warm(row(eh::Framebuffer::H - 1)));

        int crossings = 0;
        for (int y = 1; y < eh::Framebuffer::H; ++y) {
            if (warm(row(y - 1)) && !warm(row(y))) {
                ++crossings;
            }
        }
        // Exactly one, so neither half is a band stranded inside the other.
        CHECK(crossings == 1);
    }

    SECTION("the painted horizon is the row the walls are centred on") {
        int painted_horizon = -1;
        for (int y = 1; y < eh::Framebuffer::H && painted_horizon < 0; ++y) {
            if (warm(row(y - 1)) && !warm(row(y))) {
                painted_horizon = y;
            }
        }
        REQUIRE(painted_horizon > 0);

        Target near_field;
        eh::render_walls(open_field(24), near_field.framebuffer);

        int top = -1, bottom = -1;
        for (int y = 0; y < eh::Framebuffer::H; ++y) {
            const std::size_t index = static_cast<std::size_t>(y) * eh::Framebuffer::W + CENTER;
            if (far_field.pixels[index] != near_field.pixels[index]) {
                if (top < 0) {
                    top = y;
                }
                bottom = y;
            }
        }
        REQUIRE(top >= 0);
        // Non-vacuity: the nearer wall has to be a band, not one stray row, or its midpoint would
        // mean nothing. Measured 165..194 against the far field's single row.
        REQUIRE(bottom - top > 20);

        const double wall_centre = (top + bottom) / 2.0;
        CAPTURE(painted_horizon, top, bottom, wall_centre);
        CHECK(std::abs(painted_horizon - wall_centre) <= 2.0);
    }
}

// Freezing the game clock -- deleting ++gs.tick from tick()'s Playing arm -- was caught
// only by the replay trajectory digest, as an opaque hash mismatch, and it stops every
// animation the game has: the basket's pulse and the HUD's blinking objective prompt and
// low-health warning. Measured in a world where nothing else can move, the frame changed
// on 59 of 60 consecutive ticks normally and on 0 of 60 with the clock frozen.
//
// Every consumer test in the suite assigns gs.tick by hand, which is why none of them
// could see this: they pin what the clock drives, and nothing ran the clock itself. This
// test owns the producer and the seam, so it deliberately calls the real tick().
TEST_CASE("render: the animation clock advances with simulation time and reaches the screen") {
    eh::GameState state;
    eh::reset(state, 0x5eed1234u);
    state.screen = eh::Screen::Playing;

    // Crack every egg, so the only things left that can change a frame are driven by the
    // clock. Dead eggs are culled by the sprite renderer and stop chasing and attacking.
    for (eh::Entity &entity : state.entities) {
        if (entity.type == eh::EntityType::Egg) {
            entity.alive = false;
            entity.health = 0;
        }
    }
    state.eggs_remaining = 0;

    float basket_x = 0.0f;
    float basket_y = 0.0f;
    for (const eh::Entity &entity : state.entities) {
        if (entity.type == eh::EntityType::Basket) {
            basket_x = eh::fx_to_float(entity.x);
            basket_y = eh::fx_to_float(entity.y);
        }
    }
    REQUIRE(basket_x > 0.0f);

    // Stand off the basket looking straight at it, well outside BASKET_RANGE so the run
    // cannot win and leave Playing -- the clock only advances on the Playing arm.
    state.player.x = eh::fx_from_float(basket_x - 2.5f);
    state.player.y = eh::fx_from_float(basket_y);
    state.player.angle = 0;

    const eh::fx start_x = state.player.x;
    const eh::fx start_y = state.player.y;
    const uint16_t start_angle = state.player.angle;

    // Control: the basket has to be on screen, or "the frame changed" would be a claim
    // about something else entirely.
    Target lit;
    eh::render_frame(state, lit.framebuffer);
    eh::GameState hidden = state;
    for (eh::Entity &entity : hidden.entities) {
        if (entity.type == eh::EntityType::Basket) {
            entity.alive = false;
        }
    }
    Target without;
    eh::render_frame(hidden, without.framebuffer);
    int basket_pixels = 0;
    for (std::size_t i = 0; i < lit.pixels.size(); ++i) {
        if (lit.pixels[i] != without.pixels[i]) {
            ++basket_pixels;
        }
    }
    REQUIRE(basket_pixels > 5000);

    const int RUN_TICKS = 60;
    Target target;
    std::vector<uint32_t> previous;
    int changed = 0;
    for (int i = 0; i < RUN_TICKS; ++i) {
        eh::tick(state, eh::InputFrame{});
        eh::render_frame(state, target.framebuffer);
        if (!previous.empty() && target.pixels != previous) {
            ++changed;
        }
        previous = target.pixels;
    }

    // The run must not have moved the camera or ended, or the frames could differ for a
    // reason that has nothing to do with the clock.
    REQUIRE(state.screen == eh::Screen::Playing);
    REQUIRE(state.player.x == start_x);
    REQUIRE(state.player.y == start_y);
    REQUIRE(state.player.angle == start_angle);

    CAPTURE(state.tick, changed, basket_pixels);
    // Consumers divide the clock -- the HUD blinks every 8 ticks, the basket's pulse
    // steps 0.18 radians per tick -- so they are calibrated to one advance per simulation
    // tick, not merely to a value that rises.
    CHECK(state.tick == static_cast<uint32_t>(RUN_TICKS));
    CHECK(changed >= 20);
}

// Finding #61. Finding #60 established that a test which *assigns* a field can never verify
// that the game loop *produces* that value, and asked the obvious follow-up question of every
// other field a renderer reads. gs.muzzle_flash was the next one, and the answer was the same.
//
// Its write is well pinned: test_player asserts muzzle_flash == 4 on the firing tick, and
// test_sprites (finding #45) pins how the flash dims and how the recoil kicks across the whole
// countdown -- by assigning the field. Its *decay* was pinned by nothing:
//
//   decrement(gs.muzzle_flash) removed -- the weapon flashes forever    1 of 132, digest only
//   muzzle_flash decays twice per tick -- half as long                  1 of 132, digest only
//
// Both were caught only by `replay: a 600 tick input script is deterministic`, which reports
// that a hash moved and names nothing. A permanently lit muzzle flash, with the gun frozen at
// full recoil on every frame of the demo, is not a subtle regression.
//
// Recorded as a negative result, because it removed a section from this test: moving the
// decrement below fire(), so the first rendered frame carries one step less than it should,
// IS already caught by name -- `player: firing consumes ammo and respects the cooldown`
// asserts muzzle_flash == 4 on the firing tick. A draft of this test carried a third section
// pinning that same ordering in pixels. Every mutant killed it alongside another section and
// nothing killed it alone, so by the standing rule -- if two assertions never die apart they
// are one assertion -- it was redundant and is gone. The gap was the countdown, not the arm.
//
// The two sections that remain are chosen so that each has a mutant the other survives:
// releasing the trigger must not freeze the flash, and holding it must not relight the flash
// on every tick. MUZZLE_FLASH_TICKS is read from the shared header rather than written as a
// literal -- finding #16 owns that number, and this test owns the loop that spends it.
TEST_CASE("render: one trigger pull lights the weapon for the whole flash and no longer") {
    const eh::InputFrame idle{};
    eh::InputFrame fire{};
    fire.buttons = eh::InputFrame::Fire;

    eh::GameState quiet;
    eh::reset(quiet, 0x5eed1234u);
    quiet.screen = eh::Screen::Playing;
    quiet.muzzle_flash = 0;
    const std::vector<uint32_t> rest = weapon_pixels(quiet);

    eh::GameState armed = quiet;
    armed.muzzle_flash = eh::MUZZLE_FLASH_TICKS;

    // Control: an unlit weapon and a fully lit one must be materially different pictures, or
    // every "the weapon is lit" claim below is satisfied by a renderer that draws nothing.
    REQUIRE(differing_pixels(weapon_pixels(armed), rest) > 5000);

    SECTION("one shot lights the weapon for exactly the flash duration, then releases it") {
        eh::GameState gs = quiet;
        int lit = 0;
        bool ended = false;
        for (int frame = 0; frame < 12; ++frame) {
            eh::tick(gs, frame == 0 ? fire : idle);
            REQUIRE(gs.screen == eh::Screen::Playing);
            if (differing_pixels(weapon_pixels(gs), rest) > 0) {
                CAPTURE(frame);
                // Nothing fires again in this window, so a relit frame means the countdown
                // is not monotone rather than that a second shot went off.
                CHECK_FALSE(ended);
                ++lit;
            } else {
                ended = true;
            }
        }
        CAPTURE(lit);
        // The trigger is released after the first tick, so this also pins that the countdown
        // is driven by the tick and not by the button -- a flash that only decays while Fire
        // is held stays lit here forever and is invisible to the held-trigger section below.
        CHECK(lit == static_cast<int>(eh::MUZZLE_FLASH_TICKS));
    }

    SECTION("holding the trigger relights the flash per shot, not per frame") {
        eh::GameState gs = quiet;
        const int ammo_before = static_cast<int>(gs.player.ammo);

        int lit = 0;
        int dark = 0;
        for (int frame = 0; frame < 40; ++frame) {
            eh::tick(gs, fire);
            if (differing_pixels(weapon_pixels(gs), rest) > 0) {
                ++lit;
            } else {
                ++dark;
            }
        }

        const int shots = ammo_before - static_cast<int>(gs.player.ammo);
        CAPTURE(shots, lit, dark);
        // Non-vacuity from both ends: the trigger has to have produced several shots, and the
        // weapon has to have gone dark at some point, or "lit == shots * duration" is
        // satisfied by a gun that never fires or by one that never stops flashing.
        REQUIRE(shots > 1);
        CHECK(dark > 0);
        // The relationship, with no cooldown constant reproduced: every shot buys exactly one
        // flash duration of light, and pulling the trigger between shots buys nothing.
        CHECK(lit == shots * static_cast<int>(eh::MUZZLE_FLASH_TICKS));
    }
}
TEST_CASE("render: a wall one tile away fills the screen and shrinks in proportion to distance") {
    // Wall columns chosen so each face lands a whole number of half-tiles from the player at
    // x = 1.5. The reference wall is 40.5 tiles away -- a nine-pixel sliver -- so the topmost
    // row that differs from it is the near wall's top edge, measured rather than derived.
    constexpr std::array<int, 4> WALL_COLUMNS{4, 6, 8, 12};
    constexpr std::array<double, 4> DEPTHS{2.5, 4.5, 6.5, 10.5};
    constexpr double HORIZON = eh::Framebuffer::H / 2;

    Target reference;
    eh::render_walls(scale_corridor(42), reference.framebuffer);

    std::array<int, 4> tops{};
    for (std::size_t i = 0; i < WALL_COLUMNS.size(); ++i) {
        Target frame;
        eh::render_walls(scale_corridor(WALL_COLUMNS[i]), frame.framebuffer);
        tops[i] = wall_top_row(frame, reference);

        INFO("depth " << DEPTHS[i] << " top row " << tops[i]);
        REQUIRE(tops[i] > 0);
        REQUIRE(tops[i] < HORIZON);
    }

    // Non-vacuity: the top edge must really climb toward the horizon as the wall recedes, so
    // nothing below can be satisfied by an edge that never moved. Measured 108 -> 163.
    REQUIRE(tops[3] - tops[0] > 40);
    for (std::size_t i = 1; i < tops.size(); ++i) {
        CHECK(tops[i] > tops[i - 1]);
    }

    // The contract, stated absolutely and in world terms: the wall's half-height in pixels
    // times its distance in tiles is half the screen. Equivalently, a wall one tile away
    // exactly fills the frame vertically. Measured 180.00, 180.00, 175.50, 178.50 against
    // H/2 = 180.
    //
    // The top row is an integer, so one pixel of quantization costs up to `depth` in the
    // product. The tolerance is that bound plus two, not a number tuned until it passed.
    for (std::size_t i = 0; i < DEPTHS.size(); ++i) {
        const double product = (HORIZON - static_cast<double>(tops[i])) * DEPTHS[i];
        INFO("depth " << DEPTHS[i] << " top " << tops[i] << " (H/2 - top) * depth " << product);
        CHECK(std::abs(product - HORIZON) <= DEPTHS[i] + 2.0);
    }
}

TEST_CASE("render: walking up to a wall magnifies it right through the near field") {
    constexpr int CENTER = eh::Framebuffer::W / 2;
    // Player x against a wall whose face is at x = 4, so depth = 4 - x.
    constexpr std::array<float, 5> PLAYER_X{1.0f, 2.0f, 3.0f, 3.5f, 3.75f};
    constexpr std::array<double, 5> DEPTHS{3.0, 2.0, 1.0, 0.5, 0.25};

    std::array<int, 5> runs{};
    for (std::size_t i = 0; i < PLAYER_X.size(); ++i) {
        Target frame;
        eh::render_walls(near_corridor(PLAYER_X[i]), frame.framebuffer);
        runs[i] = longest_constant_run(frame, CENTER);
        INFO("depth " << DEPTHS[i] << " longest run " << runs[i]);
        REQUIRE(runs[i] > 0);
    }

    // Non-vacuity: the far sample must show fine vertical detail, so nothing below can be
    // satisfied by a frame that is one flat colour top to bottom. Measured 25 at depth 3.
    REQUIRE(runs[0] < eh::Framebuffer::H / 4);

    // The contract. Closing on a wall magnifies its texture, so each step nearer must
    // enlarge the largest constant band. Measured 25, 36, 74, 147, 180.
    //
    // This is what the near-depth clamp and the projected-height ceiling are for, and it is
    // the assertion that notices when either stops the wall growing: clamping at one tile
    // freezes the last three samples at 74, clamping at half a tile freezes the last two at
    // 147, and ceilinging the projected height at one screen freezes them all.
    for (std::size_t i = 1; i < runs.size(); ++i) {
        INFO("depth " << DEPTHS[i - 1] << " -> " << DEPTHS[i] << " run " << runs[i - 1] << " -> "
                      << runs[i]);
        CHECK(runs[i] > runs[i - 1]);
    }

    // An absolute anchor as well as the relative law, because a monotone sequence alone
    // cannot say how far the magnification actually goes. A quarter tile from the wall a
    // single texel row must span more than a third of the screen. Measured 180 of 360.
    INFO("closest sample run " << runs[4]);
    CHECK(runs[4] > eh::Framebuffer::H / 3);
}

// The billboard renderer's near field. Screen coverage is the instrument because the two
// obvious alternatives are both blind here: the sprite's on-screen height saturates at the
// full screen from 1.25 tiles inward, and the width of its changed-pixel box is clipped by
// the corridor's side walls and is not even monotone (326 px at 0.90, 322 px at 0.75).
// Measured coverage, out of 230400 pixels, is monotone until the egg fills the screen at 0.3:
//
//   depth  2.50   1.50   1.00   0.75   0.50   0.40   0.30
//   drawn 14990  41652  77438 104512 164850 208904 230400 (whole screen)
//
// The sweep therefore stops at 0.5, well clear of saturation.
TEST_CASE("render: an egg closing to attack range keeps filling more of the screen") {
    // 0.75 mirrors EGG_ATTACK_RANGE in entities.cpp. It is a fixture choice -- render the egg
    // at the distance where it starts hurting you -- not an assertion about that constant,
    // which test_entities owns. Widening the attack range does not make this test wrong.
    constexpr std::array<float, 5> DEPTHS{2.5f, 1.5f, 1.0f, 0.75f, 0.5f};
    constexpr int ATTACK_RANGE_SAMPLE = 3;
    constexpr int TOTAL_PIXELS = eh::Framebuffer::W * eh::Framebuffer::H;

    std::array<int, DEPTHS.size()> drawn{};
    for (std::size_t i = 0; i < DEPTHS.size(); ++i) {
        const eh::GameState game = egg_corridor(DEPTHS[i]);

        // render_walls ignores entities, so this is the same wall frame at every depth and
        // the difference is exactly what the billboard layer contributed.
        Target walls;
        eh::render_walls(game, walls.framebuffer);

        Target both;
        eh::render_walls(game, both.framebuffer);
        eh::render_sprites(game, both.framebuffer);

        drawn[i] = differing_pixels(both.pixels, walls.pixels);
        INFO("depth " << DEPTHS[i] << " drew " << drawn[i] << " of " << TOTAL_PIXELS);
        CHECK(drawn[i] > 0);
    }

    // An egg at contact range is on screen at all. This is what a near clamp deletes.
    INFO("at attack range the egg drew " << drawn[ATTACK_RANGE_SAMPLE] << " pixels");
    CHECK(drawn[ATTACK_RANGE_SAMPLE] > 0);

    // ...and it is close enough to be unmissable, not a handful of stray pixels. Absolute, so
    // it does not depend on the far samples the way the ordering below does.
    CHECK(drawn[ATTACK_RANGE_SAMPLE] > TOTAL_PIXELS / 4);

    // It keeps growing the whole way in. A near depth clamp or a projected-size ceiling
    // freezes this sequence; nothing else in the suite ever reaches the range where they bind.
    for (std::size_t i = 1; i < DEPTHS.size(); ++i) {
        INFO("depth " << DEPTHS[i] << " (" << drawn[i] << ") must beat depth " << DEPTHS[i - 1]
                      << " (" << drawn[i - 1] << ")");
        CHECK(drawn[i] > drawn[i - 1]);
    }

    // Non-vacuity: the far end of the sweep must be genuinely small, so a renderer that filled
    // the screen at every depth could not satisfy the ordering above by standing still.
    CHECK(drawn[0] < TOTAL_PIXELS / 8);

    // The ordering above pins a sequence, not a scale. A ceiling on projected size compresses
    // the whole curve while leaving it monotone -- capping width at one screen turned the last
    // two samples into 127556 and 129208, still increasing, still passing. Only a magnitude
    // assertion sees it, so this one is absolute: an egg pressed against you blocks the view.
    constexpr float CONTACT_DEPTH = 0.3f;
    const eh::GameState contact = egg_corridor(CONTACT_DEPTH);

    Target contact_walls;
    eh::render_walls(contact, contact_walls.framebuffer);

    Target contact_both;
    eh::render_walls(contact, contact_both.framebuffer);
    eh::render_sprites(contact, contact_both.framebuffer);

    const int contact_drawn = differing_pixels(contact_both.pixels, contact_walls.pixels);
    INFO("at " << CONTACT_DEPTH << " tiles the egg drew " << contact_drawn << " of " << TOTAL_PIXELS
               << " (shipped renderer covers every pixel; a one-screen width"
               << " ceiling covers 130776)");
    CHECK(contact_drawn > TOTAL_PIXELS * 9 / 10);
}
