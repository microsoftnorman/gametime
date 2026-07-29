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