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
